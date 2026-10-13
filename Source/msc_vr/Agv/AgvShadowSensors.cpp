#include "AgvShadowSensors.h"
#include "AgvLidarComponent.h"
#include "AgvLidarLocalizerComponent.h"
#include "AgvPath.h"
#include "AgvSafetyComponent.h"
#include "AgvTestVehicle.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "WarehouseForklift.h"
#include "WarehouseAutonomyTypes.h"
#include "WarehousePallet.h"

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
	Safety->ForksFirstMaxSpeedCm = 60.f;
	Safety->WarningFieldLengthCm = 480.f;
	Safety->bDrawFields = bDrawFields;
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
	ParkedSeconds = bMoving ? 0.f : ParkedSeconds + DeltaTime;
	const bool bSettled = Localizer->LastCorrectionCm < 0.5f && Localizer->LastCorrectionDeg < 0.05f;
	if (!bAsleep && ParkedSeconds > 0.5f && bSettled)
	{
		bAsleep = true;
	}
	else if (bAsleep && bMoving)
	{
		bAsleep = false;
		Safety->ResetScans();
	}

	// What the forklift is doing sets the field set: the carried pallet is part of the outline, and docking / fork
	// work / unloading / tight elevator moves use the approach bumper (the vehicle's own task tells it, as on a real AGV).
	if (const AWarehouseForklift* Forklift = Cast<AWarehouseForklift>(GetOwner()))
	{
		using EState = EWarehouseAIState;
		static const EState Approach[] = { EState::SlowApproach, EState::DetectPallet, EState::EstimatePose, EState::AlignVehicle,
			EState::CorrectFork, EState::InsertFork, EState::VerifyInsertion, EState::LiftLoad, EState::VerifyLoad, EState::DepartPickup,
			EState::AlignUnload, EState::LowerLoad, EState::WithdrawFork, EState::VerifyUnload, EState::DockCharge, EState::Charging,
			EState::LeaveCharger, EState::RecoverLower, EState::RecoverWithdraw, EState::AlignParking, EState::ElevatorBoard,
			EState::ElevatorRide, EState::ElevatorExit };
		Safety->bApproachMode = false;
		for (const EState State : Approach)
		{
			Safety->bApproachMode |= Forklift->AIState == State;
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
	const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;

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
	StopEvents += bStopped && !bWasStopped;
	bWasStopped = bStopped;
	LocalizerStatus = Localizer->StatusText;
	SafetyStatus = Safety->StatusText;
}

bool UAgvShadowSensorsComponent::GetEstimatedActorTransform(FTransform& Out) const
{
	FVector2D Reference;
	double Yaw;
	if (!bBuilt || !Localizer || !Drive || !Localizer->GetPose(Reference, Yaw))
	{
		return false;
	}
	const FTransform Actual = GetOwner()->GetActorTransform();
	FRotator Rotation = Actual.Rotator();
	Rotation.Yaw = FMath::RadiansToDegrees(Yaw);
	const FVector2D Origin = Reference - AgvMath::Dir(Yaw) * Drive->ReferenceOffsetCm;
	Out = FTransform(Rotation, FVector(Origin, Actual.GetLocation().Z), Actual.GetScale3D());
	return true;
}

FString UAgvShadowSensorsComponent::Summary() const
{
	return FString::Printf(TEXT("AGV_SHADOW %s: moving %.0f s, localization error max %.1f cm / mean %.1f cm, heading max %.2f deg, "
		"would warn %.1f s and stop %.1f s (%d stops) while moving, sensors %.2f ms/frame moving, localizer %s"),
		*GetOwner()->GetName(), MovingSeconds, MaxErrorCm, MeanErrorCm, MaxHeadingErrorDeg, WarningSeconds, StopSeconds, StopEvents,
		MeanSensorMsMoving, *LocalizerStatus);
}

// --- Manager ------------------------------------------------------------------------------------------------------

AAgvShadowManager::AAgvShadowManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AAgvShadowManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bAttached)
	{
		// The forklifts make their chassis the root in BeginPlay; attach on the first tick, after that.
		for (TActorIterator<AWarehouseForklift> It(GetWorld()); It; ++It)
		{
			UAgvShadowSensorsComponent* Kit = NewObject<UAgvShadowSensorsComponent>(*It, TEXT("AgvShadowSensors"));
			Kit->bDrawFields = bDrawFields;
			Kit->RegisterComponent();
			Kits.Add(Kit);
		}
		bAttached = true;
		UE_LOG(LogAgvShadow, Log, TEXT("AGV_SHADOW attached to %d forklift(s)"), Kits.Num());
		return;
	}
	SinceReport += DeltaSeconds;
	if (ReportIntervalSeconds > 0.f && SinceReport >= ReportIntervalSeconds)
	{
		SinceReport = 0.f;
		UE_LOG(LogAgvShadow, Log, TEXT("%s"), *Report());
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
