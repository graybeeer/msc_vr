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
#include "PhysicsEngine/BodySetup.h"
#include "Engine/StaticMesh.h"

namespace
{
bool AtWaitingPose(const AWarehouseForklift* Vehicle,const FTransform& Pose)
{
 const FVector Offset=Vehicle->GetActorLocation()-Pose.GetLocation();
 // Route arrival is 5 cm. Allow 1 cm for solver settling after the route
 // brakes, or a completed approach can wait forever just outside acceptance.
 // Floor level and a stopped vehicle remain mandatory; cabin docking is 1 cm.
 return Offset.SizeSquared2D()<36.f && FMath::Abs(Offset.Z)<1.f && FMath::Abs(Vehicle->CurrentSpeedCm)<1.f;
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
 for (const TCHAR* Name : {TEXT("ShaftFrame"),TEXT("GuideRails"),TEXT("ShaftEnclosure"),TEXT("Landing_L1"),TEXT("Landing_L2"),TEXT("Landing_L3"),TEXT("Bollards_L1"),TEXT("Bollards_L2"),TEXT("Bollards_L3")})
 {
  auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Cargo_%s"),Name));
  Mesh->SetupAttachment(RootComponent); Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
  CargoParts.Add(Mesh);
 }
 CargoCabin=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cargo_CabinShell"));
 CargoCabin->SetupAttachment(Platform); CargoCabin->SetAbsolute(false,false,true);
 CargoCabin->SetRelativeLocation(FVector(0,0,50));
 CargoCabin->SetCollisionProfileName(TEXT("BlockAllDynamic"));
 CargoDeckVisual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cargo_DeckVisual"));
 CargoDeckVisual->SetupAttachment(Platform); CargoDeckVisual->SetAbsolute(false,false,true);
 CargoDeckVisual->SetCollisionProfileName(TEXT("NoCollision"));
 for (int32 I=0;I<3;++I)
 {
  auto* Support=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Cargo_DeckSupport_%d"),I));
  Support->SetupAttachment(Platform); Support->SetStaticMesh(Cube.Object); Support->SetAbsolute(false,false,true);
  Support->SetCollisionProfileName(TEXT("NoCollision")); Support->SetVisibility(false); Support->SetHiddenInGame(true);
  CargoDeckSupports.Add(Support);
 }
}
void AWarehouseElevator::OnConstruction(const FTransform& Transform)
{
 Super::OnConstruction(Transform); ApplyCargoModel();
}
void AWarehouseElevator::ConfigureModelCollision(UStaticMesh* Mesh,FName Part)
{
 if (!Mesh) return;
 Mesh->CreateBodySetup(); auto* Setup=Mesh->GetBodySetup();
 Setup->AggGeom.EmptyElements();
 const FString Name=Part.ToString();
 auto Box=[&](FVector Center,FVector Size)
 { FKBoxElem B; B.Center=Center; B.X=Size.X; B.Y=Size.Y; B.Z=Size.Z; Setup->AggGeom.BoxElems.Add(B); };
 if (Name==TEXT("LiftPlatform"))
 {
  Box(FVector::ZeroVector,FVector(350,240,10));
  for (float X : {-110.f,0.f,110.f}) Box(FVector(X,0,-7),FVector(13,234,6));
 }
 // The guided leaf clears the sill by 1 cm. It must not scrape a static
 // floor while the vertical guide simultaneously locks its height.
 else if (Name.StartsWith(TEXT("Gate_"))) Box(FVector::ZeroVector,FVector(6,119.6f,268));
 else if (Name==TEXT("CabinShell"))
 {
  for (float Side : {-1.f,1.f})
  {
   Box(FVector(5,Side*122.5f,142),FVector(340,4.5f,284));
   for (float Z : {48.f,103.f}) Box(FVector(5,Side*117.6f,Z),FVector(328,6.5f,10));
   Box(FVector(0,Side*112,279),FVector(315,10,4.5f));
  }
  Box(FVector(176.5f,0,142),FVector(4,249,284));
  Box(FVector(5,0,286.5f),FVector(340,249,5));
  for (float Z : {48.f,103.f}) Box(FVector(172,0,Z),FVector(7,232,10));
 }
 Setup->CollisionTraceFlag=Setup->AggGeom.GetElementCount() ? CTF_UseSimpleAndComplex : CTF_UseComplexAsSimple;
 Setup->InvalidatePhysicsData(); Setup->CreatePhysicsMeshes(); Mesh->MarkPackageDirty();
}
void AWarehouseElevator::ApplyCargoModel()
{
 if (!bUseCargoModel) return;
 auto Load=[](const FString& Name) { return LoadObject<UStaticMesh>(nullptr,*(TEXT("/Game/Warehouse/Elevator/Meshes/SM_ELV_")+Name+TEXT(".SM_ELV_")+Name)); };
 auto Assign=[&](UStaticMeshComponent* Part,const FString& Name)
 {
  if (auto* Mesh=Load(Name))
  {
   Part->SetStaticMesh(Mesh); Part->EmptyOverrideMaterials(); Part->SetRelativeScale3D(FVector::OneVector);
   Part->SetVisibility(true); Part->SetHiddenInGame(false);
  }
 };
 // Preserve the validated, nonuniformly scaled native floor shape. Baking
 // that same box into centimetre-sized FBX collision changes Chaos contact
 // margins at the 1 cm floor opening seam. The separate art skin has no
 // duplicate floor contact; its beams have dedicated physical proxies.
 Platform->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
 Platform->SetRelativeScale3D(FVector(3.5f,2.4f,.1f));
 Platform->SetVisibility(false); Platform->SetHiddenInGame(true);
 Assign(CargoDeckVisual,TEXT("LiftPlatform"));
 CargoDeckVisual->SetRelativeLocation(FVector::ZeroVector);
 for (int32 I=0;I<CargoDeckSupports.Num();++I)
 {
  auto* Support=CargoDeckSupports[I].Get();
  Support->SetRelativeLocation(FVector((I-1)*110.f/3.5f,0,-70));
  Support->SetRelativeScale3D(FVector(.13f,2.34f,.06f));
  Support->SetCollisionProfileName(TEXT("BlockAllDynamic"));
 }
 for (int32 I=0;I<Gates.Num();++I) Assign(Gates[I],FString::Printf(TEXT("Gate_%d_%d"),I/2,I%2 ? 1 : -1));
 Assign(CargoCabin,TEXT("CabinShell"));
 CargoCabin->SetRelativeLocation(FVector(0,0,50));
 for (const auto& Part : CargoParts) Assign(Part.Get(),Part->GetName().RightChop(6));
 TArray<UStaticMeshComponent*> Parts; GetComponents(Parts);
 for (auto* Part : Parts)
  if (Part->GetName().StartsWith(TEXT("Column_")) || Part->GetName().StartsWith(TEXT("SideFence_")) || Part->GetName().StartsWith(TEXT("BackFence_")) || Part->GetName().StartsWith(TEXT("DoorHeader_")))
  {
   Part->SetVisibility(false); Part->SetHiddenInGame(true);
   Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  }
 Display->SetVisibility(false); Display->SetHiddenInGame(true);
}
void AWarehouseElevator::BeginPlay()
{
 InitialShaftPosition=GetActorLocation();
 ApplyCargoModel();
 Super::BeginPlay();
 TArray<UStaticMeshComponent*> Parts; GetComponents(Parts);
 UStaticMeshComponent* Anchor=nullptr;
 for (auto* Part : Parts)
 {
  if (Part->GetName().StartsWith(TEXT("Column_")) && !Anchor) Anchor=Part;
  if (WarehousePhysics::IsPhysicalContact(Part)) WarehousePhysics::ConfigureContact(Part,EWarehouseSurface::Steel);
 }
 if (bUseCargoModel)
 {
  // The guide must constrain the actual fixed frame, not an invisible legacy
  // cube. Its existing DisableCollision flag then covers the real mounting pair.
  for (const auto& Part : CargoParts)
   if (Part->GetName()==TEXT("Cargo_ShaftFrame") && Part->GetStaticMesh()) Anchor=Part.Get();
  if (Anchor)
   for (const auto& Part : CargoParts)
    if (Part->GetName()==TEXT("Cargo_GuideRails") || Part->GetName()==TEXT("Cargo_ShaftEnclosure"))
    {
     Part->AttachToComponent(Anchor,FAttachmentTransformRules(EAttachmentRule::KeepWorld,true));
     Part->WeldTo(Anchor,NAME_None,true);
    }
 }
 if (!Anchor) return;
 GuideAnchor=Anchor;
 auto Guide=[&](UStaticMeshComponent* Body,const FTransform& Frame)
 {
  auto* Joint=NewObject<UPhysicsConstraintComponent>(this);
  AddInstanceComponent(Joint); Joint->RegisterComponent(); Joint->SetWorldTransform(Frame);
  Joint->SetDisableCollision(true); Joint->SetProjectionEnabled(false);
  Joint->ConstraintInstance.DisableMassConditioning();
  Joint->ConstraintInstance.DisableParentDominates();
  Joint->SetLinearXLimit(LCM_Locked,0); Joint->SetLinearYLimit(LCM_Locked,0); Joint->SetLinearZLimit(LCM_Locked,0);
  Joint->SetAngularTwistLimit(ACM_Locked,0); Joint->SetAngularSwing1Limit(ACM_Locked,0); Joint->SetAngularSwing2Limit(ACM_Locked,0);
  Joint->SetConstrainedComponents(Anchor,NAME_None,Body,NAME_None);
  Joint->SetLinearDriveAccelerationMode(false);
  return Joint;
 };
 WarehousePhysics::ConfigureContact(Platform,EWarehouseSurface::Steel,500.f);
 if (bUseCargoModel && CargoCabin->GetStaticMesh())
 {
  // Set each child's own body before auto-welding; the component setter can
  // resolve an already welded child to its parent and override the wrong body.
  CargoCabin->BodyInstance.SetMassOverride(100.f,true);
  for (const auto& Support : CargoDeckSupports) Support->BodyInstance.SetMassOverride(30.f,true);
  Platform->BodyInstance.SetMassOverride(310.f,true);
 }
 Platform->SetSimulatePhysics(true);
 if (bUseCargoModel && CargoCabin->GetStaticMesh())
 {
  // Chaos sums overrides of welded children. Preserve the original 500 kg
  // moving assembly as a 400 kg deck (310 + 3*30) and a 100 kg cabin, rather than adding
  // the imported shell's volume-derived mass to a 500 kg deck.
  CargoCabin->WeldTo(Platform,NAME_None,true);
  for (const auto& Support : CargoDeckSupports) Support->WeldTo(Platform,NAME_None,true);
  Platform->BodyInstance.UpdateMassProperties();
 }
 PlatformJoint=Guide(Platform,FTransform(GetActorQuat(),GetActorTransform().TransformPosition(FVector(0,0,395))));
 PlatformJoint->SetConstraintReferencePosition(EConstraintFrame::Frame2,FVector::ZeroVector);
 PlatformJoint->SetLinearZLimit(LCM_Limited,400);
 PlatformJoint->SetLinearPositionDrive(false,false,true); PlatformJoint->SetLinearVelocityDrive(false,false,true);
 PlatformJoint->SetLinearDriveParams(300000.f,50000.f,4000000.f); // 40kN, includes platform and payload weight.
 PlatformJoint->SetLinearPositionTarget(FVector(0,0,400));
 SetDeckLock(true);
 for (int32 I=0;I<Gates.Num();++I)
 {
  auto* Gate=Gates[I].Get();
  WarehousePhysics::ConfigureContact(Gate,EWarehouseSurface::Steel,80.f);
  const float Side=I%2 ? 1.f : -1.f;
  const FVector Middle=Gate->GetComponentLocation()+GetActorRightVector()*Side*62.5f;
  Gate->SetSimulatePhysics(true);
  auto* Joint=Guide(Gate,FTransform(GetActorQuat(),Middle));
  Joint->SetConstraintReferencePosition(EConstraintFrame::Frame2,FVector::ZeroVector);
  Joint->SetLinearYLimit(LCM_Limited,62.5f);
  Joint->SetLinearPositionDrive(false,true,false); Joint->SetLinearVelocityDrive(false,true,false);
  Joint->SetLinearDriveParams(12000.f,2500.f,60000.f); // finite 600N door drive.
  Joint->SetLinearPositionTarget(FVector(0,Side*62.5f,0)); DoorJoints.Add(Joint);
 }
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
void AWarehouseElevator::SetDeckLock(bool Locked)
{
 if (!PlatformJoint || !GuideAnchor) return;
 const FVector AnchorWorld=GetActorTransform().TransformPosition(FVector(0,0,Locked ? CurrentFloor*400.f-5.f : 395.f));
 PlatformJoint->SetConstraintReferencePosition(EConstraintFrame::Frame1,GuideAnchor->GetComponentTransform().InverseTransformPositionNoScale(AnchorWorld));
 PlatformJoint->SetLinearZLimit(Locked ? LCM_Locked : LCM_Limited,Locked ? 0.f : 400.f);
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
 if (!IsValid(Vehicle) || FromFloor<0 || FromFloor>2 || ToFloor<0 || ToFloor>2 || FromFloor==ToFloor ||
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
bool AWarehouseElevator::IsCertifiedLandingSeam(AWarehouseForklift* Vehicle,const UStaticMeshComponent* Wheel,const FHitResult& Contact) const
{
 // A tyre can span the real 6 cm shaft clearance while its centre ray lies
 // in air. Prove both coplanar bearings at this PLC-authorized doorway; this
 // does not grant a route over arbitrary gaps or disable physical contact.
 auto* Surface=Contact.GetComponent();
 if (!Wheel || !Wheel->GetStaticMesh() || !Surface || Surface->IsSimulatingPhysics() || !Contact.GetActor() ||
     !Contact.GetActor()->ActorHasTag(TEXT("WarehouseMapStructure")) ||
     Contact.Normal.Z<.98f || Contact.PenetrationDepth>.5f) return false;
 const FVector Local=GetActorTransform().InverseTransformPosition(Contact.ImpactPoint);
 if (Local.X<-182 || Local.X>-174 || FMath::Abs(Local.Y)>115 ||
     FMath::Abs(Contact.ImpactPoint.Z-FloorHeight(CurrentFloor))>.5f) return false;
 const FVector Centre=Wheel->GetComponentLocation();
 // A rotated world AABB grows as the tyre spins; it is not the tyre radius.
 const float Radius=Wheel->GetStaticMesh()->GetBoundingBox().GetExtent().Z*Wheel->GetComponentScale().GetAbs().Z;
 if (FVector::Dist2D(Centre,Contact.ImpactPoint)>Radius ||
     FMath::Abs(Centre.Z-Radius-FloorHeight(CurrentFloor))>.5f) return false;
 FHitResult Deck,Slab;
 const FVector OnDeck=GetActorTransform().TransformPosition(FVector(-170,Local.Y,CurrentFloor*400));
 if (!TraceCertifiedPlatformSupport(Vehicle,OnDeck,Deck)) return false;
 const FVector OnSlab=GetActorTransform().TransformPosition(FVector(-182.5f,Local.Y,CurrentFloor*400));
 FCollisionQueryParams Params(SCENE_QUERY_STAT(CertifiedLiftSeam),false,Vehicle);
 return Surface->LineTraceComponent(Slab,OnSlab+FVector(0,0,2),OnSlab-FVector(0,0,2),Params) &&
     Slab.ImpactNormal.Z>.98f && FMath::Abs(Slab.ImpactPoint.Z-Deck.ImpactPoint.Z)<.5f;
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
bool AWarehouseElevator::AreLandingDoorsClosed() const
{
 if (Gates.Num()!=6) return false;
 for (int32 I=0;I<Gates.Num();++I)
 {
  if (!IsValid(Gates[I])) return false;
  const float Y=GetActorTransform().InverseTransformPosition(Gates[I]->GetComponentLocation()).Y;
  if (FMath::Abs(Y-(I%2 ? 60.f : -60.f))>=.5f || Gates[I]->GetPhysicsLinearVelocity().Size()>2.f) return false;
 }
 return true;
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
  const float Opening=Floor==CurrentFloor ? DoorOpening : 0.f;
  if (DoorJoints.IsValidIndex(I)) DoorJoints[I]->SetLinearPositionTarget(FVector(0,Side*(62.5f-Opening),0));
  // Updating a Chaos drive target does not wake a settled native body.
  const float ActualY=GetActorTransform().InverseTransformPosition(Gates[I]->GetComponentLocation()).Y;
  if (FMath::Abs(ActualY-Side*(60.f+Opening))>.1f) Gates[I]->WakeAllRigidBodies();
 }
 const float Actual=ActualDoorOpening();
 return Open ? Actual>=124.f : AreLandingDoorsClosed();
}
bool AWarehouseElevator::MovePlatform(int32 Floor,float Dt)
{
 if (!AreLandingDoorsClosed() || !CabinClear() || (bBoarded && !FitsInside(ReservedVehicle))) return false;
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
 if (PlatformJoint) PlatformJoint->SetLinearPositionTarget(FVector(0,0,400-PlatformTarget-Mass*980.f/300000.f));
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
