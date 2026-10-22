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
 // The route stops within 5 cm; allow 1 cm for finite-brake settling.
 // Floor level and a stopped vehicle remain mandatory. Cabin docking is 1 cm.
 return Offset.SizeSquared2D()<36.f && FMath::Abs(Offset.Z)<1.f && FMath::Abs(Vehicle->CurrentSpeedCm)<1.f;
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
 auto CargoPart=[&](const FString& Name)
 {
  auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Cargo_%s"),*Name));
  Mesh->SetupAttachment(RootComponent); Mesh->SetCollisionProfileName(TEXT("NoCollision"));
  CargoParts.Add(Mesh);
 };
 for (const TCHAR* Name:{TEXT("ShaftFrame"),TEXT("GuideRails"),TEXT("ShaftEnclosure")}) CargoPart(Name);
 for (int32 Floor=1;Floor<=4;++Floor)
  for (const TCHAR* Name:{TEXT("Landing"),TEXT("Bollards"),TEXT("CallStand")})
   CargoPart(FString::Printf(TEXT("%s_L%d"),Name,Floor));
 CargoCabin=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cargo_CabinShell"));
 CargoCabin->SetupAttachment(Platform); CargoCabin->SetAbsolute(false,false,true);
 CargoCabin->SetRelativeLocation(FVector(0,0,50)); CargoCabin->SetCollisionProfileName(TEXT("NoCollision"));
 CargoDeckVisual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cargo_DeckVisual"));
 CargoDeckVisual->SetupAttachment(Platform); CargoDeckVisual->SetAbsolute(false,false,true);
 CargoDeckVisual->SetCollisionProfileName(TEXT("NoCollision"));
 for (int32 I=0;I<3;++I)
 {
  auto* Support=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Cargo_DeckSupport_%d"),I));
  Support->SetupAttachment(Platform);Support->SetStaticMesh(Cube.Object);Support->SetAbsolute(false,false,true);
  Support->SetCollisionProfileName(TEXT("NoCollision"));Support->SetVisibility(false);Support->SetHiddenInGame(true);
  CargoDeckSupports.Add(Support);
 }
}
void AWarehouseElevator::OnConstruction(const FTransform& Transform)
{
 Super::OnConstruction(Transform); ConfigureFloors(); ApplyCargoModel();
}
void AWarehouseElevator::ConfigureModelCollision(UStaticMesh* Mesh,FName Part)
{
#if WITH_EDITOR
 if (!Mesh) return;
 Mesh->Modify();Mesh->CreateBodySetup();auto* Setup=Mesh->GetBodySetup();
 Setup->AggGeom.EmptyElements();const FString Name=Part.ToString();
 auto Box=[&](FVector Center,FVector Size)
 { FKBoxElem B;B.Center=Center;B.X=Size.X;B.Y=Size.Y;B.Z=Size.Z;Setup->AggGeom.BoxElems.Add(B); };
 auto Cylinder=[&](FVector Center,float Radius,float Height,bool AlongX=false)
 {
  FKConvexElem Hull;
  for (int32 I=0;I<24;++I) for (float End:{-.5f,.5f})
  {
   const float Angle=I*2.f*PI/24.f;
   const float A=Radius*FMath::Cos(Angle),B=Radius*FMath::Sin(Angle);
   Hull.VertexData.Add(Center+(AlongX ? FVector(End*Height,A,B) : FVector(A,B,End*Height)));
  }
  Hull.UpdateElemBox();Setup->AggGeom.ConvexElems.Add(MoveTemp(Hull));
 };
 // Match the separate solids in build_cargo_elevator.py / its call-stand
 // refinement (metres there, centimetres here). A whole-mesh convex hull
 // would fill the doorway/shaft; triangle-only bodies cannot be the query
 // shape in native component overlaps. Keep those openings genuinely empty.
 const float FloorZ=(FMath::Clamp(FCString::Atoi(*Name.Right(1)),1,4)-1)*400.f;
 if (Name==TEXT("LiftPlatform"))
 {
  Box(FVector::ZeroVector,FVector(350,240,10));
  for (float X:{-110.f,0.f,110.f}) Box(FVector(X,0,-7),FVector(13,234,6));
 }
 else if (Name.StartsWith(TEXT("Gate_"))) Box(FVector::ZeroVector,FVector(6,119.6f,268));
 else if (Name==TEXT("CabinShell"))
 {
  for (float Side:{-1.f,1.f})
  {
   Box(FVector(5,Side*122.5f,142),FVector(340,4.5f,284));
   for (float Z:{48.f,103.f}) Box(FVector(5,Side*117.6f,Z),FVector(328,6.5f,10));
   Box(FVector(0,Side*112,279),FVector(315,10,4.5f));
  }
  Box(FVector(176.5f,0,142),FVector(4,249,284));Box(FVector(5,0,286.5f),FVector(340,249,5));
  for (float Z:{48.f,103.f}) Box(FVector(172,0,Z),FVector(7,232,10));
 }
 else if (Name==TEXT("ShaftFrame"))
 {
  for (float X:{-187.f,192.f}) for (float Y:{-132.f,132.f})
  {
   Box(FVector(X,Y,580),FVector(16,16,1160));
   Box(FVector(X,Y,-5),FVector(24,22,10));
  }
  for (float Z:{0.f,400.f,800.f,1155.f})
  {
   for (float Y:{-137.f,137.f}) Box(FVector(0,Y,Z),FVector(380,12,20));
   Box(FVector(192,0,Z),FVector(16,280,20));
  }
 }
 else if (Name==TEXT("GuideRails"))
 {
  for (float Y:{-108.f,108.f})
  {
   Box(FVector(188.5f,Y,560),FVector(7,8,1130));
   for (float Z:{80.f,380.f,680.f,980.f}) Box(FVector(193.5f,Y,Z),FVector(10,19,6.5f));
  }
 }
 else if (Name==TEXT("ShaftEnclosure"))
 {
  for (int32 Floor=0;Floor<3;++Floor)
  {
   const float Height=Floor<2 ? 395.f : 360.f,Z=Floor*400.f+Height*.5f;
   for (float Side:{-1.f,1.f}) Box(FVector(0,Side*136,Z),FVector(372,3.5f,Height));
   Box(FVector(196,0,Z),FVector(3.5f,280,Height));
  }
 }
 else if (Name.StartsWith(TEXT("Landing_L")))
 {
  for (float Side:{-1.f,1.f})
  {
   for (float Offset:{-2.075f,2.075f}) Box(FVector(-219,Side*127+Offset,FloorZ+135),FVector(17,4.5f,270));
   Box(FVector(-194,Side*126.5f,FloorZ+135),FVector(6,13,270));
   Box(FVector(-216,Side*127,FloorZ+135),FVector(4,14,270));
  }
  Box(FVector(-213,0,FloorZ+286),FVector(18,276,32));
  Box(FVector(-223,0,FloorZ+302.5f),FVector(5,278,1.8f));
  Box(FVector(-204,0,FloorZ+282),FVector(10,512,8.5f));
  Box(FVector(-203,0,FloorZ-2.5f),FVector(33,248,5));
  Box(FVector(-223.2f,-79,FloorZ+288),FVector(.8f,34,13));
 }
 else if (Name.StartsWith(TEXT("Bollards_L")))
 {
  for (float Side:{-1.f,1.f})
  {
   Cylinder(FVector(-249,Side*152,FloorZ+48),9,96);
   for (float Z:{38.f,70.f}) Cylinder(FVector(-249,Side*152,FloorZ+Z),9.2f,12);
   Cylinder(FVector(-249,Side*152,FloorZ+96.5f),9.2f,3.5f);
   Box(FVector(-249,Side*152,FloorZ+1.5f),FVector(25,25,3));
   for (float X:{-8.f,8.f}) for (float Y:{-8.f,8.f})
    Cylinder(FVector(-249+X,Side*152+Y,FloorZ+3.3f),1.2f,1.4f);
  }
 }
 else if (Name.StartsWith(TEXT("CallStand_L")))
 {
  // Unlike landing meshes, the call-stand mesh origin is its own floor base.
  Box(FVector(0,0,1.8f),FVector(30,28,3.6f));
  Box(FVector(0,0,5),FVector(13,15,6));
  Box(FVector(1.2f,0,49),FVector(7.5f,8.5f,84));
  Box(FVector(0,0,92.5f),FVector(8,10,6));
  Box(FVector(0,0,119),FVector(14,23,52));
  Box(FVector(-7.3f,0,119),FVector(.8f,19,47));
  Box(FVector(-7.9f,0,132),FVector(.6f,15,10.5f));
  for (float Z:{120.f,109.f})
  {
   Cylinder(FVector(-8.4f,0,Z),2.9f,1.5f,true);
   Cylinder(FVector(-9.4f,0,Z),2.2f,.9f,true);
  }
  Box(FVector(-8,0,98.5f),FVector(1.2f,9,7));
  Cylinder(FVector(-10.3f,0,98.5f),2.3f,3,true);
  for (float X:{-10.5f,10.5f}) for (float Y:{-9.5f,9.5f}) Cylinder(FVector(X,Y,4.4f),1.1f,1.4f);
 }
 Setup->CollisionTraceFlag=Setup->AggGeom.GetElementCount() ? CTF_UseSimpleAndComplex : CTF_UseComplexAsSimple;
 Setup->InvalidatePhysicsData();Setup->CreatePhysicsMeshes();Mesh->MarkPackageDirty();
#endif
}
void AWarehouseElevator::ApplyCargoModel()
{
 // Replacing or un-welding a moving body while its live guide exists is unsafe.
 // Floor edits use UpdateCargoModelFloors and retain those bodies and joints.
 if (HasActorBegunPlay()) return;
 auto Load=[](const FString& Name)
 { return LoadObject<UStaticMesh>(nullptr,*(TEXT("/Game/Warehouse/Elevator/Meshes/SM_ELV_")+Name+TEXT(".SM_ELV_")+Name)); };
 TMap<FString,UStaticMesh*> Models;
 if (bUseCargoModel)
 {
  for (const auto& Part:CargoParts) Models.Add(Part->GetName().RightChop(6),Load(Part->GetName().RightChop(6)));
  for (int32 I=0;I<Gates.Num();++I)
  {
   const FString Name=FString::Printf(TEXT("Gate_%d_%d"),I/2,I%2 ? 1 : -1);Models.Add(Name,Load(Name));
  }
  Models.Add(TEXT("CabinShell"),Load(TEXT("CabinShell")));Models.Add(TEXT("LiftPlatform"),Load(TEXT("LiftPlatform")));
 }
 // A partially installed rig falls back as a whole, with no missing barriers.
 bCargoModelApplied=bUseCargoModel;
 for (const auto& Pair:Models) if (!Pair.Value) { bCargoModelApplied=false;break; }
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
 Platform->SetStaticMesh(Cube);Platform->SetRelativeScale3D(FVector(3.5f,2.4f,.1f));
 Platform->SetVisibility(!bCargoModelApplied);Platform->SetHiddenInGame(bCargoModelApplied);
 Platform->SetCollisionProfileName(TEXT("BlockAllDynamic"));
 auto Assign=[&](UStaticMeshComponent* Part,const FString& Name,bool Physical)
 {
  Part->SetStaticMesh(bCargoModelApplied ? Models.FindRef(Name) : nullptr);
  Part->EmptyOverrideMaterials();Part->SetRelativeScale3D(FVector::OneVector);
  Part->SetVisibility(bCargoModelApplied);Part->SetHiddenInGame(!bCargoModelApplied);
  Part->SetCollisionProfileName(bCargoModelApplied && Physical ? TEXT("BlockAllDynamic") : TEXT("NoCollision"));
 };
 Assign(CargoDeckVisual,TEXT("LiftPlatform"),false);CargoDeckVisual->SetRelativeLocation(FVector::ZeroVector);
 Assign(CargoCabin,TEXT("CabinShell"),true);CargoCabin->SetRelativeLocation(FVector(0,0,50));
 for (int32 I=0;I<CargoDeckSupports.Num();++I)
 {
  auto* Support=CargoDeckSupports[I].Get();
  Support->SetRelativeLocation(FVector((I-1)*110.f/3.5f,0,-70));Support->SetRelativeScale3D(FVector(.13f,2.34f,.06f));
  Support->SetCollisionProfileName(bCargoModelApplied ? TEXT("BlockAllDynamic") : TEXT("NoCollision"));
 }
 auto* Yellow=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Warehouse/Materials/MI_SafetyYellow.MI_SafetyYellow"));
 for (int32 I=0;I<Gates.Num();++I)
 {
  auto* Gate=Gates[I].Get();Gate->SetStaticMesh(bCargoModelApplied ? Models.FindRef(FString::Printf(TEXT("Gate_%d_%d"),I/2,I%2 ? 1 : -1)) : Cube);
  Gate->EmptyOverrideMaterials();Gate->SetRelativeScale3D(bCargoModelApplied ? FVector::OneVector : FVector(.06,1.2,2.7));
  if (!bCargoModelApplied) Gate->SetMaterial(0,Yellow);
 }
 for (const auto& Part:CargoParts) Assign(Part.Get(),Part->GetName().RightChop(6),true);
 Display->SetVisibility(!bCargoModelApplied);Display->SetHiddenInGame(bCargoModelApplied);
 ConfigureFloors();
}
void AWarehouseElevator::UpdateCargoModelFloors()
{
 for (const auto& Part:CargoParts)
 {
  const FString Name=Part->GetName();int32 Floor=INDEX_NONE;
  const int32 Suffix=Name.Find(TEXT("_L"),ESearchCase::CaseSensitive,ESearchDir::FromEnd);
  if (Suffix!=INDEX_NONE) Floor=FCString::Atoi(*Name.Mid(Suffix+2))-1;
  const bool Active=bCargoModelApplied && Part->GetStaticMesh() && (Floor==INDEX_NONE || Floor<FloorCount);
  Part->SetVisibility(Active);Part->SetHiddenInGame(!Active);
  Part->SetCollisionEnabled(Active ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
  if (!Active) continue;
  FVector Position=Name.StartsWith(TEXT("Cargo_CallStand_")) ? FVector(-285,-205,Floor*400.f) : FVector::ZeroVector;
  FVector Scale=FVector::OneVector;
  if (Floor==INDEX_NONE)
  {
   const FBox Bounds=Part->GetStaticMesh()->GetBoundingBox();
   Scale.Z=(Bounds.GetSize().Z+(FloorCount-3)*400.f)/Bounds.GetSize().Z;
   Position.Z=Bounds.Min.Z*(1.f-Scale.Z); // Extend the top while retaining its actual foundation.
  }
  // Some fixed parts are welded to the stable first-floor landing at runtime.
  // Author their transform against the shaft, independent of that parent.
  Part->SetWorldTransform(FTransform(FQuat::Identity,Position,Scale)*GetActorTransform());
 }
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
    const bool LegacyFrame=bCargoModelApplied && !Gates.Contains(Part);
    Part->SetVisibility(Active && !LegacyFrame); Part->SetHiddenInGame(!Active || LegacyFrame);
    Part->SetCollisionEnabled(Active && !LegacyFrame ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    if (PhysicalGate && Active && !Part->IsSimulatingPhysics()) Part->SetSimulatePhysics(true);
   }
  if (Name.StartsWith(TEXT("Column_")))
  {
   Part->SetVisibility(!bCargoModelApplied);Part->SetHiddenInGame(bCargoModelApplied);
   Part->SetCollisionEnabled(bCargoModelApplied ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
  }
 }
 UpdateCargoModelFloors();
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
 ApplyCargoModel();
 Super::BeginPlay();
 ConfigureFloors();
 TArray<UStaticMeshComponent*> Parts; GetComponents(Parts);
 UStaticMeshComponent* Anchor=nullptr;
 for (auto* Part : Parts)
 {
  if (Part->GetName()==TEXT("DoorHeader_0")) Anchor=Part;
  if (WarehousePhysics::IsPhysicalContact(Part)) WarehousePhysics::ConfigureContact(Part,EWarehouseSurface::Steel);
 }
 if (bCargoModelApplied)
 {
  // This base landing never scales when the number of floors changes. Keep
  // the canonical main-branch guide frames on that stable physical anchor.
  for (const auto& Part:CargoParts) if (Part->GetName()==TEXT("Cargo_Landing_L1")) Anchor=Part.Get();
  if (Anchor) for (const auto& Part:CargoParts)
   if (Part->GetName()==TEXT("Cargo_ShaftFrame") || Part->GetName()==TEXT("Cargo_GuideRails") || Part->GetName()==TEXT("Cargo_ShaftEnclosure"))
   {
    Part->AttachToComponent(Anchor,FAttachmentTransformRules(EAttachmentRule::KeepWorld,true));Part->WeldTo(Anchor,NAME_None,true);
   }
 }
 if (!Anchor) return;
 GuideAnchor=Anchor;
 WarehousePhysics::ConfigureContact(Platform,EWarehouseSurface::Steel,500.f);
 if (bCargoModelApplied)
 {
  CargoCabin->BodyInstance.SetMassOverride(100.f,true);
  for (const auto& Support:CargoDeckSupports) Support->BodyInstance.SetMassOverride(30.f,true);
  Platform->BodyInstance.SetMassOverride(310.f,true);
 }
 Platform->SetSimulatePhysics(true);
 if (bCargoModelApplied)
 {
  CargoCabin->WeldTo(Platform,NAME_None,true);
  for (const auto& Support:CargoDeckSupports) Support->WeldTo(Platform,NAME_None,true);
  Platform->BodyInstance.UpdateMassProperties();
 }
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
bool AWarehouseElevator::IsCertifiedLandingSeam(AWarehouseForklift* Vehicle,const UStaticMeshComponent* Wheel,const FHitResult& Contact) const
{
 // A tyre spans the 6 cm shaft clearance while its centre ray lies in air.
 // Require both actual bearings at the PLC-authorized, aligned doorway.
 auto* Surface=Contact.GetComponent();
 if (!Wheel || !Wheel->GetStaticMesh() || !Surface || Surface->IsSimulatingPhysics() || !Contact.GetActor() ||
     !Contact.GetActor()->ActorHasTag(TEXT("WarehouseMapStructure")) ||
     !Surface->IsQueryCollisionEnabled() || Surface->GetCollisionResponseToChannel(ECC_Visibility)!=ECR_Block ||
     Contact.PenetrationDepth>.5f || !Platform || !Platform->GetStaticMesh()) return false;
 const FVector Local=GetActorTransform().InverseTransformPosition(Contact.ImpactPoint);
 const FBox DeckBounds=Platform->GetStaticMesh()->GetBoundingBox().TransformBy(Platform->GetComponentTransform().GetRelativeTransform(GetActorTransform()));
 const FBox SlabBounds=Surface->Bounds.GetBox().TransformBy(GetActorTransform().Inverse());
 const float DeckEdge=DeckBounds.Min.X,SlabEdge=SlabBounds.Max.X,Gap=DeckEdge-SlabEdge;
 if (Gap<=0 || Gap>6.1f || Local.X<SlabEdge-.5f || Local.X>DeckEdge+.5f || FMath::Abs(Local.Y)>115 ||
     FMath::Abs(Contact.ImpactPoint.Z-FloorHeight(CurrentFloor))>.5f) return false;
 const FVector Centre=Wheel->GetComponentLocation();
 const float Radius=Wheel->GetStaticMesh()->GetBoundingBox().GetExtent().Z*Wheel->GetComponentScale().GetAbs().Z;
 const float CentreX=GetActorTransform().InverseTransformPosition(Centre).X;
 if (Gap>Radius || CentreX<SlabEdge-.2f || CentreX>DeckEdge+.2f || FVector::Dist2D(Centre,Contact.ImpactPoint)>Radius) return false;
 // At the middle of the measured gap a round tyre rests on both top edges,
 // not on a plane beneath its centre. For an 8cm radius/6cm gap the physical
 // sag is 0.585cm and the edge normal Z is 0.927. Allow 2mm for the convex
 // tread/contact skin, while retaining actual coplanar bearings below.
 const float SupportedHeight=FMath::Sqrt(FMath::Max(0.f,Radius*Radius-Gap*Gap*.25f));
 const float ContactTolerance=.2f,MaxSag=Radius-SupportedHeight+ContactTolerance;
 const float BottomOffset=Centre.Z-Radius-FloorHeight(CurrentFloor);
 if (BottomOffset< -MaxSag || BottomOffset>.5f || Contact.Normal.Z<(SupportedHeight-ContactTolerance)/Radius) return false;
 FHitResult Deck,Slab;
 const FVector OnDeck=GetActorTransform().TransformPosition(FVector(DeckEdge+5,Local.Y,CurrentFloor*400));
 if (!TraceCertifiedPlatformSupport(Vehicle,OnDeck,Deck)) return false;
 const FVector OnSlab=GetActorTransform().TransformPosition(FVector(SlabEdge-1.5f,Local.Y,CurrentFloor*400));
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
 // Unused fourth-floor doors do not participate, but every active landing
 // must be closed and settled, not only the current floor's minimum opening.
 if (DoorOpening>.01f || Gates.Num()<FloorCount*2) return false;
 for (int32 I=0;I<FloorCount*2;++I)
 {
  if (!IsValid(Gates[I])) return false;
  const float Y=GetActorTransform().InverseTransformPosition(Gates[I]->GetComponentLocation()).Y;
  if (FMath::Abs(Y-(I%2 ? 60.f : -60.f))>=.5f || Gates[I]->GetPhysicsLinearVelocity().Size()>=2.f) return false;
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
 for (int32 I=0; I<FloorCount*2; ++I)
 {
  const int32 Floor=I/2; const float Side=I%2 ? 1.f : -1.f;
  const float Opening=Floor==CurrentFloor ? DoorOpening : 0.f;
  if (DoorJoints.IsValidIndex(I)) DoorJoints[I]->SetLinearPositionTarget(FVector(0,Side*(62.5f-Opening),0));
  // Updating a Chaos drive target does not wake a settled native body.
  const float ActualY=GetActorTransform().InverseTransformPosition(Gates[I]->GetComponentLocation()).Y;
  if (FMath::Abs(ActualY-Side*(60.f+Opening))>.1f) Gates[I]->WakeAllRigidBodies();
 }
 return Open ? ActualDoorOpening()>=124.f : AreLandingDoorsClosed();
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
