#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "WarehouseFleetCoordinator.h"
#include "WarehouseChargingStation.h"
#include "WarehouseWaitingBay.h"
#include "WarehouseElevator.h"
#include "WarehouseDamageSystem.h"
#include "Agv/AgvShadowSensors.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"

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
 if (bFloorTransfer || (IsValid(Elevator) && Elevator->ReservedVehicle==this)) return false;
 if (bClearingForksForWork) return true; // Retain the grounded target through withdrawal/verification, too.
 if ((bSupportingPallet && PalletOnForks()) || HasRetainedForkContact())
 {
  WorkStopStatus=TEXT("새 운반 지시를 받으면 기존 짐을 먼저 내려놓고 포크를 뺍니다.");
  return true;
 }
 bWorkStopNeedsRecovery=false;bSupportingPallet=false;TargetPallet=nullptr;CarriedCargo.Reset();
 WorkStopStatus=TEXT("중지 후 물리 정리 완료 · 새 작업을 시작할 수 있습니다.");
 return true;
}

bool AWarehouseForklift::FindRetainedLoadSupport(FTransform& Destination) const
{
 if (!IsValid(TargetPallet) || !SensorKit || !AutonomousSensorsReady()) return false;
 const FTransform PalletPose=TargetPallet->GetActorTransform();
 const FVector Point=PalletPose.TransformPosition(FVector(40,40,0));
 const FVector Start=Point+FVector(0,0,1),End(Point.X,Point.Y,GetActorLocation().Z-4);
 FAgvObstacleIndices Candidates;
 FBox Column(ForceInit);Column+=Start;Column+=End;
 SensorKit->FindKnownCandidates(Column.ExpandBy(5),Candidates);
 double Highest=-DBL_MAX;
 for (int32 Index:Candidates)
 {
  const auto& Obstacle=SensorKit->GetKnownObstacles()[Index];
  // Only published fixed surfaces have certified geometry. Unseen movable
  // objects never become a support just because a live world ray finds them.
  auto* Part=Obstacle.Component.Get();
  if (!Part || !UAgvShadowSensorsComponent::IsBuildingMapComponent(Part)) continue;
  const FTransform Live=Part->GetComponentTransform();
  const auto Remap=[&](FVector P) { return Live.TransformPosition(Obstacle.ObservedTransform.InverseTransformPosition(P)); };
  FCollisionQueryParams Params(SCENE_QUERY_STAT(RetainedLoadSupport),false,this);
  FHitResult Hit;
  if (!Part->LineTraceComponent(Hit,Remap(Start),Remap(End),Params)) continue;
  const FVector Normal=Obstacle.ObservedTransform.TransformVectorNoScale(Live.InverseTransformVectorNoScale(Hit.ImpactNormal));
  const FVector Support=Obstacle.ObservedTransform.TransformPosition(Live.InverseTransformPosition(Hit.ImpactPoint));
  if (Normal.Z<.98f || Support.Z>Point.Z+1 || Support.Z<=Highest) continue;
  FTransform Pose(FRotator(0,PalletPose.Rotator().Yaw,0),FVector(PalletPose.GetLocation().X,PalletPose.GetLocation().Y,Support.Z));
  bool Supported=true;
  for (float X:{-40.f,40.f}) for (float Y:{-40.f,40.f})
  {
   FHitResult Foot;
   Supported &= PerceivedFloorSupport(Pose.TransformPosition(FVector(X,Y,0)),&Foot) &&
    FMath::Abs(Foot.ImpactPoint.Z-Support.Z)<=.5f;
  }
  if (Supported) { Highest=Support.Z;Destination=Pose; }
 }
 return Highest>-DBL_MAX;
}

bool AWarehouseForklift::PrepareForksForNextWork()
{
 if (!bSupportingPallet && !HasRetainedForkContact() && !PendingJobs.IsEmpty())
 {
  // Mechanical occupancy of the tine bed also catches a manually inserted
  // pallet that was never lifted/assigned. This is not an ambient sensor scan.
  TArray<FOverlapResult> Contacts;
  const FTransform Forks=Carriage->GetComponentTransform();
  GetWorld()->OverlapMultiByObjectType(Contacts,Forks.TransformPosition(FVector(57.5f,0,7.5f)),Forks.GetRotation(),
   FCollisionObjectQueryParams::AllObjects,FCollisionShape::MakeBox(FVector(57.5f,43,7.5f)),
   FCollisionQueryParams(SCENE_QUERY_STAT(RetainedForkOccupancy),false,this));
  for (const auto& Contact:Contacts) if (auto* Pallet=Cast<AWarehousePallet>(Contact.GetActor()))
  {
   if (PalletClaimedByOther(Pallet))
   { BrakeDrive();SetOperationError(TEXT("FORK_CLEARANCE_BLOCKED"),TEXT("포크와 다른 차량의 팔레트가 접촉해 새 작업을 대기합니다."));return true; }
   TargetPallet=Pallet;bWorkStopNeedsRecovery=true;break;
  }
 }
 if (!bSupportingPallet && !HasRetainedForkContact())
 { WorkStopCanResume();return false; }
 BrakeDrive();DesiredLift=LiftOffset;LiftMotorTarget=LiftOffset;
 if (PendingJobs.IsEmpty())
 { SetStatus(TEXT("기존 짐 유지 · 새 운반 지시를 받으면 먼저 하역합니다."));return true; }
 if (ChassisBody->GetPhysicsLinearVelocity().Size()>=1.f || ChassisBody->GetPhysicsAngularVelocityInDegrees().Size()>=.5f) return true;
 const double Now=GetWorld()->GetTimeSeconds();
 if (Now<NextForkClearanceCheck) return true;
 NextForkClearanceCheck=Now+1.;
 // The new order stays in the queue until the actual load is grounded and the
 // forks have withdrawn. This internal handoff has no customer job/completion ID.
 FTransform Drop;
 const FWarehouseWorkOrder Previous=ActiveJob;
 ActiveJob=FWarehouseWorkOrder();ActiveJob.Pallet=TargetPallet;
 TrackCargo();
 bool Safe=FindRetainedLoadSupport(Drop);
 if (Safe)
 {
  ActiveJob.Destination=Drop;
  ActiveJob.bRackDestination=Drop.GetLocation().Z-GetActorLocation().Z>5.f;
  Safe=DestinationClear() && IsFloorFootprintValid(TargetPallet,Drop,false);
 }
 if (!Safe)
 {
  ActiveJob=Previous;
  WorkStopStatus=TEXT("새 운반 지시 대기 · 기존 짐을 내려놓을 안전한 받침·빈 공간이 없습니다. 주변을 정리하거나 G로 하역하세요.");
  SetOperationError(TEXT("FORK_CLEARANCE_BLOCKED"),WorkStopStatus);
  return true;
 }
 bClearingForksForWork=true;bWorkStopNeedsRecovery=true;
 bPalletReleased=!bSupportingPallet;
 WaitSeconds=0;DestinationTrafficSeconds=0;LostSupportSeconds=0;
 bObservedTargetValid=false;bHasRecognizedTargetPose=false;
 LastErrorCode=NAME_None;LastErrorMessage.Reset();
 WorkStopStatus=TEXT("새 작업 준비 · 기존 짐 하역 → 포크 철수 중");
 TransitionAI(EWarehouseAIState::LowerLoad,WorkStopStatus);
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
 bClearingForksForWork=false;NextForkClearanceCheck=-1;
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
  TEXT("작업 중지 · 적재물과 포크를 유지합니다. 새 운반 지시를 받으면 먼저 하역·포크 철수를 진행합니다.") :
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
