#include "AgvShadowSensors.h"
#include "AgvLidarComponent.h"
#include "AgvLidarLocalizerComponent.h"
#include "AgvPath.h"
#include "AgvSafetyComponent.h"
#include "AgvTestVehicle.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "WarehouseForklift.h"
#include "WarehouseAutonomyTypes.h"
#include "WarehousePallet.h"
#include "WarehouseChargingStation.h"

DEFINE_LOG_CATEGORY_STATIC(LogAgvShadow, Log, All);

namespace
{
	// Usage in the console during play: agv.localize 1|0 (main forklifts travel on the AGV module's LiDAR estimate)
	FAutoConsoleCommandWithWorldAndArgs LocalizeCommand(
		TEXT("agv.localize"),
		TEXT("agv.localize 1: the main warehouse forklifts make their route travel decisions on the AGV module's LiDAR "
			"localization (adds the shadow sensors if needed). agv.localize 0: back to the exact pose."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || !World->IsGameWorld())
			{
				return;
			}
			const bool bOn = Args.IsEmpty() || Args[0] != TEXT("0");
			bool bHaveManager = false;
			for (TActorIterator<AAgvShadowManager> It(World); It; ++It)
			{
				bHaveManager = true;
			}
			if (bOn && !bHaveManager)
			{
				World->SpawnActor<AAgvShadowManager>();
			}
			for (TActorIterator<AWarehouseForklift> It(World); It; ++It)
			{
				It->bUseAgvLocalization = bOn;
			}
			UE_LOG(LogAgvShadow, Log, TEXT("AGV_SHADOW localization for travel %s"), bOn ? TEXT("ON") : TEXT("OFF"));
		}));

	// Usage in the console during play: agv.safety 1|0 (main forklifts obey the AGV module's safety fields)
	FAutoConsoleCommandWithWorldAndArgs SafetyCommand(
		TEXT("agv.safety"),
		TEXT("agv.safety 1: the main warehouse forklifts obey the AGV module's safety fields (protective stop = obstacle "
			"wait, warning = slow; adds the shadow sensors if needed). agv.safety 0: off."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || !World->IsGameWorld())
			{
				return;
			}
			const bool bOn = Args.IsEmpty() || Args[0] != TEXT("0");
			bool bHaveManager = false;
			for (TActorIterator<AAgvShadowManager> It(World); It; ++It)
			{
				bHaveManager = true;
			}
			if (bOn && !bHaveManager)
			{
				World->SpawnActor<AAgvShadowManager>();
			}
			for (TActorIterator<AWarehouseForklift> It(World); It; ++It)
			{
				It->bUseAgvSafety = bOn;
			}
			UE_LOG(LogAgvShadow, Log, TEXT("AGV_SHADOW safety fields in control %s"), bOn ? TEXT("ON") : TEXT("OFF"));
		}));

	// Usage in the console during play: agv.shadow (adds shadow sensors to every main forklift; again = report)
	FAutoConsoleCommandWithWorldAndArgs ShadowCommand(
		TEXT("agv.shadow"),
		TEXT("Shadow mode: put the AGV module's sensors, localizer and safety fields on every main warehouse forklift "
			"(measure only, no control). Run again to log the results. agv.shadow draw = also draw the safety fields."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || !World->IsGameWorld())
			{
				return;
			}
			for (TActorIterator<AAgvShadowManager> It(World); It; ++It)
			{
				// Already attached (at play start by default): "draw" toggles the field drawing, otherwise report.
				if (Args.Contains(TEXT("draw")))
				{
					It->SetDrawFields(!It->bDrawFields);
					UE_LOG(LogAgvShadow, Log, TEXT("AGV_SHADOW field drawing %s"), It->bDrawFields ? TEXT("ON") : TEXT("OFF"));
					return;
				}
				UE_LOG(LogAgvShadow, Log, TEXT("%s"), *It->Report());
				return;
			}
			AAgvShadowManager* Manager = World->SpawnActor<AAgvShadowManager>();
			if (Manager)
			{
				Manager->bDrawFields = Args.Contains(TEXT("draw"));
			}
		}));
}

// --- External drive ---------------------------------------------------------------------------------------------

void UAgvExternalDriveComponent::Step(float Dt)
{
	if (!Body || !Body->IsSimulatingPhysics() || Dt <= 0.f)
	{
		return;
	}
	const AActor* Owner = GetOwner();
	const FVector Reference = Owner->GetActorTransform().TransformPosition(FVector(ReferenceOffsetCm, 0.0, 0.0));
	const double Speed = FVector::DotProduct(Body->GetPhysicsLinearVelocityAtPoint(Reference), Owner->GetActorForwardVector());
	const double YawRateDeg = Body->GetPhysicsAngularVelocityInDegrees().Z;
	SpeedCmS = (float)Speed;
	YawRateDegS = (float)YawRateDeg;
	// The steer angle and wheel speed this motion implies (v = V cos d, w = -V sin d / L), wheel within +-90 deg.
	const double TurnTerm = -FMath::DegreesToRadians(YawRateDeg) * WheelbaseCm();
	double Wheel = FMath::Sqrt(Speed * Speed + TurnTerm * TurnTerm);
	double Steer = 0.0;
	if (Wheel > 0.5)
	{
		if (Speed < 0.0)
		{
			Wheel = -Wheel;
		}
		Steer = FMath::Atan2(TurnTerm / Wheel, Speed / Wheel);
	}
	else
	{
		Wheel = 0.0;
	}
	SteerAngleDeg = (float)FMath::RadiansToDegrees(Steer);
	WheelSpeedCmS = (float)Wheel;
	DriveWheelTravelCm += Wheel * Dt;
	// The motion going on stands for the command (the field set follows it).
	CommandSpeedCmS = Speed;
	CommandYawRateDegS = YawRateDeg;
}

// --- Shadow sensors -----------------------------------------------------------------------------------------------

UAgvShadowSensorsComponent::UAgvShadowSensorsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After physics: the sensors see the body where this frame's simulation put it.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UAgvShadowSensorsComponent::Build()
{
	AActor* Owner = GetOwner();
	USceneComponent* Mount = Owner->GetRootComponent();
	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Mount);
	if (!Body)
	{
		return;
	}

	// Same model and origin as the test vehicle (fork heel; support-wheel axle at -5.3, drive-steer axis at -56.3).
	Drive = NewObject<UAgvExternalDriveComponent>(Owner, TEXT("AgvShadowDrive"));
	Drive->Body = Body;
	Drive->ReferenceOffsetCm = (float)AAgvTestVehicle::SupportWheelX;
	Drive->DriveWheelOffsetCm = (float)AAgvTestVehicle::DriveWheelX;
	Drive->RegisterComponent();

	auto Lidar = [&](const TCHAR* Name, const FVector& Location, const FRotator& Rotation)
	{
		UAgvLidarComponent* Sensor = NewObject<UAgvLidarComponent>(Owner, Name);
		Sensor->SetupAttachment(Mount);
		Sensor->SetRelativeLocationAndRotation(Location, Rotation);
		Sensor->RegisterComponent();
		Lidars.Add(Sensor);
		return Sensor;
	};
	// As AAgvTestVehicle: top 3D LiDAR for localization, two corner safety scanners, two fork-side 3D sensors.
	UAgvLidarComponent* Top = Lidar(TEXT("AgvShadowTopLidar"), AAgvTestVehicle::Refined(-36, -10, 272), FRotator::ZeroRotator);
	Top->HorizontalResolutionDeg = 0.8f;
	Top->RotationHz = 5.f;
	TArray<UAgvLidarComponent*> Scanners;
	for (const double Side : { 1.0, -1.0 })
	{
		UAgvLidarComponent* Scanner = Lidar(Side > 0 ? TEXT("AgvShadowScannerL") : TEXT("AgvShadowScannerR"),
			AAgvTestVehicle::Refined(-73, 47.0 * Side, 27), FRotator(0.0, 135.0 * Side, 0.0));
		Scanner->bUseForLocalization = false;
		Scanner->HorizontalFovDeg = 270.f;
		Scanner->HorizontalResolutionDeg = 1.f;
		Scanner->Channels = 1;
		Scanner->VerticalMinDeg = Scanner->VerticalMaxDeg = 0.f;
		Scanner->RotationHz = 25.f;
		Scanner->MinRangeCm = 5.f;
		Scanner->MaxRangeCm = 550.f;
		Scanner->RangeNoiseCm = 1.f;
		Scanner->DropoutProbability = 0.005f;
		Scanner->NoiseSeed = Side > 0 ? 23 : 29;
		Scanners.Add(Scanner);
	}
	for (const double Y : { 26.0, -46.0 })
	{
		UAgvLidarComponent* Sensor = Lidar(Y > 0 ? TEXT("AgvShadowForkSensorL") : TEXT("AgvShadowForkSensorR"),
			AAgvTestVehicle::Refined(-22, Y, 235), FRotator(-30.0, 0.0, 0.0));
		Sensor->bUseForLocalization = false;
		Sensor->HorizontalFovDeg = 80.f;
		Sensor->HorizontalResolutionDeg = 3.f;
		Sensor->Channels = 15;
		Sensor->VerticalMinDeg = -25.f;
		Sensor->VerticalMaxDeg = 25.f;
		Sensor->RotationHz = 20.f;
		Sensor->MinRangeCm = 20.f;
		Sensor->MaxRangeCm = 600.f;
		Sensor->RangeNoiseCm = 1.f;
		Sensor->DropoutProbability = 0.01f;
		Sensor->NoiseSeed = Y > 0.0 ? 31 : 37;
		Scanners.Add(Sensor);
	}

	Localizer = NewObject<UAgvLidarLocalizerComponent>(Owner, TEXT("AgvShadowLocalizer"));
	Localizer->RegisterComponent();
	Localizer->InitializePose();

	Safety = NewObject<UAgvSafetyComponent>(Owner, TEXT("AgvShadowSafety"));
	for (UAgvLidarComponent* Scanner : Scanners)
	{
		Safety->Scanners.Add(Scanner);
	}
	Safety->Footprint = FBox2D(FVector2D(-97.4, -49.7), FVector2D(115.0, 49.7));
	Safety->ForkRootXCm = 0.f;     // 115 cm forks (VNSL14) end at the footprint's +115
	Safety->ForkHalfWidthCm = 34.f; // 68 cm over the forks
	Safety->ForksFirstMaxSpeedCm = 60.f;
	Safety->WarningFieldLengthCm = 480.f;
	// Speed-dependent fields down to creep speed, as safety scanners switch field sets by speed: at 20 cm/s the stop
	// takes about 8 cm, so a fixed 1 m minimum (the test vehicle's) stopped the forklift at every slow manoeuvre.
	Safety->MinProtectiveLengthCm = 40.f;
	Safety->FieldMarginCm = 20.f;
	Safety->bDrawFields = bDrawFields;
	// The forklift's planner clears its route against the exact geometry; fixed structure along it is shaped out of
	// the protective field (people, vehicles and cargo still stop it).
	Safety->bPlannedPathIgnoresMappedStructure = true;
	Safety->bApproachIgnoresMappedStructure = true;
	Safety->ApproachMutedFromXCm = 0.f; // fork root: 115 cm forks end at the footprint's +115
	Safety->RegisterComponent();
	bBuilt = true;
	UE_LOG(LogAgvShadow, Log, TEXT("AGV_SHADOW %s: sensors, localizer and safety attached (measure only)"), *Owner->GetName());
}

void UAgvShadowSensorsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bBuilt)
	{
		Build();
		return;
	}
	if (DeltaTime <= 0.f)
	{
		return;
	}
	Drive->Step(DeltaTime);
	const bool bMoving = FMath::Abs(Drive->SpeedCmS) > 2.f || FMath::Abs(Drive->YawRateDegS) > 1.f;

	// Sleep the LiDARs while parked and settled (as the test vehicle); on waking drop the stale scans.
	// A forklift with work in hand may start any moment: keep its sensors awake (fresh scans before it moves).
	bool bBusy = false;
	if (const AWarehouseForklift* Forklift = Cast<AWarehouseForklift>(GetOwner()))
	{
		using EState = EWarehouseAIState;
		const EState State = Forklift->AIState;
		bBusy = State != EState::Off && State != EState::Ready && State != EState::Charging && State != EState::Paused
			&& State != EState::Fault && State != EState::WaitingCharger;
	}
	ParkedSeconds = bMoving || bBusy ? 0.f : ParkedSeconds + DeltaTime;
	const bool bSettled = Localizer->LastCorrectionCm < 0.5f && Localizer->LastCorrectionDeg < 0.05f;
	if (!bAsleep && ParkedSeconds > 0.5f && bSettled)
	{
		bAsleep = true;
	}
	else if (bAsleep && (bMoving || bBusy))
	{
		bAsleep = false;
		Safety->ResetScans();
	}

	// What the forklift is doing sets the field set: the carried pallet is part of the outline, and docking / fork
	// work / unloading / tight elevator moves use the approach bumper (the vehicle's own task tells it, as on a real AGV).
	// So do the steps between, where it stands at the rack, dock or charger and only settles or plans (lowering the load
	// to travel height right after withdrawing, planning the next route): it is still at the object it worked on.
	if (const AWarehouseForklift* Forklift = Cast<AWarehouseForklift>(GetOwner()))
	{
		using EState = EWarehouseAIState;
		static const EState Approach[] = { EState::SlowApproach, EState::DetectPallet, EState::EstimatePose, EState::AlignVehicle,
			EState::CorrectFork, EState::InsertFork, EState::VerifyInsertion, EState::LiftLoad, EState::VerifyLoad, EState::DepartPickup,
			EState::TravelHeight, EState::PlanDelivery, EState::ReportComplete, EState::PlanCharge, EState::PlanParking,
			EState::RecoverReplan, EState::PlanPickup, EState::ValidateJob, EState::AlignUnload, EState::LowerLoad, EState::WithdrawFork, EState::VerifyUnload, EState::DockCharge, EState::Charging,
			EState::LeaveCharger, EState::RecoverLower, EState::RecoverWithdraw, EState::AlignParking, EState::ElevatorBoard,
			EState::ElevatorRide, EState::ElevatorExit };
		Safety->bApproachMode = false;
		for (const EState State : Approach)
		{
			Safety->bApproachMode |= Forklift->AIState == State;
		}
		// The pallet being worked on (approach only): its outline in the vehicle frame plus 5 cm.
		Safety->ApproachTargetBox = FBox2D(ForceInit);
		const bool bUnloading = Forklift->AIState == EState::AlignUnload || Forklift->AIState == EState::LowerLoad
			|| Forklift->AIState == EState::WithdrawFork || Forklift->AIState == EState::VerifyUnload;
		const bool bCharging = Forklift->AIState == EState::DockCharge || Forklift->AIState == EState::Charging
			|| Forklift->AIState == EState::LeaveCharger;
		const bool bPickupPending = Forklift->TargetPallet && !Forklift->GetSupportedPallet();
		const bool bDelivering = Forklift->GetSupportedPallet() && (Forklift->AIState == EState::PlanDelivery
			|| Forklift->AIState == EState::NavigateDelivery || bUnloading);
		if (Safety->bApproachMode && bCharging && Forklift->ChargingStation)
		{
			// Docking / leaving the charger: the charger is what the vehicle docks against.
			FVector Center, Extent;
			Forklift->ChargingStation->GetActorBounds(true, Center, Extent);
			const FTransform Vehicle = Forklift->GetActorTransform();
			for (const double SX : { -1.0, 1.0 })
			{
				for (const double SY : { -1.0, 1.0 })
				{
					Safety->ApproachTargetBox += FVector2D(Vehicle.InverseTransformPosition(Center + FVector(SX * Extent.X, SY * Extent.Y, 0.0)));
				}
			}
			Safety->ApproachTargetBox = Safety->ApproachTargetBox.ExpandBy(10.0);
		}
		else if (bDelivering)
		{
			// Unloading: the destination slot (pallet size plus its clearance to the rack or the next pallet).
			const FTransform Vehicle = Forklift->GetActorTransform();
			const FTransform& Slot = Forklift->ActiveJob.Destination;
			for (const double SX : { -1.0, 1.0 })
			{
				for (const double SY : { -1.0, 1.0 })
				{
					Safety->ApproachTargetBox += FVector2D(Vehicle.InverseTransformPosition(Slot.TransformPosition(FVector(SX * 75.0, SY * 75.0, 0.0))));
				}
			}
		}
		else if (bPickupPending)
		{
			FVector Center, Extent;
			Forklift->TargetPallet->GetActorBounds(true, Center, Extent);
			const FTransform Vehicle = Forklift->GetActorTransform();
			for (const double SX : { -1.0, 1.0 })
			{
				for (const double SY : { -1.0, 1.0 })
				{
					Safety->ApproachTargetBox += FVector2D(Vehicle.InverseTransformPosition(Center + FVector(SX * Extent.X, SY * Extent.Y, 0.0)));
				}
			}
			Safety->ApproachTargetBox = Safety->ApproachTargetBox.ExpandBy(5.0);
		}
		Safety->LoadBox = FBox2D(ForceInit);
		if (const AWarehousePallet* Pallet = Forklift->GetSupportedPallet())
		{
			// The pallet's outline in the vehicle frame, grown for cargo overhanging it.
			FVector Center, Extent;
			Pallet->GetActorBounds(true, Center, Extent);
			const FTransform Vehicle = Forklift->GetActorTransform();
			for (const double SX : { -1.0, 1.0 })
			{
				for (const double SY : { -1.0, 1.0 })
				{
					Safety->LoadBox += FVector2D(Vehicle.InverseTransformPosition(Center + FVector(SX * Extent.X, SY * Extent.Y, 0.0)));
				}
			}
			Safety->LoadBox = Safety->LoadBox.ExpandBy(LoadOverhangCm);
		}
	}

	// The forklift's planned route ahead, so the fields follow where it will drive (a column the route turns before is
	// not in the way) and end at its next stop (a turn on the spot or a change of direction).
	Safety->ExternalPathAhead.Reset();
	if (const AWarehouseForklift* Forklift = Cast<AWarehouseForklift>(GetOwner()))
	{
		const TArray<FWarehouseRoutePoint>& Route = Forklift->PlannedRoute;
		// Following a route is read from the route itself, not from a list of the forklift's task states: whenever the
		// vehicle stands on its planned route (within OnRouteToleranceCm), whatever it is doing (a job leg, idle parking,
		// a yield ordered by the fleet coordinator, a wait for the safety field to clear), the fields follow that route.
		const double Offset = Drive->ReferenceOffsetCm;
		auto Reference = [Offset](const FTransform& Pose)
		{
			const double Yaw = FMath::DegreesToRadians(Pose.Rotator().Yaw);
			return FVector(FVector2D(Pose.GetLocation()) + AgvMath::Dir(Yaw) * Offset, Yaw);
		};
		FVector2D Here;
		double HereYaw;
		if (!Localizer->GetPose(Here, HereYaw))
		{
			Drive->GetTruePose(Here, HereYaw);
		}
		// The route point the vehicle is at (nearest), then forward to the next pivot or direction change.
		int32 Nearest = 0;
		for (int32 Index = 1; Index < Route.Num(); ++Index)
		{
			if (FVector2D::DistSquared(FVector2D(Reference(Route[Index].Pose)), Here) < FVector2D::DistSquared(FVector2D(Reference(Route[Nearest].Pose)), Here))
			{
				Nearest = Index;
			}
		}
		const bool bOnRoute = Route.Num() > 1 && !Safety->bApproachMode
			&& FVector2D::Distance(FVector2D(Reference(Route[Nearest].Pose)), Here) < OnRouteToleranceCm;
		if (bOnRoute)
		{
			Safety->ExternalPathAhead.Add(FVector(Here, HereYaw));
			for (int32 Index = Nearest + 1; Index < Route.Num(); ++Index)
			{
				if (Route[Index].bPivot || Route[Index].bReverse != Route[Nearest].bReverse)
				{
					break;
				}
				// Dense enough for the sweep (10 cm steps between route points).
				const FVector From = Safety->ExternalPathAhead.Last();
				const FVector To = Reference(Route[Index].Pose);
				const int32 Steps = FMath::Max(1, FMath::CeilToInt32(FVector2D::Distance(FVector2D(From), FVector2D(To)) / 10.0));
				for (int32 Step = 1; Step <= Steps; ++Step)
				{
					const double T = double(Step) / Steps;
					Safety->ExternalPathAhead.Add(FVector(FMath::Lerp(FVector2D(From), FVector2D(To), T), From.Z + AgvMath::Wrap(To.Z - From.Z) * T));
				}
			}
		}
	}

	const double Start = FPlatformTime::Seconds();
	if (!bAsleep)
	{
		for (UAgvLidarComponent* Lidar : Lidars)
		{
			Lidar->Step(DeltaTime);
		}
	}
	Safety->Step(DeltaTime);
	Localizer->Step(DeltaTime);
	UpdateNavigationPose(DeltaTime);
	const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;

	// Hunting on the spot: a turn in place that reverses direction (ignoring creeping yaw below 0.5 deg/s).
	if (FMath::Abs(Drive->SpeedCmS) < 2.f && FMath::Abs(Drive->YawRateDegS) > 0.5f)
	{
		const int32 Sign = Drive->YawRateDegS > 0.f ? 1 : -1;
		TurnReversals += LastTurnSign != 0 && Sign != LastTurnSign;
		LastTurnSign = Sign;
	}
	else if (FMath::Abs(Drive->SpeedCmS) >= 2.f)
	{
		LastTurnSign = 0;
	}

	// Score the estimate against the actual pose (evaluation only; nothing here feeds back).
	FVector2D Estimate, Truth;
	double EstimateYaw, TruthYaw;
	if (Localizer->GetPose(Estimate, EstimateYaw))
	{
		Drive->GetTruePose(Truth, TruthYaw);
		const double Error = FVector2D::Distance(Estimate, Truth);
		MaxErrorCm = FMath::Max(MaxErrorCm, (float)Error);
		MaxHeadingErrorDeg = FMath::Max(MaxHeadingErrorDeg, (float)FMath::Abs(FMath::RadiansToDegrees(AgvMath::Wrap(EstimateYaw - TruthYaw))));
		ErrorSum += Error;
		++ErrorSamples;
		MeanErrorCm = (float)(ErrorSum / ErrorSamples);
		if (bNavValid)
		{
			MaxNavErrorCm = FMath::Max(MaxNavErrorCm, (float)FVector2D::Distance(NavPosition, Truth));
			MaxNavHeadingErrorDeg = FMath::Max(MaxNavHeadingErrorDeg, (float)FMath::Abs(FMath::RadiansToDegrees(AgvMath::Wrap(NavYaw - TruthYaw))));
		}
	}
	if (bMoving)
	{
		MovingSeconds += DeltaTime;
		WarningSeconds += Safety->State == EAgvSafetyState::Warning ? DeltaTime : 0.f;
		StopSeconds += Safety->State == EAgvSafetyState::Stop ? DeltaTime : 0.f;
		SensorMsMovingSum += Ms;
		++MovingFrames;
		MeanSensorMsMoving = (float)(SensorMsMovingSum / MovingFrames);
	}
	const bool bStopped = Safety->State == EAgvSafetyState::Stop;
	if (bStopped && !bWasStopped)
	{
		// Diagnosis: what the protective field saw (the actor around the first point in it).
		const FVector Point = Safety->LastProtectiveHit;
		const FVector World = GetOwner()->GetActorTransform().TransformPosition(Point);
		FString Seen = TEXT("nothing found");
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(AgvShadowDiag), false, GetOwner());
		if (GetWorld()->OverlapMultiByObjectType(Overlaps, World, FQuat::Identity, FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),
			FCollisionShape::MakeSphere(6.f), Query))
		{
			for (const FOverlapResult& Overlap : Overlaps)
			{
				if (const AActor* Hit = Overlap.GetActor())
				{
					Seen = Hit->GetActorNameOrLabel();
					break;
				}
			}
		}
		const AWarehouseForklift* Forklift = Cast<AWarehouseForklift>(GetOwner());
		UE_LOG(LogAgvShadow, Log, TEXT("AGV_SHADOW_STOP %s: %s, speed %.0f cm/s, point (%.0f, %.0f, %.0f) vehicle frame, object %s, task state %d"),
			*GetOwner()->GetName(), *Safety->FieldSet, Drive->SpeedCmS, Point.X, Point.Y, Point.Z, *Seen,
			Forklift ? (int32)Forklift->AIState : -1);
	}
	StopEvents += bStopped && !bWasStopped;
	bWasStopped = bStopped;
	LocalizerStatus = Localizer->StatusText;
	SafetyStatus = Safety->StatusText;
}

float UAgvShadowSensorsComponent::GetSafetySpeedLimitCm() const
{
	return bBuilt && Drive ? Drive->SafetySpeedLimitCmS : TNumericLimits<float>::Max();
}

void UAgvShadowSensorsComponent::UpdateNavigationPose(float Dt)
{
	FVector2D Estimate, Odometry;
	double EstimateYaw, OdometryYaw;
	if (!Localizer->GetPose(Estimate, EstimateYaw))
	{
		bNavValid = false;
		return;
	}
	Localizer->GetOdometryPose(Odometry, OdometryYaw);
	if (bNavValid)
	{
		// The measured motion since the last frame (vehicle frame), applied at the driving pose.
		const FVector2D Moved = AgvMath::Rotate(-LastOdomYaw, Odometry - LastOdomPosition);
		const double Turned = AgvMath::Wrap(OdometryYaw - LastOdomYaw);
		NavPosition += AgvMath::Rotate(NavYaw, Moved);
		NavYaw = AgvMath::Wrap(NavYaw + Turned);
		const FVector2D Offset = Estimate - NavPosition;
		const double YawOffset = AgvMath::Wrap(EstimateYaw - NavYaw);
		if (Offset.Size() > NavSnapCm || FMath::Abs(YawOffset) > FMath::DegreesToRadians((double)NavSnapDeg))
		{
			bNavValid = false;
		}
		else
		{
			const bool bTurningOnTheSpot = Moved.Size() < 2.0 * Dt && FMath::Abs(Turned) > FMath::DegreesToRadians(0.5) * Dt;
			const double YawStep = FMath::DegreesToRadians((double)(bTurningOnTheSpot ? NavTurningCorrectionDegPerS : NavCorrectionDegPerS)) * Dt;
			NavYaw = AgvMath::Wrap(NavYaw + FMath::Clamp(YawOffset, -YawStep, YawStep));
			const double Step = NavCorrectionCmPerS * Dt;
			NavPosition += Offset.Size() > Step ? Offset.GetSafeNormal() * Step : Offset;
		}
	}
	if (!bNavValid)
	{
		NavPosition = Estimate;
		NavYaw = EstimateYaw;
		bNavValid = true;
	}
	LastOdomPosition = Odometry;
	LastOdomYaw = OdometryYaw;
}

bool UAgvShadowSensorsComponent::GetEstimatedActorTransform(FTransform& Out) const
{
	if (!bBuilt || !Localizer || !Drive || !bNavValid || LocalizerStatus == TEXT("LOST"))
	{
		return false;
	}
	const FVector2D Reference = NavPosition;
	const double Yaw = NavYaw;
	const FTransform Actual = GetOwner()->GetActorTransform();
	FRotator Rotation = Actual.Rotator();
	Rotation.Yaw = FMath::RadiansToDegrees(Yaw);
	const FVector2D Origin = Reference - AgvMath::Dir(Yaw) * Drive->ReferenceOffsetCm;
	Out = FTransform(Rotation, FVector(Origin, Actual.GetLocation().Z), Actual.GetScale3D());
	return true;
}

void UAgvShadowSensorsComponent::SetDrawFields(bool bDraw)
{
	bDrawFields = bDraw;
	if (Safety)
	{
		Safety->bDrawFields = bDraw;
	}
}

FString UAgvShadowSensorsComponent::Summary() const
{
	return FString::Printf(TEXT("AGV_SHADOW %s: moving %.0f s, localization error max %.1f cm / mean %.1f cm, heading max %.2f deg, "
		"driving pose max %.1f cm / %.2f deg, turn reversals on the spot %d, "
		"would warn %.1f s and stop %.1f s (%d stops) while moving, sensors %.2f ms/frame moving, localizer %s"),
		*GetOwner()->GetName(), MovingSeconds, MaxErrorCm, MeanErrorCm, MaxHeadingErrorDeg, MaxNavErrorCm, MaxNavHeadingErrorDeg,
		TurnReversals, WarningSeconds, StopSeconds, StopEvents, MeanSensorMsMoving, *LocalizerStatus);
}

// --- Manager ------------------------------------------------------------------------------------------------------

AAgvShadowManager::AAgvShadowManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AAgvShadowManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// The forklifts make their chassis the root in BeginPlay; attach from the first tick, after that, and then once a
	// second to forklifts added later (layout editor).
	SinceAttachCheck += DeltaSeconds;
	if (!bAttached || SinceAttachCheck >= 1.f)
	{
		SinceAttachCheck = 0.f;
		int32 Added = 0;
		for (TActorIterator<AWarehouseForklift> It(GetWorld()); It; ++It)
		{
			if (It->FindComponentByClass<UAgvShadowSensorsComponent>())
			{
				continue;
			}
			UAgvShadowSensorsComponent* Kit = NewObject<UAgvShadowSensorsComponent>(*It, TEXT("AgvShadowSensors"));
			Kit->bDrawFields = bDrawFields;
			Kit->RegisterComponent();
			Kits.Add(Kit);
			++Added;
		}
		if (Added > 0)
		{
			UE_LOG(LogAgvShadow, Log, TEXT("AGV_SHADOW attached to %d forklift(s)"), Added);
		}
		if (!bAttached)
		{
			bAttached = true;
			return;
		}
	}
	SinceReport += DeltaSeconds;
	if (ReportIntervalSeconds > 0.f && SinceReport >= ReportIntervalSeconds)
	{
		SinceReport = 0.f;
		UE_LOG(LogAgvShadow, Log, TEXT("%s"), *Report());
	}
}

void AAgvShadowManager::SetDrawFields(bool bDraw)
{
	bDrawFields = bDraw;
	for (UAgvShadowSensorsComponent* Kit : Kits)
	{
		if (IsValid(Kit))
		{
			Kit->SetDrawFields(bDraw);
		}
	}
}

FString AAgvShadowManager::Report() const
{
	FString Out = FString::Printf(TEXT("AGV_SHADOW report, %d vehicle(s)"), Kits.Num());
	for (const UAgvShadowSensorsComponent* Kit : Kits)
	{
		if (Kit)
		{
			Out += TEXT("\n") + Kit->Summary();
		}
	}
	return Out;
}

// --- Main forklift hook -------------------------------------------------------------------------------------------

// AWarehouseForklift::NavigationTransform is declared in the main forklift (WarehouseForklift.h) and defined here, next
// to the sensors it reads, so the forklift's own files only carry the switch and the calls.
FTransform AWarehouseForklift::NavigationTransform() const
{
	if (bUseAgvLocalization)
	{
		if (const UAgvShadowSensorsComponent* Kit = FindComponentByClass<UAgvShadowSensorsComponent>())
		{
			FTransform Estimate;
			if (Kit->GetEstimatedActorTransform(Estimate))
			{
				return Estimate;
			}
		}
	}
	return GetActorTransform();
}

float AWarehouseForklift::AgvTravelSpeedLimit(FString& OutReason) const
{
	float Limit = TNumericLimits<float>::Max();
	if (!bUseAgvSafety && !bUseAgvLocalization)
	{
		return Limit;
	}
	const UAgvShadowSensorsComponent* Kit = FindComponentByClass<UAgvShadowSensorsComponent>();
	if (!Kit)
	{
		return Limit; // sensors not attached yet (first frame)
	}
	if (bUseAgvLocalization)
	{
		// Lost: the vehicle no longer knows where it is: stop for an operator (a fault, not an obstacle wait).
		if (Kit->LocalizerStatus == TEXT("LOST"))
		{
			OutReason = TEXT("AGV LOCALIZATION LOST");
			return 0.f;
		}
		// Scans not matching for a while: dead reckoning only, so slow down until a match is back.
		if (Kit->LocalizerStatus == TEXT("ODOMETRY ONLY"))
		{
			Limit = FMath::Min(Limit, Kit->OdometryOnlySpeedCm);
		}
	}
	if (bUseAgvSafety)
	{
		const float Field = Kit->GetSafetySpeedLimitCm();
		if (Field <= 0.f)
		{
			OutReason = TEXT("OBSTACLE - AGV SAFETY FIELD"); // waits and resumes as an obstacle
			return 0.f;
		}
		Limit = FMath::Min(Limit, Field);
	}
	return Limit;
}
