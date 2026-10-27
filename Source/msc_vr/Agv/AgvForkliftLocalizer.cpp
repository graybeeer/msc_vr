#include "AgvForkliftLocalizer.h"
#include "AgvLidarComponent.h"
#include "AgvPath.h"
#include "AgvShadowSensors.h"
#include "WarehouseForklift.h"
#include "WarehouseAutonomyTypes.h"
#include "CanvasItem.h"
#include "Components/PrimitiveComponent.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "TextureResource.h"
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
			"the exact pose; travel still uses the exact pose). Again = log the results. agv.loc reset = clear the statistics. "
			"agv.loc view = show/hide the top-left panel of what forklift 1 believes. agv.loc drive [0] = route travel on the estimate (on/off)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UAgvLocalizationSubsystem* Subsystem = World ? World->GetSubsystem<UAgvLocalizationSubsystem>() : nullptr;
			if (!Subsystem)
			{
				return;
			}
			if (Args.Contains(TEXT("drive")))
			{
				Subsystem->SetDriveOnEstimate(!Args.Contains(TEXT("0")));
				return;
			}
			if (Args.Contains(TEXT("view")))
			{
				Subsystem->SetPanelVisible(!Subsystem->IsPanelVisible());
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
		DriveWheelbaseCm = ReferenceOffset.X - Actor.InverseTransformPosition(DriveWheel->GetComponentLocation()).X;
		// A steered wheel's world box keeps its radius as half height.
		WheelRadiusCm = FMath::Max(1.0, (double)DriveWheel->Bounds.BoxExtent.Z);
		// The vehicle with a pallet on its forks: forward by about a pallet's overhang, sideways by its extra width.
		const FBox Local = Forklift->CalculateComponentsBoundingBoxInLocalSpace(false);
		VehicleBox = FBox2D(FVector2D(Local.Min), FVector2D(Local.Max));
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
	CorrectionOffset = DrivingOffset = FVector2D::ZeroVector;
	CorrectionYaw = DrivingYaw = 0.0;
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

void UAgvForkliftLocalizerComponent::SlewDrivingCorrection(float Dt)
{
	// Move the driving pose (not the correction's parameters: a yaw change about the map origin would swing a vehicle
	// far from it sideways) towards the matched pose at limited rates, then express it as the driving correction.
	FVector2D Target, Current;
	double TargetYaw, CurrentYaw;
	ToMap(OdomPosition, OdomYaw, Target, TargetYaw);
	Current = AgvMath::Rotate(DrivingYaw, OdomPosition) + DrivingOffset;
	CurrentYaw = OdomYaw + DrivingYaw;
	const FVector2D Delta = Target - Current;
	const double DeltaYaw = AgvMath::Wrap(TargetYaw - CurrentYaw);
	if (Delta.Size() > 100.0 || FMath::Abs(DeltaYaw) > FMath::DegreesToRadians(10.0))
	{
		// Too far apart to blend (a re-initialisation or a recovered heading): take the match.
		DrivingOffset = CorrectionOffset;
		DrivingYaw = CorrectionYaw;
		return;
	}
	const double MaxStep = CorrectionRateCmS * Dt;
	Current += Delta.Size() > MaxStep ? Delta.GetSafeNormal() * MaxStep : Delta;
	const double MaxTurn = FMath::DegreesToRadians((double)CorrectionRateDegS) * Dt;
	CurrentYaw += FMath::Clamp(DeltaYaw, -MaxTurn, MaxTurn);
	DrivingYaw = CurrentYaw - OdomYaw;
	DrivingOffset = Current - AgvMath::Rotate(DrivingYaw, OdomPosition);
}

bool UAgvForkliftLocalizerComponent::GetPose(FVector2D& OutPosition, double& OutYaw) const
{
	if (!bInitialized || !bAvailable || bLost)
	{
		return false;
	}
	OutPosition = AgvMath::Rotate(DrivingYaw, OdomPosition) + DrivingOffset;
	OutYaw = OdomYaw + DrivingYaw;
	OutPosition += PositionBiasCm;
	OutYaw += FMath::DegreesToRadians((double)YawBiasDeg);
	return true;
}

bool UAgvForkliftLocalizerComponent::GetEstimatedActorTransform(FTransform& Out, bool bEvenIfLost) const
{
	FVector2D Reference;
	double Yaw;
	if (!Forklift || !bInitialized)
	{
		return false;
	}
	if (!GetPose(Reference, Yaw))
	{
		if (!bEvenIfLost)
		{
			return false;
		}
		Reference = AgvMath::Rotate(DrivingYaw, OdomPosition) + DrivingOffset;
		Yaw = OdomYaw + DrivingYaw;
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
			SlewDrivingCorrection(LastDt);
		}
		if (Forklift->IsRemoteControlled() || bNoMap)
		{
			// Manual driving switches the scans off by design: the odometry keeps the estimate and the first scans after
			// the session decide again, instead of a fault the moment the vehicle returns to autonomous work.
			TravelSinceMatchCm = 0.f;
		}
		if (TravelSinceMatchCm > LostAfterTravelCm && !bLost)
		{
			UE_LOG(LogAgvForkliftLoc, Warning, TEXT("AGV_LOC_LOST %s after %.0f cm without a LiDAR match"), *Forklift->GetName(), TravelSinceMatchCm);
			bLost = true;
			++LostEvents;
		}
		StatusText = bNoMap ? TEXT("NO MAP - EXACT POSE") : bLost ? TEXT("LOST") : TravelSinceMatchCm > 200.f ? TEXT("ODOMETRY ONLY") : TEXT("LIDAR");
		Measure();
		if (bMatchToLog)
		{
			bMatchToLog = false;
			UE_LOG(LogAgvForkliftLoc, Log, TEXT("AGV_LOC_SHADOW %s t=%.1f err=%.1fcm yaw=%.2fdeg ratio=%.2f pts=%d corr=%.1fcm matched_along=%.1f matched_across=%.1f speed=%.0f ms=%.1f %s"),
				*Forklift->GetName(), GetWorld()->GetTimeSeconds(), ErrorCm, HeadingErrorDeg, LastInlierRatio, LastMatchPoints,
				LastCorrectionCm, MatchAlongCm, MatchAcrossCm, FVector::DotProduct(Body->GetPhysicsLinearVelocity(), Forklift->GetActorForwardVector()),
				LastMatchMilliseconds, *StatusText);
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
	// The drive wheel's forward share is the travel of the load-wheel axle.
	const double Measured = Rim * FMath::Cos(Steer);

	// Yaw-rate gyro with bias and angle random walk; held at standstill (zero-velocity update).
	const double Rate = FVector::DotProduct(Body->GetPhysicsAngularVelocityInRadians(), Up) + FMath::DegreesToRadians((double)GyroBiasDegS);
	double TurnRad = Rate * Dt + FMath::DegreesToRadians((double)GyroNoiseDegSqrtS) * FMath::Sqrt((double)Dt) * Gauss();
	if (FMath::Abs(Measured) < 0.01 && FMath::Abs(TurnRad) < FMath::DegreesToRadians((double)StandstillGyroDegS) * Dt)
	{
		TurnRad = 0.0;
	}
	// Side slip of the load-wheel axle from the same sensors: the drive wheel rolls along its heading, so the axle
	// moves sideways by the wheel's side share minus what the yaw (gyro) explains over the wheelbase. Assuming no side
	// slip there drifted the estimate up to 20 cm between one-second scans in curves (2026-10-27).
	const double Side = bModelSideSlip ? Rim * FMath::Sin(Steer) + TurnRad * DriveWheelbaseCm : 0.0;
	OdomPosition += AgvMath::Rotate(OdomYaw + TurnRad * 0.5, FVector2D(Measured, Side));
	OdomYaw += TurnRad;
	TravelSinceMatchCm += (float)FVector2D(Measured, Side).Size();

	// Diagnostic: measured against the actual travel of the reference point (sign and scale of the encoder model).
	const FVector ReferenceWorld = Actor.TransformPosition(FVector(ReferenceOffset, 0.0));
	const double TrueTravel = FVector::DotProduct(Body->GetPhysicsLinearVelocityAtPoint(ReferenceWorld), Forward) * Dt;
	EncoderMeasuredTrue += Measured * TrueTravel;
	EncoderTrueSquared += TrueTravel * TrueTravel;
	EncoderScale = EncoderTrueSquared > 1.0 ? (float)(EncoderMeasuredTrue / EncoderTrueSquared) : 0.f;
	const double TrueSide = FVector::DotProduct(Body->GetPhysicsLinearVelocityAtPoint(ReferenceWorld), Right) * Dt;
	SideMeasuredTrue += Side * TrueSide;
	SideTrueSquared += TrueSide * TrueSide;
	SideSlipScale = SideTrueSquared > 1.0 ? (float)(SideMeasuredTrue / SideTrueSquared) : 0.f;
	SideResidualSquared += FMath::Square(Side - TrueSide);
	SideSlipResidualCm = (float)FMath::Sqrt(SideResidualSquared);
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
			// Nothing to localize against (e.g. a test scene with only a floor slab): every match is rejected.
			UE_LOG(LogAgvForkliftLoc, Warning, TEXT("AGV_LOC_MAP_EMPTY %s no known structure between %.0f and %.0f cm"), *Forklift->GetName(), Low, High);
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
	const bool bHadNoMap = bNoMap;
	const bool bHaveMap = EnsureMap();
	// No known structure at all (a test scene with only a floor slab): there is nothing to localize against, so the
	// estimate is unavailable rather than lost and travel uses the exact pose. A map that exists but does not match
	// is still lost.
	bNoMap = !bHaveMap;
	if (bNoMap != bHadNoMap)
	{
		UE_LOG(LogAgvForkliftLoc, Warning, TEXT("AGV_LOC_NO_MAP %s %s"), *Forklift->GetName(), bNoMap ? TEXT("no known structure: travel on the exact pose") : TEXT("map available again: travel on the estimate"));
		bLost = false;
		if (!bNoMap)
		{
			InitializePose();
		}
	}
	if (!bHaveMap)
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
		if (bLost)
		{
			// The scan fits the map again from the dead-reckoned estimate: localized again.
			UE_LOG(LogAgvForkliftLoc, Log, TEXT("AGV_LOC_RELOCALIZED %s"), *Forklift->GetName());
			bLost = false;
		}
	}
	else
	{
		++RejectedMatches;
	}
	{
		// Diagnostic: the matched pose (before the driving blend) against the truth, along and across the vehicle.
		FVector2D Matched;
		double MatchedYaw;
		ToMap(OdomPosition, OdomYaw, Matched, MatchedYaw);
		const FTransform Actual = Forklift->GetActorTransform();
		const FVector2D TrueReference = FVector2D(Actual.GetLocation()) + AgvMath::Rotate(FMath::DegreesToRadians(Actual.Rotator().Yaw), ReferenceOffset);
		const FVector2D Delta = Matched - TrueReference;
		const double Yaw = FMath::DegreesToRadians(Actual.Rotator().Yaw);
		MatchAlongCm = (float)FVector2D::DotProduct(Delta, AgvMath::Dir(Yaw));
		MatchAcrossCm = (float)FVector2D::DotProduct(Delta, AgvMath::Normal(Yaw));
	}
	// The scan as the vehicle now places it: on the map where it believes it stands.
	LastScanInMap.Reset(Points.Num());
	LastScanMapped.Reset(Points.Num());
	for (const FVector2D& Point : Points)
	{
		const FVector2D Placed = AgvMath::Rotate(CorrectionYaw, Point) + CorrectionOffset;
		LastScanInMap.Add(Placed);
		LastScanMapped.Add(Map->Sample(Placed) <= InlierDistanceCm);
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
	// Diagnostic: once per episode of a large error, what the vehicle was doing.
	if (ErrorCm > 10.f && !bHighErrorLogged)
	{
		bHighErrorLogged = true;
		const FVector Velocity = Body->GetPhysicsLinearVelocity();
		UE_LOG(LogAgvForkliftLoc, Warning, TEXT("AGV_LOC_HIGH_ERROR %s t=%.2f err=%.1fcm yaw=%.2fdeg state=%d speed=%.0f lateral=%.0f yawrate=%.1f since_match=%.0fcm dt=%.4f status=%s"),
			*Forklift->GetName(), GetWorld()->GetTimeSeconds(), ErrorCm, HeadingErrorDeg, (int32)Forklift->AIState,
			FVector::DotProduct(Velocity, Actual.GetUnitAxis(EAxis::X)), FVector::DotProduct(Velocity, Actual.GetUnitAxis(EAxis::Y)),
			Body->GetPhysicsAngularVelocityInDegrees().Z, TravelSinceMatchCm, LastDt, *Forklift->Status.Replace(TEXT("\n"), TEXT(" ")));
	}
	else if (ErrorCm < 5.f)
	{
		bHighErrorLogged = false;
	}
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
	SideMeasuredTrue = SideTrueSquared = SideResidualSquared = 0.0;
	SideSlipScale = SideSlipResidualCm = 0.f;
	ErrorHistogram.Init(0, AgvForkliftLoc::Bins);
}

FString UAgvForkliftLocalizerComponent::Summary() const
{
	const int32 Matches = AcceptedMatches + RejectedMatches;
	return FString::Printf(TEXT("%s %s: error mean %.1f p95 %.1f max %.1f cm, heading max %.2f deg (%d frames); matches %d/%d accepted, %d recovered, %.1f ms mean; lost %d (%d frames); reinit %d; encoder scale %.3f; side slip scale %.2f residual %.0f cm; map %d builds, last %.0f ms"),
		Forklift ? *Forklift->GetName() : TEXT("?"), *StatusText, MeanErrorCm, P95ErrorCm, MaxErrorCm, MaxHeadingErrorDeg, ErrorSamples,
		AcceptedMatches, Matches, RecoveredMatches, Matches ? MatchMillisecondsSum / Matches : 0.0, LostEvents, LostFrames, Reinitializations,
		EncoderScale, SideSlipScale, SideSlipResidualCm, MapBuilds, MapBuildMilliseconds);
}

// --- Subsystem ----------------------------------------------------------------------------------------------------

bool UAgvLocalizationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UAgvLocalizationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// Vehicles spawned later (tests copy rigs into new scenes, the layout editor adds vehicles) are checked too.
	SpawnHandle = InWorld.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UAgvLocalizationSubsystem::OnActorSpawned));
	if (FParse::Param(FCommandLine::Get(), TEXT("AgvLocOff")))
	{
		// A/B runs: every vehicle on the exact pose, nothing attached, as before the localization work.
		bDriveOverride = true;
		bDriveOnEstimate = false;
		for (TActorIterator<AWarehouseForklift> It(&InWorld); It; ++It)
		{
			It->bUseAgvLocalization = false;
		}
		UE_LOG(LogAgvForkliftLoc, Log, TEXT("AGV_LOC off (-AgvLocOff)"));
		return;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("AgvLocDrive")))
	{
		SetDriveOnEstimate(true);
		return;
	}
	bool bWanted = FParse::Param(FCommandLine::Get(), TEXT("AgvLocShadow"));
	for (TActorIterator<AWarehouseForklift> It(&InWorld); It && !bWanted; ++It)
	{
		bWanted = It->bUseAgvLocalization;
	}
	if (bWanted)
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
	if (DrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(DrawHandle);
		DrawHandle.Reset();
	}
	Maps.Reset();
	TextureMap.Reset();
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
	DrawHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UAgvLocalizationSubsystem::DrawPanel));
	UE_LOG(LogAgvForkliftLoc, Log, TEXT("AGV_LOC localization ON"));
}

void UAgvLocalizationSubsystem::OnActorSpawned(AActor* Actor)
{
	AWarehouseForklift* Forklift = Cast<AWarehouseForklift>(Actor);
	if (!Forklift)
	{
		return;
	}
	if (bDriveOverride)
	{
		Forklift->bUseAgvLocalization = bDriveOnEstimate;
	}
	if (bEnabled)
	{
		Attach(Actor);
	}
	else if (Forklift->bUseAgvLocalization)
	{
		Enable(); // attaches every forklift, this one included
	}
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
	if (bDriveOverride)
	{
		Forklift->bUseAgvLocalization = bDriveOnEstimate;
	}
}

void UAgvLocalizationSubsystem::SetDriveOnEstimate(bool bOn)
{
	Enable();
	bDriveOverride = true;
	bDriveOnEstimate = bOn;
	for (TActorIterator<AWarehouseForklift> It(GetWorld()); It; ++It)
	{
		It->bUseAgvLocalization = bOn;
	}
	UE_LOG(LogAgvForkliftLoc, Log, TEXT("AGV_LOC route travel on the LiDAR estimate %s"), bOn ? TEXT("ON") : TEXT("OFF"));
}

// --- AWarehouseForklift hooks (declared in WarehouseForklift.h) ---------------------------------------------------

FTransform AWarehouseForklift::NavigationTransform() const
{
	FTransform Estimated;
	const UAgvForkliftLocalizerComponent* Localizer = bUseAgvLocalization ? FindComponentByClass<UAgvForkliftLocalizerComponent>() : nullptr;
	if (Localizer && Localizer->HasNoMap())
	{
		return GetActorTransform();
	}
	// When lost, travel stops (AgvLocalizationBlocked); planning a stop still gets the dead-reckoned guess, never the truth.
	return Localizer && Localizer->GetEstimatedActorTransform(Estimated, true) ? Estimated : GetActorTransform();
}

bool AWarehouseForklift::AgvLocalizationBlocked(FString& OutReason) const
{
	const UAgvForkliftLocalizerComponent* Localizer = bUseAgvLocalization ? FindComponentByClass<UAgvForkliftLocalizerComponent>() : nullptr;
	if (Localizer && Localizer->IsLost())
	{
		OutReason = TEXT("AGV LOCALIZATION LOST");
		return true;
	}
	return false;
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

// --- View panel ---------------------------------------------------------------------------------------------------

namespace AgvForkliftLoc
{
	/** Clips a segment to the rectangle [Min, Max] (Liang-Barsky). False when nothing is left. */
	static bool ClipSegment(FVector2D& A, FVector2D& B, const FVector2D& Min, const FVector2D& Max)
	{
		const FVector2D D = B - A;
		double T0 = 0.0, T1 = 1.0;
		const double P[4] = { -D.X, D.X, -D.Y, D.Y };
		const double Q[4] = { A.X - Min.X, Max.X - A.X, A.Y - Min.Y, Max.Y - A.Y };
		for (int32 I = 0; I < 4; ++I)
		{
			if (FMath::IsNearlyZero(P[I]))
			{
				if (Q[I] < 0.0)
				{
					return false;
				}
				continue;
			}
			const double T = Q[I] / P[I];
			if (P[I] < 0.0)
			{
				T0 = FMath::Max(T0, T);
			}
			else
			{
				T1 = FMath::Min(T1, T);
			}
			if (T0 > T1)
			{
				return false;
			}
		}
		B = A + D * T1;
		A = A + D * T0;
		return true;
	}
}

UAgvForkliftLocalizerComponent* UAgvLocalizationSubsystem::FindPanelVehicle() const
{
	// Forklift 1: the vehicle named with "1", otherwise the first by name.
	UAgvForkliftLocalizerComponent* Best = nullptr;
	FString BestName;
	for (TActorIterator<AWarehouseForklift> It(GetWorld()); It; ++It)
	{
		UAgvForkliftLocalizerComponent* Localizer = It->FindComponentByClass<UAgvForkliftLocalizerComponent>();
		if (!Localizer)
		{
			continue;
		}
		if (It->VehicleName.Contains(TEXT("1")))
		{
			return Localizer;
		}
		const FString Name = It->GetActorNameOrLabel();
		if (!Best || Name < BestName)
		{
			Best = Localizer;
			BestName = Name;
		}
	}
	return Best;
}

void UAgvLocalizationSubsystem::UpdateMapTexture(const TSharedPtr<const FAgvDistanceMap>& Map)
{
	if (MapTexture && TextureMap == Map)
	{
		return;
	}
	TextureMap = Map;
	MapTexture = nullptr;
	if (!Map || Map->Width <= 0 || Map->Width > 16384 || Map->Height > 16384)
	{
		return;
	}
	UTexture2D* Texture = UTexture2D::CreateTransient(Map->Width, Map->Height, PF_B8G8R8A8);
	if (!Texture)
	{
		return;
	}
	Texture->Filter = TF_Nearest;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Index = 0; Index < Map->Width * Map->Height; ++Index)
	{
		// Mapped structure solid, its edge faint, free space clear.
		const float Distance = Map->Distance[Index];
		Pixels[Index] = Distance <= 0.f ? FColor(205, 210, 215, 255) : Distance < Map->CellCm ? FColor(205, 210, 215, 90) : FColor(0, 0, 0, 0);
	}
	Mip.BulkData.Unlock();
	Texture->UpdateResource();
	MapTexture = Texture;
}

void UAgvLocalizationSubsystem::DrawPanel(UCanvas* Canvas, APlayerController* Player)
{
	if (!bShowPanel || !Canvas || !Canvas->Canvas || !Player || Player->GetWorld() != GetWorld())
	{
		return;
	}
	UAgvForkliftLocalizerComponent* Vehicle = FindPanelVehicle();
	AWarehouseForklift* Forklift = Vehicle ? Vehicle->GetForklift() : nullptr;
	if (!Forklift)
	{
		return;
	}

	// A top view (world X right, Y down, as in the editor top view) centred on where the vehicle believes it is.
	const double Size = FMath::Clamp(Canvas->ClipY * 0.36, 220.0, 420.0);
	const FVector2D Panel(16.0, 16.0);
	const FVector2D PanelMax = Panel + FVector2D(Size);
	const double SpanCm = 2400.0, Scale = Size / SpanCm;
	FTransform Estimated;
	const bool bEstimate = Vehicle->GetEstimatedActorTransform(Estimated);
	const FTransform Actual = Forklift->GetActorTransform();
	const FVector2D Centre(bEstimate ? Estimated.GetLocation() : Actual.GetLocation());
	auto ToScreen = [&](const FVector2D& World) { return Panel + (World - Centre + FVector2D(SpanCm * 0.5)) * Scale; };
	auto Line = [&](FVector2D A, FVector2D B, const FLinearColor& Color, float Thickness)
	{
		if (AgvForkliftLoc::ClipSegment(A, B, Panel, PanelMax))
		{
			FCanvasLineItem Item(A, B);
			Item.SetColor(Color);
			Item.LineThickness = Thickness;
			Canvas->DrawItem(Item);
		}
	};
	auto Outline = [&](const FTransform& Pose, const FLinearColor& Color, float Thickness)
	{
		const FBox2D& Box = Vehicle->GetVehicleBox();
		const FVector2D Local[4] = { Box.Min, FVector2D(Box.Max.X, Box.Min.Y), Box.Max, FVector2D(Box.Min.X, Box.Max.Y) };
		for (int32 I = 0; I < 4; ++I)
		{
			Line(ToScreen(FVector2D(Pose.TransformPosition(FVector(Local[I], 0.0)))), ToScreen(FVector2D(Pose.TransformPosition(FVector(Local[(I + 1) % 4], 0.0)))), Color, Thickness);
		}
		Line(ToScreen(FVector2D(Pose.GetLocation())), ToScreen(FVector2D(Pose.TransformPosition(FVector(Box.Max.X + 120.0, 0.0, 0.0)))), Color, Thickness);
	};

	FCanvasTileItem Back(Panel, FVector2D(Size), FLinearColor(0.02f, 0.03f, 0.05f, 0.78f));
	Back.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Back);

	// The map it localizes against.
	const TSharedPtr<const FAgvDistanceMap> Map = Vehicle->GetMap();
	UpdateMapTexture(Map);
	if (Map && MapTexture && MapTexture->GetResource())
	{
		const double Width = Map->Width * Map->CellCm, Height = Map->Height * Map->CellCm;
		const FVector2D Min = Centre - FVector2D(SpanCm * 0.5);
		const FVector2D UV0((Min.X - Map->Origin.X) / Width, (Min.Y - Map->Origin.Y) / Height);
		const FVector2D UV1(UV0.X + SpanCm / Width, UV0.Y + SpanCm / Height);
		FCanvasTileItem Tile(Panel, MapTexture->GetResource(), FVector2D(Size), UV0, UV1, FLinearColor::White);
		Tile.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Tile);
	}

	// The route it follows.
	FVector2D Previous = ToScreen(FVector2D(Actual.GetLocation()));
	for (const FWarehouseRoutePoint& Point : Forklift->PlannedRoute)
	{
		const FVector2D Next = ToScreen(FVector2D(Point.Pose.GetLocation()));
		Line(Previous, Next, FLinearColor(0.25f, 0.55f, 1.f, 0.9f), 2.f);
		Previous = Next;
	}

	// The last scan where it believes the points are: on the map (green) or unmapped things (orange).
	const TArray<FVector2D>& Scan = Vehicle->GetLastScanInMap();
	const TArray<bool>& Mapped = Vehicle->GetLastScanMapped();
	for (int32 Index = 0; Index < Scan.Num(); ++Index)
	{
		const FVector2D Screen = ToScreen(Scan[Index]);
		if (Screen.X < Panel.X || Screen.Y < Panel.Y || Screen.X > PanelMax.X - 3.0 || Screen.Y > PanelMax.Y - 3.0)
		{
			continue;
		}
		FCanvasTileItem Dot(Screen - FVector2D(1.0), FVector2D(3.0), Mapped.IsValidIndex(Index) && Mapped[Index] ? FLinearColor(0.3f, 1.f, 0.4f, 1.f) : FLinearColor(1.f, 0.55f, 0.1f, 1.f));
		Canvas->DrawItem(Dot);
	}

	// Actual pose (white) under the estimate (yellow): the gap is the localization error.
	Outline(Actual, FLinearColor(1.f, 1.f, 1.f, 0.6f), 1.f);
	if (bEstimate)
	{
		Outline(Estimated, FLinearColor(1.f, 0.85f, 0.1f, 1.f), 2.f);
	}
	const FLinearColor Frame(0.6f, 0.65f, 0.7f, 1.f);
	Line(Panel, FVector2D(PanelMax.X, Panel.Y), Frame, 1.f);
	Line(FVector2D(PanelMax.X, Panel.Y), PanelMax, Frame, 1.f);
	Line(PanelMax, FVector2D(Panel.X, PanelMax.Y), Frame, 1.f);
	Line(FVector2D(Panel.X, PanelMax.Y), Panel, Frame, 1.f);

	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (Font)
	{
		auto Text = [&](const FString& Value, double Y, const FLinearColor& Color)
		{
			FCanvasTextItem Item(FVector2D(Panel.X + 6.0, Y), FText::FromString(Value), Font, Color);
			Item.EnableShadow(FLinearColor::Black);
			Canvas->DrawItem(Item);
		};
		Text(TEXT("FORKLIFT 1 - AI WORLD (24 m)"), Panel.Y + 4.0, FLinearColor::White);
		Text(FString::Printf(TEXT("%s  err %.1f cm  match %.2f  pts %d"), *Vehicle->StatusText, Vehicle->ErrorCm, Vehicle->LastInlierRatio, Vehicle->LastMatchPoints),
			PanelMax.Y + 4.0, FLinearColor::White);
		Text(TEXT("map grey | scan on map green | unmapped orange | route blue | estimate yellow | actual white"), PanelMax.Y + 20.0, FLinearColor(0.75f, 0.78f, 0.8f, 1.f));
	}
}
