#include "AgvTestVehicle.h"
#include "AgvDynamicDriveComponent.h"
#include "AgvLidarComponent.h"
#include "AgvLidarLocalizerComponent.h"
#include "AgvNavigatorComponent.h"
#include "AgvSafetyComponent.h"
#include "AgvPath.h"
#include "WarehousePhysics.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "UObject/ConstructorHelpers.h"

AAgvTestVehicle::AAgvTestVehicle()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup=TG_PrePhysics;
	Tags.Add(TEXT("WarehousePhysicalVehicle"));
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// The teammate's orange AGV (WarehouseForklift on main), forks along local +X, origin at the fork heel face: the same
	// meshes (/Game/Warehouse/AGV/Meshes, the refined model at 215 / 282.55, wheels pivoted at their centres) at the same
	// places. Three wheels: drive-steer under the rear body, fixed support wheels beside the fork heels.
	auto Part = [&](const TCHAR* Name, USceneComponent* Parent, const FVector& Location)
	{
		UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Mesh->SetupAttachment(Parent);
		Mesh->SetStaticMesh(ConstructorHelpers::FObjectFinder<UStaticMesh>(*FString::Printf(TEXT("/Game/Warehouse/AGV/Meshes/SM_Refined_AGV_%s"), Name)).Object);
		Mesh->SetRelativeLocation(Location);
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetCollisionObjectType(ECC_WorldDynamic);
		Mesh->SetCollisionResponseToAllChannels(ECR_Block);
		return Mesh;
	};
	Chassis = Part(TEXT("Body"), RootComponent, FVector::ZeroVector);
	UStaticMeshComponent* LiftStage = Part(TEXT("LiftStage"), Chassis, FVector::ZeroVector);
	Part(TEXT("LiftRam"), LiftStage, FVector::ZeroVector);
	Part(TEXT("LiftPulley"), LiftStage, FVector::ZeroVector);
	Part(TEXT("LiftChains"), Chassis, FVector(0.0, 0.0, 15.0));
	// The carrier sits behind the fork heels, as on the teammate's model.
	USceneComponent* Carriage = CreateDefaultSubobject<USceneComponent>(TEXT("CarriageFrame"));
	Carriage->SetupAttachment(Chassis);
	Part(TEXT("Carriage"), Carriage, FVector(-10.0, 0.0, 0.0));
	Part(TEXT("ForkL"), Carriage, FVector::ZeroVector);
	Part(TEXT("ForkR"), Carriage, FVector::ZeroVector);
	// The drive unit's mesh is modelled in place: hang it off a pivot on its steer axis so it turns about that axis.
	SteerPivot = CreateDefaultSubobject<USceneComponent>(TEXT("SteerPivot"));
	SteerPivot->SetupAttachment(Chassis);
	SteerPivot->SetRelativeLocation(FVector(DriveWheelX, 0.0, 0.0));
	UStaticMeshComponent* DriveUnit=Part(TEXT("DriveSteer"), SteerPivot, FVector(-DriveWheelX, 0.0, 0.0));
	DriveWheel = Part(TEXT("DriveWheel"), SteerPivot, FVector(0.0, 0.0, 15.6));
	SupportWheelL = Part(TEXT("LoadWheelL"), Chassis, FVector(SupportWheelX, SupportWheelY, 8.0));
	SupportWheelR = Part(TEXT("LoadWheelR"), Chassis, FVector(SupportWheelX, -SupportWheelY, 8.0));
	VisualSteerPivot=CreateDefaultSubobject<USceneComponent>(TEXT("VisualSteerPivot"));
	VisualSteerPivot->SetupAttachment(Chassis);
	VisualSteerPivot->SetRelativeLocation(SteerPivot->GetRelativeLocation());
	auto Visual=[&](const TCHAR* Name,UStaticMeshComponent* Source,USceneComponent* Parent)
	{
		auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Mesh->SetupAttachment(Parent);
		Mesh->SetStaticMesh(Source->GetStaticMesh());
		Mesh->SetRelativeTransform(Source->GetRelativeTransform());
		for (int32 I=0;I<Source->GetNumMaterials();++I) Mesh->SetMaterial(I,Source->GetMaterial(I));
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->ComponentTags.Add(TEXT("WarehouseCosmetic"));
		Source->SetVisibility(false);
		return Mesh;
	};
	Visual(TEXT("VisualDriveUnit"),DriveUnit,VisualSteerPivot);
	VisualDriveWheel=Visual(TEXT("VisualDriveWheel"),DriveWheel,VisualSteerPivot);
	VisualSupportWheelL=Visual(TEXT("VisualSupportWheelL"),SupportWheelL,Chassis);
	VisualSupportWheelR=Visual(TEXT("VisualSupportWheelR"),SupportWheelR,Chassis);

	Navigator = CreateDefaultSubobject<UAgvNavigatorComponent>(TEXT("Navigator"));
	Drive = CreateDefaultSubobject<UAgvDynamicDriveComponent>(TEXT("Drive"));
	Localizer = CreateDefaultSubobject<UAgvLidarLocalizerComponent>(TEXT("Localizer"));

	// The support wheels' fixed axle is the non-slip line: the reference point is its middle, and turning on the spot
	// pivots there, swinging the fork tips through a radius of about 1.25 m and the body end about 1.05 m.
	Drive->ReferenceOffsetCm = (float)SupportWheelX;
	Drive->DriveWheelOffsetCm = (float)DriveWheelX;
	Drive->DriveWheelRadiusCm = 15.6f;
	// Centre of mass from a part estimate (refined source cm): battery, drive and electronics in the body (~650 kg at
	// x -30), mast, carriage and forks (~250 kg at x 35), tower and covers (~100 kg at x -20), 70 cm high: 1,000 kg
	// (VNSL14), about 36 cm behind the fork heels. No counterweight: the load hangs in front of the support wheels and
	// the body behind them holds it (RatedLoadKg; the tipping limit is in FORKLIFT_NAVIGATION.md).
	Drive->ChassisMassKg = 1000.f;
	Drive->RatedPayloadKg=RatedLoadKg;
	Drive->ChassisCenterOfMassCm = Refined((650.0 * -30.0 + 250.0 * 35.0 + 100.0 * -20.0) / 1000.0, 0.0, 70.0);
	Drive->ChassisYawInertiaKgM2 = (float)(400.0 * ModelScale * ModelScale);
	// Same tractive and braking force as at the source model's wheel (radius 20.5).
	Drive->MaxDriveTorqueNm = (float)(Drive->MaxDriveTorqueNm * Drive->DriveWheelRadiusCm / 20.5);
	Drive->SafetyBrakeTorqueNm = (float)(Drive->SafetyBrakeTorqueNm * Drive->DriveWheelRadiusCm / 20.5);
	auto Wheel = [](double Y)
	{
		FAgvPassiveWheel Result;
		Result.PositionCm = FVector2D(SupportWheelX, Y);
		Result.RadiusCm = 8.f;
		return Result;
	};
	Drive->PassiveWheels = { Wheel(SupportWheelY), Wheel(-SupportWheelY) };
	// The fork tips move at yaw rate x 1.25 m when turning on the spot; 45 deg/s keeps them near 1 m/s.
	Navigator->MaxYawRateDeg = 45.f;
	// The short wheelbase (51 cm) turns quickly: steer 0.2 s before an arc (0.4 s overshoots it by 5 cm, 0.2 s: 1.3 cm).
	Navigator->CurvaturePreviewSeconds = 0.2f;

	// Sensors at the refined model's positions (Refined()), on the root. Optical centre of the puck on the sensor tower.
	TopLidar = CreateDefaultSubobject<UAgvLidarComponent>(TEXT("TopLidar"));
	TopLidar->SetupAttachment(RootComponent);
	TopLidar->SetRelativeLocation(Refined(-36, -10, 272));
	// Sensor resolutions below are halved/quartered from the first build to keep the ray casting real-time in the
	// warehouse (all five sensors 14 -> about 5 ms per frame); localization and detection are re-checked with them.
	TopLidar->HorizontalResolutionDeg = 0.8f;
	// 5 revolutions per second (16-channel pucks offer 5-20 Hz): half the rays of 10 Hz, same localization accuracy
	// in the warehouse tour (scans are de-skewed with odometry).
	TopLidar->RotationHz = 5.f;

	// Two low safety scanners at the body-end corners (optical belt about 21 cm above the floor), each facing diagonally
	// outward with 270 deg: together they cover the main travel direction (-X) and both sides of the body. The refined
	// model has none modelled; this is where the original model has them, moved out to the refined body's corners.
	auto CornerScanner = [&](const TCHAR* Name, double Y, double Yaw, int32 Seed)
	{
		UAgvLidarComponent* Scanner = CreateDefaultSubobject<UAgvLidarComponent>(Name);
		Scanner->SetupAttachment(RootComponent);
		Scanner->SetRelativeLocationAndRotation(Refined(-73, Y, 27), FRotator(0.0, Yaw, 0.0));
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
		Scanner->NoiseSeed = Seed;
		return Scanner;
	};
	ScannerL = CornerScanner(TEXT("ScannerL"), 47.0, 135.0, 23);
	ScannerR = CornerScanner(TEXT("ScannerR"), -47.0, -135.0, 29);

	// Fork-side 3D obstacle sensors (ToF depth cameras) in the two lenses at the ends of the tower crossbar, looking
	// toward the forks and down: 80 x 50 deg, 20 frames/s, 0.2-6 m. The +Y one sits behind the mast as modelled.
	auto ForkSensor = [&](const TCHAR* Name, double Y)
	{
		UAgvLidarComponent* Sensor = CreateDefaultSubobject<UAgvLidarComponent>(Name);
		Sensor->SetupAttachment(RootComponent);
		Sensor->SetRelativeLocationAndRotation(Refined(-22, Y, 235), FRotator(-30.0, 0.0, 0.0));
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
		return Sensor;
	};
	ForkSensorL = ForkSensor(TEXT("ForkSensorL"), 26.0);
	ForkSensorR = ForkSensor(TEXT("ForkSensorR"), -46.0);

	Safety = CreateDefaultSubobject<UAgvSafetyComponent>(TEXT("Safety"));
	Safety->Scanners = { ScannerL, ScannerR, ForkSensorL, ForkSensorR };
	// Body end (-97.4) to the fork tips (115), across the support wheels (+-49.7), as measured on the teammate's model.
	Safety->Footprint = FBox2D(FVector2D(-97.4, -49.7), FVector2D(115.0, 49.7));
	// Speeds chosen by the user (2026-10-07) above the VNSL14 sheet (1.3 / 0.3 m/s): 1.8 m/s body first, 0.6 m/s forks
	// first (checked with the rated load: no tipping, people stopped for; FORKLIFT_NAVIGATION.md). The wheel limit is
	// above the travel speed so a curve at full speed is not cut by it.
	Navigator->MaxSpeedCm = 180.f;
	Navigator->ForksFirstSpeedCm = 60.f;
	// Carrying a load (after lifting; the vehicle does not travel while lifting): 1.4 m/s body first, the VNSL14
	// loaded / empty ratio (1.0 / 1.3) applied to 1.8 m/s. Forks first stays 0.6 m/s (checked at the rated load).
	Navigator->LoadedMaxSpeedCm = 140.f;
	Safety->ForksFirstMaxSpeedCm = 60.f;
	Drive->MaxWheelSpeedCm = 200.f;
	// The warning field must reach past the protective field at the speed it slows to plus the distance to slow there
	// from full speed: (180^2 - 30^2) / (2 x 50) = 315 cm + 100 cm, with margin.
	Safety->WarningFieldLengthCm = 480.f;
	// Room check for turns on the spot: body and the two forks (outer edges 16-34 cm from the centre line).
	Navigator->PivotFootprint = {
		FBox2D(FVector2D(-97.4, -49.7), FVector2D(0.0, 49.7)),
		FBox2D(FVector2D(0.0, -34.0), FVector2D(115.0, -16.0)),
		FBox2D(FVector2D(0.0, 16.0), FVector2D(115.0, 34.0)) };
}

void AAgvTestVehicle::BeginPlay()
{
	Super::BeginPlay();
	InitializeContactPhysics();
}

void AAgvTestVehicle::InitializeContactPhysics()
{
	Tags.AddUnique(TEXT("WarehousePhysicalVehicle"));
	TArray<UStaticMeshComponent*> Parts;
	GetComponents(Parts);
	Parts.RemoveAll([](UStaticMeshComponent* Part) { return !Part->GetStaticMesh() || Part->ComponentHasTag(TEXT("WarehouseCosmetic")); });
	auto Weight=[](const UStaticMeshComponent* Part)
	{
		const FString Name=Part->GetName();
		if (Name==TEXT("Body")) return 789.f;
		if (Name==TEXT("DriveSteer")) return 40.f;
		if (Name==TEXT("Carriage") || Name==TEXT("LiftStage")) return 30.f;
		if (Name==TEXT("ForkL") || Name==TEXT("ForkR")) return 25.f;
		if (Name==TEXT("DriveWheel")) return 20.f;
		if (Name==TEXT("LiftChains")) return 1.f;
		return 10.f;
	};
	float TotalWeight=0;
	for (auto* Part : Parts) TotalWeight+=Weight(Part);
	for (auto* Part : Parts)
	{
		Part->SetMobility(EComponentMobility::Movable);
		Part->BodyInstance.bAutoWeld=false;
		Part->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		const bool Wheel=Part==DriveWheel || Part==SupportWheelL || Part==SupportWheelR;
		WarehousePhysics::ConfigureContact(Part,Wheel ? EWarehouseSurface::Rubber : EWarehouseSurface::Steel,
			Drive->ChassisMassKg*Weight(Part)/FMath::Max(1.f,TotalWeight));
		if (Wheel)
		{
			// Native normal contact + explicit tyre shear, not two friction solvers on the same contact.
			Part->ComponentTags.AddUnique(TEXT("AgvExplicitTyre"));
			// ConfigureContact has already cached its rubber material in Chaos.
			// Mutating that UObject alone leaves the cached native friction unchanged.
			// A fresh material creates its native handle from these final values.
			auto* Material=NewObject<UPhysicalMaterial>(Part);
			Material->Friction=Material->StaticFriction=0;
			Material->Restitution=.15f;
			Material->bOverrideFrictionCombineMode=true;
			Material->FrictionCombineMode=EFrictionCombineMode::Min;
			Material->bOverrideRestitutionCombineMode=true;
			Material->RestitutionCombineMode=EFrictionCombineMode::Average;
			Part->SetPhysMaterialOverride(Material);
		}
	}
	USceneComponent* PreviousRoot=RootComponent;
	Chassis->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	SetRootComponent(Chassis);
	if (PreviousRoot!=Chassis) PreviousRoot->AttachToComponent(Chassis,FAttachmentTransformRules::KeepWorldTransform);
	Chassis->BodyInstance.bLockXRotation=Chassis->BodyInstance.bLockYRotation=Chassis->BodyInstance.bLockZRotation=false;
	Chassis->BodyInstance.bLockRotation=false;
	Chassis->BodyInstance.bLockTranslation=false;
	Chassis->BodyInstance.bLockXTranslation=Chassis->BodyInstance.bLockYTranslation=Chassis->BodyInstance.bLockZTranslation=false;
	Chassis->BodyInstance.SetDOFLock(EDOFMode::SixDOF);
	Chassis->SetSimulatePhysics(true);
	for (auto* Part : Parts) if (Part!=Chassis) Part->WeldTo(Chassis,NAME_None,true);
	const FVector LocalCenter=GetActorTransform().InverseTransformPosition(Chassis->GetCenterOfMass());
	// The offset moves the chassis member's COM, so scale it by cluster/own mass.
	Chassis->SetCenterOfMass((Drive->ChassisCenterOfMassCm-LocalCenter)*(TotalWeight/Weight(Chassis)));
	Drive->SetPhysicsBody(Chassis);
	UE_LOG(LogTemp,Log,TEXT("AGV_CHAOS_WELD_MASS %s %.3f kg / target %.3f kg, %d physical pieces"),
		*GetName(),Chassis->GetMass(),Drive->ChassisMassKg,Parts.Num());
	ensureMsgf(FMath::Abs(Chassis->GetMass()-Drive->ChassisMassKg)<1.f,TEXT("Prototype welded mass differs from its own-mass ledger"));
}

void AAgvTestVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StepSimulation(DeltaSeconds);
}

void AAgvTestVehicle::StepSimulation(float Dt)
{
	Navigator->Step(Dt);
	UpdateSensorSleep(Dt);
	Drive->Step(Dt);
	UpdateWheelMeshes();
	if (!bSensorsAsleep)
	{
		TopLidar->Step(Dt);
		ScannerL->Step(Dt);
		ScannerR->Step(Dt);
		ForkSensorL->Step(Dt);
		ForkSensorR->Step(Dt);
	}
	Safety->Step(Dt);
	if (!WakeRevolutions.IsEmpty())
	{
		bool bFresh = true;
		for (int32 Index = 0; Index < Safety->Scanners.Num() && Index < WakeRevolutions.Num(); ++Index)
		{
			bFresh &= !Safety->Scanners[Index] || Safety->Scanners[Index]->GetRevolutionCount() > WakeRevolutions[Index];
		}
		if (bFresh)
		{
			WakeRevolutions.Reset();
		}
		else
		{
			Drive->SetSafetySpeedLimit(0.f);
		}
	}
	Localizer->Step(Dt);
}

void AAgvTestVehicle::UpdateSensorSleep(float Dt)
{
	const bool bParked = bSleepSensorsWhenParked && !Navigator->IsNavigating() && FMath::Abs(Drive->SpeedCmS) < 1.f
		&& FMath::Abs(Drive->YawRateDegS) < 0.5f && FMath::Abs(Drive->GetCommandSpeedCmS()) < 0.5 && FMath::Abs(Drive->GetCommandYawRateDegS()) < 0.5;
	ParkedSeconds = bParked ? ParkedSeconds + Dt : 0.f;
	// Not before the estimate has settled (the LiDAR still pulls it in after a wrong start pose, for example).
	const bool bSettled = !Localizer->bUseLidar || (Localizer->LastCorrectionCm < 0.5f && Localizer->LastCorrectionDeg < 0.05f);
	if (!bSensorsAsleep && bParked && bSettled && ParkedSeconds >= SleepAfterSeconds)
	{
		bSensorsAsleep = true;
	}
	else if (bSensorsAsleep && !bParked)
	{
		// Waking: the scan in progress began before the sleep. Drop it and hold the vehicle until each safety scanner
		// has completed a revolution started after this (two counts on: the current partial one, then a full one).
		bSensorsAsleep = false;
		Safety->ResetScans();
		WakeRevolutions.Reset();
		for (const UAgvLidarComponent* Scanner : Safety->Scanners)
		{
			WakeRevolutions.Add(Scanner ? Scanner->GetRevolutionCount() + 1 : 0);
		}
		Drive->SetSafetySpeedLimit(0.f);
	}
}

void AAgvTestVehicle::UpdateWheelMeshes()
{
	// The wheels roll about their local Y axis; the drive unit turns about its steer axis.
	const auto Spin = [](double TravelCm, double RadiusCm)
	{
		return -FMath::Fmod(FMath::RadiansToDegrees(TravelCm / RadiusCm), 360.0);
	};
	VisualSteerPivot->SetRelativeRotation(FRotator(0.0, Drive->SteerAngleDeg, 0.0));
	VisualDriveWheel->SetRelativeRotation(FRotator(Spin(Drive->DriveWheelTravelCm, Drive->DriveWheelRadiusCm), 0.0, 0.0));
	// In the order of the drive's PassiveWheels.
	UStaticMeshComponent* const Meshes[] = { VisualSupportWheelL, VisualSupportWheelR };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Meshes) && Index < Drive->PassiveWheels.Num(); ++Index)
	{
		const FAgvPassiveWheel& Wheel = Drive->PassiveWheels[Index];
		Meshes[Index]->SetRelativeRotation(FRotator(Spin(Wheel.TravelCm, Wheel.RadiusCm), Wheel.bCaster ? Wheel.SwivelDeg : 0.0, 0.0));
	}
}

float AAgvTestVehicle::SimulateUntilIdle(float MaxSeconds, float Dt)
{
	UE_LOG(LogTemp,Warning,TEXT("AGV synchronous stepping cannot advance Chaos. Run an asynchronous PIE world tick."));
	return 0.f;
}

void AAgvTestVehicle::TeleportReference(FVector2D Position, float YawDeg)
{
	if (HasActorBegunPlay())
	{
		UE_LOG(LogTemp,Warning,TEXT("AGV runtime teleport rejected; submit a navigation order instead."));
		return;
	}
	Drive->Halt();
	const FVector2D Origin = Position - AgvMath::Dir(FMath::DegreesToRadians((double)YawDeg)) * Drive->ReferenceOffsetCm;
	SetActorTransform(FTransform(FRotator(0.0,YawDeg,0.0),FVector(Origin.X,Origin.Y,GetActorLocation().Z),GetActorScale3D()));
	Localizer->InitializePose();
	Safety->ResetScans();
}
