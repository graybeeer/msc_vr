#include "WarehouseShippingSystem.h"
#include "WarehouseLogisticsTruck.h"
#include "WarehouseComputer.h"
#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehouseForklift.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseLayoutEditor.h"
#include "msc_vrCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/TargetPoint.h"
#include "EngineUtils.h"

namespace
{
 bool Finished(const FWarehouseShipment& S)
 { return S.State==EWarehouseShipmentState::Completed || S.State==EWarehouseShipmentState::Cancelled; }
 FTransform SlotPose(const FWarehouseShippingZone& Z,int32 Slot)
 { return Z.Slots[Slot]*Z.Anchor->GetActorTransform(); }
 bool Held(const AActor* Context,AWarehousePallet* P,const TArray<TObjectPtr<AActor>>& Cargo)
 {
  for (TActorIterator<Amsc_vrCharacter> It(Context->GetWorld());It;++It)
   if (It->GetHeldCargo()==P || Cargo.Contains(It->GetHeldCargo())) return true;
  for (TActorIterator<AWarehouseForklift> It(Context->GetWorld());It;++It)
   if (It->GetSupportedPallet()==P || It->ActiveJob.Pallet==P) return true;
  return false;
 }
 TArray<TObjectPtr<AActor>> Contents(AWarehouseDamageSystem* Strength,AWarehousePallet* P)
 {
  TArray<TObjectPtr<AActor>> Result;
  for (auto* A:Strength->GetSupportedActors(P,false)) if (Cast<AWarehouseCargo>(A)) Result.AddUnique(A);
  for (auto* A:P->GetWrappedCargo()) Result.AddUnique(A);
  return Result;
 }
}

AWarehouseShippingSystem::AWarehouseShippingSystem()
{
 RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("ShippingControl"));
 PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=.25f;
}
AWarehouseShippingSystem* AWarehouseShippingSystem::Find(const AActor* Context)
{
 if (!Context || !Context->GetWorld()) return nullptr;
 for (TActorIterator<AWarehouseShippingSystem> It(Context->GetWorld());It;++It) return *It;
 return Context->GetWorld()->SpawnActor<AWarehouseShippingSystem>();
}
void AWarehouseShippingSystem::RefreshLayoutCatalog()
{
 if (!HasAuthority()) return;
 Trucks.RemoveAll([](const auto& T){return !IsValid(T);});
 for (TActorIterator<AWarehouseLogisticsTruck> It(GetWorld());It;++It) Trucks.AddUnique(*It);
 Zones.RemoveAll([](const auto& Z){return !IsValid(Z.Anchor);});
 AWarehouseLayoutEditor* Layout=nullptr;
 for (TActorIterator<AWarehouseLayoutEditor> It(GetWorld());It;++It) {Layout=*It;break;}
 if (!Layout) return;
 for (auto& Area:Layout->Zones)
 {
  if (Area.Name!=TEXT("Inbound") && Area.Name!=TEXT("Outbound")) continue;
  auto* Existing=Zones.FindByPredicate([&](const auto& Z){return Area.Members.Contains(Z.Anchor);});
  if (Existing)
  {
   // The anchor already moved with its group. Retain IDs, reserved slot indexes and local poses.
   Existing->HalfSize=Existing->Anchor->GetActorQuat().UnrotateVector(Area.HalfSize).GetAbs();
   continue;
  }
  if (!Area.Members.ContainsByPredicate([](const auto& A){return IsValid(A);})) continue;
  AActor* Anchor=nullptr;
  for (AActor* A:Area.Members) if (IsValid(A) && Cast<ATargetPoint>(A) && A->GetActorLocation().Equals(Area.Centre,1)) {Anchor=A;break;}
  if (!Anchor)
  {
   // A new staging group may have only paint/NPC actors. Its shipping origin must be the area centre.
   Anchor=GetWorld()->SpawnActor<ATargetPoint>(Area.Centre,FRotator::ZeroRotator);
   if (!Anchor) continue;
   Anchor->Tags.Add(TEXT("LayoutType:Zone"));
   for (AActor* A:Area.Members) if (IsValid(A))
   { for (const FName Tag:A->Tags) if (Tag.ToString().StartsWith(TEXT("LayoutGroup:")) || Tag.ToString().StartsWith(TEXT("WarehouseFloor"))) Anchor->Tags.AddUnique(Tag);break; }
   Area.Members.Add(Anchor);
  }
  FWarehouseShippingZone Zone;Zone.Anchor=Anchor;Zone.bInbound=Area.Name==TEXT("Inbound");
  Zone.ZoneId=FString::Printf(TEXT("%s-%s"),Zone.bInbound ? TEXT("IN") : TEXT("OUT"),*Anchor->GetName());
  Zone.HalfSize=Anchor->GetActorQuat().UnrotateVector(Area.HalfSize).GetAbs();
  auto AddSlot=[&](const FTransform& WorldPose)
  {
   if (Zone.Slots.Num()>=6) return;
   const FTransform Local=WorldPose.GetRelativeTransform(Anchor->GetActorTransform());const FVector P=Local.GetLocation();
   // A 110 cm pallet needs 5 cm of margin; reject out-of-area and overlapping slots.
   if (FMath::Abs(P.X)>Zone.HalfSize.X-60 || FMath::Abs(P.Y)>Zone.HalfSize.Y-60 || FMath::Abs(P.Z)>3) return;
   if (Zone.Slots.ContainsByPredicate([&](const FTransform& S){return FVector::Dist2D(S.GetLocation(),P)<125;})) return;
   Zone.Slots.Add(Local);
  };
  for (const auto& Pose:Area.Destinations) AddSlot(Pose);
  // Inbound clones do not have user-facing outbound destinations. Derive bounded handover slots.
  for (double Y=-Zone.HalfSize.Y+70;Y<=Zone.HalfSize.Y-60 && Zone.Slots.Num()<6;Y+=140)
   for (double X=-Zone.HalfSize.X+70;X<=Zone.HalfSize.X-60 && Zone.Slots.Num()<6;X+=140)
    AddSlot(FTransform(FRotator(0,180,0),FVector(X,Y,.15))*Anchor->GetActorTransform());
  if (!Zone.bInbound)
  {
   // Console dispatch resolves these same world destinations to an outbound index.
   Area.Destinations.Reset();
   for (const auto& Slot:Zone.Slots) Area.Destinations.Add(Slot*Anchor->GetActorTransform());
  }
  Zones.Add(MoveTemp(Zone));
 }
}
void AWarehouseShippingSystem::Tick(float Dt) { Super::Tick(Dt);RefreshShipping();AdvanceOperations(); }
FString AWarehouseShippingSystem::GetPalletShipment(AWarehousePallet* P) const
{
 for (const auto& S:Shipments) if (!Finished(S))
  for (const auto& L:S.Lines) if (L.Pallet==P && !L.bOperatorStopped) return S.ShipmentId;
 return FString();
}
FString AWarehouseShippingSystem::LocatePallet(AWarehousePallet* P) const
{
 for (const auto& S:Shipments) for (const auto& L:S.Lines) if (L.Pallet==P)
 {
  if (S.State==EWarehouseShipmentState::Completed && S.Direction==EWarehouseShipmentDirection::Outbound)
   return S.Name+TEXT(" · 출고 완료 / 운송 중");
  if (!Finished(S) && IsValid(S.Truck) && S.Truck->IsUnitOnTruck(P)) return S.Name+TEXT(" · 트럭 적재함");
 }
 return FString();
}
bool AWarehouseShippingSystem::CreateOutbound(const FString& Name,const TArray<AWarehousePallet*>& Pallets,const FString& ZoneId,AWarehouseLogisticsTruck* Truck)
{ return Create(Name,Pallets,ZoneId,Truck,EWarehouseShipmentDirection::Outbound); }
bool AWarehouseShippingSystem::StartInbound(AWarehouseLogisticsTruck* Truck,const FString& ZoneId)
{
 TArray<AWarehousePallet*> Pallets;
 if (IsValid(Truck)) for (AWarehousePallet* P:Truck->ManifestPallets) Pallets.Add(P);
 return Create(IsValid(Truck) ? Truck->TruckId+TEXT(" 입고") : TEXT("입고"),Pallets,ZoneId,Truck,EWarehouseShipmentDirection::Inbound);
}
bool AWarehouseShippingSystem::Create(const FString& Name,const TArray<AWarehousePallet*>& Pallets,const FString& ZoneId,AWarehouseLogisticsTruck* Truck,EWarehouseShipmentDirection Direction)
{
 auto Fail=[&](const TCHAR* Why) { LastMessage=Why;return false; };
 const auto* Zone=Zones.FindByPredicate([&](const auto& Z) { return Z.ZoneId==ZoneId; });
 if (!Zone || !IsValid(Zone->Anchor) || Zone->Slots.IsEmpty()) return Fail(TEXT("입출고 구역 또는 팔레트 자리가 없습니다. [NO_STAGING_ZONE]"));
 if (Zone->bInbound!=(Direction==EWarehouseShipmentDirection::Inbound)) return Fail(TEXT("입고·출고 구역의 역할이 맞지 않습니다."));
 if (!IsValid(Truck) || !Trucks.Contains(Truck) || Truck->State==EWarehouseTruckState::Departing || Truck->State==EWarehouseTruckState::Departed || Truck->State==EWarehouseTruckState::Returning)
  return Fail(TEXT("배송편에 사용할 트럭이 없습니다. [NO_TRUCK]"));
 if (!Truck->IsManifestReady()) return Fail(TEXT("트럭의 초기 입고 명세를 확인 중이거나 초기화 오류가 있습니다."));
 for (const auto& S:Shipments) if (!Finished(S) && S.Truck==Truck) return Fail(TEXT("이 트럭은 다른 배송편에 배정되어 있습니다."));
 if (Pallets.IsEmpty()) return Fail(TEXT("배송편의 팔레트 목록이 비어 있습니다."));
 if (Pallets.Num()>Truck->LoadSlots.Num()) return Fail(TEXT("트럭의 팔레트 적재 자리가 부족합니다. [TRUCK_CAPACITY]"));
 if (Direction==EWarehouseShipmentDirection::Outbound && !Truck->ManifestPallets.IsEmpty())
  return Fail(TEXT("다른 물류가 실린 트럭은 출고 배송편에 배정할 수 없습니다."));
 auto* Strength=AWarehouseDamageSystem::Find(this);
 if (!Strength) return Fail(TEXT("물류 하중 관리가 없습니다. [NO_STRENGTH_SYSTEM]"));
 Strength->EnsureSupportSnapshot();
 TSet<AWarehousePallet*> Unique;float TotalMass=0;
 FWarehouseShipment S;S.Name=Name.TrimStartAndEnd().IsEmpty() ? TEXT("배송편") : Name.Left(80);
 S.ShipmentId=FString::Printf(TEXT("SHIP-%s-%06d"),*GetName(),NextShipment);
 S.ZoneId=ZoneId;S.Direction=Direction;S.Truck=Truck;S.TruckVisitId=Truck->GetVisitId();S.StartedAt=GetWorld()->GetTimeSeconds();
 TSet<int32> ReservedSlots;
 for (const auto& Existing:Shipments) if (!Finished(Existing) && Existing.ZoneId==ZoneId)
  for (const auto& L:Existing.Lines) ReservedSlots.Add(L.StagingSlot);
 for (auto* P:Pallets)
 {
  if (!IsValid(P) || P->GetWorld()!=GetWorld() || Unique.Contains(P)) return Fail(TEXT("팔레트가 삭제되었거나 목록에 중복되어 있습니다."));
  Unique.Add(P);
  if (!GetPalletShipment(P).IsEmpty()) return Fail(TEXT("이미 다른 배송편에 예약된 팔레트입니다."));
  if (Strength->HasFailed(P) || P->ReceiptState==EWarehouseReceiptState::WrapTorn) return Fail(TEXT("파손·재검수 물류는 출고할 수 없습니다."));
  FWarehouseShipmentLine L;L.Pallet=P;L.PalletLabel=P->GetName();L.TruckSlot=S.Lines.Num();
  L.Cargo=Contents(Strength,P);
  if (Direction==EWarehouseShipmentDirection::Inbound)
  {
   if (!Truck->IsUnitOnTruck(P)) return Fail(TEXT("입고 명세의 팔레트가 해당 트럭에 없습니다."));
   L.bLoaded=true;L.Cargo.Reset();
   for (AActor* A:Truck->GetUnitCargo(P)) L.Cargo.Add(A);
  }
  else
  {
   if (P->ReceiptState!=EWarehouseReceiptState::Confirmed || P->ReceivingState!=EWarehouseReceivingState::Accepted)
    return Fail(TEXT("입고 검수·랩핑이 완료되지 않았거나 격리된 물류는 출고할 수 없습니다. [QUALITY_HOLD]"));
   if (P->ActorHasTag(TEXT("LogisticsInTransit")) || Held(this,P,L.Cargo)) return Fail(TEXT("운반 중이거나 사람이 든 팔레트는 예약할 수 없습니다."));
   if (L.Cargo.IsEmpty()) return Fail(TEXT("빈 팔레트는 출고 명세로 사용할 수 없습니다."));
  }
  TotalMass+=P->PalletMassKg+Strength->GetSupportedMass(P);
  for (int32 I=0;I<Zone->Slots.Num();++I) if (!ReservedSlots.Contains(I))
  { L.StagingSlot=I;ReservedSlots.Add(I);break; }
  if (L.StagingSlot==INDEX_NONE) return Fail(TEXT("배송편 전체를 준비할 대기 자리가 부족합니다. [STAGING_CAPACITY]"));
  S.Lines.Add(MoveTemp(L));
 }
 const float RequiredMass=Direction==EWarehouseShipmentDirection::Outbound ? TotalMass : Truck->PayloadMassKg();
 if (!FMath::IsFinite(RequiredMass) || !FMath::IsFinite(Truck->RatedPayloadKg) || RequiredMass<=0 || RequiredMass>Truck->RatedPayloadKg)
  return Fail(TEXT("배송편 무게가 트럭 허용 하중을 넘습니다. [TRUCK_OVERLOAD]"));
 Shipments.Add(MoveTemp(S));++NextShipment;
 LastMessage=TEXT("배송편을 등록했습니다. 준비 조건을 확인하고 작업을 진행합니다.");return true;
}

bool AWarehouseShippingSystem::HasHandover(const FWarehouseShipmentLine& L,const FWarehouseShippingZone& Z,FString& Why) const
{
 auto Fail=[&](const TCHAR* Message) { Why=Message;return false; };
 if (!IsValid(L.Pallet) || !IsValid(Z.Anchor) || !Z.Slots.IsValidIndex(L.StagingSlot)) return Fail(TEXT("팔레트 또는 대기 자리가 없습니다."));
 const FTransform Target=SlotPose(Z,L.StagingSlot);
 const auto* Body=L.Pallet->GetPalletBody();
 const FVector Local=Z.Anchor->GetActorTransform().InverseTransformPosition(L.Pallet->GetActorLocation());
 if (FMath::Abs(Local.X)>Z.HalfSize.X-55 || FMath::Abs(Local.Y)>Z.HalfSize.Y-55 ||
     FVector::Dist2D(L.Pallet->GetActorLocation(),Target.GetLocation())>5 ||
     FMath::Abs(L.Pallet->GetActorLocation().Z-Target.GetLocation().Z)>3)
  return Fail(TEXT("팔레트를 지정 대기 자리에 내려놓아야 합니다."));
 if (!Body->IsSimulatingPhysics() || Body->GetPhysicsLinearVelocity().Size()>5 || Body->GetPhysicsAngularVelocityInDegrees().Size()>5 || Held(this,L.Pallet,L.Cargo))
  return Fail(TEXT("팔레트의 정지·포크 인출을 기다립니다."));
 auto* Strength=AWarehouseDamageSystem::Find(this);
 if (!Strength || Strength->HasFailed(L.Pallet) || L.Pallet->ReceiptState!=EWarehouseReceiptState::Confirmed || L.Pallet->ReceivingState!=EWarehouseReceivingState::Accepted)
  return Fail(TEXT("팔레트가 파손되어 검수가 필요합니다."));
 const auto Actual=Contents(Strength,L.Pallet);
 if (Actual.Num()!=L.Cargo.Num()) return Fail(TEXT("예약 후 팔레트 적재 명세가 바뀌었습니다."));
 for (AActor* A:L.Cargo)
 {
  if (!IsValid(A) || !Actual.Contains(A) || Strength->HasFailed(A)) return Fail(TEXT("예약한 화물이 누락되거나 손상되었습니다."));
  if (const auto* Part=Cast<UPrimitiveComponent>(A->GetRootComponent()); !Part || Part->GetPhysicsLinearVelocity().Size()>5)
   return Fail(TEXT("적재물이 아직 움직이고 있습니다."));
 }
 // This is a handover transaction check, not a remote driving safety sensor.
 // A manually reinserted tine must be clear before cinematic ownership changes.
 TArray<FOverlapResult> Overlaps;
 GetWorld()->OverlapMultiByChannel(Overlaps,L.Pallet->GetActorLocation()+FVector(0,0,8),L.Pallet->GetActorQuat(),ECC_Visibility,
  FCollisionShape::MakeBox(FVector(54,54,7)),FCollisionQueryParams(SCENE_QUERY_STAT(ShippingForkClear),false,this));
 for (const auto& Hit:Overlaps) if (Cast<AWarehouseForklift>(Hit.GetActor())) return Fail(TEXT("포크가 인계 공간에 남아 있습니다."));
 FCollisionQueryParams Params(SCENE_QUERY_STAT(ShippingHandover),false,this);Params.AddIgnoredActor(L.Pallet);
 for (AActor* A:L.Cargo) Params.AddIgnoredActor(A);
 for (float X:{-40.f,40.f}) for (float Y:{-40.f,40.f})
 {
  FHitResult Hit;const FVector P=Target.TransformPosition(FVector(X,Y,0));
  if (!GetWorld()->LineTraceSingleByChannel(Hit,P+FVector(0,0,2),P-FVector(0,0,5),ECC_Visibility,Params) || Hit.ImpactNormal.Z<.98f)
   return Fail(TEXT("팔레트가 바닥에 안정적으로 놓이지 않았습니다."));
  if (Cast<AWarehouseForklift>(Hit.GetActor())) return Fail(TEXT("포크가 팔레트 아래에 남아 있습니다."));
 }
 return true;
}

bool AWarehouseShippingSystem::ZonePlacementClear(const FWarehouseShippingZone& Z,int32 Slot,AWarehousePallet* P,FString& Why) const
{
 if (!IsValid(P)) { Why=TEXT("하역할 팔레트가 삭제되었습니다. [MISSING_PALLET]");return false; }
 if (!IsValid(Z.Anchor) || !Z.Slots.IsValidIndex(Slot)) { Why=TEXT("하역할 구역이 없습니다.");return false; }
 const FTransform Pose=SlotPose(Z,Slot);
 FTransform Source=P->GetActorTransform();FBox LocalPallet(ForceInit);
 const FBox PalletBounds=AWarehouseDamageSystem::GetPhysicalBounds(P);
 if (!PalletBounds.IsValid) { Why=TEXT("하역할 팔레트의 크기를 확인할 수 없습니다.");return false; }
 for (int32 I=0;I<8;++I) LocalPallet+=Source.InverseTransformPosition(FVector(I&1?PalletBounds.Max.X:PalletBounds.Min.X,I&2?PalletBounds.Max.Y:PalletBounds.Min.Y,I&4?PalletBounds.Max.Z:PalletBounds.Min.Z));
 const FVector Bottom(LocalPallet.GetCenter().X,LocalPallet.GetCenter().Y,LocalPallet.Min.Z);
 FTransform Goal(Pose.GetRotation(),Pose.GetLocation()-Pose.GetRotation().RotateVector(Bottom*Source.GetScale3D()),Source.GetScale3D());
 TArray<AActor*> Members{P};for (AWarehouseLogisticsTruck* T:Trucks) if (IsValid(T) && T->IsUnitOnTruck(P)) Members.Append(T->GetUnitCargo(P));
 FBox DestinationBounds(ForceInit);
 for (AActor* A:Members)
 {
  const FBox B=AWarehouseDamageSystem::GetPhysicalBounds(A);if (!B.IsValid) continue;
  for (int32 I=0;I<8;++I)
  {
   const FVector V=Goal.TransformPosition(Source.InverseTransformPosition(FVector(I&1?B.Max.X:B.Min.X,I&2?B.Max.Y:B.Min.Y,I&4?B.Max.Z:B.Min.Z)));
   const FVector Local=Z.Anchor->GetActorTransform().InverseTransformPosition(V);
   if (FMath::Abs(Local.X)>Z.HalfSize.X || FMath::Abs(Local.Y)>Z.HalfSize.Y)
   { Why=TEXT("입고 물류가 구역 경계를 벗어납니다.");return false; }
   DestinationBounds+=V;
  }
 }
 if (!DestinationBounds.IsValid) { Why=TEXT("물류 크기가 유효하지 않습니다.");return false; }
 DestinationBounds.Min.Z+=2;DestinationBounds.Max.Z-=.5;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(ShippingPlacement),false,this);Params.AddIgnoredActor(P);
 if (auto* Strength=AWarehouseDamageSystem::Find(this)) for (AActor* A:Strength->GetSupportedActors(P,false)) Params.AddIgnoredActor(A);
 for (auto* A:P->GetWrappedCargo()) Params.AddIgnoredActor(A);
 if (GetWorld()->OverlapBlockingTestByChannel(DestinationBounds.GetCenter(),FQuat::Identity,ECC_Visibility,
   FCollisionShape::MakeBox(DestinationBounds.GetExtent()),Params))
 { Why=TEXT("입고 대기 자리가 점유되어 하역을 기다립니다.");return false; }
 for (float X:{-40.f,40.f}) for (float Y:{-40.f,40.f})
 {
  FHitResult Hit;const FVector Probe=Pose.TransformPosition(FVector(X,Y,0));
  if (!GetWorld()->LineTraceSingleByChannel(Hit,Probe+FVector(0,0,2),Probe-FVector(0,0,5),ECC_Visibility,Params) || Hit.ImpactNormal.Z<.98f)
  { Why=TEXT("입고 대기 자리의 바닥 받침이 없습니다.");return false; }
 }
 return true;
}

void AWarehouseShippingSystem::RefreshShipping()
{
 if (auto* Layout=AWarehouseLayoutEditor::Find(this);Layout && Layout->bEditing) return;
 if (auto* Strength=AWarehouseDamageSystem::Find(this)) Strength->EnsureSupportSnapshot();
 for (auto& S:Shipments)
 {
  if (Finished(S)) continue;
  S.ElapsedSeconds=GetWorld()->GetTimeSeconds()-S.StartedAt;
  const auto* Zone=Zones.FindByPredicate([&](const auto& Z) { return Z.ZoneId==S.ZoneId; });
  S.WaitReason.Reset();
  if (!IsValid(S.Truck)) { for (auto& L:S.Lines) { L.PreparationStartedAt=-1;L.bPrepared=false; } S.WaitReason=TEXT("배정 트럭이 없습니다. [NO_TRUCK]");continue; }
  if (S.State==EWarehouseShipmentState::Departing)
  {
   if (S.Truck->State==EWarehouseTruckState::Departed)
   { S.State=EWarehouseShipmentState::Completed;S.FinishedAt=GetWorld()->GetTimeSeconds();S.ElapsedSeconds=S.FinishedAt-S.StartedAt;S.Status=TEXT("출고 완료 · 운송 중");if (S.Direction==EWarehouseShipmentDirection::Inbound) S.Status=TEXT("입고 하역·인계 완료"); }
   if (S.Truck->State==EWarehouseTruckState::Unavailable) S.WaitReason=TEXT("출발 중 트럭 구성 오류: ")+S.Truck->Status;
   continue;
  }
  if (!Zone || !IsValid(Zone->Anchor)) { for (auto& L:S.Lines) { L.PreparationStartedAt=-1;L.bPrepared=false; } S.WaitReason=TEXT("입출고 구역이 없습니다. [NO_STAGING_ZONE]");S.bCargoReady=false;continue; }
  Advance(S,*Zone);
 }
}

void AWarehouseShippingSystem::Advance(FWarehouseShipment& S,const FWarehouseShippingZone& Z)
{
 auto* Truck=S.Truck.Get();const double Now=GetWorld()->GetTimeSeconds();
 // Layout and inventory can change while a visit is waiting. Revalidate before
 // either dispatch or transfer indexes a reserved staging slot or pallet.
 for (auto& L:S.Lines)
 {
  if (!IsValid(L.Pallet) || !Z.Slots.IsValidIndex(L.StagingSlot))
  {
   for (auto& Line:S.Lines) { Line.PreparationStartedAt=-1;Line.bPrepared=false; }
   L.Status=!IsValid(L.Pallet) ? TEXT("팔레트 누락 [MISSING_PALLET]") : TEXT("예약한 대기 자리 누락 [MISSING_STAGING_SLOT]");
   S.WaitReason=L.Status;S.bCargoReady=false;S.bTruckReady=Truck->IsReady();return;
  }
 }
 const bool DispatchDue=Now>=S.NextDispatchAt;
 if (DispatchDue) S.NextDispatchAt=Now+1.;
 const bool NeedsHandling=S.Lines.ContainsByPredicate([&](const auto& L) { return S.Direction==EWarehouseShipmentDirection::Outbound ? !L.bLoaded : !L.bArrived; });
 if (NeedsHandling && Truck->IsReady()) Truck->PrepareDock();
 S.bTruckReady=Truck->IsDockPrepared();bool All=true;
 for (auto& L:S.Lines)
 {
  if (!IsValid(L.Pallet)) { L.Status=TEXT("팔레트 누락");S.WaitReason=L.Status;All=false;continue; }
  if (S.Direction==EWarehouseShipmentDirection::Inbound)
  { if (!L.bArrived) All=false;continue; }
  if (L.bLoaded)
  { if (!Truck->IsUnitOnTruck(L.Pallet)) { S.WaitReason=TEXT("상차한 팔레트가 트럭에서 누락되었습니다.");All=false; } continue; }
  if (IsValid(L.Vehicle) && !L.JobId.IsEmpty() && L.Vehicle->CompletedJobIds.Contains(L.JobId)) L.bDeliveryReported=true;
  if (!L.bDeliveryReported && !L.JobId.IsEmpty())
  {
   const bool Exists=IsValid(L.Vehicle) && (L.Vehicle->ActiveJob.JobId==L.JobId || L.Vehicle->PendingJobs.ContainsByPredicate([&](const auto& Job) { return Job.JobId==L.JobId; }));
   if (!Exists)
   {
    const int32 ReportIndex=IsValid(L.Vehicle) ? L.Vehicle->JobReports.FindLastByPredicate([&](const auto& R){return R.JobId==L.JobId && (R.Result==TEXT("CANCELLED") || R.Result==TEXT("DEFERRED"));}) : INDEX_NONE;
    if (ReportIndex!=INDEX_NONE)
    {
     const auto& Report=L.Vehicle->JobReports[ReportIndex];
     L.bOperatorStopped=Report.Detail.Contains(TEXT("[USER_STOP]"));
     if (Report.Result==TEXT("DEFERRED")) L.RetryDispatchAt=Now+30.;
     L.Status=L.bOperatorStopped ? TEXT("사용자가 작업을 중지했습니다.") : TEXT("운반 불가 · 다른 작업을 먼저 처리하고 30초 후 다시 확인합니다.");
    }
    L.JobId.Reset();L.Vehicle=nullptr;
   }
  }
  if (L.bOperatorStopped) {All=false;S.WaitReason=L.Status;continue;}
  const bool Reported=L.bDeliveryReported;
  FString Reason;
  const bool Ready=Reported && HasHandover(L,Z,Reason);
  if (!Ready) L.StableSince=-1;
  else if (L.StableSince<0) L.StableSince=Now;
  L.bArrived=Ready && Now-L.StableSince>=.5;
  L.Status=L.bArrived ? TEXT("출고 구역 인계 완료") : Reported ? Reason : TEXT("지게차 운반 대기 / 진행 중");
  if (!L.bArrived) { L.bPrepared=false;L.PreparationStartedAt=-1; }
  else if (!L.bPrepared)
  {
   int32 Busy=0;
   for (const auto& Other:Shipments) if (!Finished(Other)) for (const auto& Line:Other.Lines)
    if (Line.PreparationStartedAt>=0 && !Line.bPrepared) ++Busy;
   if (L.PreparationStartedAt<0 && Busy<FMath::Max(1,OutboundPreparationStations)) L.PreparationStartedAt=Now;
   if (L.PreparationStartedAt>=0 && Now-L.PreparationStartedAt>=FMath::Max(.1f,OutboundPreparationSeconds)) L.bPrepared=true;
   L.Status=L.bPrepared ? TEXT("검품·배송 라벨 준비 완료") : L.PreparationStartedAt<0 ? TEXT("출고 작업대 대기") : TEXT("검품·배송 라벨 처리 중");
   if (!L.bPrepared) { All=false;S.WaitReason=L.Status; }
  }
  if (!L.bArrived)
  {
   All=false;
   if (!Reason.IsEmpty()) S.WaitReason=Reason;
   if (L.JobId.IsEmpty() && DispatchDue && Now>=L.RetryDispatchAt)
   {
    bool FreeVehicle=false;
    for (TActorIterator<AWarehouseForklift> V(GetWorld());V;++V)
     if (V->CanAcceptNewWork() && V->ActiveJob.JobId.IsEmpty() && V->PendingJobs.IsEmpty()) { FreeVehicle=true;break; }
    if (!FreeVehicle) { S.WaitReason=TEXT("가용 지게차를 기다립니다.");continue; }
    AWarehouseComputer* Console=nullptr;for (TActorIterator<AWarehouseComputer> It(GetWorld());It;++It) if (It->CanUse()) { Console=*It;break; }
    if (!Console) { S.WaitReason=TEXT("운반 지시를 내릴 관제 컴퓨터가 없습니다.");continue; }
    Console->RefreshLayoutDestinations();const FTransform Pose=SlotPose(Z,L.StagingSlot);
    const int32 Dest=Console->OutboundDestinations.IndexOfByPredicate([&](const auto& P) { return P.Equals(Pose,.5f); });
    auto* Cargo=L.Cargo.IsEmpty() ? nullptr : Cast<AWarehouseCargo>(L.Cargo[0]);
    if (Dest==INDEX_NONE || !Cargo) { S.WaitReason=TEXT("출고 구역의 운반 목적지 또는 화물이 유효하지 않습니다.");continue; }
    if (!Console->IssueShipmentOutbound(Cargo,nullptr,Dest,S.ShipmentId)) { S.WaitReason=Console->LastMessage;continue; }
    for (const auto& T:Console->Tasks) if (T.Pallet==L.Pallet && !T.bCompleted && !T.bCancelled && !T.bDeferred)
    { L.JobId=T.JobId;L.Vehicle=T.Vehicle;break; }
   }
  }
 }
 if (!S.Lines.IsEmpty() && !S.Lines.ContainsByPredicate([](const auto& L){return !L.bOperatorStopped;}))
 {
  S.State=EWarehouseShipmentState::Cancelled;S.Status=TEXT("사용자 중지 · 배송편 취소");S.FinishedAt=Now;
  S.ElapsedSeconds=Now-S.StartedAt;return;
 }
 S.bCargoReady=All;
 const bool Transferred=S.Lines.ContainsByPredicate([&](const auto& L) { return S.Direction==EWarehouseShipmentDirection::Outbound ? L.bLoaded : L.bArrived; });
 S.State=Transferred ? EWarehouseShipmentState::Transferring : All ? EWarehouseShipmentState::WaitingTruck : EWarehouseShipmentState::Preparing;
 S.Status=S.Direction==EWarehouseShipmentDirection::Outbound ? TEXT("출고 준비 · 지게차 운반") : TEXT("입고 하역 대기");
 if (S.Direction==EWarehouseShipmentDirection::Outbound && !All) return;
 if (!Truck->IsReady())
 { S.WaitReason=Truck->Status.IsEmpty() ? TEXT("지정 도크의 트럭 준비를 기다립니다.") : Truck->Status;return; }
 const bool Complete=S.Lines.ContainsByPredicate([&](const auto& L) { return S.Direction==EWarehouseShipmentDirection::Outbound ? !L.bLoaded : !L.bArrived; })==false;
 if (Complete)
 {
  const bool ManifestCorrect=S.Direction==EWarehouseShipmentDirection::Inbound ? Truck->ManifestPallets.IsEmpty() :
   Truck->ManifestPallets.Num()==S.Lines.Num() && !Truck->ManifestPallets.ContainsByPredicate([&](AWarehousePallet* P) { return !S.Lines.ContainsByPredicate([&](const auto& L) { return L.Pallet==P && L.bLoaded; }); });
  if (!ManifestCorrect) { S.WaitReason=TEXT("트럭 적재 명세가 배송편과 일치하지 않습니다.");return; }
  S.State=EWarehouseShipmentState::ReadyToDepart;S.Status=TEXT("명세 확인 완료 · 출발 준비");
  if (!Truck->PrepareDeparture() || !Truck->IsDeparturePrepared()) { S.WaitReason=Truck->DockServiceStatus;return; }
  if (Truck->Depart()) { S.State=EWarehouseShipmentState::Departing;S.Status=TEXT("트럭 출발 중"); }
  else S.WaitReason=Truck->Status;
  return;
 }
 if (!Truck->PrepareDock() || !Truck->IsDockPrepared()) { S.WaitReason=Truck->DockServiceStatus;return; }
 if (Now<S.NextTransferAt) return;
 for (auto& L:S.Lines)
 {
  const bool In=S.Direction==EWarehouseShipmentDirection::Inbound;
  if (In ? L.bArrived : L.bLoaded) continue;
  if (In && !ZonePlacementClear(Z,L.StagingSlot,L.Pallet,S.WaitReason)) return;
  if (!Truck->TransferUnit(L.Pallet,L.TruckSlot,!In,In ? SlotPose(Z,L.StagingSlot) : FTransform::Identity))
  { S.WaitReason=Truck->Status;return; }
  if (In) { L.bArrived=true;L.bLoaded=false;L.Status=TEXT("입고 구역 인계 완료 · NPC 검수 대기"); }
  else { L.bLoaded=true;L.Status=TEXT("트럭 상차 완료"); }
  S.State=EWarehouseShipmentState::Transferring;S.Status=In ? TEXT("NPC 하역 중 · 한 팔레트씩 인계") : TEXT("NPC 상차 중 · 한 팔레트씩 적재");
  S.NextTransferAt=Now+FMath::Max(.1f,Truck->TransferIntervalSeconds);
  UE_LOG(LogTemp,Log,TEXT("SHIPPING_TRANSFER %s pallet=%s direction=%s"),*S.ShipmentId,*L.Pallet->GetName(),In ? TEXT("INBOUND") : TEXT("OUTBOUND"));
  return;
 }
}

bool AWarehouseShippingSystem::CancelShipment(const FString& Id)
{
 auto* S=Shipments.FindByPredicate([&](const auto& V) { return V.ShipmentId==Id; });
 if (!S || Finished(*S)) return false;
 if (S->State==EWarehouseShipmentState::Departing || S->Lines.ContainsByPredicate([&](const auto& L) { return S->Direction==EWarehouseShipmentDirection::Inbound ? L.bArrived : L.bLoaded; }))
 { LastMessage=TEXT("상하차가 시작된 배송편은 취소할 수 없습니다.");return false; }
 for (const auto& L:S->Lines) if (IsValid(L.Vehicle) && L.Vehicle->ActiveJob.JobId==L.JobId && !L.JobId.IsEmpty())
 { LastMessage=TEXT("진행 중인 지게차 운반을 마친 뒤 취소하세요.");return false; }
 for (const auto& L:S->Lines) if (!L.JobId.IsEmpty())
 {
  for (TActorIterator<AWarehouseComputer> It(GetWorld());It;++It) It->CancelQueuedJob(L.JobId);
  // A damaged/deleted console may be unable to cancel its queue. Keep the
  // reservation until the real queued instruction has been removed.
  if (IsValid(L.Vehicle) && L.Vehicle->PendingJobs.ContainsByPredicate([&](const auto& J) { return J.JobId==L.JobId; }))
  { LastMessage=TEXT("관제 컴퓨터를 복구한 뒤 대기 운반을 취소하세요. 배송편 예약을 유지합니다.");return false; }
 }
 S->State=EWarehouseShipmentState::Cancelled;S->Status=TEXT("배송편 취소");S->FinishedAt=GetWorld()->GetTimeSeconds();
 S->ElapsedSeconds=S->FinishedAt-S->StartedAt;
 S->WaitReason.Reset();
 LastMessage=TEXT("배송편과 대기 예약을 취소했습니다.");return true;
}
