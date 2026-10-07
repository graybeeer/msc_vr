#include "AgvLidarLocalizerComponent.h"
#include "AgvDriveComponent.h"
#include "AgvLidarComponent.h"
#include "AgvPath.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogAgvLocalization, Log, All);

namespace
{
	/** Exact 1D squared distance transform (Felzenszwalb & Huttenlocher) of F into D, in cell units. */
	void DistanceTransform1D(const TArray<double>& F, TArray<double>& D)
	{
		const int32 N = F.Num();
		TArray<int32> V;
		TArray<double> Z;
		V.SetNum(N);
		Z.SetNum(N + 1);
		auto Intersect = [&F](int32 Q, int32 P) { return ((F[Q] + double(Q) * Q) - (F[P] + double(P) * P)) / (2.0 * Q - 2.0 * P); };
		int32 K = 0;
		V[0] = 0;
		Z[0] = -TNumericLimits<double>::Max();
		Z[1] = TNumericLimits<double>::Max();
		for (int32 Q = 1; Q < N; ++Q)
		{
			double S = Intersect(Q, V[K]);
			while (S <= Z[K])
			{
				--K;
				S = Intersect(Q, V[K]);
			}
			++K;
			V[K] = Q;
			Z[K] = S;
			Z[K + 1] = TNumericLimits<double>::Max();
		}
		D.SetNum(N);
		K = 0;
		for (int32 Q = 0; Q < N; ++Q)
		{
			while (Z[K + 1] < Q)
			{
				++K;
			}
			D[Q] = double(Q - V[K]) * (Q - V[K]) + F[V[K]];
		}
	}
}

double FAgvDistanceMap::Sample(const FVector2D& Point) const
{
	// Cell centres sit at half-cell offsets.
	const double GX = (Point.X - Origin.X) / CellCm - 0.5;
	const double GY = (Point.Y - Origin.Y) / CellCm - 0.5;
	const int32 X0 = FMath::FloorToInt32(GX);
	const int32 Y0 = FMath::FloorToInt32(GY);
	if (X0 < 0 || Y0 < 0 || X0 + 1 >= Width || Y0 + 1 >= Height)
	{
		return CapCm;
	}
	const double FX = GX - X0, FY = GY - Y0;
	const float* Row0 = &Distance[Y0 * Width + X0];
	const float* Row1 = Row0 + Width;
	return FMath::Lerp(FMath::Lerp((double)Row0[0], (double)Row0[1], FX), FMath::Lerp((double)Row1[0], (double)Row1[1], FX), FY);
}

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

	// Tricycle odometry from the drive wheel encoder (counts the rim, so wheel spin is counted as travel) and the
	// steering encoder, with the controller's nominal geometry.
	const double Travel = Drive->DriveWheelTravelCm - LastWheelTravelCm;
	LastWheelTravelCm = Drive->DriveWheelTravelCm;
	const double Gauss = FMath::Sqrt(-2.0 * FMath::Loge(FMath::Max(Noise.FRand(), 1e-12f))) * FMath::Cos(2.0 * UE_DOUBLE_PI * Noise.FRand());
	const double Measured = Travel * (1.0 + WheelRadiusErrorPercent / 100.0) * (1.0 + Gauss * EncoderNoisePercent / 100.0);
	const double Steer = FMath::DegreesToRadians((double)Drive->SteerAngleDeg + SteerOffsetDeg);
	const double TurnRad = -Measured * FMath::Sin(Steer) / Drive->WheelbaseCm();
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

	// Exact Euclidean distance transform: columns then rows.
	const double Far = 1e12;
	TArray<double> Squared;
	Squared.SetNum(Count);
	TArray<double> F, D;
	F.SetNum(Map->Height);
	for (int32 X = 0; X < Map->Width; ++X)
	{
		for (int32 Y = 0; Y < Map->Height; ++Y)
		{
			F[Y] = Occupied[Y * Map->Width + X] ? 0.0 : Far;
		}
		DistanceTransform1D(F, D);
		for (int32 Y = 0; Y < Map->Height; ++Y)
		{
			Squared[Y * Map->Width + X] = D[Y];
		}
	}
	F.SetNum(Map->Width);
	Map->Distance.SetNum(Count);
	for (int32 Y = 0; Y < Map->Height; ++Y)
	{
		for (int32 X = 0; X < Map->Width; ++X)
		{
			F[X] = Squared[Y * Map->Width + X];
		}
		DistanceTransform1D(F, D);
		for (int32 X = 0; X < Map->Width; ++X)
		{
			// Measured to the occupied cell's edge rather than its centre, so a point on a surface reads ~0.
			Map->Distance[Y * Map->Width + X] = (float)FMath::Clamp(FMath::Sqrt(D[X]) * Cell - Cell * 0.5, 0.0, Map->CapCm);
		}
	}
	Map->OccupiedCells = Occupied.FilterByPredicate([](bool B) { return B; }).Num();
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

	// One point per voxel so dense nearby surfaces do not outweigh far ones.
	TMap<FIntPoint, FVector2D> Voxels;
	for (const FVector2D& Point : ScanPoints)
	{
		Voxels.FindOrAdd(FIntPoint(FMath::FloorToInt32(Point.X / MatchVoxelCm), FMath::FloorToInt32(Point.Y / MatchVoxelCm)), Point);
	}
	TArray<FVector2D> Points;
	Voxels.GenerateValueArray(Points);
	LastMatchPoints = Points.Num();
	if (Points.Num() < MinMatchPoints || Map->Width == 0)
	{
		++RejectedMatches;
		LastInlierRatio = 0.f;
		return;
	}

	// Gauss-Newton on the distance field: find the correction that puts the scan onto mapped surfaces.
	// Robust (redescending) weights: a point counts fully when it lies on a mapped surface and not at all once it is
	// RobustScaleCm or more off it, so unmapped things near the structure (cargo on a rack shelf, a person beside a
	// column, another vehicle) do not drag the estimate towards the nearest mapped surface. A plain capped/Huber
	// weight let every such point pull with a constant force: cargo on the racks biased the estimate by ~10 cm.
	// The scale starts wide (the cap: large errors, e.g. a wrong start pose or wheel spin on a slippery floor, still
	// converge) and halves each iteration down to RobustScaleCm (slower shrinking lets points near the structure, cargo
	// on a shelf, pull for longer: 13 cm error in the warehouse tour at 0.7).
	const double Step = Map->CellCm;
	auto Refine = [&](double& Tx, double& Ty, double& Th)
	{
		double Scale = Map->CapCm;
		for (int32 Iteration = 0; Iteration < MatchIterations; ++Iteration, Scale = FMath::Max((double)RobustScaleCm, Scale * 0.5))
		{
			double H[3][3] = {}, G[3] = {};
			const double C = FMath::Cos(Th), S = FMath::Sin(Th);
			for (const FVector2D& P : Points)
			{
				const FVector2D Q(C * P.X - S * P.Y + Tx, S * P.X + C * P.Y + Ty);
				const double R = Map->Sample(Q);
				if (R >= Scale)
				{
					continue;
				}
				const double GX = (Map->Sample(Q + FVector2D(Step, 0)) - Map->Sample(Q - FVector2D(Step, 0))) / (2.0 * Step);
				const double GY = (Map->Sample(Q + FVector2D(0, Step)) - Map->Sample(Q - FVector2D(0, Step))) / (2.0 * Step);
				const double J[3] = { GX, GY, GX * (-S * P.X - C * P.Y) + GY * (C * P.X - S * P.Y) };
				const double W = FMath::Square(1.0 - FMath::Square(R / Scale)); // Tukey biweight
				for (int32 I = 0; I < 3; ++I)
				{
					G[I] += W * J[I] * R;
					for (int32 K = 0; K < 3; ++K)
					{
						H[I][K] += W * J[I] * J[K];
					}
				}
			}
			// Light damping keeps a poorly constrained direction (a long featureless wall) from running away.
			for (int32 I = 0; I < 3; ++I)
			{
				H[I][I] += 1e-3 * (1.0 + H[I][I]);
				G[I] = -G[I];
			}
			double Delta[3];
			if (!AgvMath::Solve3(H, G, Delta))
			{
				break;
			}
			Tx += Delta[0];
			Ty += Delta[1];
			Th += Delta[2];
			if (Scale <= RobustScaleCm && FMath::Abs(Delta[0]) + FMath::Abs(Delta[1]) < 0.01 && FMath::Abs(Delta[2]) < 1e-5)
			{
				break;
			}
		}
		int32 Inliers = 0;
		const double C = FMath::Cos(Th), S = FMath::Sin(Th);
		for (const FVector2D& P : Points)
		{
			Inliers += Map->Sample(FVector2D(C * P.X - S * P.Y + Tx, S * P.X + C * P.Y + Ty)) <= InlierDistanceCm;
		}
		return (double)Inliers / Points.Num();
	};
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
