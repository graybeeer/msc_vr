#include "WarehouseConveyor.h"

#include "WarehouseCargo.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "WarehousePalletLift.h"
#include "WarehousePhysics.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
 FBox ConveyorBounds(const AActor* Frame,AActor* Cargo)
 {
  FBox Local(ForceInit);
  if (!IsValid(Cargo)) return Local;
  TInlineComponentArray<UStaticMeshComponent*> Parts(Cargo);
  for (UStaticMeshComponent* Part:Parts) if (WarehousePhysics::IsPhysicalContact(Part))
   if (const UBodySetup* Body=Part->GetBodySetup();Body && Body->AggGeom.GetElementCount()>0)
    Local+=Body->AggGeom.CalcAABB(Part->GetComponentTransform().GetRelativeTransform(Frame->GetActorTransform()));
  if (Local.IsValid) return Local;
  const FBox World=AWarehouseDamageSystem::GetPhysicalBounds(Cargo);
  if (World.IsValid) for (int32 X=0;X<2;++X) for (int32 Y=0;Y<2;++Y) for (int32 Z=0;Z<2;++Z)
   Local+=Frame->GetActorTransform().InverseTransformPosition(FVector(X ? World.Max.X : World.Min.X,Y ? World.Max.Y : World.Min.Y,Z ? World.Max.Z : World.Min.Z));
  return Local;
 }
 float ConveyorMass(const TArray<AActor*>& Actors)
 {
  TSet<const FBodyInstance*> Seen;float Mass=0.f;
  for (AActor* Actor:Actors) if (IsValid(Actor))
  {
   TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);
   for (UPrimitiveComponent* Part:Parts) if (WarehousePhysics::IsPhysicalContact(Part))
   {
    FBodyInstance* Body=Part->GetBodyInstance(NAME_None,true);while (Body && Body->WeldParent) Body=Body->WeldParent;
    if (Body && Body->IsValidBodyInstance() && !Seen.Contains(Body)) { Seen.Add(Body);Mass+=Body->GetBodyMass(); }
   }
  }
  return Mass;
 }
}

AWarehouseConveyor::AWarehouseConveyor()
{
 PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PrePhysics;
 RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("ConveyorRoot"));
 Frame=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MachineFrame"));Frame->SetupAttachment(RootComponent);
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> FrameModel(TEXT("/Game/Warehouse/Conveyor/Meshes/SM_Conveyor_Frame"));
 Frame->SetStaticMesh(Cube.Object);Frame->SetRelativeLocation(FVector(0,-65,29));Frame->SetRelativeScale3D(FVector(3,.06,.12));
 Frame->SetCollisionProfileName(TEXT("BlockAllDynamic"));
 if (FrameModel.Get()) { Frame->SetStaticMesh(FrameModel.Get());Frame->SetRelativeLocation(FVector::ZeroVector);Frame->SetRelativeScale3D(FVector::OneVector); }
 Tags.Add(TEXT("WarehouseMapStructure"));Tags.Add(TEXT("FixedWarehouseConveyor"));Tags.Add(TEXT("WarehousePhysicalRig"));
 Display=CreateDefaultSubobject<UTextRenderComponent>(TEXT("MachineStatus"));Display->SetupAttachment(RootComponent);
 Display->SetHorizontalAlignment(EHTA_Center);Display->SetWorldSize(10);Display->SetRelativeLocation(FVector(0,-74,70));Display->SetRelativeRotation(FRotator(0,-90,0));
}

void AWarehouseConveyor::OnConstruction(const FTransform& Transform)
{ Super::OnConstruction(Transform);RebuildConveyor(); }

bool AWarehouseConveyor::RebuildConveyor()
{
 if (HasActorBegunPlay() && !IsIdleForLayout())
 { SetStatus(EWarehouseConveyorState::Stopped,TEXT("화물·장애물 또는 승강기 인계가 있어 규격을 바꿀 수 없습니다"));return false; }
 for (UPhysicsConstraintComponent* Joint:RollerJoints) if (Joint) Joint->DestroyComponent();RollerJoints.Reset();
 if (RotaryJoint) { RotaryJoint->DestroyComponent();RotaryJoint=nullptr; }
 for (UStaticMeshComponent* Part:GeneratedParts) if (Part) Part->DestroyComponent();GeneratedParts.Reset();Rollers.Reset();
 RotaryCarrier=nullptr;
 if (Kind==EWarehouseConveyorKind::RotaryTransfer) return BuildRotaryConveyor();
 LengthCm=FMath::Clamp(LengthCm,125.f,1200.f);WidthCm=FMath::Clamp(WidthCm,50.f,150.f);
 SurfaceHeightCm=FMath::Clamp(SurfaceHeightCm,20.f,150.f);
 UStaticMesh* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube"));
 UStaticMesh* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder"));
 UStaticMesh* FrameModel=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Warehouse/Conveyor/Meshes/SM_Conveyor_Frame"));
 if (auto* RollerModel=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Warehouse/Conveyor/Meshes/SM_Conveyor_Roller"))) Cylinder=RollerModel;
 UMaterialInterface* Steel=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Warehouse/Materials/MI_DarkSteel"));
 UMaterialInterface* Yellow=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Warehouse/Materials/MI_SafetyYellow"));
 Frame->SetStaticMesh(FrameModel ? FrameModel : Cube);
 Frame->SetRelativeLocation(FrameModel ? FVector::ZeroVector : FVector(0,-WidthCm*.5f-3.f,SurfaceHeightCm-9.f));
 Frame->SetRelativeScale3D(FrameModel ? FVector(LengthCm/300.f,WidthCm/125.f,SurfaceHeightCm/40.f) : FVector(LengthCm,6,12)/100.f);
 if (!FrameModel && Steel) Frame->SetMaterial(0,Steel);
 auto Box=[&](const FString& Name,FVector Location,FVector Size,UMaterialInterface* Material)
 {
  auto* Part=NewObject<UStaticMeshComponent>(this,*Name);AddInstanceComponent(Part);Part->SetupAttachment(RootComponent);
  Part->SetStaticMesh(Cube);Part->SetRelativeLocation(Location);Part->SetRelativeScale3D(Size/100.f);
  Part->SetCollisionProfileName(TEXT("BlockAllDynamic"));if (Material) Part->SetMaterial(0,Material);Part->RegisterComponent();
  WarehousePhysics::ConfigureContact(Part,EWarehouseSurface::Steel);GeneratedParts.Add(Part);return Part;
 };
 if (!FrameModel)
 {
  Box(TEXT("OppositeSideRail"),FVector(0,WidthCm*.5f+3.f,SurfaceHeightCm-9.f),FVector(LengthCm,6,12),Steel);
  const int32 Legs=FMath::Max(2,FMath::CeilToInt(LengthCm/180.f));
  for (int32 Index=0;Index<Legs;++Index) for (int32 Side:{-1,1})
  {
   const float X=FMath::Lerp(-LengthCm*.5f+15.f,LengthCm*.5f-15.f,Index/float(Legs-1));
   Box(FString::Printf(TEXT("Leg_%d_%d"),Index,Side),FVector(X,Side*(WidthCm*.5f+3.f),(SurfaceHeightCm-15.f)*.5f),FVector(6,6,SurfaceHeightCm-15.f),Steel);
   Box(FString::Printf(TEXT("Foot_%d_%d"),Index,Side),FVector(X,Side*(WidthCm*.5f+3.f),1.5f),FVector(15,12,3),Steel);
  }
 }
 const float Radius=Kind==EWarehouseConveyorKind::PalletRoller ? 3.f : 2.f;
 const float Pitch=Kind==EWarehouseConveyorKind::PalletRoller ? 9.f : 5.f;
 // Tangent carton rollers rub in opposite directions at their mutual contact.
 // Leave a real gap; floor rounding never shrinks the 5cm centre spacing.
 const float Intervals=(LengthCm-2.f*Radius)/Pitch;
 const int32 Count=(Kind==EWarehouseConveyorKind::CartonBelt ? FMath::FloorToInt(Intervals) : FMath::CeilToInt(Intervals))+1;
 for (int32 Index=0;Index<Count;++Index)
 {
  auto* Roller=NewObject<UStaticMeshComponent>(this,*FString::Printf(TEXT("PoweredRoller_%d"),Index));
  AddInstanceComponent(Roller);Roller->SetupAttachment(RootComponent);Roller->SetStaticMesh(Cylinder);
  const float X=FMath::Lerp(-LengthCm*.5f+Radius,LengthCm*.5f-Radius,Index/float(Count-1));
  Roller->SetRelativeLocation(FVector(X,0,SurfaceHeightCm-Radius));Roller->SetRelativeRotation(FRotator(0,0,90));
  Roller->SetRelativeScale3D(FVector(Radius*2.f/100.f,Radius*2.f/100.f,WidthCm/100.f));
  if (Kind==EWarehouseConveyorKind::CartonBelt && Steel) Roller->SetMaterial(0,Steel);
  Roller->SetCollisionProfileName(TEXT("PhysicsActor"));Roller->RegisterComponent();GeneratedParts.Add(Roller);Rollers.Add(Roller);
 }
 // Narrow returns and guards stay below the conveying surface; no invisible carrying plane.
 if (!FrameModel)
 {
  Box(TEXT("DriveMotorHousing"),FVector(-LengthCm*.5f+24.f,WidthCm*.5f+14.f,SurfaceHeightCm-14.f),FVector(28,22,18),Steel);
  for (int32 End:{-1,1})
  {
   Box(FString::Printf(TEXT("PhotoeyeBracket_%d"),End),FVector(End*(LengthCm*.5f-35.f),-WidthCm*.5f-8.f,SurfaceHeightCm+8.f),FVector(5,8,12),Yellow);
   Box(FString::Printf(TEXT("Photoeye_%d"),End),FVector(End*(LengthCm*.5f-35.f),-WidthCm*.5f-3.f,SurfaceHeightCm+10.f),FVector(3,3,4),Steel);
  }
 }
 if (Kind==EWarehouseConveyorKind::CartonBelt)
 {
  // Real, visible parcel guides. Their open ends allow transfer while the
  // 0.5cm crown clearance keeps rotating roller ends from rubbing the guides.
  // Pallet equipment deliberately retains its open fork approach geometry.
  for (int32 Side:{-1,1})
   Box(FString::Printf(TEXT("CartonSideGuide_%d"),Side),FVector(0,Side*(WidthCm*.5f+2.f),SurfaceHeightCm+6.25f),FVector(LengthCm-4.f,2.f,11.5f),Steel);
 }
 Display->SetRelativeLocation(FVector(0,-WidthCm*.5f-15.f,SurfaceHeightCm+24.f));
 Display->SetText(FText::FromString(Kind==EWarehouseConveyorKind::PalletRoller ? TEXT("PALLET ROLLER") : TEXT("CARTON ROLLER")));
 if (HasActorBegunPlay()) MakeRollerJoints();
 return true;
}

bool AWarehouseConveyor::BuildRotaryConveyor()
{
 LengthCm=200.f;WidthCm=200.f;SurfaceHeightCm=40.f;
 RotaryPhase=EWarehouseRotaryPhase::Empty;RotaryLoad=nullptr;RotaryPayload.Reset();
 RotaryTargetYaw=CarrierYawDegrees=RotaryRateDegrees=RotaryStableSeconds=RotaryNoProgressSeconds=RotaryLastProgressYaw=0.f;
 UStaticMesh* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder"));
 UStaticMesh* BaseModel=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Warehouse/Conveyor/Meshes/SM_Conveyor_TurntableBase"));
 UStaticMesh* RotorModel=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Warehouse/Conveyor/Meshes/SM_Conveyor_TurntableRotor"));
 UStaticMesh* RollerModel=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Warehouse/Conveyor/Meshes/SM_Conveyor_Roller"));
 Frame->SetStaticMesh(BaseModel ? BaseModel : Cylinder);
 Frame->SetRelativeLocation(BaseModel ? FVector::ZeroVector : FVector(0,0,11));
 Frame->SetRelativeScale3D(BaseModel ? FVector::OneVector : FVector(2.3f,2.3f,.22f));
 Frame->SetCollisionProfileName(TEXT("BlockAllDynamic"));
 RotaryCarrier=NewObject<UStaticMeshComponent>(this,TEXT("RotaryCarrier"));AddInstanceComponent(RotaryCarrier);
 RotaryCarrier->SetupAttachment(RootComponent);RotaryCarrier->SetStaticMesh(RotorModel ? RotorModel : Cylinder);
 RotaryCarrier->SetRelativeLocation(FVector(0,0,RotorModel ? 35.f : 27.5f));
 RotaryCarrier->SetRelativeScale3D(RotorModel ? FVector::OneVector : FVector(2.f,2.f,.05f));
 RotaryCarrier->SetCollisionProfileName(TEXT("PhysicsActor"));RotaryCarrier->RegisterComponent();GeneratedParts.Add(RotaryCarrier);
 for (int32 Index=0;Index<21;++Index)
 {
  const float X=-95.f+Index*9.5f;
  const float Width=2.f*FMath::Sqrt(10000.f-FMath::Square(FMath::Abs(X)+3.f))-4.f;
  auto* Roller=NewObject<UStaticMeshComponent>(this,*FString::Printf(TEXT("RotaryRoller_%d"),Index));AddInstanceComponent(Roller);
  Roller->SetupAttachment(RootComponent);Roller->SetStaticMesh(RollerModel ? RollerModel : Cylinder);
  Roller->SetRelativeLocation(FVector(X,0,37));Roller->SetRelativeRotation(FRotator(0,0,90));
  Roller->SetRelativeScale3D(FVector(.06f,.06f,Width/100.f));Roller->SetCollisionProfileName(TEXT("PhysicsActor"));
  Roller->RegisterComponent();GeneratedParts.Add(Roller);Rollers.Add(Roller);
 }
 Display->SetRelativeLocation(FVector(0,-125,65));Display->SetText(FText::FromString(TEXT("90 DEG ROTARY TRANSFER")));
 if (HasActorBegunPlay()) { MakeRotaryJoint();MakeRollerJoints(); }
 return true;
}

FTransform AWarehouseConveyor::SurfaceFrame() const
{
 return FTransform(Kind==EWarehouseConveyorKind::RotaryTransfer && RotaryCarrier ? RotaryCarrier->GetComponentQuat() : GetActorQuat(),GetActorLocation());
}

float AWarehouseConveyor::RollerRadius() const
{ return Kind==EWarehouseConveyorKind::CartonBelt ? 2.f : 3.f; }

void AWarehouseConveyor::MakeRotaryJoint()
{
 if (RotaryJoint || !RotaryCarrier) return;
 WarehousePhysics::ConfigureContact(RotaryCarrier,EWarehouseSurface::Steel,80.f);RotaryCarrier->SetSimulatePhysics(true);
 RotaryJoint=NewObject<UPhysicsConstraintComponent>(this);AddInstanceComponent(RotaryJoint);RotaryJoint->RegisterComponent();
 const FQuat Axle=FRotationMatrix::MakeFromXZ(GetActorUpVector(),GetActorForwardVector()).ToQuat();
 RotaryJoint->SetWorldTransform(FTransform(Axle,RotaryCarrier->GetComponentLocation()));
 RotaryJoint->SetDisableCollision(true);RotaryJoint->SetProjectionEnabled(false);
 RotaryJoint->ConstraintInstance.DisableMassConditioning();RotaryJoint->ConstraintInstance.DisableParentDominates();
 RotaryJoint->SetLinearXLimit(LCM_Locked,0);RotaryJoint->SetLinearYLimit(LCM_Locked,0);RotaryJoint->SetLinearZLimit(LCM_Locked,0);
 RotaryJoint->SetAngularTwistLimit(ACM_Free,0);RotaryJoint->SetAngularSwing1Limit(ACM_Locked,0);RotaryJoint->SetAngularSwing2Limit(ACM_Locked,0);
 RotaryJoint->SetConstrainedComponents(Frame,NAME_None,RotaryCarrier,NAME_None);
 RotaryJoint->SetAngularDriveMode(EAngularDriveMode::TwistAndSwing);RotaryJoint->SetAngularDriveAccelerationMode(false);
 RotaryJoint->SetAngularVelocityDriveTwistAndSwing(true,false);RotaryJoint->SetAngularDriveParams(0,20000000.f,10000000.f); // Finite 1000 Nm.
}

void AWarehouseConveyor::DriveRotary(float Dt)
{
 if (Kind!=EWarehouseConveyorKind::RotaryTransfer || !RotaryCarrier || !RotaryJoint) return;
 CarrierYawDegrees=FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,RotaryCarrier->GetComponentRotation().Yaw);
 const float Error=FMath::FindDeltaAngleDegrees(CarrierYawDegrees,RotaryTargetYaw);
 const bool Allowed=bEnabled && bEStopReleased &&
  (State==EWarehouseConveyorState::Running || State==EWarehouseConveyorState::Stopped || State==EWarehouseConveyorState::PickupReady);
 const float Desired=Allowed ? FMath::Clamp(Error*2.f,-30.f,30.f) : 0.f;
 RotaryRateDegrees=FMath::FInterpConstantTo(RotaryRateDegrees,Desired,Dt,20.f);
 RotaryJoint->SetAngularVelocityTarget(FVector(-RotaryRateDegrees/360.f,0,0));
 if (FMath::Abs(RotaryRateDegrees)>.01f) RotaryCarrier->WakeAllRigidBodies();
}

void AWarehouseConveyor::UpdateRotary(float Seconds,const TArray<AActor*>& Loads,bool Foreign,int32 Count)
{
 if (!RotaryCarrier || !RotaryJoint)
 { SetStatus(EWarehouseConveyorState::Fault,TEXT("회전 이송대 구동부가 없습니다"));return; }
 if (!bEnabled || !bEStopReleased)
 { SetStatus(EWarehouseConveyorState::Stopped,bEnabled ? TEXT("회전 이송대 비상 정지") : TEXT("회전 이송대 전원 꺼짐"));return; }
 if (Foreign || Count>1)
 { SetStatus(EWarehouseConveyorState::DownstreamBlocked,Foreign ? TEXT("회전 반경 안 사람·차량·장애물 / 정지") : TEXT("회전대에는 한 묶음만 적재하세요"));return; }
 if (!FMath::IsFinite(MeasuredMassKg) || MeasuredMassKg>FMath::Clamp(RatedMassKg,1.f,2000.f))
 { SetStatus(EWarehouseConveyorState::Overloaded,TEXT("회전 이송대 정격 하중 초과"));return; }
 if (Lift)
 { SetStatus(EWarehouseConveyorState::Fault,TEXT("승강기는 직선 연결 컨베이어를 통해 연결하세요"));return; }
 const float ActualRate=FMath::RadiansToDegrees(FVector::DotProduct(RotaryCarrier->GetPhysicsAngularVelocityInRadians(),GetActorUpVector()));
 const float TurnYaw=bTurnRight ? 90.f : -90.f;
 if (RotaryPhase==EWarehouseRotaryPhase::Empty)
 {
  if (!ZoneLoad) { SetStatus(EWarehouseConveyorState::Stopped,TEXT("입구 이송 대기"));return; }
  RotaryLoad=ZoneLoad;RotaryPhase=EWarehouseRotaryPhase::Centering;RotaryStableSeconds=0.f;
 }
 if (RotaryPhase!=EWarehouseRotaryPhase::Resetting && !IsValid(RotaryLoad))
 { SetStatus(EWarehouseConveyorState::Fault,TEXT("회전 중 화물이 삭제되었습니다 / 작업자 확인"));return; }
 if (RotaryPhase==EWarehouseRotaryPhase::Centering)
 {
  const FBox Bounds=ConveyorBounds(this,RotaryLoad);
  if (!Bounds.IsValid) { SetStatus(EWarehouseConveyorState::Fault,TEXT("화물 충돌 형상을 확인하세요"));return; }
  const float Error=-Bounds.GetCenter().X;
  CommandSpeedCm=FMath::Abs(Error)>1.f ? FMath::Clamp(Error*1.1f,-FMath::Min(SpeedCm,20.f),FMath::Min(SpeedCm,20.f)) : 0.f;
  SetStatus(EWarehouseConveyorState::Running,TEXT("회전 중심으로 실제 롤러 이송"));
  if (CommandSpeedCm!=0.f) { RotaryStableSeconds=0.f;return; }
  if (!Loads.Contains(RotaryLoad)) { SetStatus(EWarehouseConveyorState::Fault,TEXT("화물이 회전대 롤러에서 벗어났습니다"));return; }
  bool Fits=true;
  for (AActor* Object:Loads)
  {
   const FBox Box=ConveyorBounds(this,Object);
   if (!Box.IsValid || Box.Min.Z<SurfaceHeightCm-1.f || Box.Max.Z>SurfaceHeightCm+220.f) { Fits=false;break; }
   for (double X:{Box.Min.X,Box.Max.X}) for (double Y:{Box.Min.Y,Box.Max.Y}) if (X*X+Y*Y>95.f*95.f) Fits=false;
  }
  if (!Fits) { SetStatus(EWarehouseConveyorState::DownstreamBlocked,TEXT("화물 전체가 회전 안전 반경95cm 안에 있어야 합니다"));return; }
  AWarehouseConveyor* Next=FindNext();
  if (!Next || !Next->CanReceiveLoad(RotaryLoad))
  { SetStatus(EWarehouseConveyorState::DownstreamBlocked,TEXT("90도 출구 연결·수신 구간을 확인하세요"));return; }
  UPrimitiveComponent* Body=RotaryLoad->FindComponentByClass<UPrimitiveComponent>();
  if (!Body || Body->GetPhysicsLinearVelocity().Size()>2.f || Body->GetPhysicsAngularVelocityInDegrees().Size()>1.f || FMath::Abs(ActualRate)>.5f)
  { RotaryStableSeconds=0.f;return; }
  RotaryStableSeconds+=Seconds;if (RotaryStableSeconds<.5f) return;
  RotaryPayload.Reset();for (AActor* Object:Loads) RotaryPayload.Add(Object);
  RotaryTargetYaw=TurnYaw;RotaryPhase=EWarehouseRotaryPhase::Rotating;RotaryStableSeconds=0.f;
  RotaryNoProgressSeconds=0.f;RotaryLastProgressYaw=CarrierYawDegrees;
 }
 if (RotaryPhase==EWarehouseRotaryPhase::Rotating || RotaryPhase==EWarehouseRotaryPhase::Resetting)
 {
  SetStatus(EWarehouseConveyorState::Running,RotaryPhase==EWarehouseRotaryPhase::Rotating ? TEXT("유한 모터 토크로90도 회전") : TEXT("빈 회전대 입구 방향 복귀"));
  if (FMath::Abs(FMath::FindDeltaAngleDegrees(RotaryLastProgressYaw,CarrierYawDegrees))>.2f)
  { RotaryLastProgressYaw=CarrierYawDegrees;RotaryNoProgressSeconds=0.f; }
  else RotaryNoProgressSeconds+=Seconds;
  if (RotaryNoProgressSeconds>10.f && FMath::Abs(FMath::FindDeltaAngleDegrees(CarrierYawDegrees,RotaryTargetYaw))>.5f)
  { SetStatus(EWarehouseConveyorState::Fault,TEXT("회전 구동이 막혔습니다 / 간섭·하중 확인"));return; }
  if (FMath::Abs(FMath::FindDeltaAngleDegrees(CarrierYawDegrees,RotaryTargetYaw))>.5f || FMath::Abs(ActualRate)>.5f)
  { RotaryStableSeconds=0.f;return; }
  RotaryStableSeconds+=Seconds;if (RotaryStableSeconds<.5f) return;
  if (RotaryPhase==EWarehouseRotaryPhase::Resetting)
  { RotaryPhase=EWarehouseRotaryPhase::Empty;RotaryLoad=nullptr;RotaryPayload.Reset();SetStatus(EWarehouseConveyorState::Stopped,TEXT("회전 이송 완료 / 다음 화물 대기"));return; }
  RotaryPhase=EWarehouseRotaryPhase::Discharging;RotaryStableSeconds=0.f;
 }
 if (RotaryPhase==EWarehouseRotaryPhase::Discharging)
 {
  bool Inside=false;
  for (AActor* Object:RotaryPayload)
  {
   if (!IsValid(Object)) { SetStatus(EWarehouseConveyorState::Fault,TEXT("이송 화물이 삭제되었습니다 / 작업자 확인"));return; }
   const FBox Box=ConveyorBounds(this,Object);if (!Box.IsValid) continue;
   const double X=FMath::Clamp(0.,Box.Min.X,Box.Max.X),Y=FMath::Clamp(0.,Box.Min.Y,Box.Max.Y);
   if (X*X+Y*Y<=10000. && Box.Max.Z>=SurfaceHeightCm-1.f && Box.Min.Z<=SurfaceHeightCm+220.f) Inside=true;
  }
  if (!Inside && Loads.IsEmpty())
  {
   ++CompletedRotaryTransfers;RotaryPhase=EWarehouseRotaryPhase::Resetting;RotaryTargetYaw=0.f;
   RotaryStableSeconds=RotaryNoProgressSeconds=0.f;RotaryLastProgressYaw=CarrierYawDegrees;return;
  }
  AWarehouseConveyor* Next=FindNext();
  if (!Next || !Next->CanReceiveLoad(RotaryLoad))
  { SetStatus(EWarehouseConveyorState::DownstreamBlocked,TEXT("출구 점유 / 회전대에서 대기"));return; }
  CommandSpeedCm=FMath::Min(SpeedCm,20.f);SetStatus(EWarehouseConveyorState::Running,TEXT("90도 출구로 실제 롤러 반출"));
 }
}

bool AWarehouseConveyor::ConfigureGeometry(float NewLengthCm,float NewWidthCm,float NewSurfaceHeightCm,float NewSpeedCm)
{
 if (Kind==EWarehouseConveyorKind::RotaryTransfer &&
  (!FMath::IsNearlyEqual(NewLengthCm,200.f,.01f) || !FMath::IsNearlyEqual(NewWidthCm,200.f,.01f) || !FMath::IsNearlyEqual(NewSurfaceHeightCm,40.f,.01f)))
 { SetStatus(EWarehouseConveyorState::Stopped,TEXT("회전 이송대는 지름200cm·운반면 높이40cm 고정입니다"));return false; }
 if (!FMath::IsFinite(NewLengthCm) || !FMath::IsFinite(NewWidthCm) || !FMath::IsFinite(NewSurfaceHeightCm) || !FMath::IsFinite(NewSpeedCm) ||
  NewLengthCm<125.f || NewLengthCm>1200.f || NewWidthCm<50.f || NewWidthCm>(Kind==EWarehouseConveyorKind::RotaryTransfer ? 200.f : 150.f) || NewSurfaceHeightCm<20.f || NewSurfaceHeightCm>150.f || NewSpeedCm<1.f || NewSpeedCm>50.f)
 { SetStatus(EWarehouseConveyorState::Stopped,TEXT("길이125~1200·폭50~150·표면높이20~150·속도1~50cm/s 범위입니다"));return false; }
 if (HasActorBegunPlay() && !IsIdleForLayout()) { SetStatus(EWarehouseConveyorState::Stopped,TEXT("화물을 비우고 승강기 작업을 끝내세요"));return false; }
 const FVector OldSettings(LengthCm,WidthCm,SurfaceHeightCm);const float OldSpeed=SpeedCm;
 LengthCm=NewLengthCm;WidthCm=NewWidthCm;SurfaceHeightCm=NewSurfaceHeightCm;SpeedCm=NewSpeedCm;
 if (RebuildConveyor()) return true;
 LengthCm=OldSettings.X;WidthCm=OldSettings.Y;SurfaceHeightCm=OldSettings.Z;SpeedCm=OldSpeed;
 return false;
}

bool AWarehouseConveyor::SetConveyorPose(float YawDegrees,float InclineDegrees,float BaseHeightCm)
{
 if (!FMath::IsFinite(YawDegrees) || !FMath::IsFinite(InclineDegrees) || !FMath::IsFinite(BaseHeightCm) ||
  FMath::Abs(InclineDegrees)>20.f || BaseHeightCm<0.f || BaseHeightCm>2000.f)
 { SetStatus(EWarehouseConveyorState::Stopped,TEXT("경사 -20~20도·기준높이0~2000cm 범위입니다"));return false; }
 if (Kind==EWarehouseConveyorKind::RotaryTransfer && FMath::Abs(InclineDegrees)>.01f)
 { SetStatus(EWarehouseConveyorState::Stopped,TEXT("회전 이송대는 수평으로 설치하세요"));return false; }
 FVector Position=GetActorLocation();Position.Z=BaseHeightCm;
 return RebuildAtPose(FTransform(FRotator(InclineDegrees,FMath::UnwindDegrees(YawDegrees),0),Position));
}

bool AWarehouseConveyor::RebuildAtPose(const FTransform& Proposed)
{
 if (HasActorBegunPlay() && !IsIdleForLayout()) { SetStatus(EWarehouseConveyorState::Stopped,TEXT("화물을 비우고 승강기 작업을 끝내세요"));return false; }
 if (FMath::Min(Proposed.TransformPosition(FVector(-LengthCm*.5f,0,0)).Z,Proposed.TransformPosition(FVector(LengthCm*.5f,0,0)).Z)<-.1f)
 { SetStatus(EWarehouseConveyorState::Stopped,TEXT("낮은 쪽 지지대가 바닥 아래로 내려갑니다. 기준높이를 높이세요"));return false; }
 const FTransform Original=GetActorTransform();
 // Empty machine editing may rebuild its rig. Cargo poses and velocities are never rewritten.
 for (UPhysicsConstraintComponent* Joint:RollerJoints) if (Joint) Joint->DestroyComponent();RollerJoints.Reset();
 if (RotaryJoint) { RotaryJoint->DestroyComponent();RotaryJoint=nullptr; }
 if (RotaryCarrier) RotaryCarrier->SetSimulatePhysics(false);
 for (UStaticMeshComponent* Roller:Rollers) if (Roller) Roller->SetSimulatePhysics(false);
 SetActorTransform(Proposed,false,nullptr,ETeleportType::TeleportPhysics);
 if (RebuildConveyor()) return true;
 const FString Error=Status;
 SetActorTransform(Original,false,nullptr,ETeleportType::TeleportPhysics);
 // This empty source footprint was validated synchronously above; restoring
 // its motors is required even when the proposed footprint contains a person.
 RebuildConveyor();SetStatus(EWarehouseConveyorState::Stopped,Error);return false;
}

bool AWarehouseConveyor::SnapEntranceTo(AWarehouseConveyor* Previous)
{
 if (!IsValid(Previous) || Previous==this || FVector::DotProduct(Previous->GetExitDirection(),GetActorForwardVector())<FMath::Cos(FMath::DegreesToRadians(1.f)))
 { SetStatus(EWarehouseConveyorState::Stopped,TEXT("직결은 진행 방향·경사가 같아야 합니다(차이1도 이하). 방향 전환은 회전 이송대를 사용하세요"));return false; }
 // Leave the same practical roller/rail seam as the internal roller pitch.
 // Coincident frame end faces can otherwise begin in native contact inflation.
 const FVector Delta=Previous->GetExitPosition()+Previous->GetExitDirection()*3.f-GetEntrancePosition();
 const FVector Final=GetActorLocation()+Delta;
 if (Final.Z<0.f || Final.Z>2000.f || !RebuildAtPose(FTransform(GetActorQuat(),Final))) return false;
 Previous->NextConveyor=this;return true;
}

bool AWarehouseConveyor::SetTurnDirection(bool TurnRight)
{
 if (Kind!=EWarehouseConveyorKind::RotaryTransfer || (HasActorBegunPlay() && !IsIdleForLayout()))
 { SetStatus(EWarehouseConveyorState::Stopped,TEXT("빈 회전 이송대에서만 방향을 변경할 수 있습니다"));return false; }
 bTurnRight=TurnRight;return true;
}

void AWarehouseConveyor::BeginPlay()
{
 Super::BeginPlay();
 if (Rollers.IsEmpty()) RebuildConveyor();
 TInlineComponentArray<UStaticMeshComponent*> Parts(this);
 for (UStaticMeshComponent* Part:Parts) if (WarehousePhysics::IsPhysicalContact(Part)) WarehousePhysics::ConfigureContact(Part,EWarehouseSurface::Steel);
 if (Kind==EWarehouseConveyorKind::RotaryTransfer) MakeRotaryJoint();
 MakeRollerJoints();
}

void AWarehouseConveyor::MakeRollerJoints()
{
 if (!RollerJoints.IsEmpty()) return;
 const FTransform Reference=SurfaceFrame();
 const FQuat Axle=FRotationMatrix::MakeFromXZ(Reference.GetUnitAxis(EAxis::Y),Reference.GetUnitAxis(EAxis::Z)).ToQuat();
 for (UStaticMeshComponent* Roller:Rollers)
 {
  WarehousePhysics::ConfigureContact(Roller,EWarehouseSurface::Steel,8.f);Roller->SetSimulatePhysics(true);
  auto* Joint=NewObject<UPhysicsConstraintComponent>(this);AddInstanceComponent(Joint);Joint->RegisterComponent();
  Joint->SetWorldTransform(FTransform(Axle,Roller->GetComponentLocation()));Joint->SetDisableCollision(true);Joint->SetProjectionEnabled(false);
  Joint->ConstraintInstance.DisableMassConditioning();Joint->ConstraintInstance.DisableParentDominates();
  Joint->SetLinearXLimit(LCM_Locked,0);Joint->SetLinearYLimit(LCM_Locked,0);Joint->SetLinearZLimit(LCM_Locked,0);
  Joint->SetAngularTwistLimit(ACM_Free,0);Joint->SetAngularSwing1Limit(ACM_Locked,0);Joint->SetAngularSwing2Limit(ACM_Locked,0);
  Joint->SetConstrainedComponents(RotaryCarrier ? RotaryCarrier.Get() : Frame.Get(),NAME_None,Roller,NAME_None);Joint->SetAngularDriveMode(EAngularDriveMode::TwistAndSwing);
  Joint->SetAngularDriveAccelerationMode(false);Joint->SetAngularVelocityDriveTwistAndSwing(true,false);
  Joint->SetAngularDriveParams(0,30000.f,FMath::Clamp(RollerTorqueNm,.1f,100.f)*10000.f);RollerJoints.Add(Joint);
 }
}

bool AWarehouseConveyor::HasBearing(UStaticMeshComponent* Body) const
{
 if (!Body || !Body->IsSimulatingPhysics()) return false;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(ConveyorActualBearing),false,Body->GetOwner());
 for (UStaticMeshComponent* Roller:Rollers)
  if (Roller && Roller->ComponentOverlapComponent(Body,Body->GetComponentLocation()-GetActorUpVector()*.8f,Body->GetComponentQuat(),Params)) return true;
 return false;
}

TArray<AActor*> AWarehouseConveyor::ReadZone(bool& ForeignObject,int32& BaseCount,float& MassKg) const
{
 ForeignObject=false;BaseCount=0;MassKg=0.f;TArray<AActor*> Loads;
 if (!GetWorld()) return Loads;
 TArray<FOverlapResult> Hits;FCollisionQueryParams Params(SCENE_QUERY_STAT(ConveyorPhotoeyes),false,this);
 GetWorld()->OverlapMultiByObjectType(Hits,GetActorTransform().TransformPosition(FVector(0,0,SurfaceHeightCm+110.f)),GetActorQuat(),
  FCollisionObjectQueryParams::AllObjects,FCollisionShape::MakeBox(FVector(LengthCm*.5f,WidthCm*.5f,109.8f)),Params);
 for (const FOverlapResult& Hit:Hits)
 {
  AActor* Actor=Hit.GetActor();if (!IsValid(Actor) || !WarehousePhysics::IsPhysicalContact(Hit.GetComponent())) continue;
  if (Actor->IsA<AWarehouseConveyor>() || Actor==Lift) continue;
  if (Actor->IsA<APawn>() || Actor->IsA<AWarehouseForklift>() || Actor->ActorHasTag(TEXT("WarehousePhysicalHuman"))) { ForeignObject=true;continue; }
  UStaticMeshComponent* Body=nullptr;
  if (auto* Pallet=Cast<AWarehousePallet>(Actor)) Body=Pallet->GetPalletBody();
  else if (auto* Cargo=Cast<AWarehouseCargo>(Actor)) Body=Cargo->GetCargoBody();
  else { ForeignObject=true;continue; }
  if (Loads.Contains(Actor) || !HasBearing(Body)) continue;
  Loads.Add(Actor);++BaseCount;
 }
 if (auto* Damage=AWarehouseDamageSystem::Find(this))
 {
  Damage->EnsureSupportSnapshot();const TArray<AActor*> Bases=Loads;
  for (AActor* Base:Bases) for (AActor* Child:Damage->GetSupportedActors(Base,false)) if (IsValid(Child)) Loads.AddUnique(Child);
 }
 // Imported parcels need not already belong to a strength-profile graph. Only
 // an actual downward contact with an existing load can add their mass.
 for (int32 Pass=0;Pass<Hits.Num();++Pass)
 {
  bool Added=false;
  for (const FOverlapResult& Hit:Hits)
  {
   AActor* Actor=Hit.GetActor();auto* Cargo=Cast<AWarehouseCargo>(Actor);
   if (!Cargo || Loads.Contains(Actor)) continue;
   UStaticMeshComponent* Body=Cargo->GetCargoBody();
   if (!Body || !Body->IsSimulatingPhysics()) continue;
   const FBox CargoBounds=AWarehouseDamageSystem::GetPhysicalBounds(Actor);
   if (!CargoBounds.IsValid) continue;
   FCollisionQueryParams ContactParams(SCENE_QUERY_STAT(ConveyorStackContact),false,Actor);
   const TArray<AActor*> BearingLoads=Loads;
   for (AActor* Base:BearingLoads)
   {
    const FBox BaseBounds=AWarehouseDamageSystem::GetPhysicalBounds(Base);
    if (!BaseBounds.IsValid || CargoBounds.GetCenter().Z<=BaseBounds.GetCenter().Z) continue;
    TInlineComponentArray<UPrimitiveComponent*> Parts(Base);
    for (UPrimitiveComponent* Part:Parts) if (WarehousePhysics::IsPhysicalContact(Part) &&
     Part->ComponentOverlapComponent(Body,Body->GetComponentLocation()-FVector(0,0,.8f),Body->GetComponentQuat(),ContactParams))
    { Loads.AddUnique(Actor);Added=true;break; }
    if (Loads.Contains(Actor)) break;
   }
  }
  if (!Added) break;
 }
 MassKg=ConveyorMass(Loads);return Loads;
}

TArray<AActor*> AWarehouseConveyor::GetPhysicalLoads() const
{ bool Foreign=false;int32 Count=0;float Mass=0.f;return ReadZone(Foreign,Count,Mass); }

bool AWarehouseConveyor::IsIdleForLayout() const
{
 if (Kind==EWarehouseConveyorKind::RotaryTransfer && RotaryPhase!=EWarehouseRotaryPhase::Empty) return false;
 if (TransferPallet || bReceivingFromLift || (Lift && Lift->ReservedConveyor==this)) return false;
 if (!GetWorld()) return true;
 bool Foreign=false;int32 Count=0;float Mass=0.f;
 if (!ReadZone(Foreign,Count,Mass).IsEmpty() || Foreign) return false;
 TArray<FOverlapResult> Hits;FCollisionQueryParams Params(SCENE_QUERY_STAT(ConveyorLayoutOccupancy),false,this);
 GetWorld()->OverlapMultiByObjectType(Hits,GetActorTransform().TransformPosition(FVector(0,0,SurfaceHeightCm+110.f)),GetActorQuat(),
  FCollisionObjectQueryParams::AllObjects,FCollisionShape::MakeBox(FVector(LengthCm*.5f,WidthCm*.5f,109.8f)),Params);
 for (const FOverlapResult& Hit:Hits) if (WarehousePhysics::IsPhysicalContact(Hit.GetComponent()))
  if (AActor* Actor=Hit.GetActor();Actor && (Actor->IsA<AWarehouseCargo>() || Actor->IsA<AWarehousePallet>())) return false;
 return true;
}

bool AWarehouseConveyor::IsHandlingCargo(AActor* Cargo) const
{ return IsValid(Cargo) && (Cargo==TransferPallet || (Cargo==RotaryLoad && RotaryPhase!=EWarehouseRotaryPhase::Resetting) || GetPhysicalLoads().Contains(Cargo)); }

FVector AWarehouseConveyor::GetExitPosition() const
{
 if (Kind==EWarehouseConveyorKind::RotaryTransfer) return GetActorLocation()+GetExitDirection()*100.f+GetActorUpVector()*SurfaceHeightCm;
 return GetActorTransform().TransformPosition(FVector(LengthCm*.5f,0,SurfaceHeightCm));
}

FVector AWarehouseConveyor::GetEntrancePosition() const
{ return GetActorTransform().TransformPosition(FVector(-LengthCm*.5f,0,SurfaceHeightCm)); }

FVector AWarehouseConveyor::GetExitDirection() const
{ return Kind==EWarehouseConveyorKind::RotaryTransfer ? GetActorRightVector()*(bTurnRight ? 1.f : -1.f) : GetActorForwardVector(); }

bool AWarehouseConveyor::CanReceiveLoad(AActor* Cargo) const
{
 if (!bEnabled || !bEStopReleased) return false;
 if (Kind==EWarehouseConveyorKind::RotaryTransfer && RotaryPhase!=EWarehouseRotaryPhase::Empty &&
  (RotaryPhase!=EWarehouseRotaryPhase::Centering || Cargo!=RotaryLoad)) return false;
 bool Foreign=false;int32 Count=0;float Mass=0.f;const TArray<AActor*> Loads=ReadZone(Foreign,Count,Mass);
 return !Foreign && Mass<=RatedMassKg && (Count==0 || (IsValid(Cargo) && Loads.Contains(Cargo)));
}

bool AWarehouseConveyor::PortAligned(const AWarehousePalletLift* CargoLift) const
{
 if (!CargoLift || LiftFloor<0 || LiftFloor>=CargoLift->FloorCount) return false;
 const FVector End=CargoLift->GetActorTransform().InverseTransformPosition(GetExitPosition());
 return FMath::Abs(End.X+62.5f)<=4.f && FMath::Abs(End.Y)<=3.f &&
  FMath::Abs(End.Z-(LiftFloor*400.f+CargoLift->ConveyorHeightCm))<=1.f &&
  FVector::DotProduct(GetActorForwardVector(),CargoLift->GetActorForwardVector())>.999f;
}

AWarehouseConveyor* AWarehouseConveyor::FindNext() const
{
 auto Connected=[&](const AWarehouseConveyor* Other)
 {
  if (!IsValid(Other) || Other==this) return false;
  return FVector::Dist(Other->GetEntrancePosition(),GetExitPosition())<=4.f && FVector::DotProduct(Other->GetActorForwardVector(),GetExitDirection())>=FMath::Cos(FMath::DegreesToRadians(1.f));
 };
 if (NextConveyor) return Connected(NextConveyor) ? NextConveyor.Get() : nullptr;
 for (TActorIterator<AWarehouseConveyor> It(GetWorld());It;++It) if (Connected(*It)) return *It;
 return nullptr;
}

AWarehouseConveyor* AWarehouseConveyor::FindLiftReceiver() const
{
 if (!Lift) return nullptr;
 for (TActorIterator<AWarehouseConveyor> It(GetWorld());It;++It)
  if (*It!=this && It->Lift==Lift && It->LiftFloor==DestinationFloor && It->PortAligned(Lift) && It->bEnabled && It->bEStopReleased && It->GetPhysicalLoads().IsEmpty()) return *It;
 return nullptr;
}

void AWarehouseConveyor::SetStatus(EWarehouseConveyorState NewState,const FString& Message)
{
 State=NewState;Status=Message;
 // The engine's default distance-field world font has no Korean glyphs.
 // Keep the localized Status for Slate, and show a native identifier here.
 if (Display) Display->SetText(FText::FromString(FString::Printf(TEXT("%s\n%s\n%.0f / %.0f KG"),
  Kind==EWarehouseConveyorKind::RotaryTransfer ? TEXT("ROTARY TRANSFER") : Kind==EWarehouseConveyorKind::PalletRoller ? TEXT("PALLET ROLLER") : TEXT("CARTON ROLLER"),
  *StaticEnum<EWarehouseConveyorState>()->GetNameStringByValue(static_cast<int64>(State)),MeasuredMassKg,RatedMassKg)));
}

bool AWarehouseConveyor::AcknowledgeCompletedLift()
{
 if (!IsValid(Lift) || !bTransferRequested || Lift->ReservedConveyor!=this ||
  Lift->CompletedTransfers<=TransferStartCount || Lift->State!=EWarehousePalletLiftState::Idle || Lift->AcceptedPallet) return false;
 // This is completion bookkeeping, not a motion permission. The lift has
 // already proven that all payload and the doorway are physically clear.
 CommandSpeedCm=0.f;Lift->ReleaseConveyor(this);
 if (Lift->ReservedConveyor==this) return false;
 ++CompletedLiftTransfers;TransferPallet=nullptr;ReceivingPort=nullptr;bTransferRequested=false;
 StableLoadSeconds=MissingInfeedSeconds=0.f;return true;
}

bool AWarehouseConveyor::CancelMissingInfeed(float Seconds)
{
 if (!IsValid(Lift) || !IsValid(TransferPallet) || bTransferRequested || Lift->AcceptedPallet || Lift->ReservedConveyor!=this)
 { MissingInfeedSeconds=0.f;return false; }
 const FBox SourceBounds=ConveyorBounds(this,TransferPallet),LiftBounds=ConveyorBounds(Lift,TransferPallet);
 const float LandingHeight=LiftFloor*400.f+Lift->ConveyorHeightCm;
 const FBox SourceRegion(FVector(-LengthCm*.5f,-WidthCm*.5f,SurfaceHeightCm-1.f),FVector(LengthCm*.5f,WidthCm*.5f,SurfaceHeightCm+220.f));
 const FBox LandingRegion(FVector(-62.5f,-62.5f,LandingHeight-1.f),FVector(62.5f,62.5f,LandingHeight+220.f));
 if (!SourceBounds.IsValid || !LiftBounds.IsValid || SourceBounds.Intersect(SourceRegion) || LiftBounds.Intersect(LandingRegion) ||
  HasBearing(TransferPallet->GetPalletBody()) || Lift->HasPlatformSupport(TransferPallet))
 { MissingInfeedSeconds=0.f;return false; }
 MissingInfeedSeconds+=Seconds;
 if (MissingInfeedSeconds<.5f) return false;
 CommandSpeedCm=0.f;Lift->CommandCarrierConveyor(this,0.f);Lift->ReleaseConveyor(this);
 // An empty carrier already travelling to the source must finish its safe
 // door/level cycle before releasing ownership. Keep the owner until then.
 if (Lift->ReservedConveyor==this)
 { SetStatus(EWarehouseConveyorState::DownstreamBlocked,TEXT("인계 화물이 회수되었습니다 / 빈 승강기 정지·예약 해제 대기"));return true; }
 TransferPallet=nullptr;ReceivingPort=nullptr;bTransferRequested=false;StableLoadSeconds=MissingInfeedSeconds=0.f;
 SetStatus(EWarehouseConveyorState::Stopped,TEXT("인계 화물 회수 확인 / 승강기 예약 취소"));return true;
}

void AWarehouseConveyor::UpdateLift(float Seconds)
{
 if (!IsValid(Lift))
 { CommandSpeedCm=0;SetStatus(EWarehouseConveyorState::Fault,TEXT("화물용 승강기가 없습니다"));return; }
 if (!bLiftInfeed && !TransferPallet)
 { SetStatus(EWarehouseConveyorState::Stopped,TEXT("승강기 수신 대기"));return; }
 if (!PortAligned(Lift) || !Lift->bPoweredConveyor || DestinationFloor<0 || DestinationFloor>=Lift->FloorCount || DestinationFloor==LiftFloor)
 { CommandSpeedCm=0;SetStatus(EWarehouseConveyorState::Fault,TEXT("승강기 포트 높이·방향·목적층을 확인하세요"));return; }
 if (!IsValid(TransferPallet))
 {
  TransferPallet=nullptr;bTransferRequested=false;StableLoadSeconds=0.f;ReceivingPort=nullptr;
  if (!bLiftInfeed || !ZoneLoad) return;
  auto* Pallet=Cast<AWarehousePallet>(ZoneLoad);
  if (!Pallet) { CommandSpeedCm=0;SetStatus(EWarehouseConveyorState::Fault,TEXT("화물용 승강기는 팔레트 한 개가 필요합니다"));return; }
  ReceivingPort=FindLiftReceiver();
  if (!ReceivingPort) { CommandSpeedCm=0;SetStatus(EWarehouseConveyorState::DownstreamBlocked,TEXT("목적층 수신 컨베이어가 없거나 사용 중입니다"));return; }
  if (!Lift->ReserveConveyor(this)) { CommandSpeedCm=0;SetStatus(EWarehouseConveyorState::DownstreamBlocked,TEXT("승강기 사용 순서를 기다립니다"));return; }
  TransferPallet=Pallet;TransferStartCount=Lift->CompletedTransfers;
 }
 if (Lift->ReservedConveyor!=this)
 { CommandSpeedCm=0;SetStatus(EWarehouseConveyorState::Fault,TEXT("승강기 예약이 해제되었습니다"));return; }
 if (bTransferRequested)
 {
  CommandSpeedCm=0;SetStatus(EWarehouseConveyorState::LiftTravelling,TEXT("승강기 이동·하역 중"));
  if (Lift->State==EWarehousePalletLiftState::AwaitUnload && Lift->CurrentFloor==DestinationFloor)
  {
   // A removed or repositioned destination zone can be replaced while the
   // loaded carrier safely waits behind its landing interlock.
   if (!IsValid(ReceivingPort) || !ReceivingPort->PortAligned(Lift)) ReceivingPort=FindLiftReceiver();
   if (!IsValid(ReceivingPort) || !ReceivingPort->bEnabled || !ReceivingPort->bEStopReleased || !ReceivingPort->PortAligned(Lift))
   { SetStatus(EWarehouseConveyorState::DownstreamBlocked,TEXT("목적층 수신 컨베이어를 복구하세요"));return; }
   bool Foreign=false;int32 Count=0;float Mass=0;const TArray<AActor*> Existing=ReceivingPort->ReadZone(Foreign,Count,Mass);
   if (Foreign || (Count>0 && !Existing.Contains(TransferPallet)))
   { SetStatus(EWarehouseConveyorState::DownstreamBlocked,TEXT("목적층 수신 구역이 막혔습니다"));return; }
   ReceivingPort->bReceivingFromLift=true;ReceivingPort->TransferPallet=TransferPallet;
   ReceivingPort->CommandSpeedCm=-FMath::Min(SpeedCm,20.f);
   Lift->CommandCarrierConveyor(this,-FMath::Min(SpeedCm,20.f));
  }
  return;
 }
 if (Lift->CurrentFloor!=LiftFloor)
 {
  CommandSpeedCm=0;
  if (Lift->State==EWarehousePalletLiftState::Idle) Lift->CallForConveyor(this,LiftFloor);
  SetStatus(EWarehouseConveyorState::LoadingLift,TEXT("빈 승강기 호출 중"));return;
 }
 if (!Lift->IsLandingOpen(LiftFloor)) { CommandSpeedCm=0;SetStatus(EWarehouseConveyorState::LoadingLift,TEXT("승강기 도착·문 열림 대기"));return; }
 const FBox Bounds=ConveyorBounds(Lift,TransferPallet);
 const float Centre=Bounds.IsValid ? Bounds.GetCenter().X : -1000.f;
 const float Remaining=-Centre;
 const float Target=Remaining>1.f ? FMath::Clamp(Remaining*1.1f,2.f,FMath::Min(SpeedCm,20.f)) : 0.f;
 CommandSpeedCm=Target;Lift->CommandCarrierConveyor(this,Target);
 SetStatus(EWarehouseConveyorState::LoadingLift,TEXT("팔레트 실제 접촉 이송·중심 정렬 중"));
 if (Target>0.f || !Bounds.IsValid || Bounds.Min.X< -62.5f || Bounds.Max.X>62.5f || Bounds.Min.Y< -62.5f || Bounds.Max.Y>62.5f)
 { StableLoadSeconds=0.f;return; }
 if (TransferPallet->GetPalletBody()->GetPhysicsLinearVelocity().Size()>2.f) { StableLoadSeconds=0;return; }
 StableLoadSeconds+=Seconds;
 if (StableLoadSeconds>=.6f && Lift->RequestConveyorTransfer(this,TransferPallet,LiftFloor,DestinationFloor)) bTransferRequested=true;
}

void AWarehouseConveyor::UpdateSensors(float Seconds)
{
 bool Foreign=false;int32 Count=0;const TArray<AActor*> Loads=ReadZone(Foreign,Count,MeasuredMassKg);
 ZoneLoad=Loads.IsEmpty() ? nullptr : Loads[0];EndPallet=nullptr;CommandSpeedCm=0;
 if (Kind==EWarehouseConveyorKind::RotaryTransfer) { UpdateRotary(Seconds,Loads,Foreign,Count);return; }
 const bool CompletedTransfer=AcknowledgeCompletedLift();
 if (CancelMissingInfeed(Seconds)) return;
 if (!bEnabled || !bEStopReleased)
 { if (Lift) Lift->CommandCarrierConveyor(this,0.f);SetStatus(EWarehouseConveyorState::Stopped,bEnabled ? TEXT("비상 정지") : TEXT("전원 꺼짐"));return; }
 if (Foreign || Count>1)
 { if (Lift) Lift->CommandCarrierConveyor(this,0.f);SetStatus(EWarehouseConveyorState::DownstreamBlocked,Foreign ? TEXT("사람·지게차·이물질 감지 / 정지") : TEXT("한 구간에 화물이 둘 이상입니다"));return; }
 if (!FMath::IsFinite(MeasuredMassKg) || MeasuredMassKg>FMath::Clamp(RatedMassKg,1.f,2000.f))
 { if (Lift) Lift->CommandCarrierConveyor(this,0.f);SetStatus(EWarehouseConveyorState::Overloaded,TEXT("컨베이어 정격 하중 초과"));return; }
 if (ZoneLoad)
 {
  const FBox Box=ConveyorBounds(this,ZoneLoad);
  if (!Box.IsValid || Box.Min.Y < -WidthCm*.5f || Box.Max.Y>WidthCm*.5f)
  { SetStatus(EWarehouseConveyorState::Fault,TEXT("화물이 컨베이어 유효 폭을 벗어났습니다"));return; }
 }
 if (CompletedTransfer) { SetStatus(EWarehouseConveyorState::Stopped,TEXT("층간 화물 인계 완료"));return; }
 if (bReceivingFromLift)
 {
  const bool StillLeaving=IsValid(Lift) && Lift->AcceptedPallet==TransferPallet;
  if (!ZoneLoad && !StillLeaving) { bReceivingFromLift=false;TransferPallet=nullptr;SetStatus(EWarehouseConveyorState::Stopped,TEXT("하역 화물 인출 완료"));return; }
  if (ZoneLoad)
  {
   const FBox Box=ConveyorBounds(this,ZoneLoad);
   const float Remaining=Box.Min.X+LengthCm*.5f-3.f;
   CommandSpeedCm=Remaining>1.f ? -FMath::Clamp(Remaining*1.1f,2.f,FMath::Min(SpeedCm,20.f)) : 0.f;
   if (CommandSpeedCm==0.f)
   { EndPallet=Cast<AWarehousePallet>(ZoneLoad);SetStatus(EWarehouseConveyorState::PickupReady,TEXT("끝단 정지 / 지게차 인수 대기")); }
   else SetStatus(EWarehouseConveyorState::ReceivingLift,TEXT("승강기 화물 수신 중"));
  }
  return;
 }
 if (Lift || TransferPallet) { UpdateLift(Seconds);return; }
 if (!ZoneLoad) { SetStatus(EWarehouseConveyorState::Stopped,TEXT("다음 화물 대기"));return; }
 const FBox Box=ConveyorBounds(this,ZoneLoad);
 AWarehouseConveyor* Next=FindNext();bool NextFree=false;
 if (Next && Next->bEnabled && Next->bEStopReleased)
 {
  NextFree=Next->CanReceiveLoad(ZoneLoad);
 }
 if (NextFree) { CommandSpeedCm=FMath::Clamp(SpeedCm,1.f,50.f);SetStatus(EWarehouseConveyorState::Running,TEXT("다음 구간으로 이송"));return; }
 const float Remaining=LengthCm*.5f-3.f-Box.Max.X;
 CommandSpeedCm=Remaining>1.f ? FMath::Clamp(Remaining*1.1f,2.f,FMath::Clamp(SpeedCm,1.f,50.f)) : 0.f;
 if (CommandSpeedCm>0) SetStatus(EWarehouseConveyorState::Running,TEXT("끝단 정위치 접근"));
 else
 {
  EndPallet=Cast<AWarehousePallet>(ZoneLoad);
  SetStatus(bPickupEnd ? EWarehouseConveyorState::PickupReady : EWarehouseConveyorState::DownstreamBlocked,
   bPickupEnd ? TEXT("끝단 정지 / 지게차 인수 대기") : TEXT("다음 구간 점유 / 상류 정지"));
 }
}

void AWarehouseConveyor::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);const float Dt=FMath::Clamp(DeltaSeconds,0.f,.133333f);
 SinceSensors+=Dt;if (SinceSensors>=.1f) { const float Elapsed=SinceSensors;SinceSensors=0;UpdateSensors(Elapsed); }
 // A machine photoeye is separate from the AGV's one-second perception cycle.
 const float Target=bEnabled && bEStopReleased ? CommandSpeedCm : 0.f;
 RampedSpeedCm=FMath::FInterpConstantTo(RampedSpeedCm,Target,Dt,40.f);
 DriveRotary(Dt);
 const float Radius=RollerRadius();
 for (int32 Index=0;Index<RollerJoints.Num();++Index)
 {
  // Joint velocity is mount minus roller; physical upper-surface travel has the opposite sign.
  RollerJoints[Index]->SetAngularVelocityTarget(FVector(-RampedSpeedCm/(2.f*PI*Radius),0,0));
  if (FMath::Abs(RampedSpeedCm)>.01f) Rollers[Index]->WakeAllRigidBodies();
 }
 ActualSurfaceSpeedCm=Rollers.IsEmpty() ? 0.f : FVector::DotProduct(Rollers[Rollers.Num()/2]->GetPhysicsAngularVelocityInRadians(),SurfaceFrame().GetUnitAxis(EAxis::Y))*Radius;
}

void AWarehouseConveyor::EndPlay(const EEndPlayReason::Type Reason)
{ if (IsValid(Lift)) Lift->ReleaseConveyor(this);Super::EndPlay(Reason); }
