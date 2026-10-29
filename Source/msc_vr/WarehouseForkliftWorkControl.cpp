#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "WarehouseFleetCoordinator.h"
#include "WarehouseChargingStation.h"
#include "WarehouseWaitingBay.h"
#include "WarehouseElevator.h"
#include "WarehouseDamageSystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

bool AWarehouseForklift::HasRetainedForkContact() const
{
 if (!IsValid(TargetPallet) || !Carriage) return false;
 // Conservative clearance of the known workpiece, not a new obstacle scan.
 // Its real bodies remain independent and fully collidable during cancellation.
 const FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(TargetPallet);
 if (!Bounds.IsValid) return false;
 const FBox Local=Bounds.TransformBy(Carriage->GetComponentTransform().ToInverseMatrixWithScale());
 return Local.Min.X<117 && Local.Max.X>-2 && Local.Min.Y<47 && Local.Max.Y>-47 && Local.Min.Z<15 && Local.Max.Z>0;
}

bool AWarehouseForklift::WorkStopCanResume()
{
 if (!bWorkStopNeedsRecovery) return true;
 if (bFloorTransfer || (IsValid(Elevator) && Elevator->ReservedVehicle==this) ||
     (bSupportingPallet && PalletOnForks()) || HasRetainedForkContact())
 {
  WorkStopStatus=TEXT("작업은 중지됐습니다. 적재물·포크 접촉을 유지하며 제동 중입니다. G 원격 조작으로 하역하고 포크를 빼세요.");
  SetOperationError(TEXT("WORK_STOP_RECOVERY"),WorkStopStatus);
  return false;
 }
 bWorkStopNeedsRecovery=false;bSupportingPallet=false;TargetPallet=nullptr;CarriedCargo.Reset();
 WorkStopStatus=TEXT("중지 후 물리 정리 완료 · 새 작업을 시작할 수 있습니다.");
 return true;
}

bool AWarehouseForklift::StopCurrentWork()
{
 if (!HasAuthority() || IsRemoteControlled())
 { WorkStopStatus=TEXT("원격 조작을 종료한 뒤 작업을 중지하세요.");return false; }
 // Clear the queue before reporting, so report listeners never observe a cancelled instruction as still queued.
 const auto Queued=PendingJobs;PendingJobs.Reset();
 const auto Current=ActiveJob;
 for (const auto& Job:Queued)
 {
  ActiveJob=Job;ReportJob(TEXT("CANCELLED"),TEXT("[USER_STOP] 사용자가 차량의 대기 작업을 완전히 중지했습니다."));
  WorkOrderSourcePoses.Remove(Job.JobId);
 }
 ActiveJob=Current;
 if (bFloorTransfer && !IsValid(Elevator)) bFloorTransfer=false;
 if (bFloorTransfer || (IsValid(Elevator) && Elevator->ReservedVehicle==this))
 {
  // Unboarded reservations can be released. Occupied platforms must finish the physical transfer.
  if (IsValid(Elevator) && Elevator->CancelTransfer(this)) bFloorTransfer=false;
  else
  {
   bWorkStopRequested=true;
   WorkStopStatus=TEXT("중지 접수 · 승강기에서 안전하게 하차한 뒤 현재 작업을 취소합니다.");
   if (!bPowered && CheckSystems().IsEmpty() && AIState==EWarehouseAIState::Paused) ToggleAutonomy();
   if (!bPowered) WorkStopStatus+=TEXT(" 하차를 계속하려면 고장 원인을 해결하고 운행을 재개하세요.");
   SetOperationError(TEXT("WORK_STOP_PENDING"),WorkStopStatus);
   return true;
  }
 }
 bWorkStopRequested=false;
 bWorkStopNeedsRecovery=bSupportingPallet || HasRetainedForkContact();
 if (!ActiveJob.JobId.IsEmpty())
 {
  ReportJob(TEXT("CANCELLED"),TEXT("[USER_STOP] 사용자가 현재 운반 작업을 완전히 중지했습니다."));
  WorkOrderSourcePoses.Remove(ActiveJob.JobId);
 }
 ActiveJob=FWarehouseWorkOrder();PlannedRoute.Reset();RouteIndex=0;bRouteStarted=false;
 bResumeAfterCheck=false;bReplanAfterRemote=false;ResumeAI=EWarehouseAIState::Ready;WaitResumeAI=EWarehouseAIState::Ready;
 bObservedTargetValid=false;bHasRecognizedTargetPose=false;
 bEmergencyBlocked=false;EmergencyReason.Reset();StalledSeconds=0;WaitSeconds=0;RecoveryAttempts=0;ForkInsertionRetries=0;
 bPowered=false;bAutoStart=false;BrakeDrive();DesiredLift=LiftOffset;LiftMotorTarget=LiftOffset;
 if (!bWorkStopNeedsRecovery) {TargetPallet=nullptr;CarriedCargo.Reset();bSupportingPallet=false;}
 if (FleetCoordinator) FleetCoordinator->Release(this);
 if (IsValid(WaitingBay)) WaitingBay->Release(this);
 if (IsValid(ChargingStation)) ChargingStation->ShowCharge(BatteryPercent,false);
 WorkStopStatus=bWorkStopNeedsRecovery ?
  TEXT("작업 중지 · 적재물과 포크는 현재 위치에 유지합니다. G로 하역·포크 정리 후 새 작업을 시작하세요.") :
  TEXT("현재 작업과 대기 작업을 중지했습니다. 재개해도 취소한 작업을 다시 실행하지 않습니다.");
 TransitionAI(EWarehouseAIState::Paused,WorkStopStatus);
 SetOperationError(TEXT("WORK_STOPPED"),WorkStopStatus);
 return true;
}

bool AWarehouseForklift::IsWorkDeferred(AWarehousePallet* Pallet) const
{
 const auto* Until=DeferredWorkUntil.Find(Pallet);
 return Until && GetWorld() && GetWorld()->GetTimeSeconds()<*Until;
}

bool AWarehouseForklift::TryDeferCurrentWork(const FString& Reason,const FString& PlayerMessage)
{
 // A damaged vehicle, an occupied lift or retained freight cannot safely switch jobs.
 if (!HasAuthority() || !bAutonomousMode || IsRemoteControlled() || ActiveJob.JobId.IsEmpty() ||
     bSupportingPallet || bFloorTransfer || bWorkStopRequested || bWorkStopNeedsRecovery ||
     !CheckSystems().IsEmpty() || (IsValid(Elevator) && Elevator->ReservedVehicle==this) || HasRetainedForkContact()) return false;
 // The current truck must actually have room to work elsewhere. Real jams retain the existing emergency stop.
 if (!NavigationClear(GetActorTransform())) return false;
 if (IsValid(ActiveJob.Pallet)) DeferredWorkUntil.Add(ActiveJob.Pallet,GetWorld()->GetTimeSeconds()+30.);
 for (auto It=DeferredWorkUntil.CreateIterator();It;++It)
  if (!It.Key().IsValid() || It.Value()<=GetWorld()->GetTimeSeconds()) It.RemoveCurrent();
 const FString Detail=TEXT("[WORK_DEFERRED] 운반할 수 없어 이 작업을 보류하고 다른 작업을 먼저 처리합니다. ")+
  (PlayerMessage.IsEmpty() ? Reason : PlayerMessage);
 ReportJob(TEXT("DEFERRED"),Detail);
 WorkOrderSourcePoses.Remove(ActiveJob.JobId);
 ActiveJob=FWarehouseWorkOrder();TargetPallet=nullptr;CarriedCargo.Reset();
 PlannedRoute.Reset();RouteIndex=0;bRouteStarted=false;bResumeAfterCheck=false;ResumeAI=EWarehouseAIState::Ready;
 bObservedTargetValid=false;bHasRecognizedTargetPose=false;bPalletReleased=false;
 bEmergencyBlocked=false;EmergencyReason.Reset();StalledSeconds=0;WaitSeconds=0;
 if (FleetCoordinator) FleetCoordinator->Release(this);
 TransitionAI(EWarehouseAIState::Ready,TEXT("운반 불가 작업 보류 · 다른 작업 대기"));
 SetOperationError(TEXT("WORK_DEFERRED"),Detail);
 return true;
}
