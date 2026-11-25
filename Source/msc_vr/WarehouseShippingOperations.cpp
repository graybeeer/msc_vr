#include "WarehouseShippingSystem.h"
#include "WarehouseLogisticsTruck.h"
#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseLayoutEditor.h"
#include "WarehousePlayMode.h"
#include "WarehouseExperimentSubsystem.h"
#include "WarehouseOperationsSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"

namespace
{
 bool ExperimentActive(const UWorld* World)
 {
  const auto* GI=World ? World->GetGameInstance() : nullptr;
  const auto* E=GI ? GI->GetSubsystem<UWarehouseExperimentSubsystem>() : nullptr;
  return E && (E->bBatchRunning || E->bLoading);
 }
 bool OpenShipment(const FWarehouseShipment& S)
 { return S.State!=EWarehouseShipmentState::Completed && S.State!=EWarehouseShipmentState::Cancelled; }
 bool CountPalletProducts(AWarehouseDamageSystem* Strength,AWarehousePallet* Pallet,TMap<FString,int32>& Quantities)
 {
  bool bHasCargo=false;
  for (auto* Actor:Strength->GetSupportedActors(Pallet,false)) if (const auto* Cargo=Cast<AWarehouseCargo>(Actor))
  { ++Quantities.FindOrAdd(Cargo->CargoKind.ToString());bHasCargo=true; }
  return bHasCargo;
 }
}

bool AWarehouseShippingSystem::CanPlanShipping() const
{
 const auto* Settings=UWarehouseOperationsSettings::Find(this);
 return HasAuthority() && Settings && Settings->IsManualOperations() && !AWarehousePlayStage::IsPlay(this) && !ExperimentActive(GetWorld());
}

bool AWarehouseShippingSystem::ScheduleShipping(const FString& Name,const FString& ZoneId,AWarehouseLogisticsTruck* Truck,
 int32 DelaySeconds,const TArray<AWarehousePallet*>& Pallets,int32 InboundUnits,int32 ProductIndex,const FString& ProductName)
{
 auto Fail=[&](const TCHAR* Why) { LastMessage=Why;return false; };
 if (!CanPlanShipping()) return Fail(TEXT("전체 수동 관제에서 입출고 일정을 등록하세요."));
 const auto* Zone=Zones.FindByPredicate([&](const auto& Z){return Z.ZoneId==ZoneId && IsValid(Z.Anchor);});
 if (!Zone || Zone->bInbound!=(InboundUnits>0)) return Fail(TEXT("입고·출고 역할에 맞는 구역을 선택하세요."));
 if (!IsValid(Truck) || !Trucks.Contains(Truck)) return Fail(TEXT("등록된 트럭을 선택하세요."));
 const int32 Units=Zone->bInbound ? InboundUnits : Pallets.Num();
 if (Units<1 || Units>FMath::Min(Zone->Slots.Num(),Truck->LoadSlots.Num())) return Fail(TEXT("팔레트 수가 트럭 또는 구역의 적재 자리를 초과합니다."));
 if (DelaySeconds<0 || DelaySeconds>86400) return Fail(TEXT("지금부터 0~86400 시뮬레이션 초를 입력하세요."));

 FWarehouseShippingPlan Plan;
 Plan.PlannedAt=GetWorld()->GetTimeSeconds()+DelaySeconds;
 Plan.Name=Name.TrimStartAndEnd().Left(80);Plan.ZoneId=ZoneId;Plan.Truck=Truck;
 if (Zone->bInbound)
 {
  if (!Pallets.IsEmpty() || ProductIndex<0 || ProductIndex>=AWarehouseCargo::GetCargoProfileCount()) return Fail(TEXT("입고할 품목을 선택하세요."));
  Plan.InboundUnits=Units;Plan.ProductIndex=ProductIndex;
  Plan.Products=ProductName.TrimStartAndEnd().Left(80);
  if (Plan.Products.IsEmpty()) Plan.Products=AWarehouseCargo::GenerateCargoRecipe(ScenarioSeed,0,ProductIndex,FVector(75,65,65)).ProductKind.ToString();
 }
 else
 {
  auto* Strength=AWarehouseDamageSystem::Find(this);
  if (!Strength) return Fail(TEXT("재고 하중 정보를 확인할 수 없습니다."));
  Strength->EnsureSupportSnapshot();
  TMap<FString,int32> Quantities;
  for (auto* Pallet:Pallets)
  {
   if (!IsValid(Pallet) || Pallet->GetWorld()!=GetWorld() || Plan.Pallets.Contains(Pallet)) return Fail(TEXT("유효하고 서로 다른 출고 팔레트를 선택하세요."));
   if (!GetPalletShipment(Pallet).IsEmpty() || ShippingPlans.ContainsByPredicate([&](const auto& P){return P.ShipmentId.IsEmpty() && P.Pallets.Contains(Pallet);}))
    return Fail(TEXT("이미 배송편이나 일정에 지정된 팔레트입니다."));
   if (Pallet->ReceiptState!=EWarehouseReceiptState::Confirmed || Pallet->ReceivingState!=EWarehouseReceivingState::Accepted)
    return Fail(TEXT("검수·랩핑이 완료된 팔레트만 출고할 수 있습니다."));
   if (!CountPalletProducts(Strength,Pallet,Quantities)) return Fail(TEXT("빈 팔레트는 출고 일정에 넣을 수 없습니다."));
   Plan.Pallets.Add(Pallet);
  }
  TArray<FString> Lines;
  for (const auto& Pair:Quantities) Lines.Add(FString::Printf(TEXT("%s %d상자"),*Pair.Key,Pair.Value));
  Lines.Sort();Plan.Products=FString::Join(Lines,TEXT(", "));
  Plan.OutboundQuantities=MoveTemp(Quantities);
 }
 Plan.Id=NextShippingPlan++;
 if (Plan.Name.IsEmpty()) Plan.Name=FString::Printf(TEXT("%s 일정 %d"),Zone->bInbound ? TEXT("입고") : TEXT("출고"),Plan.Id);
 Plan.Status=TEXT("예정");
 ShippingPlans.Add(MoveTemp(Plan));
 ShippingPlans.StableSort([](const auto& A,const auto& B){return A.PlannedAt<B.PlannedAt;});
 LastMessage=TEXT("입출고 일정을 등록했습니다.");return true;
}

bool AWarehouseShippingSystem::CancelShippingPlan(int32 Id)
{
 if (CanPlanShipping() && ShippingPlans.RemoveAll([Id](const auto& P){return P.Id==Id && P.ShipmentId.IsEmpty() && !P.bManifestQueued;})>0)
 { LastMessage=TEXT("예정 일정을 취소했습니다.");return true; }
 LastMessage=TEXT("아직 화물 등록을 시작하지 않은 일정만 취소할 수 있습니다.");return false;
}

void AWarehouseShippingSystem::AdvanceShippingPlans()
{
 if (ShippingPlans.IsEmpty() || !CanPlanShipping()) return;
 if (auto* Layout=AWarehouseLayoutEditor::Find(this);Layout && Layout->bEditing) return;
 const double Now=GetWorld()->GetTimeSeconds();
 if (Now<NextShippingPlanCheck) return;
 NextShippingPlanCheck=Now+1;
 TSet<AWarehouseLogisticsTruck*> Checked;
 for (auto& Plan:ShippingPlans)
 {
  if (!Plan.ShipmentId.IsEmpty() || Plan.PlannedAt>Now) continue;
  auto* Truck=Plan.Truck.Get();
  if (!IsValid(Truck) || !Trucks.Contains(Truck)) { Plan.Status=TEXT("대기 · 지정 트럭이 없습니다.");continue; }
  if (!Plan.bManifestQueued && ShippingPlans.ContainsByPredicate([Truck](const auto& P){return P.Truck==Truck && P.bManifestQueued && P.ShipmentId.IsEmpty();}))
  { Plan.Status=TEXT("대기 · 트럭에 먼저 등록한 입고 화물 처리 중");continue; }
  if (Checked.Contains(Truck) || Shipments.ContainsByPredicate([&](const auto& S){return OpenShipment(S) && S.Truck==Truck;}))
  { Plan.Status=TEXT("대기 · 트럭의 앞선 배송편 처리 중");continue; }
  Checked.Add(Truck);
  const auto* Zone=Zones.FindByPredicate([&](const auto& Z){return Z.ZoneId==Plan.ZoneId && IsValid(Z.Anchor);});
  const int32 Units=Plan.InboundUnits>0 ? Plan.InboundUnits : Plan.Pallets.Num();
  if (!Zone || Zone->bInbound!=(Plan.InboundUnits>0) || Zone->Slots.Num()<Units || Truck->LoadSlots.Num()<Units)
  { Plan.Status=TEXT("대기 · 지정 구역과 적재 자리 확인 필요");continue; }
  if (Truck->State==EWarehouseTruckState::Departed)
  { Truck->ReturnForNextVisit();Plan.Status=Truck->Status;continue; }
  if (!Truck->IsReady() || !Truck->IsManifestReady()) { Plan.Status=Truck->Status;continue; }
  if (Plan.InboundUnits>0 && !Plan.bManifestQueued)
  {
   if (!Truck->ManifestPallets.IsEmpty()) { Plan.Status=TEXT("대기 · 트럭의 기존 입고 화물을 먼저 하역하세요.");continue; }
   if (Truck->DockServiceState!=EWarehouseDockServiceState::Awaiting)
   { if (Truck->PrepareDeparture() && Truck->IsDeparturePrepared()) Truck->Depart();Plan.Status=TEXT("대기 · 빈 트럭의 앞선 방문 정리 중");continue; }
   Plan.bManifestQueued=GenerateArrival(Truck,Plan.InboundUnits,Plan.ProductIndex,Plan.Id,Plan.Products);
   Plan.Status=Plan.bManifestQueued ? Truck->Status : OperationsStatus;
   continue;
  }
  TArray<AWarehousePallet*> Pallets;
  for (AWarehousePallet* Pallet:Plan.InboundUnits>0 ? Truck->ManifestPallets : Plan.Pallets) Pallets.Add(Pallet);
  if (Plan.InboundUnits==0)
  {
   auto* Strength=AWarehouseDamageSystem::Find(this);
   TMap<FString,int32> CurrentQuantities;
   if (Strength) Strength->EnsureSupportSnapshot();
   const bool bInvalidPallet=Pallets.ContainsByPredicate([&](auto* P){return !IsValid(P) || !Strength || !CountPalletProducts(Strength,P,CurrentQuantities);});
   bool bQuantitiesChanged=CurrentQuantities.Num()!=Plan.OutboundQuantities.Num();
   for (const auto& Pair:Plan.OutboundQuantities) bQuantitiesChanged|=CurrentQuantities.FindRef(Pair.Key)!=Pair.Value;
   if (bInvalidPallet || bQuantitiesChanged)
   { Plan.Status=TEXT("대기 · 출고 팔레트의 품목 또는 수량이 변경되었습니다.");continue; }
  }
  const bool Started=Create(Plan.Name,Pallets,Plan.ZoneId,Truck,Plan.InboundUnits>0 ? EWarehouseShipmentDirection::Inbound : EWarehouseShipmentDirection::Outbound,Plan.Id);
  Plan.Status=LastMessage;
  if (Started) Plan.ShipmentId=Shipments.Last().ShipmentId;
 }
}

bool AWarehouseShippingSystem::SetContinuousOperations(bool Enabled)
{
 if (!Enabled) { bContinuousOperations=false;OperationsStatus=TEXT("새 물량 유입 중지 · 접수된 작업은 계속 진행");return true; }
 auto Fail=[&](const TCHAR* Why) { OperationsStatus=Why;return false; };
 if (AWarehousePlayStage::IsPlay(this) || ExperimentActive(GetWorld())) return Fail(TEXT("플레이 모드·반복 실험에서는 추가 물량을 생성하지 않습니다."));
 if (!FMath::IsFinite(VisitIntervalSeconds) || VisitIntervalSeconds<5 || MaxGeneratedUnits<1 || MaxGeneratedUnits>1000)
  return Fail(TEXT("방문 간격은 5초 이상, 생성 한도는 1~1000묶음으로 설정하세요."));
 if (!Zones.ContainsByPredicate([](const auto& Z){return Z.bInbound && IsValid(Z.Anchor) && !Z.Slots.IsEmpty();}))
  return Fail(TEXT("입고 구역이 없습니다. [NO_STAGING_ZONE]"));
 const auto PreviousInboundTruck=ActiveInboundTruck;
 if (IsValid(RepeatingInboundTruck)) ActiveInboundTruck=RepeatingInboundTruck;
 if (!ActiveInboundTruck.IsValid())
 {
  // Prefer an authored inbound visit; no assumptions about truck names or dock numbers.
  for (AWarehouseLogisticsTruck* T:Trucks) if (IsValid(T) && !T->InitialPallets.IsEmpty()) { ActiveInboundTruck=T;break; }
  if (!ActiveInboundTruck.IsValid()) for (const auto& S:Shipments) if (S.Direction==EWarehouseShipmentDirection::Inbound && IsValid(S.Truck)) {ActiveInboundTruck=S.Truck;break;}
  if (!ActiveInboundTruck.IsValid()) for (AWarehouseLogisticsTruck* T:Trucks) if (IsValid(T) && T->IsReady() && T->ManifestPallets.IsEmpty()) {ActiveInboundTruck=T;break;}
 }
 if (!ActiveInboundTruck.IsValid() || !Trucks.Contains(ActiveInboundTruck.Get())) return Fail(TEXT("방문 가능한 입고 트럭이 없습니다. [NO_TRUCK]"));
 if (PreviousInboundTruck!=ActiveInboundTruck) InboundVisitWaitStartedAt=-1;
 if (GeneratedUnits>=MaxGeneratedUnits) return Fail(TEXT("이번 실행의 생성 한도에 도달했습니다. 한도를 늘리거나 초기 배치를 다시 불러오세요."));
 bContinuousOperations=true;NextOperationsAt=0;OperationsStatus=TEXT("반복 입고 시작 · 도크와 입고 자리 확인");return true;
}

float AWarehouseShippingSystem::GetTruckVisitInterval(bool bInbound) const
{
 static constexpr float IntervalScale[]={2.f,4.f/3.f,1.f,2.f/3.f,1.f/3.f};
 const auto* Settings=UWarehouseOperationsSettings::Find(this);
 const int32 Level=Settings ? Settings->GetTruckVisitLevel(bInbound) : 3;
 const float BaseInterval=FMath::IsFinite(VisitIntervalSeconds) ? FMath::Max(5.f,VisitIntervalSeconds) : 45.f;
 return BaseInterval*IntervalScale[Level-1];
}

FString AWarehouseShippingSystem::GetOperationsSummary() const
{
 int32 Checking=0,Waiting=0,Ready=0;
 for (const auto& S:Shipments) if (OpenShipment(S) && S.Direction==EWarehouseShipmentDirection::Outbound)
  for (const auto& L:S.Lines) if (!L.bLoaded)
  { if (L.bPrepared) ++Ready;else if (L.PreparationStartedAt>=0) ++Checking;else if (L.bArrived) ++Waiting; }
 return FString::Printf(TEXT("출고 검품·라벨: 대기 %d / 처리 %d / 준비 %d\n반복 입고: %s · 신규 팔레트 %d / %d\n%s"),
  Waiting,Checking,Ready,bContinuousOperations ? TEXT("켜짐") : TEXT("꺼짐"),GeneratedUnits,MaxGeneratedUnits,*OperationsStatus);
}

void AWarehouseShippingSystem::AdvanceOperations()
{
 if (!bContinuousOperations || !HasAuthority()) return;
 if (AWarehousePlayStage::IsPlay(this) || ExperimentActive(GetWorld())) { SetContinuousOperations(false);return; }
 if (auto* L=AWarehouseLayoutEditor::Find(this);L && L->bEditing) return;
 const double Now=GetWorld()->GetTimeSeconds();if (Now<NextOperationsAt) return;NextOperationsAt=Now+1;
 auto* Truck=ActiveInboundTruck.Get();
 if (!IsValid(Truck) || !Trucks.Contains(Truck)) { OperationsStatus=TEXT("입고 트럭이 삭제되었습니다. 반복 입고를 다시 설정하세요. [NO_TRUCK]");bContinuousOperations=false;return; }
 if (ShippingPlans.ContainsByPredicate([&](const auto& P){return P.Truck==Truck && P.ShipmentId.IsEmpty() && (P.bManifestQueued || (CanPlanShipping() && P.PlannedAt<=Now));}))
 { OperationsStatus=TEXT("수동 입출고 일정의 트럭 사용을 기다립니다.");return; }
 const auto* Zone=Zones.FindByPredicate([](const auto& Z){return Z.bInbound && IsValid(Z.Anchor) && !Z.Slots.IsEmpty();});
 if (!Zone) { OperationsStatus=TEXT("입고 구역이 없어 대기합니다. [NO_STAGING_ZONE]");return; }
 for (const auto& S:Shipments) if (OpenShipment(S) && S.Truck==Truck)
 { OperationsStatus=S.Status+TEXT(" · ")+S.WaitReason;return; }
 if (Truck->State==EWarehouseTruckState::Returning || Truck->State==EWarehouseTruckState::Departing)
 { OperationsStatus=Truck->Status;return; }
 if (!Truck->IsManifestReady()) { OperationsStatus=TEXT("입고 명세 등록 확인 · ")+Truck->Status;return; }
 if (Truck->State==EWarehouseTruckState::AtDock && !Truck->ManifestPallets.IsEmpty())
 { StartInbound(Truck,Zone->ZoneId);OperationsStatus=LastMessage;return; }
 if (GeneratedUnits>=MaxGeneratedUnits) { SetContinuousOperations(false);OperationsStatus=TEXT("설정한 신규 물량 공급 완료");return; }
 if (Truck->State==EWarehouseTruckState::Departed)
 {
  if (InboundVisitWaitStartedAt<0 || WaitingInboundVisit!=Truck->VisitNumber)
  { InboundVisitWaitStartedAt=Now;WaitingInboundVisit=Truck->VisitNumber; }
  const double Remaining=InboundVisitWaitStartedAt+GetTruckVisitInterval(true)-Now;
  if (Remaining>0) {OperationsStatus=FString::Printf(TEXT("다음 입고 방문까지 %.0f초"),Remaining);return;}
  if (!Truck->ReturnForNextVisit()) {OperationsStatus=Truck->Status;return;}
  InboundVisitWaitStartedAt=-1;OperationsStatus=TEXT("다음 입고 트럭 도착 중");return;
 }
 if (!Truck->IsReady()) { OperationsStatus=Truck->Status;return; }
 // A cancelled outbound visit can leave an empty dock secured. Complete that
 // visit before generating stock, instead of spawning and rejecting it every second.
 if (Truck->DockServiceState!=EWarehouseDockServiceState::Awaiting)
 {
  if (Truck->PrepareDeparture() && Truck->IsDeparturePrepared()) Truck->Depart();
  OperationsStatus=TEXT("빈 차량 방문 정리 · ")+Truck->DockServiceStatus+TEXT(" · ")+Truck->Status;return;
 }
 // One pallet per visit keeps the exercise bounded and prevents dock overbooking.
 if (Shipments.ContainsByPredicate([&](const auto& S){return OpenShipment(S) && S.ZoneId==Zone->ZoneId;}))
 {OperationsStatus=TEXT("다른 배송편의 입고 인계를 기다립니다.");return;}
 const FTransform Slot=Zone->Slots[0]*Zone->Anchor->GetActorTransform();
 FCollisionQueryParams Query(SCENE_QUERY_STAT(RepeatInboundSpace),false,this);
 if (GetWorld()->OverlapBlockingTestByChannel(Slot.TransformPosition(FVector(0,0,90)),Slot.GetRotation(),ECC_Visibility,FCollisionShape::MakeBox(FVector(56,56,85)),Query))
 {OperationsStatus=TEXT("입고 대기 자리 점유 · 새 물량 공급 대기");return;}
 if (GenerateArrival(Truck)) OperationsStatus=TEXT("새 방문 물류 명세를 등록했습니다.");
}

bool AWarehouseShippingSystem::GenerateArrival(AWarehouseLogisticsTruck* Truck,int32 Units,int32 ProductIndex,int32 PlanId,const FString& ProductName)
{
 auto* Strength=AWarehouseDamageSystem::Find(this);
 auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2.SM_Carton_Intact_2"));
 if (!Strength || !Mesh || Units<1 || Truck->LoadSlots.Num()<Units)
 { OperationsStatus=TEXT("물류 모델·하중 관리·트럭 적재 자리를 확인하세요.");return false; }
 TArray<AWarehousePallet*> Pallets;
 TArray<AActor*> Created;
 auto Fail=[&](const FString& Message)
 {
  Strength->RemoveLayoutActors(Created);
  for (auto* Actor:Created) Actor->Destroy();
  OperationsStatus=Message;return false;
 };
 FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 const FVector MeshSize=Mesh->GetBoundingBox().GetSize();
 float TotalMass=0;
 for (int32 Slot=0;Slot<Units;++Slot)
 {
  const int32 Item=PlanId ? Slot : GeneratedUnits;
  const int32 Profile=PlanId ? ProductIndex : Item%AWarehouseCargo::GetCargoProfileCount();
  auto Recipe=AWarehouseCargo::GenerateCargoRecipe(PlanId ? ScenarioSeed^PlanId : ScenarioSeed,Item,Profile,FVector(75,65,65),1.f);
  if (!Recipe.bValid) return Fail(TEXT("입고 상품 생성 조건이 올바르지 않습니다."));
  if (!ProductName.IsEmpty()) Recipe.ProductKind=FText::FromString(ProductName);
  const FTransform Base=Truck->LoadSlots[Slot]*Truck->GetActorTransform();
  auto* Pallet=GetWorld()->SpawnActor<AWarehousePallet>(Base.GetLocation(),Base.Rotator(),Spawn);
  if (!Pallet) return Fail(TEXT("입고 팔레트를 만들 수 없습니다."));
  Created.Add(Pallet);Pallets.Add(Pallet);
  TotalMass+=Pallet->PalletMassKg+Recipe.GrossMassKg;
  if (!FMath::IsFinite(Truck->RatedPayloadKg) || TotalMass>Truck->RatedPayloadKg)
   return Fail(TEXT("예정 물량이 트럭 허용 하중을 넘습니다."));
  const FVector Scale=Recipe.SizeCm/MeshSize;
  const FBox PBox=AWarehouseDamageSystem::GetPhysicalBounds(Pallet);
  if (!PBox.IsValid) return Fail(TEXT("입고 팔레트의 크기를 확인할 수 없습니다."));
  const FVector Center(PBox.GetCenter().X,PBox.GetCenter().Y,PBox.Max.Z+.2+Recipe.SizeCm.Z*.5);
  const FVector Origin=Center-Base.GetRotation().RotateVector(Mesh->GetBoundingBox().GetCenter()*Scale);
  const FTransform Pose(Base.GetRotation(),Origin,Scale);
  auto* Cargo=GetWorld()->SpawnActorDeferred<AWarehouseCargo>(AWarehouseCargo::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if (!Cargo) return Fail(TEXT("입고 상자를 만들 수 없습니다."));
  Created.Add(Cargo);Cargo->SetCargoMesh(Mesh);
  if (!Cargo->ApplyCargoRecipe(Recipe)) return Fail(TEXT("입고 상자 생성 실패"));
  Cargo->CargoId=FName(*FString::Printf(TEXT("ARR-%d-%06d-%s"),Recipe.DeliverySeed,Item+1,*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
  UGameplayStatics::FinishSpawningActor(Cargo,Pose);
  Pallet->ExpectedCartonCount=1;Pallet->ExpectedCargoByKind.Add(Recipe.ProductKind.ToString(),1);
  FWarehouseStrength P;P.Name=Pallet->GetName();P.Members={Pallet};P.MassKg=Pallet->PalletMassKg;P.RatedLoadKg=1500;P.PhysicsMeshes={Pallet->GetPalletBody()->GetStaticMesh()};Strength->RegisterLayoutProfile(P);
  FWarehouseStrength C;C.Name=Cargo->CargoId.ToString();C.Members={Cargo};C.MassKg=Recipe.GrossMassKg;C.RatedLoadKg=Recipe.bFragile ? 60.f:150.f;C.Failure=EWarehouseFailure::Crush;C.PhysicsMeshes={Mesh};Strength->RegisterLayoutProfile(C);
 }
 if (!Truck->QueueArrivalManifest(Pallets)) return Fail(Truck->Status);
 if (!PlanId) GeneratedUnits+=Units;
 return true;
}
