#include "AgvForkliftLocalizer.h"
#include "AgvLidarComponent.h"
#include "AgvPath.h"
#include "AgvShadowSensors.h"
#include "WarehouseForklift.h"
#include "WarehouseAutonomyTypes.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Hash/CityHash.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"

DEFINE_LOG_CATEGORY_STATIC(LogAgvForkliftLoc, Log, All);

namespace AgvForkliftLoc
{
	// Error histogram for the 95th percentile: 0.5 cm bins up to 200 cm, the last bin holds the rest.
	constexpr double BinCm = 0.5;
	constexpr int32 Bins = 401;

	// Usage in the console during play: agv.loc (start shadow localization; again = report), agv.loc reset
	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("agv.loc"),
		TEXT("agv.loc: LiDAR localization in shadow mode on every main warehouse forklift (measures the estimate against "
			"the exact pose; travel still uses the exact pose). Again = log the results. agv.loc reset = clear the statistics."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UAgvLocalizationSubsystem* Subsystem = World ? World->GetSubsystem<UAgvLocalizationSubsystem>() : nullptr;
			if (!Subsystem)
			{
				return;
			}
			if (Args.Contains(TEXT("reset")))
			{
				for (TActorIterator<AWarehouseForklift> It(World); It; ++It)
				{
					if (UAgvForkliftLocalizerComponent* Localizer = It->FindComponentByClass<UAgvForkliftLocalizerComponent>())
					{
						Localizer->ResetStatistics();
					}
				}
				return;
			}
			if (!Subsystem->IsEnabled())
			{
				Subsystem->Enable();
				return;
			}
			UE_LOG(LogAgvForkliftLoc, Log, TEXT("%s"), *Subsystem->Report());
		}));
}

// --- Localizer ----------------------------------------------------------------------------------------------------

UAgvForkliftLocalizerComponent::UAgvForkliftLocalizerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UAgvForkliftLocalizerComponent::BeginPlay()
{
	Super::BeginPlay();
	// After the vehicle's own tick, which takes the one-second snapshot at this frame's pose.
	if (AActor* Owner = GetOwner())
	{
		AddTickPrerequisiteActor(Owner);
	}
	Noise.Initialize(NoiseSeed);
	ErrorHistogram.Init(0, AgvForkliftLoc::Bins);
}

void UAgvForkliftLocalizerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bInitialized)
	{
		UE_LOG(LogAgvForkliftLoc, Log, TEXT("AGV_LOC_SUMMARY %s"), *Summary());
	}
	Super::EndPlay(EndPlayReason);
}

bool UAgvForkliftLocalizerComponent::ResolveParts()
{
	if (!Forklift)
	{
		Forklift = Cast<AWarehouseForklift>(GetOwner());
		if (!Forklift)
		{
			return false;
		}
	}
	if (!Body || !DriveWheel)
	{
		// The vehicle's parts are found by name: the localizer reads them like sensors and never drives them.
		TArray<UPrimitiveComponent*> Parts;
		Forklift->GetComponents(Parts);
		UPrimitiveComponent* LoadLeft = nullptr;
		UPrimitiveComponent* LoadRight = nullptr;
		UPrimitiveComponent* FoundBody = nullptr;
		UPrimitiveComponent* FoundWheel = nullptr;
		for (UPrimitiveComponent* Part : Parts)
		{
			const FString Name = Part->GetName();
			if (Name == TEXT("Body")) FoundBody = Part;
			else if (Name == TEXT("DriveWheel")) FoundWheel = Part;
			else if (Name == TEXT("LoadWheelL")) LoadLeft = Part;
			else if (Name == TEXT("LoadWheelR")) LoadRight = Part;
		}
		if (!FoundBody || !FoundWheel || !LoadLeft || !LoadRight)
		{
			return false;
		}
		Body = FoundBody;
		DriveWheel = FoundWheel;
		const FTransform Actor = Forklift->GetActorTransform();
		ReferenceOffset = FVector2D((Actor.InverseTransformPosition(LoadLeft->GetComponentLocation()) + Actor.InverseTransformPosition(LoadRight->GetComponentLocation())) * 0.5);
		// A steered wheel's world box keeps its radius as half height.
		WheelRadiusCm = FMath::Max(1.0, (double)DriveWheel->Bounds.BoxExtent.Z);
		// The vehicle with a pallet on its forks: forward by about a pallet's overhang, sideways by its extra width.
		const FBox Local = Forklift->CalculateComponentsBoundingBoxInLocalSpace(false);
		SelfFilter = FBox2D(FVector2D(Local.Min.X - 10.0, Local.Min.Y - 20.0), FVector2D(Local.Max.X + 40.0, Local.Max.Y + 20.0));
		UE_LOG(LogAgvForkliftLoc, Log, TEXT("AGV_LOC_PARTS %s reference=%s wheel radius=%.1f self=%s"), *Forklift->GetName(),
			*ReferenceOffset.ToString(), WheelRadiusCm, *SelfFilter.ToString());
	}
	if (!SensorKit)
	{
		SensorKit = Forklift->FindComponentByClass<UAgvShadowSensorsComponent>();
	}
	return Body->IsSimulatingPhysics();
}

void UAgvForkliftLocalizerComponent::InitializePose()
{
	if (!Forklift)
	{
		return;
	}
	const FTransform Actor = Forklift->GetActorTransform();
	OdomYaw = FMath::DegreesToRadians(Actor.Rotator().Yaw);
	OdomPosition = FVector2D(Actor.GetLocation()) + AgvMath::Rotate(OdomYaw, ReferenceOffset);
	CorrectionOffset = FVector2D::ZeroVector;
	CorrectionYaw = 0.0;
	TravelSinceMatchCm = 0.f;
	LastTrueLocation = Actor.GetLocation();
	// A snapshot already taken belongs to the old pose.
	SeenSnapshot = SensorKit ? SensorKit->SnapshotCount : -1;
	bLost = false;
	bInitialized = true;
	StatusText = TEXT("INITIALIZED");
}

void UAgvForkliftLocalizerComponent::ToMap(const FVector2D& Position, double Yaw, FVector2D& OutPosition, double& OutYaw) const
{
	OutPosition = AgvMath::Rotate(CorrectionYaw, Position) + CorrectionOffset;
	OutYaw = Yaw + CorrectionYaw;
}

bool UAgvForkliftLocalizerComponent::GetPose(FVector2D& OutPosition, double& OutYaw) const
{
	if (!bInitialized || !bAvailable || bLost)
	{
		return false;
	}
	ToMap(OdomPosition, OdomYaw, OutPosition, OutYaw);
	OutPosition += PositionBiasCm;
	OutYaw += FMath::DegreesToRadians((double)YawBiasDeg);
	return true;
}

bool UAgvForkliftLocalizerComponent::GetEstimatedActorTransform(FTransform& Out) const
{
	FVector2D Reference;
	double Yaw;
	if (!Forklift || !GetPose(Reference, Yaw))
	{
		return false;
	}
	const FTransform Actor = Forklift->GetActorTransform();
	const FVector2D Origin = Reference - AgvMath::Rotate(Yaw, ReferenceOffset);
	FRotator Rotation = Actor.Rotator();
	Rotation.Yaw = FMath::RadiansToDegrees(Yaw);
	Out = FTransform(Rotation, FVector(Origin, Actor.GetLocation().Z), Actor.GetScale3D());
	return true;
}

bool UAgvForkliftLocalizerComponent::IsMappedStructure(const FVector2D& WorldPointCm, double ToleranceCm) const
{
	return Map && Map->Width > 0 && Map->Sample(WorldPointCm) <= ToleranceCm;
}

double UAgvForkliftLocalizerComponent::Gauss()
{
	return FMath::Sqrt(-2.0 * FMath::Loge(FMath::Max(Noise.FRand(), 1e-12f))) * FMath::Cos(2.0 * UE_DOUBLE_PI * Noise.FRand());
}

void UAgvForkliftLocalizerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!ResolveParts())
	{
		return;
	}
	if (!bInitialized)
	{
		InitializePose();
		LastDt = DeltaTime;
		return;
	}
	const FVector TrueLocation = Forklift->GetActorLocation();
	// On the elevator the floor moves the vehicle, not its wheels: the operator's floor handoff re-initialises the
	// estimate afterwards, as it does when the layout editor puts the vehicle somewhere else.
	if (Forklift->AIState == EWarehouseAIState::ElevatorRide)
	{
		bRiding = true;
	}
	else
	{
		const double Expected = Body->GetPhysicsLinearVelocity().Size() * FMath::Max(LastDt, DeltaTime);
		if (bRiding || FVector::Dist(TrueLocation, LastTrueLocation) > Expected + 30.0)
		{
			UE_LOG(LogAgvForkliftLoc, Log, TEXT("AGV_LOC_REINIT %s %s"), *Forklift->GetName(), bRiding ? TEXT("after elevator") : TEXT("relocated"));
			bRiding = false;
			++Reinitializations;
			InitializePose();
		}
		else if (LastDt > 0.f)
		{
			// The state read now is the result of the physics step that ran for the previous frame's time.
			StepOdometry(LastDt);
			CollectAndMatchScan();
		}
		if (TravelSinceMatchCm > LostAfterTravelCm && !bLost)
		{
			UE_LOG(LogAgvForkliftLoc, Warning, TEXT("AGV_LOC_LOST %s after %.0f cm without a LiDAR match"), *Forklift->GetName(), TravelSinceMatchCm);
			bLost = true;
			++LostEvents;
		}
		StatusText = bLost ? TEXT("LOST") : TravelSinceMatchCm > 200.f ? TEXT("ODOMETRY ONLY") : TEXT("LIDAR");
		Measure();
		if (bMatchToLog)
		{
			bMatchToLog = false;
			UE_LOG(LogAgvForkliftLoc, Log, TEXT("AGV_LOC_SHADOW %s t=%.1f err=%.1fcm yaw=%.2fdeg ratio=%.2f pts=%d corr=%.1fcm ms=%.1f %s"),
				*Forklift->GetName(), GetWorld()->GetTimeSeconds(), ErrorCm, HeadingErrorDeg, LastInlierRatio, LastMatchPoints,
				LastCorrectionCm, LastMatchMilliseconds, *StatusText);
		}
	}
	LastTrueLocation = TrueLocation;
	LastDt = DeltaTime;
}

void UAgvForkliftLocalizerComponent::StepOdometry(float Dt)
{
	const FTransform Actor = Forklift->GetActorTransform();
	const FVector Up = Actor.GetUnitAxis(EAxis::Z);
	const FVector Forward = Actor.GetUnitAxis(EAxis::X);
	const FVector Right = Actor.GetUnitAxis(EAxis::Y);

	// Drive wheel encoder: the rim's rotation relative to the chassis (a spinning wheel counts as travel). A rolling
	// wheel's centre moves along Axle x Up at Spin * radius.
	const FVector Axle = DriveWheel->GetRightVector();
	const double Spin = FVector::DotProduct(DriveWheel->GetPhysicsAngularVelocityInRadians() - Body->GetPhysicsAngularVelocityInRadians(), Axle);
	const FVector Roll = FVector::CrossProduct(Axle, Up).GetSafeNormal();
	// Steering encoder: the rolling direction in the vehicle frame (0 = straight ahead, 90 deg = turning on the spot).
	const double Steer = FMath::Atan2(FVector::DotProduct(Roll, Right), FVector::DotProduct(Roll, Forward)) + FMath::DegreesToRadians((double)SteerOffsetDeg);
	const double Rim = Spin * WheelRadiusCm * Dt * (1.0 + WheelRadiusErrorPercent / 100.0) * (1.0 + Gauss() * EncoderNoisePercent / 100.0);
	// The fixed load-wheel axle moves straight ahead only: the drive wheel's forward share is its travel.
	const double Measured = Rim * FMath::Cos(Steer);

	// Yaw-rate gyro with bias and angle random walk; held at standstill (zero-velocity update).
	const double Rate = FVector::DotProduct(Body->GetPhysicsAngularVelocityInRadians(), Up) + FMath::DegreesToRadians((double)GyroBiasDegS);
	double TurnRad = Rate * Dt + FMath::DegreesToRadians((double)GyroNoiseDegSqrtS) * FMath::Sqrt((double)Dt) * Gauss();
	if (FMath::Abs(Measured) < 0.01 && FMath::Abs(TurnRad) < FMath::DegreesToRadians((double)StandstillGyroDegS) * Dt)
	{
		TurnRad = 0.0;
	}
	OdomPosition += AgvMath::Dir(OdomYaw + TurnRad * 0.5) * Measured;
	OdomYaw += TurnRad;
	TravelSinceMatchCm += (float)FMath::Abs(Measured);

	// Diagnostic: measured against the actual travel of the reference point (sign and scale of the encoder model).
	const FVector ReferenceWorld = Actor.TransformPosition(FVector(ReferenceOffset, 0.0));
	const double TrueTravel = FVector::DotProduct(Body->GetPhysicsLinearVelocityAtPoint(ReferenceWorld), Forward) * Dt;
	EncoderMeasuredTrue += Measured * TrueTravel;
	EncoderTrueSquared += TrueTravel * TrueTravel;
	EncoderScale = EncoderTrueSquared > 1.0 ? (float)(EncoderMeasuredTrue / EncoderTrueSquared) : 0.f;
}

bool UAgvForkliftLocalizerComponent::EnsureMap()
{
	if (Map && SensorKit->MapRevision == SeenMapRevision)
	{
		return Map->Width > 0;
	}
	SeenMapRevision = SensorKit->MapRevision;
	const double Floor = SensorKit->SnapshotFloorZCm;
	const double Low = Floor + MapMinZCm, High = Floor + MapMaxZCm;

	// The sensor kit's known map (building structure and fixed racks) in the height band. The key covers the parts
	// and their boxes, so a revision that only re-acquired the same structure reuses the map.
	TArray<UPrimitiveComponent*> Parts;
	TArray<int64> KeyData;
	KeyData.Add(FMath::RoundToInt64(Low));
	KeyData.Add(FMath::RoundToInt64(High));
	KeyData.Add(FMath::RoundToInt64(MapCellCm * 100.0));
	KeyData.Add(FMath::RoundToInt64(MatchDistanceCapCm * 100.0));
	for (const FAgvObservedObstacle& Entry : SensorKit->GetKnownObstacles())
	{
		UPrimitiveComponent* Part = Entry.Component.Get();
		if (!Part)
		{
			continue;
		}
		const FBox Bounds = Part->Bounds.GetBox();
		if (Bounds.Max.Z <= Low || Bounds.Min.Z >= High)
		{
			continue;
		}
		Parts.Add(Part);
		KeyData.Add((int64)(UPTRINT)Part);
		for (const FVector& Corner : { Bounds.Min, Bounds.Max })
		{
			KeyData.Add(FMath::RoundToInt64(Corner.X));
			KeyData.Add(FMath::RoundToInt64(Corner.Y));
			KeyData.Add(FMath::RoundToInt64(Corner.Z));
		}
	}
	const uint64 Key = CityHash64(reinterpret_cast<const char*>(KeyData.GetData()), KeyData.Num() * sizeof(int64));
	if (Map && Key == MapKey)
	{
		return Map->Width > 0;
	}
	MapKey = Key;

	auto Build = [&]() -> TSharedPtr<const FAgvDistanceMap>
	{
		const double Start = FPlatformTime::Seconds();
		TSharedPtr<FAgvDistanceMap> Built = MakeShared<FAgvDistanceMap>();
		Built->CellCm = MapCellCm;
		Built->CapCm = MatchDistanceCapCm;
		FBox2D Extent(ForceInit);
		for (const UPrimitiveComponent* Part : Parts)
		{
			const FBox Bounds = Part->Bounds.GetBox();
			Extent += FVector2D(Bounds.Min);
			Extent += FVector2D(Bounds.Max);
		}
		if (!Extent.bIsValid)
		{
			return Built;
		}
		const double Cell = Built->CellCm;
		Built->Origin = Extent.Min - FVector2D(Built->CapCm * 2.0);
		Built->Width = FMath::CeilToInt32((Extent.Max.X - Built->Origin.X + Built->CapCm * 2.0) / Cell);
		Built->Height = FMath::CeilToInt32((Extent.Max.Y - Built->Origin.Y + Built->CapCm * 2.0) / Cell);
		if ((int64)Built->Width * Built->Height > 40000000)
		{
			UE_LOG(LogAgvForkliftLoc, Warning, TEXT("AGV_LOC_MAP too large (%d x %d cells), not built"), Built->Width, Built->Height);
			Built->Width = Built->Height = 0;
			return Built;
		}
		// Occupancy: a cell is occupied when a cell-sized column over the band overlaps the part.
		TArray<bool> Occupied;
		Occupied.Init(false, Built->Width * Built->Height);
		// Slightly inside the cell so a part that only touches a cell's edge does not fill it.
		const FCollisionShape Column = FCollisionShape::MakeBox(FVector(Cell * 0.5 - 0.05, Cell * 0.5 - 0.05, (High - Low) * 0.5));
		for (const UPrimitiveComponent* Part : Parts)
		{
			const FBox Bounds = Part->Bounds.GetBox();
			const int32 X0 = FMath::Max(0, FMath::FloorToInt32((Bounds.Min.X - Built->Origin.X) / Cell));
			const int32 X1 = FMath::Min(Built->Width - 1, FMath::FloorToInt32((Bounds.Max.X - Built->Origin.X) / Cell));
			const int32 Y0 = FMath::Max(0, FMath::FloorToInt32((Bounds.Min.Y - Built->Origin.Y) / Cell));
			const int32 Y1 = FMath::Min(Built->Height - 1, FMath::FloorToInt32((Bounds.Max.Y - Built->Origin.Y) / Cell));
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					bool& Filled = Occupied[Y * Built->Width + X];
					if (!Filled)
					{
						const FVector Centre(Built->Origin.X + (X + 0.5) * Cell, Built->Origin.Y + (Y + 0.5) * Cell, (Low + High) * 0.5);
						Filled = Part->OverlapComponent(Centre, FQuat::Identity, Column);
					}
				}
			}
		}
		Built->BuildFromOccupancy(Occupied);
		MapBuildMilliseconds = (float)((FPlatformTime::Seconds() - Start) * 1000.0);
		++MapBuilds;
		UE_LOG(LogAgvForkliftLoc, Log, TEXT("AGV_LOC_MAP %s %d parts, %d x %d cells of %.0f cm, %d occupied, floor %.0f, %.0f ms"),
			*Forklift->GetName(), Parts.Num(), Built->Width, Built->Height, Cell, Built->OccupiedCells, Floor, MapBuildMilliseconds);
		return Built;
	};
	UAgvLocalizationSubsystem* Subsystem = GetWorld()->GetSubsystem<UAgvLocalizationSubsystem>();
	Map = Subsystem ? Subsystem->FindOrBuildMap(Key, Build) : Build();
	return Map && Map->Width > 0;
}

void UAgvForkliftLocalizerComponent::CollectAndMatchScan()
{
	if (!SensorKit || !SensorKit->bSnapshotValid || SensorKit->SnapshotCount == SeenSnapshot)
	{
		return;
	}
	SeenSnapshot = SensorKit->SnapshotCount;
	const double Start = FPlatformTime::Seconds();
	if (!EnsureMap())
	{
		++RejectedMatches;
		return;
	}

	// The snapshot was taken in the vehicle's tick this frame, at the pose the odometry has now. Only range, beam
	// angle and the fixed mount are used; the height band uses the floor the vehicle stands on.
	const FTransform Actor = Forklift->GetActorTransform();
	const double Low = SensorKit->SnapshotFloorZCm + MapMinZCm, High = SensorKit->SnapshotFloorZCm + MapMaxZCm;
	TArray<FVector2D> ScanPoints;
	for (const UAgvLidarComponent* Lidar : SensorKit->Lidars)
	{
		if (!Lidar)
		{
			continue;
		}
		const FTransform Sensor = Lidar->GetComponentTransform();
		const FTransform Mount = Sensor.GetRelativeTransform(Actor);
		for (const FAgvLidarReturn& Return : Lidar->GetLastReturns())
		{
			const double Z = Sensor.TransformPosition(Return.SensorPoint).Z;
			if (Z < Low || Z > High)
			{
				continue;
			}
			const FVector2D Vehicle(Mount.TransformPosition(Return.SensorPoint));
			if (SelfFilter.IsInside(Vehicle))
			{
				continue;
			}
			ScanPoints.Add(OdomPosition + AgvMath::Rotate(OdomYaw, Vehicle - ReferenceOffset));
		}
	}
	TArray<FVector2D> Points;
	AgvScanMatch::Voxelize(ScanPoints, MatchVoxelCm, Points);
	LastMatchPoints = Points.Num();
	if (Points.Num() < MinMatchPoints)
	{
		++RejectedMatches;
		LastInlierRatio = 0.f;
		return;
	}

	AgvScanMatch::FParams Params;
	Params.VoxelCm = MatchVoxelCm;
	Params.InlierDistanceCm = InlierDistanceCm;
	Params.RobustScaleCm = RobustScaleCm;
	Params.Iterations = MatchIterations;
	double Tx = CorrectionOffset.X, Ty = CorrectionOffset.Y, Th = CorrectionYaw;
	double Ratio = AgvScanMatch::Refine(*Map, Points, Params, Tx, Ty, Th);
	if (Ratio < MinInlierRatio)
	{
		// Recovery from headings around the current estimate, keeping its position (see UAgvLidarLocalizerComponent).
		FVector2D Position;
		double Yaw;
		ToMap(OdomPosition, OdomYaw, Position, Yaw);
		for (const double OffsetDeg : { 2.0, -2.0, 4.0, -4.0, 7.0, -7.0, 10.0, -10.0, 15.0, -15.0 })
		{
			double SeedTh = CorrectionYaw + FMath::DegreesToRadians(OffsetDeg);
			FVector2D Seed = Position - AgvMath::Rotate(SeedTh, OdomPosition);
			const double SeedRatio = AgvScanMatch::Refine(*Map, Points, Params, Seed.X, Seed.Y, SeedTh);
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
	if (Ratio >= MinInlierRatio)
	{
		FVector2D Before, After;
		double BeforeYaw, AfterYaw;
		ToMap(OdomPosition, OdomYaw, Before, BeforeYaw);
		CorrectionOffset += (FVector2D(Tx, Ty) - CorrectionOffset) * CorrectionGain;
		CorrectionYaw += AgvMath::Wrap(Th - CorrectionYaw) * CorrectionGain;
		ToMap(OdomPosition, OdomYaw, After, AfterYaw);
		LastCorrectionCm = (float)FVector2D::Distance(Before, After);
		TravelSinceMatchCm = 0.f;
		++AcceptedMatches;
	}
	else
	{
		++RejectedMatches;
	}
	LastMatchMilliseconds = (float)((FPlatformTime::Seconds() - Start) * 1000.0);
	MatchMillisecondsSum += LastMatchMilliseconds;
	bMatchToLog = bLogMatches;
}

void UAgvForkliftLocalizerComponent::Measure()
{
	FTransform Estimated;
	if (!GetEstimatedActorTransform(Estimated))
	{
		++LostFrames;
		return;
	}
	const FTransform Actual = Forklift->GetActorTransform();
	ErrorCm = (float)FVector2D::Distance(FVector2D(Estimated.GetLocation()), FVector2D(Actual.GetLocation()));
	HeadingErrorDeg = (float)FMath::Abs(FMath::FindDeltaAngleDegrees(Estimated.Rotator().Yaw, Actual.Rotator().Yaw));
	++ErrorSamples;
	ErrorSum += ErrorCm;
	MeanErrorCm = (float)(ErrorSum / ErrorSamples);
	MaxErrorCm = FMath::Max(MaxErrorCm, ErrorCm);
	MaxHeadingErrorDeg = FMath::Max(MaxHeadingErrorDeg, HeadingErrorDeg);
	if (ErrorHistogram.Num() != AgvForkliftLoc::Bins)
	{
		ErrorHistogram.Init(0, AgvForkliftLoc::Bins);
	}
	++ErrorHistogram[FMath::Min(AgvForkliftLoc::Bins - 1, FMath::FloorToInt32(ErrorCm / AgvForkliftLoc::BinCm))];
	int32 Count = 0;
	for (int32 Bin = 0; Bin < ErrorHistogram.Num(); ++Bin)
	{
		Count += ErrorHistogram[Bin];
		if (Count >= ErrorSamples * 0.95)
		{
			P95ErrorCm = (float)((Bin + 1) * AgvForkliftLoc::BinCm);
			break;
		}
	}
}

void UAgvForkliftLocalizerComponent::ResetStatistics()
{
	ErrorSamples = LostFrames = LostEvents = Reinitializations = 0;
	AcceptedMatches = RejectedMatches = RecoveredMatches = 0;
	ErrorSum = MatchMillisecondsSum = 0.0;
	MeanErrorCm = MaxErrorCm = P95ErrorCm = MaxHeadingErrorDeg = 0.f;
	EncoderMeasuredTrue = EncoderTrueSquared = 0.0;
	EncoderScale = 0.f;
	ErrorHistogram.Init(0, AgvForkliftLoc::Bins);
}

FString UAgvForkliftLocalizerComponent::Summary() const
{
	const int32 Matches = AcceptedMatches + RejectedMatches;
	return FString::Printf(TEXT("%s %s: error mean %.1f p95 %.1f max %.1f cm, heading max %.2f deg (%d frames); matches %d/%d accepted, %d recovered, %.1f ms mean; lost %d (%d frames); reinit %d; encoder scale %.3f; map %d builds, last %.0f ms"),
		Forklift ? *Forklift->GetName() : TEXT("?"), *StatusText, MeanErrorCm, P95ErrorCm, MaxErrorCm, MaxHeadingErrorDeg, ErrorSamples,
		AcceptedMatches, Matches, RecoveredMatches, Matches ? MatchMillisecondsSum / Matches : 0.0, LostEvents, LostFrames, Reinitializations,
		EncoderScale, MapBuilds, MapBuildMilliseconds);
}

// --- Subsystem ----------------------------------------------------------------------------------------------------

bool UAgvLocalizationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UAgvLocalizationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (FParse::Param(FCommandLine::Get(), TEXT("AgvLocShadow")))
	{
		Enable();
	}
}

void UAgvLocalizationSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->RemoveOnActorSpawnedHandler(SpawnHandle);
	}
	Maps.Reset();
	Super::Deinitialize();
}

void UAgvLocalizationSubsystem::Enable()
{
	UWorld* World = GetWorld();
	if (bEnabled || !World)
	{
		return;
	}
	bEnabled = true;
	for (TActorIterator<AWarehouseForklift> It(World); It; ++It)
	{
		Attach(*It);
	}
	SpawnHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UAgvLocalizationSubsystem::Attach));
	UE_LOG(LogAgvForkliftLoc, Log, TEXT("AGV_LOC shadow localization ON"));
}

void UAgvLocalizationSubsystem::Attach(AActor* Actor)
{
	AWarehouseForklift* Forklift = Cast<AWarehouseForklift>(Actor);
	if (!Forklift || Forklift->FindComponentByClass<UAgvForkliftLocalizerComponent>())
	{
		return;
	}
	UAgvForkliftLocalizerComponent* Localizer = NewObject<UAgvForkliftLocalizerComponent>(Forklift, TEXT("AgvForkliftLocalizer"));
	Localizer->RegisterComponent();
}

FString UAgvLocalizationSubsystem::Report() const
{
	FString Text = TEXT("AGV_LOC_REPORT");
	for (TActorIterator<AWarehouseForklift> It(GetWorld()); It; ++It)
	{
		if (const UAgvForkliftLocalizerComponent* Localizer = It->FindComponentByClass<UAgvForkliftLocalizerComponent>())
		{
			Text += TEXT("\n  ") + Localizer->Summary();
		}
	}
	return Text;
}

TSharedPtr<const FAgvDistanceMap> UAgvLocalizationSubsystem::FindOrBuildMap(uint64 Key, TFunctionRef<TSharedPtr<const FAgvDistanceMap>()> Build)
{
	if (const TSharedPtr<const FAgvDistanceMap>* Found = Maps.Find(Key))
	{
		return *Found;
	}
	// Keep a few: one per floor is the usual need.
	if (Maps.Num() >= 8)
	{
		Maps.Reset();
	}
	return Maps.Add(Key, Build());
}
