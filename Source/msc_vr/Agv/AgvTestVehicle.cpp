#include "AgvTestVehicle.h"
#include "AgvDynamicDriveComponent.h"
#include "AgvLidarComponent.h"
#include "AgvLidarLocalizerComponent.h"
#include "AgvNavigatorComponent.h"
#include "AgvSafetyComponent.h"
#include "AgvPath.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AAgvTestVehicle::AAgvTestVehicle()
{
	PrimaryActorTick.bCanEverTick = true;
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
		// Solid to queries only (no physics): blocks the vehicle's own sensors and is seen by everyone else's.
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh->SetCollisionObjectType(ECC_WorldDynamic);
		Mesh->SetCollisionResponseToAllChannels(ECR_Block);
		return Mesh;
	};
	Chassis = Part(TEXT("Body"), RootComponent, FVector::ZeroVector);
	UStaticMeshComponent* LiftStage = Part(TEXT("LiftStage"), Chassis, FVector::ZeroVector);
	Part(TEXT("LiftRam"), LiftStage, FVector::ZeroVector);
	Part(TEXT("LiftPulley"), LiftStage, FVector::ZeroVector);
	Part(TEXT("LiftChains"), Chassis, FVector(0.0, 0.0, 15.0))->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
	Part(TEXT("DriveSteer"), SteerPivot, FVector(-DriveWheelX, 0.0, 0.0));
	DriveWheel = Part(TEXT("DriveWheel"), SteerPivot, FVector(0.0, 0.0, 15.6));
	SupportWheelL = Part(TEXT("LoadWheelL"), Chassis, FVector(SupportWheelX, SupportWheelY, 8.0));
	SupportWheelR = Part(TEXT("LoadWheelR"), Chassis, FVector(SupportWheelX, -SupportWheelY, 8.0));

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
	// VNSL14: 0.3 m/s with the forks leading.
	Safety->ForksFirstMaxSpeedCm = 30.f;
}

void AAgvTestVehicle::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	StepSimulation(DeltaSeconds);
}

void AAgvTestVehicle::StepSimulation(float Dt)
{
	Navigator->Step(Dt);
	Drive->Step(Dt);
	UpdateWheelMeshes();
	TopLidar->Step(Dt);
	ScannerL->Step(Dt);
	ScannerR->Step(Dt);
	ForkSensorL->Step(Dt);
	ForkSensorR->Step(Dt);
	Safety->Step(Dt);
	Localizer->Step(Dt);
}

void AAgvTestVehicle::UpdateWheelMeshes()
{
	// The wheels roll about their local Y axis; the drive unit turns about its steer axis.
	const auto Spin = [](double TravelCm, double RadiusCm)
	{
		return -FMath::Fmod(FMath::RadiansToDegrees(TravelCm / RadiusCm), 360.0);
	};
	SteerPivot->SetRelativeRotation(FRotator(0.0, Drive->SteerAngleDeg, 0.0));
	DriveWheel->SetRelativeRotation(FRotator(Spin(Drive->DriveWheelTravelCm, Drive->DriveWheelRadiusCm), 0.0, 0.0));
	// In the order of the drive's PassiveWheels.
	UStaticMeshComponent* const Meshes[] = { SupportWheelL, SupportWheelR };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Meshes) && Index < Drive->PassiveWheels.Num(); ++Index)
	{
		const FAgvPassiveWheel& Wheel = Drive->PassiveWheels[Index];
		Meshes[Index]->SetRelativeRotation(FRotator(Spin(Wheel.TravelCm, Wheel.RadiusCm), Wheel.bCaster ? Wheel.SwivelDeg : 0.0, 0.0));
	}
}

float AAgvTestVehicle::SimulateUntilIdle(float MaxSeconds, float Dt)
{
	float Elapsed = 0.f;
	while (Navigator->IsNavigating() && Elapsed < MaxSeconds && Dt > 0.f)
	{
		StepSimulation(Dt);
		Elapsed += Dt;
	}
	return Elapsed;
}

void AAgvTestVehicle::TeleportReference(FVector2D Position, float YawDeg)
{
	Drive->Halt();
	const FVector2D Origin = Position - AgvMath::Dir(FMath::DegreesToRadians((double)YawDeg)) * Drive->ReferenceOffsetCm;
	SetActorLocationAndRotation(FVector(Origin.X, Origin.Y, GetActorLocation().Z), FRotator(0.0, YawDeg, 0.0));
	Localizer->InitializePose();
	Safety->ResetScans();
}
