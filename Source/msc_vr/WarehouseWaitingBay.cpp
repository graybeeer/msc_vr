#include "WarehouseWaitingBay.h"
#include "WarehouseForklift.h"
#include "WarehouseDamageSystem.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Components/WidgetComponent.h"
#include "WarehouseWorker.h"
#include "WarehousePhysics.h"

AWarehouseWaitingBay::AWarehouseWaitingBay()
{
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ParkingPose")));
 Tags={TEXT("LayoutType:WaitingBay"),TEXT("WarehouseDecoration")};
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Paint(TEXT("/Game/Warehouse/AGV/Materials/M_Sensor_status_green"));
 for (int32 I=0;I<4;++I)
 {
  auto* Part=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("ParkingLine%d"),I));
  Part->SetupAttachment(RootComponent);Part->SetStaticMesh(Cube.Object);Part->SetMaterial(0,Paint.Object);
  Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 }
 auto* Pick=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ParkingSelection"));Pick->SetupAttachment(RootComponent);
 Pick->SetStaticMesh(Cube.Object);Pick->SetVisibility(false);Pick->SetCastShadow(false);
 WarehousePhysics::ConfigureLayoutSelection(Pick);
 auto* Label=CreateDefaultSubobject<UWidgetComponent>(TEXT("ParkingLabel"));Label->SetupAttachment(RootComponent);
 Label->SetRelativeLocation(FVector(0,0,70));Label->SetWidgetSpace(EWidgetSpace::Screen);Label->SetWidgetClass(UWarehouseRoleWidget::StaticClass());
 Label->SetDrawSize(FVector2D(260,42));Label->SetDrawAtDesiredSize(true);Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);Label->SetWindowFocusable(false);
}
void AWarehouseWaitingBay::ConfigureMarkerCollision()
{
 TArray<UStaticMeshComponent*> Parts;GetComponents(Parts);
 for (auto* Part:Parts)
  if (Part->GetFName()==TEXT("ParkingSelection")) WarehousePhysics::ConfigureLayoutSelection(Part);
  else Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
void AWarehouseWaitingBay::OnConstruction(const FTransform& Transform)
{
 Super::OnConstruction(Transform);
 ConfigureMarkerCollision();
 TArray<UStaticMeshComponent*> Parts;GetComponents(Parts);
 for (int32 I=0;I<Parts.Num();++I)
 {
  if (Parts[I]->GetFName()==TEXT("ParkingSelection"))
  { Parts[I]->SetRelativeLocation(FVector(0,0,.1));Parts[I]->SetRelativeScale3D(FVector(SizeCm.X,SizeCm.Y,.2)/100.);continue; }
  const int32 Line=FCString::Atoi(*Parts[I]->GetName().Right(1));
  const bool AlongX=Line<2;const double Side=Line%2 ? 1. : -1.;
  Parts[I]->SetRelativeLocation(AlongX ? FVector(0,Side*SizeCm.Y*.5,.1) : FVector(Side*SizeCm.X*.5,0,.1));
  Parts[I]->SetRelativeScale3D((AlongX ? FVector(SizeCm.X,3,.2) : FVector(3,SizeCm.Y,.2))/100.);
 }
}
void AWarehouseWaitingBay::BeginPlay()
{
 Super::BeginPlay();
 ConfigureMarkerCollision(); // Also repair legacy saved prototypes without resaving the warehouse.
 if (auto* Component=GetComponentByClass<UWidgetComponent>())
 {
  Component->InitWidget();if (auto* Widget=Cast<UWarehouseRoleWidget>(Component->GetUserWidgetObject()))
  { Widget->Label=FText::FromString(BayName);Widget->FontSize=WarehouseUIStyle::CompactSize;Widget->TextColor=WarehouseUIStyle::Accent; }
 }
}
bool AWarehouseWaitingBay::TryReserve(AWarehouseForklift* Vehicle)
{
 if (!IsValid(Vehicle) || Vehicle->GetWorld()!=GetWorld() || (IsValid(Occupant) && Occupant!=Vehicle)) return false;
 const FBox Space=FBox(FVector(-SizeCm.X*.5,-SizeCm.Y*.5,0),FVector(SizeCm.X*.5,SizeCm.Y*.5,240)).TransformBy(GetActorTransform().ToMatrixWithScale());
 for (TActorIterator<AWarehouseForklift> Other(GetWorld());Other;++Other)
  if (*Other!=Vehicle && AWarehouseDamageSystem::GetPhysicalBounds(*Other).Intersect(Space)) return false;
 Occupant=Vehicle;return true;
}
void AWarehouseWaitingBay::Release(AWarehouseForklift* Vehicle)
{ if (Occupant==Vehicle) Occupant=nullptr; }
bool AWarehouseWaitingBay::IsParked(const AWarehouseForklift* Vehicle) const
{
 return IsValid(Vehicle) && FMath::Abs(Vehicle->GetActorLocation().Z-GetActorLocation().Z)<1.f &&
  FVector::Dist2D(Vehicle->GetActorLocation(),GetActorLocation())<=FMath::Clamp(PositionToleranceCm,1.f,10.f) &&
  FMath::Abs(FMath::FindDeltaAngleDegrees(Vehicle->GetActorRotation().Yaw,GetActorRotation().Yaw))<=FMath::Clamp(HeadingToleranceDegrees,1.f,5.f);
}
