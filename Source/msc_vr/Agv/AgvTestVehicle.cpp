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

	// Five-wheel orange AGV, forks along local +X: the original model's parts (/Game/Warehouse/AGV/Original5) plus the
	// refined model's drive-steer unit under the middle of the chassis. Wheel parts are pivoted at their centres,
	// everything else at the vehicle origin (see FORKLIFT_NAVIGATION.md).
	auto Part = [&](const TCHAR* Name, const TCHAR* Asset, USceneComponent* Parent, const FVector& Location)
	{
		UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Mesh->SetupAttachment(Parent);
		Mesh->SetStaticMesh(ConstructorHelpers::FObjectFinder<UStaticMesh>(Asset).Object);
		Mesh->SetRelativeLocation(Location);
		// Solid to queries only (no physics): blocks the vehicle's own sensors and is seen by everyone else's.
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Mesh->SetCollisionObjectType(ECC_WorldDynamic);
		Mesh->SetCollisionResponseToAllChannels(ECR_Block);
		return Mesh;
	};
	Chassis = Part(TEXT("Body"), TEXT("/Game/Warehouse/AGV/Original5/SM_AGV5_Body"), RootComponent, FVector::ZeroVector);
	LiftStage = Part(TEXT("LiftStage"), TEXT("/Game/Warehouse/AGV/Original5/SM_AGV5_LiftStage"), Chassis, FVector::ZeroVector);
	LiftCarriage = Part(TEXT("Carriage"), TEXT("/Game/Warehouse/AGV/Original5/SM_AGV5_Carriage"), LiftStage, FVector::ZeroVector);
	Part(TEXT("Forks"), TEXT("/Game/Warehouse/AGV/Original5/SM_AGV5_Forks"), LiftCarriage, FVector::ZeroVector);

	// Wheels: drive-steer (middle), casters (rear corners), fixed load rollers (fork legs). Positions match PassiveWheels.
	DriveSteer = Part(TEXT("DriveSteer"), TEXT("/Game/Warehouse/AGV/Refined/SM_AGV_DriveSteer"), Chassis, FVector(DriveWheelX, 0, 43.5));
	DriveWheel = Part(TEXT("DriveWheel"), TEXT("/Game/Warehouse/AGV/Refined/SM_AGV_Wheel_Drive"), DriveSteer, FVector(0, 0, -23));
	RearWheelL = Part(TEXT("RearWheelL"), TEXT("/Game/Warehouse/AGV/Original5/SM_AGV5_RearWheelL"), Chassis, FVector(-58, 34, 17));
	RearWheelR = Part(TEXT("RearWheelR"), TEXT("/Game/Warehouse/AGV/Original5/SM_AGV5_RearWheelR"), Chassis, FVector(-58, -34, 17));
	LoadRollerL = Part(TEXT("LoadRollerL"), TEXT("/Game/Warehouse/AGV/Original5/SM_AGV5_LoadRollerL"), Chassis, FVector(153, 31, 9.8));
	LoadRollerR = Part(TEXT("LoadRollerR"), TEXT("/Game/Warehouse/AGV/Original5/SM_AGV5_LoadRollerR"), Chassis, FVector(153, -31, 9.8));

	Navigator = CreateDefaultSubobject<UAgvNavigatorComponent>(TEXT("Navigator"));
	Drive = CreateDefaultSubobject<UAgvDynamicDriveComponent>(TEXT("Drive"));
	Localizer = CreateDefaultSubobject<UAgvLidarLocalizerComponent>(TEXT("Localizer"));

	// The load rollers' fixed axle is the non-slip line: the reference point is its middle, and turning on the spot
	// pivots there, swinging the body end through a radius of about 2.5 m. Wheels sit on the ground at their radius
	// (the model's roller centre is 0.7 cm and its rear wheel centre 1 cm above its own floor).
	Drive->ReferenceOffsetCm = 153.f;
	Drive->DriveWheelOffsetCm = DriveWheelX;
	// Centre of mass from a part estimate: battery, drive and electronics in the body (~550 kg at x -30), mast,
	// carriage and forks (~250 kg at x 35), fork legs (~100 kg at x 90), tower and covers (~100 kg at x -20).
	Drive->ChassisCenterOfMassCm = FVector(0.0, 0.0, 70.0);
	Drive->ChassisYawInertiaKgM2 = 400.f;
	// The stabiliser casters are spring-loaded (a fifth of the stiffness), so the drive wheel and the load rollers
	// carry the weight and the drive wheel keeps its grip, as on real stackers.
	auto Wheel = [](double X, double Y, double Radius, bool bCaster)
	{
		FAgvPassiveWheel Result;
		Result.PositionCm = FVector2D(X, Y);
		Result.RadiusCm = (float)Radius;
		Result.bCaster = bCaster;
		Result.Stiffness = bCaster ? 0.2f : 1.f;
		return Result;
	};
	Drive->PassiveWheels = { Wheel(-58, 34, 17, true), Wheel(-58, -34, 17, true), Wheel(153, 31, 9.8, false), Wheel(153, -31, 9.8, false) };
	// The body end moves at yaw rate x 2.5 m when turning on the spot; 25 deg/s keeps it near 1.1 m/s.
	Navigator->MaxYawRateDeg = 25.f;

	// Optical centre of the puck on the sensor tower mast.
	TopLidar = CreateDefaultSubobject<UAgvLidarComponent>(TEXT("TopLidar"));
	TopLidar->SetupAttachment(Chassis);
	TopLidar->SetRelativeLocation(FVector(-36, -10, 277));

	// The model's two low safety scanners at the body-end corners (optical belt 27 cm above the floor), each facing
	// diagonally outward with 270 deg: together they cover the main travel direction (-X) and both sides of the body.
	auto CornerScanner = [&](const TCHAR* Name, double Y, double Yaw, int32 Seed)
	{
		UAgvLidarComponent* Scanner = CreateDefaultSubobject<UAgvLidarComponent>(Name);
		Scanner->SetupAttachment(Chassis);
		Scanner->SetRelativeLocationAndRotation(FVector(-73, Y, 27), FRotator(0.0, Yaw, 0.0));
		Scanner->bUseForLocalization = false;
		Scanner->HorizontalFovDeg = 270.f;
		Scanner->HorizontalResolutionDeg = 0.5f;
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
	ScannerL = CornerScanner(TEXT("ScannerL"), 41.0, 135.0, 23);
	ScannerR = CornerScanner(TEXT("ScannerR"), -41.0, -135.0, 29);

	// Fork-side 3D obstacle sensors (ToF depth cameras) in the two lenses at the ends of the tower crossbar, looking
	// toward the forks and down: 80 x 50 deg, 20 frames/s, 0.2-6 m. The +Y one sits behind the mast as modelled.
	auto ForkSensor = [&](const TCHAR* Name, double Y)
	{
		UAgvLidarComponent* Sensor = CreateDefaultSubobject<UAgvLidarComponent>(Name);
		Sensor->SetupAttachment(Chassis);
		Sensor->SetRelativeLocationAndRotation(FVector(-22, Y, 235), FRotator(-30.0, 0.0, 0.0));
		Sensor->bUseForLocalization = false;
		Sensor->HorizontalFovDeg = 80.f;
		Sensor->HorizontalResolutionDeg = 1.5f;
		Sensor->Channels = 30;
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
	Safety->Footprint = FBox2D(FVector2D(-94.0, -54.0), FVector2D(182.0, 54.0));
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
	// The wheels roll about their local Y axis; casters first swivel about the vertical into their direction of travel.
	const auto Spin = [](double TravelCm, double RadiusCm)
	{
		return -FMath::Fmod(FMath::RadiansToDegrees(TravelCm / RadiusCm), 360.0);
	};
	DriveSteer->SetRelativeRotation(FRotator(0.0, Drive->SteerAngleDeg, 0.0));
	DriveWheel->SetRelativeRotation(FRotator(Spin(Drive->DriveWheelTravelCm, Drive->DriveWheelRadiusCm), 0.0, 0.0));
	// In the order of the drive's PassiveWheels.
	UStaticMeshComponent* const Meshes[] = { RearWheelL, RearWheelR, LoadRollerL, LoadRollerR };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Meshes) && Index < Drive->PassiveWheels.Num(); ++Index)
	{
		const FAgvPassiveWheel& Wheel = Drive->PassiveWheels[Index];
		if (Meshes[Index])
		{
			Meshes[Index]->SetRelativeRotation(FRotator(Spin(Wheel.TravelCm, Wheel.RadiusCm), Wheel.bCaster ? Wheel.SwivelDeg : 0.0, 0.0));
		}
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
}
