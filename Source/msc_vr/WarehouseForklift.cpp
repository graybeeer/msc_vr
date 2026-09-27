#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "UObject/ConstructorHelpers.h"

AWarehouseForklift::AWarehouseForklift()
{
 PrimaryActorTick.bCanEverTick = true;
 RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
 Carriage = CreateDefaultSubobject<USceneComponent>(TEXT("Carriage"));
 Carriage->SetupAttachment(RootComponent);
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Yellow(TEXT("/Game/Warehouse/Materials/MI_SafetyYellow"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Steel(TEXT("/Game/Warehouse/Materials/MI_DarkSteel"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Rubber(TEXT("/Game/Warehouse/Materials/MI_Rubber"));
 auto Part = [&](FName Name, FVector P, FVector Size, bool Moving=false, bool Wheel=false)
 {
  auto* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
  Mesh->SetupAttachment(Moving ? Carriage.Get() : RootComponent.Get());
  Mesh->SetStaticMesh(Wheel ? Cylinder.Object : Cube.Object);
  Mesh->SetMaterial(0,Wheel ? Rubber.Object : (Name==TEXT("Chassis") || Name==TEXT("Battery") ? Yellow.Object : Steel.Object));
  Mesh->SetRelativeLocation(P);
  Mesh->SetRelativeScale3D(Size/100.f);
  if (Wheel) Mesh->SetRelativeRotation(FRotator(0,0,90));
  Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
 };
 Part(TEXT("Chassis"), FVector(-40,0,45), FVector(150,100,50));
 Part(TEXT("Battery"), FVector(-70,0,92), FVector(80,86,44));
 Part(TEXT("SensorTower"), FVector(-60,0,133), FVector(28,28,36));
 Part(TEXT("Lidar"), FVector(-60,0,157), FVector(35,35,12));
 for (int Side : {-1,1})
 {
  Part(FName(*FString::Printf(TEXT("Mast%d"),Side)), FVector(45,Side*48,120), FVector(10,10,200));
  Part(FName(*FString::Printf(TEXT("Fork%d"),Side)), FVector(110,Side*22,7.5), FVector(120,8,4),true);
  for (int X : {-90,10}) Part(FName(*FString::Printf(TEXT("Wheel%d_%d"),X,Side)), FVector(X,Side*55,25.5), FVector(50,50,16),false,true);
 }
 Part(TEXT("MastTop"), FVector(45,0,218), FVector(12,106,8));
 Part(TEXT("ForkBack"), FVector(44,0,45), FVector(8,85,75),true);
 Part(TEXT("LoadGuard"), FVector(44,0,98), FVector(8,85,8),true);
 Beacon = CreateDefaultSubobject<UPointLightComponent>(TEXT("Beacon"));
 Beacon->SetupAttachment(RootComponent);
 Beacon->SetRelativeLocation(FVector(-70,0,175));
 Beacon->SetLightColor(FLinearColor(1.f,.4f,0.f));
 Beacon->SetIntensity(0);
 Beacon->SetAttenuationRadius(250);
 Display = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Status"));
 Display->SetupAttachment(RootComponent);
 Display->SetRelativeLocation(FVector(-118,0,165));
 Display->SetRelativeRotation(FRotator(0,180,0));
 Display->SetHorizontalAlignment(EHTA_Center);
 Display->SetWorldSize(12);
 Display->SetText(FText::FromString(Status));
}

void AWarehouseForklift::SetStatus(const FString& Message)
{
 Status = Message;
 Display->SetText(FText::FromString(Message));
 Beacon->SetIntensity(bPowered ? 1800 : 0);
}
void AWarehouseForklift::StopFor(const FString& Reason)
{
 bPowered=false;
 SetStatus(Reason + TEXT("\nE : RETRY / RESUME"));
}
void AWarehouseForklift::TogglePower()
{
 if (bPowered) { StopFor(TEXT("PAUSED")); return; }
 if (!IsValid(TargetPallet)) { StopFor(TEXT("NO TARGET PALLET")); return; }
 if (State == EWarehouseCycle::Complete) { SetStatus(TEXT("COMPLETE - RESTART LEVEL")); return; }
 if (State == EWarehouseCycle::Idle) { StartLocation=GetActorLocation(); State=EWarehouseCycle::Approach; }
 bPowered=true;
 SetStatus(TEXT("RUNNING - KEEP CLEAR\nE : PAUSE"));
}
void AWarehouseForklift::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 AdvanceSimulation(DeltaSeconds);
}
bool AWarehouseForklift::ClearToMove(FVector Delta, bool LiftOnly)
{
 // Safety envelope includes both the chassis and exposed forks. Pawn includes players and human NPCs.
 FHitResult PersonHit;
 FCollisionObjectQueryParams People(ECC_Pawn);
 FCollisionQueryParams SafetyParams(SCENE_QUERY_STAT(PersonSafety),false,this);
 const FVector SafetyCenter=GetActorLocation()+GetActorForwardVector()*30+FVector(0,0,110);
 const FVector SafetyEnd=SafetyCenter+(LiftOnly ? FVector::ZeroVector : Delta+Delta.GetSafeNormal()*100);
 if (GetWorld()->SweepSingleByObjectType(PersonHit,SafetyCenter,SafetyEnd,GetActorQuat(),People,
     FCollisionShape::MakeBox(FVector(145,85,110)),SafetyParams))
 {
  StopFor(TEXT("PERSON IN SAFETY ZONE"));
  return false;
 }
 TArray<UStaticMeshComponent*> Parts;
 GetComponents(Parts);
 if (bSupportingPallet && IsValid(TargetPallet))
 {
  TArray<UStaticMeshComponent*> LoadParts;
  TargetPallet->GetComponents(LoadParts);
  Parts.Append(LoadParts);
 }
 FComponentQueryParams Params(SCENE_QUERY_STAT(ForkliftSweep),this);
 if (bSupportingPallet) Params.AddIgnoredActor(TargetPallet);
 for (auto* Part : Parts)
 {
  if (LiftOnly && Part->GetOwner()==this && Part->GetAttachParent()!=Carriage) continue;
  TArray<FHitResult> Hits;
  GetWorld()->ComponentSweepMulti(Hits,Part,Part->GetComponentLocation(),Part->GetComponentLocation()+Delta,Part->GetComponentQuat(),Params);
  for (const FHitResult& Hit : Hits)
  {
   if (Hit.bBlockingHit && (FVector::DotProduct(Delta,Hit.Normal)<-.001f || (Hit.bStartPenetrating && Hit.PenetrationDepth>.2f)))
   {
    StopFor(TEXT("OBSTACLE / CONTACT"));
    return false;
   }
  }
 }
 return true;
}
bool AWarehouseForklift::MoveVehicle(FVector Delta)
{
 if (!ClearToMove(Delta,false)) return false;
 SetActorLocation(GetActorLocation()+Delta);
 return true;
}
bool AWarehouseForklift::MoveLift(float Height)
{
 if (!ClearToMove(FVector(0,0,Height-LiftOffset),true)) return false;
 LiftOffset=Height;
 Carriage->SetRelativeLocation(FVector(0,0,Height));
 return true;
}
void AWarehouseForklift::AdvanceSimulation(float DeltaSeconds)
{
 if (!bPowered) return;
 if (!IsValid(TargetPallet)) { StopFor(TEXT("TARGET LOST")); return; }
 const float Dt=FMath::Clamp(DeltaSeconds,0.f,.05f);
 const FVector Forward=GetActorForwardVector();
 switch(State)
 {
 case EWarehouseCycle::Approach:
 {
  const FVector Offset=TargetPallet->GetActorLocation()-GetActorLocation();
  if (FMath::Abs(FVector::DotProduct(Offset,GetActorRightVector()))>5 ||
      FVector::DotProduct(Forward,TargetPallet->GetActorForwardVector())<FMath::Cos(FMath::DegreesToRadians(2.f)))
  { StopFor(TEXT("ALIGN PALLET")); break; }
  const float Remaining=FVector::DotProduct(Offset,Forward)-120;
  if (Remaining < -1) { StopFor(TEXT("TARGET TOO CLOSE")); break; }
  if (Remaining>.01f) { MoveVehicle(Forward*FMath::Min(Remaining,45*Dt)); break; }
  if (!TargetPallet->CanEngage(GetActorTransform())) { StopFor(TEXT("FORK INSERTION FAILED")); break; }
  State=EWarehouseCycle::Lift;
  SetStatus(TEXT("LIFTING"));
  break;
 }
 case EWarehouseCycle::Lift:
  if (!bSupportingPallet)
  {
   // Both tines must be fully inserted before the underside contact is accepted.
   if (!TargetPallet->CanEngage(GetActorTransform())) { StopFor(TEXT("FORK INSERTION FAILED")); break; }
   if (LiftOffset<2.45f) { MoveLift(FMath::Min(2.45f,LiftOffset+15*Dt)); break; }
   bSupportingPallet=TargetPallet->AttachToComponent(Carriage,FAttachmentTransformRules::KeepWorldTransform);
   if (!bSupportingPallet) { StopFor(TEXT("SUPPORT FAILED")); break; }
  }
  if (MoveLift(FMath::Min(50.f,LiftOffset+20*Dt)) && LiftOffset>=50) { State=EWarehouseCycle::Reverse; SetStatus(TEXT("REVERSING")); }
  break;
 case EWarehouseCycle::Reverse:
 {
  const FVector Delta=StartLocation-GetActorLocation();
  if (Delta.Size()>.01f) MoveVehicle(Delta.GetClampedToMaxSize(35*Dt));
  else { State=EWarehouseCycle::Lower; SetStatus(TEXT("LOWERING")); }
  break;
 }
 case EWarehouseCycle::Lower:
  if (bSupportingPallet)
  {
   if (!MoveLift(FMath::Max(2.45f,LiftOffset-20*Dt))) break;
   if (LiftOffset<=2.45f) { TargetPallet->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform); bSupportingPallet=false; }
  }
  else if (MoveLift(FMath::Max(0.f,LiftOffset-15*Dt)) && LiftOffset<=0)
  { State=EWarehouseCycle::Withdraw; WithdrawStart=GetActorLocation(); SetStatus(TEXT("WITHDRAWING")); }
  break;
 case EWarehouseCycle::Withdraw:
 {
  const float Remaining=150-FVector::Dist(WithdrawStart,GetActorLocation());
  if (Remaining>.01f) MoveVehicle(-Forward*FMath::Min(Remaining,35*Dt));
  else { State=EWarehouseCycle::Complete; bPowered=false; SetStatus(TEXT("CYCLE COMPLETE")); }
  break;
 }
 default: break;
 }
}
