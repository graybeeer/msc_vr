#include "WarehouseBuildingLayout.h"
#include "WarehousePlayMode.h"
#include "WarehousePhysics.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseElevator.h"
#include "WarehousePalletLift.h"
#include "WarehouseElevatorLayout.h"
#include "WarehouseForklift.h"
#include "WarehouseComputer.h"
#include "WarehouseLayoutEditor.h"
#include "WarehouseLayoutInventory.h"
#include "WarehouseCargo.h"
#include "WarehousePallet.h"
#include "msc_vrCharacter.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/PlayerController.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/SpotLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"

namespace
{
 enum class EPart { Floor,Wall,Roof,Column,Beam,Rail,Pipe,Tray,Light,Stair,Exterior };
 struct FPart
 {
  EPart Role;
  FVector Centre,Size;
  FQuat Rotation=FQuat::Identity;
  int32 Floor=0;
  AStaticMeshActor* Source=nullptr;
  FBox Bounds() const { return FBox(-Size*.5,Size*.5).TransformBy(FTransform(Rotation,Centre)); }
 };
 void ReportCollision(const TCHAR* Stage,const FPart& Part,const FBox& PartWorld,const AActor* Occupant,const FBox& OccupantWorld)
 {
  UE_LOG(LogTemp,Warning,TEXT("BUILDING_GEOMETRY_BLOCK stage=%s actor=%s role=%d floor=%d part=%s occupant=%s source=%s"),Stage,
   *Occupant->GetActorNameOrLabel(),int32(Part.Role),Part.Floor+1,*PartWorld.ToString(),*OccupantWorld.ToString(),
   IsValid(Part.Source) ? *Part.Source->GetActorNameOrLabel() : TEXT("template"));
 }
 const FWarehouseMezzanineSettings& Upper(const FWarehouseBuildingSettings& Settings,int32 Floor)
 { return Floor==1 ? Settings.Floor2 : Floor==2 ? Settings.Floor3 : Settings.Floor4; }
 FBox2D Footprint(const FWarehouseBuildingSettings& S) { return FBox2D(FVector2D(-S.WidthCm*.5,-S.DepthCm*.5),FVector2D(S.WidthCm*.5,S.DepthCm*.5)); }
 FBox2D Deck(const FWarehouseMezzanineSettings& S) { return FBox2D(S.OffsetCm-S.SizeCm*.5,S.OffsetCm+S.SizeCm*.5); }
 FBox2D Atrium(const FWarehouseMezzanineSettings& S) { return FBox2D(S.OffsetCm-S.AtriumSizeCm*.5,S.OffsetCm+S.AtriumSizeCm*.5); }
 FBox2D StairOpening(const FWarehouseMezzanineSettings& S)
 {
  const FQuat Rotation(FVector::UpVector,FMath::DegreesToRadians(S.StairYawDegrees));FBox2D Box(ForceInit);
  for (double X:{-336.,336.}) for (double Y:{-75.,75.}) Box+=S.StairOffsetCm+FVector2D(Rotation.RotateVector(FVector(X,Y,0)));
  return Box;
 }
 bool Contains(const FBox2D& A,const FBox2D& B,double Margin=0)
 { return B.Min.X>=A.Min.X+Margin && B.Min.Y>=A.Min.Y+Margin && B.Max.X<=A.Max.X-Margin && B.Max.Y<=A.Max.Y-Margin; }
 bool Overlaps(const FBox2D& A,const FBox2D& B,double Margin=0)
 { return A.Min.X+Margin<B.Max.X && B.Min.X+Margin<A.Max.X && A.Min.Y+Margin<B.Max.Y && B.Min.Y+Margin<A.Max.Y; }
 bool Overlaps(const FBox& A,const FBox& B,double Margin=.5)
 { return A.Min.X+Margin<B.Max.X && B.Min.X+Margin<A.Max.X && A.Min.Y+Margin<B.Max.Y && B.Min.Y+Margin<A.Max.Y && A.Min.Z+Margin<B.Max.Z && B.Min.Z+Margin<A.Max.Z; }
 TArray<FBox2D> Subtract(const FBox2D& Rect,const FBox2D& Hole)
 {
  if (!Overlaps(Rect,Hole)) return {Rect};
  const FVector2D Low(FMath::Max(Rect.Min.X,Hole.Min.X),FMath::Max(Rect.Min.Y,Hole.Min.Y));
  const FVector2D High(FMath::Min(Rect.Max.X,Hole.Max.X),FMath::Min(Rect.Max.Y,Hole.Max.Y));
  TArray<FBox2D> Result;
  auto Add=[&](double X0,double Y0,double X1,double Y1) { if (X1-X0>.1 && Y1-Y0>.1) Result.Emplace(FVector2D(X0,Y0),FVector2D(X1,Y1)); };
  Add(Rect.Min.X,Rect.Min.Y,Low.X,Rect.Max.Y);Add(High.X,Rect.Min.Y,Rect.Max.X,Rect.Max.Y);
  Add(Low.X,Rect.Min.Y,High.X,Low.Y);Add(Low.X,High.Y,High.X,Rect.Max.Y);return Result;
 }
 struct FShaft { AActor* Actor=nullptr;TArray<FBox2D> Opening;FBox2D Clearance,Landing; };
 TArray<FShaft> Shafts(const AWarehouseBuildingLayout* Building,const TArray<AActor*>& Removed={})
 {
  TArray<FShaft> Result;
  auto Add=[&](const AActor* Actor,FVector2D Ground,FVector2D Size,FVector2D Offset,double Yaw,FBox2D Landing)
  {
   if (!Actor->GetActorEnableCollision() || Removed.Contains(Actor)) return;
   const FQuat Rotation(FVector::UpVector,FMath::DegreesToRadians(Yaw));FShaft Shaft;Shaft.Actor=const_cast<AActor*>(Actor);
   for (int32 Floor=0;Floor<4;++Floor)
   {
    const FVector2D Half=(Floor ? Size : Ground)*.5,Centre=Floor ? Offset : FVector2D::ZeroVector;FBox2D Bounds(ForceInit);
    for (double X:{-Half.X,Half.X}) for (double Y:{-Half.Y,Half.Y})
     Bounds+=FVector2D(Actor->GetActorLocation()-Building->GetActorLocation()+Rotation.RotateVector(FVector(X+Centre.X,Y+Centre.Y,0)));
    Shaft.Opening.Add(Bounds);
   }
   Shaft.Landing=FBox2D(ForceInit);
   for (double X:{Landing.Min.X,Landing.Max.X}) for (double Y:{Landing.Min.Y,Landing.Max.Y})
    Shaft.Landing+=FVector2D(Actor->GetActorLocation()-Building->GetActorLocation()+Rotation.RotateVector(FVector(X,Y,0)));
   const FBox Physical=AWarehouseDamageSystem::GetPhysicalBounds(const_cast<AActor*>(Actor));
   if (Physical.IsValid) Shaft.Clearance=FBox2D(FVector2D(Physical.Min-Building->GetActorLocation()),FVector2D(Physical.Max-Building->GetActorLocation())).ExpandBy(10);
   else Shaft.Clearance=Shaft.Opening[0].ExpandBy(15);
   Result.Add(MoveTemp(Shaft));
  };
  for (TActorIterator<AWarehouseElevator> It(Building->GetWorld());It;++It) Add(*It,It->GroundOpeningSizeCm,It->UpperOpeningSizeCm,It->UpperOpeningOffsetCm,It->GetActorRotation().Yaw-90.,FBox2D(FVector2D(-175,-625),FVector2D(175,-190)));
  for (TActorIterator<AWarehousePalletLift> It(Building->GetWorld());It;++It) Add(*It,It->OpeningSizeCm,It->OpeningSizeCm,FVector2D::ZeroVector,It->GetActorRotation().Yaw,FBox2D(It->LandingLocalMin,It->LandingLocalMax));
  return Result;
 }
 bool EntranceExterior(const AActor* Actor)
 { const FString N=Actor->GetActorNameOrLabel();return N.StartsWith(TEXT("EXT_Canopy")) || N==TEXT("EXT_DockPlatform"); }
 bool Architecture(const AActor* Actor)
 {
  if (Actor->ActorHasTag(TEXT("LayoutType:Rack"))) return false;
  if (EntranceExterior(Actor)) return true;
  const FString N=Actor->GetActorNameOrLabel();
  if (N==TEXT("Floor") || N==TEXT("WH_InboundFloor") || N.StartsWith(TEXT("MZ_GroundRear")) || N.StartsWith(TEXT("WH_Wall_")) ||
      N==TEXT("WH_LoadingDoor_Header") || N==TEXT("WH_Roof") || N.StartsWith(TEXT("WH_Column_")) || N.StartsWith(TEXT("WH_InboundColumn_")) || N.StartsWith(TEXT("WH_RoofBeam_")) || N.StartsWith(TEXT("WH_InboundRoofBeam_")) ||
      N.StartsWith(TEXT("WH_Light_")) || N.StartsWith(TEXT("WH_LightFixture_")) || N.StartsWith(TEXT("WH_WorkLight_")) || N.StartsWith(TEXT("WH_SideFixture_")) || N.StartsWith(TEXT("WS_"))) return true;
  if (N.StartsWith(TEXT("MZ_L")))
   for (const TCHAR* Role:{TEXT("_Deck_"),TEXT("_Column_"),TEXT("_Beam_"),TEXT("_CrossBeam_"),TEXT("_Inner"),TEXT("_Outer"),TEXT("_Stair"),TEXT("_Step_"),TEXT("_Lamp_"),TEXT("_Light_"),TEXT("_FacadeBand_"),TEXT("_LevelSign")})
    if (N.Contains(Role)) return true;
  for (const TCHAR* Prefix:{TEXT("EXT_FrontCladding_"),TEXT("EXT_SideCladding_"),TEXT("EXT_SideParapet_"),TEXT("EXT_FrontParapet"),TEXT("EXT_RoofVent"),
       TEXT("EXT_FrontBlueBand_"),TEXT("EXT_FrontWindow_"),TEXT("EXT_FrontJoint_"),TEXT("EXT_FrontMullion_"),TEXT("EXT_SideBlueBand_"),TEXT("EXT_SideWindow_"),TEXT("EXT_SideMullion_"),TEXT("EXT_Foundation")}) if (N.StartsWith(Prefix)) return true;
  if (Actor->IsA<AStaticMeshActor>() && Actor->ActorHasTag(TEXT("WarehouseMapStructure")) && !Actor->ActorHasTag(TEXT("LayoutType:Rack")) &&
      !Actor->ActorHasTag(TEXT("ElevatorLayoutPit")) && !Actor->ActorHasTag(TEXT("LayoutType:Elevator"))) return true;
  return N==TEXT("EXT_HeaderCladding") || Actor->ActorHasTag(TEXT("ElevatorRuntimeFloor")) || Actor->ActorHasTag(TEXT("ElevatorLayoutFloor"));
 }
 AStaticMeshActor* Template(const AWarehouseBuildingLayout* B,EPart Role,AStaticMeshActor* Source=nullptr)
 {
  if (IsValid(Source)) return Source;
  switch (Role)
  {
  case EPart::Floor:case EPart::Stair:return B->FloorTemplate;
  case EPart::Wall:return B->WallTemplate;
  case EPart::Roof:return B->RoofTemplate ? B->RoofTemplate.Get() : B->FloorTemplate.Get();
  case EPart::Column:return B->ColumnTemplate;
  case EPart::Beam:return B->BeamTemplate;
  case EPart::Rail:return B->RailTemplate;
  case EPart::Pipe:return B->PipeTemplate;
  case EPart::Tray:return B->TrayTemplate;
  default:return B->LightTemplate;
  }
 }
 bool Preserved(const AWarehouseBuildingLayout* Building,const AActor* Actor);
 bool UnchangedFloorSupport(const AWarehouseBuildingLayout* Building,const FPart& Part,const FBox& Surface,const AActor* Occupant,const FBox& Body)
 {
  // Match the existing layout editor's 2cm native resting-contact tolerance.
  // It only applies to an already-supported horizontal floor at the same level.
  if (Part.Role!=EPart::Floor || !Part.Rotation.IsIdentity(.00001) || Body.Min.Z<Surface.Max.Z-2 || Body.Max.Z<=Surface.Max.Z) return false;
  FCollisionQueryParams Query(SCENE_QUERY_STAT(BuildingExistingFloorSupport));Query.AddIgnoredActor(Occupant);
  const FVector Centre=Body.GetCenter(),Point(Centre.X,Centre.Y,Surface.Max.Z);
  FHitResult Hit;
  if (!Building->GetWorld()->LineTraceSingleByChannel(Hit,Point+FVector(0,0,3),Point-FVector(0,0,3),ECC_Visibility,Query) ||
      Hit.ImpactNormal.Z<.95 || FMath::Abs(Hit.ImpactPoint.Z-Surface.Max.Z)>.1 || !WarehousePhysics::IsPhysicalContact(Hit.GetComponent())) return false;
  const AActor* Support=Hit.GetActor();if (!Support) return false;const FString Name=Support->GetActorNameOrLabel();
  return Support->ActorHasTag(TEXT("ElevatorLayoutFloor")) || Support->ActorHasTag(TEXT("ElevatorRuntimeFloor")) ||
   Name==TEXT("Floor") || Name==TEXT("WH_InboundFloor") || Name.StartsWith(TEXT("MZ_GroundRear")) ||
   (Name.StartsWith(TEXT("MZ_L")) && Name.Contains(TEXT("_Deck_")));
 }
 FBox SourceWall(const AWarehouseBuildingLayout* Building,const TCHAR* Label,int32 Axis,bool Positive)
 {
  const FBox2D Shell=Building->GetSourceFootprint();const double Edge=Positive ? Shell.Max[Axis] : Shell.Min[Axis];
  for (AActor* Candidate:Building->ReplacedStructure) if (auto* Source=Cast<AStaticMeshActor>(Candidate);IsValid(Source) && Source->GetActorNameOrLabel()==Label)
  {
   const FBox World=Source->GetStaticMeshComponent()->Bounds.GetBox();const FBox Local(World.Min-Building->GetActorLocation(),World.Max-Building->GetActorLocation());
   // Detached template meshes are not authored wall placements.
   if (Local.IsValid && FMath::Abs(Local.GetCenter()[Axis]-Edge)<100) return Local;
  }
  return FBox(ForceInit);
 }
 TArray<FPart> Plan(const AWarehouseBuildingLayout* Building,const FWarehouseBuildingSettings& S,const TArray<AActor*>& Removed={})
 {
  TArray<FPart> Parts;const FBox2D Shell=Footprint(S);const auto Lifts=Shafts(Building,Removed);
  auto Add=[&](EPart Role,FVector Centre,FVector Size,int32 Floor=0,FQuat Rotation=FQuat::Identity,AStaticMeshActor* Source=nullptr)
  { if (Size.GetMin()>.05) Parts.Add({Role,Centre,Size,Rotation,Floor,Source}); };
  auto FreeOfShaft=[&](FVector2D Centre,FVector2D Size)
  { const FBox2D Box(Centre-Size*.5,Centre+Size*.5);for (const auto& Lift:Lifts) if (Overlaps(Box,Lift.Clearance)) return false;return true; };
  auto Slab=[&](FBox2D Rect,int32 Floor)
  {
   TArray<FBox2D> Pieces={Rect};
   if (Floor>0) for (const FBox2D Hole:{Atrium(Upper(S,Floor)),StairOpening(Upper(S,Floor))})
   { TArray<FBox2D> Next;for (const auto& P:Pieces) Next.Append(Subtract(P,Hole));Pieces=MoveTemp(Next); }
   // Keep the uncut support domain as the source. The shared shaft transaction
   // cuts live replacements, so moving/deleting a lift can restore its old hole.
   for (const auto& P:Pieces) Add(EPart::Floor,FVector(P.GetCenter(),Floor*400.-10),FVector(P.GetSize(),20),Floor);
  };
  Slab(Shell,0);
  // Keep authored wall faces and loading-opening dimensions. Resizing moves
  // each perimeter by its shell-edge delta instead of shifting old mountings.
  const FBox2D OriginalShell=Building->GetSourceFootprint();
  const FBox Header=SourceWall(Building,TEXT("WH_LoadingDoor_Header"),1,false);
  const double DoorHalf=Header.IsValid ? Header.GetSize().X*.5 : 700,DoorHeight=Header.IsValid ? Header.Min.Z : 490;
  const double SourceDoor=Header.IsValid ? Header.GetCenter().X : -Building->GetActorLocation().X;
  const double DoorCentre=FMath::Clamp(SourceDoor,Shell.Min.X+DoorHalf+50,Shell.Max.X-DoorHalf-50);
  auto Wall=[&](FBox2D Rect,double Low=0,double High=-1)
  { if (High<0) High=S.RoofHeightCm;Add(EPart::Wall,FVector(Rect.GetCenter(),(Low+High)*.5),FVector(Rect.GetSize(),High-Low)); };
  const FBox North=SourceWall(Building,TEXT("WH_Wall_N"),1,true);
  Wall(North.IsValid ? FBox2D(FVector2D(Shell.Min.X+North.Min.X-OriginalShell.Min.X,Shell.Max.Y+North.Min.Y-OriginalShell.Max.Y),
   FVector2D(Shell.Max.X+North.Max.X-OriginalShell.Max.X,Shell.Max.Y+North.Max.Y-OriginalShell.Max.Y)) :
   FBox2D(FVector2D(Shell.Min.X,Shell.Max.Y),FVector2D(Shell.Max.X,Shell.Max.Y+35)));
  for (bool Positive:{false,true})
  {
   const FBox Side=SourceWall(Building,Positive ? TEXT("WH_Wall_E") : TEXT("WH_Wall_W"),0,Positive);
   const double Edge=Positive ? Shell.Max.X : Shell.Min.X,OldEdge=Positive ? OriginalShell.Max.X : OriginalShell.Min.X;
   Wall(Side.IsValid ? FBox2D(FVector2D(Edge+Side.Min.X-OldEdge,Shell.Min.Y+Side.Min.Y-OriginalShell.Min.Y),
    FVector2D(Edge+Side.Max.X-OldEdge,Shell.Max.Y+Side.Max.Y-OriginalShell.Max.Y)) :
    FBox2D(FVector2D(Edge+(Positive ? 0 : -35),Shell.Min.Y-35),FVector2D(Edge+(Positive ? 35 : 0),Shell.Max.Y+35)));
  }
  for (bool Right:{false,true})
  {
   const FBox Side=SourceWall(Building,Right ? TEXT("WH_Wall_S_Right") : TEXT("WH_Wall_S_Left"),1,false);
   const double LowY=Side.IsValid ? Shell.Min.Y+Side.Min.Y-OriginalShell.Min.Y : Shell.Min.Y-35;
   const double HighY=Side.IsValid ? Shell.Min.Y+Side.Max.Y-OriginalShell.Min.Y : Shell.Min.Y;
   const double LowX=Right ? DoorCentre+DoorHalf : Shell.Min.X+(Side.IsValid ? Side.Min.X-OriginalShell.Min.X : 0);
   const double HighX=Right ? Shell.Max.X+(Side.IsValid ? Side.Max.X-OriginalShell.Max.X : 0) : DoorCentre-DoorHalf;
   if (HighX>LowX) Wall(FBox2D(FVector2D(LowX,LowY),FVector2D(HighX,HighY)));
  }
  if (S.RoofHeightCm>DoorHeight) Wall(FBox2D(FVector2D(DoorCentre-DoorHalf,Shell.Min.Y+(Header.IsValid ? Header.Min.Y-OriginalShell.Min.Y : -35)),
   FVector2D(DoorCentre+DoorHalf,Shell.Min.Y+(Header.IsValid ? Header.Max.Y-OriginalShell.Min.Y : 0))),DoorHeight);
  // Entrance overhangs belong to the facade. Preserve their authored mesh,
  // scale and relative mounting while following the new south wall/doorway.
  const double SourceSouth=IsValid(Building->FloorTemplate) ? Building->FloorTemplate->GetStaticMeshComponent()->Bounds.GetBox().Min.Y : Building->GetActorLocation().Y+Footprint(Building->GetSettings()).Min.Y;
  const double SourceDoorX=Building->GetActorLocation().X+SourceDoor;
  const FVector FacadeDelta(Building->GetActorLocation().X+DoorCentre-SourceDoorX,Building->GetActorLocation().Y+Shell.Min.Y-SourceSouth,0);
  for (AActor* Candidate:Building->ReplacedStructure) if (auto* Source=Cast<AStaticMeshActor>(Candidate);IsValid(Source) && EntranceExterior(Source))
  {
   const FBox Bounds=Source->GetStaticMeshComponent()->Bounds.GetBox();
   Add(EPart::Exterior,Bounds.GetCenter()-Building->GetActorLocation()+FacadeDelta,Bounds.GetSize(),0,FQuat::Identity,Source);
  }
  Add(EPart::Roof,FVector(0,0,S.RoofHeightCm+15),FVector(S.WidthCm+70,S.DepthCm+70,30));
  // Foundation follows the shell; saved exterior roads and docks remain untouched.
  for (double X:{Shell.Min.X+25,Shell.Max.X-25}) Add(EPart::Wall,FVector(X,0,-65),FVector(50,S.DepthCm,130));
  Add(EPart::Wall,FVector(0,Shell.Max.Y-25,-65),FVector(S.WidthCm,50,130));
  // Existing authored mesh/material modules are reused; no replacement asset is baked.
  if (Building->ColumnTemplate) for (double X:{Shell.Min.X+40,Shell.Max.X-40})
   for (int32 I=0,N=FMath::CeilToInt((S.DepthCm-80)/800.);I<=N;++I)
   {
    const double Y=FMath::Lerp(Shell.Min.Y+40,Shell.Max.Y-40,double(I)/N);
    if (FreeOfShaft(FVector2D(X,Y),FVector2D(40,40))) Add(EPart::Column,FVector(X,Y,S.RoofHeightCm*.5),FVector(40,40,S.RoofHeightCm));
   }
  if (Building->BeamTemplate) for (int32 I=0,N=FMath::CeilToInt(S.DepthCm/800.);I<=N;++I)
   Add(EPart::Beam,FVector(0,FMath::Lerp(Shell.Min.Y+40,Shell.Max.Y-40,double(I)/N),S.RoofHeightCm-35),FVector(S.WidthCm-80,20,30));
  auto Rail=[&](FVector2D A,FVector2D B,int32 Floor)
  {
   if (!Building->RailTemplate) return;
   const FVector2D Delta=B-A;const double Length=Delta.Size();if (Length<1) return;
   const FQuat Rotation(FVector::UpVector,FMath::Atan2(Delta.Y,Delta.X));
   const int32 Count=FMath::Max(1,FMath::CeilToInt(Length/160.));
   for (int32 I=0;I<Count;++I)
   {
    const FVector2D Centre=A+Delta*((I+.5)/Count);const double Span=Length/Count;
    const FVector2D Extent(FMath::Abs(Delta.X/Length)*Span+5,FMath::Abs(Delta.Y/Length)*Span+5);
    const auto& M=Upper(S,Floor);const FQuat StairRotation(FVector::UpVector,FMath::DegreesToRadians(M.StairYawDegrees));
    const FVector2D Exit=M.StairOffsetCm+FVector2D(StairRotation.RotateVector(FVector(406,-60,0)));
    if (!FreeOfShaft(Centre,Extent) || (Centre-Exit).Size()<200) continue;
    for (double Height:{10.,57.,110.}) Add(EPart::Rail,FVector(Centre,Floor*400.+Height),FVector(Span,5,Height==10 ? 14 : 5),Floor,Rotation);
    Add(EPart::Rail,FVector(A+Delta*(double(I)/Count),Floor*400.+55),FVector(5,5,110),Floor);
   }
   if (FreeOfShaft(B,FVector2D(5,5))) Add(EPart::Rail,FVector(B,Floor*400.+55),FVector(5,5,110),Floor);
  };
  for (int32 Floor=1;Floor<4;++Floor) if (Upper(S,Floor).bEnabled)
  {
   const auto& M=Upper(S,Floor);const FBox2D Outer=Deck(M),Hole=Atrium(M);Slab(Outer,Floor);
   if (Building->ColumnTemplate) for (double X:{Outer.Min.X+25,Outer.Max.X-25,Hole.Min.X-12,Hole.Max.X+12})
    for (int32 I=0,N=FMath::CeilToInt((M.SizeCm.Y-50)/900.);I<=N;++I)
    {
     const double Y=FMath::Lerp(Outer.Min.Y+25,Outer.Max.Y-25,double(I)/N);
     if (FreeOfShaft(FVector2D(X,Y),FVector2D(22,22))) Add(EPart::Column,FVector(X,Y,(Floor-.5)*400),FVector(22,22,400),Floor-1);
    }
   if (Building->BeamTemplate) for (double X:{Outer.Min.X+25,Outer.Max.X-25,Hole.Min.X-12,Hole.Max.X+12})
   {
    if (FreeOfShaft(FVector2D(X,M.OffsetCm.Y),FVector2D(26,M.SizeCm.Y-50)))
     Add(EPart::Beam,FVector(X,M.OffsetCm.Y,Floor*400.-38),FVector(26,M.SizeCm.Y-50,30),Floor);
   }
   for (const FBox2D Rect:{Outer,Hole})
   {
    Rail(Rect.Min,FVector2D(Rect.Max.X,Rect.Min.Y),Floor);Rail(FVector2D(Rect.Max.X,Rect.Min.Y),Rect.Max,Floor);
    Rail(Rect.Max,FVector2D(Rect.Min.X,Rect.Max.Y),Floor);Rail(FVector2D(Rect.Min.X,Rect.Max.Y),Rect.Min,Floor);
   }
   const FQuat R(FVector::UpVector,FMath::DegreesToRadians(M.StairYawDegrees));const FVector Origin(M.StairOffsetCm,(Floor-1)*400.);
   for (int32 I=0;I<24;++I) Add(EPart::Stair,Origin+R.RotateVector(FVector(-336+(I+.5)*28,0,(I+.5)*400./24)),FVector(28,130,400./24),Floor-1,R);
   for (int32 End:{-1,1}) Add(EPart::Stair,Origin+R.RotateVector(FVector(End*406,-60,End<0 ? -10 : 390)),FVector(140,250,20),End<0 ? Floor-1 : Floor,R);
   // Physical sloping handrails, mounted on both edges of the stair flight.
   if (Building->RailTemplate) for (double Side:{-65.,65.})
   {
    const FVector A=Origin+R.RotateVector(FVector(-336,Side,110)),B=Origin+R.RotateVector(FVector(336,Side,510));
    Add(EPart::Rail,(A+B)*.5,FVector((B-A).Size(),5,5),Floor,FRotationMatrix::MakeFromX(B-A).ToQuat());
    for (int32 I=0;I<=6;++I) Add(EPart::Rail,Origin+R.RotateVector(FVector(-336+I*112,Side,I*400./6+55)),FVector(5,5,110),Floor);
   }
  }
  // Density follows the final roof/deck dimensions, using real service modules.
  for (int32 Ceiling=1;Ceiling<=4;++Ceiling)
  {
   const bool Roof=Ceiling==4;if (!Roof && !Upper(S,Ceiling).bEnabled) continue;
   const FBox2D Area=Roof ? Shell : Deck(Upper(S,Ceiling));const double Z=Roof ? S.RoofHeightCm : Ceiling*400.;
   const int32 NX=FMath::Max(1,FMath::CeilToInt(Area.GetSize().X/800.)),NY=FMath::Max(1,FMath::CeilToInt(Area.GetSize().Y/600.));
   for (int32 IX=0;IX<NX;++IX) for (int32 IY=0;IY<NY;++IY)
   {
    const FVector2D P(Area.Min.X+(IX+.5)*Area.GetSize().X/NX,Area.Min.Y+(IY+.5)*Area.GetSize().Y/NY);
    if (!Roof && Atrium(Upper(S,Ceiling)).IsInside(P)) continue;
    if (!FreeOfShaft(P,FVector2D(140,80))) continue;
    if (Building->LightTemplate) Add(EPart::Light,FVector(P,Z-70),FVector(120,16,12),Roof ? 3 : Ceiling-1);
    if (Building->PipeTemplate) Add(EPart::Pipe,FVector(P.X,P.Y-70,Z-100),FVector(Area.GetSize().X/NX,6,6),Roof ? 3 : Ceiling-1);
    if (Building->TrayTemplate) Add(EPart::Tray,FVector(P.X,P.Y+70,Z-85),FVector(Area.GetSize().X/NX,35,12),Roof ? 3 : Ceiling-1);
   }
  }
  TArray<FBox> Occupied;
  for (TActorIterator<AActor> It(Building->GetWorld());It;++It) if (Preserved(Building,*It) && !Removed.Contains(*It))
  {
   const FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(*It);
   if (Bounds.IsValid) Occupied.Emplace(Bounds.Min-Building->GetActorLocation(),Bounds.Max-Building->GetActorLocation());
  }
  Parts.RemoveAll([&](const FPart& P)
  {
   if (P.Role!=EPart::Light && P.Role!=EPart::Pipe && P.Role!=EPart::Tray) return false;
   const FBox Bounds=P.Bounds();const FBox2D XY(FVector2D(Bounds.Min),FVector2D(Bounds.Max));
   for (const auto& Lift:Lifts) if (Overlaps(XY,Lift.Clearance)) return true;
   if (P.Centre.Z<S.RoofHeightCm-120)
   {
    const int32 Ceiling=FMath::Clamp(FMath::RoundToInt((P.Centre.Z+85)/400.),1,3);const auto& M=Upper(S,Ceiling);
    if (!Contains(Deck(M),XY) || (M.AtriumSizeCm.GetMin()>0 && Overlaps(Atrium(M),XY))) return true;
   }
   for (const FBox& Body:Occupied) if (Overlaps(Bounds,Body,1)) return true;
   return false;
  });
  return Parts;
 }
 bool Preserved(const AWarehouseBuildingLayout* Building,const AActor* Actor)
 {
  return Actor!=Building && !Building->ReplacedStructure.Contains(Actor) && !Actor->ActorHasTag(TEXT("WarehouseBuildingGenerated")) &&
   !Actor->ActorHasTag(TEXT("ElevatorRuntimeFloor")) && Actor->GetActorEnableCollision();
 }
 void DestroyParts(TArray<TObjectPtr<AStaticMeshActor>>& Actors)
 { for (AStaticMeshActor* Actor:Actors) if (IsValid(Actor)) Actor->Destroy();Actors.Reset(); }
}

AWarehouseBuildingLayout::AWarehouseBuildingLayout()
{
 PrimaryActorTick.bCanEverTick=false;SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("BuildingOrigin")));
 Tags={TEXT("LayoutFixed"),TEXT("WarehouseBuildingController")};
}
AWarehouseBuildingLayout* AWarehouseBuildingLayout::Find(const AActor* Context)
{
 if (!IsValid(Context) || !Context->GetWorld()) return nullptr;
 for (TActorIterator<AWarehouseBuildingLayout> It(Context->GetWorld());It;++It) return *It;
 FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Building=Context->GetWorld()->SpawnActor<AWarehouseBuildingLayout>(FVector(400,0,0),FRotator::ZeroRotator,Params);
 if (Building) Building->CaptureExistingLayout();return Building;
}
void AWarehouseBuildingLayout::CaptureExistingLayout()
{
 if (bCaptured || !GetWorld()) return;bCaptured=true;
 FBox2D Ground(ForceInit),UpperBounds[3]={FBox2D(ForceInit),FBox2D(ForceInit),FBox2D(ForceInit)};
 for (TActorIterator<AActor> It(GetWorld());It;++It)
 {
  if (!Architecture(*It) || It->ActorHasTag(TEXT("WarehouseBuildingGenerated"))) continue;
  ReplacedStructure.AddUnique(*It);auto* Mesh=Cast<AStaticMeshActor>(*It);if (!Mesh) continue;
  const FString N=It->GetActorNameOrLabel();
  if (!FloorTemplate && (N==TEXT("Floor") || N==TEXT("WH_InboundFloor"))) FloorTemplate=Mesh;
  if (!WallTemplate && N==TEXT("WH_Wall_N")) WallTemplate=Mesh;
  if (!RoofTemplate && N==TEXT("WH_Roof")) RoofTemplate=Mesh;
  if (!ColumnTemplate && N.StartsWith(TEXT("WH_Column_"))) ColumnTemplate=Mesh;
  if (!BeamTemplate && N.StartsWith(TEXT("WH_RoofBeam_"))) BeamTemplate=Mesh;
  if (!RailTemplate && N.Contains(TEXT("_InnerLeft_Bar55"))) RailTemplate=Mesh;
  if (!PipeTemplate && N.StartsWith(TEXT("WS_FirePipeStraight_2m_"))) PipeTemplate=Mesh;
  if (!TrayTemplate && N.StartsWith(TEXT("WS_TrayStraight_2m_"))) TrayTemplate=Mesh;
  if (!LightTemplate && N.StartsWith(TEXT("WS_LEDLinear_1200_"))) LightTemplate=Mesh;
  const FBox Bounds=Mesh->GetStaticMeshComponent()->Bounds.GetBox();
  if (It->ActorHasTag(TEXT("ElevatorLayoutFloor")) || N==TEXT("Floor") || N==TEXT("WH_InboundFloor") || N.Contains(TEXT("_Deck_")))
  {
   const int32 Floor=FMath::RoundToInt(Bounds.Max.Z/400.);
   if (Floor==0) { Ground+=FVector2D(Bounds.Min);Ground+=FVector2D(Bounds.Max); }
   else if (Floor>=1 && Floor<=3) { UpperBounds[Floor-1]+=FVector2D(Bounds.Min);UpperBounds[Floor-1]+=FVector2D(Bounds.Max); }
  }
 }
 if (RoofTemplate)
 {
  const FBox Bounds=RoofTemplate->GetStaticMeshComponent()->Bounds.GetBox();Settings.RoofHeightCm=Bounds.Min.Z;
  if (Ground.bIsValid) { SetActorLocation(FVector(Ground.GetCenter(),0));Settings.WidthCm=Ground.GetSize().X;Settings.DepthCm=Ground.GetSize().Y; }
  for (int32 Floor=1;Floor<=3;++Floor)
  {
   auto& M=Floor==1 ? Settings.Floor2 : Floor==2 ? Settings.Floor3 : Settings.Floor4;
   M.bEnabled=UpperBounds[Floor-1].bIsValid;
   if (M.bEnabled) { M.SizeCm=UpperBounds[Floor-1].GetSize();M.OffsetCm=UpperBounds[Floor-1].GetCenter()-FVector2D(GetActorLocation()); }
  }
 }
 CapturedFootprint=Footprint(Settings);
 // Retain only native, already-existing fixed contacts before source bodies
 // are hidden. Never grant this baseline relationship to moving cargo/people.
 for (AActor* Actor:ReplacedStructure) if (auto* Source=Cast<AStaticMeshActor>(Actor);IsValid(Source) && EntranceExterior(Source))
 {
  auto* Part=Source->GetStaticMeshComponent();if (!WarehousePhysics::IsPhysicalContact(Part)) continue;
  FComponentQueryParams Query(SCENE_QUERY_STAT(BuildingBaselineContact),Source);
  TArray<FOverlapResult> Hits;
  GetWorld()->ComponentOverlapMultiByChannel(Hits,Part,Part->GetComponentLocation(),Part->GetComponentQuat(),ECC_Visibility,Query);
  for (const auto& Hit:Hits) if (Hit.bBlockingHit && Hit.GetActor() && Hit.GetActor()->IsA<AStaticMeshActor>() && Preserved(this,Hit.GetActor()))
   if (auto* Other=Cast<UStaticMeshComponent>(Hit.GetComponent());Other && Other->Mobility==EComponentMobility::Static && WarehousePhysics::IsPhysicalContact(Other) && !Other->IsSimulatingPhysics())
    ExteriorContacts.Add({Source,Other,Part->GetStaticMesh().Get(),Other->GetStaticMesh().Get(),Part->GetComponentTransform(),Other->GetComponentTransform(),AWarehouseDamageSystem::GetPhysicalBounds(Hit.GetActor())});
 }
}
FBox AWarehouseBuildingLayout::GetFootprint() const
{ return FBox(FVector(-Settings.WidthCm*.5,-Settings.DepthCm*.5,0)+GetActorLocation(),FVector(Settings.WidthCm*.5,Settings.DepthCm*.5,Settings.RoofHeightCm)+GetActorLocation()); }

bool AWarehouseBuildingLayout::ValidateSettings(const FWarehouseBuildingSettings& Proposed,FString& Reason) const
{
 TArray<AActor*> Removed;TArray<AWarehouseForklift*> Relocate;return ValidateSettingsInternal(Proposed,Reason,Removed,Relocate);
}
bool AWarehouseBuildingLayout::ValidateSettingsInternal(const FWarehouseBuildingSettings& Proposed,FString& Reason,TArray<AActor*>& Removed,TArray<AWarehouseForklift*>& Relocate) const
{
 Reason.Empty();
 auto Fail=[&](const FString& Message) { Reason=Message;return false; };
 auto Remove=[&](AActor* Actor,const FString& Message)
 {
  if (auto* Vehicle=Cast<AWarehouseForklift>(Actor);Vehicle && GetWorld()->IsGameWorld()) { Relocate.AddUnique(Vehicle);return true; }
  if (const auto* Lift=Cast<AWarehouseElevator>(Actor);Lift && Lift->bHumanService)
   return Fail(TEXT("사람이 이용 중인 엘리베이터는 삭제할 수 없습니다. 하차 후 건물을 편집하세요."));
  // The player is never silently deleted.
  const auto* Pawn=Cast<APawn>(Actor);
  if (Actor->IsA<AWarehouseForklift>() || (Pawn && Pawn->IsPlayerControlled()) || !GetWorld()->IsGameWorld())
   return Fail(Message+TEXT(" · 플레이어는 삭제하지 않습니다. 남는 바닥으로 먼저 이동하세요."));
  Removed.AddUnique(Actor);return true;
 };
 if (!GetWorld() || !GetActorRotation().IsNearlyZero(.01) || !GetActorScale3D().Equals(FVector::OneVector,.001) || FMath::Abs(GetActorLocation().Z)>.01)
  return Fail(TEXT("건물 기준점은 지상 높이·회전 0도·배율 1이어야 합니다."));
 if (!FMath::IsFinite(Proposed.WidthCm) || !FMath::IsFinite(Proposed.DepthCm) || !FMath::IsFinite(Proposed.RoofHeightCm) ||
     Proposed.WidthCm<1600 || Proposed.WidthCm>9600 || Proposed.DepthCm<1600 || Proposed.DepthCm>8000 || Proposed.RoofHeightCm<300 || Proposed.RoofHeightCm>2000)
  return Fail(TEXT("건물은 가로 16~96m·세로 16~80m·높이 3~20m 범위여야 합니다."));
 const FBox2D Shell=Footprint(Proposed);int32 Highest=0;
 const FBox Header=SourceWall(this,TEXT("WH_LoadingDoor_Header"),1,false);
 if (Header.IsValid && (Proposed.WidthCm<Header.GetSize().X+100 || Proposed.RoofHeightCm<Header.Min.Z))
  return Fail(TEXT("기존 상하차 출입구의 폭·높이와 양쪽 벽 공간을 유지해야 합니다."));
 for (int32 Floor=1;Floor<4;++Floor)
 {
  const auto& M=Upper(Proposed,Floor);if (!M.bEnabled) continue;Highest=Floor;
  if (M.OffsetCm.ContainsNaN() || M.SizeCm.ContainsNaN() || M.AtriumSizeCm.ContainsNaN() || M.StairOffsetCm.ContainsNaN() || !FMath::IsFinite(M.StairYawDegrees) ||
      M.SizeCm.X<800 || M.SizeCm.Y<800 || M.AtriumSizeCm.X<0 || M.AtriumSizeCm.Y<0 || M.SizeCm.X-M.AtriumSizeCm.X<400 || M.SizeCm.Y-M.AtriumSizeCm.Y<400 || !Contains(Shell,Deck(M),20))
   return Fail(FString::Printf(TEXT("%d층 복층은 건물 안에 있어야 하며, 중정 둘레에 최소 2m 폭의 바닥이 필요합니다."),Floor+1));
  if (Floor>1 && !Upper(Proposed,Floor-1).bEnabled) return Fail(TEXT("복층은 아래층부터 순서대로 활성화해야 합니다."));
 }
 if (Proposed.RoofHeightCm<Highest*400+300) return Fail(TEXT("최상층 바닥 위로 최소 3m의 층고를 확보해야 합니다."));
 const int32 Floors=Highest+1;
 bool HaveLift=false;
 for (const auto& Shaft:Shafts(this,Removed))
 {
  const FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(Shaft.Actor);
  const FBox2D XY(FVector2D(Bounds.Min-GetActorLocation()),FVector2D(Bounds.Max-GetActorLocation()));
  bool Incompatible=!Contains(Shell,XY,1);
  if (const auto* Lift=Cast<AWarehouseElevator>(Shaft.Actor))
   Incompatible|=Lift->FloorCount!=Floors && (Lift->State!=EWarehouseElevatorState::Idle || Lift->CurrentFloor>=Floors || Lift->ReservedVehicle);
  if (const auto* Lift=Cast<AWarehousePalletLift>(Shaft.Actor))
   Incompatible|=Lift->FloorCount!=Floors && (!Lift->IsIdleForLayout() || Lift->CurrentFloor>=Floors);
  if (Incompatible) { if (!Remove(Shaft.Actor,TEXT("승강기 위치/운행 층이 새 건물 범위를 벗어납니다."))) return false; }
  else HaveLift=true;
 }
 if (HaveLift && !GetWorld()->IsGameWorld()) return Fail(TEXT("승강기 설치 건물은 플레이 중 건물 편집 화면에서 변경하세요."));
 if (HaveLift && Proposed.RoofHeightCm<Highest*400+340) return Fail(TEXT("승강기 상부 프레임과 천장 사이에 여유 공간을 확보하세요."));
 // Editing pauses the world; retained vehicles and loaded lifts keep their bodies and loads.
 const auto Parts=Plan(this,Proposed,Removed);const FVector Origin=GetActorLocation();
 for (const FShaft& Shaft:Shafts(this,Removed)) for (int32 Floor=0;Floor<Floors;++Floor)
  for (const FBox2D& Required:{Shaft.Opening[Floor],Shaft.Landing})
  {
   TArray<FBox2D> Unsupported={Required};
   for (const FPart& P:Parts) if (P.Role==EPart::Floor && P.Floor==Floor)
   { const FBox B=P.Bounds();TArray<FBox2D> Next;for (const auto& Rect:Unsupported) Next.Append(Subtract(Rect,FBox2D(FVector2D(B.Min),FVector2D(B.Max))));Unsupported=MoveTemp(Next); }
   if (!Unsupported.IsEmpty())
   {
    if (!Remove(Shaft.Actor,FString::Printf(TEXT("%d층 승강기 진입 바닥이 사라집니다."),Floor+1))) return false;
    return ValidateSettingsInternal(Proposed,Reason,Removed,Relocate);
   }
  }
 for (const auto& Part:Parts) if (Part.Role==EPart::Stair)
 {
  const FBox B=Part.Bounds();if (!Contains(Shell,FBox2D(FVector2D(B.Min),FVector2D(B.Max)),2)) return Fail(TEXT("계단·계단참이 건물 외곽을 벗어납니다. 계단 위치를 옮기세요."));
 }
 for (int32 Floor=1;Floor<Floors;++Floor)
 {
  const auto& M=Upper(Proposed,Floor);const FQuat R(FVector::UpVector,FMath::DegreesToRadians(M.StairYawDegrees));
  for (int32 End:{-1,1})
  {
   const int32 LandingFloor=End<0 ? Floor-1 : Floor;FBox2D Landing(ForceInit);
   for (double X:{-70.,70.}) for (double Y:{-125.,125.})
    Landing+=M.StairOffsetCm+FVector2D(R.RotateVector(FVector(End*406+X,-60+Y,0)));
   bool Connected=false;
   for (const auto& P:Parts) if (P.Role==EPart::Floor && P.Floor==LandingFloor)
   {
    const FBox B=P.Bounds();const double Width=FMath::Min(Landing.Max.X,B.Max.X)-FMath::Max(Landing.Min.X,B.Min.X);
    const double Depth=FMath::Min(Landing.Max.Y,B.Max.Y)-FMath::Max(Landing.Min.Y,B.Min.Y);
    if (Width>=89.9 && Depth>=89.9) { Connected=true;break; }
   }
   if (!Connected) return Fail(FString::Printf(TEXT("%d층 계단참이 실제 바닥과 연결되지 않습니다. 중정·계단 구멍 밖에 통행 폭 90cm를 확보하세요."),LandingFloor+1));
  }
 }
 const FBox2D Previous=Footprint(Settings);
 for (TActorIterator<AActor> It(GetWorld());It;++It) if (Preserved(this,*It) && !Removed.Contains(*It) && !Relocate.Contains(Cast<AWarehouseForklift>(*It)))
 {
  const FBox World=AWarehouseDamageSystem::GetPhysicalBounds(*It);if (!World.IsValid) continue;
  const FBox Bounds(World.Min-Origin,World.Max-Origin);const FBox2D XY(FVector2D(Bounds.Min),FVector2D(Bounds.Max));
  const bool Interior=Contains(Previous,XY);
  if (Overlaps(Previous,XY) && Bounds.Min.Z<Settings.RoofHeightCm && Bounds.Max.Z>-.5)
  {
   const FBox2D Required=Interior ? XY : FBox2D(FVector2D(FMath::Max(Previous.Min.X,XY.Min.X),FMath::Max(Previous.Min.Y,XY.Min.Y)),
    FVector2D(FMath::Min(Previous.Max.X,XY.Max.X),FMath::Min(Previous.Max.Y,XY.Max.Y)));
   const double Margin=Interior ? 1. : -.1,Top=Interior ? Bounds.Max.Z : FMath::Min(Bounds.Max.Z,double(Settings.RoofHeightCm));
   if (!Contains(Shell,Required,Margin) || Top>Proposed.RoofHeightCm-Margin)
   {
    if (!Remove(*It,It->GetActorNameOrLabel()+TEXT("이 새 건물 밖이나 천장에 걸립니다."))) return false;
    continue;
   }
   // Shelf loads inherit their supporting rack's floor; use the physical bottom,
   // never a tall actor centre that can extend into the next storey.
   const int32 Floor=FMath::Clamp(FMath::FloorToInt((Bounds.Min.Z+20)/400.),0,3);
   if (Interior && Floor>0 && !It->IsA<AWarehouseElevator>() && !It->IsA<AWarehousePalletLift>())
   {
    const auto& M=Upper(Proposed,Floor);
    TArray<FBox2D> Unsupported={XY};
    for (const auto& P:Parts) if ((P.Role==EPart::Floor || P.Role==EPart::Stair) && P.Floor==Floor && FMath::Abs(P.Bounds().Max.Z-Floor*400.)<.1)
    { const FBox B=P.Bounds();TArray<FBox2D> Next;for (const auto& Rect:Unsupported) Next.Append(Subtract(Rect,FBox2D(FVector2D(B.Min),FVector2D(B.Max)).ExpandBy(1)));Unsupported=MoveTemp(Next); }
    if (!M.bEnabled || !Unsupported.IsEmpty())
    {
     if (!Remove(*It,FString::Printf(TEXT("%s의 %d층 지지 바닥이 사라집니다."),*It->GetActorNameOrLabel(),Floor+1))) return false;
     continue;
    }
   }
  }
  for (const auto& Part:Parts)
  {
   if (Part.Role==EPart::Floor && (It->IsA<AWarehouseElevator>() || It->IsA<AWarehousePalletLift>())) continue;
   if (Part.Role==EPart::Light || Part.Role==EPart::Pipe || Part.Role==EPart::Tray) continue; // These are automatically omitted around preserved goods below.
   const FBox Planned=Part.Bounds();
   if (Overlaps(Bounds,Planned,1.))
   {
    const FBox ProposedWorld(Planned.Min+Origin,Planned.Max+Origin);
    if (UnchangedFloorSupport(this,Part,ProposedWorld,*It,World)) continue;
    if (Part.Role==EPart::Exterior && Part.Rotation.IsIdentity(.00001))
    {
     if (!ExteriorActuallyOverlaps(Part.Source,*It,ProposedWorld) || PreservesExistingExteriorContact(Part.Source,*It,ProposedWorld,World)) continue;
    }
    ReportCollision(TEXT("planned"),Part,FBox(Planned.Min+Origin,Planned.Max+Origin),*It,World);
    if (!Remove(*It,It->GetActorNameOrLabel()+TEXT("이 변경할 벽·바닥·기둥·계단·난간과 겹칩니다."))) return false;
    break;
   }
  }
 }
 return true;
}

bool AWarehouseBuildingLayout::ExteriorActuallyOverlaps(const AStaticMeshActor* Source,const AActor* Other,const FBox& ProposedWorld) const
{
 if (!IsValid(Source) || !IsValid(Other)) return true;
 auto* Original=Source->GetStaticMeshComponent();auto* Mesh=Original->GetStaticMesh().Get();
 const auto* Body=Original->GetBodySetup();
 // This exact query path is only for the unchanged convex facade module.
 // Unsupported geometry stays conservative rather than becoming an exemption.
 if (!Mesh || !Body || Body->AggGeom.GetElementCount()==0 || Body->CollisionTraceFlag==CTF_UseComplexAsSimple ||
     !ProposedWorld.GetSize().Equals(Original->Bounds.GetBox().GetSize(),.1)) return true;
 auto* Probe=NewObject<UStaticMeshComponent>(const_cast<AWarehouseBuildingLayout*>(this),NAME_None,RF_Transient);
 Probe->SetMobility(EComponentMobility::Movable);Probe->SetStaticMesh(Mesh);
 FTransform Pose=Original->GetComponentTransform();Pose.AddToTranslation(ProposedWorld.GetCenter()-Original->Bounds.GetBox().GetCenter());
 Probe->SetWorldTransform(Pose);Probe->SetVisibility(false);Probe->SetHiddenInGame(true);
 Probe->SetCollisionProfileName(TEXT("BlockAll"));Probe->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Probe->RegisterComponentWithWorld(GetWorld());
 const auto* Instance=Probe->GetBodyInstance();bool Blocking=true;
 if (Instance && Instance->IsValidBodyInstance())
 {
  FComponentQueryParams Query(SCENE_QUERY_STAT(BuildingExteriorPlacement),this);Query.AddIgnoredActor(Source);
  TArray<FOverlapResult> Hits;
  GetWorld()->ComponentOverlapMultiByChannel(Hits,Probe,Probe->GetComponentLocation(),Probe->GetComponentQuat(),ECC_Visibility,Query);
  Blocking=Hits.ContainsByPredicate([Other](const FOverlapResult& Hit)
  { return Hit.bBlockingHit && Hit.GetActor()==Other && WarehousePhysics::IsPhysicalContact(Hit.GetComponent()); });
 }
 Probe->DestroyComponent();return Blocking;
}

bool AWarehouseBuildingLayout::PreservesExistingExteriorContact(const AStaticMeshActor* Source,const AActor* OtherActor,const FBox& ProposedWorld,const FBox& OtherWorld) const
{
 if (!IsValid(Source) || !Source->GetStaticMeshComponent()->GetStaticMesh()) return false;
 const auto* Component=Source->GetStaticMeshComponent();const FBox Original=Component->Bounds.GetBox();
 if (!Original.Min.Equals(ProposedWorld.Min,.1) || !Original.Max.Equals(ProposedWorld.Max,.1) || !Overlaps(Original,OtherWorld,1.)) return false;
 auto SamePose=[](const FTransform& A,const FTransform& B)
 { return A.GetLocation().Equals(B.GetLocation(),.1) && A.GetRotation().Equals(B.GetRotation(),.00001) && A.GetScale3D().Equals(B.GetScale3D(),.00001); };
 for (const auto& Contact:ExteriorContacts) if (Contact.Source.Get()==Source && Contact.Other.IsValid() && Contact.Other->GetOwner()==OtherActor)
 {
  const auto* Other=Contact.Other.Get();
  if (Other->Mobility==EComponentMobility::Static && !Other->IsSimulatingPhysics() && WarehousePhysics::IsPhysicalContact(Other) &&
      Component->GetStaticMesh().Get()==Contact.SourceMesh.Get() && Other->GetStaticMesh().Get()==Contact.OtherMesh.Get() &&
      SamePose(Component->GetComponentTransform(),Contact.SourceTransform) && SamePose(Other->GetComponentTransform(),Contact.OtherTransform) &&
      Contact.OtherBounds.Min.Equals(OtherWorld.Min,.1) && Contact.OtherBounds.Max.Equals(OtherWorld.Max,.1)) return true;
 }
 return false;
}

void AWarehouseBuildingLayout::HideOriginalStructure()
{
 if (OriginalStates.IsEmpty()) for (AActor* Actor:ReplacedStructure) if (IsValid(Actor))
 {
  FOriginalState& Saved=OriginalStates.AddDefaulted_GetRef();Saved.Actor=Actor;Saved.Hidden=Actor->IsHidden();Saved.Collision=Actor->GetActorEnableCollision();
  TArray<UPrimitiveComponent*> Parts;Actor->GetComponents(Parts);
  for (auto* Part:Parts) Saved.Parts.Add({Part,Part->IsVisible(),Part->GetCollisionEnabled()});
 }
 for (auto& Saved:OriginalStates) if (auto* Actor=Saved.Actor.Get())
 {
  Actor->SetActorHiddenInGame(true);Actor->SetActorEnableCollision(false);
  for (auto& P:Saved.Parts) if (auto* Part=P.Part.Get()) { Part->SetVisibility(false);Part->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
 }
}
void AWarehouseBuildingLayout::PrepareExperimentRestore()
{
 if (HasActorBegunPlay()) return;
 CaptureExistingLayout();
 if (OriginalStates.IsEmpty()) for (AActor* Actor:ReplacedStructure) if (IsValid(Actor))
 {
  FOriginalState& Saved=OriginalStates.AddDefaulted_GetRef();Saved.Actor=Actor;Saved.Hidden=Actor->IsHidden();Saved.Collision=Actor->GetActorEnableCollision();
  TArray<UPrimitiveComponent*> Parts;Actor->GetComponents(Parts);
  for (auto* Part:Parts) Saved.Parts.Add({Part,Part->IsVisible(),Part->GetCollisionEnabled()});
 }
}
void AWarehouseBuildingLayout::RestoreExperimentRuntimeState()
{
 if (HasActorBegunPlay()) return;
 if (!bHasAppliedChanges) { OriginalStates.Reset();return; }
 HideOriginalStructure();GeneratedLights.Reset();
 for (TActorIterator<ASpotLight> It(GetWorld());It;++It) if (It->ActorHasTag(TEXT("WarehouseBuildingGenerated"))) GeneratedLights.Add(*It);
}
void AWarehouseBuildingLayout::RestoreOriginalStructure()
{
 for (auto& Saved:OriginalStates) if (auto* Actor=Saved.Actor.Get())
 {
  Actor->SetActorHiddenInGame(Saved.Hidden);Actor->SetActorEnableCollision(Saved.Collision);
  for (auto& P:Saved.Parts) if (auto* Part=P.Part.Get()) { Part->SetVisibility(P.Visible);Part->SetCollisionEnabled(P.Collision); }
 }
 OriginalStates.Reset();
}

bool AWarehouseBuildingLayout::ApplySettings(const FWarehouseBuildingSettings& Proposed)
{
 if (AWarehousePlayStage::Find(this)) { Status=TEXT("플레이 스테이지의 건물·층수·지형은 고정입니다.");return false; }
 CaptureExistingLayout();FString Reason;TArray<AActor*> Removed;TArray<AWarehouseForklift*> Relocate;
 if (!ValidateSettingsInternal(Proposed,Reason,Removed,Relocate)) { Status=Reason;return false; }
 const bool WasPaused=UGameplayStatics::IsGamePaused(this),GameWorld=GetWorld()->IsGameWorld();
 if (GameWorld && !WasPaused) UGameplayStatics::SetGamePaused(this,true);
 bool Committed=false;
 struct FStaged { TWeakObjectPtr<AActor> Actor;bool Hidden,Collision; };
 TArray<FStaged> Staged;
 TArray<AWarehouseLayoutEditor*> Movers;
 auto* Store=GameWorld ? AWarehouseLayoutInventory::Find(this) : nullptr;
 auto* LiveLayout=GameWorld ? AWarehouseLayoutEditor::Find(this) : nullptr;
 const int32 HeldBefore=Store ? Store->HeldCargo.Num() : 0;
 const int32 InventoryRevision=Store ? Store->Revision : 0;
 const FString InventoryMessage=Store ? Store->LastMessage : FString();
 ON_SCOPE_EXIT
 {
  if (!Committed)
  {
   for (const auto& Entry:Staged) if (Entry.Actor.IsValid())
   { Entry.Actor->SetActorHiddenInGame(Entry.Hidden);Entry.Actor->SetActorEnableCollision(Entry.Collision); }
   if (Store) { Store->HeldCargo.SetNum(HeldBefore);Store->Revision=InventoryRevision;Store->LastMessage=InventoryMessage; }
  }
  for (auto* Mover:Movers)
  {
   Mover->Restore(!Committed);Mover->Destroy();
  }
  if (Committed) for (auto* Vehicle:Relocate) if (IsValid(Vehicle))
  { Vehicle->SuspendForBuildingEdit({});Vehicle->RefreshAfterLayout(Vehicle->GetSupportedPallet()!=nullptr); }
  if (GameWorld && !WasPaused) UGameplayStatics::SetGamePaused(this,false);
 };
 // Expand complete rack groups and supported loads before hiding anything.
 auto ExpandRemoval=[&]()
 {
  auto* Strength=AWarehouseDamageSystem::Find(this);if (Strength) Strength->EnsureSupportSnapshot();
  for (int32 I=0;I<Removed.Num();++I)
  {
   AActor* Actor=Removed[I];
   const auto* Pawn=Cast<APawn>(Actor);
   if (auto* Vehicle=Cast<AWarehouseForklift>(Actor)) { Relocate.AddUnique(Vehicle);continue; }
   if (const auto* Lift=Cast<AWarehouseElevator>(Actor);Lift && Lift->bHumanService)
   { Status=TEXT("사람이 이용 중인 엘리베이터는 하차 후 변경하세요.");return false; }
   if (const auto* Lift=Cast<AWarehouseElevator>(Actor);Lift && IsValid(Lift->ReservedVehicle)) Relocate.AddUnique(Lift->ReservedVehicle);
   if (const auto* Lift=Cast<AWarehousePalletLift>(Actor))
    for (TActorIterator<AActor> It(GetWorld());It;++It) if (Lift->IsHandlingCargo(*It)) Removed.AddUnique(*It);
   if (Pawn && Pawn->IsPlayerControlled())
   { Status=Actor->GetActorNameOrLabel()+TEXT("의 실제 지지/공간을 유지해야 합니다. 남는 바닥으로 먼저 재배치하세요.");return false; }
   for (TActorIterator<Amsc_vrCharacter> It(GetWorld());It;++It) if (It->GetHeldCargo()==Actor)
   { Status=TEXT("플레이어가 들고 있는 물건은 내려놓은 뒤 건물을 편집하세요.");return false; }
   for (FName Tag:Actor->Tags) if (Tag.ToString().StartsWith(TEXT("LayoutGroup:")))
    for (TActorIterator<AActor> It(GetWorld());It;++It) if (It->ActorHasTag(Tag) && !ReplacedStructure.Contains(*It)) Removed.AddUnique(*It);
   if (Strength) for (AActor* Load:Strength->GetSupportedActors(Actor,false)) if (IsValid(Load)) Removed.AddUnique(Load);
   if (const auto* Pallet=Cast<AWarehousePallet>(Actor)) for (auto* Load:Pallet->GetWrappedCargo()) if (IsValid(Load)) Removed.AddUnique(Load);
  }
  for (auto* Vehicle:Relocate)
  {
   Removed.Remove(Vehicle);
   for (AActor* Load:Vehicle->GetPhysicalLoads()) Removed.Remove(Load);
  }
  return true;
 };
 if (!ExpandRemoval()) return false;
 if (!Removed.IsEmpty())
 {
  Store=AWarehouseLayoutInventory::Find(this);Store->CapturePrototypes(LiveLayout);
  for (AActor* Actor:Removed)
  { Staged.Add({Actor,Actor->IsHidden(),Actor->GetActorEnableCollision()});Actor->SetActorHiddenInGame(true);Actor->SetActorEnableCollision(false); }
 }
 const auto Parts=Plan(this,Proposed);TArray<TObjectPtr<AStaticMeshActor>> Next;
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));if (!Cube) { Status=TEXT("구조용 메시를 불러올 수 없습니다.");return false; }
 // Build a complete inactive replacement first. A failed allocation cannot remove support.
 for (int32 I=0;I<Parts.Num();++I)
 {
  const auto& Part=Parts[I];auto* Source=Template(this,Part.Role,Part.Source);auto* Original=IsValid(Source) ? Source->GetStaticMeshComponent() : nullptr;
  UStaticMesh* Mesh=Original ? Original->GetStaticMesh().Get() : Cube;
  if (!Mesh) { DestroyParts(Next);Status=TEXT("건축 템플릿 메시가 없습니다.");return false; }
  FActorSpawnParameters Spawn;Spawn.Owner=this;Spawn.ObjectFlags|=RF_Transient;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  auto* Actor=GetWorld()->SpawnActor<AStaticMeshActor>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
  if (!Actor) { DestroyParts(Next);Status=TEXT("건물 생성 실패 · 원래 구조 유지");return false; }Next.Add(Actor);
  Actor->SetActorHiddenInGame(true);Actor->SetActorEnableCollision(false);
  Actor->Tags={TEXT("LayoutFixed"),TEXT("WarehouseBuildingGenerated"),TEXT("WarehouseMapStructure"),FName(*FString::Printf(TEXT("WarehouseFloor%d"),Part.Floor))};
  if (Part.Role==EPart::Floor) { Actor->Tags.Add(TEXT("ElevatorLayoutFloor"));if (!Part.Floor) Actor->Tags.Add(TEXT("ObserverArea")); }
  if (Part.Role==EPart::Roof || (Part.Centre.Z>Proposed.RoofHeightCm-120 && (Part.Role==EPart::Pipe || Part.Role==EPart::Tray || Part.Role==EPart::Light || Part.Role==EPart::Beam))) Actor->Tags.Add(TEXT("ObserverRoof"));
  if (Part.Role==EPart::Wall) Actor->Tags.Add(TEXT("ObserverFadeWall"));
  if (Part.Role==EPart::Exterior)
  { Actor->Tags.Add(TEXT("WarehouseEntranceExterior"));if (Source->ActorHasTag(TEXT("ObserverRoof"))) Actor->Tags.Add(TEXT("ObserverRoof")); }
  if (Part.Role==EPart::Pipe || Part.Role==EPart::Tray || Part.Role==EPart::Light) Actor->Tags.Add(TEXT("WarehouseServices"));
  auto* Component=Actor->GetStaticMeshComponent();Component->SetMobility(EComponentMobility::Movable);Component->SetStaticMesh(Mesh);
  if (Original) for (int32 Slot=0;Slot<Original->GetNumMaterials();++Slot) Component->SetMaterial(Slot,Original->GetMaterial(Slot));
  const FQuat BaseRotation=Original ? Original->GetComponentQuat() : FQuat::Identity;
  const FVector OldScale=Original ? Original->GetComponentScale() : FVector::OneVector;
  const FVector OldSize=Original ? Original->Bounds.GetBox().GetSize() : Mesh->GetBoundingBox().GetSize();
  const FVector Factor=Part.Size/OldSize.ComponentMax(FVector(.01));FVector Scale;
  Scale.X=OldScale.X*FVector::DotProduct(BaseRotation.GetAxisX().GetAbs(),Factor);
  Scale.Y=OldScale.Y*FVector::DotProduct(BaseRotation.GetAxisY().GetAbs(),Factor);
  Scale.Z=OldScale.Z*FVector::DotProduct(BaseRotation.GetAxisZ().GetAbs(),Factor);
  const FQuat Rotation=Part.Rotation*BaseRotation;
  Actor->SetActorTransform(FTransform(Rotation,GetActorLocation()+Part.Centre-Rotation.RotateVector(Mesh->GetBoundingBox().GetCenter()*Scale),Scale));
  const auto Surface=Part.Role==EPart::Exterior && Original ? WarehousePhysics::SurfaceFor(Original) :
   Part.Role==EPart::Floor || Part.Role==EPart::Wall || Part.Role==EPart::Stair ? EWarehouseSurface::Concrete : EWarehouseSurface::Steel;
  Component->SetCollisionProfileName(TEXT("BlockAll"));WarehousePhysics::ConfigureContact(Component,Surface);
  Component->SetMobility(EComponentMobility::Static);
#if WITH_EDITOR
  Actor->SetActorLabel(FString::Printf(TEXT("BLD_L%d_%02d_%04d"),Part.Floor+1,int32(Part.Role),I));
#endif
 }
 TArray<FBox> MovedBounds;
 TSet<AWarehouseForklift*> Positioned;
 auto RelocateVehicles=[&]()
 {
  for (auto* Vehicle:Relocate)
  {
   if (Positioned.Contains(Vehicle)) continue;
   TArray<AActor*> Members{Vehicle};Members.Append(Vehicle->GetPhysicalLoads());
   FBox Before(ForceInit);
   for (AActor* Member:Members)
   {
    const int32 StagedIndex=Staged.IndexOfByPredicate([&](const auto& Entry) { return Entry.Actor==Member; });
    if (StagedIndex!=INDEX_NONE)
    { Member->SetActorHiddenInGame(Staged[StagedIndex].Hidden);Member->SetActorEnableCollision(Staged[StagedIndex].Collision);Staged.RemoveAt(StagedIndex); }
    Removed.Remove(Member);Before+=AWarehouseDamageSystem::GetPhysicalBounds(Member);
   }
   if (!Before.IsValid) { Status=TEXT("지게차 실제 충돌 형상을 확인할 수 없어 건물 변경을 취소했습니다.");return false; }
   const FVector Start=Vehicle->GetActorLocation();
   FActorSpawnParameters Spawn;Spawn.ObjectFlags|=RF_Transient;
   auto* Mover=GetWorld()->SpawnActor<AWarehouseLayoutEditor>(Spawn);
   if (!Mover) { Status=TEXT("지게차 재배치 준비 실패 · 건물 유지");return false; }
   Movers.Add(Mover);Mover->CaptureMoving(Members);
   bool Found=false;const int32 CurrentFloor=FMath::Clamp(FMath::FloorToInt((Start.Z+20)/400.),0,3);
   TArray<int32> Floors{CurrentFloor};if (CurrentFloor) Floors.Add(0);
   for (int32 Floor:Floors)
   {
    if (Floor && !Upper(Proposed,Floor).bEnabled) continue;
    TArray<FVector> Candidates;
    for (double X=-Proposed.WidthCm*.5+100;X<=Proposed.WidthCm*.5-100;X+=100)
     for (double Y=-Proposed.DepthCm*.5+100;Y<=Proposed.DepthCm*.5-100;Y+=100) Candidates.Add(GetActorLocation()+FVector(X,Y,Floor*400.));
    Candidates.Sort([&](const FVector& A,const FVector& B) { return FVector::DistSquared(A,Start)<FVector::DistSquared(B,Start); });
    for (const FVector& Point:Candidates)
    {
     const FVector Offset=Point-Start;const FBox Body(Before.Min+Offset,Before.Max+Offset);
     const FBox2D XY(FVector2D(Body.Min-GetActorLocation()),FVector2D(Body.Max-GetActorLocation()));
     if (!Contains(Footprint(Proposed),XY,5) || Body.Max.Z>Proposed.RoofHeightCm-5 || Body.Min.Z<Floor*400.-2) continue;
     TArray<FBox2D> Unsupported{XY};bool Clear=true;
     for (int32 I=0;I<Next.Num();++I)
     {
      const FBox Part=Next[I]->GetStaticMeshComponent()->Bounds.GetBox();
      if (Parts[I].Role==EPart::Floor && Parts[I].Floor==Floor && FMath::Abs(Part.Max.Z-Floor*400.)<.1)
      {
       const FBox2D Bearing(FVector2D(Part.Min-GetActorLocation()),FVector2D(Part.Max-GetActorLocation()));TArray<FBox2D> Remains;
       for (const auto& Rect:Unsupported) Remains.Append(Subtract(Rect,Bearing));Unsupported=MoveTemp(Remains);continue;
      }
      if (Overlaps(Body,Part,1)) { Clear=false;break; }
     }
     if (!Clear || !Unsupported.IsEmpty()) continue;
     for (const FBox& Other:MovedBounds) if (Overlaps(Body,Other,5)) { Clear=false;break; }
     if (!Clear) continue;
     for (TActorIterator<AActor> It(GetWorld());It;++It) if (Preserved(this,*It) && !Members.Contains(*It) && !Removed.Contains(*It))
     {
      const FBox Other=AWarehouseDamageSystem::GetPhysicalBounds(*It);if (Other.IsValid && Overlaps(Body,Other,5)) { Clear=false;break; }
     }
     if (!Clear) continue;
     Mover->Delta=Offset;Mover->ApplyPreview();MovedBounds.Add(Body);Positioned.Add(Vehicle);Found=true;break;
    }
    if (Found) break;
   }
   if (!Found) { Status=Vehicle->VehicleName+TEXT("와 적재물을 함께 둘 안전한 바닥/공간이 없습니다. 건물과 물건을 원복했습니다.");return false; }
  }
  return true;
 };
 if (!RelocateVehicles()) { DestroyParts(Next);return false; }
 // A non-box imported module can have a different oriented bound from its planning
 // rectangle. Check the instantiated geometry before disabling any original support.
 TArray<TPair<AActor*,FBox>> Occupants;
 for (TActorIterator<AActor> It(GetWorld());It;++It) if (Preserved(this,*It))
 { const FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(*It);if (Bounds.IsValid) Occupants.Emplace(*It,Bounds); }
 for (int32 I=0;I<Next.Num();++I) for (const auto& Occupant:Occupants)
 {
  if (Removed.Contains(Occupant.Key)) continue;
  if (Parts[I].Role==EPart::Floor && (Occupant.Key->IsA<AWarehouseElevator>() || Occupant.Key->IsA<AWarehousePalletLift>())) continue;
  const auto* Part=Next[I]->GetStaticMeshComponent();const FBox Bounds=Part->Bounds.GetBox();
  if (!Overlaps(Bounds,Occupant.Value,1)) continue;
  if (UnchangedFloorSupport(this,Parts[I],Bounds,Occupant.Key,Occupant.Value)) continue;
  const auto* Source=IsValid(Parts[I].Source) ? Parts[I].Source->GetStaticMeshComponent() : nullptr;
  if (Parts[I].Role==EPart::Exterior && Source && Part->GetStaticMesh()==Source->GetStaticMesh() &&
      Part->GetComponentQuat().Equals(Source->GetComponentQuat(),.00001) &&
      Part->GetComponentScale().Equals(Source->GetComponentScale(),.00001) &&
      (!ExteriorActuallyOverlaps(Parts[I].Source,Occupant.Key,Bounds) || PreservesExistingExteriorContact(Parts[I].Source,Occupant.Key,Bounds,Occupant.Value))) continue;
  ReportCollision(TEXT("generated"),Parts[I],Bounds,Occupant.Key,Occupant.Value);
  if (auto* Vehicle=Cast<AWarehouseForklift>(Occupant.Key))
  {
   Relocate.AddUnique(Vehicle);if (!RelocateVehicles()) { DestroyParts(Next);return false; }continue;
  }
  Removed.AddUnique(Occupant.Key);
  if (!Store || !ExpandRemoval()) { DestroyParts(Next);return false; }
  if (!RelocateVehicles()) { DestroyParts(Next);return false; }
  Store->CapturePrototypes(LiveLayout);
  for (AActor* Actor:Removed) if (!Staged.ContainsByPredicate([&](const auto& Entry) { return Entry.Actor==Actor; }))
  { Staged.Add({Actor,Actor->IsHidden(),Actor->GetActorEnableCollision()});Actor->SetActorHiddenInGame(true);Actor->SetActorEnableCollision(false); }
 }
 // Persist every parcel before deleting anything. A duplicate ID cancels the whole transaction.
 if (Store) for (AActor* Actor:Removed) if (auto* Cargo=Cast<AWarehouseCargo>(Actor))
  if (!Store->HoldCargo(Cargo)) { DestroyParts(Next);Status=Store->LastMessage;return false; }
 const int32 Floors=Proposed.Floor4.bEnabled ? 4 : Proposed.Floor3.bEnabled ? 3 : Proposed.Floor2.bEnabled ? 2 : 1;
 TArray<TPair<AWarehouseElevator*,int32>> GeneralFloors;TArray<TPair<AWarehousePalletLift*,int32>> CargoFloors;
 auto RestoreLiftFloors=[&]()
 {
  for (const auto& Pair:GeneralFloors) if (IsValid(Pair.Key)) Pair.Key->SetFloorCount(Pair.Value);
  for (const auto& Pair:CargoFloors) if (IsValid(Pair.Key)) Pair.Key->SetFloorCount(Pair.Value);
 };
 for (TActorIterator<AWarehouseElevator> It(GetWorld());It;++It) if (!Removed.Contains(*It) && It->FloorCount!=Floors)
 {
  GeneralFloors.Emplace(*It,It->FloorCount);
  if (!It->SetFloorCount(Floors)) { RestoreLiftFloors();DestroyParts(Next);Status=TEXT("범용 승강기 층수 변경 실패 · 건물 유지");return false; }
 }
 for (TActorIterator<AWarehousePalletLift> It(GetWorld());It;++It) if (!Removed.Contains(*It) && It->FloorCount!=Floors)
 {
  CargoFloors.Emplace(*It,It->FloorCount);
  if (!It->SetFloorCount(Floors)) { RestoreLiftFloors();DestroyParts(Next);Status=TEXT("화물용 승강기 층수 변경 실패 · 건물 유지");return false; }
 }
 const auto PreviousSettings=Settings;auto PreviousActors=GeneratedStructure;
 WarehouseElevatorLayout::RestoreInitial(this);HideOriginalStructure();
 // Keep previous support until the replacement is fully live; no simulation frame runs here.
 for (AStaticMeshActor* Actor:PreviousActors) if (IsValid(Actor)) { Actor->SetActorHiddenInGame(true);Actor->SetActorEnableCollision(false);Actor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
 GeneratedStructure=MoveTemp(Next);Settings=Proposed;
 for (AStaticMeshActor* Actor:GeneratedStructure) { Actor->SetActorHiddenInGame(false);Actor->SetActorEnableCollision(true); }
 // Bind source slabs before shaft splitting, so its live replacement pieces
 // inherit the same existing strength profile and accumulated structural damage.
 if (auto* Strength=AWarehouseDamageSystem::Find(this))
 {
  for (int32 I=0;I<GeneratedStructure.Num();++I)
  {
   auto* Source=Template(this,Parts[I].Role,Parts[I].Source);
   for (auto& Profile:Strength->Objects) if (Profile.Members.Contains(Source))
   { Profile.PhysicsMeshes.SetNum(Profile.Members.Num());Profile.Members.Add(GeneratedStructure[I]);Profile.PhysicsMeshes.Add(nullptr);break; }
   Strength->RegisterPhysicalContacts(GeneratedStructure[I]);
  }
  Strength->RefreshLayoutBindings();
 }
 // Original labels remain saved, so remove their source markers while capturing only
 // this transaction's slabs. Restore them immediately after the helper captures.
 TArray<TPair<AActor*,TArray<FName>>> OriginalTags;
 for (AActor* Actor:ReplacedStructure) if (IsValid(Actor)) { OriginalTags.Emplace(Actor,Actor->Tags);Actor->Tags.Remove(TEXT("ElevatorLayoutFloor")); }
 const bool HaveLift=!Shafts(this).IsEmpty();
 const bool OpeningsReady=!GetWorld()->IsGameWorld() || !HaveLift || WarehouseElevatorLayout::Rebuild(this,false,Removed);
 for (const auto& Pair:OriginalTags) if (IsValid(Pair.Key)) Pair.Key->Tags=Pair.Value;
 if (!OpeningsReady)
 {
  RestoreLiftFloors();
  if (auto* Strength=AWarehouseDamageSystem::Find(this))
  { TArray<AActor*> FailedParts;for (AStaticMeshActor* Actor:GeneratedStructure) FailedParts.Add(Actor);Strength->RemoveLayoutActors(FailedParts); }
  WarehouseElevatorLayout::RestoreInitial(this);DestroyParts(GeneratedStructure);GeneratedStructure=MoveTemp(PreviousActors);Settings=PreviousSettings;
  for (AStaticMeshActor* Actor:GeneratedStructure) if (IsValid(Actor)) { Actor->SetActorHiddenInGame(false);Actor->SetActorEnableCollision(true);Actor->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); }
  if (GeneratedStructure.IsEmpty()) RestoreOriginalStructure();else WarehouseElevatorLayout::Rebuild(this,false);
  Status=TEXT("승강기 개구부를 갱신할 수 없습니다. 건물 변경을 취소했습니다.");return false;
 }
 if (auto* Strength=AWarehouseDamageSystem::Find(this))
 {
  TArray<AActor*> OldParts;for (AStaticMeshActor* Actor:PreviousActors) OldParts.Add(Actor);Strength->RemoveLayoutActors(OldParts);
  Strength->RefreshLayoutBindings();
 }
 // Only after every geometry/lift check succeeds do operational state and live stock change.
 int32 PausedVehicles=0;
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
 {
  const bool Running=It->bPowered || It->IsRemoteControlled();
  if (auto* Operator=Cast<Amsc_vrCharacter>(It->GetRemoteOperator())) Operator->EndRemoteControl();
  It->SuspendForBuildingEdit(Removed);PausedVehicles+=Running ? 1 : 0;
 }
 if (Store && !Removed.IsEmpty()) { Store->ForgetActors(Removed);for (AActor* Actor:Removed) if (IsValid(Actor)) Actor->Destroy(); }
 Committed=true;
 DestroyParts(PreviousActors);bHasAppliedChanges=true;++Revision;
 for (AActor* Light:GeneratedLights) if (IsValid(Light)) Light->Destroy();GeneratedLights.Reset();
 for (const FPart& Part:Parts) if (Part.Role==EPart::Light)
 {
  FActorSpawnParameters Spawn;Spawn.Owner=this;Spawn.ObjectFlags|=RF_Transient;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  if (auto* Light=GetWorld()->SpawnActor<ASpotLight>(GetActorLocation()+Part.Centre-FVector(0,0,10),FRotator(-90,0,0),Spawn))
  {
   GeneratedLights.Add(Light);Light->Tags={TEXT("LayoutFixed"),TEXT("WarehouseBuildingGenerated"),FName(*FString::Printf(TEXT("WarehouseFloor%d"),Part.Floor))};
   if (Part.Centre.Z>Settings.RoofHeightCm-120) Light->Tags.Add(TEXT("ObserverRoof"));
   auto* Component=Light->SpotLightComponent.Get();Component->SetMobility(EComponentMobility::Movable);
   Component->SetIntensityUnits(ELightUnits::Lumens);Component->SetIntensity(8500.f);Component->SetAttenuationRadius(1050.f);
   Component->SetOuterConeAngle(75.f);Component->SetInnerConeAngle(55.f);Component->SetCastShadows(false);
  }
 }
 if (auto* Layout=AWarehouseLayoutEditor::Find(this)) Layout->NotifyStructureChanged();
 else
 {
  for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) It->RefreshAfterLayout(false);
  for (TActorIterator<AWarehouseComputer> It(GetWorld());It;++It) It->RefreshState();
 }
 Status=FString::Printf(TEXT("건물 적용 · %.0f × %.0fm · 지게차 %d대 일시정지/%d대 안전 재배치 · 상자 %d개 보류 · 간섭 요소 %d개 정리"),Settings.WidthCm/100,Settings.DepthCm/100,PausedVehicles,Positioned.Num(),Store ? Store->HeldCargo.Num()-HeldBefore : 0,Removed.Num());
 return true;
}
bool AWarehouseBuildingLayout::RebuildGeometry() { return ApplySettings(Settings); }
void AWarehouseBuildingLayout::EndPlay(const EEndPlayReason::Type Reason)
{
 WarehouseElevatorLayout::RestoreInitial(this);
 if (auto* Strength=AWarehouseDamageSystem::Find(this)) { TArray<AActor*> Removed;for (AStaticMeshActor* Actor:GeneratedStructure) Removed.Add(Actor);Strength->RemoveLayoutActors(Removed); }
 DestroyParts(GeneratedStructure);for (AActor* Light:GeneratedLights) if (IsValid(Light)) Light->Destroy();GeneratedLights.Reset();
 RestoreOriginalStructure();Super::EndPlay(Reason);
}
