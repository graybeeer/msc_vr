#include "AgvLidarLocalizerComponent.h"
#include "AgvDriveComponent.h"
#include "AgvLidarComponent.h"
#include "AgvPath.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogAgvLocalization, Log, All);

bool UAgvLidarLocalizerComponent::ResolveParts()
{
	if (!Drive)
	{
		Drive = Cast<UAgvTricycleDriveComponent>(GetOwner()->FindComponentByClass<UAgvDriveComponent>());
		GetOwner()->GetComponents<UAgvLidarComponent>(Lidars);
		Lidars.RemoveAll([](const UAgvLidarComponent* Lidar) { return !Lidar->bUseForLocalization; });
		SeenRevolutions.Init(0, Lidars.Num());
	}
	return Drive != nullptr;
}

void UAgvLidarLocalizerComponent::InitializePose()
{
	if (!ResolveParts())
	{
		return;
	}
	Drive->GetTruePose(OdomPosition, OdomYaw);
	CorrectionOffset = FVector2D::ZeroVector;
	CorrectionYaw = 0.0;
	LastWheelTravelCm = Drive->DriveWheelTravelCm;
	// Drop the partial scan; it was taken somewhere else.
	TArray<FVector> Discard;
	for (int32 Index = 0; Index < Lidars.Num(); ++Index)
	{
		Lidars[Index]->TakePoints(Discard);
		SeenRevolutions[Index] = Lidars[Index]->GetRevolutionCount();
	}
	ScanPoints.Reset();
	bPartialScan = true;
	TravelSinceMatchCm = 0.f;
	LastCorrectionCm = 1000.f;
	LastCorrectionDeg = 180.f;
	AcceptedMatches = RejectedMatches = RecoveredMatches = 0;
	bLost = false;
	bInitialized = true;
	StatusText = bUseLidar ? TEXT("LIDAR") : TEXT("ODOMETRY ONLY");
}

bool UAgvLidarLocalizerComponent::IsMappedStructure(const FVector2D& WorldPointCm, double ToleranceCm) const
{
	return Map && Map->Width > 0 && Map->Sample(WorldPointCm) <= ToleranceCm;
}

void UAgvLidarLocalizerComponent::OffsetEstimate(FVector2D OffsetCm, float YawDeg)
{
	FVector2D Position;
	double Yaw;
	ToMap(OdomPosition, OdomYaw, Position, Yaw);
	CorrectionYaw += FMath::DegreesToRadians((double)YawDeg);
	CorrectionOffset = Position + OffsetCm - AgvMath::Rotate(CorrectionYaw, OdomPosition);
	LastCorrectionCm = 1000.f;
	LastCorrectionDeg = 180.f;
}

void UAgvLidarLocalizerComponent::ToMap(const FVector2D& Position, double Yaw, FVector2D& OutPosition, double& OutYaw) const
{
	OutPosition = AgvMath::Rotate(CorrectionYaw, Position) + CorrectionOffset;
	OutYaw = Yaw + CorrectionYaw;
}

bool UAgvLidarLocalizerComponent::GetPose(FVector2D& OutPosition, double& OutYaw) const
{
	if (bIdealPose || !bInitialized)
	{
		// Before the first step the vehicle stands where it was placed, which is what initialization would report.
		return Super::GetPose(OutPosition, OutYaw);
	}
	if (!bAvailable || bLost)
	{
		return false;
	}
	ToMap(OdomPosition, OdomYaw, OutPosition, OutYaw);
	OutPosition += PositionBiasCm;
	OutYaw += FMath::DegreesToRadians((double)YawBiasDeg);
	return true;
}

void UAgvLidarLocalizerComponent::Step(float Dt)
{
	if (!ResolveParts())
	{
		return;
	}
	if (!bInitialized)
	{
		InitializePose();
	}
	if (!bNoiseSeeded)
	{
		Noise.Initialize(NoiseSeed);
		bNoiseSeeded = true;
	}

	// Odometry: travel from the drive wheel encoder (counts the rim, so wheel spin is counted as travel) with the steering
	// encoder for its direction, heading from the gyro (or, without one, the steering geometry). The steering geometry
	// assumes the wheels roll without side slip; on the Chaos body a curve at speed turned the vehicle 2.9 deg off that
	// within 0.4 s and the next scan match locked onto the wrong wall (2026-10-12).
	const double Travel = Drive->DriveWheelTravelCm - LastWheelTravelCm;
	LastWheelTravelCm = Drive->DriveWheelTravelCm;
	const double Gauss = FMath::Sqrt(-2.0 * FMath::Loge(FMath::Max(Noise.FRand(), 1e-12f))) * FMath::Cos(2.0 * UE_DOUBLE_PI * Noise.FRand());
	const double Measured = Travel * (1.0 + WheelRadiusErrorPercent / 100.0) * (1.0 + Gauss * EncoderNoisePercent / 100.0);
	const double Steer = FMath::DegreesToRadians((double)Drive->SteerAngleDeg + SteerOffsetDeg);
	double TurnRad = -Measured * FMath::Sin(Steer) / Drive->WheelbaseCm();
	if (bUseGyro)
	{
		const double Rate = FMath::DegreesToRadians((double)Drive->YawRateDegS + GyroBiasDegS);
		const double GyroGauss = FMath::Sqrt(-2.0 * FMath::Loge(FMath::Max(Noise.FRand(), 1e-12f))) * FMath::Cos(2.0 * UE_DOUBLE_PI * Noise.FRand());
		TurnRad = Rate * Dt + FMath::DegreesToRadians((double)GyroNoiseDegSqrtS) * FMath::Sqrt((double)Dt) * GyroGauss;
		if (FMath::Abs(Travel) < 0.01 && FMath::Abs(TurnRad) < FMath::DegreesToRadians((double)StandstillGyroDegS) * Dt)
		{
			TurnRad = 0.0; // standing still: hold the heading (zero-velocity update)
		}
	}
	OdomPosition += AgvMath::Dir(OdomYaw + TurnRad * 0.5) * (Measured * FMath::Cos(Steer));
	OdomYaw += TurnRad;
	TravelSinceMatchCm += (float)FMath::Abs(Measured);

	// Collect this step's LiDAR points at this step's odometry pose (undoes the motion during a revolution).
	const AActor* Owner = GetOwner();
	const FVector2D VehicleOrigin = OdomPosition - AgvMath::Dir(OdomYaw) * Drive->ReferenceOffsetCm;
	bool bRevolution = false;
	TArray<FVector> Local;
	for (int32 Index = 0; Index < Lidars.Num(); ++Index)
	{
		const FTransform Mount = Lidars[Index]->GetComponentTransform().GetRelativeTransform(Owner->GetActorTransform());
		Local.Reset();
		Lidars[Index]->TakePoints(Local);
		for (const FVector& Point : Local)
		{
			const FVector Vehicle = Mount.TransformPosition(Point);
			if (Vehicle.Z < MapMinZCm || Vehicle.Z > MapMaxZCm || SelfFilter.IsInside(FVector2D(Vehicle)))
			{
				continue;
			}
			ScanPoints.Add(VehicleOrigin + AgvMath::Rotate(OdomYaw, FVector2D(Vehicle)));
		}
		if (Lidars[Index]->GetRevolutionCount() != SeenRevolutions[Index])
		{
			SeenRevolutions[Index] = Lidars[Index]->GetRevolutionCount();
			bRevolution = true;
		}
	}
	if (bRevolution)
	{
		// A scan cut short by a re-initialisation covers only part of the surroundings and biases the match (5.5 cm on
		// the test tour): wait for the first whole revolution.
		if (bUseLidar && !bPartialScan)
		{
			MatchScan();
		}
		bPartialScan = false;
		ScanPoints.Reset();
	}

	if (!bUseLidar)
	{
		StatusText = TEXT("ODOMETRY ONLY");
	}
	else if (TravelSinceMatchCm > LostAfterTravelCm)
	{
		if (!bLost)
		{
			UE_LOG(LogAgvLocalization, Warning, TEXT("AGV_LOC %s: lost after %.0f cm without a LiDAR match"), *Owner->GetName(), TravelSinceMatchCm);
		}
		bLost = true;
		StatusText = TEXT("LOST");
	}
	else
	{
		StatusText = TravelSinceMatchCm > 200.f ? TEXT("ODOMETRY ONLY") : TEXT("LIDAR");
	}
}

void UAgvLidarLocalizerComponent::BuildMap()
{
	const double Start = FPlatformTime::Seconds();
	Map = MakeShared<FAgvDistanceMap>();
	Map->CellCm = MapCellCm;
	Map->CapCm = MatchDistanceCapCm;

	// Permanent structure only: static, blocking geometry in the height band (racks, columns, walls). Cargo, pallets,
	// people and vehicles are movable and change all the time, so they are never landmarks; their points are rejected
	// by the match (robust weights below). The floor sits below the band.
	const double Floor = GetOwner()->GetActorLocation().Z;
	const double Low = Floor + MapMinZCm, High = Floor + MapMaxZCm;
	TArray<UPrimitiveComponent*> Obstacles;
	FBox2D Extent(ForceInit);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->FindComponentByClass<UAgvDriveComponent>())
		{
			continue;
		}
		TArray<UPrimitiveComponent*> Components;
		It->GetComponents<UPrimitiveComponent>(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			const FBox Bounds = Component->Bounds.GetBox();
			if (Component->Mobility == EComponentMobility::Static && Component->IsQueryCollisionEnabled()
				&& Component->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block && Bounds.Max.Z > Low && Bounds.Min.Z < High)
			{
				Obstacles.Add(Component);
				Extent += FVector2D(Bounds.Min);
				Extent += FVector2D(Bounds.Max);
			}
		}
	}
	if (!Extent.bIsValid)
	{
		Map->Width = Map->Height = 0;
		MapOccupiedCells = 0;
		return;
	}
	const double Cell = Map->CellCm;
	Map->Origin = Extent.Min - FVector2D(Map->CapCm * 2.0);
	Map->Width = FMath::CeilToInt32((Extent.Max.X - Map->Origin.X + Map->CapCm * 2.0) / Cell);
	Map->Height = FMath::CeilToInt32((Extent.Max.Y - Map->Origin.Y + Map->CapCm * 2.0) / Cell);
	const int32 Count = Map->Width * Map->Height;

	// Occupancy: a cell is occupied when a cell-sized column over the band overlaps the obstacle.
	TArray<bool> Occupied;
	Occupied.Init(false, Count);
	// Slightly inside the cell so an obstacle that only touches a cell's edge does not fill it.
	const FCollisionShape Column = FCollisionShape::MakeBox(FVector(Cell * 0.5 - 0.05, Cell * 0.5 - 0.05, (High - Low) * 0.5));
	for (const UPrimitiveComponent* Component : Obstacles)
	{
		const FBox Bounds = Component->Bounds.GetBox();
		const int32 X0 = FMath::Max(0, FMath::FloorToInt32((Bounds.Min.X - Map->Origin.X) / Cell));
		const int32 X1 = FMath::Min(Map->Width - 1, FMath::FloorToInt32((Bounds.Max.X - Map->Origin.X) / Cell));
		const int32 Y0 = FMath::Max(0, FMath::FloorToInt32((Bounds.Min.Y - Map->Origin.Y) / Cell));
		const int32 Y1 = FMath::Min(Map->Height - 1, FMath::FloorToInt32((Bounds.Max.Y - Map->Origin.Y) / Cell));
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				bool& Cell_ = Occupied[Y * Map->Width + X];
				if (!Cell_)
				{
					const FVector Centre(Map->Origin.X + (X + 0.5) * Cell, Map->Origin.Y + (Y + 0.5) * Cell, (Low + High) * 0.5);
					Cell_ = Component->OverlapComponent(Centre, FQuat::Identity, Column);
				}
			}
		}
	}

	Map->BuildFromOccupancy(Occupied);
	MapOccupiedCells = Map->OccupiedCells;
	MapBuildMilliseconds = (float)((FPlatformTime::Seconds() - Start) * 1000.0);
	UE_LOG(LogAgvLocalization, Log, TEXT("AGV_LOC map %d x %d cells of %.0f cm, %d occupied, %.0f ms"), Map->Width, Map->Height, Cell, MapOccupiedCells, MapBuildMilliseconds);
}

void UAgvLidarLocalizerComponent::MatchScan()
{
	const double Start = FPlatformTime::Seconds();
	if (!Map)
	{
		BuildMap();
	}

	TArray<FVector2D> Points;
	AgvScanMatch::Voxelize(ScanPoints, MatchVoxelCm, Points);
	LastMatchPoints = Points.Num();
	if (Points.Num() < MinMatchPoints || Map->Width == 0)
	{
		++RejectedMatches;
		LastInlierRatio = 0.f;
		return;
	}

	// Gauss-Newton on the distance field: find the correction that puts the scan onto mapped surfaces (AgvScanMatching).
	AgvScanMatch::FParams Params;
	Params.VoxelCm = MatchVoxelCm;
	Params.InlierDistanceCm = InlierDistanceCm;
	Params.RobustScaleCm = RobustScaleCm;
	Params.Iterations = MatchIterations;
	auto Refine = [&](double& Tx, double& Ty, double& Th) { return AgvScanMatch::Refine(*Map, Points, Params, Tx, Ty, Th); };
	double Tx = CorrectionOffset.X, Ty = CorrectionOffset.Y, Th = CorrectionYaw;
	double Ratio = Refine(Tx, Ty, Th);
	if (Ratio < MinInlierRatio)
	{
		// Recovery: the heading is the usual culprit (wheel spin while turning on the spot fools the odometry's yaw, and a
		// few degrees put distant walls far outside the robust window). Re-run the match from headings around the
		// current estimate, keeping its position, and take the best fit. Only on a failed match, so it costs nothing
		// in normal running.
		FVector2D Position;
		double Yaw;
		ToMap(OdomPosition, OdomYaw, Position, Yaw);
		for (const double OffsetDeg : { 2.0, -2.0, 4.0, -4.0, 7.0, -7.0, 10.0, -10.0, 15.0, -15.0 })
		{
			double SeedTh = CorrectionYaw + FMath::DegreesToRadians(OffsetDeg);
			FVector2D Seed = Position - AgvMath::Rotate(SeedTh, OdomPosition);
			const double SeedRatio = Refine(Seed.X, Seed.Y, SeedTh);
			if (SeedRatio > Ratio)
			{
				Ratio = SeedRatio;
				Tx = Seed.X;
				Ty = Seed.Y;
				Th = SeedTh;
			}
		}
		RecoveredMatches += Ratio >= MinInlierRatio;
	}
	LastInlierRatio = (float)Ratio;
	if (LastInlierRatio >= MinInlierRatio)
	{
		FVector2D Before, After;
		double BeforeYaw, AfterYaw;
		ToMap(OdomPosition, OdomYaw, Before, BeforeYaw);
		CorrectionOffset += (FVector2D(Tx, Ty) - CorrectionOffset) * CorrectionGain;
		CorrectionYaw += AgvMath::Wrap(Th - CorrectionYaw) * CorrectionGain;
		ToMap(OdomPosition, OdomYaw, After, AfterYaw);
		LastCorrectionCm = (float)FVector2D::Distance(Before, After);
		LastCorrectionDeg = (float)FMath::RadiansToDegrees(FMath::Abs(AgvMath::Wrap(AfterYaw - BeforeYaw)));
		TravelSinceMatchCm = 0.f;
		++AcceptedMatches;
	}
	else
	{
		++RejectedMatches;
	}
	LastMatchMilliseconds = (float)((FPlatformTime::Seconds() - Start) * 1000.0);
}
