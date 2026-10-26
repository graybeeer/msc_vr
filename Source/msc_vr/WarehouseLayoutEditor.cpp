#include "WarehouseLayoutEditor.h"
#include "WarehousePlayMode.h"
#include "Components/SceneComponent.h"
#include "WarehouseForklift.h"
#include "WarehouseChargingStation.h"
#include "WarehouseElevator.h"
#include "WarehousePalletLift.h"
#include "WarehouseConveyor.h"
#include "WarehouseElevatorLayout.h"
#include "WarehouseLayoutSnap.h"
#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehouseWorker.h"
#include "WarehouseComputer.h"
#include "WarehouseInboundZone.h"
#include "WarehouseFleetCoordinator.h"
#include "WarehouseDamageSystem.h"
#include "WarehousePhysics.h"
#include "WarehouseLayoutInventory.h"
#include "WarehouseWaitingBay.h"
#include "msc_vrCharacter.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
 FName GroupOf(AActor* Actor)
 {
  if (Actor) for (FName Tag:Actor->Tags) if (Tag.ToString().StartsWith(TEXT("LayoutGroup:"))) return Tag;
  return NAME_None;
 }
 bool InsideXY(const FBox& Box,FVector Point)
 { return Point.X>=Box.Min.X && Point.X<=Box.Max.X && Point.Y>=Box.Min.Y && Point.Y<=Box.Max.Y; }
 bool IsShaft(const AActor* Actor)
 { return Actor && (Actor->IsA<AWarehouseElevator>() || Actor->IsA<AWarehousePalletLift>()); }
 bool OnlyBoxSurfaceContact(UPrimitiveComponent* Part,UPrimitiveComponent* Surface)
 {
  const auto* Mesh=Cast<UStaticMeshComponent>(Part);
  const auto* Setup=Mesh && Mesh->GetStaticMesh() ? Mesh->GetStaticMesh()->GetBodySetup() : nullptr;
  auto* Body=Surface->GetBodyInstance();
  if (!Setup || !Body || !Body->IsValidBodyInstance() || Setup->AggGeom.BoxElems.IsEmpty() ||
      Setup->AggGeom.BoxElems.Num()!=Setup->AggGeom.GetElementCount()) return false;
  // A compound landing has a recessed threshold below its jamb bases. Whole
  // mesh bounds cannot distinguish that from jamb/floor face contact. Query
  // each solid against the actual surface body and retain any real penetration.
  for (const FKBoxElem& Box:Setup->AggGeom.BoxElems)
  {
   const FTransform Shape=Box.GetTransform()*Part->GetComponentTransform();
   const FVector Half=FVector(Box.X,Box.Y,Box.Z)*.5f*Shape.GetScale3D().GetAbs();
   FMTDResult MTD;
   if (Body->OverlapTest(Shape.GetLocation(),Shape.GetRotation(),FCollisionShape::MakeBox(Half),&MTD) && MTD.Distance>.05f) return false;
  }
  return true;
 }
 bool ShaftBusy(AActor* Actor)
 {
  if (const auto* Lift=Cast<AWarehouseElevator>(Actor)) return Lift->State!=EWarehouseElevatorState::Idle || Lift->ReservedVehicle;
  if (const auto* Lift=Cast<AWarehousePalletLift>(Actor)) return !Lift->IsIdleForLayout();
  return false;
 }
 bool HandledByPalletLift(AActor* Actor)
 {
  if (!IsValid(Actor) || !Actor->GetWorld()) return false;
  for (TActorIterator<AWarehousePalletLift> It(Actor->GetWorld());It;++It) if (It->IsHandlingCargo(Actor)) return true;
  return false;
 }
 bool HandledByConveyorTransfer(AActor* Actor)
 {
  if (!IsValid(Actor) || !Actor->GetWorld()) return false;
  for (TActorIterator<AWarehouseConveyor> It(Actor->GetWorld());It;++It)
   if ((It->State==EWarehouseConveyorState::LoadingLift || It->State==EWarehouseConveyorState::LiftTravelling ||
        It->State==EWarehouseConveyorState::ReceivingLift ||
        (It->Kind==EWarehouseConveyorKind::RotaryTransfer && (It->RotaryPhase==EWarehouseRotaryPhase::Centering ||
         It->RotaryPhase==EWarehouseRotaryPhase::Rotating || It->RotaryPhase==EWarehouseRotaryPhase::Discharging))) && It->IsHandlingCargo(Actor)) return true;
  return false;
 }
}
AWarehouseLayoutEditor::AWarehouseLayoutEditor()
{
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Layout")));
}
AWarehouseLayoutEditor* AWarehouseLayoutEditor::Find(const AActor* Context)
{
 if (!Context || !Context->GetWorld()) return nullptr;
 for (TActorIterator<AWarehouseLayoutEditor> It(Context->GetWorld());It;++It) return *It;
 return Context->GetWorld()->SpawnActor<AWarehouseLayoutEditor>();
}
void AWarehouseLayoutEditor::SetEditing(bool Enabled,int32 Floor)
{
 if (bDragging) EndDrag(false);
 if (!Enabled || FloorIndex!=FMath::Clamp(Floor,0,3)) ClearSelection();
 if (Enabled==bEditing) { FloorIndex=FMath::Clamp(Floor,0,3);return; }
 bEditing=Enabled;FloorIndex=FMath::Clamp(Floor,0,3);MouseWasDown=false;
 if (Enabled)
 {
  AWarehouseLayoutInventory::Find(this)->CapturePrototypes(this);
  WarehouseElevatorLayout::Initialize(this);
  WasPaused=UGameplayStatics::IsGamePaused(this);
  if (!UGameplayStatics::SetGamePaused(this,true)) { bEditing=false;Status=TEXT("재배치 일시정지를 시작할 수 없습니다.");return; }
  Status=TEXT("재배치 · 시뮬레이션 일시정지 · 좌클릭 드래그 · 10cm 격자");
 }
 else { UGameplayStatics::SetGamePaused(this,WasPaused);Status=TEXT("재배치 종료 · 경로 갱신 · 시뮬레이션 재개"); }
}
void AWarehouseLayoutEditor::SetCategory(EWarehouseLayoutCategory Value)
{ if (bDragging) EndDrag(false);ClearSelection();Category=Value;AddChoice=0;MouseWasDown=false; }
void AWarehouseLayoutEditor::SetAction(EWarehouseLayoutAction Value)
{ if (bDragging) EndDrag(false);if (Value==EWarehouseLayoutAction::Add) { ClearSelection();if (Category==EWarehouseLayoutCategory::All) { Category=EWarehouseLayoutCategory::Rack;AddChoice=0; } }Action=Value;MouseWasDown=false;Status=Value==EWarehouseLayoutAction::Move ? TEXT("재배치 · 선택 후 드래그") : Value==EWarehouseLayoutAction::Delete ? TEXT("삭제 · 클릭 또는 다중 선택 후 선택 삭제 · 상자는 보류 목록으로 이동") : TEXT("추가 · 원본 종류 선택 후 클릭·드래그 · 상자는 보류 목록의 첫 항목만 배치"); }
void AWarehouseLayoutEditor::CycleAddChoice()
{ const auto Names=AWarehouseLayoutInventory::Find(this)->Choices(Category);AddChoice=Names.IsEmpty() ? 0 : (AddChoice+1)%Names.Num(); }
FString AWarehouseLayoutEditor::GetAddChoiceLabel() const
{
 const auto Names=AWarehouseLayoutInventory::Find(this)->Choices(Category);
 if (const auto* Stage=AWarehousePlayStage::Find(this);Stage && Stage->StageIndex>0)
 {
  const FString Name=Category==EWarehouseLayoutCategory::Forklift ? TEXT("무인 지게차") :
   Category==EWarehouseLayoutCategory::Charger ? TEXT("공유 충전기") :
   Category==EWarehouseLayoutCategory::WaitingBay ? TEXT("일반 대기 자리") : Names.IsValidIndex(AddChoice) ? Names[AddChoice] : FString();
  return (Name.IsEmpty() ? FString() : Name+TEXT(" · "))+Stage->PurchaseDescription(Category);
 }
 return Names.IsValidIndex(AddChoice) ? Names[AddChoice] : TEXT("추가 가능한 요소 없음");
}
bool AWarehouseLayoutEditor::Eligible(AActor* Actor) const
{
 if (const auto* Stage=AWarehousePlayStage::Find(this);Stage && Stage->StageIndex>0 && !Stage->CanEditActor(Actor)) return false;
 // Older saved metadata may have marked a charging bay as fixed.
 if (Actor && Actor->ActorHasTag(TEXT("LogisticsInTransit"))) return false;
 if (!Actor || Actor==this || (Actor->ActorHasTag(TEXT("LayoutFixed")) && !Actor->IsA<AWarehouseChargingStation>())) return false;
 if (Actor->IsA<AWarehouseChargingStation>() && Category!=EWarehouseLayoutCategory::Charger && Category!=EWarehouseLayoutCategory::All) return false;
 const auto* PC=GetWorld()->GetFirstPlayerController();
 if (PC && (Actor==PC->GetPawn() || PC->HiddenActors.Contains(Actor))) return false;
 if (IsShaft(Actor)) return Category==EWarehouseLayoutCategory::Elevator || Category==EWarehouseLayoutCategory::All;
 const FName FloorTag(*FString::Printf(TEXT("WarehouseFloor%d"),FloorIndex));
 const bool TaggedFloor=Actor->ActorHasTag(TEXT("WarehouseFloor0")) || Actor->ActorHasTag(TEXT("WarehouseFloor1")) || Actor->ActorHasTag(TEXT("WarehouseFloor2")) || Actor->ActorHasTag(TEXT("WarehouseFloor3"));
 const bool Dynamic=Actor->IsA<AWarehouseForklift>() || Actor->IsA<AWarehousePallet>() || Actor->IsA<AWarehouseCargo>() || Actor->IsA<AWarehouseWorker>();
 // Cargo/vehicles can change floors during real elevator transport. Their
 // original authoring tag cannot override their current physical floor.
 if (!Dynamic && TaggedFloor ? !Actor->ActorHasTag(FloorTag) : FMath::FloorToInt((FMath::Max(0.,Actor->GetActorLocation().Z)+1.)/400.)!=FloorIndex) return false;
 switch (Category)
 {
 case EWarehouseLayoutCategory::Conveyor:return Actor->IsA<AWarehouseConveyor>();
 case EWarehouseLayoutCategory::Rack:return Actor->ActorHasTag(TEXT("LayoutType:Rack"));
 case EWarehouseLayoutCategory::Forklift:return Actor->IsA<AWarehouseForklift>();
 case EWarehouseLayoutCategory::Elevator:return Actor->ActorHasTag(TEXT("LayoutType:Elevator"));
 case EWarehouseLayoutCategory::Zone:return false;
 case EWarehouseLayoutCategory::Charger:return Actor->IsA<AWarehouseChargingStation>();
 case EWarehouseLayoutCategory::WaitingBay:return Actor->IsA<AWarehouseWaitingBay>();
 case EWarehouseLayoutCategory::Cargo:return Actor->IsA<AWarehouseCargo>();
 case EWarehouseLayoutCategory::Pallet:return Actor->IsA<AWarehousePallet>();
 case EWarehouseLayoutCategory::All:return ZoneForActor(Actor)!=INDEX_NONE || Actor->ActorHasTag(TEXT("LayoutType:Rack")) || Actor->ActorHasTag(TEXT("LayoutType:Elevator")) ||
  Actor->ActorHasTag(TEXT("LayoutType:Other")) || Actor->IsA<AWarehouseForklift>() || Actor->IsA<AWarehouseChargingStation>() ||
  Actor->IsA<AWarehouseConveyor>() || Actor->IsA<AWarehouseWaitingBay>() || Actor->IsA<AWarehouseCargo>() || Actor->IsA<AWarehousePallet>() || Actor->IsA<AWarehouseWorker>();
 default:return Actor->ActorHasTag(TEXT("LayoutType:Other")) || Actor->IsA<AWarehouseCargo>() || Actor->IsA<AWarehousePallet>() || Actor->IsA<AWarehouseWorker>();
 }
}
bool AWarehouseLayoutEditor::ParkedAtCharger(AWarehouseForklift* Vehicle,AWarehouseChargingStation* Station) const
{
 if (!IsValid(Vehicle) || !IsValid(Station) || Vehicle->IsRemoteControlled() || Vehicle->bFloorTransfer ||
     Vehicle->GetSupportedPallet() || !Vehicle->ActiveJob.JobId.IsEmpty() || IsValid(Vehicle->ActiveJob.Pallet)) return false;
 if (IsValid(Vehicle->Elevator) && Vehicle->Elevator->ReservedVehicle==Vehicle) return false;
 if (IsValid(Vehicle->TargetPallet) && Vehicle->PalletOnForks()) return false;
 if (Vehicle->bAutonomousMode ? (Vehicle->AIState!=EWarehouseAIState::Off && Vehicle->AIState!=EWarehouseAIState::Ready && Vehicle->AIState!=EWarehouseAIState::Charging) :
     (Vehicle->State!=EWarehouseCycle::Idle && Vehicle->State!=EWarehouseCycle::Complete && Vehicle->State!=EWarehouseCycle::Charging)) return false;
 // Layout grouping tolerates native tyre settlement. Electrical docking keeps
 // its separate, stricter 1cm/0.5-degree contact requirements.
 if (FVector::Dist(Vehicle->GetActorLocation(),Station->GetActorLocation())>5.f || Vehicle->GetActorUpVector().Z<.999f ||
     FVector::DotProduct(Vehicle->GetActorForwardVector(),Station->GetActorForwardVector())<FMath::Cos(FMath::DegreesToRadians(2.f))) return false;
 const auto* Body=Cast<UPrimitiveComponent>(Vehicle->GetRootComponent());
 return Body && Body->GetPhysicsLinearVelocity().Size()<5.f && Body->GetPhysicsAngularVelocityInDegrees().Size()<2.f;
}
int32 AWarehouseLayoutEditor::ZoneForActor(AActor* Actor) const
{
 if (!Actor) return INDEX_NONE;
 for (int32 I=0;I<Zones.Num();++I)
  if (FMath::FloorToInt((Zones[I].Centre.Z+1.)/400.)==FloorIndex && Zones[I].Members.Contains(Actor)) return I;
 return INDEX_NONE;
}
int32 AWarehouseLayoutEditor::ZoneAt(FVector FloorPoint) const
{
 for (int32 I=0;I<Zones.Num();++I)
 {
  const auto& Zone=Zones[I];const FVector Local=FloorPoint-Zone.Centre;
  if (FMath::FloorToInt((Zone.Centre.Z+1.)/400.)==FloorIndex && FMath::Abs(Local.X)<=Zone.HalfSize.X && FMath::Abs(Local.Y)<=Zone.HalfSize.Y) return I;
 }
 return INDEX_NONE;
}
FBox AWarehouseLayoutEditor::GroupBounds(AActor* Actor) const
{
 if (Category==EWarehouseLayoutCategory::Zone || (Category==EWarehouseLayoutCategory::All && ZoneForActor(Actor)!=INDEX_NONE))
 {
  const int32 I=ZoneForActor(Actor);
  return I==INDEX_NONE ? FBox(ForceInit) : FBox(Zones[I].Centre-Zones[I].HalfSize,Zones[I].Centre+Zones[I].HalfSize);
 }
 FBox Bounds(ForceInit);const FName Tag=GroupOf(Actor);
 if (Tag==NAME_None) { if (Actor) Bounds=Actor->GetComponentsBoundingBox(true); }
 else for (TActorIterator<AActor> It(GetWorld());It;++It)
  if (It->ActorHasTag(Tag) && (Category!=EWarehouseLayoutCategory::All || !It->ActorHasTag(TEXT("LayoutFixed")) || It->IsA<AWarehouseChargingStation>())) Bounds+=It->GetComponentsBoundingBox(true);
 if (auto* Station=Cast<AWarehouseChargingStation>(Actor))
  for (auto* Vehicle:Station->GetLinkedVehicles()) if (ParkedAtCharger(Vehicle,Station)) Bounds+=Vehicle->GetComponentsBoundingBox(true);
 if (auto* Bay=Cast<AWarehouseWaitingBay>(Actor);Bay && IsValid(Bay->Occupant) && Bay->Occupant->IsIdleForLayout() && Bay->IsParked(Bay->Occupant))
  Bounds+=Bay->Occupant->GetComponentsBoundingBox(true);
 return Bounds;
}
AActor* AWarehouseLayoutEditor::SelectionActor(AActor* HitActor) const
{
 if (Category==EWarehouseLayoutCategory::Zone || (Category==EWarehouseLayoutCategory::All && Eligible(HitActor) && ZoneForActor(HitActor)!=INDEX_NONE))
 {
  const int32 I=ZoneForActor(HitActor);
  if (I!=INDEX_NONE) for (AActor* Member:Zones[I].Members)
   if (IsValid(Member) && (Category!=EWarehouseLayoutCategory::All || Eligible(Member))) return Member;
  return nullptr;
 }
 if (!Eligible(HitActor))
 {
  if (Category!=EWarehouseLayoutCategory::Rack || !HitActor ||
      (!HitActor->IsA<AWarehouseCargo>() && !HitActor->IsA<AWarehousePallet>())) return nullptr;
  if (FMath::FloorToInt((FMath::Max(0.,HitActor->GetActorLocation().Z)+1.)/400.)!=FloorIndex) return nullptr;
  for (TActorIterator<AActor> It(GetWorld());It;++It) if (Eligible(*It))
  {
   const FBox Box=GroupBounds(*It);
   if (Box.IsValid && Box.ExpandBy(4).IsInsideOrOn(HitActor->GetActorLocation())) { HitActor=*It;break; }
  }
  if (!Eligible(HitActor)) return nullptr;
 }
 const FName Group=GroupOf(HitActor);AActor* Representative=HitActor;
 if (Group!=NAME_None) for (TActorIterator<AActor> It(GetWorld());It;++It)
  if (It->ActorHasTag(Group) && Eligible(*It))
  {
   if ((Category==EWarehouseLayoutCategory::Elevator || Category==EWarehouseLayoutCategory::All) && IsShaft(*It)) return *It;
   if (It->GetName()<Representative->GetName()) Representative=*It;
  }
 return Representative;
}
bool AWarehouseLayoutEditor::IsSelected(AActor* Actor) const
{
 if (!Actor) return false;
 const FName Group=GroupOf(Actor);const int32 Zone=(Category==EWarehouseLayoutCategory::Zone || Category==EWarehouseLayoutCategory::All) ? ZoneForActor(Actor) : INDEX_NONE;
 return SelectedActors.ContainsByPredicate([&](AActor* Selected)
 { return IsValid(Selected) && (Selected==Actor || (Zone!=INDEX_NONE && ZoneForActor(Selected)==Zone) || (Group!=NAME_None && GroupOf(Selected)==Group)); });
}
void AWarehouseLayoutEditor::ClearSelection()
{
 if (bDragging) EndDrag(false);
 SelectedActors.Reset();SelectionCount=0;bMarqueeSelecting=false;
 if (bEditing) Status=TEXT("선택 해제 · 빈 곳을 드래그해 여러 그룹 선택 · Ctrl 클릭으로 추가/해제");
}
TArray<AActor*> AWarehouseLayoutEditor::GetSelection() const
{
 TArray<AActor*> Result;for (AActor* Actor:SelectedActors) if (IsValid(Actor)) Result.Add(Actor);return Result;
}
void AWarehouseLayoutEditor::SetSelection(const TArray<AActor*>& Actors)
{
 ClearSelection();
 for (AActor* Actor:Actors) if (AActor* Representative=SelectionActor(Actor);Representative && !IsSelected(Representative)) SelectedActors.Add(Representative);
 SelectionCount=SelectedActors.Num();
 Status=FString::Printf(TEXT("%d개 그룹 선택 · 선택한 물체 드래그 · Ctrl 클릭으로 추가/해제"),SelectionCount);
}
void AWarehouseLayoutEditor::ToggleSelection(AActor* Actor)
{
 AActor* Representative=SelectionActor(Actor);if (!Representative) return;
 if (IsSelected(Representative)) SelectedActors.RemoveAll([&](AActor* Selected) { return SelectionActor(Selected)==Representative; });
 else SelectedActors.Add(Representative);
 SelectionCount=SelectedActors.Num();
 Status=FString::Printf(TEXT("%d개 그룹 선택 · 선택한 물체 드래그 · Ctrl 클릭으로 추가/해제"),SelectionCount);
}
bool AWarehouseLayoutEditor::SelectScreenRectangle(FVector2D Start,FVector2D End,bool Additive)
{
 auto* PC=GetWorld()->GetFirstPlayerController();if (!PC || Start.ContainsNaN() || End.ContainsNaN()) return false;
 TArray<AActor*> Selection=Additive ? GetSelection() : TArray<AActor*>();
 const FBox2D Rectangle(FVector2D(FMath::Min(Start.X,End.X),FMath::Min(Start.Y,End.Y)),FVector2D(FMath::Max(Start.X,End.X),FMath::Max(Start.Y,End.Y)));
 TSet<AActor*> Seen;
 for (TActorIterator<AActor> It(GetWorld());It;++It)
 {
  // A parcel can select its rack by click; rectangle candidates are actual rack
  // parts, so unrelated loose parcels cannot accidentally select a rack.
  if (Category!=EWarehouseLayoutCategory::Zone && !Eligible(*It)) continue;
  AActor* Representative=SelectionActor(*It);if (!Representative || Seen.Contains(Representative)) continue;Seen.Add(Representative);
  const FBox Box=GroupBounds(Representative);if (!Box.IsValid) continue;
  FBox2D Projected(ForceInit);
  for (int32 Corner=0;Corner<8;++Corner)
  {
   const FVector Point(Corner&1 ? Box.Max.X : Box.Min.X,Corner&2 ? Box.Max.Y : Box.Min.Y,Corner&4 ? Box.Max.Z : Box.Min.Z);
   FVector2D Screen;if (PC->ProjectWorldLocationToScreen(Point,Screen)) Projected+=Screen;
  }
  if (Projected.bIsValid && Rectangle.Intersect(Projected)) Selection.AddUnique(Representative);
 }
 SetSelection(Selection);return SelectionCount>0;
}
bool AWarehouseLayoutEditor::BeginDrag(AActor* HitActor,FVector FloorPoint)
{
 if (!bEditing || bDragging || FloorPoint.ContainsNaN()) return false;
 if (AWarehousePlayStage::IsPlay(this) && Category==EWarehouseLayoutCategory::Zone) {Status=TEXT("플레이 모드의 입출고 위치는 고정입니다.");return false;}
 TArray<AActor*> Members;TArray<FBox> CargoAreas,ZoneAreas;
 MovingZones.Reset();ReferenceAreas.Reset();ShaftReferenceAreas.Reset();OriginalBounds=FBox(ForceInit);
 auto AddZone=[&](int32 I)
 {
  if (!Zones.IsValidIndex(I) || MovingZones.Contains(I)) return;
  const auto& Zone=Zones[I];MovingZones.Add(I);
  for (AActor* A:Zone.Members) if (IsValid(A) && (Category!=EWarehouseLayoutCategory::All || !A->ActorHasTag(TEXT("LayoutFixed")) || A->IsA<AWarehouseChargingStation>())) Members.AddUnique(A);
  const FBox Box(Zone.Centre-Zone.HalfSize,Zone.Centre+Zone.HalfSize);
  ReferenceAreas.Add(Box);CargoAreas.Add(Box);ZoneAreas.Add(Box);OriginalBounds+=Box;
 };
 if (Category==EWarehouseLayoutCategory::Zone)
 {
  const int32 HitZone=ZoneAt(FloorPoint);if (HitZone==INDEX_NONE) return false;
  TArray<int32> Wanted;
  for (AActor* Selected:SelectedActors) { const int32 I=ZoneForActor(Selected);if (I!=INDEX_NONE) Wanted.AddUnique(I); }
  if (!Wanted.Contains(HitZone)) Wanted={HitZone};
  for (int32 I:Wanted) AddZone(I);
 }
 else
 {
  HitActor=SelectionActor(HitActor);if (!HitActor) return false;
  const TArray<AActor*> Seeds=IsSelected(HitActor) ? GetSelection() : TArray<AActor*>{HitActor};
  for (AActor* Seed:Seeds)
  {
   if (!Eligible(Seed)) continue;
   if (Category==EWarehouseLayoutCategory::All)
   { const int32 I=ZoneForActor(Seed);if (I!=INDEX_NONE) { AddZone(I);continue; } }
   if (!(bAdding && AddedActors.Contains(Seed)) && ShaftBusy(Seed))
   { Status=TEXT("승강기 운행을 끝내고 적재물을 내린 뒤 재배치하세요.");return false; }
   const FName Tag=GroupOf(Seed);
   if (Tag==NAME_None) Members.AddUnique(Seed);
   else for (TActorIterator<AActor> It(GetWorld());It;++It)
    if (It->ActorHasTag(Tag) && (Category!=EWarehouseLayoutCategory::All || !It->ActorHasTag(TEXT("LayoutFixed")) || It->IsA<AWarehouseChargingStation>())) Members.AddUnique(*It);
   const FBox Box=GroupBounds(Seed);if (Box.IsValid)
   {
    OriginalBounds+=Box;
    if (!Seed->IsA<AWarehouseChargingStation>()) ReferenceAreas.Add(Box);
    if (Seed->ActorHasTag(TEXT("LayoutType:Rack"))) CargoAreas.Add(Box);
    if (IsShaft(Seed)) ShaftReferenceAreas.Add(Box);
   }
  }
 }
 // A charging bay is also the parked vehicle's start/home pose. Use the same
 // native transaction for its rig; vehicles doing work are never dragged.
 for (int32 I=0;I<Members.Num();++I) if (auto* Station=Cast<AWarehouseChargingStation>(Members[I]))
  for (auto* Vehicle:Station->GetLinkedVehicles()) if (ParkedAtCharger(Vehicle,Station))
  { Members.AddUnique(Vehicle);OriginalBounds+=Vehicle->GetComponentsBoundingBox(true); }
 for (int32 I=0;I<Members.Num();++I) if (auto* Bay=Cast<AWarehouseWaitingBay>(Members[I]);Bay && IsValid(Bay->Occupant) && Bay->Occupant->IsIdleForLayout() && Bay->IsParked(Bay->Occupant))
 { Members.AddUnique(Bay->Occupant);OriginalBounds+=Bay->Occupant->GetComponentsBoundingBox(true); }
 if (!OriginalBounds.IsValid) return false;
 OriginalSnapBounds=FBox(ForceInit);
 for (AActor* Member:Members)
 {
  if (Member->IsA<AWarehouseCargo>() || Member->IsA<AWarehousePallet>()) continue;
  const bool Frame=(Category==EWarehouseLayoutCategory::Rack || Category==EWarehouseLayoutCategory::All) && Member->ActorHasTag(TEXT("LayoutType:Rack")) ||
   (Category==EWarehouseLayoutCategory::Elevator || Category==EWarehouseLayoutCategory::All) && (IsShaft(Member) || Member->ActorHasTag(TEXT("LayoutType:Elevator")));
  if (!Frame) continue;
  TArray<UPrimitiveComponent*> Parts;Member->GetComponents(Parts);
  for (auto* Part:Parts) if (WarehousePhysics::IsPhysicalContact(Part)) OriginalSnapBounds+=Part->Bounds.GetBox();
 }
 for (AActor* Member:Members) if (!(bAdding && AddedActors.Contains(Member)) && ShaftBusy(Member))
 { Status=TEXT("승강기 운행을 끝내고 적재물을 내린 뒤 재배치하세요.");return false; }
 if (!CargoAreas.IsEmpty())
  for (TActorIterator<AActor> It(GetWorld());It;++It)
  {
   const bool ZoneObject=!It->ActorHasTag(TEXT("LayoutFixed")) &&
    ZoneAreas.ContainsByPredicate([&](const FBox& Box) { return InsideXY(Box.ExpandBy(3),It->GetActorLocation()); }) &&
    (It->IsA<AWarehouseForklift>() || It->IsA<AWarehouseWorker>() || It->ActorHasTag(TEXT("LayoutType:Other")) || It->ActorHasTag(TEXT("LayoutType:Rack")));
   if (!It->IsA<AWarehouseCargo>() && !It->IsA<AWarehousePallet>() && !ZoneObject) continue;
   const FVector P=It->GetActorLocation();
   if (CargoAreas.ContainsByPredicate([&](const FBox& Box) { return InsideXY(Box.ExpandBy(3),P); }) && P.Z>=FloorIndex*400.-2 && P.Z<(FloorIndex+1)*400.-2)
   {
    Members.AddUnique(*It);const FName Tag=GroupOf(*It);
    if (ZoneObject && Tag!=NAME_None) for (TActorIterator<AActor> Part(GetWorld());Part;++Part)
     if (Part->ActorHasTag(Tag) && (Category!=EWarehouseLayoutCategory::All || !Part->ActorHasTag(TEXT("LayoutFixed")) || Part->IsA<AWarehouseChargingStation>())) Members.AddUnique(*Part);
   }
  }
 for (int32 I=0;I<Members.Num();++I) if (auto* Vehicle=Cast<AWarehouseForklift>(Members[I]))
  if (auto* Pallet=Vehicle->GetSupportedPallet()) Members.AddUnique(Pallet);
 // A structural profile can own many floors. Never treat the entire building's
 // supported inventory as the contents of one pit/column being rearranged.
 if (auto* Strength=AWarehouseDamageSystem::Find(this))
  for (int32 I=0;I<Members.Num();++I)
   if (Members[I]->IsA<AWarehousePallet>() || Members[I]->IsA<AWarehouseCargo>() || Members[I]->ActorHasTag(TEXT("LayoutType:Rack")))
    for (AActor* A:Strength->GetSupportedActors(Members[I],I==0))
     if (IsValid(A) && (A->IsA<AWarehousePallet>() || A->IsA<AWarehouseCargo>())) Members.AddUnique(A);
 for (int32 I=0;I<Members.Num();++I)
 {
  if (auto* Cargo=Cast<AWarehouseCargo>(Members[I])) if (auto* Pallet=Cargo->GetWrappedPallet()) Members.AddUnique(Pallet);
  if (auto* Pallet=Cast<AWarehousePallet>(Members[I])) for (auto* Cargo:Pallet->GetWrappedCargo()) Members.AddUnique(Cargo);
 }
 // Move, rotation and deletion all enter this transaction. Check the expanded
 // group before disabling any collision or physics, including rack/zone contents.
 for (AActor* Member:Members) if (const auto* Conveyor=Cast<AWarehouseConveyor>(Member);Conveyor && !Conveyor->IsIdleForLayout())
 { Status=TEXT("물류가 실려 있거나 승강기 인계 중인 컨베이어는 비운 뒤 편집하세요.");return false; }
 for (AActor* Member:Members) if (HandledByPalletLift(Member))
 { Status=TEXT("화물용 엘리베이터가 운반 중인 물류는 하역을 완료한 뒤 편집하세요.");return false; }
 for (AActor* Member:Members) if (HandledByConveyorTransfer(Member))
 { Status=TEXT("컨베이어·승강기 인계 또는 회전 이송 중인 물류는 이송을 완료한 뒤 편집하세요.");return false; }
 RotationBounds=FBox(ForceInit);
 for (const FBox& Box:ZoneAreas) RotationBounds+=Box;
 for (AActor* Member:Members) if (IsValid(Member))
 {
  TArray<UPrimitiveComponent*> Parts;Member->GetComponents(Parts);
  for (const auto* Part:Parts) if (WarehousePhysics::IsPhysicalContact(Part)) RotationBounds+=Part->Bounds.GetBox();
  if (auto* Bay=Cast<AWarehouseWaitingBay>(Member))
   RotationBounds+=FBox(FVector(-Bay->SizeCm.X*.5,-Bay->SizeCm.Y*.5,0),FVector(Bay->SizeCm.X*.5,Bay->SizeCm.Y*.5,0)).TransformBy(Bay->GetActorTransform().ToMatrixWithScale());
 }
 if (!RotationBounds.IsValid) for (AActor* Member:Members) if (IsValid(Member)) RotationBounds+=Member->GetActorLocation();
 Moving.Reset();
 for (AActor* A:Members)
 {
  if (!IsValid(A)) continue;
  FMember Snapshot;Snapshot.Actor=A;Snapshot.Pose=A->GetActorTransform();Snapshot.Collision=A->GetActorEnableCollision();
  TArray<USceneComponent*> Parts;A->GetComponents(Parts);
  for (USceneComponent* C:Parts)
  {
   FPart P;P.Component=C;P.Pose=C->GetComponentTransform();P.Mobility=C->Mobility;P.Parent=C->GetAttachParent();P.Socket=C->GetAttachSocketName();
   if (auto* Joint=Cast<UPhysicsConstraintComponent>(C))
   { P.Joint=true;P.Frame1=Joint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame1);P.Frame2=Joint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame2); }
   if (auto* Primitive=Cast<UPrimitiveComponent>(C))
   {
    P.Collision=Primitive->GetCollisionEnabled();
    const auto* Own=Primitive->GetBodyInstance(NAME_None,false);
    P.Welded=Own && Own->WeldParent;
    P.Simulated=Own && !Own->WeldParent && Own->bSimulatePhysics;
    P.Linear=Primitive->GetPhysicsLinearVelocity();P.Angular=Primitive->GetPhysicsAngularVelocityInRadians();
   }
   Snapshot.Parts.Add(P);
  }
  auto Depth=[](const FPart& P) { int32 Value=0;for (auto* C=P.Component.Get();C && C->GetAttachParent();C=C->GetAttachParent()) ++Value;return Value; };
  Snapshot.Parts.Sort([&](const FPart& LeftPart,const FPart& RightPart) { return Depth(LeftPart)<Depth(RightPart); });
  Moving.Add(MoveTemp(Snapshot));
 }
 for (auto& A:Moving)
 {
  if (A.Actor.IsValid()) A.Actor->SetActorEnableCollision(false);
  for (auto& P:A.Parts) if (auto* C=P.Component.Get())
  {
   C->SetMobility(EComponentMobility::Movable);
   if (P.Simulated) if (auto* Primitive=Cast<UPrimitiveComponent>(C)) Primitive->SetSimulatePhysics(false);
  }
 }
 GrabPoint=FloorPoint;Delta=FVector::ZeroVector;Rotation=FQuat::Identity;RotationPivot=FVector::ZeroVector;bDragging=true;
 Status=FString::Printf(TEXT("%d개 요소 이동 중 · 충돌 꺼짐 · 같은 층 · 놓으면 검사"),Moving.Num());
 return true;
}
void AWarehouseLayoutEditor::UpdateDrag(FVector FloorPoint)
{
 if (!bDragging || FloorPoint.ContainsNaN()) return;
 Delta=FloorPoint-GrabPoint;Delta.Z=0;
 Delta.X=FMath::GridSnap(Delta.X,10.);Delta.Y=FMath::GridSnap(Delta.Y,10.);
 if (bEdgeSnapEnabled && (Category==EWarehouseLayoutCategory::Rack || Category==EWarehouseLayoutCategory::Elevator ||
     (Category==EWarehouseLayoutCategory::All && OriginalSnapBounds.IsValid)))
 {
  TArray<AActor*> Ignored;for (const auto& Member:Moving) if (Member.Actor.IsValid()) Ignored.Add(Member.Actor.Get());
  FString Hint;Delta=WarehouseLayoutSnap::Compute(this,OriginalSnapBounds,FloorPoint-GrabPoint,Ignored,FloorIndex,Hint);
  Status=FString::Printf(TEXT("%d개 요소 이동 중 · 충돌 꺼짐 · %s"),Moving.Num(),Hint.IsEmpty() ? TEXT("10cm 격자") : *Hint);
 }
 ApplyPreview();
}
FTransform AWarehouseLayoutEditor::EditedPose(const FTransform& Pose) const
{
 return FTransform(Rotation*Pose.GetRotation(),RotationPivot+Rotation.RotateVector(Pose.GetLocation()-RotationPivot)+Delta,Pose.GetScale3D());
}
void AWarehouseLayoutEditor::ApplyPreview()
{
 for (auto& A:Moving)
 {
  if (auto* Actor=A.Actor.Get()) Actor->SetActorTransform(EditedPose(A.Pose),false,nullptr,ETeleportType::TeleportPhysics);
  for (auto& P:A.Parts) if (auto* C=P.Component.Get()) C->SetWorldTransform(EditedPose(P.Pose),false,nullptr,ETeleportType::TeleportPhysics);
 }
}
void AWarehouseLayoutEditor::UpdateSurfaceDrag(FVector FloorPoint,AActor* Support,FVector SurfacePoint)
{
 UpdateDrag(FloorPoint);if (!bDragging) return;
 const bool CargoOnly=Moving.ContainsByPredicate([](const FMember& M) { return M.Actor.IsValid() && M.Actor->IsA<AWarehouseCargo>(); }) &&
  !Moving.ContainsByPredicate([](const FMember& M) { return M.Actor.IsValid() && !M.Actor->IsA<AWarehouseCargo>(); });
 if (!CargoOnly) return;
 FVector Target=FloorPoint;double Top=FloorIndex*400.;
 if (IsValid(Support) && (Support->IsA<AWarehousePallet>() || Support->IsA<AWarehouseCargo>()))
 {
  const FBox Box=Support->GetComponentsBoundingBox(true);
  if (!Box.IsValid || Box.Min.Z<(FloorIndex*400.-2.) || Box.Max.Z>=(FloorIndex+1)*400.-5.) return;
  const FVector Size=OriginalBounds.GetSize();
  if (Size.X>Box.GetSize().X+2 || Size.Y>Box.GetSize().Y+2)
  { Status=TEXT("상자 전체가 받침 안에 들어오지 않습니다. 더 큰 팔레트·상자 또는 바닥을 선택하세요.");return; }
  Target=Box.GetCenter();Top=Box.Max.Z;Status=TEXT("팔레트·상자 윗면 스냅 · 실제 지지와 겹침 확인 후 배치");
 }
 else if (SurfacePoint.Z<FloorIndex*400.-2 || SurfacePoint.Z>FloorIndex*400.+3) return;
 Delta.X=FMath::GridSnap(Target.X-OriginalBounds.GetCenter().X,1.);
 Delta.Y=FMath::GridSnap(Target.Y-OriginalBounds.GetCenter().Y,1.);
 Delta.Z=Top+.1-OriginalBounds.Min.Z;
 ApplyPreview();
}
bool AWarehouseLayoutEditor::BeginAdd(FVector FloorPoint)
{
 if (!bEditing || bDragging || !HasAuthority() || FloorPoint.ContainsNaN()) return false;
 if (const auto* Stage=AWarehousePlayStage::Find(this);Stage && Stage->StageIndex>0 && !Stage->CanPurchase(Category)) { Status=Stage->PurchaseDescription(Category);return false; }
 if (Category==EWarehouseLayoutCategory::All) { Status=TEXT("추가할 물체의 개별 종류를 먼저 선택하세요.");return false; }
 auto* Store=AWarehouseLayoutInventory::Find(this);Store->CapturePrototypes(this);
 TArray<AActor*> NewActors;
 if (Category==EWarehouseLayoutCategory::Cargo)
 {
  if (auto* Cargo=Store->PreviewHeldCargo(FloorPoint)) { NewActors.Add(Cargo);AddedCargoId=Cargo->CargoId; }
 }
 else
 {
  FVector SpawnPoint=FloorPoint;
  if (Category==EWarehouseLayoutCategory::Elevator) SpawnPoint.Z=0; // One shaft serves all three floors.
  NewActors=Store->SpawnPrototype(Category,AddChoice,SpawnPoint);
 }
 if (NewActors.IsEmpty()) { Status=Store->LastMessage.IsEmpty() ? TEXT("추가할 원본이 없습니다.") : Store->LastMessage;return false; }
 // Keep the new group at the current floor before the paused transaction;
 // final parcel elevation is chosen from the actual cursor hit surface.
 if (Category==EWarehouseLayoutCategory::Zone)
 {
  FWarehouseLayoutZone Preview;Preview.Name=TEXT("Preview");Preview.Centre=FloorPoint;Preview.HalfSize=Store->PreviewZoneSize(NewActors);
  for (AActor* A:NewActors) Preview.Members.Add(A);Zones.Add(Preview);
 }
 AActor* Seed=NewActors[0];
 if (Category==EWarehouseLayoutCategory::Elevator)
  for (AActor* A:NewActors) if (IsShaft(A)) { Seed=A;break; }
 // BeginPlay may be opening a fresh lift's doors while layout pauses time.
 // Only these newly spawned preview actors bypass the existing-device guard.
 bAdding=true;AddedActors.Reset();for (AActor* A:NewActors) AddedActors.Add(A);
 SetSelection({Seed});
 if (!BeginDrag(Seed,FloorPoint))
 {
  Zones.RemoveAll([&](const auto& Z) { return Z.Name==TEXT("Preview") && Z.Members.Contains(NewActors[0]); });
  for (AActor* A:NewActors) A->Destroy();bAdding=false;AddedActors.Reset();AddedCargoId=NAME_None;return false;
 }
 return true;
}
void AWarehouseLayoutEditor::RefreshAfterEdit(bool PublishTaskLocation)
{
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) It->RefreshAfterLayout(PublishTaskLocation);
 for (TActorIterator<AWarehouseComputer> It(GetWorld());It;++It) It->RefreshState();
}
AWarehouseConveyor* AWarehouseLayoutEditor::GetSelectedConveyor() const
{ return SelectedActors.Num()==1 ? Cast<AWarehouseConveyor>(SelectedActors[0]) : nullptr; }
bool AWarehouseLayoutEditor::ConveyorPlacementClear(const TArray<AWarehouseConveyor*>& Conveyors)
{
 // Reuse the exact component-shape/floor checks used by normal placement.
 // Rebuilt roller components differ from the old rig, so probe the new rig;
 // never restore stale component snapshots across a geometry rebuild.
 TArray<FMember> Previous=MoveTemp(Moving);Moving.Reset();const FBox SavedBounds=OriginalBounds;
 for (AWarehouseConveyor* Conveyor:Conveyors)
 {
  if (!IsValid(Conveyor)) continue;
  FMember Member;Member.Actor=Conveyor;Member.Collision=Conveyor->GetActorEnableCollision();
  TArray<USceneComponent*> Parts;Conveyor->GetComponents(Parts);
  for (auto* Component:Parts) { FPart Part;Part.Component=Component;Member.Parts.Add(Part); }
  Moving.Add(Member);
 }
 OriginalBounds=FBox(ForceInit);for (AWarehouseConveyor* Conveyor:Conveyors) if (IsValid(Conveyor)) OriginalBounds+=Conveyor->GetComponentsBoundingBox(true);
 const bool Clear=PlacementClear();Moving=MoveTemp(Previous);OriginalBounds=SavedBounds;return Clear;
}
bool AWarehouseLayoutEditor::ConfigureSelectedConveyor(float YawDegrees,float InclineDegrees,float BaseHeightCm,float LengthCm,float WidthCm,float SpeedCm)
{
 auto* Conveyor=GetSelectedConveyor();
 if (!bEditing || !HasAuthority() || bDragging || bMarqueeSelecting || !Conveyor || !Conveyor->IsIdleForLayout())
 { Status=TEXT("비어 있고 승강기 인계 중이 아닌 컨베이어 한 개를 선택하세요.");return false; }
 const FTransform OldPose=Conveyor->GetActorTransform();
 const float OldLength=Conveyor->LengthCm,OldWidth=Conveyor->WidthCm,OldHeight=Conveyor->SurfaceHeightCm,OldSpeed=Conveyor->SpeedCm;
 if (AWarehousePlayStage::IsPlay(this) && (!FMath::IsNearlyEqual(LengthCm,OldLength) || !FMath::IsNearlyEqual(WidthCm,OldWidth) || !FMath::IsNearlyEqual(SpeedCm,OldSpeed)))
 {Status=TEXT("플레이 모드는 구매한 컨베이어의 크기·속도를 유지합니다. 추가 모듈을 구매해 연결하세요.");return false;}
 const bool Applied=Conveyor->ConfigureGeometry(LengthCm,WidthCm,OldHeight,SpeedCm) &&
  Conveyor->SetConveyorPose(YawDegrees,InclineDegrees,BaseHeightCm);
 if (!Applied || !ConveyorPlacementClear({Conveyor}))
 {
  const FString Failure=Applied ? Status : Conveyor->Status;
  Conveyor->SetActorTransform(OldPose,false,nullptr,ETeleportType::TeleportPhysics);
  Conveyor->LengthCm=OldLength;Conveyor->WidthCm=OldWidth;Conveyor->SurfaceHeightCm=OldHeight;Conveyor->SpeedCm=OldSpeed;Conveyor->RebuildConveyor();
  Status=TEXT("컨베이어 변경 취소 · ")+Failure;return false;
 }
 Conveyor->Tags.RemoveAll([](FName Tag) { return Tag.ToString().StartsWith(TEXT("WarehouseFloor")); });
 Conveyor->Tags.Add(FName(*FString::Printf(TEXT("WarehouseFloor%d"),FMath::Clamp(FMath::FloorToInt((Conveyor->GetActorLocation().Z+1)/400.),0,3))));
 NotifyStructureChanged();Status=TEXT("컨베이어 각도·경사·높이·크기 적용 · 고정 지도 갱신");return true;
}
bool AWarehouseLayoutEditor::CanConnectSelectedConveyors() const
{
 return bEditing && HasAuthority() && !bDragging && !bMarqueeSelecting && Action!=EWarehouseLayoutAction::Add && SelectedActors.Num()==2 &&
  SelectedActors.ContainsByPredicate([](AActor* A) { return A && A->IsA<AWarehouseConveyor>(); }) &&
  !SelectedActors.ContainsByPredicate([](AActor* A) { const auto* Conveyor=Cast<AWarehouseConveyor>(A);return !Conveyor || !Conveyor->IsIdleForLayout(); });
}
bool AWarehouseLayoutEditor::ConnectSelectedConveyors()
{
 if (!CanConnectSelectedConveyors()) { Status=TEXT("비어 있는 컨베이어 두 개를 Ctrl로 순서대로 선택하세요. 첫 번째 출구에 두 번째 입구를 연결합니다.");return false; }
 auto* Previous=Cast<AWarehouseConveyor>(SelectedActors[0]);auto* Next=Cast<AWarehouseConveyor>(SelectedActors[1]);
 const FTransform OldPose=Next->GetActorTransform();auto* OldConnection=Previous->NextConveyor.Get();
 if (!Next->SnapEntranceTo(Previous)) { Status=Next->Status;return false; }
 if (!ConveyorPlacementClear({Next}))
 {
  const FString Failure=Status;Previous->NextConveyor=OldConnection;Next->SetActorTransform(OldPose,false,nullptr,ETeleportType::TeleportPhysics);Next->RebuildConveyor();
  Status=TEXT("컨베이어 연결 취소 · ")+Failure;return false;
 }
 Next->Tags.RemoveAll([](FName Tag) { return Tag.ToString().StartsWith(TEXT("WarehouseFloor")); });
 Next->Tags.Add(FName(*FString::Printf(TEXT("WarehouseFloor%d"),FMath::Clamp(FMath::FloorToInt((Next->GetActorLocation().Z+1)/400.),0,3))));
 NotifyStructureChanged();Status=TEXT("첫 컨베이어 출구 → 두 번째 입구 연결 · 입출구 높이·물리 충돌 검사 완료");return true;
}
bool AWarehouseLayoutEditor::SetSelectedConveyorTurnDirection(bool TurnRight)
{
 auto* Conveyor=GetSelectedConveyor();
 if (!bEditing || !HasAuthority() || bDragging || bMarqueeSelecting || !Conveyor || Conveyor->Kind!=EWarehouseConveyorKind::RotaryTransfer || !Conveyor->IsIdleForLayout())
 { Status=TEXT("비어 있는 90° 회전 이송대 한 개를 선택하세요.");return false; }
 const bool Previous=Conveyor->bTurnRight;
 if (!Conveyor->SetTurnDirection(TurnRight)) { Status=Conveyor->Status;return false; }
 if (!ConveyorPlacementClear({Conveyor}))
 { const FString Failure=Status;Conveyor->SetTurnDirection(Previous);Status=TEXT("회전 방향 변경 취소 · ")+Failure;return false; }
 NotifyStructureChanged();Status=TurnRight ? TEXT("회전 이송대 · 우측 90° 출구로 변경") : TEXT("회전 이송대 · 좌측 90° 출구로 변경");return true;
}
bool AWarehouseLayoutEditor::LinkSelectedConveyorToNearestLift(bool Infeed,int32 DestinationFloor)
{
 const auto* Conveyor=GetSelectedConveyor();if (!Conveyor) { Status=TEXT("승강기에 연결할 컨베이어 한 개를 선택하세요.");return false; }
 AWarehousePalletLift* Nearest=nullptr;double Distance=TNumericLimits<double>::Max();
 for (TActorIterator<AWarehousePalletLift> It(GetWorld());It;++It)
 {
  const double Candidate=FVector::DistSquared2D(Conveyor->GetActorLocation(),It->GetActorLocation());
  if (Candidate<Distance) { Distance=Candidate;Nearest=*It; }
 }
 if (!Nearest) { Status=TEXT("화물용 승강기가 없습니다. 추가 메뉴에서 먼저 배치하세요.");return false; }
 return LinkSelectedConveyorToLift(Nearest,Infeed,DestinationFloor);
}
bool AWarehouseLayoutEditor::LinkSelectedConveyorToLift(AWarehousePalletLift* Lift,bool Infeed,int32 DestinationFloor)
{
 auto* Conveyor=GetSelectedConveyor();
 if (!bEditing || !HasAuthority() || bDragging || bMarqueeSelecting || !Conveyor || !Conveyor->IsIdleForLayout())
 { Status=TEXT("빈 컨베이어 한 개를 선택하고 진행 중인 이송을 끝내세요.");return false; }
 if (!IsValid(Lift) || Lift->GetWorld()!=GetWorld() || !Lift->IsIdleForLayout())
 { Status=TEXT("연결할 화물용 승강기가 없거나 운반·예약 중입니다.");return false; }
 if (Conveyor->Kind!=EWarehouseConveyorKind::PalletRoller)
 { Status=TEXT("화물용 승강기에는 팔레트 롤러 컨베이어만 연결할 수 있습니다.");return false; }
 if (FloorIndex>=Lift->FloorCount || DestinationFloor<0 || DestinationFloor>=Lift->FloorCount || DestinationFloor==FloorIndex)
 { Status=FString::Printf(TEXT("현재 %d층과 다른 목적층(1~%d층)을 선택하세요. 승강기가 두 층을 모두 지원해야 합니다."),FloorIndex+1,Lift->FloorCount);return false; }
 if (FMath::Abs(Lift->GetActorRotation().Pitch)>.01 || FMath::Abs(Lift->GetActorRotation().Roll)>.01 || !Lift->GetActorScale3D().Equals(FVector::OneVector,.001))
 { Status=TEXT("승강기 기준축은 수평·배율 1이어야 합니다.");return false; }
 for (TActorIterator<AWarehouseConveyor> It(GetWorld());It;++It)
  if (*It!=Conveyor && It->Lift==Lift && It->LiftFloor==FloorIndex)
  { Status=TEXT("이 층의 승강기 포트에는 이미 다른 컨베이어가 연결되어 있습니다.");return false; }
 const FTransform OldPose=Conveyor->GetActorTransform();const float OldSurface=Conveyor->SurfaceHeightCm;
 auto* OldLift=Conveyor->Lift.Get();auto* OldNext=Conveyor->NextConveyor.Get();
 const int32 OldFloor=Conveyor->LiftFloor,OldDestination=Conveyor->DestinationFloor;const bool OldInfeed=Conveyor->bLiftInfeed;
 const bool PreviousPowered=Lift->bPoweredConveyor;
 auto Rollback=[&]()
 {
  Conveyor->Lift=OldLift;Conveyor->NextConveyor=OldNext;Conveyor->LiftFloor=OldFloor;Conveyor->DestinationFloor=OldDestination;Conveyor->bLiftInfeed=OldInfeed;
  Conveyor->SurfaceHeightCm=OldSurface;Conveyor->SetActorTransform(OldPose,false,nullptr,ETeleportType::TeleportPhysics);Conveyor->RebuildConveyor();
 };
 // Both ports face the carrier. A receiver runs its real rollers backwards.
 const FQuat LiftRotation=Lift->GetActorQuat();
 const FVector Port=Lift->GetActorTransform().TransformPosition(FVector(-65.5,0,FloorIndex*400.f+Lift->ConveyorHeightCm));
 const FVector Position=Port-LiftRotation.RotateVector(FVector(Conveyor->LengthCm*.5f,0,Lift->ConveyorHeightCm));
 Conveyor->Lift=Lift;Conveyor->LiftFloor=FloorIndex;Conveyor->DestinationFloor=DestinationFloor;Conveyor->bLiftInfeed=Infeed;
 const bool Placed=Conveyor->ConfigureGeometry(Conveyor->LengthCm,Conveyor->WidthCm,Lift->ConveyorHeightCm,Conveyor->SpeedCm) &&
  Conveyor->SetConveyorPose(Lift->GetActorRotation().Yaw,0,Position.Z);
 if (Placed)
 {
  // The public pose API controls height/angle; the port determines its XY.
  Conveyor->SetActorLocation(Position,false,nullptr,ETeleportType::TeleportPhysics);
 }
 const bool Rebuilt=Placed && Conveyor->RebuildConveyor();
 if (!Rebuilt)
 { const FString Failure=Conveyor->Status;Rollback();Status=TEXT("승강기 연결 취소 · ")+Failure;return false; }
 // Validate against the real powered-frame recess, rather than the manual
 // landing sill. Revert both empty machine rigs if placement is rejected.
 if (!PreviousPowered && !Lift->SetPoweredConveyor(true))
 { const FString Failure=Lift->Status;Rollback();Status=TEXT("승강기 구동 롤러를 준비할 수 없습니다 · ")+Failure;return false; }
 if (!ConveyorPlacementClear({Conveyor}))
 {
  const FString Failure=Status;Rollback();
  const bool ModeRestored=PreviousPowered || Lift->SetPoweredConveyor(false);
  Status=TEXT("승강기 연결 취소 · ")+Failure;
  if (!ModeRestored) Status+=TEXT(" · 선로는 복구했지만 승강기 구동 모드 복구를 완료하지 못했습니다: ")+Lift->Status;
  return false;
 }
 Conveyor->NextConveyor=nullptr;
 Conveyor->Tags.RemoveAll([](FName Tag) { return Tag.ToString().StartsWith(TEXT("WarehouseFloor")); });
 Conveyor->Tags.Add(FName(*FString::Printf(TEXT("WarehouseFloor%d"),FloorIndex)));
 NotifyStructureChanged();Status=FString::Printf(TEXT("%s · %d층 %s 포트 연결 · 목적층 %d층"),*Lift->GetActorNameOrLabel(),FloorIndex+1,Infeed ? TEXT("공급") : TEXT("수신"),DestinationFloor+1);return true;
}
bool AWarehouseLayoutEditor::DisconnectSelectedConveyorLift()
{
 auto* Conveyor=GetSelectedConveyor();
 if (!bEditing || !HasAuthority() || bDragging || bMarqueeSelecting || !Conveyor || !Conveyor->IsIdleForLayout())
 { Status=TEXT("물류·승강기 인계를 끝낸 빈 컨베이어만 연결 해제할 수 있습니다.");return false; }
 Conveyor->Lift=nullptr;Conveyor->bLiftInfeed=true;Conveyor->LiftFloor=FloorIndex;Conveyor->DestinationFloor=FloorIndex==0 ? 1 : 0;
 NotifyStructureChanged();Status=TEXT("승강기 연결 해제 · 컨베이어 위치와 구동 롤러는 유지");return true;
}
bool AWarehouseLayoutEditor::CanRotateSelection() const
{
 return bEditing && HasAuthority() && !bDragging && !bMarqueeSelecting && Action!=EWarehouseLayoutAction::Add &&
  !SelectedActors.IsEmpty() && SelectedActors.ContainsByPredicate([this](AActor* Actor) { return IsValid(Actor) && SelectionActor(Actor); });
}
bool AWarehouseLayoutEditor::RotateSelection(float DeltaYawDegrees)
{
 if (!CanRotateSelection() || !FMath::IsFinite(DeltaYawDegrees) || !FMath::IsNearlyEqual(FMath::Abs(DeltaYawDegrees),90.f,.001f)) return false;
 AActor* Seed=GetSelection()[0];FVector Point=Seed->GetActorLocation();
 if (Category==EWarehouseLayoutCategory::Zone)
 { const int32 I=ZoneForActor(Seed);if (I==INDEX_NONE) return false;Point=Zones[I].Centre; }
 Point.Z=FloorIndex*400.;
 if (!BeginDrag(Seed,Point)) return false;
 Rotation=FQuat(FVector::UpVector,FMath::DegreesToRadians(DeltaYawDegrees));RotationPivot=RotationBounds.GetCenter();
 ApplyPreview();
 const bool Accepted=EndDrag(true);
 if (Accepted) Status=FString::Printf(TEXT("선택 %d개 그룹 %s90° 회전 · 충돌·작업 위치 갱신"),SelectionCount,DeltaYawDegrees>0 ? TEXT("+") : TEXT("−"));
 return Accepted;
}
bool AWarehouseLayoutEditor::DeleteSelection()
{
 if (!bEditing || !HasAuthority() || bDragging || SelectedActors.IsEmpty()) return false;
 AActor* Seed=SelectedActors[0];if (!IsValid(Seed)) return false;
 FVector Point=Seed->GetActorLocation();Point.Z=FloorIndex*400.;
 if (!BeginDrag(Seed,Point)) return false;
 TArray<AActor*> Members;for (const auto& M:Moving) if (M.Actor.IsValid()) Members.Add(M.Actor.Get());
 // Parked vehicles follow their home during a MOVE, but deleting the charger
 // or parking marker must leave those independent vehicles in the world.
 if (Category==EWarehouseLayoutCategory::Charger || Category==EWarehouseLayoutCategory::WaitingBay)
  Members.RemoveAll([](AActor* A) { return A->IsA<AWarehouseForklift>(); });
 else if (Category==EWarehouseLayoutCategory::All)
 {
  TSet<AActor*> HomeVehicles;
  for (AActor* Selected:SelectedActors)
  {
   if (auto* Station=Cast<AWarehouseChargingStation>(Selected))
    for (auto* Vehicle:Station->GetLinkedVehicles()) if (ParkedAtCharger(Vehicle,Station)) HomeVehicles.Add(Vehicle);
   if (auto* Bay=Cast<AWarehouseWaitingBay>(Selected);Bay && IsValid(Bay->Occupant) && Bay->Occupant->IsIdleForLayout() && Bay->IsParked(Bay->Occupant)) HomeVehicles.Add(Bay->Occupant);
  }
  Members.RemoveAll([&](AActor* Actor)
  {
   return HomeVehicles.Contains(Actor) && !SelectedActors.Contains(Actor);
  });
 }
 auto* Store=AWarehouseLayoutInventory::Find(this);const int32 HeldBefore=Store->HeldCargo.Num();
 bool Allowed=true;
 for (AActor* A:Members)
 {
  if (AWarehousePlayStage::IsPlay(this) && (A->IsA<AWarehouseCargo>() || A->IsA<AWarehousePallet>())) {Status=TEXT("물류가 담긴 설비는 먼저 화물을 옮긴 뒤 철거하세요.");Allowed=false;break;}
  if (auto* V=Cast<AWarehouseForklift>(A);V && !V->IsIdleForLayout()) { Status=TEXT("운반·원격 조작 중인 차량은 작업이 끝난 뒤 삭제하세요.");Allowed=false;break; }
  for (TActorIterator<Amsc_vrCharacter> It(GetWorld());It;++It) if (It->GetHeldCargo()==A) { Status=TEXT("플레이어가 들고 있는 물체는 내려놓은 뒤 삭제하세요.");Allowed=false;break; }
  if (!Allowed) break;
  if (A->IsA<AWarehouseCargo>() || A->IsA<AWarehousePallet>())
   for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
    if (It->ActiveJob.Pallet==A || It->GetPhysicalLoads().Contains(A)) { Status=TEXT("운반 중인 팔레트·상자는 작업이 끝난 뒤 삭제하세요.");Allowed=false;break; }
 }
 if (Allowed) for (AActor* A:Members) if (auto* Cargo=Cast<AWarehouseCargo>(A))
  if (!Store->HoldCargo(Cargo)) { Status=Store->LastMessage;Allowed=false;break; }
 Restore(true);Moving.Reset();bDragging=false;
 if (!Allowed) { Store->HeldCargo.SetNum(HeldBefore);++Store->Revision;return false; }
 if (Members.ContainsByPredicate([](AActor* A) { return IsShaft(A); }) && !WarehouseElevatorLayout::Rebuild(this,false,Members))
 { Store->HeldCargo.SetNum(HeldBefore);++Store->Revision;Status=TEXT("승강기 삭제 후 층별 바닥을 복원할 수 없어 삭제를 취소했습니다.");return false; }
 const bool ConveyorsOnly=!Members.IsEmpty() && !Members.ContainsByPredicate([](AActor* A) { return !A || !A->IsA<AWarehouseConveyor>(); });
 Store->ForgetActors(Members);for (AActor* A:Members) A->Destroy();
 ClearSelection();RefreshAfterEdit(!ConveyorsOnly);Status=FString::Printf(TEXT("%d개 요소 삭제 · 상자 %d개를 보류된 물류에 보관"),Members.Num(),Store->HeldCargo.Num()-HeldBefore);return true;
}
bool AWarehouseLayoutEditor::PlacementClear()
{
 if (const auto* Stage=AWarehousePlayStage::Find(this);Stage && Stage->StageIndex>0)
  for (const auto& M:Moving) if (M.Actor.IsValid())
  {
   FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(M.Actor.Get());
   if (!Bounds.IsValid) Bounds=M.Actor->GetComponentsBoundingBox(true);
   if (!Stage->Contains(Bounds)) { Status=TEXT("고정된 스테이지 건물 안에 설비 전체가 들어와야 합니다.");return false; }
  }
 FComponentQueryParams Params(SCENE_QUERY_STAT(LayoutPlacement));
 TArray<AActor*> Ignored;
 for (auto& A:Moving) if (A.Actor.IsValid()) { Params.AddIgnoredActor(A.Actor.Get());Ignored.Add(A.Actor.Get()); }
 for (auto& A:Moving) if (A.Actor.IsValid()) A.Actor->SetActorEnableCollision(A.Collision);
 for (const auto& M:Moving) if (auto* Bay=Cast<AWarehouseWaitingBay>(M.Actor.Get()))
 {
  FCollisionQueryParams ParkingQuery(SCENE_QUERY_STAT(LayoutParkingSpace));
  for (const auto& Other:Moving) if (Other.Actor.IsValid()) ParkingQuery.AddIgnoredActor(Other.Actor.Get());
  const FVector Centre=Bay->GetActorLocation()+FVector(0,0,110);
  if (GetWorld()->OverlapBlockingTestByChannel(Centre,Bay->GetActorQuat(),ECC_Visibility,FCollisionShape::MakeBox(FVector(Bay->SizeCm.X*.5,Bay->SizeCm.Y*.5,100)),ParkingQuery))
  { Status=TEXT("대기 자리의 차량 공간이 다른 물체와 겹칩니다. 배치를 취소했습니다.");return false; }
 }
 for (auto& A:Moving) for (auto& P:A.Parts) if (auto* C=Cast<UPrimitiveComponent>(P.Component.Get());C && WarehousePhysics::IsPhysicalContact(C))
 {
  TArray<FOverlapResult> Overlaps;
  // Use this component's body instance: the world wrapper resolves a welded
  // child to its parent body and can then query it at the child's transform.
  C->ComponentOverlapMulti(Overlaps,GetWorld(),C->GetComponentLocation(),C->GetComponentQuat(),ECC_Visibility,Params);
  for (const auto& O:Overlaps) if (O.bBlockingHit && O.GetComponent())
  {
   const FBox Other=O.GetComponent()->Bounds.GetBox();const FBox Own=C->Bounds.GetBox();
   const FVector Depth(FMath::Min(Own.Max.X,Other.Max.X)-FMath::Max(Own.Min.X,Other.Min.X),
    FMath::Min(Own.Max.Y,Other.Max.Y)-FMath::Max(Own.Min.Y,Other.Min.Y),
    FMath::Min(Own.Max.Z,Other.Max.Z)-FMath::Max(Own.Min.Z,Other.Min.Z));
   // Native overlap includes face contact. A fitted pit/base may touch the
   // surrounding slab without occupying any of its volume.
   if (Depth.X<=.05 || Depth.Y<=.05 || Depth.Z<=.05) continue;
   if (IsShaft(A.Actor.Get()) && O.GetActor()->ActorHasTag(TEXT("ElevatorRuntimeFloor")) && OnlyBoxSurfaceContact(C,O.GetComponent())) continue;
   // Contact with the supporting floor is valid; penetration into its side or
   // into an upper deck is not. Other geometry uses native component shapes.
   // A spun tyre's render bounds extend below its actual round tread. Reuse
   // the native tyre/surface test; never exempt a wheel just by its bounds.
   if (auto* Vehicle=Cast<AWarehouseForklift>(A.Actor.Get());Vehicle && Vehicle->Wheels.Contains(Cast<UStaticMeshComponent>(C)))
   {
    if (Vehicle->WheelBearingOnFloor(Cast<UStaticMeshComponent>(C),O.GetComponent(),C->GetComponentTransform(),Ignored)) continue;
   }
   else if (Own.Min.Z>=Other.Max.Z-2.f && O.GetComponent()->GetUpVector().Z>.95f) continue;
   Status=FString::Printf(TEXT("다른 물체와 겹침: %s / %s → %s. 원래 위치로 되돌렸습니다."),*A.Actor->GetActorNameOrLabel(),*C->GetName(),*O.GetActor()->GetActorNameOrLabel());return false;
  }
 }
 // At least a real floor beneath each object touching this floor is required.
 for (const auto& M:Moving) if (auto* Cargo=Cast<AWarehouseCargo>(M.Actor.Get()))
 {
  const FBox Box=Cargo->GetComponentsBoundingBox(true);if (!Box.IsValid) return false;
  int32 SupportedPoints=0;
  for (double X:{Box.Min.X+Box.GetSize().X*.2,Box.GetCenter().X,Box.Max.X-Box.GetSize().X*.2})
   for (double Y:{Box.Min.Y+Box.GetSize().Y*.2,Box.GetCenter().Y,Box.Max.Y-Box.GetSize().Y*.2})
   {
    FCollisionQueryParams SupportQuery(SCENE_QUERY_STAT(LayoutCargoSupport));SupportQuery.AddIgnoredActor(Cargo);
    FHitResult Hit;const FVector Point(X,Y,Box.Min.Z+1);
    if (GetWorld()->LineTraceSingleByChannel(Hit,Point,Point-FVector(0,0,5),ECC_Visibility,SupportQuery) && Hit.ImpactNormal.Z>=.95f) ++SupportedPoints;
   }
  if (SupportedPoints<3) { Status=TEXT("상자 아래의 수평 받침이 부족합니다. 바닥·팔레트·상자 위에 스냅해 배치하세요.");return false; }
 }
 TSet<FName> ElevatorGroups;
 for (const auto& M:Moving) if (IsShaft(M.Actor.Get()) && GroupOf(M.Actor.Get())!=NAME_None) ElevatorGroups.Add(GroupOf(M.Actor.Get()));
 for (auto& A:Moving) if (auto* Actor=A.Actor.Get())
 {
  // Only the shaft and its own group use the all-floor opening validation.
  // A mixed selection's other objects still need their ordinary floor support.
   if (IsShaft(Actor) || ElevatorGroups.Contains(GroupOf(Actor))) continue;
  // Truck walls, doors and cargo floors are supported by their chassis. Only
  // the chassis touches the yard; tracing beneath every internal panel would
  // reject a valid truck move because its own grouped floor is ignored.
  // Logical anchors and editor sprites move with their group but do not need
  // physical support. Use the same contact classification as overlap checks.
  FBox B(ForceInit);
  for (const auto& Part:A.Parts)
   if (auto* C=Cast<UPrimitiveComponent>(Part.Component.Get());WarehousePhysics::IsPhysicalContact(C)) B+=C->Bounds.GetBox();
  if (GroupOf(Actor).ToString().StartsWith(TEXT("LayoutGroup:TruckDock")) &&
      B.IsValid && B.Min.Z>OriginalBounds.Min.Z+2.) continue;
  if (!B.IsValid || B.Min.Z>FloorIndex*400.+8 || B.Max.Z<FloorIndex*400.) continue;
  for (float X:{B.Min.X+2,B.Max.X-2}) for (float Y:{B.Min.Y+2,B.Max.Y-2})
  {
   FHitResult Hit;FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(LayoutFloor));
   for (auto& M:Moving) if (M.Actor.IsValid()) FloorParams.AddIgnoredActor(M.Actor.Get());
   const double SupportHeight=FMath::Min(FloorIndex*400.,FMath::GridSnap(B.Min.Z,10.));
   const FVector Point(X,Y,SupportHeight+3);
   if (!GetWorld()->LineTraceSingleByChannel(Hit,Point,Point-FVector(0,0,12),ECC_Visibility,FloorParams) || Hit.ImpactNormal.Z<.9f)
   {
    UE_LOG(LogTemp,Warning,TEXT("LAYOUT_FLOOR_REJECT actor=%s point=%s bounds=%s hit=%s normal=%s floor=%d"),
     *Actor->GetActorNameOrLabel(),*Point.ToString(),*B.ToString(),*GetNameSafe(Hit.GetComponent()),*Hit.ImpactNormal.ToString(),FloorIndex);
    Status=TEXT("바닥 밖이거나 개구부입니다. 원래 위치로 되돌렸습니다.");return false;
   }
  }
 }
 return true;
}
void AWarehouseLayoutEditor::Restore(bool Rollback)
{
 if (Rollback)
 {
  // Restore exact saved poses; running the edge snap at zero delta would move
  // cancelled selections towards nearby walls/racks instead of rolling back.
  Delta=FVector::ZeroVector;
  for (auto& A:Moving)
  {
   if (auto* Actor=A.Actor.Get()) Actor->SetActorTransform(A.Pose,false,nullptr,ETeleportType::TeleportPhysics);
   for (auto& P:A.Parts) if (auto* C=P.Component.Get()) C->SetWorldTransform(P.Pose,false,nullptr,ETeleportType::TeleportPhysics);
  }
 }
 for (auto& A:Moving) for (auto& P:A.Parts) if (auto* C=P.Component.Get())
 {
  if (auto* Primitive=Cast<UPrimitiveComponent>(C))
  {
   if (P.Simulated) { Primitive->SetSimulatePhysics(true);Primitive->SetPhysicsLinearVelocity(Rollback ? P.Linear : Rotation.RotateVector(P.Linear));Primitive->SetPhysicsAngularVelocityInRadians(Rollback ? P.Angular : Rotation.RotateVector(P.Angular)); }
  }
  C->SetMobility(P.Mobility);
 }
 for (auto& A:Moving) if (A.Actor.IsValid())
 {
  A.Actor->SetActorEnableCollision(A.Collision);
  for (auto& P:A.Parts) if (auto* C=P.Component.Get();C && P.Parent.IsValid() && !P.Simulated)
   C->AttachToComponent(P.Parent.Get(),FAttachmentTransformRules(EAttachmentRule::KeepWorld,P.Welded),P.Socket);
  for (auto& P:A.Parts) if (P.Joint) if (auto* Joint=Cast<UPhysicsConstraintComponent>(P.Component.Get()))
  {
   Joint->InitComponentConstraint();
   // InitComponentConstraint recomputes frames, but our mechanical joints have
   // explicit body-local reference positions. Restore those calibrated frames.
   Joint->SetConstraintReferenceFrame(EConstraintFrame::Frame1,P.Frame1);
   Joint->SetConstraintReferenceFrame(EConstraintFrame::Frame2,P.Frame2);
  }
 }
}
void AWarehouseLayoutEditor::UpdateReferences()
{
 TSet<AActor*> Members;for (const auto& A:Moving) if (A.Actor.IsValid()) Members.Add(A.Actor.Get());
 TSet<FTransform*> MovedDockAnchors;
 TSet<AWarehouseForklift*> RelocatedChargerVehicles;
 auto MovePoint=[this](FVector Point) { return RotationPivot+Rotation.RotateVector(Point-RotationPivot)+Delta; };
 auto MovePose=[this](FTransform& Pose)
 {
  const FVector Point=Pose.GetLocation();
  const bool CurrentFloor=FMath::FloorToInt((FMath::Max(0.,Point.Z)+1.)/400.)==FloorIndex;
  const bool ShaftFloor=ShaftReferenceAreas.ContainsByPredicate([&](const FBox& Box) { return InsideXY(Box.ExpandBy(4),Point); });
  if ((CurrentFloor || ShaftFloor) && ReferenceAreas.ContainsByPredicate([&](const FBox& Box) { return InsideXY(Box.ExpandBy(4),Point); })) Pose=EditedPose(Pose);
 };
 for (TActorIterator<AWarehouseInboundZone> It(GetWorld());It;++It)
  for (auto& Slot:It->RackSlots) if (Members.Contains(Slot.Rack.Get())) Slot.Pose=EditedPose(Slot.Pose);
 for (int32 ZoneIndex:MovingZones)
 {
  Zones[ZoneIndex].Centre=MovePoint(Zones[ZoneIndex].Centre);
  Zones[ZoneIndex].HalfSize=Rotation.RotateVector(Zones[ZoneIndex].HalfSize).GetAbs();
  for (auto& Pose:Zones[ZoneIndex].Destinations) Pose=EditedPose(Pose);
 }
 if (!MovingZones.IsEmpty()) for (TActorIterator<AWarehouseComputer> It(GetWorld());It;++It) It->RefreshLayoutDestinations();
 for (const auto& M:Moving) if (auto* Bay=Cast<AWarehouseWaitingBay>(M.Actor.Get()))
  for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) if (It->WaitingBay==Bay) It->IdlePose=Bay->GetActorTransform();
 for (const auto& Member:Moving) if (auto* Station=Cast<AWarehouseChargingStation>(Member.Actor.Get()))
  for (auto* Vehicle:Station->GetLinkedVehicles())
  {
   RelocatedChargerVehicles.Add(Vehicle);
   const FVector OldDock=Member.Pose.GetLocation();
   auto MoveDockReference=[&](FVector& Point) { if (FVector::Dist(Point,OldDock)<=5.f) Point=MovePoint(Point); };
   MoveDockReference(Vehicle->StartLocation);MoveDockReference(Vehicle->ReturnLocation);
   if (Members.Contains(Vehicle))
   { Vehicle->StartLocation=Vehicle->GetActorLocation();Vehicle->ReturnLocation=Vehicle->GetActorLocation(); }
   Vehicle->ChargeExitPose=Station->GetActorTransform();
   Vehicle->ChargeExitPose.AddToTranslation(Station->GetActorForwardVector()*120.f);
   const FVector OldExit=OldDock+Member.Pose.GetRotation().GetForwardVector()*120.f;
   for (auto& Pose:Vehicle->NavigationAnchors)
    if (!MovedDockAnchors.Contains(&Pose) && (FVector::Dist(Pose.GetLocation(),OldDock)<=5.f || FVector::Dist(Pose.GetLocation(),OldExit)<=5.f))
    { Pose=EditedPose(Pose);MovedDockAnchors.Add(&Pose); }
  }
 // Elevator services are local shaft poses on all three floors; include the
 // waiting point outside the shaft bounds, and update each shared anchor once.
 for (const auto& Member:Moving) if (Member.Actor.IsValid() && Member.Actor->IsA<AWarehouseElevator>())
  for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
   for (auto& Pose:It->NavigationAnchors) if (!MovedDockAnchors.Contains(&Pose))
    for (int32 Floor=0;Floor<4;++Floor)
     if (FVector::Dist(Pose.GetLocation(),Member.Pose.TransformPosition(FVector(0,0,Floor*400.)))<=5.f ||
         FVector::Dist(Pose.GetLocation(),Member.Pose.TransformPosition(FVector(-500,0,Floor*400.)))<=5.f)
     { Pose=EditedPose(Pose);MovedDockAnchors.Add(&Pose);break; }
 const bool ChargerOnly=ReferenceAreas.IsEmpty() && Moving.ContainsByPredicate([](const FMember& M) { return M.Actor.IsValid() && M.Actor->IsA<AWarehouseChargingStation>(); });
 bool ConveyorsOnly=!Members.IsEmpty();for (AActor* Member:Members) if (!Member || !Member->IsA<AWarehouseConveyor>()) { ConveyorsOnly=false;break; }
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
 {
  if (!ChargerOnly)
  {
   MovePose(It->ActiveJob.Destination);for (auto& Job:It->PendingJobs) MovePose(Job.Destination);
   for (auto& Pose:It->NavigationAnchors) if (!MovedDockAnchors.Contains(&Pose)) MovePose(Pose);
  }
  if (ChargerOnly)
  {
   // Moving a cabinet must not promote an unfinished lift to delivery. Only a
   // charging trip needs a new route; ongoing work keeps its physical phase.
   It->LastObstacleCheckSeconds=-1;It->ObstacleCheckFrame=MAX_uint64;
   It->InvalidateSensorSnapshot(TEXT("CHARGER LAYOUT UPDATED"));
   const bool ChargingPhase=It->AIState>=EWarehouseAIState::PlanCharge && It->AIState<=EWarehouseAIState::LeaveCharger;
   if (RelocatedChargerVehicles.Contains(*It) && (ChargingPhase || (It->bChargePending && It->ActiveJob.JobId.IsEmpty()))) It->RefreshAfterLayout();
  }
  else It->RefreshAfterLayout(!ConveyorsOnly);
 }
 for (TActorIterator<AWarehouseComputer> It(GetWorld());It;++It) It->RefreshState();
}
bool AWarehouseLayoutEditor::EndDrag(bool Commit)
{
 if (!bDragging) return false;
 const bool HasShaft=Moving.ContainsByPredicate([](const FMember& M) { return IsShaft(M.Actor.Get()); });
 const bool OpeningReady=!HasShaft || !Commit || WarehouseElevatorLayout::Rebuild(this);
 const bool QueueUnchanged=!bAdding || AddedCargoId.IsNone() || (!AWarehouseLayoutInventory::Find(this)->HeldCargo.IsEmpty() && AWarehouseLayoutInventory::Find(this)->HeldCargo[0].CargoId==AddedCargoId);
 auto* PlayStage=AWarehousePlayStage::Find(this);
 const bool Affordable=!bAdding || !PlayStage || PlayStage->StageIndex==0 || PlayStage->CanPurchase(Category);
 const bool Accepted=Commit && QueueUnchanged && OpeningReady && Affordable && PlacementClear();Restore(!Accepted);
 if (HasShaft && Commit && !Accepted)
 { TArray<AActor*> Excluded;if (bAdding) for (AActor* A:AddedActors) Excluded.Add(A);WarehouseElevatorLayout::Rebuild(this,false,Excluded); }
 if (!OpeningReady) Status=TEXT("승강기 이동 공간·층별 진입 바닥이 막혔습니다. 원래 위치로 되돌렸습니다.");
 if (Accepted)
 {
  if (bAdding)
  {
   auto* Store=AWarehouseLayoutInventory::Find(this);TArray<AActor*> NewActors;for (AActor* A:AddedActors) if (IsValid(A)) NewActors.Add(A);
   if (!AddedCargoId.IsNone())
   {
    if (Store->HeldCargo.IsEmpty() || Store->HeldCargo[0].CargoId!=AddedCargoId) { Restore(true);for (AActor* A:NewActors) A->Destroy();Status=TEXT("보류 목록이 변경되어 추가를 취소했습니다."); }
    else
    {
     auto Spec=Store->HeldCargo[0].Strength;Spec.Name=AddedCargoId.ToString();Spec.Members={NewActors[0]};Spec.PhysicsMeshes={Cast<AWarehouseCargo>(NewActors[0])->GetCargoBody()->GetStaticMesh()};
     if (auto* Strength=AWarehouseDamageSystem::Find(this)) Strength->RegisterLayoutProfile(Spec);
     Store->ConsumeHeldCargo(AddedCargoId);
    }
   }
   else
   {
    Zones.RemoveAll([&](const auto& Z) { return Z.Name==TEXT("Preview") && !NewActors.IsEmpty() && Z.Members.Contains(NewActors[0]); });
    Store->CommitPrototype(NewActors);
    if (PlayStage && PlayStage->StageIndex>0)
    {
     PlayStage->ChargePurchase(Category);
     if (PlayStage->bOperating) for(AActor* A:NewActors) if(auto* V=Cast<AWarehouseForklift>(A);V && !V->bPowered) V->ToggleAutonomy();
    }
   }
   const bool ConveyorsOnly=!NewActors.IsEmpty() && !NewActors.ContainsByPredicate([](AActor* A) { return !A || !A->IsA<AWarehouseConveyor>(); });
   RefreshAfterEdit(!ConveyorsOnly);
  }
  else UpdateReferences();
  ++MovedCount;Status=TEXT("배치 적용 · 충돌 복원 · 작업 위치와 경로 갱신");
 }
 else if (!Commit) Status=TEXT("이동 취소 · 원래 위치와 충돌 복원");
 if (bAdding && !Accepted)
 {
  Zones.RemoveAll([&](const auto& Z) { return Z.Name==TEXT("Preview") && !AddedActors.IsEmpty() && Z.Members.Contains(AddedActors[0]); });
  for (AActor* A:AddedActors) if (IsValid(A)) A->Destroy();
 }
 Moving.Reset();bDragging=false;bAdding=false;AddedActors.Reset();AddedCargoId=NAME_None;Rotation=FQuat::Identity;return Accepted;
}
void AWarehouseLayoutEditor::PointerDown(APlayerController* PC,FVector2D ViewportPixels,bool Additive)
{
 if (!bEditing || !PC || ViewportPixels.ContainsNaN()) return;
 if (bDragging || bMarqueeSelecting) PointerUp(false);
 SelectedActors.RemoveAll([](AActor* Actor) { return !IsValid(Actor); });SelectionCount=SelectedActors.Num();
 FVector Origin,Direction;
 if (!PC->DeprojectScreenPositionToWorld(ViewportPixels.X,ViewportPixels.Y,Origin,Direction) || FMath::Abs(Direction.Z)<.001) return;
 const double T=(FloorIndex*400.-Origin.Z)/Direction.Z;
 if (T<0) return;const FVector Point=Origin+Direction*T;
 FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(LayoutPick),true);
 for (AActor* Hidden:PC->HiddenActors) if (Hidden) Params.AddIgnoredActor(Hidden);
 Params.AddIgnoredActor(PC->GetPawn());
 GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Direction*30000,WarehousePhysics::LayoutPickChannel,Params);
 if (Action==EWarehouseLayoutAction::Add)
 { if (BeginAdd(Point)) UpdateSurfaceDrag(Point,Hit.GetActor(),Hit.ImpactPoint);return; }
 AActor* Candidate=SelectionActor(Hit.GetActor());
 if (Category==EWarehouseLayoutCategory::Zone || (Category==EWarehouseLayoutCategory::All && !Candidate))
 {
  const int32 I=ZoneAt(Point);Candidate=nullptr;
  if (I!=INDEX_NONE) for (AActor* Member:Zones[I].Members) if (IsValid(Member))
   { Candidate=SelectionActor(Member);if (Candidate) break; }
 }
 if (Candidate)
 {
  if (Additive) ToggleSelection(Candidate);
  else
  {
   if (!IsSelected(Candidate)) SetSelection({Candidate});
   if (Action==EWarehouseLayoutAction::Delete) DeleteSelection();else BeginDrag(Candidate,Point);
  }
 }
 else
 {
  if (!Additive) ClearSelection();
  MarqueeAdditive=Additive;bMarqueeSelecting=true;MarqueeStart=MarqueeEnd=ViewportPixels;
 }
}
void AWarehouseLayoutEditor::PointerMove(APlayerController* PC,FVector2D ViewportPixels)
{
 if (!bEditing || !PC || ViewportPixels.ContainsNaN()) return;
 if (bMarqueeSelecting) { MarqueeEnd=ViewportPixels;return; }
 if (!bDragging) return;
 FVector Origin,Direction;
 if (!PC->DeprojectScreenPositionToWorld(ViewportPixels.X,ViewportPixels.Y,Origin,Direction) || FMath::Abs(Direction.Z)<.001) return;
 const double T=(FloorIndex*400.-Origin.Z)/Direction.Z;
 if (T>=0)
 {
  FHitResult Hit;FCollisionQueryParams Params(SCENE_QUERY_STAT(LayoutStackSurface),true);
  Params.AddIgnoredActor(PC->GetPawn());for (AActor* Hidden:PC->HiddenActors) if (Hidden) Params.AddIgnoredActor(Hidden);
  for (const auto& M:Moving) if (M.Actor.IsValid()) Params.AddIgnoredActor(M.Actor.Get());
  GetWorld()->LineTraceSingleByChannel(Hit,Origin,Origin+Direction*30000,ECC_Visibility,Params);
  UpdateSurfaceDrag(Origin+Direction*T,Hit.GetActor(),Hit.ImpactPoint);
 }
}
void AWarehouseLayoutEditor::PointerUp(bool Commit)
{
 if (bDragging) EndDrag(Commit);
 if (bMarqueeSelecting)
 {
  if (Commit) SelectScreenRectangle(MarqueeStart,MarqueeEnd,MarqueeAdditive);
  bMarqueeSelecting=false;
 }
 MouseWasDown=false;
}
void AWarehouseLayoutEditor::MouseTick(APlayerController* PC)
{
 if (!bEditing || !PC) return;
 const bool Mouse=PC->IsInputKeyDown(EKeys::LeftMouseButton);float X,Y;
 if (!PC->GetMousePosition(X,Y)) { if (!Mouse && MouseWasDown) PointerUp(false);MouseWasDown=Mouse;return; }
 const FVector2D Pixels(X,Y);
 if (Mouse && !MouseWasDown) PointerDown(PC,Pixels,PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl));
 if (Mouse) PointerMove(PC,Pixels);
 if (!Mouse && MouseWasDown) { PointerMove(PC,Pixels);PointerUp(true); }
 MouseWasDown=Mouse;
}
void AWarehouseLayoutEditor::EndPlay(const EEndPlayReason::Type Reason)
{
 if (bEditing) SetEditing(false,FloorIndex);
 WarehouseElevatorLayout::RestoreInitial(this);
 Super::EndPlay(Reason);
}
