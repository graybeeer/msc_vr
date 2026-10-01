#include "WarehouseChargingStation.h"
#include "WarehouseForklift.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"

AWarehouseChargingStation::AWarehouseChargingStation()
{
 // Keep the contact gap when restoring the original-length chassis.
 const double RearExtension=131.*94./102.-47.2;
 RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("DockPose"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Steel(TEXT("/Game/Warehouse/AGV/Materials/M_Graphite_powder-coated_steel"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Chrome(TEXT("/Game/Warehouse/AGV/Materials/M_Machined_steel"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Green(TEXT("/Game/Warehouse/AGV/Materials/M_Sensor_status_green"));
 auto Part=[&](const TCHAR* Name, FVector Position, FVector Size, UMaterialInterface* Material)
 {
  auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(Name);
  Mesh->SetupAttachment(RootComponent);
  Mesh->SetStaticMesh(Cube.Object);
  Mesh->SetRelativeLocation(Position-FVector(RearExtension,0,0));
  Mesh->SetRelativeScale3D(Size/100.f);
  Mesh->SetMaterial(0,Material);
  Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
 };
 Part(TEXT("Cabinet"),FVector(-76,0,62),FVector(36,65,124),Steel.Object);
 Part(TEXT("ContactPlate"),FVector(-55,0,28),FVector(6,26,14),Chrome.Object);
 Part(TEXT("StatusLight"),FVector(-57,0,106),FVector(3,36,6),Green.Object);
 Display=CreateDefaultSubobject<UTextRenderComponent>(TEXT("ChargeDisplay"));
 Display->SetupAttachment(RootComponent);
 Display->SetRelativeLocation(FVector(-57-RearExtension,0,88));
 Display->SetHorizontalAlignment(EHTA_Center);
 Display->SetWorldSize(7);
 Display->SetText(FText::FromString(TEXT("24V LiFePO4\nE: REQUEST FULL CHARGE")));
}

bool AWarehouseChargingStation::IsDocked(const AWarehouseForklift* Vehicle) const
{
 return IsValid(Vehicle) && FVector::Dist(Vehicle->GetActorLocation(),GetActorLocation())<=1.f &&
  Vehicle->GetActorUpVector().Z>.999f && GetActorUpVector().Z>.999f &&
  FVector::DotProduct(Vehicle->GetActorForwardVector(),GetActorForwardVector())>=FMath::Cos(FMath::DegreesToRadians(.5f));
}
bool AWarehouseChargingStation::TryReserve(AWarehouseForklift* Vehicle)
{
 if (!bMainsPower || (IsValid(Occupant) && Occupant!=Vehicle)) return false;
 Occupant=Vehicle;
 return true;
}
void AWarehouseChargingStation::Release(AWarehouseForklift* Vehicle)
{
 if (Occupant==Vehicle) Occupant=nullptr;
 ShowCharge(0,false);
}
void AWarehouseChargingStation::ShowCharge(float Percent, bool Charging)
{
 Display->SetText(FText::FromString(Charging ? FString::Printf(TEXT("CHARGING %.0f%%\n24V LiFePO4"),Percent) : TEXT("24V LiFePO4\nE: REQUEST FULL CHARGE")));
}
void AWarehouseChargingStation::RequestManualCharge()
{
 if (IsValid(AssignedVehicle)) AssignedVehicle->RequestCharging(true);
}
