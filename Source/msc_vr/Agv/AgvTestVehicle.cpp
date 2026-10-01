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

	// Refined orange AGV (/Game/Warehouse/AGV/Refined), forks along local +X. Each mesh keeps its own pivot;
	// offsets are relative to the parent part, from the FBX hierarchy (see FORKLIFT_NAVIGATION.md).
	auto Part = [&](const TCHAR* Name, USceneComponent* Parent, const FVector& Location)
	{
		UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Mesh->SetupAttachment(Parent);
		Mesh->SetStaticMesh(ConstructorHelpers::FObjectFinder<UStaticMesh>(*FString::Printf(TEXT("/Game/Warehouse/AGV/Refined/SM_AGV_%s"), Name)).Object);
		Mesh->SetRelativeLocation(Location);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return Mesh;
	};
	Chassis = Part(TEXT("Chassis"), RootComponent, FVector::ZeroVector);
	DriveSteer = Part(TEXT("DriveSteer"), Chassis, FVector(-40, 0, 43.5));
	DriveWheel = Part(TEXT("Wheel_Drive"), DriveSteer, FVector(0, 0, -23));
	SupportWheelL = Part(TEXT("Wheel_Support_L"), Chassis, FVector(27, 45, 10.5));
	SupportWheelR = Part(TEXT("Wheel_Support_R"), Chassis, FVector(27, -45, 10.5));
	MastInner = Part(TEXT("MastInner"), Chassis, FVector(27, 0, 32));
	LiftCarriage = Part(TEXT("LiftCarriage"), MastInner, FVector(13, 0, 8));
	Part(TEXT("Fork_L"), LiftCarriage, FVector(-7.5, 31, 0));
	Part(TEXT("Fork_R"), LiftCarriage, FVector(-7.5, -31, 0));
	Part(TEXT("LiftChains"), MastInner, FVector(8, 0, 8));
	Part(TEXT("LiftPulley"), MastInner, FVector(-8, 0, 188));
	Part(TEXT("LiftRam"), MastInner, FVector(-8, 0, 111));
	Part(TEXT("Button_EStop"), Chassis, FVector(-38, -29.4, 199));
	Part(TEXT("Button_HMI_1"), Chassis, FVector(-51.7, -1.5, 158.5));
	Part(TEXT("Button_HMI_2"), Chassis, FVector(-51.7, -11, 158.5));
	Part(TEXT("Button_HMI_3"), Chassis, FVector(-51.7, -20.5, 158.5));

	Navigator = CreateDefaultSubobject<UAgvNavigatorComponent>(TEXT("Navigator"));
	Drive = CreateDefaultSubobject<UAgvDynamicDriveComponent>(TEXT("Drive"));
	Localizer = CreateDefaultSubobject<UAgvLidarLocalizerComponent>(TEXT("Localizer"));

	// Optical centre of the puck on the sensor tower mast (status ring on top at z 279.6).
	TopLidar = CreateDefaultSubobject<UAgvLidarComponent>(TEXT("TopLidar"));
	TopLidar->SetupAttachment(Chassis);
	TopLidar->SetRelativeLocation(FVector(-36, -10, 272));

	// Safety scanner just inside the body front at bumper height, facing the main travel direction (-X):
	// one plane, 270 deg, 0.5 deg, 25 scans/s, 5.5 m.
	FrontScanner = CreateDefaultSubobject<UAgvLidarComponent>(TEXT("FrontScanner"));
	FrontScanner->SetupAttachment(Chassis);
	FrontScanner->SetRelativeLocationAndRotation(FVector(-95, 0, 17), FRotator(0.0, 180.0, 0.0));
	FrontScanner->bUseForLocalization = false;
	FrontScanner->HorizontalFovDeg = 270.f;
	FrontScanner->HorizontalResolutionDeg = 0.5f;
	FrontScanner->Channels = 1;
	FrontScanner->VerticalMinDeg = FrontScanner->VerticalMaxDeg = 0.f;
	FrontScanner->RotationHz = 25.f;
	FrontScanner->MinRangeCm = 5.f;
	FrontScanner->MaxRangeCm = 550.f;
	FrontScanner->RangeNoiseCm = 1.f;
	FrontScanner->DropoutProbability = 0.005f;
	FrontScanner->NoiseSeed = 23;

	Safety = CreateDefaultSubobject<UAgvSafetyComponent>(TEXT("Safety"));
	Safety->Scanner = FrontScanner;

	// No load wheels under the forks: the non-slip point is the middle of the fixed support-wheel axle.
	Drive->ReferenceOffsetCm = 27.f;
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
	FrontScanner->Step(Dt);
	Safety->Step(Dt);
	Localizer->Step(Dt);
}

void AAgvTestVehicle::UpdateWheelMeshes()
{
	// The wheels roll about their local Y axis.
	const auto Roll = [](UStaticMeshComponent* Wheel, double TravelCm, double RadiusCm)
	{
		Wheel->SetRelativeRotation(FRotator(-FMath::Fmod(FMath::RadiansToDegrees(TravelCm / RadiusCm), 360.0), 0.0, 0.0));
	};
	DriveSteer->SetRelativeRotation(FRotator(0.0, Drive->SteerAngleDeg, 0.0));
	Roll(DriveWheel, Drive->DriveWheelTravelCm, Drive->DriveWheelRadiusCm);
	// The drive's passive wheels are the support wheels on local +Y (L) and -Y (R).
	Roll(SupportWheelL, Drive->PassiveWheels[0].TravelCm, Drive->PassiveWheels[0].RadiusCm);
	Roll(SupportWheelR, Drive->PassiveWheels[1].TravelCm, Drive->PassiveWheels[1].RadiusCm);
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
