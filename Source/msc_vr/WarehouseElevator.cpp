#include "WarehouseElevator.h"
#include "WarehouseForklift.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

AWarehouseElevator::AWarehouseElevator()
{
 PrimaryActorTick.bCanEverTick=true;
 RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("ShaftRoot"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Steel(TEXT("/Game/Warehouse/AGV/Materials/M_Graphite_powder-coated_steel"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Chrome(TEXT("/Game/Warehouse/AGV/Materials/M_Machined_steel"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Yellow(TEXT("/Game/Warehouse/Materials/MI_SafetyYellow"));
 auto Part=[&](FString Name,FVector Position,FVector Size,UMaterialInterface* Material)
 {
  auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(*Name);
  Mesh->SetupAttachment(RootComponent); Mesh->SetStaticMesh(Cube.Object);
  Mesh->SetRelativeLocation(Position); Mesh->SetRelativeScale3D(Size/100.f);
  Mesh->SetMaterial(0,Material); Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
  return Mesh;
 };
 Platform=Part(TEXT("LiftPlatform"),FVector(0,0,-5),FVector(350,240,10),Chrome.Object);
 for (int32 X : {-1,1}) for (int32 Y : {-1,1})
  Part(FString::Printf(TEXT("Column_%d_%d"),X,Y),FVector(X*187,Y*132,560),FVector(18,18,1120),Steel.Object);
 for (int32 F=0; F<3; ++F)
 {
  for (int32 Side : {-1,1})
  {
   Gates.Add(Part(FString::Printf(TEXT("Gate_%d_%d"),F,Side),FVector(-205,Side*60,F*400+135),FVector(6,120,270),Yellow.Object));
   Part(FString::Printf(TEXT("SideFence_%d_%d"),F,Side),FVector(0,Side*127,F*400+145),FVector(360,5,290),Steel.Object);
  }
  Part(FString::Printf(TEXT("BackFence_%d"),F),FVector(181,0,F*400+145),FVector(5,250,290),Steel.Object);
  Part(FString::Printf(TEXT("DoorHeader_%d"),F),FVector(-205,0,F*400+300),FVector(18,276,20),Steel.Object);
 }
 Display=CreateDefaultSubobject<UTextRenderComponent>(TEXT("ElevatorStatus"));
 Display->SetupAttachment(RootComponent); Display->SetRelativeLocation(FVector(-220,0,330));
 Display->SetRelativeRotation(FRotator(0,180,0)); Display->SetHorizontalAlignment(EHTA_Center);
 Display->SetWorldSize(12); Display->SetText(FText::FromString(Status));
}
int32 AWarehouseElevator::FloorAtHeight(float WorldZ) const
{
 const float H=WorldZ-GetActorLocation().Z;
 const int32 Floor=FMath::FloorToInt((H+.5f)/400.f);
 return Floor>=0 && Floor<3 && H-Floor*400<=160.f ? Floor : INDEX_NONE;
}
float AWarehouseElevator::FloorHeight(int32 Floor) const { return GetActorLocation().Z+Floor*400.f; }
FTransform AWarehouseElevator::BoardingPose(int32 Floor) const
{
 return FTransform(GetActorQuat(),GetActorTransform().TransformPosition(FVector(0,0,Floor*400)));
}
FTransform AWarehouseElevator::WaitingPose(int32 Floor) const
{
 return FTransform(GetActorQuat(),GetActorTransform().TransformPosition(FVector(-500,0,Floor*400)));
}
FString AWarehouseElevator::CheckInterlocks() const
{
 if (!bPowerAvailable) return TEXT("ELEVATOR POWER LOST");
 if (!bEStopReleased) return TEXT("ELEVATOR E-STOP");
 if (!bDoorSensorsHealthy) return TEXT("ELEVATOR DOOR SENSOR FAULT");
 if (!bLevelSensorHealthy) return TEXT("ELEVATOR LEVEL SENSOR FAULT");
 if (IsValid(ReservedVehicle) && ReservedVehicle->GetTransferMassKg()>FMath::Min(2000.f,RatedMassKg)) return TEXT("ELEVATOR OVERLOAD (VEHICLE + LOAD)");
 return FString();
}
void AWarehouseElevator::SetState(EWarehouseElevatorState Next,const TCHAR* Message)
{
 State=Next; Status=Message;
 Display->SetText(FText::FromString(FString::Printf(TEXT("L%d -> L%d / 2000 KG\n%s"),CurrentFloor+1,DestinationFloor+1,Message)));
 UE_LOG(LogTemp,Log,TEXT("ELEVATOR_STATE %s / L%d -> L%d"),Message,CurrentFloor+1,DestinationFloor+1);
}
bool AWarehouseElevator::PortalClear() const
{
 FCollisionQueryParams Params(SCENE_QUERY_STAT(LiftLightCurtain),false,this);
 return !GetWorld()->OverlapAnyTestByObjectType(
  GetActorTransform().TransformPosition(FVector(-205,0,CurrentFloor*400+135)),GetActorQuat(),FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),
  FCollisionShape::MakeBox(FVector(15,119,134)),Params);
}
bool AWarehouseElevator::CabinClear() const
{
 FCollisionQueryParams Params(SCENE_QUERY_STAT(LiftCabin),false,this);
 if (IsValid(ReservedVehicle))
 {
  Params.AddIgnoredActor(ReservedVehicle);
  TArray<AActor*> Loads; ReservedVehicle->GetAttachedActors(Loads,true,true); Params.AddIgnoredActors(Loads);
 }
 return !GetWorld()->OverlapAnyTestByObjectType(
  GetActorTransform().TransformPosition(FVector(0,0,PlatformHeight+136)),GetActorQuat(),FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),
  FCollisionShape::MakeBox(FVector(173,118,134)),Params);
}
bool AWarehouseElevator::FitsInside(AWarehouseForklift* Vehicle) const
{
 if (!IsValid(Vehicle)) return false;
 TArray<AActor*> Bodies{Vehicle}; TArray<AActor*> Loads; Vehicle->GetAttachedActors(Loads,true,true); Bodies.Append(Loads);
 for (AActor* Body : Bodies)
 {
  TArray<UStaticMeshComponent*> Parts; Body->GetComponents(Parts);
  for (auto* Part : Parts) if (Part->IsCollisionEnabled())
  {
   FTransform Pose=Part->GetComponentTransform();
   // A rotated wheel's render AABB extends below the tyre (up to sqrt(2)*radius).
   // Circular tyres have the same envelope at every spin angle; use their neutral spin.
   if (Body==Vehicle && Part->GetName().Contains(TEXT("Wheel"))) Pose.SetRotation(Vehicle->GetActorQuat());
   const FBox Box=Part->CalcBounds(Pose.GetRelativeTransform(GetActorTransform())).GetBox();
   if (Box.Min.X < -170 || Box.Max.X>170 || Box.Min.Y < -115 || Box.Max.Y>115 ||
       Box.Min.Z<PlatformHeight-.5f || Box.Max.Z>PlatformHeight+265.f)
   { UE_LOG(LogTemp,Verbose,TEXT("ELEVATOR_ENVELOPE %s %s"),*Part->GetName(),*Box.ToString()); return false; }
  }
 }
 return true;
}
bool AWarehouseElevator::RequestTransfer(AWarehouseForklift* Vehicle,int32 FromFloor,int32 ToFloor)
{
 if (!IsValid(Vehicle) || FromFloor<0 || FromFloor>2 || ToFloor<0 || ToFloor>2 || FromFloor==ToFloor ||
     Vehicle->GetTransferMassKg()>FMath::Min(2000.f,RatedMassKg) || !CheckInterlocks().IsEmpty()) return false;
 if (ReservedVehicle==Vehicle) return EntryFloor==FromFloor && DestinationFloor==ToFloor;
 if (IsValid(ReservedVehicle) || State!=EWarehouseElevatorState::Idle ||
     !Vehicle->GetActorLocation().Equals(WaitingPose(FromFloor).GetLocation(),1.f)) return false;
 ReservedVehicle=Vehicle; EntryFloor=FromFloor; DestinationFloor=ToFloor;
 SetState(EWarehouseElevatorState::ClosingForCall,TEXT("FMS CALL ACCEPTED - CLOSE / LOCK"));
 return true;
}
bool AWarehouseElevator::CanEnter(AWarehouseForklift* Vehicle) const
{
 return ReservedVehicle==Vehicle && IsValid(Vehicle) && State==EWarehouseElevatorState::AwaitBoarding &&
  CurrentFloor==EntryFloor && DoorOpening>=125 && FMath::Abs(PlatformHeight-EntryFloor*400)<.01f && CheckInterlocks().IsEmpty() && CabinClear();
}
bool AWarehouseElevator::CanExit(AWarehouseForklift* Vehicle) const
{
 return ReservedVehicle==Vehicle && IsValid(Vehicle) && State==EWarehouseElevatorState::AwaitExit &&
  CurrentFloor==DestinationFloor && DoorOpening>=125 && FMath::Abs(PlatformHeight-DestinationFloor*400)<.01f && CheckInterlocks().IsEmpty();
}
bool AWarehouseElevator::ConfirmBoarded(AWarehouseForklift* Vehicle)
{
 if (!CanEnter(Vehicle) || !FitsInside(Vehicle) || !PortalClear() ||
     FVector::Dist(Vehicle->GetActorLocation(),BoardingPose(EntryFloor).GetLocation())>1 ||
     FVector::DotProduct(Vehicle->GetActorForwardVector(),GetActorForwardVector())<.9999 ||
     Vehicle->GetActorUpVector().Z<.999f || FMath::Abs(Vehicle->CurrentSpeedCm)>.01f || !Vehicle->IsLiftAtTravelHeight()) return false;
 bBoarded=true;
 SetState(EWarehouseElevatorState::ClosingLoaded,TEXT("BOARDING CONFIRMED - CLOSE / LOCK")); return true;
}
bool AWarehouseElevator::ConfirmExited(AWarehouseForklift* Vehicle)
{
 if (!CanExit(Vehicle) || !Vehicle->GetActorLocation().Equals(WaitingPose(DestinationFloor).GetLocation(),1.f) || !PortalClear()) return false;
 bBoarded=false; ReservedVehicle=nullptr; ++CompletedTransfers;
 SetState(EWarehouseElevatorState::ClosingEmpty,TEXT("EXIT CONFIRMED - RELEASE RESERVATION")); return true;
}
bool AWarehouseElevator::SetDoors(bool Open,float Dt)
{
 if (!Open && !PortalClear()) { Status=TEXT("DOOR LIGHT CURTAIN BLOCKED"); return false; }
 const float Next=FMath::FInterpConstantTo(DoorOpening,Open ? 125.f : 0.f,Dt,60.f);
 FComponentQueryParams Params(SCENE_QUERY_STAT(LiftDoorSweep),this);
 for (int32 I=CurrentFloor*2; I<CurrentFloor*2+2; ++I)
 {
  const FVector Delta=GetActorRightVector()*(I%2 ? 1.f : -1.f)*(Next-DoorOpening);
  TArray<FHitResult> Hits;
  GetWorld()->ComponentSweepMulti(Hits,Gates[I],Gates[I]->GetComponentLocation(),Gates[I]->GetComponentLocation()+Delta,Gates[I]->GetComponentQuat(),Params);
  for (const auto& Hit : Hits) if (Hit.bBlockingHit && (FVector::DotProduct(Delta,Hit.Normal)<-.001f || (Hit.bStartPenetrating && Hit.PenetrationDepth>.2f)))
  { Status=TEXT("DOOR MOTION OBSTRUCTED"); return false; }
 }
 DoorOpening=Next;
 for (int32 I=0; I<Gates.Num(); ++I)
 {
  const int32 Floor=I/2; const float Side=I%2 ? 1.f : -1.f;
  Gates[I]->SetRelativeLocation(FVector(-205,Side*(60+(Floor==CurrentFloor ? DoorOpening : 0)),Floor*400+135));
 }
 return Open ? DoorOpening>=125.f : DoorOpening<=0.f;
}
bool AWarehouseElevator::MovePlatform(int32 Floor,float Dt)
{
 if (DoorOpening>0 || !CabinClear() || (bBoarded && !FitsInside(ReservedVehicle))) return false;
 const float Next=FMath::FInterpConstantTo(PlatformHeight,Floor*400.f,Dt,FMath::Clamp(TravelSpeedCm,1.f,50.f));
 FCollisionQueryParams Params(SCENE_QUERY_STAT(LiftPlatformSweep),false,this);
 if (IsValid(ReservedVehicle))
 {
  Params.AddIgnoredActor(ReservedVehicle);
  TArray<AActor*> Loads; ReservedVehicle->GetAttachedActors(Loads,true,true); Params.AddIgnoredActors(Loads);
 }
 TArray<FHitResult> Hits;
 const FVector Start=Platform->GetComponentLocation(),Delta(0,0,Next-PlatformHeight);
 GetWorld()->SweepMultiByObjectType(Hits,Start,Start+Delta,GetActorQuat(),FCollisionObjectQueryParams(FCollisionObjectQueryParams::AllObjects),FCollisionShape::MakeBox(FVector(174.5,119.5,4.9)),Params);
 for (const auto& Hit : Hits)
  if (Hit.bBlockingHit && FVector::DotProduct(Delta,Hit.Normal)<-.001f)
  { Status=TEXT("PLATFORM PATH OBSTRUCTED"); return false; }
 if (bBoarded) ReservedVehicle->SetActorLocation(ReservedVehicle->GetActorLocation()+FVector(0,0,Next-PlatformHeight));
 PlatformHeight=Next; Platform->SetRelativeLocation(FVector(0,0,PlatformHeight-5));
 if (FMath::Abs(PlatformHeight-Floor*400.f)>.01f) return false;
 CurrentFloor=Floor; return true;
}
void AWarehouseElevator::Tick(float Dt) { Super::Tick(Dt); AdvanceElevator(Dt); }
void AWarehouseElevator::AdvanceElevator(float Seconds)
{
 const float Dt=FMath::Clamp(Seconds,0.f,.05f);
 const FString Fault=CheckInterlocks();
 if (!Fault.IsEmpty()) { Status=Fault; Display->SetText(FText::FromString(Fault)); return; }
 if (IsValid(ReservedVehicle) && (!ReservedVehicle->bPowered || ReservedVehicle->AIState==EWarehouseAIState::SelfCheck || !ReservedVehicle->CheckSystems().IsEmpty())) return;
 if (State!=EWarehouseElevatorState::Idle && State!=EWarehouseElevatorState::ClosingEmpty && !IsValid(ReservedVehicle))
 { Status=TEXT("RESERVED VEHICLE LOST - RECOVERY REQUIRED"); return; }
 switch (State)
 {
 case EWarehouseElevatorState::ClosingForCall:
  if (SetDoors(false,Dt)) SetState(EWarehouseElevatorState::Calling,TEXT("CALLING PLATFORM")); break;
 case EWarehouseElevatorState::Calling:
  if (MovePlatform(EntryFloor,Dt)) SetState(EWarehouseElevatorState::OpeningEntry,TEXT("ARRIVED - DECK LOCK / OPEN")); break;
 case EWarehouseElevatorState::OpeningEntry:
  if (SetDoors(true,Dt)) SetState(EWarehouseElevatorState::AwaitBoarding,TEXT("ENTRY PERMITTED")); break;
 case EWarehouseElevatorState::ClosingLoaded:
  if (FitsInside(ReservedVehicle) && CabinClear() && SetDoors(false,Dt)) SetState(EWarehouseElevatorState::Travelling,TEXT("DOORS LOCKED - FLOOR TRANSFER")); break;
 case EWarehouseElevatorState::Travelling:
  if (MovePlatform(DestinationFloor,Dt)) SetState(EWarehouseElevatorState::OpeningExit,TEXT("DESTINATION ALIGNED - OPEN")); break;
 case EWarehouseElevatorState::OpeningExit:
  if (SetDoors(true,Dt)) SetState(EWarehouseElevatorState::AwaitExit,TEXT("EXIT PERMITTED")); break;
 case EWarehouseElevatorState::ClosingEmpty:
  if (SetDoors(false,Dt)) SetState(EWarehouseElevatorState::Idle,TEXT("READY / 2000 KG MAX")); break;
 default: break;
 }
}
