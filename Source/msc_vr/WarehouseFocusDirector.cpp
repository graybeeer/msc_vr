#include "WarehouseFocusDirector.h"
#include "WarehouseOperationsSettings.h"
#include "WarehouseShippingSystem.h"
#include "WarehouseLogisticsTruck.h"
#include "WarehouseComputer.h"
#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehouseForklift.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseInboundZone.h"
#include "WarehouseLayoutEditor.h"
#include "WarehouseExperimentSubsystem.h"
#include "WarehousePlayMode.h"
#include "Components/SceneComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
 constexpr int32 SupplyLimit=24;
 constexpr int32 KeepRackStock=3;
 constexpr double OrderIntervalSeconds=30;
 template<typename T> T* Existing(const UObject* Context)
 {
  if (Context && Context->GetWorld())
   for (TActorIterator<T> It(Context->GetWorld());It;++It) return *It;
  return nullptr;
 }
 bool Open(const FWarehouseShipment& Shipment)
 { return Shipment.State!=EWarehouseShipmentState::Completed && Shipment.State!=EWarehouseShipmentState::Cancelled; }
 bool Busy(const AWarehouseShippingSystem* Shipping,const AWarehouseLogisticsTruck* Truck)
 { return Shipping->Shipments.ContainsByPredicate([&](const auto& S){return Open(S) && S.Truck==Truck;}); }
 TArray<AWarehouseCargo*> Contents(AWarehousePallet* Pallet,AWarehouseDamageSystem* Strength)
 {
  TArray<AWarehouseCargo*> Result;
  for (AActor* A:Strength->GetSupportedActors(Pallet,false)) if (auto* C=Cast<AWarehouseCargo>(A)) Result.AddUnique(C);
  for (AActor* A:Pallet->GetWrappedCargo()) if (auto* C=Cast<AWarehouseCargo>(A)) Result.AddUnique(C);
  return Result;
 }
}

AWarehouseFocusDirector::AWarehouseFocusDirector()
{
 RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("OperationsDirector"));
 PrimaryActorTick.bCanEverTick=true;
 PrimaryActorTick.TickInterval=1.f;
}
AWarehouseFocusDirector* AWarehouseFocusDirector::Find(const UObject* Context)
{
 UWorld* World=Context ? Context->GetWorld() : nullptr;if (!World) return nullptr;
 // Slate asks for mode visibility every frame. Cache positive lookups, independently per PIE world.
 static TMap<TWeakObjectPtr<UWorld>,TWeakObjectPtr<AWarehouseFocusDirector>> Directors;
 if (const auto* Cached=Directors.Find(World);Cached && Cached->IsValid()) return Cached->Get();
 if (Directors.Num()>8) for (auto It=Directors.CreateIterator();It;++It) if (!It.Key().IsValid() || !It.Value().IsValid()) It.RemoveCurrent();
 auto* Director=Existing<AWarehouseFocusDirector>(Context);
 if (Director) Directors.Add(World,Director);
 return Director;
}
bool AWarehouseFocusDirector::IsActive(const UObject* Context)
{ const auto* Director=Find(Context);return Director && !Director->IsManualOperations(); }

void AWarehouseFocusDirector::BeginPlay()
{
 Super::BeginPlay();
 if (const auto* Settings=UWarehouseOperationsSettings::Find(this)) bManualOperations=Settings->IsManualOperations();
 const auto* GI=GetGameInstance();
 const auto* Experiment=GI ? GI->GetSubsystem<UWarehouseExperimentSubsystem>() : nullptr;
 if (HasAuthority() && !bManualOperations && !(Experiment && (Experiment->bBatchRunning || Experiment->bLoading)))
  for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) if (It->bPowered) It->TogglePower();
 NextAutomaticOrderAt=GetWorld()->GetTimeSeconds()+OrderIntervalSeconds;
 OperationsStatus=bManualOperations ? TEXT("모든 작업 수동 처리") : TEXT("배치 준비 · 지게차 운행을 시작하면 창고 자동 운영");
}
void AWarehouseFocusDirector::SetManualOperations(bool bManual,bool bPersist)
{
 if (auto* Settings=UWarehouseOperationsSettings::Find(this)) Settings->SetManualOperations(bManual,bPersist);
 if (bManualOperations==bManual) return;
 bManualOperations=bManual;
 if (bManual)
 {
  if (auto* Shipping=Existing<AWarehouseShippingSystem>(this)) Shipping->SetContinuousOperations(false);
  // Nothing accepted by shipping is cancelled. Only unsubmitted automatic orders are removed.
  PendingOrders.RemoveAll([](const auto& O){return !O.bUserRequested;});
  OperationsStatus=TEXT("모든 작업 수동 처리 · 이미 접수된 운반·배송은 계속 진행");
 }
 else
 {
  NextAutomaticOrderAt=GetWorld()->GetTimeSeconds()+OrderIntervalSeconds;
  StoredSince.Reset();
  OperationsStatus=TEXT("NPC 자동 운영 재개 · 현재 구역·설비 확인 중");
 }
 PendingOutboundCount=PendingOrders.Num();LastMessage=OperationsStatus;
}
bool AWarehouseFocusDirector::Paused(FString& Reason) const
{
 if (const auto* Editor=Existing<AWarehouseLayoutEditor>(this);Editor && Editor->bEditing)
 { Reason=TEXT("재배치 중 · NPC의 새 작업 접수 대기");return true; }
 const auto* GI=GetGameInstance();
 const auto* Experiment=GI ? GI->GetSubsystem<UWarehouseExperimentSubsystem>() : nullptr;
 if (Experiment && (Experiment->bBatchRunning || Experiment->bLoading))
 { Reason=TEXT("배치 불러오기·반복 실험 중 · 자동 운영 대기");return true; }
 return false;
}
bool AWarehouseFocusDirector::SetFleetRunning(bool bRunning)
{
 FString Reason;if (Paused(Reason)) { LastMessage=Reason;return false; }
 auto* Computer=Existing<AWarehouseComputer>(this);
 if (!HasAuthority() || !Computer || !Computer->CanUse()) {LastMessage=TEXT("사용 가능한 관제 컴퓨터가 없습니다.");return false;}
 if (auto* Stage=AWarehousePlayStage::Find(this);bRunning && Stage && Stage->StageIndex>0 && !Stage->bOperating && !Stage->bCompleted)
 {
  // Campaign owns its finite arrivals, prices and unlock rules. Do not start the warehouse shipping generator.
  if (!Stage->StartOperation()) {LastMessage=Stage->Status;return false;}
 }
 Computer->RefreshState();
 // SetVehicleRunning refreshes Fleet; copy it before iterating and never take control of a remote vehicle.
 const auto Fleet=Computer->Fleet;
 int32 Changed=0,Skipped=0;
 for (AWarehouseForklift* Vehicle:Fleet)
 {
  if (!IsValid(Vehicle) || !Computer->SetVehicleRunning(Vehicle,bRunning)) ++Skipped;
  else ++Changed;
 }
 if (!bRunning && !FleetRunning()) if (auto* Shipping=Existing<AWarehouseShippingSystem>(this)) Shipping->SetContinuousOperations(false);
 LastMessage=FString::Printf(TEXT("지게차 %d대 %s · 원격 조작/점검 필요 등 제외 %d대"),Changed,bRunning ? TEXT("운행 요청") : TEXT("정지 요청"),Skipped);
 return Changed>0;
}
bool AWarehouseFocusDirector::FleetRunning() const
{
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) if (It->bPowered) return true;
 return false;
}

bool AWarehouseFocusDirector::Eligible(AWarehousePallet* Pallet,AWarehouseShippingSystem* Shipping,AWarehouseDamageSystem* Strength,FString& Reason) const
{
 auto Fail=[&](const TCHAR* Text){Reason=Text;return false;};
 if (!IsValid(Pallet) || Pallet->GetWorld()!=GetWorld()) return Fail(TEXT("해당 팔레트가 맵에 없습니다."));
 if (Pallet->ActorHasTag(TEXT("LogisticsInTransit"))) return Fail(TEXT("이미 트럭 인계·운송 중인 팔레트입니다."));
 if (Pallet->ReceiptState!=EWarehouseReceiptState::Confirmed || Pallet->ReceivingState!=EWarehouseReceivingState::Accepted || Pallet->bWrapping)
  return Fail(TEXT("입고 검수·랩핑을 완료한 팔레트만 출고할 수 있습니다."));
 if (!Shipping->GetPalletShipment(Pallet).IsEmpty()) return Fail(TEXT("이미 배송편에 배정된 팔레트입니다."));
 if (Strength->HasFailed(Pallet)) return Fail(TEXT("손상된 팔레트는 NPC의 품질 격리 대상입니다."));
 const auto Cargo=Contents(Pallet,Strength);
 if (Cargo.IsEmpty()) return Fail(TEXT("빈 팔레트에는 출고할 물품이 없습니다."));
 if (Pallet->ActorHasTag(TEXT("PlayerHeldCargo"))) return Fail(TEXT("사람이 들고 있는 팔레트입니다."));
 for (auto* C:Cargo)
 {
  if (Strength->HasFailed(C)) return Fail(TEXT("손상된 상품은 NPC의 품질 격리 대상입니다."));
  if (C->ActorHasTag(TEXT("PlayerHeldCargo")) || C->ActorHasTag(TEXT("LogisticsInTransit"))) return Fail(TEXT("물류가 운반 중입니다."));
 }
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
  if (It->GetSupportedPallet()==Pallet || It->ActiveJob.Pallet==Pallet || It->PendingJobs.ContainsByPredicate([&](const auto& J){return J.Pallet==Pallet;}))
   return Fail(TEXT("지게차가 운반 중이거나 이미 운반을 예약했습니다."));
 return true;
}
bool AWarehouseFocusDirector::RequestOutbound(AWarehousePallet* Pallet)
{
 if (AWarehousePlayStage::IsPlay(this)) {LastMessage=TEXT("이 스테이지의 목표는 입고 팔레트의 랙 운반입니다. 별도 출고 주문은 생성하지 않습니다.");return false;}
 if (!HasAuthority() || bManualOperations) {LastMessage=TEXT("자동 운영 모드에서 사용할 수 있는 명령입니다.");return false;}
 FString Reason;if (Paused(Reason)) {LastMessage=Reason;return false;}
 if (PendingOrders.ContainsByPredicate([&](const auto& O){return O.Pallet==Pallet;}))
 {LastMessage=TEXT("이미 출고 대기 목록에 있는 팔레트입니다.");return false;}
 auto* Shipping=Existing<AWarehouseShippingSystem>(this);auto* Strength=Existing<AWarehouseDamageSystem>(this);
 if (!Shipping || !Strength) {LastMessage=TEXT("배송·하중 관리가 없어 출고를 접수할 수 없습니다.");return false;}
 Strength->EnsureSupportSnapshot();
 if (!Eligible(Pallet,Shipping,Strength,Reason)) {LastMessage=Reason;return false;}
 if (PendingOrders.Num()>=SupplyLimit) {LastMessage=TEXT("출고 대기 목록이 가득 찼습니다. 진행 중인 운반을 기다려 주세요.");return false;}
 const int32 FirstAutomatic=PendingOrders.IndexOfByPredicate([](const auto& O){return !O.bUserRequested;});
 FPendingOrder Order;Order.Pallet=Pallet;Order.bUserRequested=true;
 PendingOrders.Insert(Order,FirstAutomatic==INDEX_NONE ? PendingOrders.Num() : FirstAutomatic);
 PendingOutboundCount=PendingOrders.Num();
 if (FleetRunning()) DispatchPending(Shipping,Strength);
 else DispatchStatus=TEXT("지게차 운행 시작 대기");
 LastMessage=PendingOrders.ContainsByPredicate([&](const auto& O){return O.Pallet==Pallet;}) ? TEXT("출고 요청 접수 · ")+DispatchStatus : TEXT("출고 요청을 접수했습니다. 지게차와 NPC가 운반·상차를 진행합니다.");
 return true;
}

bool AWarehouseFocusDirector::IsOutboundPending(AWarehousePallet* Pallet) const
{
 return IsValid(Pallet) && PendingOrders.ContainsByPredicate([&](const auto& Order){return Order.Pallet==Pallet;});
}

bool AWarehouseFocusDirector::IsInboundTruck(AWarehouseLogisticsTruck* Truck,const AWarehouseShippingSystem* Shipping) const
{
 if (!IsValid(Truck)) return false;
 if (Truck==InboundTruck || Truck==Shipping->RepeatingInboundTruck || !Truck->InitialPallets.IsEmpty()) return true;
 return Shipping->Shipments.ContainsByPredicate([&](const auto& S){return S.Truck==Truck && S.Direction==EWarehouseShipmentDirection::Inbound;});
}
void AWarehouseFocusDirector::UpdateSupply(AWarehouseShippingSystem* Shipping)
{
 const auto* Zone=Shipping->Zones.FindByPredicate([](const auto& Z){return Z.bInbound && IsValid(Z.Anchor) && !Z.Slots.IsEmpty();});
 if (!Zone) {Shipping->SetContinuousOperations(false);SupplyStatus=TEXT("입고 구역 없음 · NPC 공급 대기");return;}
 if (!InboundTruck.IsValid() || !Shipping->Trucks.Contains(InboundTruck.Get()))
 {
  InboundTruck.Reset();
  for (AWarehouseLogisticsTruck* Truck:Shipping->Trucks) if (IsInboundTruck(Truck,Shipping)) {InboundTruck=Truck;break;}
  // Newly added, unassigned empty trucks can acquire a role; never repurpose an outbound visit.
  if (!InboundTruck.IsValid()) for (AWarehouseLogisticsTruck* Truck:Shipping->Trucks)
   if (IsValid(Truck) && Truck->IsReady() && !Busy(Shipping,Truck) && Truck->ManifestPallets.IsEmpty() &&
       !Shipping->Shipments.ContainsByPredicate([&](const auto& S){return S.Truck==Truck && S.Direction==EWarehouseShipmentDirection::Outbound;}))
   {InboundTruck=Truck;break;}
 }
 if (!InboundTruck.IsValid()) {Shipping->SetContinuousOperations(false);SupplyStatus=TEXT("입고 트럭 없음 · NPC 공급 대기");return;}
 Shipping->MaxGeneratedUnits=FMath::Clamp(Shipping->MaxGeneratedUnits,1,SupplyLimit);
 if (Shipping->RepeatingInboundTruck!=InboundTruck.Get())
 {Shipping->SetContinuousOperations(false);Shipping->RepeatingInboundTruck=InboundTruck.Get();}
 if (!Shipping->bContinuousOperations && Shipping->GeneratedUnits<Shipping->MaxGeneratedUnits) Shipping->SetContinuousOperations(true);
 // Authored inbound stock is still handed over when the additional-supply limit has been reached.
 for (AWarehouseLogisticsTruck* Truck:Shipping->Trucks)
  if (IsInboundTruck(Truck,Shipping) && Truck->IsReady() && Truck->IsManifestReady() && !Truck->ManifestPallets.IsEmpty() && !Busy(Shipping,Truck))
   Shipping->StartInbound(Truck,Zone->ZoneId);
 SupplyStatus=Shipping->GeneratedUnits>=Shipping->MaxGeneratedUnits ? TEXT("이번 실행의 추가 입고 공급 완료") : Shipping->OperationsStatus;
}

bool AWarehouseFocusDirector::DispatchPending(AWarehouseShippingSystem* Shipping,AWarehouseDamageSystem* Strength)
{
 PendingOrders.RemoveAll([&](const auto& O)
 {
  auto* P=O.Pallet.Get();
  if (!P || !Shipping->GetPalletShipment(P).IsEmpty()) return true;
  const auto Cargo=Contents(P,Strength);
  if (P->ActorHasTag(TEXT("LogisticsInTransit")) || Strength->HasFailed(P) || P->ReceivingState==EWarehouseReceivingState::Quarantined || P->ReceiptState==EWarehouseReceiptState::WrapTorn ||
      Cargo.IsEmpty() || Cargo.ContainsByPredicate([&](auto* C){return !IsValid(C) || Strength->HasFailed(C);}))
  {LastMessage=TEXT("출고 대기 요청 취소 · 물류가 보관·운송·품질 격리 상태이거나 비어 있습니다.");return true;}
  return false;
 });
 PendingOutboundCount=PendingOrders.Num();
 if (PendingOrders.IsEmpty()) {DispatchStatus=TEXT("출고 요청 대기");return false;}
 const auto* Zone=Shipping->Zones.FindByPredicate([](const auto& Z){return !Z.bInbound && IsValid(Z.Anchor) && !Z.Slots.IsEmpty();});
 if (!Zone) {DispatchStatus=TEXT("출고 구역 없음 · 요청 보관 중");return false;}
 AWarehouseLogisticsTruck* Available=nullptr;
 for (AWarehouseLogisticsTruck* Truck:Shipping->Trucks)
  if (IsValid(Truck) && !IsInboundTruck(Truck,Shipping) && Truck->IsReady() && Truck->IsManifestReady() && Truck->ManifestPallets.IsEmpty() && !Busy(Shipping,Truck))
  {Available=Truck;break;}
 if (!Available) {DispatchStatus=TEXT("사용 가능한 출고 트럭을 기다립니다. 입고 전용 트럭은 사용하지 않습니다.");return false;}
 int32 Index=INDEX_NONE;FString Reason;
 for (int32 I=0;I<PendingOrders.Num();++I) if (Eligible(PendingOrders[I].Pallet.Get(),Shipping,Strength,Reason)) {Index=I;break;}
 if (Index==INDEX_NONE) {DispatchStatus=Reason;return false;}
 auto* Pallet=PendingOrders[Index].Pallet.Get();
 const bool Automatic=!PendingOrders[Index].bUserRequested;
 if (!Shipping->CreateOutbound(Automatic ? TEXT("NPC 출고 주문") : TEXT("지게차 운반 요청"),{Pallet},Zone->ZoneId,Available))
 {DispatchStatus=Shipping->LastMessage;return false;}
 PendingOrders.RemoveAt(Index);PendingOutboundCount=PendingOrders.Num();
 if (Automatic) ++AutomaticOrderCount;
 NextAutomaticOrderAt=GetWorld()->GetTimeSeconds()+OrderIntervalSeconds;
 DispatchStatus=TEXT("지게차 운반 배정 · 도착 확인 후 NPC가 검품·상차합니다.");return true;
}
void AWarehouseFocusDirector::UpdateOutbound(AWarehouseShippingSystem* Shipping,AWarehouseDamageSystem* Strength)
{
 const double Now=GetWorld()->GetTimeSeconds();
 for (auto It=ReturnAt.CreateIterator();It;++It) if (!It.Key().IsValid()) It.RemoveCurrent();
 for (AWarehouseLogisticsTruck* Truck:Shipping->Trucks)
 {
  if (!IsValid(Truck) || IsInboundTruck(Truck,Shipping) || Busy(Shipping,Truck)) continue;
  if (Truck->State==EWarehouseTruckState::Departed)
  {
   auto* Due=ReturnAt.Find(Truck);
   if (!Due) ReturnAt.Add(Truck,Now+FMath::Max(5.f,Shipping->VisitIntervalSeconds));
   else if (Now>=*Due && Truck->ReturnForNextVisit()) ReturnAt.Remove(Truck);
  }
  else ReturnAt.Remove(Truck);
 }
 DispatchPending(Shipping,Strength);
 // Actual support contacts identify rack storage, not display names or fixed map coordinates.
 TSet<AWarehousePallet*> Stock;
 for (const auto& Rack:Strength->Objects)
  if (Rack.Failure==EWarehouseFailure::Rack && !Rack.bFailed && !Rack.Members.IsEmpty() && IsValid(Rack.Members[0]))
   for (auto* A:Strength->GetSupportedActors(Rack.Members[0],false)) if (auto* P=Cast<AWarehousePallet>(A))
   {
    FString Reason;if (Eligible(P,Shipping,Strength,Reason)) Stock.Add(P);
   }
 for (auto It=StoredSince.CreateIterator();It;++It) if (!Stock.Contains(It.Key().Get())) It.RemoveCurrent();
 for (auto* P:Stock) if (!StoredSince.Contains(P)) StoredSince.Add(P,Now);
 if (Stock.Num()<=KeepRackStock || Now<NextAutomaticOrderAt || !PendingOrders.IsEmpty()) return;
 AWarehousePallet* Oldest=nullptr;double OldestTime=Now;
 for (auto* P:Stock)
 {
  const double Since=StoredSince.FindRef(P);
  if (Now-Since<OrderIntervalSeconds) continue;
  if (!Oldest || Since<OldestTime || (Since==OldestTime && P->GetName()<Oldest->GetName())) {Oldest=P;OldestTime=Since;}
 }
 if (!Oldest) return;
 FPendingOrder Order;Order.Pallet=Oldest;PendingOrders.Add(Order);PendingOutboundCount=PendingOrders.Num();
 NextAutomaticOrderAt=Now+OrderIntervalSeconds;
 DispatchPending(Shipping,Strength);
}

void AWarehouseFocusDirector::UpdateInspection(AWarehouseDamageSystem* Strength)
{
 QuarantinedCount=0;
 for (TActorIterator<AWarehousePallet> It(GetWorld());It;++It)
 {
  auto* P=*It;if (P->ActorHasTag(TEXT("LogisticsInTransit")) || P->ReceivingState!=EWarehouseReceivingState::Quarantined) continue;
  ++QuarantinedCount;
  if (Strength->HasFailed(P)) continue;
  const auto Cargo=Contents(P,Strength);
  if (Cargo.IsEmpty() || Cargo.ContainsByPredicate([&](auto* C){return !IsValid(C) || Strength->HasFailed(C);})) continue;
  // A clerk reconciles a manifest mismatch against the physical cartons, then requests the normal inspection again.
  // Real damage stays quarantined; this coordinator never repairs, deletes or clones freight.
  if (!P->ReceivingStatus.Contains(TEXT("[RECEIVING_COUNT]")) && !P->ReceivingStatus.Contains(TEXT("[RECEIVING_KIND]")) && !P->ReceivingStatus.Contains(TEXT("[RECEIVING_MANIFEST]"))) continue;
  for (TActorIterator<AWarehouseInboundZone> Zone(GetWorld());Zone;++Zone) if (Zone->ContainsPallet(P))
  {
   TMap<FString,int32> Manifest;for (auto* C:Cargo) ++Manifest.FindOrAdd(C->CargoKind.ToString().TrimStartAndEnd());
   if (Zone->SetExpectedManifest(P,Cargo.Num(),Manifest)) Zone->RequestReinspection(P);
   break;
  }
 }
}
void AWarehouseFocusDirector::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if (!HasAuthority() || bManualOperations) return;
 if (Paused(OperationsStatus)) return;
 if (const auto* Stage=AWarehousePlayStage::Find(this);Stage && Stage->StageIndex>0)
 {
  bWorkflowStarted|=Stage->bOperating || Stage->bCompleted;
  OperationsStatus=Stage->bCompleted ? TEXT("스테이지 목표 완료 · 다음 스테이지를 선택하세요.") :
   Stage->bOperating ? (FleetRunning() ? TEXT("NPC 입고 운영 중 · 지게차가 팔레트를 랙으로 운반합니다.") : TEXT("지게차 정지 · 운행을 재개하면 랙 운반을 계속합니다.")) :
   TEXT("배치 준비 · 지게차 운행을 시작하면 스테이지 물류가 입고됩니다.");
  return;
 }
 auto* Shipping=Existing<AWarehouseShippingSystem>(this);auto* Strength=Existing<AWarehouseDamageSystem>(this);
 if (!Shipping || !Strength) {OperationsStatus=TEXT("배송·하중 관리 설비 없음 · 자동 운영 대기");return;}
 if (!FleetRunning())
 {
  if (Shipping->bContinuousOperations) Shipping->SetContinuousOperations(false);
  OperationsStatus=bWorkflowStarted ? TEXT("지게차 정지 · NPC의 새 입고·출고 접수 대기") : TEXT("배치 준비 · 지게차 운행을 시작하면 창고 자동 운영");
  return;
 }
 bWorkflowStarted=true;
 Strength->EnsureSupportSnapshot();
 UpdateInspection(Strength);UpdateSupply(Shipping);UpdateOutbound(Shipping,Strength);
 OperationsStatus=TEXT("NPC 자동 운영 · 플레이어는 지게차 명령·직접 조작·창고 배치에 집중");
}
FString AWarehouseFocusDirector::GetSummary() const
{
 if (const auto* Stage=AWarehousePlayStage::Find(this);Stage && Stage->StageIndex>0)
  return FString::Printf(TEXT("%s\n스테이지 %d · 랙 운반 %d / %d · 입고 %d / %d\n남은 예산 %s원 · 스테이지별 물량·해금·건물 규칙 유지\n%s"),
   *OperationsStatus,Stage->StageIndex,Stage->Delivered,Stage->DeliveryGoal,Stage->Arrived,Stage->DeliveryGoal,*FText::AsNumber(Stage->BudgetWon).ToString(),*Stage->Status);
 const auto* Shipping=Existing<AWarehouseShippingSystem>(this);
 int32 Active=0;FString ShippingStatus;bool HasWait=false,MissingFacility=false;
 if (Shipping) for (const auto& Shipment:Shipping->Shipments) if (Open(Shipment))
 {
  ++Active;
  if (!HasWait && !Shipment.WaitReason.IsEmpty()) {ShippingStatus=Shipment.WaitReason;HasWait=true;}
  else if (ShippingStatus.IsEmpty()) ShippingStatus=Shipment.Status;
  MissingFacility|=Shipment.WaitReason.Contains(TEXT("[NO_STAGING_ZONE]")) || Shipment.WaitReason.Contains(TEXT("[NO_TRUCK]"));
 }
 if (ShippingStatus.IsEmpty()) ShippingStatus=TEXT("접수 대기");
 if (MissingFacility) ShippingStatus+=TEXT(" · 원래 구역·차량을 복원하거나 모든 작업 수동 처리 설정에서 배송편을 확인하세요.");
 return FString::Printf(TEXT("%s\n%s\n%s\n진행 배송 %d건 · %s\n출고 대기 %d건 · 자동 주문 %d건 · 품질 격리 %d개\n추가 입고 %d / %d묶음 · 기존 재고와 진행 중 작업은 계속 처리"),
  *OperationsStatus,*SupplyStatus,*DispatchStatus,Active,*ShippingStatus,PendingOutboundCount,AutomaticOrderCount,QuarantinedCount,
  Shipping ? Shipping->GeneratedUnits : 0,Shipping ? Shipping->MaxGeneratedUnits : SupplyLimit);
}
