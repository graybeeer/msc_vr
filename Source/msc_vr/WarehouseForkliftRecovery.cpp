#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

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

bool AWarehouseForklift::BeginRecovery(EWarehouseAIState From,const FString& Reason)
{
 BrakeDrive();
 if (IsRemoteControlled() || IsRecovering()) return false;
 if (bFloorTransfer || (bSupportingPallet && LiftOffset>20.f))
 { RecoveryFault(TEXT("RECOVERY REQUIRES SAFE FLOOR / LOW LOAD - OPERATOR REQUIRED"),TEXT("승강기 또는 높은 적재 상태에서 자동 탈출할 수 없습니다.")); return false; }
 bRetryInsertion=!bSupportingPallet && !bPalletReleased && IsValid(TargetPallet) &&
  (From==EWarehouseAIState::CorrectFork ||
   From==EWarehouseAIState::InsertFork || From==EWarehouseAIState::VerifyInsertion);
 if (RecoveryAttempts>=3 || (bRetryInsertion && ForkInsertionRetries>=3))
 { RecoveryFault(TEXT("RECOVERY LIMIT - CHECK PALLET / CLEAR SPACE"),TEXT("복구를 3회 시도했지만 이동·포크 삽입에 실패했습니다.")); return false; }
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
 case EWarehouseAIState::WithdrawFork:
  RecoveryResume=EWarehouseAIState::VerifyUnload; bRecoveryNeedsRoute=false; break;
 case EWarehouseAIState::LeaveCharger:
  RecoveryResume=EWarehouseAIState::LeaveCharger; bRecoveryNeedsRoute=false; break;
 default:
  RecoveryFault(TEXT("CONTACT RECOVERY NEEDS OPERATOR"),TEXT("현재 작업 단계에서 자동으로 접촉을 해제할 수 없습니다.")); return false;
 }
 ++RecoveryCount; ++RecoveryAttempts;
 StalledSeconds=0;
 RecoveryStart=GetActorLocation();
 RecoveryDirection=bRetryInsertion || From==EWarehouseAIState::WithdrawFork ? -1.f : -LastDriveDirection;
 RecoveryDistance=150.f;
 if (bRetryInsertion)
 {
  // Leave enough steering distance for a new alignment after real contact drift.
  const FVector ClearPose=PalletApproach(TargetPallet->GetActorTransform(),320).GetLocation();
  RecoveryDistance=FMath::Clamp(FVector::DotProduct(GetActorLocation()-ClearPose,GetActorForwardVector())+10.f,80.f,300.f);
 }
 TransitionAI(bRetryInsertion ? EWarehouseAIState::RecoverLower : EWarehouseAIState::RecoverWithdraw,
  FString::Printf(TEXT("RECOVERY %d/3: %s - WITHDRAW / REALIGN"),RecoveryAttempts,*Reason));
 ReportJob(TEXT("RECOVERY"),Reason);
 return true;
}

bool AWarehouseForklift::PlanRecoveryDetour(const FTransform& Goal)
{
 // Reuse the swept pose graph; temporary side lanes do not change saved anchors.
 const TArray<FTransform> SavedAnchors=NavigationAnchors;
 const FTransform From=GetActorTransform();
 const FVector Delta=Goal.GetLocation()-From.GetLocation();
 const FVector Across=FVector::CrossProduct(FVector::UpVector,Delta.GetSafeNormal2D());
 const float Distance=Delta.Size2D();
 TArray<FTransform> Candidates;
 // ponytail: two local side lanes; this is bounded recovery, not a global free-space planner.
 if (Distance>250.f) for (float Side : {-220.f,220.f,-330.f,330.f})
 {
  const float Fraction=FMath::Clamp(300.f/Distance,.25f,.4f);
  Candidates.Add(FTransform(FRotator(0,From.Rotator().Yaw,0),From.GetLocation()+Delta*Fraction+Across*Side));
  Candidates.Add(FTransform(FRotator(0,Goal.Rotator().Yaw,0),From.GetLocation()+Delta*(1.f-Fraction)+Across*Side));
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
  else RecoveryFault(TEXT("NO SAFE DETOUR - CLEAR SPACE / OPERATOR REQUIRED"),TEXT("안전하게 이동할 우회 경로를 찾지 못했습니다."));
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
