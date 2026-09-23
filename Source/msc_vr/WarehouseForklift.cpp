#include "WarehouseForklift.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AWarehouseForklift::AWarehouseForklift()
{
	PrimaryActorTick.bCanEverTick = false;
	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("VehicleCollision"));
	// A simple body blocks the player around the chassis, forks and load.
	CollisionBox->InitBoxExtent(FVector(230.f, 68.f, 70.f));
	CollisionBox->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	RootComponent = CollisionBox;

	AddPart(TEXT("Chassis"), FVector(-12, 0, -32), FVector(2.1, 1.15, 0.65));
	AddPart(TEXT("BatteryHousing"), FVector(-58, 0, 12), FVector(0.75, 0.9, 0.7));
	AddPart(TEXT("MastLeft"), FVector(90, -43, 45), FVector(0.13, 0.13, 1.9));
	AddPart(TEXT("MastRight"), FVector(90, 43, 45), FVector(0.13, 0.13, 1.9));
	AddPart(TEXT("MastCrossbar"), FVector(90, 0, 130), FVector(0.15, 1.0, 0.13));
	AddPart(TEXT("ForkLeft"), FVector(170, -35, -64), FVector(1.55, 0.16, 0.1));
	AddPart(TEXT("ForkRight"), FVector(170, 35, -64), FVector(1.55, 0.16, 0.1));
	for (int32 X : {-70, 60})
	{
		for (int32 Y : {-60, 60})
		{
			AddPart(FName(*FString::Printf(TEXT("Wheel_%d_%d"), X, Y)),
				FVector(X, Y, -60), FVector(0.45, 0.2, 0.45), FRotator(90, 0, 0));
		}
	}
	UStaticMeshComponent* CargoPallet = AddPart(TEXT("CargoPallet"), FVector(170, 0, -48), FVector(1.15, 0.8, 0.12));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PalletMesh(
		TEXT("/Game/Scene_Warehouse/Assets/MS/3D/Ind_War_Storage_Pallet_Wood_Worn_01/SM_Ind_War_Storage_Pallet_Wood_Worn_01.SM_Ind_War_Storage_Pallet_Wood_Worn_01"));
	if (PalletMesh.Succeeded())
	{
		CargoPallet->SetStaticMesh(PalletMesh.Object);
		CargoPallet->SetRelativeScale3D(FVector(1.15));
	}

	Beacon = CreateDefaultSubobject<UPointLightComponent>(TEXT("SafetyBeacon"));
	Beacon->SetupAttachment(RootComponent);
	Beacon->SetRelativeLocation(FVector(-50, 0, 82));
	Beacon->SetLightColor(FLinearColor(1.f, 0.45f, 0.02f));
	Beacon->SetIntensity(1800.f);
	Beacon->SetAttenuationRadius(260.f);
}

UStaticMeshComponent* AWarehouseForklift::AddPart(FName Name, FVector Location, FVector Scale, FRotator Rotation)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Part->SetupAttachment(RootComponent);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Part->SetStaticMesh(Name.ToString().StartsWith(TEXT("Wheel")) ? Cylinder.Object : Cube.Object);
	Part->SetRelativeLocation(Location);
	Part->SetRelativeScale3D(Scale);
	Part->SetRelativeRotation(Rotation);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	return Part;
}
