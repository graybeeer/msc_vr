#include "WarehouseElevator.h"
#include "WarehouseForklift.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "WarehousePhysics.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

namespace
{
bool AtWaitingPose(const AWarehouseForklift* Vehicle,const FTransform& Pose)
{
 const FVector Offset=Vehicle->GetActorLocation()-Pose.GetLocation();
 // Match the route follower's 5cm waiting-point tolerance, while keeping
 // floor level and a stopped vehicle mandatory. Cabin docking stays 1cm.
 return Offset.SizeSquared2D()<25.f && FMath::Abs(Offset.Z)<1.f && FMath::Abs(Vehicle->CurrentSpeedCm)<1.f;
}
void BindGuideFrames(UPhysicsConstraintComponent* Joint,UStaticMeshComponent* Anchor,UStaticMeshComponent* Body,const FTransform& WorldFrame)
{
 // SetConstrainedComponents recalculates both frames from the current physics
 // bodies. Define the shaft's canonical guide explicitly, without inheriting
 // the physical anchor's mesh scale or an incidental component/COM frame.
 Joint->SetWorldTransform(WorldFrame);
 Joint->SetConstrainedComponents(Anchor,NAME_None,Body,NAME_None);
 FTransform AnchorFrame=Anchor->GetComponentTransform();AnchorFrame.RemoveScaling();
 Joint->SetConstraintReferenceFrame(EConstraintFrame::Frame1,WorldFrame.GetRelativeTransform(AnchorFrame));
 Joint->SetConstraintReferenceFrame(EConstraintFrame::Frame2,FTransform::Identity);
}
UPhysicsConstraintComponent* CreateGuide(AActor* Owner,UStaticMeshComponent* Anchor,UStaticMeshComponent* Body,const FTransform& Frame)
{
 auto* Joint=NewObject<UPhysicsConstraintComponent>(Owner);
 Owner->AddInstanceComponent(Joint);Joint->RegisterComponent();
 Joint->SetDisableCollision(true);Joint->SetProjectionEnabled(false);
 Joint->ConstraintInstance.DisableMassConditioning();Joint->ConstraintInstance.DisableParentDominates();
 Joint->SetLinearXLimit(LCM_Locked,0);Joint->SetLinearYLimit(LCM_Locked,0);Joint->SetLinearZLimit(LCM_Locked,0);
 Joint->SetAngularTwistLimit(ACM_Locked,0);Joint->SetAngularSwing1Limit(ACM_Locked,0);Joint->SetAngularSwing2Limit(ACM_Locked,0);
 BindGuideFrames(Joint,Anchor,Body,Frame);
 Joint->SetLinearDriveAccelerationMode(false);
 return Joint;
}
UPhysicsConstraintComponent* CreateDoorGuide(AActor* Owner,UStaticMeshComponent* Anchor,UStaticMeshComponent* Gate,int32 Index,float Opening)
{
 WarehousePhysics::ConfigureContact(Gate,EWarehouseSurface::Steel,80.f);
 if (!Gate->IsSimulatingPhysics()) Gate->SetSimulatePhysics(true);
 const float Side=Index%2 ? 1.f : -1.f;
 const FVector Middle=Owner->GetActorTransform().TransformPosition(FVector(-205,Side*122.5f,(Index/2)*400.f+135.f));
 auto* Joint=CreateGuide(Owner,Anchor,Gate,FTransform(Owner->GetActorQuat(),Middle));
 Joint->SetLinearYLimit(LCM_Limited,62.5f);
 Joint->SetLinearPositionDrive(false,true,false);Joint->SetLinearVelocityDrive(false,true,false);
 Joint->SetLinearDriveParams(12000.f,2500.f,60000.f); // finite 600N door drive.
 Joint->SetLinearPositionTarget(FVector(0,Side*(62.5f-Opening),0));
 return Joint;
}
}

AWarehouseElevator::AWarehouseElevator()
{
 PrimaryActorTick.bCanEverTick=true;
 PrimaryActorTick.TickGroup=TG_PrePhysics;
 Tags.Add(TEXT("WarehousePhysicalRig"));
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
 for (int32 F=0; F<4; ++F)
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
void AWarehouseElevator::OnConstruction(const FTransform& Transform)
{
 Super::OnConstruction(Transform); ConfigureFloors();
}
void AWarehouseElevator::ConfigureFloors()
{
 FloorCount=FMath::Clamp(FloorCount,1,4);
 TArray<UStaticMeshComponent*> Parts; GetComponents(Parts);
 // Older maps serialized six gate references. The new fourth-floor native
 // subobjects still exist, so rebuild the ordered list from component names.
 if (Gates.Num()!=8 || Gates.Contains(nullptr))
 {
  Gates.Reset();
  for (int32 Floor=0;Floor<4;++Floor) for (int32 Side:{-1,1})
  {
   const FString Name=FString::Printf(TEXT("Gate_%d_%d"),Floor,Side);
   if (auto* const* Part=Parts.FindByPredicate([&](const UStaticMeshComponent* P) { return P && P->GetName()==Name; })) Gates.Add(*Part);
  }
 }
 for (auto* Part:Parts)
 {
  const FString Name=Part->GetName();
  if (Name.StartsWith(TEXT("Column_")))
  {
   const EComponentMobility::Type Mobility=Part->Mobility;
   Part->SetMobility(EComponentMobility::Movable);
   const float Height=1120.f+(FloorCount-3)*400.f;
   Part->SetRelativeLocation(FVector(Part->GetRelativeLocation().X,Part->GetRelativeLocation().Y,Height*.5f));
   Part->SetRelativeScale3D(FVector(.18,.18,Height/100.f));
   Part->SetMobility(Mobility);
  }
  for (int32 Floor=0;Floor<4;++Floor)
   if (Name.StartsWith(FString::Printf(TEXT("Gate_%d_"),Floor)) ||
       Name.StartsWith(FString::Printf(TEXT("SideFence_%d_"),Floor)) ||
       Name==FString::Printf(TEXT("BackFence_%d"),Floor) || Name==FString::Printf(TEXT("DoorHeader_%d"),Floor))
   {
    const bool Active=Floor<FloorCount;
    const bool PhysicalGate=HasActorBegunPlay() && Gates.Contains(Part);
    // Retire a dynamic body before disabling its physical collision.
    if (PhysicalGate && !Active && Part->IsSimulatingPhysics()) Part->SetSimulatePhysics(false);
    Part->SetVisibility(Active); Part->SetHiddenInGame(!Active);
    Part->SetCollisionEnabled(Active ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    if (PhysicalGate && Active && !Part->IsSimulatingPhysics()) Part->SetSimulatePhysics(true);
   }
 }
}
bool AWarehouseElevator::SetFloorCount(int32 Count)
{
 if (Count<1 || Count>4 || State!=EWarehouseElevatorState::Idle || IsValid(ReservedVehicle) || CurrentFloor>=Count) return false;
 if (Count==FloorCount && Gates.Num()==8 && !Gates.Contains(nullptr)) return true;
 const int32 PreviousCount=FloorCount;
 FloorCount=Count;ConfigureFloors();
 if (Gates.Num()<FloorCount*2) { FloorCount=PreviousCount;ConfigureFloors();return false; }
 if (HasActorBegunPlay() && GuideAnchor)
 {
  // The always-present ground header is independent of mast height. Existing
  // platform/door guides keep their native bodies and reference frames intact.
  DoorJoints.SetNum(Gates.Num());
  for (int32 I=0;I<Gates.Num();++I)
  {
   auto* Previous=DoorJoints[I].Get();
   if (I>=FloorCount*2) { if (Previous) Previous->BreakConstraint();continue; }
   if (I<PreviousCount*2 && Previous) continue;
   // A dormant leaf receives a fresh native joint through the same creation
   // path as BeginPlay, rather than reusing a broken constraint instance.
   if (Previous) { Previous->BreakConstraint();Previous->DestroyComponent(); }
   DoorJoints[I]=CreateDoorGuide(this,GuideAnchor,Gates[I],I,I/2==CurrentFloor ? DoorOpening : 0.f);
  }
 }
 SetDeckLock(true);return true;
}
void AWarehouseElevator::BeginPlay()
{
 InitialShaftPosition=GetActorLocation();
 Super::BeginPlay();
 ConfigureFloors();
 TArray<UStaticMeshComponent*> Parts; GetComponents(Parts);
 UStaticMeshComponent* Anchor=nullptr;
 for (auto* Part : Parts)
 {
  if (Part->GetName()==TEXT("DoorHeader_0")) Anchor=Part;
  if (WarehousePhysics::IsPhysicalContact(Part)) WarehousePhysics::ConfigureContact(Part,EWarehouseSurface::Steel);
 }
 if (!Anchor) return;
 GuideAnchor=Anchor;
 WarehousePhysics::ConfigureContact(Platform,EWarehouseSurface::Steel,500.f);
 Platform->SetSimulatePhysics(true);
 PlatformJoint=CreateGuide(this,Anchor,Platform,FTransform(GetActorQuat(),GetActorTransform().TransformPosition(FVector(0,0,StrokeHalfHeight()-5.f))));
 PlatformJoint->SetLinearZLimit(LCM_Limited,StrokeHalfHeight());
 PlatformJoint->SetLinearPositionDrive(false,false,true); PlatformJoint->SetLinearVelocityDrive(false,false,true);
 PlatformJoint->SetLinearDriveParams(300000.f,50000.f,4000000.f); // 40kN, includes platform and payload weight.
 PlatformJoint->SetLinearPositionTarget(FVector(0,0,StrokeHalfHeight()));
 SetDeckLock(true);
 DoorJoints.SetNum(Gates.Num());
 for (int32 I=0;I<FloorCount*2;++I)
 {
  DoorJoints[I]=CreateDoorGuide(this,Anchor,Gates[I],I,0.f);
 }
}
int32 AWarehouseElevator::FloorAtHeight(float WorldZ) const
{
 const float H=WorldZ-GetActorLocation().Z;
 const int32 Floor=FMath::FloorToInt((H+.5f)/400.f);
 return Floor>=0 && Floor<FloorCount && H-Floor*400<=160.f ? Floor : INDEX_NONE;
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
void AWarehouseElevator::SetDeckLock(bool Locked)
{
 if (!PlatformJoint || !GuideAnchor) return;
 const FVector AnchorWorld=GetActorTransform().TransformPosition(FVector(0,0,Locked ? CurrentFloor*400.f-5.f : StrokeHalfHeight()-5.f));
 PlatformJoint->SetConstraintReferencePosition(EConstraintFrame::Frame1,GuideAnchor->GetComponentTransform().InverseTransformPositionNoScale(AnchorWorld));
 PlatformJoint->SetLinearZLimit(Locked ? LCM_Locked : LCM_Limited,Locked ? 0.f : StrokeHalfHeight());
 if (Locked) PlatformJoint->SetLinearPositionTarget(FVector::ZeroVector);
}
void AWarehouseElevator::SetState(EWarehouseElevatorState Next,const TCHAR* Message)
{
 State=Next; Status=Message;
 if (Next==EWarehouseElevatorState::OpeningEntry || Next==EWarehouseElevatorState::OpeningExit) SetDeckLock(true);
 Display->SetText(FText::FromString(FString::Printf(TEXT("L%d -> L%d / 2000 KG\n%s"),CurrentFloor+1,DestinationFloor+1,Message)));
 UE_LOG(LogTemp,Log,TEXT("ELEVATOR_STATE %s / L%d -> L%d"),Message,CurrentFloor+1,DestinationFloor+1);
}
bool AWarehouseElevator::PortalClear() const
{
 FCollisionQueryParams Params(SCENE_QUERY_STAT(LiftLightCurtain),false,this);
 return !GetWorld()->OverlapBlockingTestByChannel(
  GetActorTransform().TransformPosition(FVector(-205,0,CurrentFloor*400+135)),GetActorQuat(),ECC_Visibility,
  FCollisionShape::MakeBox(FVector(15,119,134)),Params);
}
bool AWarehouseElevator::CabinClear() const
{
 FCollisionQueryParams Params(SCENE_QUERY_STAT(LiftCabin),false,this);
 if (IsValid(ReservedVehicle))
 {
  Params.AddIgnoredActor(ReservedVehicle);
  Params.AddIgnoredActors(ReservedVehicle->GetPhysicalLoads());
 }
 return !GetWorld()->OverlapBlockingTestByChannel(
  GetActorTransform().TransformPosition(FVector(0,0,PlatformHeight+136)),GetActorQuat(),ECC_Visibility,
  FCollisionShape::MakeBox(FVector(173,118,134)),Params);
}
bool AWarehouseElevator::FitsInside(AWarehouseForklift* Vehicle) const
{
 if (!IsValid(Vehicle)) return false;
 TArray<AActor*> Bodies{Vehicle}; Bodies.Append(Vehicle->GetPhysicalLoads());
 for (AActor* Body : Bodies)
 {
  TArray<UStaticMeshComponent*> Parts; Body->GetComponents(Parts);
  for (auto* Part : Parts) if (WarehousePhysics::IsPhysicalContact(Part))
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
 if (!IsValid(Vehicle) || FromFloor<0 || FromFloor>=FloorCount || ToFloor<0 || ToFloor>=FloorCount || FromFloor==ToFloor ||
     Vehicle->GetTransferMassKg()>FMath::Min(2000.f,RatedMassKg) || !CheckInterlocks().IsEmpty()) return false;
 if (ReservedVehicle==Vehicle) return EntryFloor==FromFloor && DestinationFloor==ToFloor;
 if (IsValid(ReservedVehicle) || State!=EWarehouseElevatorState::Idle ||
     !AtWaitingPose(Vehicle,WaitingPose(FromFloor))) return false;
 ReservedVehicle=Vehicle; EntryFloor=FromFloor; DestinationFloor=ToFloor;
 SetState(EWarehouseElevatorState::ClosingForCall,TEXT("FMS CALL ACCEPTED - CLOSE / LOCK"));
 return true;
}
bool AWarehouseElevator::CanEnter(AWarehouseForklift* Vehicle) const
{
 return ReservedVehicle==Vehicle && IsValid(Vehicle) && State==EWarehouseElevatorState::AwaitBoarding &&
  CurrentFloor==EntryFloor && ActualDoorOpening()>=124 && FMath::Abs(PlatformHeight-EntryFloor*400)<1.f && CheckInterlocks().IsEmpty() && CabinClear();
}
bool AWarehouseElevator::CanExit(AWarehouseForklift* Vehicle) const
{
 return ReservedVehicle==Vehicle && IsValid(Vehicle) && State==EWarehouseElevatorState::AwaitExit &&
  CurrentFloor==DestinationFloor && ActualDoorOpening()>=124 && FMath::Abs(PlatformHeight-DestinationFloor*400)<1.f && CheckInterlocks().IsEmpty();
}
bool AWarehouseElevator::TraceCertifiedPlatformSupport(AWarehouseForklift* Vehicle,FVector Point,FHitResult& Support) const
{
 // The existing PLC handshake certifies this vehicle, this floor, a stationary
 // aligned deck and open doors. It grants no knowledge of other facilities or
 // unknown ground, nor permission for a vehicle merely placed in the cabin.
 if (!IsValid(Platform) || (!CanEnter(Vehicle) && !CanExit(Vehicle)) ||
     FMath::Abs(Point.Z-FloorHeight(CurrentFloor))>2.f || Platform->GetPhysicsLinearVelocity().Size()>2.f) return false;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(CertifiedLiftDeckBearing),false,Vehicle);
 FHitResult Hit;
 if (!Platform->LineTraceComponent(Hit,Point+FVector(0,0,3),Point-FVector(0,0,5),Params) ||
     Hit.ImpactNormal.Z<.98f || FMath::Abs(Hit.ImpactPoint.Z-FloorHeight(CurrentFloor))>1.f) return false;
 Support=Hit;
 return true;
}
bool AWarehouseElevator::ConfirmBoarded(AWarehouseForklift* Vehicle)
{
 if (!CanEnter(Vehicle) || !FitsInside(Vehicle) || !PortalClear() ||
     FVector::Dist(Vehicle->GetActorLocation(),BoardingPose(EntryFloor).GetLocation())>1 ||
     FVector::DotProduct(Vehicle->GetActorForwardVector(),GetActorForwardVector())<.9999 ||
     Vehicle->GetActorUpVector().Z<.995f || FMath::Abs(Vehicle->CurrentSpeedCm)>1.f || !Vehicle->IsLiftAtTravelHeight()) return false;
 bBoarded=true;
 SetState(EWarehouseElevatorState::ClosingLoaded,TEXT("BOARDING CONFIRMED - CLOSE / LOCK")); return true;
}
bool AWarehouseElevator::ConfirmExited(AWarehouseForklift* Vehicle)
{
 if (!CanExit(Vehicle) || !AtWaitingPose(Vehicle,WaitingPose(DestinationFloor)) || !PortalClear()) return false;
 bBoarded=false; ReservedVehicle=nullptr; ++CompletedTransfers;
 SetState(EWarehouseElevatorState::ClosingEmpty,TEXT("EXIT CONFIRMED - RELEASE RESERVATION")); return true;
}
float AWarehouseElevator::ActualDoorOpening() const
{
 float Opening=125.f;
 for (int32 I=CurrentFloor*2;I<CurrentFloor*2+2;++I)
  Opening=FMath::Min(Opening,float(FMath::Abs(GetActorTransform().InverseTransformPosition(Gates[I]->GetComponentLocation()).Y)-60.f));
 return Opening;
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
 for (int32 I=0; I<FloorCount*2; ++I)
 {
  const int32 Floor=I/2; const float Side=I%2 ? 1.f : -1.f;
  const float Opening=Floor==CurrentFloor ? DoorOpening : 0.f;
  if (DoorJoints.IsValidIndex(I)) DoorJoints[I]->SetLinearPositionTarget(FVector(0,Side*(62.5f-Opening),0));
  // Updating a Chaos drive target does not wake a settled native body.
  const float ActualY=GetActorTransform().InverseTransformPosition(Gates[I]->GetComponentLocation()).Y;
  if (FMath::Abs(ActualY-Side*(60.f+Opening))>.1f) Gates[I]->WakeAllRigidBodies();
 }
 if (Open) return ActualDoorOpening()>=124.f;
 // A door can briefly cross the closed sensor while its finite motor still
 // has an open target. Keep commanding zero until both leaves have settled.
 if (DoorOpening>.01f) return false;
 for (int32 I=CurrentFloor*2;I<CurrentFloor*2+2;++I)
 {
  const float Side=I%2 ? 1.f : -1.f;
  const float ActualY=GetActorTransform().InverseTransformPosition(Gates[I]->GetComponentLocation()).Y;
  if (FMath::Abs(ActualY-Side*60.f)>=.5f || Gates[I]->GetPhysicsLinearVelocity().Size()>=2.f) return false;
 }
 return true;
}
bool AWarehouseElevator::MovePlatform(int32 Floor,float Dt)
{
 if (FMath::Abs(ActualDoorOpening())>.5f || !CabinClear() || (bBoarded && !FitsInside(ReservedVehicle))) return false;
 const float Next=FMath::FInterpConstantTo(PlatformHeight,Floor*400.f,Dt,FMath::Clamp(TravelSpeedCm,1.f,50.f));
 FCollisionQueryParams Params(SCENE_QUERY_STAT(LiftPlatformSweep),false,this);
 if (IsValid(ReservedVehicle))
 {
  Params.AddIgnoredActor(ReservedVehicle);
  Params.AddIgnoredActors(ReservedVehicle->GetPhysicalLoads());
 }
 TArray<FHitResult> Hits;
 const FVector Start=Platform->GetComponentLocation(),Delta(0,0,Next-PlatformHeight);
 GetWorld()->SweepMultiByChannel(Hits,Start,Start+Delta,GetActorQuat(),ECC_Visibility,FCollisionShape::MakeBox(FVector(174.5,119.5,4.9)),Params);
 for (const auto& Hit : Hits)
  if (Hit.bBlockingHit && FVector::DotProduct(Delta,Hit.Normal)<-.001f)
  { Status=TEXT("PLATFORM PATH OBSTRUCTED"); return false; }
 SetDeckLock(false);
 PlatformTarget=FMath::FInterpConstantTo(PlatformTarget,Floor*400.f,Dt,FMath::Clamp(TravelSpeedCm,1.f,50.f));
 const float Mass=500.f+(bBoarded && IsValid(ReservedVehicle) ? ReservedVehicle->GetTransferMassKg() : 0.f);
 if (PlatformJoint) PlatformJoint->SetLinearPositionTarget(FVector(0,0,StrokeHalfHeight()-PlatformTarget-Mass*980.f/300000.f));
 Platform->WakeAllRigidBodies(); // Powered stroke; resting floor locks may sleep.
 // Passengers ride deck contact. No actor translation or velocity reset.
 if (FMath::Abs(PlatformHeight-Floor*400.f)>1.f || FMath::Abs(Platform->GetPhysicsLinearVelocity().Z)>2.f) return false;
 CurrentFloor=Floor; return true;
}
void AWarehouseElevator::Tick(float Dt) { Super::Tick(Dt); AdvanceElevator(Dt); }
void AWarehouseElevator::AdvanceElevator(float Seconds)
{
 if (PlatformJoint) PlatformHeight=GetActorTransform().InverseTransformPosition(Platform->GetComponentLocation()).Z+5.f;
 const float Dt=FMath::Clamp(Seconds,0.f,.133333f);
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
