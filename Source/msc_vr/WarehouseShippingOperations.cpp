#include "WarehouseShippingSystem.h"
#include "WarehouseLogisticsTruck.h"
#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseLayoutEditor.h"
#include "WarehousePlayMode.h"
#include "WarehouseExperimentSubsystem.h"
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
 if (IsValid(RepeatingInboundTruck)) ActiveInboundTruck=RepeatingInboundTruck;
 if (!ActiveInboundTruck.IsValid())
 {
  // Prefer an authored inbound visit; no assumptions about truck names or dock numbers.
  for (AWarehouseLogisticsTruck* T:Trucks) if (IsValid(T) && !T->InitialPallets.IsEmpty()) { ActiveInboundTruck=T;break; }
  if (!ActiveInboundTruck.IsValid()) for (const auto& S:Shipments) if (S.Direction==EWarehouseShipmentDirection::Inbound && IsValid(S.Truck)) {ActiveInboundTruck=S.Truck;break;}
  if (!ActiveInboundTruck.IsValid()) for (AWarehouseLogisticsTruck* T:Trucks) if (IsValid(T) && T->IsReady() && T->ManifestPallets.IsEmpty()) {ActiveInboundTruck=T;break;}
 }
 if (!ActiveInboundTruck.IsValid() || !Trucks.Contains(ActiveInboundTruck.Get())) return Fail(TEXT("방문 가능한 입고 트럭이 없습니다. [NO_TRUCK]"));
 if (GeneratedUnits>=MaxGeneratedUnits) return Fail(TEXT("이번 실행의 생성 한도에 도달했습니다. 한도를 늘리거나 초기 배치를 다시 불러오세요."));
 bContinuousOperations=true;NextOperationsAt=0;OperationsStatus=TEXT("반복 입고 시작 · 도크와 입고 자리 확인");return true;
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
  if (NextVisitAt<0) NextVisitAt=Now+FMath::Max(5.f,VisitIntervalSeconds);
  if (Now<NextVisitAt) {OperationsStatus=FString::Printf(TEXT("다음 입고 방문까지 %.0f초"),NextVisitAt-Now);return;}
  if (!Truck->ReturnForNextVisit()) {OperationsStatus=Truck->Status;return;}
  NextVisitAt=-1;OperationsStatus=TEXT("다음 입고 트럭 도착 중");return;
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

bool AWarehouseShippingSystem::GenerateArrival(AWarehouseLogisticsTruck* Truck)
{
 auto* Strength=AWarehouseDamageSystem::Find(this);
 auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Warehouse/Cargo/BoxesPalletsPack/Meshes/SM_Carton_Intact_2.SM_Carton_Intact_2"));
 if (!Strength || !Mesh || Truck->LoadSlots.IsEmpty()) { OperationsStatus=TEXT("물류 모델·하중 관리·트럭 적재 자리를 확인하세요.");return false; }
 const FTransform Base=Truck->LoadSlots[0]*Truck->GetActorTransform();
 FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Pallet=GetWorld()->SpawnActor<AWarehousePallet>(Base.GetLocation(),Base.Rotator(),Spawn);if (!Pallet) return false;
 const auto Recipe=AWarehouseCargo::GenerateCargoRecipe(ScenarioSeed,GeneratedUnits,GeneratedUnits%AWarehouseCargo::GetCargoProfileCount(),FVector(75,65,65),1.f);
 if (!Recipe.bValid) {Pallet->Destroy();OperationsStatus=TEXT("입고 상품 생성 조건이 올바르지 않습니다.");return false;}
 const FVector Scale=Recipe.SizeCm/Mesh->GetBoundingBox().GetSize();
 const FBox PBox=AWarehouseDamageSystem::GetPhysicalBounds(Pallet);
 if (!PBox.IsValid) {Pallet->Destroy();OperationsStatus=TEXT("입고 팔레트의 크기를 확인할 수 없습니다.");return false;}
 const FVector Center(PBox.GetCenter().X,PBox.GetCenter().Y,PBox.Max.Z+.2+Recipe.SizeCm.Z*.5);
 const FVector Origin=Center-Base.GetRotation().RotateVector(Mesh->GetBoundingBox().GetCenter()*Scale);
 const FTransform Pose(Base.GetRotation(),Origin,Scale);
 auto* Cargo=GetWorld()->SpawnActorDeferred<AWarehouseCargo>(AWarehouseCargo::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if (!Cargo) {Pallet->Destroy();return false;}
 Cargo->SetCargoMesh(Mesh);
 if (!Cargo->ApplyCargoRecipe(Recipe)) {Cargo->Destroy();Pallet->Destroy();OperationsStatus=TEXT("입고 상자 생성 실패");return false;}
 Cargo->CargoId=FName(*FString::Printf(TEXT("ARR-%d-%06d-%s"),ScenarioSeed,GeneratedUnits+1,*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
 UGameplayStatics::FinishSpawningActor(Cargo,Pose);
 Pallet->ExpectedCartonCount=1;Pallet->ExpectedCargoByKind.Add(Recipe.ProductKind.ToString(),1);
 FWarehouseStrength P;P.Name=Pallet->GetName();P.Members={Pallet};P.MassKg=Pallet->PalletMassKg;P.RatedLoadKg=1500;P.PhysicsMeshes={Pallet->GetPalletBody()->GetStaticMesh()};Strength->RegisterLayoutProfile(P);
 FWarehouseStrength C;C.Name=Cargo->CargoId.ToString();C.Members={Cargo};C.MassKg=Recipe.GrossMassKg;C.RatedLoadKg=Recipe.bFragile ? 60.f:150.f;C.Failure=EWarehouseFailure::Crush;C.PhysicsMeshes={Mesh};Strength->RegisterLayoutProfile(C);
 if (!Truck->QueueArrivalManifest({Pallet}))
 { Strength->RemoveLayoutActors({Pallet,Cargo});Cargo->Destroy();Pallet->Destroy();OperationsStatus=Truck->Status;return false; }
 ++GeneratedUnits;return true;
}
