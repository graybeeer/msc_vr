#include "WarehousePalletLift.h"

#include "WarehousePallet.h"
#include "WarehouseForklift.h"
#include "WarehouseDamageSystem.h"
#include "WarehousePhysics.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float PalletLiftHalfDeck=62.5f;
constexpr float PalletLiftLoadHeight=220.f;
constexpr float PalletLiftSpring=300000.f;
constexpr float PalletLiftDoorTravel=70.f;
constexpr float PalletLiftClosedGateY=31.25f;

UStaticMesh* PalletLiftFrameMesh(int32 Floors,bool Powered)
{
 if (Floors<3) return nullptr;
 const TCHAR* Path=Powered ? (Floors==4 ?
  TEXT("/Game/Warehouse/PalletLift/Meshes/SM_PalletLift_FramePowered4") : TEXT("/Game/Warehouse/PalletLift/Meshes/SM_PalletLift_FramePowered3")) : (Floors==4 ?
  TEXT("/Game/Warehouse/PalletLift/Meshes/SM_PalletLift_Frame4") : TEXT("/Game/Warehouse/PalletLift/Meshes/SM_PalletLift_Frame"));
 return LoadObject<UStaticMesh>(nullptr,Path);
}

FBox PalletLiftLocalBounds(const AActor* Lift,AActor* Object)
{
 const FBox World=AWarehouseDamageSystem::GetPhysicalBounds(Object);
 FBox Local(ForceInit);
 if (World.IsValid) for (int32 X=0;X<2;++X) for (int32 Y=0;Y<2;++Y) for (int32 Z=0;Z<2;++Z)
  Local+=Lift->GetActorTransform().InverseTransformPosition(FVector(X ? World.Max.X : World.Min.X,
   Y ? World.Max.Y : World.Min.Y,Z ? World.Max.Z : World.Min.Z));
 return Local;
}

TArray<FOverlapResult> PalletLiftOccupants(const AActor* Lift,float Height)
{
 TArray<FOverlapResult> Hits;
 const FVector Centre=Lift->GetActorTransform().TransformPosition(FVector(0,0,Height+110.f));
 FCollisionQueryParams Params(SCENE_QUERY_STAT(PalletLiftCabin),false,Lift);
 Lift->GetWorld()->OverlapMultiByObjectType(Hits,Centre,Lift->GetActorQuat(),
  FCollisionObjectQueryParams::AllObjects,FCollisionShape::MakeBox(FVector(62.3f,62.3f,109.8f)),Params);
 return Hits;
}

float PalletLiftPhysicalMass(const TArray<AActor*>& Objects)
{
 float Mass=0.f;
 TSet<const FBodyInstance*> Seen;
 for (AActor* Object:Objects) if (IsValid(Object))
 {
  TInlineComponentArray<UPrimitiveComponent*> Parts(Object);
  for (UPrimitiveComponent* Part:Parts) if (WarehousePhysics::IsPhysicalContact(Part))
  {
   FBodyInstance* Body=Part->GetBodyInstance(NAME_None,true);
   while (Body && Body->WeldParent) Body=Body->WeldParent;
   if (Body && Body->IsValidBodyInstance() && !Seen.Contains(Body))
   { Seen.Add(Body);Mass+=Body->GetBodyMass(); }
  }
 }
 return Mass;
}
}

AWarehousePalletLift::AWarehousePalletLift()
{
 PrimaryActorTick.bCanEverTick=true;
 PrimaryActorTick.TickGroup=TG_PrePhysics;
 Tags.Add(TEXT("WarehousePhysicalRig"));
 Tags.Add(TEXT("WarehousePalletLift"));
 RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("PalletLiftRoot"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> FrameAsset(TEXT("/Game/Warehouse/PalletLift/Meshes/SM_PalletLift_Frame"));
 static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> CarrierAsset(TEXT("/Game/Warehouse/PalletLift/Meshes/SM_PalletLift_Carrier"));
 static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> GateAsset(TEXT("/Game/Warehouse/PalletLift/Meshes/SM_PalletLift_Gate"));
 static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Steel(TEXT("/Game/Warehouse/Materials/MI_DarkSteel"));
 static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Yellow(TEXT("/Game/Warehouse/Materials/MI_SafetyYellow"));
 auto Part=[&](FString Name,FVector Position,FVector Size,UMaterialInterface* Material)
 {
  auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(*Name);
  Mesh->SetupAttachment(RootComponent);
  Mesh->SetStaticMesh(Cube.Object);
  Mesh->SetRelativeLocation(Position);Mesh->SetRelativeScale3D(Size/100.f);
  if (Material) Mesh->SetMaterial(0,Material);
  Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
  return Mesh;
 };
 Frame=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Frame"));
 Frame->SetupAttachment(RootComponent);Frame->SetStaticMesh(FrameAsset.Get());
 Frame->SetCollisionProfileName(TEXT("BlockAllDynamic"));
 Platform=Part(TEXT("CargoPlatform"),FVector(0,0,35),FVector(125,125,10),Steel.Get());
 if (CarrierAsset.Get())
 {
  Platform->SetStaticMesh(CarrierAsset.Get());Platform->SetRelativeScale3D(FVector::OneVector);
  Platform->SetRelativeLocation(FVector(0,0,ConveyorHeightCm));
 }
 // Real open shaft framing, never a solid cube occupying the carrier's travel space.
 for (int32 X:{-1,1}) for (int32 Y:{-1,1})
  Part(FString::Printf(TEXT("Column_%d_%d"),X,Y),FVector(X*84,Y*84,560),FVector(12,12,1120),Steel.Get());
 for (int32 Side:{-1,1})
 {
  Part(FString::Printf(TEXT("FrameTopX_%d"),Side),FVector(0,Side*84,1114),FVector(168,12,12),Steel.Get());
  Part(FString::Printf(TEXT("FrameTopY_%d"),Side),FVector(Side*84,0,1114),FVector(12,168,12),Steel.Get());
 }
 const float BraceLength=FMath::Sqrt(168.f*168.f+250.f*250.f);
 const float BraceAngle=FMath::RadiansToDegrees(FMath::Atan2(250.f,168.f));
 for (int32 Floor=0;Floor<4;++Floor)
 {
  for (int32 Side:{-1,1})
  {
   auto* Gate=Part(FString::Printf(TEXT("CargoGate_%d_%d"),Floor,Side),FVector(-94,Side*PalletLiftClosedGateY,Floor*400+157),FVector(3,62.5,230),Yellow.Get());
   if (GateAsset.Get()) { Gate->SetStaticMesh(GateAsset.Get());Gate->SetRelativeScale3D(FVector::OneVector); }
   Gates.Add(Gate);
  }
  Part(FString::Printf(TEXT("FrameBackBeam_%d"),Floor),FVector(84,0,Floor*400+285),FVector(12,168,10),Steel.Get());
  for (int32 Side:{-1,1}) for (int32 Diagonal:{-1,1})
  {
   auto* Brace=Part(FString::Printf(TEXT("FrameSideBrace_%d_%d_%d"),Floor,Side,Diagonal),
    FVector(0,Side*84,Floor*400+165),FVector(BraceLength,4,4),Steel.Get());
   Brace->SetRelativeRotation(FRotator(Diagonal*BraceAngle,0,0));
  }
  for (int32 Diagonal:{-1,1})
  {
   auto* Brace=Part(FString::Printf(TEXT("FrameRearBrace_%d_%d"),Floor,Diagonal),
    FVector(84,0,Floor*400+165),FVector(4,BraceLength,4),Steel.Get());
   Brace->SetRelativeRotation(FRotator(0,0,Diagonal*BraceAngle));
  }
 }
 Display=CreateDefaultSubobject<UTextRenderComponent>(TEXT("PalletLiftStatus"));
 Display->SetupAttachment(RootComponent);Display->SetRelativeLocation(FVector(-110,0,305));
 Display->SetRelativeRotation(FRotator(0,180,0));Display->SetWorldSize(11);
 Display->SetHorizontalAlignment(EHTA_Center);Display->SetText(FText::FromString(Status));
 ConfigureTower();
}

void AWarehousePalletLift::OnConstruction(const FTransform& Transform)
{ Super::OnConstruction(Transform);ConfigureTower(); }

void AWarehousePalletLift::ConfigureTower()
{
 FloorCount=FMath::Clamp(FloorCount,1,4);
 // Older saved actors serialized the original six-entry array. New default
 // subobjects still exist after loading; restore all eight by their stable names.
 TInlineComponentArray<UStaticMeshComponent*> Parts(this);
 TArray<TObjectPtr<UStaticMeshComponent>> AllGates;AllGates.SetNum(8);
 for (UStaticMeshComponent* Part:Parts) if (Part->GetName().StartsWith(TEXT("CargoGate_")))
 {
  FString Level,Side;
  if (Part->GetName().RightChop(10).Split(TEXT("_"),&Level,&Side))
  {
   const int32 Index=FCString::Atoi(*Level)*2+(FCString::Atoi(*Side)>0 ? 1 : 0);
   if (AllGates.IsValidIndex(Index)) AllGates[Index]=Part;
  }
 }
 if (!AllGates.Contains(nullptr)) Gates=MoveTemp(AllGates);
 UStaticMesh* Model=PalletLiftFrameMesh(FloorCount,bPoweredConveyor);
 if (Frame) Frame->SetStaticMesh(Model);
 const float Height=FloorCount*400.f-80.f;
 ShaftClearanceHalf.Z=Height*.5f;ShaftClearanceCenter.Z=Height*.5f;
 for (UStaticMeshComponent* Part:Parts)
 {
  const FString Name=Part->GetName();
  const bool Fallback=Name.StartsWith(TEXT("Column_")) || Name.StartsWith(TEXT("FrameTop")) ||
   Name.StartsWith(TEXT("FrameBack")) || Name.StartsWith(TEXT("FrameSide")) || Name.StartsWith(TEXT("FrameRear"));
  if (Name.StartsWith(TEXT("Column_")))
  { FVector Position=Part->GetRelativeLocation();Position.Z=Height*.5f;Part->SetRelativeLocation(Position);
    FVector Scale=Part->GetRelativeScale3D();Scale.Z=Height/100.f;Part->SetRelativeScale3D(Scale); }
  if (Name.StartsWith(TEXT("FrameTop")))
  { FVector Position=Part->GetRelativeLocation();Position.Z=Height-6.f;Part->SetRelativeLocation(Position); }
  if (Fallback)
  {
   bool Active=!Model;
   if (Name.StartsWith(TEXT("FrameBack")) || Name.StartsWith(TEXT("FrameSide")) || Name.StartsWith(TEXT("FrameRear")))
    Active=Active && FMath::FloorToInt(Part->GetRelativeLocation().Z/400.f)<FloorCount;
   Part->SetVisibility(Active);Part->SetCollisionEnabled(Active ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
  }
 }
 for (int32 Index=0;Index<Gates.Num();++Index)
 {
  const bool Active=Index<FloorCount*2;
  Gates[Index]->SetVisibility(Active);Gates[Index]->SetCollisionEnabled(Active ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
 }
}

void AWarehousePalletLift::BeginPlay()
{
 InitialShaftPosition=GetActorLocation();
 Super::BeginPlay();
 ConfigureTower();
 if (bPoweredConveyor && FloorCount>=3 && !Frame->GetStaticMesh())
 { SetState(EWarehousePalletLiftState::Fault,TEXT("POWERED SHAFT FRAME ASSET MISSING"));return; }
 // Support preparation after CDO creation, without changing any original mesh asset.
 if (auto* Carrier=LoadObject<UStaticMesh>(nullptr,bPoweredConveyor ?
  TEXT("/Game/Warehouse/PalletLift/Meshes/SM_PalletLift_CarrierPoweredFrame") : TEXT("/Game/Warehouse/PalletLift/Meshes/SM_PalletLift_Carrier")))
 {
  Platform->SetStaticMesh(Carrier);Platform->SetRelativeScale3D(FVector::OneVector);
  Platform->SetRelativeLocation(FVector(0,0,ConveyorHeightCm));
 }
 else if (bPoweredConveyor)
 { SetState(EWarehousePalletLiftState::Fault,TEXT("POWERED CARRIER FRAME ASSET MISSING"));return; }
 if (auto* GateMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Warehouse/PalletLift/Meshes/SM_PalletLift_Gate")))
  for (UStaticMeshComponent* Gate:Gates) { Gate->SetStaticMesh(GateMesh);Gate->SetRelativeScale3D(FVector::OneVector); }
 TInlineComponentArray<UStaticMeshComponent*> Parts(this);
 for (UStaticMeshComponent* Part:Parts)
 {
  const bool Fallback=Part->GetName().StartsWith(TEXT("Column_")) || Part->GetName().StartsWith(TEXT("FrameTop")) ||
   Part->GetName().StartsWith(TEXT("FrameBack")) || Part->GetName().StartsWith(TEXT("FrameSide")) || Part->GetName().StartsWith(TEXT("FrameRear"));
  if (Frame->GetStaticMesh() && Fallback)
  { Part->SetVisibility(false);Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);continue; }
  if (!GuideAnchor && Part->GetName().StartsWith(TEXT("Column_"))) GuideAnchor=Part;
  if (WarehousePhysics::IsPhysicalContact(Part)) WarehousePhysics::ConfigureContact(Part,EWarehouseSurface::Steel);
 }
 if (WarehousePhysics::IsPhysicalContact(Frame)) GuideAnchor=Frame;
 if (!GuideAnchor || !Platform->GetStaticMesh())
 { SetState(EWarehousePalletLiftState::Fault,TEXT("CARRIER / SHAFT BODY MISSING"));return; }
 PlatformTopOffsetCm=bPoweredConveyor ? 0.f : Platform->GetStaticMesh()->GetBoundingBox().Max.Z*Platform->GetComponentScale().Z;
 PlatformHeightCm=ConveyorHeightCm;PlatformTargetCm=PlatformHeightCm;
 auto Guide=[&](UStaticMeshComponent* Body,const FVector& LocalCentre)
 {
  auto* Joint=NewObject<UPhysicsConstraintComponent>(this);
  AddInstanceComponent(Joint);Joint->RegisterComponent();
  Joint->SetWorldTransform(FTransform(GetActorQuat(),GetActorTransform().TransformPosition(LocalCentre)));
  Joint->SetDisableCollision(true);Joint->SetProjectionEnabled(false);
  Joint->ConstraintInstance.DisableMassConditioning();Joint->ConstraintInstance.DisableParentDominates();
  Joint->SetLinearXLimit(LCM_Locked,0);Joint->SetLinearYLimit(LCM_Locked,0);Joint->SetLinearZLimit(LCM_Locked,0);
  Joint->SetAngularTwistLimit(ACM_Locked,0);Joint->SetAngularSwing1Limit(ACM_Locked,0);Joint->SetAngularSwing2Limit(ACM_Locked,0);
  Joint->SetConstrainedComponents(GuideAnchor,NAME_None,Body,NAME_None);
  Joint->SetConstraintReferencePosition(EConstraintFrame::Frame2,FVector::ZeroVector);
  Joint->SetLinearDriveAccelerationMode(false);
  return Joint;
 };
 WarehousePhysics::ConfigureContact(Platform,EWarehouseSurface::Steel,bPoweredConveyor ? 16.f : 120.f);
 Platform->SetSimulatePhysics(true);
 PlatformJoint=Guide(Platform,FVector(0,0,ConveyorHeightCm+StrokeHalfHeight()-PlatformTopOffsetCm));
 PlatformJoint->SetLinearPositionDrive(false,false,true);PlatformJoint->SetLinearVelocityDrive(false,false,true);
 PlatformJoint->SetLinearDriveParams(PalletLiftSpring,50000.f,4000000.f); // Finite 40kN vertical drive.
 SetDeckLock(true);
 ConfigureGateGuides();
 if (bPoweredConveyor) ConfigureCarrierRollers();
 SetState(EWarehousePalletLiftState::OpeningDoors,TEXT("OPENING LOADING GATE"));
}

bool AWarehousePalletLift::SetFloorCount(int32 Count)
{
 if (Count<1 || Count>4) { ShowStatus(TEXT("건물은 최대 4층까지 지원합니다"));return false; }
 if (Count==FloorCount) return true;
 if (bPoweredConveyor && Count>=3 && !PalletLiftFrameMesh(Count,true))
 { ShowStatus(TEXT("해당 층수의 컨베이어용 승강기 프레임이 없습니다"));return false; }
 if (HasActorBegunPlay() && (!IsIdleForLayout() || CurrentFloor>=Count))
 { ShowStatus(TEXT("빈 운반대를 유지할 층으로 이동하고 승강기 작업을 끝내세요"));return false; }
 FloorCount=Count;ConfigureTower();
 if (HasActorBegunPlay())
 {
  GuideAnchor=WarehousePhysics::IsPhysicalContact(Frame) ? Frame.Get() : nullptr;
  if (!GuideAnchor)
  { TInlineComponentArray<UStaticMeshComponent*> Parts(this);for (UStaticMeshComponent* Part:Parts)
     if (Part->GetName().StartsWith(TEXT("Column_")) && WarehousePhysics::IsPhysicalContact(Part)) { GuideAnchor=Part;break; } }
  if (!GuideAnchor) { SetState(EWarehousePalletLiftState::Fault,TEXT("SHAFT GUIDE MISSING"));return false; }
  PlatformJoint->SetConstrainedComponents(GuideAnchor,NAME_None,Platform,NAME_None);
  PlatformJoint->SetConstraintReferencePosition(EConstraintFrame::Frame2,FVector::ZeroVector);SetDeckLock(true);
  ConfigureGateGuides();
 }
 return true;
}

void AWarehousePalletLift::ConfigureGateGuides()
{
 for (UPhysicsConstraintComponent* Joint:GateJoints) if (Joint) Joint->DestroyComponent();
 GateJoints.Reset();GateJoints.SetNum(Gates.Num());
 for (int32 Index=0;Index<Gates.Num();++Index)
 {
  UStaticMeshComponent* Gate=Gates[Index];
  if (Index>=ActiveGateCount()) { Gate->SetSimulatePhysics(false);continue; }
  const int32 Floor=Index/2;const float Side=Index%2 ? 1.f : -1.f;
  WarehousePhysics::ConfigureContact(Gate,EWarehouseSurface::Steel,50.f);Gate->SetSimulatePhysics(true);
  auto* Joint=NewObject<UPhysicsConstraintComponent>(this);AddInstanceComponent(Joint);Joint->RegisterComponent();
  const FVector Centre(-94,Side*(PalletLiftClosedGateY+PalletLiftDoorTravel*.5f),Floor*400+157);
  Joint->SetWorldTransform(FTransform(GetActorQuat(),GetActorTransform().TransformPosition(Centre)));
  Joint->SetDisableCollision(true);Joint->SetProjectionEnabled(false);Joint->ConstraintInstance.DisableMassConditioning();Joint->ConstraintInstance.DisableParentDominates();
  // UE shares one linear-limit distance across all axes; set the limited axis last.
  Joint->SetLinearXLimit(LCM_Locked,0);Joint->SetLinearZLimit(LCM_Locked,0);Joint->SetLinearYLimit(LCM_Limited,PalletLiftDoorTravel*.5f);
  Joint->SetAngularTwistLimit(ACM_Locked,0);Joint->SetAngularSwing1Limit(ACM_Locked,0);Joint->SetAngularSwing2Limit(ACM_Locked,0);
  Joint->SetConstrainedComponents(GuideAnchor,NAME_None,Gate,NAME_None);Joint->SetConstraintReferencePosition(EConstraintFrame::Frame2,FVector::ZeroVector);
  Joint->SetLinearDriveAccelerationMode(false);Joint->SetLinearPositionDrive(false,true,false);Joint->SetLinearVelocityDrive(false,true,false);
  Joint->SetLinearDriveParams(12000.f,2500.f,60000.f);
  const float Opening=Floor==CurrentFloor ? DoorOpeningCm : 0.f;
  Joint->SetLinearPositionTarget(FVector(0,Side*(PalletLiftDoorTravel*.5f-Opening),0));GateJoints[Index]=Joint;
 }
}

float AWarehousePalletLift::FloorHeight(int32 Floor) const
{ return GetActorLocation().Z+ConveyorHeightCm+Floor*400.f; }

FText AWarehousePalletLift::GetTypeLabel() const
{ return FText::FromString(TEXT("화물용 엘리베이터")); }

FText AWarehousePalletLift::GetInteractionText(int32 PlayerFloor) const
{
 const FString Action=PlayerFloor==CurrentFloor ? TEXT("E: 적재 팔레트 다음 층으로 보내기") : TEXT("E: 빈 운반대 호출");
 return FText::FromString(FString::Printf(TEXT("화물용 엘리베이터 · %d층 → %d층\n%s\n%s"),
  CurrentFloor+1,DestinationFloor+1,*Status,*Action));
}

void AWarehousePalletLift::ShowStatus(const FString& Message)
{
 Status=Message;
 if (Display) Display->SetText(FText::FromString(FString::Printf(TEXT("PALLET LIFT · L%d -> L%d\n%.0f / %.0f KG\n%s"),
  CurrentFloor+1,DestinationFloor+1,PayloadMassKg,RatedMassKg,*Status)));
}

void AWarehousePalletLift::SetState(EWarehousePalletLiftState Next,const FString& Message)
{
 State=Next;ShowStatus(Message);
 UE_LOG(LogTemp,Log,TEXT("PALLET_LIFT_STATE %s %s / L%d -> L%d"),*GetName(),*Message,CurrentFloor+1,DestinationFloor+1);
}

FString AWarehousePalletLift::CheckInterlocks() const
{
 if (!bPowerAvailable) return TEXT("POWER LOST - BRAKE HOLD");
 if (!bEStopReleased) return TEXT("E-STOP - BRAKE HOLD");
 if (!bDoorSensorsHealthy) return TEXT("DOOR SENSOR FAULT");
 if (!bLevelSensorHealthy) return TEXT("LEVEL SENSOR FAULT");
 return FString();
}

bool AWarehousePalletLift::CabinFreeOfPeopleAndVehicles() const
{
 for (const FOverlapResult& Hit:PalletLiftOccupants(this,PlatformHeightCm))
  if (WarehousePhysics::IsPhysicalContact(Hit.GetComponent()))
   if (AActor* Actor=Hit.GetActor();Actor && (Actor->IsA<APawn>() || Actor->IsA<AWarehouseForklift>() || Actor->ActorHasTag(TEXT("WarehousePhysicalHuman")))) return false;
 return true;
}

bool AWarehousePalletLift::HasPlatformSupport(AWarehousePallet* Pallet) const
{
 if (!IsValid(Pallet) || !Pallet->GetPalletBody() || !Pallet->GetPalletBody()->IsSimulatingPhysics()) return false;
 const FBox Box=PalletLiftLocalBounds(this,Pallet);
 if (!Box.IsValid || FMath::Abs(Box.Min.Z-PlatformHeightCm)>1.f) return false;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(PalletLiftDeckContact),false,Pallet);
 UStaticMeshComponent* Body=Pallet->GetPalletBody();
 // Query all real runner/roller shapes with the same 1cm contact tolerance.
 // A few vertical rays can miss roller crowns even while the pallet is supported.
 if (Platform->ComponentOverlapComponent(Body,Body->GetComponentLocation()-FVector(0,0,1),Body->GetComponentQuat(),Params)) return true;
 for (UStaticMeshComponent* Roller:CarrierRollers)
  if (Roller->ComponentOverlapComponent(Body,Body->GetComponentLocation()-FVector(0,0,1),Body->GetComponentQuat(),Params)) return true;
 return false;
}

bool AWarehousePalletLift::IsLandingOpen(int32 Floor) const
{
 if (Floor!=CurrentFloor || (State!=EWarehousePalletLiftState::Idle && State!=EWarehousePalletLiftState::AwaitUnload) || !CheckInterlocks().IsEmpty()) return false;
 for (int32 Index=Floor*2;Index<Floor*2+2;++Index)
  if (!Gates.IsValidIndex(Index) || FMath::Abs(GetActorTransform().InverseTransformPosition(Gates[Index]->GetComponentLocation()).Y)<PalletLiftClosedGateY+PalletLiftDoorTravel-1.f) return false;
 return true;
}

bool AWarehousePalletLift::ReserveConveyor(AActor* Requester)
{
 if (!IsValid(Requester) || !bPoweredConveyor || !CheckInterlocks().IsEmpty()) return false;
 if (ReservedConveyor==Requester) return true;
 if (ReservedConveyor || !IsIdleForLayout()) return false;
 ReservedConveyor=Requester;return true;
}

void AWarehousePalletLift::ReleaseConveyor(AActor* Requester)
{
 if (ReservedConveyor!=Requester) return;
 CarrierCommandCm=0.f;CarrierCommandAge=1.f;
 // Do not abandon a physically loaded trip if its originating zone is removed.
 if (State==EWarehousePalletLiftState::Idle && !AcceptedPallet) ReservedConveyor=nullptr;
}

bool AWarehousePalletLift::CallForConveyor(AActor* Requester,int32 Floor)
{ return IsValid(Requester) && ReservedConveyor==Requester && CallEmpty(Floor); }

bool AWarehousePalletLift::RequestConveyorTransfer(AActor* Requester,AWarehousePallet* Pallet,int32 FromFloor,int32 ToFloor)
{ return IsValid(Requester) && ReservedConveyor==Requester && AcceptTransfer(Pallet,FromFloor,ToFloor); }

bool AWarehousePalletLift::CommandCarrierConveyor(AActor* Requester,float SpeedCm)
{
 if (!IsValid(Requester) || ReservedConveyor!=Requester || !bPoweredConveyor || !IsLandingOpen(CurrentFloor) || !FMath::IsFinite(SpeedCm)) return false;
 CarrierCommandCm=FMath::Clamp(SpeedCm,-20.f,20.f);CarrierCommandAge=0.f;return true;
}

bool AWarehousePalletLift::SetPoweredConveyor(bool Enabled)
{
 if (Enabled==bPoweredConveyor && (!HasActorBegunPlay() || (Enabled ? CarrierRollers.Num()==13 : CarrierRollers.IsEmpty()))) return true;
 UStaticMesh* PoweredMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Warehouse/PalletLift/Meshes/SM_PalletLift_CarrierPoweredFrame"));
 UStaticMesh* ManualMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Warehouse/PalletLift/Meshes/SM_PalletLift_Carrier"));
 UStaticMesh* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder"));
 auto ValidMesh=[](UStaticMesh* Mesh) { return Mesh && Mesh->GetBodySetup() && Mesh->GetBodySetup()->AggGeom.GetElementCount()>0; };
 if (!ValidMesh(ManualMesh) || (Enabled && (!ValidMesh(PoweredMesh) || !ValidMesh(Cylinder))))
 { ShowStatus(TEXT("컨베이어 캐리어 메시·실제 충돌 형상이 없습니다"));return false; }
 if (FloorCount>=3 && !PalletLiftFrameMesh(FloorCount,Enabled))
 { ShowStatus(TEXT("해당 구동 방식의 승강기 프레임이 없습니다"));return false; }
 if (!HasActorBegunPlay()) { bPoweredConveyor=Enabled;ConfigureTower();return true; }
 if (!IsIdleForLayout() || !PortalClear() || !CheckInterlocks().IsEmpty() || !PlatformJoint || !GuideAnchor ||
  Platform->GetPhysicsLinearVelocity().Size()>2.f || Platform->GetPhysicsAngularVelocityInDegrees().Size()>1.f)
 { ShowStatus(TEXT("빈 운반대가 정지하고 문 주변과 인터록이 정상일 때만 구동 방식을 변경할 수 있습니다"));return false; }
 const bool Previous=bPoweredConveyor;
 const float DeckHeight=GetActorTransform().InverseTransformPosition(Platform->GetComponentLocation()).Z+PlatformTopOffsetCm;
 auto Install=[&](bool Powered)
 {
  for (UPhysicsConstraintComponent* Joint:CarrierRollerJoints) if (Joint) Joint->DestroyComponent();CarrierRollerJoints.Reset();
  for (UStaticMeshComponent* Roller:CarrierRollers) if (Roller) Roller->DestroyComponent();CarrierRollers.Reset();
  // Replacing the shaft mesh recreates its rigid body. Release every guide
  // before the replacement, then bind the real carrier and doors to the new body.
  for (UPhysicsConstraintComponent* Joint:GateJoints) if (Joint) Joint->DestroyComponent();GateJoints.Reset();
  PlatformJoint->TermComponentConstraint();Platform->SetSimulatePhysics(false);
  Platform->SetStaticMesh(Powered ? PoweredMesh : ManualMesh);
  bPoweredConveyor=Powered;
  ConfigureTower();
  GuideAnchor=WarehousePhysics::IsPhysicalContact(Frame) ? Frame.Get() : nullptr;
  if (!GuideAnchor)
  { TInlineComponentArray<UStaticMeshComponent*> Parts(this);for (UStaticMeshComponent* Part:Parts)
     if (Part->GetName().StartsWith(TEXT("Column_")) && WarehousePhysics::IsPhysicalContact(Part)) { GuideAnchor=Part;break; } }
  if (!GuideAnchor) return false;
  WarehousePhysics::ConfigureContact(GuideAnchor,EWarehouseSurface::Steel);
  PlatformTopOffsetCm=Powered ? 0.f : ManualMesh->GetBoundingBox().Max.Z*Platform->GetComponentScale().Z;
  const FVector Position=GetActorTransform().TransformPosition(FVector(0,0,DeckHeight-PlatformTopOffsetCm));
  // Empty equipment reconfiguration preserves the installed deck datum. No load is moved.
  Platform->SetWorldLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
  WarehousePhysics::ConfigureContact(Platform,EWarehouseSurface::Steel,Powered ? 16.f : 120.f);Platform->SetSimulatePhysics(true);
  FBodyInstance* Body=Platform->GetBodyInstance(NAME_None,true);
  if (!Body || !Body->IsValidBodyInstance()) return false;
  PlatformJoint->SetConstrainedComponents(GuideAnchor,NAME_None,Platform,NAME_None);
  PlatformJoint->SetConstraintReferencePosition(EConstraintFrame::Frame2,FVector::ZeroVector);SetDeckLock(true);
  PlatformHeightCm=PlatformTargetCm=DeckHeight;
  ConfigureGateGuides();
  CarrierCommandCm=CarrierRampedCm=0.f;CarrierCommandAge=1.f;
  if (Powered) ConfigureCarrierRollers();
  return !Powered || (CarrierRollers.Num()==13 && CarrierRollerJoints.Num()==13);
 };
 if (!Install(Enabled))
 { Install(Previous);ShowStatus(TEXT("구동 캐리어 생성에 실패하여 원래 구성을 복구했습니다"));return false; }
 ShowStatus(Enabled ? TEXT("POWERED ROLLER CARRIER READY / 120 KG") : TEXT("MANUAL CARRIER RESTORED / 120 KG"));return true;
}

void AWarehousePalletLift::ConfigureCarrierRollers()
{
 UStaticMesh* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder"));
 if (!Cylinder) return;
 UMaterialInterface* Steel=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Warehouse/PalletLift/Materials/M_Lift_MachinedSteel"));
 const float DeckHeight=GetActorTransform().InverseTransformPosition(Platform->GetComponentLocation()).Z+PlatformTopOffsetCm;
 for (int32 Index=0;Index<13;++Index)
 {
  auto* Roller=NewObject<UStaticMeshComponent>(this,*FString::Printf(TEXT("PoweredCarrierRoller_%d"),Index));
  AddInstanceComponent(Roller);Roller->SetupAttachment(RootComponent);Roller->SetStaticMesh(Cylinder);
  if (Steel) Roller->SetMaterial(0,Steel);
  Roller->SetRelativeLocation(FVector(-57.f+Index*9.5f,0,DeckHeight-3.f));
  Roller->SetRelativeRotation(FRotator(0,0,90));Roller->SetRelativeScale3D(FVector(.06f,.06f,1.13f));
  Roller->SetCollisionProfileName(TEXT("PhysicsActor"));Roller->RegisterComponent();
  WarehousePhysics::ConfigureContact(Roller,EWarehouseSurface::Steel,8.f);Roller->SetSimulatePhysics(true);
  auto* Joint=NewObject<UPhysicsConstraintComponent>(this);
  AddInstanceComponent(Joint);Joint->RegisterComponent();
  Joint->SetWorldTransform(FTransform(FRotationMatrix::MakeFromXZ(GetActorRightVector(),GetActorUpVector()).ToQuat(),Roller->GetComponentLocation()));
  Joint->SetDisableCollision(true);Joint->SetProjectionEnabled(false);Joint->ConstraintInstance.DisableMassConditioning();
  Joint->SetLinearXLimit(LCM_Locked,0);Joint->SetLinearYLimit(LCM_Locked,0);Joint->SetLinearZLimit(LCM_Locked,0);
  Joint->SetAngularTwistLimit(ACM_Free,0);Joint->SetAngularSwing1Limit(ACM_Locked,0);Joint->SetAngularSwing2Limit(ACM_Locked,0);
  Joint->SetConstrainedComponents(Platform,NAME_None,Roller,NAME_None);
  Joint->SetAngularDriveMode(EAngularDriveMode::TwistAndSwing);Joint->SetAngularDriveAccelerationMode(false);
  Joint->SetAngularVelocityDriveTwistAndSwing(true,false);Joint->SetAngularDriveParams(0,30000.f,150000.f); // 15 Nm per roller.
  CarrierRollers.Add(Roller);CarrierRollerJoints.Add(Joint);
 }
}

void AWarehousePalletLift::DriveCarrierRollers(float Dt)
{
 if (!bPoweredConveyor) return;
 CarrierCommandAge+=Dt;
 float Target=CarrierCommandAge<=.35f && IsValid(ReservedConveyor) && IsLandingOpen(CurrentFloor) ? CarrierCommandCm : 0.f;
 if (!CabinFreeOfPeopleAndVehicles()) Target=0.f;
 CarrierRampedCm=FMath::FInterpConstantTo(CarrierRampedCm,Target,Dt,40.f);
 for (int32 Index=0;Index<CarrierRollerJoints.Num();++Index)
 {
  // Positive shaft rotation around +Y drives its upper contact towards +X.
  CarrierRollerJoints[Index]->SetAngularVelocityTarget(FVector(-CarrierRampedCm/(2.f*PI*3.f),0,0));
  if (FMath::Abs(CarrierRampedCm)>.01f) CarrierRollers[Index]->WakeAllRigidBodies();
 }
}

FString AWarehousePalletLift::ValidateLoad(AWarehousePallet* Pallet,TArray<AActor*>& Loads,float& MassKg) const
{
 Loads.Reset();MassKg=0.f;
 if (!HasPlatformSupport(Pallet)) return TEXT("PALLET NOT SUPPORTED BY CARRIER");
 if (!CabinFreeOfPeopleAndVehicles()) return TEXT("PERSON / FORKLIFT / FORKS IN CARRIER");
 Loads.Add(Pallet);
 if (auto* Damage=AWarehouseDamageSystem::Find(this))
 {
  Damage->EnsureSupportSnapshot();
  for (AActor* Actor:Damage->GetSupportedActors(Pallet,false)) if (IsValid(Actor)) Loads.AddUnique(Actor);
 }
 const TArray<FOverlapResult> Occupants=PalletLiftOccupants(this,PlatformHeightCm);
 // Unregistered loose cargo uses the same short, actual bearing trace, never an assumed attachment.
 for (int32 Pass=0;Pass<Occupants.Num();++Pass)
 {
  bool Added=false;
  for (const FOverlapResult& Contact:Occupants)
  {
   AActor* Actor=Contact.GetActor();
   if (!IsValid(Actor) || Loads.Contains(Actor) || !WarehousePhysics::IsPhysicalContact(Contact.GetComponent())) continue;
   if (Actor->IsA<AWarehousePallet>()) return TEXT("ONLY ONE PALLET ALLOWED");
   const FBox World=AWarehouseDamageSystem::GetPhysicalBounds(Actor);
   if (!World.IsValid) continue;
   FVector Point=World.GetCenter();Point.Z=World.Min.Z;
   TArray<FHitResult> Hits;FCollisionQueryParams Params(SCENE_QUERY_STAT(PalletLiftCargoBearing),false,Actor);
   Params.AddIgnoredActor(this);
   GetWorld()->LineTraceMultiByChannel(Hits,Point+FVector(0,0,2),Point-FVector(0,0,4),ECC_Visibility,Params);
   for (const FHitResult& Hit:Hits) if (Hit.bBlockingHit && WarehousePhysics::IsPhysicalContact(Hit.GetComponent()))
   {
    if (Hit.ImpactNormal.Z>.7f && Loads.Contains(Hit.GetActor())) { Loads.AddUnique(Actor);Added=true; }
    break;
   }
  }
  if (!Added) break;
 }
 for (const FOverlapResult& Contact:Occupants)
  if (WarehousePhysics::IsPhysicalContact(Contact.GetComponent()) && !Loads.Contains(Contact.GetActor())) return TEXT("FOREIGN OBJECT IN CARRIER");
 for (AActor* Actor:Loads)
 {
  if (Actor!=Pallet && Actor->IsA<AWarehousePallet>()) return TEXT("ONLY ONE PALLET ALLOWED");
  const FBox Box=PalletLiftLocalBounds(this,Actor);
  if (!Box.IsValid || Box.Min.X < -PalletLiftHalfDeck || Box.Max.X>PalletLiftHalfDeck ||
      Box.Min.Y < -PalletLiftHalfDeck || Box.Max.Y>PalletLiftHalfDeck ||
      Box.Min.Z<PlatformHeightCm-1.f || Box.Max.Z>PlatformHeightCm+PalletLiftLoadHeight)
   return TEXT("LOAD OUTSIDE 125 CM CARRIER / 220 CM HEIGHT");
 }
 MassKg=PalletLiftPhysicalMass(Loads);
 if (!FMath::IsFinite(MassKg) || MassKg<=0 || MassKg>FMath::Clamp(RatedMassKg,1.f,2000.f)) return TEXT("PALLET LIFT OVERLOAD");
 return FString();
}

bool AWarehousePalletLift::PortalClear() const
{
 TArray<FOverlapResult> Hits;FCollisionQueryParams Params(SCENE_QUERY_STAT(PalletLiftPortal),false,this);
 GetWorld()->OverlapMultiByObjectType(Hits,GetActorTransform().TransformPosition(FVector(-94,0,CurrentFloor*400+155)),GetActorQuat(),
  FCollisionObjectQueryParams::AllObjects,FCollisionShape::MakeBox(FVector(8,62.5f,114)),Params);
 for (const FOverlapResult& Hit:Hits) if (WarehousePhysics::IsPhysicalContact(Hit.GetComponent())) return false;
 return true;
}

bool AWarehousePalletLift::IsIdleForLayout() const
{
 if (State!=EWarehousePalletLiftState::Idle || AcceptedPallet || ReservedConveyor) return false;
 for (const FOverlapResult& Hit:PalletLiftOccupants(this,PlatformHeightCm))
  if (WarehousePhysics::IsPhysicalContact(Hit.GetComponent())) return false;
 return true;
}

bool AWarehousePalletLift::IsHandlingCargo(AActor* Cargo) const
{
 return IsValid(Cargo) && (AcceptedPallet==Cargo || Payload.Contains(Cargo));
}

bool AWarehousePalletLift::RequestTransfer(AWarehousePallet* Pallet,int32 FromFloor,int32 ToFloor)
{
 if (ReservedConveyor) { ShowStatus(TEXT("CONVEYOR TRANSFER RESERVED"));return false; }
 return AcceptTransfer(Pallet,FromFloor,ToFloor);
}

bool AWarehousePalletLift::AcceptTransfer(AWarehousePallet* Pallet,int32 FromFloor,int32 ToFloor)
{
 if (State!=EWarehousePalletLiftState::Idle || FromFloor!=CurrentFloor || ToFloor<0 || ToFloor>=FloorCount || ToFloor==FromFloor) return false;
 const FString Interlock=CheckInterlocks();
 if (!Interlock.IsEmpty()) { ShowStatus(Interlock);return false; }
 TArray<AActor*> Loads;float Mass=0.f;
 const FString Error=ValidateLoad(Pallet,Loads,Mass);
 if (!Error.IsEmpty()) { ShowStatus(Error);return false; }
 if (Platform->GetPhysicsLinearVelocity().Size()>2.f || Pallet->GetPalletBody()->GetPhysicsLinearVelocity().Size()>2.f)
 { ShowStatus(TEXT("WAIT FOR LOAD TO SETTLE"));return false; }
 AcceptedPallet=Pallet;Payload.Reset();for (AActor* Actor:Loads) Payload.Add(Actor);
 PayloadMassKg=Mass;DestinationFloor=ToFloor;bLoadedTransfer=true;StationarySeconds=0.f;SincePayloadCheck=.1f;LoadInterlock.Reset();
 SetState(EWarehousePalletLiftState::ClosingDoors,TEXT("PALLET ACCEPTED - WITHDRAW FORKS / CLOSE GATE"));
 return true;
}

bool AWarehousePalletLift::CallToFloor(int32 Floor)
{
 if (ReservedConveyor) { ShowStatus(TEXT("CONVEYOR TRANSFER RESERVED"));return false; }
 return CallEmpty(Floor);
}

bool AWarehousePalletLift::CallEmpty(int32 Floor)
{
 if (Floor<0 || Floor>=FloorCount || State!=EWarehousePalletLiftState::Idle) return false;
 const FString Error=CheckInterlocks();if (!Error.IsEmpty()) { ShowStatus(Error);return false; }
 if (!CabinFreeOfPeopleAndVehicles()) { ShowStatus(TEXT("PERSON / FORKLIFT / FORKS IN CARRIER"));return false; }
 for (const FOverlapResult& Hit:PalletLiftOccupants(this,PlatformHeightCm))
  if (WarehousePhysics::IsPhysicalContact(Hit.GetComponent())) { ShowStatus(TEXT("CARRIER IS NOT EMPTY"));return false; }
 DestinationFloor=Floor;bLoadedTransfer=false;
 if (Floor==CurrentFloor) { SetState(EWarehousePalletLiftState::OpeningDoors,TEXT("OPENING LOADING GATE"));return true; }
 SetState(EWarehousePalletLiftState::ClosingDoors,TEXT("EMPTY CARRIER CALL ACCEPTED"));return true;
}

bool AWarehousePalletLift::TryInteract(int32 PlayerFloor)
{
 if (ReservedConveyor || PlayerFloor<0 || PlayerFloor>=FloorCount || State!=EWarehousePalletLiftState::Idle) return false;
 if (PlayerFloor!=CurrentFloor) return CallToFloor(PlayerFloor);
 AWarehousePallet* Pallet=nullptr;
 for (const FOverlapResult& Hit:PalletLiftOccupants(this,PlatformHeightCm))
  if (auto* Candidate=Cast<AWarehousePallet>(Hit.GetActor());Candidate && WarehousePhysics::IsPhysicalContact(Hit.GetComponent()))
  {
   if (Pallet && Pallet!=Candidate) { ShowStatus(TEXT("ONLY ONE PALLET ALLOWED"));return false; }
   Pallet=Candidate;
  }
 return Pallet ? RequestTransfer(Pallet,CurrentFloor,(CurrentFloor+1)%FloorCount) : CallToFloor(PlayerFloor);
}

void AWarehousePalletLift::SetDeckLock(bool Locked)
{
 if (!PlatformJoint || !GuideAnchor) return;
 const float Height=Locked ? CurrentFloor*400.f+ConveyorHeightCm : ConveyorHeightCm+StrokeHalfHeight();
 const FVector World=GetActorTransform().TransformPosition(FVector(0,0,Height-PlatformTopOffsetCm));
 PlatformJoint->SetConstraintReferencePosition(EConstraintFrame::Frame1,GuideAnchor->GetComponentTransform().InverseTransformPositionNoScale(World));
 PlatformJoint->SetLinearZLimit(Locked ? LCM_Locked : LCM_Limited,Locked ? 0.f : StrokeHalfHeight());
 if (Locked) PlatformJoint->SetLinearPositionTarget(FVector::ZeroVector);
}

void AWarehousePalletLift::HoldPlatform()
{
 if (!PlatformJoint) return;
 PlatformTargetCm=PlatformHeightCm;
 PlatformJoint->SetLinearVelocityTarget(FVector::ZeroVector);
 PlatformJoint->SetLinearPositionTarget(FVector(0,0,ConveyorHeightCm+StrokeHalfHeight()-PlatformTargetCm-
  (120.f+PayloadMassKg)*980.f/PalletLiftSpring));
 Platform->WakeAllRigidBodies();
}

bool AWarehousePalletLift::AllDoorsClosed() const
{
 for (int32 Index=0;Index<ActiveGateCount();++Index)
 { const UStaticMeshComponent* Gate=Gates[Index];
  if (FMath::Abs(FMath::Abs(GetActorTransform().InverseTransformPosition(Gate->GetComponentLocation()).Y)-PalletLiftClosedGateY)>.5f) return false;
 }
 return true;
}

bool AWarehousePalletLift::SetDoors(bool Open,float Dt)
{
 if (!Open && !PortalClear()) { ShowStatus(TEXT("DOOR LIGHT CURTAIN BLOCKED"));return false; }
 const float Next=FMath::FInterpConstantTo(DoorOpeningCm,Open ? PalletLiftDoorTravel : 0.f,Dt,60.f);
 for (int32 Index=0;Index<ActiveGateCount();++Index)
 {
  const int32 Floor=Index/2;const float Side=Index%2 ? 1.f : -1.f;
  UStaticMeshComponent* Gate=Gates[Index];
  const float Target=Side*(PalletLiftClosedGateY+(Floor==CurrentFloor ? Next : 0.f));
  const float Actual=GetActorTransform().InverseTransformPosition(Gate->GetComponentLocation()).Y;
  const FVector Delta=GetActorRightVector()*(Target-Actual);
  TArray<FHitResult> Hits;FComponentQueryParams Params(SCENE_QUERY_STAT(PalletLiftGateSweep),this);
  GetWorld()->ComponentSweepMulti(Hits,Gate,Gate->GetComponentLocation(),Gate->GetComponentLocation()+Delta,Gate->GetComponentQuat(),Params);
  for (const FHitResult& Hit:Hits) if (Hit.bBlockingHit && WarehousePhysics::IsPhysicalContact(Hit.GetComponent()) &&
   (FVector::DotProduct(Delta,Hit.Normal)<-.001f || (Hit.bStartPenetrating && Hit.PenetrationDepth>.2f)))
  {
   if (Status!=TEXT("GATE MOTION OBSTRUCTED")) UE_LOG(LogTemp,Warning,TEXT("PALLET_LIFT_GATE_CONTACT %s / %s / %s depth=%.3f normal=%s"),
    *GetName(),Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("NONE"),Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("NONE"),Hit.PenetrationDepth,*Hit.Normal.ToString());
   ShowStatus(TEXT("GATE MOTION OBSTRUCTED"));return false;
  }
 }
 DoorOpeningCm=Next;
 for (int32 Index=0;Index<ActiveGateCount();++Index)
 {
  const int32 Floor=Index/2;const float Side=Index%2 ? 1.f : -1.f;
  const float Opening=Floor==CurrentFloor ? DoorOpeningCm : 0.f;
  if (GateJoints[Index]) GateJoints[Index]->SetLinearPositionTarget(FVector(0,Side*(PalletLiftDoorTravel*.5f-Opening),0));
  const float Target=Side*(PalletLiftClosedGateY+Opening);
  if (FMath::Abs(GetActorTransform().InverseTransformPosition(Gates[Index]->GetComponentLocation()).Y-Target)>.1f) Gates[Index]->WakeAllRigidBodies();
 }
 if (!Open) return AllDoorsClosed();
 for (int32 Index=CurrentFloor*2;Index<CurrentFloor*2+2;++Index)
  if (FMath::Abs(GetActorTransform().InverseTransformPosition(Gates[Index]->GetComponentLocation()).Y)-PalletLiftClosedGateY<PalletLiftDoorTravel-1.f) return false;
 return true;
}

bool AWarehousePalletLift::MovePlatform(float Dt)
{
 if (!AllDoorsClosed() || !CabinFreeOfPeopleAndVehicles())
 { HoldPlatform();ShowStatus(TEXT("DOOR / PERSON / FORKLIFT INTERLOCK"));return false; }
 const float Goal=ConveyorHeightCm+DestinationFloor*400.f;
 const float Next=FMath::FInterpConstantTo(PlatformTargetCm,Goal,Dt,FMath::Clamp(TravelSpeedCm,1.f,50.f));
 const FVector Delta(0,0,Next-PlatformHeightCm);
 FComponentQueryParams Params(SCENE_QUERY_STAT(PalletLiftStroke),this);
 for (AActor* Actor:Payload) if (IsValid(Actor)) Params.AddIgnoredActor(Actor);
 TArray<UPrimitiveComponent*> Moving{Platform};
 for (UStaticMeshComponent* Roller:CarrierRollers) Moving.Add(Roller);
 for (AActor* Actor:Payload) if (IsValid(Actor))
 { TInlineComponentArray<UPrimitiveComponent*> Parts(Actor);for (UPrimitiveComponent* Part:Parts) if (WarehousePhysics::IsPhysicalContact(Part)) Moving.Add(Part); }
 for (UPrimitiveComponent* Part:Moving)
 {
  TArray<FHitResult> Hits;
  GetWorld()->ComponentSweepMulti(Hits,Part,Part->GetComponentLocation(),Part->GetComponentLocation()+Delta,Part->GetComponentQuat(),Params);
  for (const FHitResult& Hit:Hits) if (Hit.bBlockingHit && WarehousePhysics::IsPhysicalContact(Hit.GetComponent()) && FVector::DotProduct(Delta,Hit.Normal)<-.001f)
  {
   if (Status!=TEXT("CARRIER / PAYLOAD PATH OBSTRUCTED")) UE_LOG(LogTemp,Warning,TEXT("PALLET_LIFT_STROKE_CONTACT %s moving=%s blocker=%s / %s depth=%.3f normal=%s point=%s"),
    *GetName(),*Part->GetName(),Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("NONE"),
    Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("NONE"),Hit.PenetrationDepth,*Hit.Normal.ToString(),*Hit.ImpactPoint.ToString());
   HoldPlatform();ShowStatus(TEXT("CARRIER / PAYLOAD PATH OBSTRUCTED"));return false;
  }
 }
 SetDeckLock(false);PlatformTargetCm=Next;
 PlatformJoint->SetLinearPositionTarget(FVector(0,0,ConveyorHeightCm+StrokeHalfHeight()-PlatformTargetCm-(120.f+PayloadMassKg)*980.f/PalletLiftSpring));
 Platform->WakeAllRigidBodies();
 if (FMath::Abs(PlatformHeightCm-Goal)>1.f || Platform->GetPhysicsLinearVelocity().Size()>2.f) StationarySeconds=0.f;
 else StationarySeconds+=Dt;
 if (StationarySeconds<.5f) return false;
 CurrentFloor=DestinationFloor;SetDeckLock(true);return true;
}

void AWarehousePalletLift::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if (!PlatformJoint || State==EWarehousePalletLiftState::Fault) return;
 PlatformHeightCm=GetActorTransform().InverseTransformPosition(Platform->GetComponentLocation()).Z+PlatformTopOffsetCm;
 const float Dt=FMath::Clamp(DeltaSeconds,0.f,.133333f);
 DriveCarrierRollers(Dt);
 const FString Interlock=CheckInterlocks();
 if (!Interlock.IsEmpty()) { if (State==EWarehousePalletLiftState::Moving) HoldPlatform();ShowStatus(Interlock);return; }
 SincePayloadCheck+=Dt;
 if (bLoadedTransfer && (State==EWarehousePalletLiftState::ClosingDoors || State==EWarehousePalletLiftState::Moving) && SincePayloadCheck>=.1f)
 {
  SincePayloadCheck=0.f;TArray<AActor*> Loads;float Mass=0.f;
  LoadInterlock=ValidateLoad(AcceptedPallet,Loads,Mass);
  if (LoadInterlock.IsEmpty() && Loads.Num()!=Payload.Num()) LoadInterlock=TEXT("ACCEPTED LOAD CHANGED / RECONFIRM PALLET");
  if (LoadInterlock.IsEmpty()) for (AActor* Actor:Payload) if (!IsValid(Actor) || !Loads.Contains(Actor)) LoadInterlock=TEXT("ACCEPTED LOAD CHANGED / SUPPORT LOST");
  if (LoadInterlock.IsEmpty()) PayloadMassKg=Mass;
 }
 if (!LoadInterlock.IsEmpty() && (State==EWarehousePalletLiftState::ClosingDoors || State==EWarehousePalletLiftState::Moving))
 { HoldPlatform();ShowStatus(LoadInterlock);return; }
 if (!bLoadedTransfer && (State==EWarehousePalletLiftState::ClosingDoors || State==EWarehousePalletLiftState::Moving))
  for (const FOverlapResult& Hit:PalletLiftOccupants(this,PlatformHeightCm))
   if (WarehousePhysics::IsPhysicalContact(Hit.GetComponent()))
   { HoldPlatform();ShowStatus(TEXT("UNREQUESTED LOAD IN EMPTY CARRIER - REMOVE LOAD"));return; }
 switch (State)
 {
 case EWarehousePalletLiftState::ClosingDoors:
  if (!CabinFreeOfPeopleAndVehicles()) { ShowStatus(TEXT("PERSON / FORKLIFT / FORKS IN CARRIER"));break; }
  if (SetDoors(false,Dt)) SetState(EWarehousePalletLiftState::Moving,TEXT("GATES LOCKED - MOVING CARRIER"));
  break;
 case EWarehousePalletLiftState::Moving:
  if (MovePlatform(Dt)) SetState(EWarehousePalletLiftState::OpeningDoors,TEXT("LEVEL ALIGNED - OPENING GATE"));
  break;
 case EWarehousePalletLiftState::OpeningDoors:
  if (SetDoors(true,Dt)) SetState(bLoadedTransfer ? EWarehousePalletLiftState::AwaitUnload : EWarehousePalletLiftState::Idle,
   bLoadedTransfer ? TEXT("REMOVE PALLET FROM CARRIER") : TEXT("READY / ONE PALLET / 2000 KG"));
  break;
 case EWarehousePalletLiftState::AwaitUnload:
 {
  if (!IsValid(AcceptedPallet))
  { SetState(EWarehousePalletLiftState::Fault,TEXT("ACCEPTED PALLET LOST - OPERATOR RECOVERY REQUIRED"));break; }
  bool LostCargo=false;
  for (AActor* Actor:Payload) if (!IsValid(Actor)) LostCargo=true;
  if (LostCargo)
  { SetState(EWarehousePalletLiftState::Fault,TEXT("ACCEPTED CARGO LOST - OPERATOR RECOVERY REQUIRED"));break; }
  bool StillInside=false;
  for (const FOverlapResult& Hit:PalletLiftOccupants(this,PlatformHeightCm))
   if (WarehousePhysics::IsPhysicalContact(Hit.GetComponent()) && Payload.Contains(Hit.GetActor())) StillInside=true;
  if (!StillInside && !HasPlatformSupport(AcceptedPallet) && PortalClear())
  {
   AcceptedPallet=nullptr;Payload.Reset();PayloadMassKg=0.f;bLoadedTransfer=false;LoadInterlock.Reset();++CompletedTransfers;
   SetState(EWarehousePalletLiftState::Idle,TEXT("UNLOADING CONFIRMED - TRANSFER COMPLETE"));
  }
  break;
 }
 default:break;
 }
}
