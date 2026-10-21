#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "WarehouseChargingStation.h"
#include "WarehouseFleetCoordinator.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

// Default 0 (operator fault), the project owner's decision of 2026-10-21: a route that cannot be found ends in a fault so the
// training scenario can show the cause. 1 turns on waiting and replanning every few seconds for unattended runs.
static TAutoConsoleVariable<int32> CVarOptRouteWait(TEXT("Warehouse.Opt.RouteWait"),0,TEXT("1: no route -> wait and replan every few seconds instead of an operator fault; 0 (default): fault."));

bool AWarehouseForklift::IsRecovering() const
{
 return AIState==EWarehouseAIState::RecoverLower || AIState==EWarehouseAIState::RecoverWithdraw || AIState==EWarehouseAIState::RecoverReplan;
}

void AWarehouseForklift::RecoveryFault(const TCHAR* Reason,const TCHAR* PlayerMessage)
{
 bEmergencyBlocked=true; EmergencyReason=PlayerMessage; EmergencyLocation=GetActorLocation();
 ReportJob(TEXT("EMERGENCY"),Reason);
 FaultAI(Reason);
}

bool AWarehouseForklift::IsRouteWaitReason(const FString& Reason)
{
 return Reason.Contains(TEXT("NO PICKUP ROUTE")) || Reason.Contains(TEXT("NO DELIVERY ROUTE")) || Reason.Contains(TEXT("NO CLEAR ROUTE"));
}

bool AWarehouseForklift::BeginRouteWait(EWarehouseAIState From,const FString& Reason)
{
 // Only a vehicle that is itself clear waits; reversing cannot open a blocked lane or goal, and the next plan runs from here.
 if (CVarOptRouteWait.GetValueOnGameThread()==0 || RouteWaitSeconds>=RouteWaitTimeoutSeconds) return false;
 BrakeDrive();
 bRouteWaiting=true;
 WaitResumeAI=From; WaitObstacleReason=Reason; WaitSeconds=0;
 if (!bRouteWaiting || RouteWaitSeconds<=0.f)
  UE_LOG(LogTemp,Log,TEXT("AGV_ROUTE_WAIT %s: %s, waiting and replanning every %.1f s"),*VehicleName,*Reason,RouteWaitRetrySeconds);
 TransitionAI(EWarehouseAIState::WaitingObstacle,FString::Printf(TEXT("%s · 통로가 열릴 때까지 대기 후 재계획 (WAITING FOR ROUTE)"),*Reason));
 return true;
}

bool AWarehouseForklift::BeginRecovery(EWarehouseAIState From,const FString& Reason)
{
 BrakeDrive();
 if (IsRemoteControlled() || IsRecovering()) return false;
 if (bFloorTransfer || (bSupportingPallet && LiftOffset>20.f))
 { RecoveryFault(TEXT("RECOVERY REQUIRES SAFE FLOOR / LOW LOAD - OPERATOR REQUIRED"),TEXT("승강기 또는 높은 적재 상태에서 자동 탈출할 수 없습니다.")); return false; }
 bool AlignmentRetry=false;
 if (From==EWarehouseAIState::AlignVehicle && IsValid(TargetPallet))
 {
  const FVector Offset=TargetPallet->GetActorLocation()-GetActorLocation();
  const float Ahead=FVector::DotProduct(Offset,GetActorForwardVector());
  AlignmentRetry=Ahead>0 && Ahead<=PalletDetectionRangeCm &&
   FMath::Abs(FVector::DotProduct(Offset,GetActorRightVector()))<=50.f &&
   FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,TargetPallet->GetActorRotation().Yaw))<=15.f &&
   FMath::Abs(FloorBase(TargetPallet->GetActorLocation().Z)-FloorBase(GetActorLocation().Z))<1.f;
 }
 bRetryInsertion=!bSupportingPallet && !bPalletReleased && IsValid(TargetPallet) &&
  (From==EWarehouseAIState::CorrectFork ||
   From==EWarehouseAIState::InsertFork || From==EWarehouseAIState::VerifyInsertion || AlignmentRetry);
 if (RecoveryAttempts>=3 || (bRetryInsertion && ForkInsertionRetries>=3))
 { RecoveryFault(TEXT("RECOVERY LIMIT - CHECK PALLET / CLEAR SPACE"),TEXT("복구를 3회 시도했지만 이동·포크 삽입에 실패했습니다.")); return false; }
 const bool FailedDeliveryRoute=From==EWarehouseAIState::PlanDelivery && Reason==TEXT("OBSTACLE / NO DELIVERY ROUTE");
 const bool FailedPickupRoute=From==EWarehouseAIState::PlanPickup && Reason==TEXT("OBSTACLE / NO PICKUP ROUTE");
 const bool FloorAlignmentRetry=From==EWarehouseAIState::AlignUnload && Reason==TEXT("FLOOR LOAD ALIGNMENT OUTSIDE SLOT");
 if ((FailedDeliveryRoute || (FailedPickupRoute && IsValid(TargetPallet))) &&
     PlanRecoveryDetour(PalletApproach(FailedPickupRoute ? TargetPallet->GetActorTransform() : ActiveJob.Destination,220)))
 {
  ++RecoveryCount; ++RecoveryAttempts;
  ProgressLocation=GetActorLocation();StalledSeconds=0;WaitSeconds=0;
  TransitionAI(FailedPickupRoute ? EWarehouseAIState::NavigatePickup : EWarehouseAIState::NavigateDelivery,
   FailedPickupRoute ? TEXT("CLEARANCE ROUTE FOUND - RESUME PICKUP") : TEXT("CLEARANCE ROUTE FOUND - RESUME DELIVERY"));
  bRouteStarted=true;
  return true;
 }
 bRecoveryNeedsRoute=true;
 RecoveryResume=From;
 if (bRetryInsertion)
 {
  ++ForkInsertionRetries;
  RecoveryResume=EWarehouseAIState::DetectPallet;
  bRecoveryNeedsRoute=false;
 }
 else switch (From)
 {
 case EWarehouseAIState::PlanPickup:
 case EWarehouseAIState::NavigatePickup:
 case EWarehouseAIState::AlignVehicle:
  if (!IsValid(TargetPallet)) { FaultAI(TEXT("TARGET LOST")); return false; }
  RecoveryGoal=PalletApproach(TargetPallet->GetActorTransform(),220);
  RecoveryResume=EWarehouseAIState::NavigatePickup; break;
 case EWarehouseAIState::PlanDelivery:
 case EWarehouseAIState::NavigateDelivery:
 case EWarehouseAIState::DepartPickup:
 case EWarehouseAIState::AlignUnload:
  RecoveryGoal=PalletApproach(ActiveJob.Destination,220);
  RecoveryResume=EWarehouseAIState::NavigateDelivery; break;
 case EWarehouseAIState::PlanCharge:
 case EWarehouseAIState::NavigateCharge:
 case EWarehouseAIState::DockCharge:
  RecoveryGoal=ChargeExitPose; RecoveryResume=EWarehouseAIState::NavigateCharge; break;
 case EWarehouseAIState::PlanParking:
 case EWarehouseAIState::NavigateParking:
 case EWarehouseAIState::AlignParking:
  RecoveryGoal=IdlePose;RecoveryResume=EWarehouseAIState::NavigateParking;break;
 case EWarehouseAIState::WithdrawFork:
  RecoveryResume=EWarehouseAIState::VerifyUnload; bRecoveryNeedsRoute=false; break;
 case EWarehouseAIState::LeaveCharger:
  RecoveryResume=EWarehouseAIState::LeaveCharger; bRecoveryNeedsRoute=false; break;
 default:
  RecoveryFault(TEXT("CONTACT RECOVERY NEEDS OPERATOR"),TEXT("현재 작업 단계에서 자동으로 접촉을 해제할 수 없습니다.")); return false;
 }
 bool ChargerForwardRecovery=false;
 if (FailedPickupRoute && !bSupportingPallet && IsValid(ChargingStation) && ChargingStation->AssignedVehicle==this)
 {
  const FVector Local=ChargingStation->GetActorTransform().InverseTransformPosition(GetActorLocation());
  if (Local.X>=0 && Local.X<=250 && FMath::Abs(Local.Y)<=10 && FMath::Abs(Local.Z)<=5 &&
      FVector::DotProduct(GetActorForwardVector(),ChargingStation->GetActorForwardVector())>=FMath::Cos(FMath::DegreesToRadians(5.f)))
  {
   const FTransform Forward(FRotator(0,GetActorRotation().Yaw,0),GetActorLocation()+GetActorForwardVector().GetSafeNormal2D()*150.f);
   TArray<FWarehouseRoutePoint> Path;float Cost;
   TGuardValue<bool> Planning(bPlanningRoute,true);
   if (!ConnectPoses(GetActorTransform(),Forward,Path,Cost) || Path.IsEmpty() ||
       Path.ContainsByPredicate([](const FWarehouseRoutePoint& P){return P.bReverse || P.bPivot;}) ||
       (FleetCoordinator && !FleetCoordinator->Permit(this,&Forward)))
   {
    // No route and no free way out right now: another vehicle's lease or a blocked lane usually clears by itself.
    // Wait and replan like any other missing route; only a long wait ends in the operator fault.
    if (BeginRouteWait(From,Reason)) return true;
    RecoveryFault(TEXT("NO SAFE CHARGER DEPARTURE - OPERATOR REQUIRED"),TEXT("충전기 앞에서 안전한 출차 경로를 확보하지 못했습니다."));return false;
   }
   ChargerForwardRecovery=true;
   UE_LOG(LogTemp,Log,TEXT("AGV_CHARGER_DEPARTURE_RECOVERY %s from=%s to=%s"),*VehicleName,*GetActorLocation().ToString(),*Forward.GetLocation().ToString());
  }
 }
 // A clear vehicle with an unreachable destination is not physically jammed.
 // Blindly reversing cannot fix a blocked rack face or a disconnected aisle.
 if ((FailedPickupRoute || FailedDeliveryRoute) && !ChargerForwardRecovery && NavigationClear(GetActorTransform()))
 {
  TGuardValue<bool> Planning(bPlanningRoute,true);
  const FTransform Goal=PalletApproach(FailedPickupRoute ? TargetPallet->GetActorTransform() : ActiveJob.Destination,220);
  const bool GoalClear=NavigationClear(Goal);
  if (BeginRouteWait(From,Reason)) return true;
  RecoveryFault(GoalClear ? TEXT("NO CONNECTED ROUTE AFTER LAYOUT CHANGE") : TEXT("PALLET APPROACH SPACE BLOCKED"),
   GoalClear ? TEXT("현재 배치에서 연결 가능한 경로를 찾지 못했습니다. 통로 폭과 랙 배치를 확인한 뒤 E로 재시도하세요.") :
    TEXT("팔레트 접근 공간이 막혀 있습니다. 랙 앞 작업 공간과 팔레트 방향을 확인한 뒤 E로 재시도하세요."));
  return false;
 }
 ++RecoveryCount; ++RecoveryAttempts;
 StalledSeconds=0;
 RecoveryStart=GetActorLocation();
 // A missing delivery route after rack withdrawal is not an inward contact.
 // Keep clearing the source rack instead of undoing the last reverse into it.
 RecoveryDirection=bRetryInsertion || From==EWarehouseAIState::WithdrawFork || FailedDeliveryRoute || FloorAlignmentRetry ? -1.f : -LastDriveDirection;
 if (ChargerForwardRecovery) RecoveryDirection=1.f;
 RecoveryDistance=150.f;
 if (bRetryInsertion)
 {
  // Leave enough steering distance for a new alignment after real contact drift.
  const FVector ClearPose=PalletApproach(TargetPallet->GetActorTransform(),320).GetLocation();
  RecoveryDistance=FMath::Clamp(FVector::DotProduct(GetActorLocation()-ClearPose,GetActorForwardVector())+10.f,0.f,300.f);
  // Do not back an already-clear pallet out of its configured sensor range.
  const FVector Offset=TargetPallet->GetActorLocation()-GetActorLocation();
  const float Along=FVector::DotProduct(Offset,GetActorForwardVector().GetSafeNormal2D());
  const float SideSquared=FMath::Max(0.f,float(Offset.SizeSquared())-Along*Along);
  const float SensorRadius=FMath::Max(0.f,PalletDetectionRangeCm-20.f);
  const float RemainingSquared=SensorRadius*SensorRadius-SideSquared;
  const float VisibleRetreat=RemainingSquared>0 ? FMath::Max(0.f,FMath::Sqrt(RemainingSquared)-Along) : 0.f;
  RecoveryDistance=FMath::Min(RecoveryDistance,VisibleRetreat);
 }
 TransitionAI(bRetryInsertion ? EWarehouseAIState::RecoverLower : EWarehouseAIState::RecoverWithdraw,
  FString::Printf(TEXT("RECOVERY %d/3: %s - WITHDRAW / REALIGN"),RecoveryAttempts,*Reason));
 ReportJob(TEXT("RECOVERY"),Reason);
 return true;
}

bool AWarehouseForklift::PlanRecoveryDetour(const FTransform& Goal)
{
 TGuardValue<bool> CurrentRackWaypoints(bUseCurrentRackWaypoints,true);
 // Reuse the swept pose graph; temporary side lanes do not change saved anchors.
 const TArray<FTransform> SavedAnchors=NavigationAnchors;
 const FTransform From=GetActorTransform();
 const FVector Delta=Goal.GetLocation()-From.GetLocation();
 const FVector Across=FVector::CrossProduct(FVector::UpVector,Delta.GetSafeNormal2D());
 const float Distance=Delta.Size2D();
 TArray<FTransform> Candidates;
 if (bTrafficEscape)
  for (float DistanceCm : {-120.f,-60.f,60.f,120.f})
   Candidates.Add(FTransform(From.GetRotation(),From.GetLocation()+From.GetUnitAxis(EAxis::X)*DistanceCm));
 // ponytail: two local side lanes; this is bounded recovery, not a global free-space planner.
 if (Distance>250.f) for (float Side : {-220.f,220.f,-330.f,330.f})
 {
  const float Fraction=FMath::Clamp(300.f/Distance,.25f,.4f);
  Candidates.Add(FTransform(FRotator(0,From.Rotator().Yaw,0),From.GetLocation()+Delta*Fraction+Across*Side));
  Candidates.Add(FTransform(FRotator(0,Goal.Rotator().Yaw,0),From.GetLocation()+Delta*(1.f-Fraction)+Across*Side));
 }
 // Finish a loaded standby pose by reversing from its front when nearby racks
 // leave no swept turning space at the goal. The normal graph checks clearance.
 if (bSupportingPallet && !bTrafficEscape && FMath::Abs(Goal.GetLocation().Z-From.GetLocation().Z)<=1.f)
 {
  const FVector Lead=Goal.GetLocation()+Goal.GetUnitAxis(EAxis::X)*180.f;
  Candidates.Add(FTransform(Goal.GetRotation(),Lead));
  // Reach that lead from either side without requiring a pivot in the slot.
  for (float Side : {-350.f,350.f})
   Candidates.Add(FTransform(Goal.GetRotation(),Lead+Goal.GetUnitAxis(EAxis::Y)*Side));
 }
 Candidates.Append(SavedAnchors);
 NavigationAnchors=MoveTemp(Candidates);
 const bool Found=PlanAutonomousRoute(Goal);
 NavigationAnchors=SavedAnchors;
 if (Found)
 {
  // The new swept plan supersedes the blocked preview from the old pose.
  bRouteObstacleClear=true;
  ++DetourCount; ReportJob(TEXT("REPLANNED"),TEXT("SWEPT LOCAL / EXISTING LANE ROUTE"));
 }
 return Found;
}

void AWarehouseForklift::AdvanceRecovery(float Dt)
{
 if (AIState==EWarehouseAIState::RecoverLower)
 {
  const float Height=FMath::Max(0.f,float(TargetPallet->GetActorLocation().Z-GetActorLocation().Z));
  if (MoveLift(Height) && FMath::Abs(LiftOffset-Height)<.5f &&
      FMath::Abs(CarriageBody->GetPhysicsLinearVelocity().Z)<1.f)
  {
   RecoveryStart=GetActorLocation();
   TransitionAI(EWarehouseAIState::RecoverWithdraw,TEXT("LOWERED - WITHDRAW JAMMED FORKS"));
  }
  else if (AIElapsed>8.f) RecoveryFault(TEXT("RECOVERY LOWER BLOCKED - OPERATOR REQUIRED"),TEXT("포크가 걸려 내려가지 않습니다."));
  return;
 }
 if (AIState==EWarehouseAIState::RecoverWithdraw)
 {
  // Wait for finite braking before reversing. Native contacts decide actual travel.
  if (AIElapsed<.5f || (AIElapsed<2.f && FMath::Abs(CurrentSpeedCm)>5.f)) return;
  const float Travel=FVector::DotProduct(GetActorLocation()-RecoveryStart,GetActorForwardVector())*RecoveryDirection;
  const float Remaining=RecoveryDistance-Travel;
  if (Remaining>1.f && AIElapsed<14.f)
  {
   const FVector Forward=GetActorForwardVector().GetSafeNormal2D();
   FHitResult Ground;
   const FVector Preview=GetActorLocation()+Forward*RecoveryDirection*35.f*Dt;
   FCollisionQueryParams Params(SCENE_QUERY_STAT(RecoveryFloor),false,this);
   if (!GetWorld()->LineTraceSingleByChannel(Ground,Preview+FVector(0,0,3),Preview-FVector(0,0,5),ECC_Visibility,Params) || Ground.ImpactNormal.Z<.98f)
   { RecoveryFault(TEXT("RECOVERY FLOOR UNSUPPORTED"),TEXT("탈출 방향에 안전한 바닥이 없습니다.")); return; }
   if (CommandDrive(RecoveryDirection*FMath::Min(35.f,Remaining*2.f),0,Dt) && Status.StartsWith(TEXT("RECOVERY BLOCKED:")))
    SetStatus(TEXT("RECOVERY - LOW SPEED WITHDRAWAL"));
   return;
  }
  BrakeDrive();
  if (FMath::Abs(CurrentSpeedCm)>1.f && AIElapsed<16.f) return;
  if (Remaining>1.f && (bRetryInsertion || !NavigationClear(FTransform(FRotator(0,GetActorRotation().Yaw,0),GetActorLocation()))))
  { RecoveryFault(TEXT("RECOVERY WITHDRAW BLOCKED - OPERATOR REQUIRED"),TEXT("주변 물체에 막혀 포크를 빼거나 후진할 수 없습니다.")); return; }
  TransitionAI(EWarehouseAIState::RecoverReplan,TEXT("WITHDRAW COMPLETE - RESENSE / REPLAN"));
  return;
 }
 if (AIState==EWarehouseAIState::RecoverReplan)
 {
  if (bRetryInsertion)
  {
   // Read the pallet again, including configured perception errors. Never move
   // the pallet or waive engagement checks to manufacture a successful retry.
   TransitionAI(RecoveryResume,TEXT("RETRY - DETECT CURRENT PALLET POSE"));
  }
  else if (!bRecoveryNeedsRoute) TransitionAI(RecoveryResume,TEXT("RESUME AFTER CONTACT WITHDRAWAL"));
  else if (PlanRecoveryDetour(RecoveryGoal))
  {
   TransitionAI(RecoveryResume,TEXT("DETOUR FOUND - RESUME WORK ORDER"));
   bRouteStarted=true;
  }
  else if (!BeginRouteWait(RecoveryResume,TEXT("OBSTACLE / NO CLEAR ROUTE")))
   RecoveryFault(TEXT("NO SAFE DETOUR - CLEAR SPACE / OPERATOR REQUIRED"),TEXT("안전하게 이동할 우회 경로를 찾지 못했습니다."));
  ProgressLocation=GetActorLocation(); StalledSeconds=0;
 }
}

void AWarehouseForklift::CheckDriveProgress(float Dt)
{
 if (IsRecovering() || AIState==EWarehouseAIState::WaitingObstacle || FMath::Abs(DesiredDriveSpeed)<.5f)
 { StalledSeconds=0; ProgressLocation=GetActorLocation(); return; }
 if (FVector::Dist2D(GetActorLocation(),ProgressLocation)>=1.f)
 {
  ProgressLocation=GetActorLocation(); StalledSeconds=0;
 }
 else
 {
  StalledSeconds+=Dt;
  if (StalledSeconds>=2.5f) BeginRecovery(AIState,TEXT("DRIVE COMMANDED BUT NO PHYSICAL PROGRESS"));
 }
}
