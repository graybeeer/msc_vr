#include "WarehouseForklift.h"
#include "WarehouseFleetCoordinator.h"
#include "WarehouseChargingStation.h"
#include "WarehouseWaitingBay.h"
#include "WarehouseElevator.h"
#include "WarehousePallet.h"
#include "WarehouseInboundZone.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"

void AWarehouseForklift::SetOperationError(FName Code,const FString& Message)
{
 LastErrorCode=Code;LastErrorMessage=Message;
 const FString Suffix=TEXT("[")+Code.ToString()+TEXT("]");
 SetStatus(Message.Contains(Suffix) ? Message : Message+TEXT(" ")+Suffix);
}
bool AWarehouseForklift::IsIdleForLayout() const
{
 return !IsRemoteControlled() && !bFloorTransfer && !bSupportingPallet && ActiveJob.JobId.IsEmpty() && PendingJobs.IsEmpty() &&
  (AIState==EWarehouseAIState::Off || AIState==EWarehouseAIState::Ready || AIState==EWarehouseAIState::Charging ||
   AIState==EWarehouseAIState::WaitingCharger || AIState==EWarehouseAIState::Paused);
}
bool AWarehouseForklift::CanAcceptNewWork() const
{
 return bAutonomousMode && bPowered && !IsRemoteControlled() && CheckSystems().IsEmpty() &&
  (!bChargePending || bOpportunityCharge) && BatteryPercent>ChargeBelowPercent;
}
FString AWarehouseForklift::ValidateJobResources(const FWarehouseWorkOrder& Job) const
{
 if (!IsValid(Job.Pallet) || Job.Pallet->GetWorld()!=GetWorld()) return TEXT("물류를 운반할 수 없다: 팔레트가 없습니다. [NO_PALLET]");
 if (Job.Pallet->ActorHasTag(TEXT("LogisticsInTransit"))) return TEXT("트럭 상하차 물류는 인계 후 운반할 수 있습니다. [TRUCK_HANDLING]");
 if (Job.Pallet->ReceiptState==EWarehouseReceiptState::WrapTorn) return TEXT("운반할 수 없다: 팔레트 랩이 파손되어 재검수가 필요합니다. [WRAP_TORN]");
 if (Job.Pallet->ReceiptState==EWarehouseReceiptState::Pending)
  for (TActorIterator<AWarehouseInboundZone> It(GetWorld());It;++It) if (It->ContainsPallet(Job.Pallet))
   return TEXT("운반할 수 없다: 입고 담당 NPC의 입고 확정을 기다리세요. [INBOUND_PENDING]");
 if (Job.bRequiresDestinationResource && !IsValid(Job.DestinationResource)) return TEXT("운반할 수 없다: 목적지 랙 또는 입출고 구역이 삭제되었습니다. [NO_DESTINATION_RESOURCE]");
 const auto Floor=[](double Z) { return FMath::FloorToInt((FMath::Max(0.,Z)+1.)/400.); };
 if ((Floor(GetActorLocation().Z)!=Floor(Job.Pallet->GetActorLocation().Z) ||
      Floor(Job.Pallet->GetActorLocation().Z)!=Floor(Job.Destination.GetLocation().Z)))
 {
  bool Found=false;
  for (TActorIterator<AWarehouseElevator> It(GetWorld());It;++It)
   if (It->FloorAtHeight(Job.Pallet->GetActorLocation().Z)!=INDEX_NONE && It->FloorAtHeight(Job.Destination.GetLocation().Z)!=INDEX_NONE) { Found=true;break; }
  if (!Found) return TEXT("지게차가 올라갈 수 없다: 해당 층을 연결하는 승강기가 없습니다. [NO_ELEVATOR]");
 }
 return FString();
}
AWarehouseChargingStation* AWarehouseFleetCoordinator::AllocateCharger(AWarehouseForklift* Vehicle)
{
 if (!IsValid(Vehicle) || Vehicle->GetWorld()!=GetWorld()) return nullptr;
 auto Reachable=[&](AWarehouseForklift* V,AWarehouseChargingStation* Station)
 {
  if (FMath::Abs(Station->GetActorLocation().Z-V->GetActorLocation().Z)<=100) return true;
  for (TActorIterator<AWarehouseElevator> Lift(GetWorld());Lift;++Lift)
   if (Lift->FloorAtHeight(Station->GetActorLocation().Z)!=INDEX_NONE && Lift->FloorAtHeight(V->GetActorLocation().Z)!=INDEX_NONE) return true;
  return false;
 };
 if (IsValid(Vehicle->ChargingStation) && Vehicle->ChargingStation->Occupant==Vehicle &&
     Vehicle->ChargingStation->bMainsPower && Vehicle->ChargingStation->PowerKw>0 && Vehicle->ChargingStation->Efficiency>0)
  return Vehicle->ChargingStation;
 // Existing reservations never get stolen. Unreserved requests prioritize
 // required charging, then the lower battery, then stable vehicle identity.
 const bool Required=Vehicle->BatteryPercent<=Vehicle->ChargeBelowPercent || !Vehicle->bOpportunityCharge;
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
 {
  if (*It==Vehicle || !It->bPowered || It->IsRemoteControlled() || !It->ActiveJob.JobId.IsEmpty() || It->bSupportingPallet ||
      (IsValid(It->ChargingStation) && It->ChargingStation->Occupant==*It) ||
      !(It->bChargePending || It->BatteryPercent<=It->ChargeBelowPercent)) continue;
  const bool OtherRequired=It->BatteryPercent<=It->ChargeBelowPercent || !It->bOpportunityCharge;
  bool HasReachable=false;for (TActorIterator<AWarehouseChargingStation> Station(GetWorld());Station;++Station)
   if (Station->bMainsPower && Station->PowerKw>0 && Station->Efficiency>0 && Reachable(*It,*Station)) { HasReachable=true;break; }
  if (!HasReachable) continue;
  if ((OtherRequired && !Required) || (OtherRequired==Required &&
      (It->BatteryPercent<Vehicle->BatteryPercent-.01f || (FMath::IsNearlyEqual(It->BatteryPercent,Vehicle->BatteryPercent,.01f) && It->GetName()<Vehicle->GetName())))) return nullptr;
 }
 AWarehouseChargingStation* Best=nullptr;double Cost=MAX_dbl;bool HasPowered=false,HasReachable=false;
 for (TActorIterator<AWarehouseChargingStation> It(GetWorld());It;++It)
 {
  if (!It->bMainsPower || It->PowerKw<=0 || It->Efficiency<=0 || (IsValid(It->Occupant) && It->Occupant!=Vehicle)) continue;
  HasPowered=true;if (!Reachable(Vehicle,*It)) continue;HasReachable=true;
  bool PhysicallyOccupied=false;
  for (TActorIterator<AWarehouseForklift> Other(GetWorld());Other;++Other)
   if (*Other!=Vehicle && FVector::Dist(Other->GetActorLocation(),It->GetActorLocation())<5.f)
   {
    PhysicallyOccupied=true;
    if (Other->bPowered && Other->bAutonomousMode && Other->IsIdleForLayout() && Other->BatteryPercent>Other->ChargeBelowPercent && !Other->bChargePending)
     Other->PlanIdleParking(true); // Vacate the shared dock for this request.
   }
  if (PhysicallyOccupied) continue;
  const double Distance=FVector::DistSquared(It->GetActorLocation(),Vehicle->GetActorLocation());
  if (Distance<Cost) { Cost=Distance;Best=*It; }
 }
 if (!Best || !Best->TryReserve(Vehicle))
 {
  if (HasPowered && !HasReachable) Vehicle->SetOperationError(TEXT("NO_ELEVATOR"),TEXT("충전할 수 없다: 충전기 층으로 올라갈 수 있는 승강기가 없습니다."));
  return nullptr;
 }
 if (IsValid(Vehicle->WaitingBay)) Vehicle->WaitingBay->Release(Vehicle);
 return Best;
}
bool AWarehouseForklift::TryAcquireCharger()
{
 // This query reports its current resources, rather than retaining an earlier
 // work-order elevator error or a charger that has since been removed.
 LastErrorCode=NAME_None;LastErrorMessage.Reset();
 if (!FleetCoordinator) FleetCoordinator=AWarehouseFleetCoordinator::Find(this);
 auto* Station=FleetCoordinator ? FleetCoordinator->AllocateCharger(this) : nullptr;
 if (!Station) return false;
 if (IsValid(ChargingStation) && ChargingStation!=Station) ChargingStation->Release(this);
 ChargingStation=Station;LastErrorCode=NAME_None;LastErrorMessage.Reset();return true;
}
bool AWarehouseForklift::PlanIdleParking(bool WaitingForCharge)
{
 if (!bIdleParkingEnabled || bSupportingPallet || !ActiveJob.JobId.IsEmpty() || IsRemoteControlled()) return false;
 auto FindBay=[&]()
 {
  if (IsValid(WaitingBay) && WaitingBay->TryReserve(this)) return WaitingBay.Get();
  AWarehouseWaitingBay* Best=nullptr;double Cost=MAX_dbl;
  for (TActorIterator<AWarehouseWaitingBay> It(GetWorld());It;++It)
  {
   if (IsValid(It->Occupant) && It->Occupant!=this) continue;
   if (FMath::Abs(It->GetActorLocation().Z-GetActorLocation().Z)>5) continue;
   const double Distance=FVector::DistSquared(It->GetActorLocation(),GetActorLocation());
   if (Distance<Cost) { Best=*It;Cost=Distance; }
  }
  return Best && Best->TryReserve(this) ? Best : nullptr;
 };
 auto* Bay=FindBay();
 if (!Bay)
 {
  if (IsValid(ChargingStation) && ChargingStation->IsDocked(this))
  {
   if (!WaitingForCharge)
   { if (LastErrorCode==TEXT("NO_WAITING_BAY")) { LastErrorCode=NAME_None;LastErrorMessage.Reset(); }return false; }
   ChargeExitPose=ChargingStation->GetActorTransform();ChargeExitPose.AddToTranslation(ChargingStation->GetActorForwardVector()*120.f);
   TransitionAI(EWarehouseAIState::LeaveCharger,TEXT("다른 차량의 충전을 위해 충전 대기 자리를 비웁니다."));return true;
  }
  if (!WaitingForCharge && BatteryPercent>ChargeBelowPercent)
  {
   bOpportunityCharge=true; // Idle parking never outranks required charging.
   if (TryAcquireCharger())
   {
    bIdleChargeParking=true;bChargePending=true;
    TransitionAI(EWarehouseAIState::PlanCharge,TEXT("일반 대기 자리 사용 중 · 빈 충전 자리로 대기 이동"));return true;
   }
   bOpportunityCharge=false;
  }
  if (LastErrorCode.IsNone()) SetOperationError(TEXT("NO_WAITING_BAY"),TEXT("대기할 일반 대기 자리가 없습니다. 현재 위치에서 안전 정지합니다."));
  return false;
 }
 WaitingBay=Bay;IdlePose=Bay->GetActorTransform();
 if (!WaitingForCharge && LastErrorCode==TEXT("NO_WAITING_BAY")) { LastErrorCode=NAME_None;LastErrorMessage.Reset(); }
 if (Bay->IsParked(this)) return false;
 if (IsValid(ChargingStation) && FVector::Dist(GetActorLocation(),ChargingStation->GetActorLocation())<5.f)
 {
  ChargeExitPose=ChargingStation->GetActorTransform();ChargeExitPose.AddToTranslation(ChargingStation->GetActorForwardVector()*120.f);
  TransitionAI(EWarehouseAIState::LeaveCharger,TEXT("충전 자리를 비우고 일반 대기 자리로 이동합니다."));return true;
 }
 TransitionAI(EWarehouseAIState::PlanParking,WaitingForCharge ? LastErrorMessage+TEXT(" · 일반 대기 자리로 이동") : TEXT("작업 없음 · 일반 대기 자리로 이동"));return true;
}
bool AWarehouseForklift::AdvanceIdleParking(float Dt)
{
 const bool Parking=AIState==EWarehouseAIState::PlanParking || AIState==EWarehouseAIState::NavigateParking || AIState==EWarehouseAIState::AlignParking;
 if (!Parking && AIState!=EWarehouseAIState::WaitingCharger) return false;
 if (!PendingJobs.IsEmpty() && BatteryPercent>ChargeBelowPercent && !bChargeToFull)
 {
  if (IsValid(WaitingBay)) WaitingBay->Release(this);
  if (bOpportunityCharge) { bOpportunityCharge=false;bChargePending=false;if (IsValid(ChargingStation)) ChargingStation->Release(this); }
  TransitionAI(EWarehouseAIState::Ready,TEXT("새 작업 수신 · 대기 이동을 취소하고 작업 위치로 이동"));return true;
 }
 if (Parking && !IsValid(WaitingBay))
 { SetOperationError(TEXT("NO_WAITING_BAY"),TEXT("대기할 수 없다: 지정 대기 자리가 삭제되었습니다."));TransitionAI(bChargePending ? EWarehouseAIState::WaitingCharger : EWarehouseAIState::Ready,Status);return true; }
 if (AIState==EWarehouseAIState::WaitingCharger)
 {
  BrakeDrive();const double Now=GetWorld()->GetTimeSeconds();if (Now<NextResourceCheck) return true;NextResourceCheck=Now+1.;
  if (TryAcquireCharger()) { TransitionAI(EWarehouseAIState::PlanCharge,TEXT("충전 자리를 예약했습니다."));return true; }
  bool HasStation=false;for (TActorIterator<AWarehouseChargingStation> It(GetWorld());It;++It) { HasStation=true;break; }
  if (LastErrorCode!=TEXT("NO_ELEVATOR")) SetOperationError(HasStation ? FName(TEXT("CHARGERS_BUSY")) : FName(TEXT("NO_CHARGER")),
   HasStation ? TEXT("충전할 수 없다: 충전소가 모두 사용 중이거나 전원이 꺼져 있습니다. 대기 후 다시 확인합니다.") : TEXT("충전할 수 없다: 충전기가 없습니다. 충전기를 추가하면 다시 확인합니다."));
  PlanIdleParking(true);return true;
 }
 if (AIState==EWarehouseAIState::PlanParking)
 {
  const double Now=GetWorld()->GetTimeSeconds();if (Now<NextResourceCheck) return true;NextResourceCheck=Now+1.;
  IdlePose=WaitingBay->GetActorTransform();
  if (BeginFloorTransfer(IdlePose.GetLocation().Z,EWarehouseAIState::PlanParking)) return true;
  if (PlanAutonomousRoute(IdlePose))
  {
   if (LastErrorCode==TEXT("NO_PARKING_ROUTE")) { LastErrorCode=NAME_None;LastErrorMessage.Reset(); }
   TransitionAI(EWarehouseAIState::NavigateParking,TEXT("일반 대기 자리로 주행 중"));bRouteStarted=true;
  }
  else
  {
   if (bChargePending && !LastErrorMessage.IsEmpty())
   {
    // Keep the charging failure as the primary resource error. A blocked
    // secondary parking route must not hide that the charger is absent.
    TransitionAI(EWarehouseAIState::WaitingCharger,LastErrorMessage+TEXT(" · 대기 자리 경로도 막혀 현재 위치에서 대기"));
    NextResourceCheck=Now+1.;
   }
   else { SetOperationError(TEXT("NO_PARKING_ROUTE"),TEXT("대기 자리로 이동할 수 없다: 경로가 막혔습니다."));BrakeDrive(); }
  }
  return true;
 }
 if (AIState==EWarehouseAIState::NavigateParking)
 {
  // A bay is a marked area, not an electrical contact. Stop in its permitted
  // pose before the generic route endpoint starts another steering pivot.
  if (WaitingBay->IsParked(this) && FMath::Abs(CurrentSpeedCm)<10.f)
  {
   TransitionAI(bChargePending ? EWarehouseAIState::WaitingCharger : EWarehouseAIState::Ready,
    bChargePending ? TEXT("일반 대기 자리에서 충전소 예약을 기다립니다.") : TEXT("일반 대기 자리에서 다음 작업 대기"));return true;
  }
  if (!bRouteStarted)
  { if (!PlanAutonomousRoute(IdlePose)) { BrakeDrive();return true; }bRouteStarted=true; }
  if (FollowRoute(EmptyTravelSpeedCm,Dt)) TransitionAI(EWarehouseAIState::AlignParking,TEXT("대기 자리 정렬 중"));
  return true;
 }
 if (MovePrecise(IdlePose.GetLocation(),15,Dt))
  TransitionAI(bChargePending ? EWarehouseAIState::WaitingCharger : EWarehouseAIState::Ready,
   bChargePending ? TEXT("일반 대기 자리에서 충전소 예약을 기다립니다.") : TEXT("일반 대기 자리에서 다음 작업 대기"));
 return true;
}
