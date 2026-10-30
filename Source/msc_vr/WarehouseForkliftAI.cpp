#include "WarehouseForklift.h"
#include "Agv/AgvForkliftLocalizer.h"
#include "WarehousePerformance.h"
#include "WarehouseExperimentMetrics.h"
#include "WarehouseExperimentSubsystem.h"
#include "Misc/ScopeExit.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

static uint32 WarehouseNavigationQueryCount=0;
#include "WarehouseFleetCoordinator.h"
#include "WarehouseComputer.h"
#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehouseDamageSystem.h"
#include "WarehousePhysics.h"
#include "WarehouseChargingStation.h"
#include "WarehouseElevator.h"
#include "WarehouseWaitingBay.h"
#include "Agv/AgvShadowSensors.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "PhysicsEngine/BodySetup.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarWarehouseGeometryPruning(TEXT("Warehouse.NavigationGeometryPruning"),1,
 TEXT("Reject unexecutable curves before native collision queries; 0 preserves the reference query order."));

// Each synchronous plan owns its query setup; every native collision check stays live.
struct FWarehouseNavigationQuery
{
 FCollisionQueryParams Params;
 FBox Envelope;
 FVector Pivot;
 bool bHasPivot;
 TArray<AActor*> PerceptionIgnored;
 FVector DiagnosticGoal=FVector::ZeroVector;
 bool bDiagnostics=false;
 bool bInsertionRetryEscape=false;
 bool bObservedPickupEscape=false;
 mutable uint8 GoalBlockReports=0;
 bool bGeometryFirst=false;
 mutable uint32 GeometryRejectedCandidates=0;
};

FString AWarehouseForklift::CheckSystems(bool bIncludeAutonomySensors) const
{
 if (bMechanicalFailure) return TEXT("MECHANICAL FAILURE");
 if (bIncludeAutonomySensors && !bSafetyScannerHealthy) return TEXT("SAFETY SCANNER FAULT");
 if (!bEStopReleased) return TEXT("E-STOP ACTIVE");
 if (!bBrakeHealthy) return TEXT("BRAKE FAULT");
 if (!bSteeringHealthy) return TEXT("STEERING FAULT");
 if (bIncludeAutonomySensors && !bForkSensorHealthy) return TEXT("FORK / MAST SENSOR FAULT");
 if (bIncludeAutonomySensors && !bLocalizationHealthy) return TEXT("LOCALIZATION LOST");
 if (!bBatteryHealthy || BatteryVoltage<=0 || BatteryCapacityAh<=0) return TEXT("BATTERY FAULT");
 if (BatteryPercent<=0 && !(IsValid(ChargingStation) && ChargingStation->IsDocked(this))) return TEXT("BATTERY EMPTY - RECOVERY REQUIRED");
 if (bIncludeAutonomySensors && !bPalletSensorHealthy) return TEXT("PALLET SENSOR FAULT");
 if (bIncludeAutonomySensors && !bLoadSensorHealthy) return TEXT("LOAD SENSOR FAULT");
 return FString();
}
void AWarehouseForklift::ReportJob(const FString& Result,const FString& Detail)
{
 FWarehouseJobReport Report;
 Report.JobId=ActiveJob.JobId; Report.Result=Result; Report.Detail=Detail; Report.SimulationSeconds=GetWorld()->GetTimeSeconds();
 JobReports.Add(Report);
 if (JobReports.Num()>100) JobReports.RemoveAt(0);
 OnJobReport.Broadcast(Report);
 FWarehouseExperimentMetrics::NotifyReport(this,Report);
 UE_LOG(LogTemp,Log,TEXT("AGV_REPORT [%s] %s: %s"),*Report.JobId,*Result,*Detail);
}
void AWarehouseForklift::TransitionAI(EWarehouseAIState Next,const FString& Message)
{
 // A new insertion/withdrawal/turn intent must consume the existing measured
 // snapshot again; this does not request another sensor acquisition.
 MovementObstacleSnapshot=LiftObstacleSnapshot=RouteObstacleSnapshot=TurnObstacleSnapshot=INDEX_NONE;
 AIState=Next; AIElapsed=0; DockStableSeconds=-1;bDockPullout=false;bRouteTurning=false; bRouteStarted=false; BrakeDrive();
 if (FleetCoordinator && (Next==EWarehouseAIState::Ready || Next==EWarehouseAIState::Paused ||
     Next==EWarehouseAIState::Fault || Next==EWarehouseAIState::Off)) FleetCoordinator->Release(this);
 SetStatus(Message);
 ReportJob(TEXT("STATE"),Message);
}
void AWarehouseForklift::FaultAI(const FString& Reason)
{
 if (TryDeferCurrentWork(Reason)) return;
 if (AIState!=EWarehouseAIState::Fault)
 {
  if (AIState!=EWarehouseAIState::SelfCheck || !bResumeAfterCheck) ResumeAI=AIState;
  TransitionAI(EWarehouseAIState::Fault,Reason);
  ReportJob(TEXT("FAULT"),Reason);
 }
 bPowered=false; BrakeDrive();
 SetStatus(Reason+TEXT("\nFIX CAUSE THEN E: SELF CHECK"));
 if (IsValid(ChargingStation)) ChargingStation->ShowCharge(BatteryPercent,false);
}
void AWarehouseForklift::ToggleAutonomy()
{
 if (bMechanicalFailure) { SetMechanicalFailure(); return; }
 if (!bPowered && !WorkStopCanResume()) return;
 if (bPowered)
 {
  if (AIState!=EWarehouseAIState::SelfCheck || !bResumeAfterCheck) ResumeAI=AIState;
  bPowered=false;
  TransitionAI(EWarehouseAIState::Paused,TEXT("PAUSED - E: SELF CHECK / RESUME"));
  if (IsValid(ChargingStation)) ChargingStation->ShowCharge(BatteryPercent,false);
  return;
 }
 bResumeAfterCheck=AIState==EWarehouseAIState::Paused || AIState==EWarehouseAIState::Fault;
 if (AIState==EWarehouseAIState::Fault)
 { RecoveryAttempts=0;ForkInsertionRetries=0;ObservedRouteReplans=0;ObservedRouteProgress=GetActorLocation(); }
 bEmergencyBlocked=false; EmergencyReason.Reset();
 bPowered=true;
 TransitionAI(EWarehouseAIState::SelfCheck,TEXT("POWER ON - SELF CHECK"));
}
bool AWarehouseForklift::SubmitJob(const FWarehouseWorkOrder& Job)
{
 if (!HasAuthority()) return false;
 if (bWorkStopRequested) return false;
 if (IsWorkDeferred(Job.Pallet))
 { SetOperationError(TEXT("WORK_RETRY_WAIT"),TEXT("운반 불가 물류는 30초간 보류합니다. 다른 물류를 먼저 처리하세요."));return false; }
 if (const auto* Instance=GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
  if (const auto* Experiment=Instance->GetSubsystem<UWarehouseExperimentSubsystem>();Experiment && Experiment->bBatchRunning && Job.SourceSystem!=TEXT("FMS-EXPERIMENT"))
  { SetOperationError(TEXT("EXPERIMENT_ACTIVE"),TEXT("반복 실험 중에는 저장된 공통 주문만 실행할 수 있습니다."));return false; }
 if (Job.JobId.IsEmpty() || Job.Destination.ContainsNaN() ||
     !Job.Destination.GetScale3D().Equals(FVector::OneVector,.001f) || PendingJobs.Num()>=32) return false;
 const FString Resources=ValidateJobResources(Job);
 if (!Resources.IsEmpty())
 {
  FName Code(TEXT("JOB_RESOURCES"));int32 Start=INDEX_NONE;
  if (Resources.EndsWith(TEXT("]")) && Resources.FindLastChar('[',Start))
   Code=FName(*Resources.Mid(Start+1,Resources.Len()-Start-2));
  SetOperationError(Code,Resources);return false;
 }
 if (ActiveJob.JobId==Job.JobId || CompletedJobIds.Contains(Job.JobId)) return false;
 for (const auto& Existing : PendingJobs) if (Existing.JobId==Job.JobId || Existing.Pallet==Job.Pallet) return false;
 if (!ActiveJob.JobId.IsEmpty() && ActiveJob.Pallet==Job.Pallet) return false;
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) if (*It!=this)
 {
  if (It->CompletedJobIds.Contains(Job.JobId) || It->ActiveJob.JobId==Job.JobId || It->ActiveJob.Pallet==Job.Pallet ||
      (It->bSupportingPallet && It->TargetPallet==Job.Pallet)) return false;
  for (const auto& Other : It->PendingJobs)
   if (Other.JobId==Job.JobId || Other.Pallet==Job.Pallet) return false;
 }
 PendingJobs.Add(Job);
 // A WMS request supplies the source location once. Later navigation cannot
 // silently follow a hidden pallet's changing true transform.
 if (IsValid(Job.Pallet)) WorkOrderSourcePoses.Add(Job.JobId,Job.Pallet->GetActorTransform());
 LastErrorCode=NAME_None;LastErrorMessage.Reset();
 for (TActorIterator<AWarehouseComputer> It(GetWorld());It;++It) It->RecordJobSubmitted(this,Job);
 FWarehouseExperimentMetrics::NotifySubmitted(this,Job.JobId);
 return true;
}
void AWarehouseForklift::RefreshAfterLayout(bool PublishTaskLocation)
{
 if (bClearingForksForWork)
 {
  bClearingForksForWork=false;ActiveJob=FWarehouseWorkOrder();NextForkClearanceCheck=-1;
  ResumeAI=EWarehouseAIState::Ready;
  if (bPowered) TransitionAI(EWarehouseAIState::Ready,TEXT("배치 변경 · 기존 짐 하역 위치 재확인"));
 }
 if (FleetCoordinator) FleetCoordinator->Release(this);
 PlannedRoute.Reset();RouteIndex=0;bRouteStarted=false;
 LastObstacleCheckSeconds=-1;ObstacleCheckFrame=MAX_uint64;
 InvalidateSensorSnapshot(TEXT("LAYOUT MAP REVISION / FRESH SCAN REQUIRED"));
 if (auto* Localizer=FindComponentByClass<UAgvForkliftLocalizerComponent>()) Localizer->InitializePose();
 ObservedRouteReplans=0;ObservedRouteProgress=GetActorLocation();
 ProgressLocation=GetActorLocation();StalledSeconds=0;WaitSeconds=0;
 if (PublishTaskLocation && IsValid(TargetPallet))
 {
  // Layout authoring also publishes this explicit task-location update.
  if (!ActiveJob.JobId.IsEmpty()) WorkOrderSourcePoses.Add(ActiveJob.JobId,TargetPallet->GetActorTransform());
  if (SensorMode!=EWarehouseSensorMode::Autonomous) ObservedPallet=TargetPallet->GetActorTransform();
 }
 if (!ActiveJob.JobId.IsEmpty() && IsValid(ActiveJob.Pallet))
 {
  const auto Next=bSupportingPallet ? EWarehouseAIState::PlanDelivery : EWarehouseAIState::PlanPickup;
  ResumeAI=Next;
  if (bPowered && AIState!=EWarehouseAIState::Fault && !bFloorTransfer) TransitionAI(Next,TEXT("LAYOUT UPDATED / REPLAN"));
 }
 else if (bChargePending && bPowered && !bFloorTransfer) TransitionAI(EWarehouseAIState::PlanCharge,TEXT("LAYOUT UPDATED / CHARGER REPLAN"));
}
void AWarehouseForklift::SuspendForBuildingEdit(const TArray<AActor*>& Removed)
{
 auto UsesRemoved=[&](const FWarehouseWorkOrder& Job)
 { return Removed.Contains(Job.Pallet.Get()) || Removed.Contains(Job.DestinationResource.Get()); };
 const bool WasRunning=bPowered || IsRemoteControlled();
 bool Cancelled=false;
 auto Cancel=[&](const FWarehouseWorkOrder& Job)
 {
  const auto Previous=ActiveJob;ActiveJob=Job;
  ReportJob(TEXT("CANCELLED"),TEXT("건물 편집으로 작업 대상/보관 랙이 제거되었습니다. 물류는 보류 목록을 확인하세요."));
  ActiveJob=Previous;WorkOrderSourcePoses.Remove(Job.JobId);Cancelled=true;
 };
 const auto PreviousQueue=PendingJobs;PendingJobs.RemoveAll(UsesRemoved);
 for (const auto& Job:PreviousQueue) if (UsesRemoved(Job)) Cancel(Job);
 if (!ActiveJob.JobId.IsEmpty() && UsesRemoved(ActiveJob)) { Cancel(ActiveJob);ActiveJob=FWarehouseWorkOrder(); }
 if (Removed.Contains(TargetPallet.Get())) { bSupportingPallet=false;TargetPallet=nullptr;CarriedCargo.Reset(); }
 else CarriedCargo.RemoveAll([&](AActor* Cargo) { return Removed.Contains(Cargo); });
 if (Removed.Contains(Elevator.Get())) { bFloorTransfer=false;Elevator=nullptr; }
 // Editing is a paused administrative transaction, not a driving controller.
 // Restore physical joints first, then discard the pre-edit motion on commit.
 BrakeDrive();CurrentSpeedCm=0;
 TArray<AActor*> Stopped{this};Stopped.Append(GetPhysicalLoads());
 for (AActor* Actor:Stopped) if (IsValid(Actor))
 {
  TArray<UPrimitiveComponent*> Parts;Actor->GetComponents(Parts);
  for (auto* Part:Parts) if (Part->IsSimulatingPhysics())
  { Part->SetPhysicsLinearVelocity(FVector::ZeroVector);Part->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector); }
 }
 if (!WasRunning && !Cancelled) return;
 if (IsRemoteControlled()) EndRemoteControl(RemoteOperator);
 bPowered=false;BrakeDrive();
 if (bMechanicalFailure || AIState==EWarehouseAIState::Fault) return;
 ResumeAI=ActiveJob.JobId.IsEmpty() ? EWarehouseAIState::Ready : bSupportingPallet ? EWarehouseAIState::PlanDelivery : EWarehouseAIState::PlanPickup;
 bResumeAfterCheck=true;
 TransitionAI(EWarehouseAIState::Paused,TEXT("건물 변경으로 일시정지 · E로 재점검 후 작업 재개"));
}
FTransform AWarehouseForklift::PalletApproach(const FTransform& Pose,float Distance) const
{
 const FRotator Rotation(0,Pose.Rotator().Yaw,0);
 FVector Point=Pose.GetLocation()-Rotation.Vector()*Distance;
 Point.Z=FloorBase(Pose.GetLocation().Z);
 return FTransform(Rotation,Point);
}
FBox AWarehouseForklift::NavigationEnvelope() const
{
 FBox Envelope(FVector(-126,-53,3),FVector(119,53,240));
 // Include the actual load overhang in turning clearance, not just the truck footprint.
 if (bSupportingPallet)
 {
  TArray<AActor*> Loads{TargetPallet};
  for (AActor* Cargo : CarriedCargo) Loads.Add(Cargo);
  for (AActor* Load : Loads) if (IsValid(Load))
  {
   TArray<UStaticMeshComponent*> Parts;Load->GetComponents(Parts);
   for (auto* Part : Parts) if (WarehousePhysics::IsPhysicalContact(Part) && Part->GetStaticMesh())
   {
    // Transform mesh-local bounds directly into the vehicle frame. Inverting
    // a world AABB inflated a 110cm square to ~220cm during a 45-degree turn.
    const FMatrix Relative=Part->GetComponentTransform().ToMatrixWithScale()*GetActorTransform().ToInverseMatrixWithScale();
    Envelope+=Part->GetStaticMesh()->GetBoundingBox().TransformBy(Relative);
   }
  }
 }
 return Envelope;
}
FWarehouseNavigationQuery AWarehouseForklift::BuildNavigationQuery() const
{
 FWarehouseNavigationQuery Query{FCollisionQueryParams(SCENE_QUERY_STAT(AGVNavigation),false,this),NavigationEnvelope(),FVector::ZeroVector,Wheels.Num()>=3};
 Query.bInsertionRetryEscape=bPlanningRoute && bRetryInsertion && ForkInsertionRetries>0 && AIState==EWarehouseAIState::AlignVehicle && !bSupportingPallet;
 Query.bObservedPickupEscape=bObservedPickupEscape && !bSupportingPallet && bHasRecognizedTargetPose &&
  (AIState==EWarehouseAIState::NavigatePickup || AIState==EWarehouseAIState::PlanPickup || AIState==EWarehouseAIState::RecoverReplan ||
   (AIState==EWarehouseAIState::WaitingObstacle && WaitResumeAI==EWarehouseAIState::NavigatePickup));
 auto& Params=Query.Params;
 Query.PerceptionIgnored=GetPhysicalLoads();
 Query.bGeometryFirst=bPlanningRoute && CVarWarehouseGeometryPruning.GetValueOnGameThread()!=0;
 if (bSupportingPallet && IsValid(TargetPallet)) Params.AddIgnoredActor(TargetPallet);
 for (AActor* Load : CarriedCargo) if (IsValid(Load)) Params.AddIgnoredActor(Load);
 // The central passage lease, rather than a frozen planning snapshot, schedules
 // healthy autonomous vehicles. Actual movement still checks their physical bodies.
 if (bPlanningRoute && !bTrafficEscape)
  for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
   if (*It!=this && It->bPowered && It->bAutonomousMode && !It->IsRemoteControlled() && It->CheckSystems().IsEmpty())
   { Params.AddIgnoredActor(*It);Query.PerceptionIgnored.Add(*It); }
 // Keep normal lane plans away from posts; the finite steering servo cannot
 // track an obstacle-hugging curve with zero clearance. Fork alignment keeps
 // its real insertion geometry, without this travel-only margin.
 if (bPlanningRoute && AIState!=EWarehouseAIState::AlignVehicle)
  Query.Envelope=Query.Envelope.ExpandBy(FVector(25,25,0));
 if (Query.bHasPivot)
  Query.Pivot=GetActorTransform().InverseTransformPosition((Wheels[1]->GetComponentLocation()+Wheels[2]->GetComponentLocation())*.5);
 return Query;
}
bool AWarehouseForklift::NavigationClear(const FTransform& Pose,const FWarehouseNavigationQuery* Query) const
{
 if (!Query)
 {
  const auto Local=BuildNavigationQuery();
  return NavigationClear(Pose,&Local);
 }
 ++WarehouseNavigationQueryCount;
 if (AIState==EWarehouseAIState::AlignUnload) return PhysicalPoseClear(Pose);
 const auto& Params=Query->Params;
 const auto& Envelope=Query->Envelope;
 if (bPlanningRoute && FleetCoordinator &&
     !FleetCoordinator->PlanningClear(this,Envelope.TransformBy(Pose.ToMatrixWithScale()))) return false;
 const FVector Center=Pose.TransformPosition(Envelope.GetCenter());
 const bool SensorDriven=SensorMode==EWarehouseSensorMode::Autonomous;
 const bool BoxBlocked=SensorDriven ? !PerceivedBoxClear(Pose,Envelope,Query->PerceptionIgnored) :
  GetWorld()->OverlapBlockingTestByChannel(Center,Pose.GetRotation(),ECC_Visibility,FCollisionShape::MakeBox(Envelope.GetExtent()),Params);
 // At an already-blocked escape start the rectangular travel envelope can
 // include empty space between the fork tines. Check every real collision
 // part for a short way out, including translated poses and supported loads.
 // Beyond 60cm planning keeps the normal travel margin; native movement
 // always retains swept contacts and the scanner.
 const bool LegacyEscape=(bTrafficEscape || Query->bInsertionRetryEscape) && FVector::Dist2D(Pose.GetLocation(),GetActorLocation())<60.f;
 const bool ObservedPickupEscape=Query->bObservedPickupEscape && FVector::Dist2D(Pose.GetLocation(),RecoveryStart)<60.f;
 const bool PhysicalEscape=BoxBlocked && (LegacyEscape || ObservedPickupEscape) && PhysicalPoseClear(Pose,true);
 if (BoxBlocked && !PhysicalEscape)
 {
  const bool DiagnoseGoal=Query->bDiagnostics && Query->GoalBlockReports<3 &&
   FVector::DistSquared2D(Pose.GetLocation(),Query->DiagnosticGoal)<=500.f*500.f;
  if (!SensorDriven && (UE_LOG_ACTIVE(LogTemp,Verbose) || !bPlanningRoute || DiagnoseGoal))
  {
   TArray<FOverlapResult> Hits;
   GetWorld()->OverlapMultiByChannel(Hits,Center,Pose.GetRotation(),ECC_Visibility,FCollisionShape::MakeBox(Envelope.GetExtent()),Params);
   for (const auto& Hit : Hits) if (Hit.bBlockingHit)
   {
    if (DiagnoseGoal && Query->GoalBlockReports<3)
    {
     ++Query->GoalBlockReports;
     UE_LOG(LogTemp,Log,TEXT("AGV_PLAN_GOAL_BLOCKED %s pose=%s yaw=%.1f actor=%s component=%s"),
      *VehicleName,*Pose.GetLocation().ToString(),Pose.Rotator().Yaw,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()));
    }
    if (!bPlanningRoute) { UE_LOG(LogTemp,Log,TEXT("AGV_ROUTE_OBSTACLE %s at %s actor=%s"),*VehicleName,*Pose.GetLocation().ToString(),*GetNameSafe(Hit.GetActor())); }
    else { UE_LOG(LogTemp,Verbose,TEXT("AGV route blocked at %s by %s"),*Pose.GetLocation().ToString(),*GetNameSafe(Hit.GetActor())); }
   }
  }
  return false;
 }
 FHitResult Ground;
 const FVector P=Pose.GetLocation();
 const bool Supported=SensorDriven ? PerceivedFloorSupport(P) :
  GetWorld()->LineTraceSingleByChannel(Ground,P+FVector(0,0,3),P-FVector(0,0,5),ECC_Visibility,Params) && Ground.ImpactNormal.Z>.98f;
 if (Supported && bTrafficEscape)
  for (float X : {-110.f,0.f}) for (float Y : {-40.f,40.f})
  {
   const FVector Corner=Pose.TransformPosition(FVector(X,Y,0));
   FHitResult Foot;
   if (SensorDriven)
   { if (!PerceivedFloorSupport(Corner)) return false; }
   else if (!GetWorld()->LineTraceSingleByChannel(Foot,Corner+FVector(0,0,3),Corner-FVector(0,0,5),ECC_Visibility,Params) || Foot.ImpactNormal.Z<.98f) return false;
  }
 if (!Supported) UE_LOG(LogTemp,Verbose,TEXT("AGV route unsupported at %s, hit %s"),*P.ToString(),*GetNameSafe(Ground.GetActor()));
 return Supported;
}
bool AWarehouseForklift::ConnectPoses(const FTransform& From,const FTransform& To,TArray<FWarehouseRoutePoint>& Route,float& Cost,const FWarehouseNavigationQuery* Query) const
{
 Route.Reset(); Cost=FLT_MAX;
 const FVector A=From.GetLocation(),D=To.GetLocation();
 const float Distance=FVector::Dist2D(A,D);
 if (FMath::Abs(A.Z-D.Z)>1 || From.ContainsNaN() || To.ContainsNaN()) return false;
 if (!Query)
 {
  const auto Local=BuildNavigationQuery();
  return ConnectPoses(From,To,Route,Cost,&Local);
 }
 // Native turn-in-place rotates around the undriven front axle, not the actor pivot.
 if (Query->bHasPivot)
 {
  const FVector Pivot=Query->Pivot;
  const float Angle=FMath::FindDeltaAngleDegrees(From.Rotator().Yaw,To.Rotator().Yaw);
  if (FMath::Abs(Angle)>.1f && FVector::Dist2D(From.TransformPosition(Pivot),To.TransformPosition(Pivot))<1.f)
  {
   const FVector Axle=From.TransformPosition(Pivot);
   const int32 Samples=FMath::CeilToInt(FMath::Abs(Angle)/2.f);
   for (int32 I=1;I<=Samples;++I)
   {
    const FRotator Turn(0,From.Rotator().Yaw+Angle*I/Samples,0);
    FVector Point=Axle-Turn.RotateVector(Pivot);Point.Z=A.Z;
    if (!NavigationClear(FTransform(Turn,Point),Query)) return false;
   }
   FWarehouseRoutePoint Step;Step.Pose=To;Step.bPivot=true;Route.Add(Step);
   Cost=FMath::Abs(FMath::DegreesToRadians(Angle))*MinimumTurningRadiusCm;
   return true;
  }
 }
 if (Distance<.1f)
 {
  if (FMath::Abs(FMath::FindDeltaAngleDegrees(From.Rotator().Yaw,To.Rotator().Yaw))>.1f) return false;
  Cost=0; return true;
 }
 for (float Direction : {1.f,-1.f})
  for (float Handle : {Distance/3.f,FMath::Max(Distance*.5f,MinimumTurningRadiusCm),FMath::Max(Distance,MinimumTurningRadiusCm*2)})
  {
   const FVector B=A+From.GetUnitAxis(EAxis::X)*Handle*Direction;
   const FVector C=D-To.GetUnitAxis(EAxis::X)*Handle*Direction;
   const int32 Samples=FMath::Clamp(FMath::CeilToInt((Distance+2*Handle)/5.f),2,2000);
   TArray<FWarehouseRoutePoint> Candidate;
   FTransform Previous=From;
   float Length=0;
   bool Valid=true;
   for (int32 I=1; I<=Samples; ++I)
   {
    const float T=float(I)/Samples,S=1-T;
    const FVector P=S*S*S*A+3*S*S*T*B+3*S*T*T*C+T*T*T*D;
    const FVector V=3*S*S*(B-A)+6*S*T*(C-B)+3*T*T*(D-C);
    const FVector Acc=6*S*(C-2*B+A)+6*T*(D-2*C+B);
    const float Curvature=FMath::Abs(V.X*Acc.Y-V.Y*Acc.X)/FMath::Max(.001f,FMath::Pow(V.Size2D(),3.f));
    if (V.Size2D()<.01f || Curvature>1.f/FMath::Max(117.3f,MinimumTurningRadiusCm)+.00001f) { Valid=false; break; }
    const FRotator Heading(0,(V*Direction).Rotation().Yaw,0);
    FWarehouseRoutePoint Step; Step.Pose=FTransform(Heading,P); Step.bReverse=Direction<0;
    const float Segment=FVector::Dist2D(P,Previous.GetLocation());
    // A collinear cubic can double back through a zero-speed cusp between
    // samples while its sampled curvature is zero. A fixed-direction vehicle
    // cannot execute that sudden 180-degree tangent change.
    const float Turn=FMath::Abs(FMath::FindDeltaAngleDegrees(Previous.Rotator().Yaw,Heading.Yaw))*PI/180.f;
    if (Segment>10 || Turn>Segment/FMath::Max(117.3f,MinimumTurningRadiusCm)+.02f ||
        (!Query->bGeometryFirst && !NavigationClear(Step.Pose,Query))) { Valid=false; break; }
    Length+=Segment;
    Candidate.Add(Step); Previous=Step.Pose;
    // Remaining segments cannot reduce accumulated cost. Keep close floating
    // point ties and the original query order for every competitive candidate.
    if (Cost<FLT_MAX && Length*(Direction<0 ? 1.15f : 1.f)>Cost+.01f) { Valid=false;break; }
   }
   const float Weighted=Length*(Direction<0 ? 1.15f : 1.f);
   if (Query->bGeometryFirst)
   {
    if (!Valid || Weighted>=Cost) ++Query->GeometryRejectedCandidates;
    else for (const auto& Step : Candidate)
     if (!NavigationClear(Step.Pose,Query)) { Valid=false;break; }
   }
   if (Valid && Weighted<Cost) { Route=MoveTemp(Candidate); Cost=Weighted; }
  }
 return Cost<FLT_MAX;
}
bool AWarehouseForklift::ConnectLanePoses(const FTransform& Start,const FTransform& Goal,TArray<FWarehouseRoutePoint>& Route,float& Cost,const FWarehouseNavigationQuery* Query) const
{
 if (!Query)
 {
  const auto Local=BuildNavigationQuery();
  return ConnectLanePoses(Start,Goal,Route,Cost,&Local);
 }
 if (ConnectPoses(Start,Goal,Route,Cost,Query)) return true;
 Route.Reset();Cost=FLT_MAX;
 // Every lane junction can use the same physically swept pivot/straight/pivot
 // connection. Curved-only graph edges could not turn into the west staging aisle.
 if (Query->bHasPivot)
 {
  const FVector LocalPivot=Query->Pivot;
  const float Bearing=(Goal.TransformPosition(LocalPivot)-Start.TransformPosition(LocalPivot)).Rotation().Yaw;
  for (float Yaw : {Bearing,Bearing+180.f})
  {
   const FRotator Heading(0,Yaw,0);
   FVector StartPoint=Start.TransformPosition(LocalPivot)-Heading.RotateVector(LocalPivot);StartPoint.Z=Start.GetLocation().Z;
   FVector EndPoint=Goal.TransformPosition(LocalPivot)-Heading.RotateVector(LocalPivot);EndPoint.Z=Goal.GetLocation().Z;
   const FTransform TurnedStart(Heading,StartPoint),TurnedEnd(Heading,EndPoint);
   TArray<FWarehouseRoutePoint> First,Travel,Last;float A,B,C;
   if (ConnectPoses(Start,TurnedStart,First,A,Query) && ConnectPoses(TurnedStart,TurnedEnd,Travel,B,Query) && ConnectPoses(TurnedEnd,Goal,Last,C,Query) && A+B+C<Cost)
   { Route=MoveTemp(First);Route.Append(Travel);Route.Append(Last);Cost=A+B+C; }
  }
 }
 return Cost<FLT_MAX;
}
bool AWarehouseForklift::PlanAutonomousRoute(const FTransform& Goal)
{
 RouteObstacleSnapshot=INDEX_NONE;
 if (IsRemoteControlled() || SensorMode!=EWarehouseSensorMode::Autonomous)
 {
  PlannedRoute.Reset();RouteIndex=0;bRouteStarted=false;
  return false;
 }
 CSV_SCOPED_TIMING_STAT(Warehouse,RoutePlanning);
 CSV_CUSTOM_STAT(Warehouse,RoutePlans,1,ECsvCustomStatOp::Accumulate);
 const uint32 QueryStart=WarehouseNavigationQueryCount;
 ON_SCOPE_EXIT { CSV_CUSTOM_STAT(Warehouse,NavigationQueries,int32(WarehouseNavigationQueryCount-QueryStart),ECsvCustomStatOp::Accumulate); };
 if (!FleetCoordinator) FleetCoordinator=AWarehouseFleetCoordinator::Find(this);
 TGuardValue<bool> Planning(bPlanningRoute,true);
 // No game-thread sensor/layout/time update can interleave this synchronous
 // search. Restore the optional on every exit; never reuse it in another tick.
 TGuardValue<TOptional<bool>> Readiness(PlanningSensorReadiness,TOptional<bool>(AutonomousSensorsReady()));
 auto Query=BuildNavigationQuery();
 ON_SCOPE_EXIT { CSV_CUSTOM_STAT(Warehouse,GeometryRejectedCandidates,int32(Query.GeometryRejectedCandidates),ECsvCustomStatOp::Accumulate); };
 // AGV localization: plan from the estimate (an insertion-retry escape keeps the exact pose it measures from).
 const FTransform StartPose=bUseAgvLocalization && !Query.bInsertionRetryEscape ? NavigationTransform() : GetActorTransform();
 const FVector RouteStart=StartPose.GetLocation();
 const bool Diagnostics=FParse::Param(FCommandLine::Get(),TEXT("DriveDiagnostics"));
 Query.bDiagnostics=Diagnostics;Query.DiagnosticGoal=Goal.GetLocation();
 int32 CandidateCount=0,StartConnections=0,GoalConnections=0;bool Found=false;
 ON_SCOPE_EXIT
 {
  if (Found && Query.bInsertionRetryEscape) RecoveryStart=RouteStart;
  if (!Found)
   UE_LOG(LogTemp,Log,TEXT("AGV_PLAN_NO_ROUTE %s from=%s goal=%s candidates=%d start_edges=%d goal_edges=%d start_clear=%d goal_clear=%d"),
    *VehicleName,*GetActorLocation().ToString(),*Goal.GetLocation().ToString(),CandidateCount,StartConnections,GoalConnections,
    NavigationClear(GetActorTransform(),&Query),NavigationClear(Goal,&Query));
 };
 PlannedRoute.Reset(); RouteIndex=0;bEndpointDriving=false;bEndpointOrienting=false;bRouteTurning=false;EndpointSegment=-1;
 float DirectCost;
 if (ConnectLanePoses(StartPose,Goal,PlannedRoute,DirectCost,&Query)) { Found=true;return true; }
 // Pose graph: each edge is a swept, curvature-limited cubic. Anchors describe warehouse travel lanes.
 // ponytail: at most 40 lane poses; use incremental hybrid A* for a large unstructured warehouse.
 TArray<FTransform> Nodes{StartPose,Goal};
 if (Query.bHasPivot)
 {
  const FVector Pivot=Query.Pivot;
  const float Bearing=(Goal.GetLocation()-RouteStart).Rotation().Yaw;
  for (const FTransform& Base : {StartPose,Goal}) for (float Yaw : {Bearing,Bearing+180.f})
  {
   const FRotator Heading(0,Yaw,0);
   FVector Point=Base.TransformPosition(Pivot)-Heading.RotateVector(Pivot);Point.Z=Base.GetLocation().Z;
   const FTransform Turned(Heading,Point);
   if (NavigationClear(Turned,&Query)) Nodes.Add(Turned);
  }
 }
 int32 FloorCandidates=0;
 for (const auto& Anchor : NavigationAnchors)
  if (FMath::Abs(Anchor.GetLocation().Z-RouteStart.Z)<1.f)
  {
   if (++FloorCandidates>40) break;
   if (NavigationClear(Anchor,&Query)) Nodes.Add(Anchor);
  }
 CandidateCount=Nodes.Num()-2;
 // Layout recovery must not depend solely on the lanes saved before a rack moved.
 // Generate a bounded set from available obstacle data. In sensor mode even
 // a static rack needs an actual observation; its hidden true bounds are not a map.
 if (bUseCurrentRackWaypoints)
 {
  TArray<FTransform> RackCandidates;
  TArray<FBox> CandidateBounds;
  if (SensorMode==EWarehouseSensorMode::Autonomous)
  {
   if (SensorKit) for (const auto& Observed : SensorKit->GetObservedObstacles())
    if (!Observed.bPerson && Observed.Actor.Get()!=this && !Query.PerceptionIgnored.Contains(Observed.Actor.Get()))
     CandidateBounds.Add(Observed.WorldBounds);
  }
  else if (auto* Strength=AWarehouseDamageSystem::Find(this))
   for (const auto& Spec : Strength->Objects) if (Spec.Failure==EWarehouseFailure::Rack)
   {
    FBox Bounds(ForceInit);
    for (AActor* Member : Spec.Members) if (IsValid(Member))
    {
     const FBox Part=AWarehouseDamageSystem::GetPhysicalBounds(Member);
     if (Part.IsValid) Bounds+=Part;
    }
    CandidateBounds.Add(Bounds);
   }
  for (const FBox& Bounds : CandidateBounds)
  {
    if (!Bounds.IsValid || Bounds.Min.Z>RouteStart.Z+Query.Envelope.Max.Z || Bounds.Max.Z<RouteStart.Z+3) continue;
    const float Clearance=FMath::Max(Query.Envelope.Min.Size2D(),Query.Envelope.Max.Size2D())+10.f;
    const FBox Around=Bounds.ExpandBy(FVector(Clearance,Clearance,0));
    const FVector Center=Around.GetCenter();
    for (float X : {float(Around.Min.X),float(Center.X),float(Around.Max.X)})
     for (float Y : {float(Around.Min.Y),float(Center.Y),float(Around.Max.Y)})
     {
      if (X==Center.X && Y==Center.Y) continue;
      const FVector Point(X,Y,RouteStart.Z);
      if (FVector::Dist2D(Point,RouteStart)+FVector::Dist2D(Point,Goal.GetLocation())>
          FVector::Dist2D(RouteStart,Goal.GetLocation())+2400.f) continue;
      RackCandidates.Add(FTransform(FRotator(0,Goal.Rotator().Yaw,0),Point));
     }
   }
  RackCandidates.Sort([&](const FTransform& A,const FTransform& B)
  {
   auto Score=[&](const FTransform& P) { return FVector::Dist2D(P.GetLocation(),RouteStart)+FVector::Dist2D(P.GetLocation(),Goal.GetLocation()); };
   return Score(A)<Score(B);
  });
  int32 Added=0,Checked=0;
  for (const FTransform& Candidate : RackCandidates)
  {
   if (++Checked>192 || Added>=48) break;
   if (Nodes.ContainsByPredicate([&](const FTransform& P){return FVector::Dist2D(P.GetLocation(),Candidate.GetLocation())<30.f;})) continue;
   if (NavigationClear(Candidate,&Query)) { Nodes.Add(Candidate);++Added; }
  }
  CandidateCount=Nodes.Num()-2;
 }
 TArray<float> Costs; Costs.Init(FLT_MAX,Nodes.Num()); Costs[0]=0;
 TArray<bool> Closed; Closed.Init(false,Nodes.Num());
 TArray<TArray<FWarehouseRoutePoint>> Paths; Paths.SetNum(Nodes.Num());
 for (int32 N=0; N<Nodes.Num(); ++N)
 {
  int32 Current=INDEX_NONE; float Best=FLT_MAX;
  for (int32 I=0; I<Nodes.Num(); ++I)
   if (!Closed[I] && Costs[I]<FLT_MAX)
   {
    const float Score=Costs[I]+FVector::Dist2D(Nodes[I].GetLocation(),Goal.GetLocation());
    if (Score<Best) { Best=Score; Current=I; }
   }
  if (Current==INDEX_NONE) break;
  if (Current==1) { PlannedRoute=MoveTemp(Paths[1]); Found=true;return true; }
  Closed[Current]=true;
  for (int32 Next=1; Next<Nodes.Num(); ++Next) if (!Closed[Next])
  {
   const float Distance=FVector::Dist2D(Nodes[Current].GetLocation(),Nodes[Next].GetLocation());
   if ((Current==0 && Next==1) || (Next!=1 && Distance>1100.f) || Costs[Current]+Distance>=Costs[Next]) continue;
   TArray<FWarehouseRoutePoint> Edge; float EdgeCost;
   if (ConnectLanePoses(Nodes[Current],Nodes[Next],Edge,EdgeCost,&Query) && Costs[Current]+EdgeCost<Costs[Next])
   {
    if (Current==0) ++StartConnections;
    if (Next==1) ++GoalConnections;
    Costs[Next]=Costs[Current]+EdgeCost;
    Paths[Next]=Paths[Current]; Paths[Next].Append(Edge);
   }
  }
 }
 if (Diagnostics)
 {
  FString NodesReport;
  for (int32 I=0;I<Nodes.Num();++I)
   NodesReport+=FString::Printf(TEXT(" [%d:%s yaw=%.1f reachable=%d cost=%.1f]"),I,*Nodes[I].GetLocation().ToString(),
    Nodes[I].Rotator().Yaw,Costs[I]<FLT_MAX,Costs[I]<FLT_MAX ? Costs[I] : -1.f);
  UE_LOG(LogTemp,Log,TEXT("AGV_PLAN_NODES %s%s"),*VehicleName,*NodesReport);
 }
 return false;
}
bool AWarehouseForklift::FollowRoute(float Speed,float Dt)
{
 if (PlannedRoute.IsEmpty()) { BrakeDrive(); return true; }
 // A temporary traffic retreat is a normal lane stop, even when it interrupts
 // a charging trip. It must not use the electrical approach's precision phase.
 const bool ChargingAlignment=!bTrafficYielding &&
  (AIState==EWarehouseAIState::NavigateCharge || (AIState==EWarehouseAIState::DockCharge && bDockPullout));
 const float HeadingTolerance=FMath::DegreesToRadians(ChargingAlignment && RouteIndex>=PlannedRoute.Num()-1 ? .2f : AIState==EWarehouseAIState::AlignUnload || AIState==EWarehouseAIState::ElevatorApproach ? .5f : 1.f);
 if (FleetCoordinator && !FleetCoordinator->Permit(this)) return false;
 RouteIndex=FMath::Min(RouteIndex,PlannedRoute.Num()-1);
 // AGV localization: route travel decisions use NavigationTransform(). Alignment, docking, elevator approach and the
 // last 80cm keep the exact pose (the vehicle's precise docking/fork sensors); collision forecasts stay exact.
 const bool bPreciseState=ChargingAlignment || AIState==EWarehouseAIState::AlignUnload || AIState==EWarehouseAIState::AlignVehicle ||
  AIState==EWarehouseAIState::ElevatorApproach;
 const FTransform TravelPose=bPreciseState ? GetActorTransform() : NavigationTransform();
 FString LocalizationReason;
 if (!bPreciseState && AgvLocalizationBlocked(LocalizationReason)) { BrakeDrive(); StopFor(LocalizationReason); return false; }
 if (PlannedRoute[RouteIndex].bPivot)
 {
  const float Angle=FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(TravelPose.Rotator().Yaw,PlannedRoute[RouteIndex].Pose.Rotator().Yaw));
  BrakeDrive();
  // Stop before starting the pivot, not on every tick of the pivot: the
  // actor origin moves around the load-wheel axle during a physical turn.
  if (!bRouteTurning && FMath::Abs(CurrentSpeedCm)>1.f) return false;
  bRouteTurning=true;
  if (FMath::Abs(Angle)>HeadingTolerance)
  { CommandTurn(FMath::Sign(Angle)*FMath::Clamp(FMath::Abs(Angle)*2.f,.02f,.15f),Dt);return false; }
  if (FMath::Abs(CurrentSpeedCm)>1.f) return false;
  if (ChassisBody && FMath::Abs(ChassisBody->GetPhysicsAngularVelocityInDegrees().Z)>.5f) return false;
  bRouteTurning=false;
  if (RouteIndex==PlannedRoute.Num()-1) return true;
  ++RouteIndex;
 }
 const FTransform FinalGoal=PlannedRoute.Last().Pose;
 int32 SegmentEnd=PlannedRoute.Num()-1;
 for (int32 I=RouteIndex+1;I<PlannedRoute.Num();++I)
  if (PlannedRoute[I].bPivot || PlannedRoute[I].bReverse!=PlannedRoute[RouteIndex].bReverse)
  { SegmentEnd=I-1;break; }
 const auto& Goal=PlannedRoute[SegmentEnd];
 if (EndpointSegment!=SegmentEnd) { EndpointSegment=SegmentEnd;bEndpointDriving=false;bEndpointOrienting=false;bRouteTurning=false; }
 const FTransform GoalPose=Goal.Pose;
 const FTransform Pose=FVector::Dist2D(GetActorLocation(),Goal.Pose.GetLocation())<80.f ? GetActorTransform() : TravelPose;
 const float Remaining=FVector::Dist2D(Pose.GetLocation(),Goal.Pose.GetLocation());
 const float HeadingError=FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(Pose.Rotator().Yaw,Goal.Pose.Rotator().Yaw));
 // Tighten heading for the loaded pallet's lever arm while retaining enough
 // position margin for nonholonomic steering. Final pallet acceptance stays 2cm.
 const float PositionTolerance=ChargingAlignment && SegmentEnd==PlannedRoute.Num()-1 ? .5f : AIState==EWarehouseAIState::AlignUnload ? 1.5f : AIState==EWarehouseAIState::AlignVehicle && AgvEstimateInUse() ? 4.f : 5.f;
 // Inside the charging capture region align to its fixed line heading before
 // handing off. Chasing a sub-centimetre point bearing repeatedly slews the
 // real steering spindle and can drift out of the docking lateral envelope.
 if (ChargingAlignment && SegmentEnd==PlannedRoute.Num()-1 && Remaining<ChargingCaptureDistanceCm &&
     FMath::Abs(FVector::DotProduct(GetActorLocation()-GoalPose.GetLocation(),GoalPose.GetUnitAxis(EAxis::Y)))<ChargingDockLateralLimitCm)
  bEndpointOrienting=true;
 if (Remaining<80.f && Wheels.Num()>=3 && AIState!=EWarehouseAIState::AlignUnload)
 {
  // Finish a lane pose at the real load-wheel axle. Rotating at the actor
  // origin moved the origin away from the goal and repeatedly replanned tiny
  // pivot/straight/pivot corrections without ever settling.
  const FVector Axle=(Wheels[1]->GetComponentLocation()+Wheels[2]->GetComponentLocation())*.5f;
  const FVector LocalAxle=GetActorTransform().InverseTransformPosition(Axle);
  const FVector Offset=GoalPose.TransformPosition(LocalAxle)-Axle;
  const float Distance=Offset.Size2D();
  // The following pivot supplies its own heading. Straightening first at
  // this intermediate stop caused repeated short turn/drive corrections.
  if (SegmentEnd+1<PlannedRoute.Num() && PlannedRoute[SegmentEnd+1].bPivot && Distance<PositionTolerance)
  {
   BrakeDrive();bEndpointDriving=false;
   if (FMath::Abs(CurrentSpeedCm)<1.f && ChassisBody->GetPhysicsAngularVelocityInDegrees().Size()<.5f)
   { RouteIndex=SegmentEnd+1;bRouteTurning=false; }
   return false;
  }
  if (!bEndpointOrienting && Distance>PositionTolerance*.7f)
  {
   // A small braking overshoot is corrected in reverse without turning the
   // whole loaded vehicle around beside the pallet.
   const float Sign=FVector::DotProduct(Offset,GetActorForwardVector())<0.f ? -1.f : 1.f;
   const float Bearing=Offset.Rotation().Yaw+(Sign<0 ? 180.f : 0.f);
   const float Turn=FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,Bearing));
   // Let the physical yaw stop before straightening the steered wheel.
   // Separate enter/leave angles prevent turn/drive chatter during its slew.
   if (bEndpointDriving && FMath::Abs(Turn)>FMath::DegreesToRadians(25.f)) bEndpointDriving=false;
   if (!bEndpointDriving)
   {
    BrakeDrive();
    if (FMath::Abs(Turn)>FMath::DegreesToRadians(3.f))
     CommandTurn(FMath::Sign(Turn)*FMath::Clamp(FMath::Abs(Turn)*1.5f,.04f,.2f),Dt);
    else if (ChassisBody && FMath::Abs(ChassisBody->GetPhysicsAngularVelocityInDegrees().Z)<.5f) bEndpointDriving=true;
   }
   else
   {
    const float Side=FVector::DotProduct(Offset,GetActorRightVector());
    CommandDrive(Sign*FMath::Min3(Speed,20.f,Distance*1.5f),2.f*Side/FMath::Max(400.f,Offset.SizeSquared2D()),Dt);
   }
   return false;
  }
  // Complete final orientation before correcting any drift from that pivot.
  // Interrupting the turn at the position boundary caused repeated steering
  // reversals and prevented either phase from completing.
  bEndpointOrienting=true;bEndpointDriving=false;BrakeDrive();
  if (!bRouteTurning && FMath::Abs(CurrentSpeedCm)>=1.f) return false;
  bRouteTurning=true;
  if (FMath::Abs(HeadingError)>=HeadingTolerance)
  { CommandTurn(FMath::Sign(HeadingError)*FMath::Clamp(FMath::Abs(HeadingError)*2.f,.02f,.2f),Dt);return false; }
  if (FMath::Abs(CurrentSpeedCm)>=1.f) return false;
  if (ChassisBody && FMath::Abs(ChassisBody->GetPhysicsAngularVelocityInDegrees().Z)>=.5f) return false;
  bRouteTurning=false;
  if (Remaining>=PositionTolerance) { bEndpointOrienting=false;return false; }
  if (SegmentEnd==PlannedRoute.Num()-1) return true;
  RouteIndex=SegmentEnd+1;return false;
 }
 if (Remaining<PositionTolerance)
 {
  BrakeDrive();
  if (!bRouteTurning && FMath::Abs(CurrentSpeedCm)>=1.f) return false;
  bRouteTurning=true;
  if (FMath::Abs(HeadingError)<HeadingTolerance)
  {
   if (FMath::Abs(CurrentSpeedCm)>=1.f || (ChassisBody && FMath::Abs(ChassisBody->GetPhysicsAngularVelocityInDegrees().Z)>=.5f)) return false;
   bRouteTurning=false;
   if (SegmentEnd==PlannedRoute.Num()-1) return true;
   RouteIndex=SegmentEnd+1;return false;
  }
  CommandTurn(FMath::Sign(HeadingError)*FMath::Clamp(FMath::Abs(HeadingError)*2.f,.02f,.15f),Dt);
  return false;
 }
 // A force-driven vehicle can overshoot. Brake and plan a short reverse
 // correction instead of chasing a forward waypoint behind the truck.
 const float GoalAlong=FVector::DotProduct(GoalPose.GetLocation()-Pose.GetLocation(),GoalPose.GetUnitAxis(EAxis::X))*(Goal.bReverse ? -1.f : 1.f);
 if (Remaining<150.f && GoalAlong<-1.f)
 {
  // Correct small braking overshoot with a slow physical reverse. A full
  // pose-graph search for a few centimetres can stall the game thread.
  if (Remaining<20.f && FMath::Abs(HeadingError)<FMath::DegreesToRadians(5.f) &&
      FMath::Abs(FVector::DotProduct(GoalPose.GetLocation()-Pose.GetLocation(),Pose.GetUnitAxis(EAxis::Y)))<5.f)
  { MovePrecise(GoalPose.GetLocation(),5.f,Dt);return false; }
  BrakeDrive();
  if (FMath::Abs(CurrentSpeedCm)<2.f && !PlanAutonomousRoute(FinalGoal)) StopFor(TEXT("OBSTACLE / OVERSHOOT REALIGNMENT"));
  return false;
 }
 RouteIndex=FMath::Min(RouteIndex,PlannedRoute.Num()-1);
 while (RouteIndex+1<PlannedRoute.Num() && !PlannedRoute[RouteIndex+1].bPivot && PlannedRoute[RouteIndex+1].bReverse==PlannedRoute[RouteIndex].bReverse &&
        FVector::DistSquared2D(Pose.GetLocation(),PlannedRoute[RouteIndex+1].Pose.GetLocation())<FVector::DistSquared2D(Pose.GetLocation(),PlannedRoute[RouteIndex].Pose.GetLocation())) ++RouteIndex;
 const float LookAhead=FMath::Clamp(FMath::Abs(CurrentSpeedCm)*.8f,40.f,145.f);
 int32 Aim=RouteIndex;
 float Ahead=0;
 while (Aim+1<PlannedRoute.Num() && !PlannedRoute[Aim+1].bPivot && Ahead<LookAhead && PlannedRoute[Aim+1].bReverse==PlannedRoute[Aim].bReverse)
 { Ahead+=FVector::Dist2D(PlannedRoute[Aim].Pose.GetLocation(),PlannedRoute[Aim+1].Pose.GetLocation()); ++Aim; }
 const auto& Step=PlannedRoute[Aim];
 const FVector Local=Pose.InverseTransformPosition(Step.Pose.GetLocation());
 const float Sign=Step.bReverse ? -1.f : 1.f;
 const float Limit=bSupportingPallet ? FMath::Min(100.f,LoadedTravelSpeedCm) : FMath::Min(180.f,EmptyTravelSpeedCm);
 float Desired=FMath::Min(FMath::Min(Speed,Limit),FMath::Sqrt(240.f*Remaining));
 if (Remaining<20.f) Desired=FMath::Min(Desired,Remaining*1.8f);
 float Curvature=2.f*Local.Y/FMath::Max(900.f,Local.SizeSquared2D());
 if (Remaining<40.f) Curvature+=Sign*HeadingError/40.f;
 Desired=FMath::Min(Desired,FMath::Sqrt((bSupportingPallet ? 40.f : 80.f)/FMath::Max(.0001f,FMath::Abs(Curvature))));
 const FVector Preview=GetActorLocation()+GetActorForwardVector()*Sign*Desired*Dt;
 if (ConsumeObstacleSnapshot(RouteObstacleSnapshot))
 {
  const FTransform PreviewPose(GetActorQuat(),Preview);
  bRouteObstacleClear=NavigationClear(PreviewPose);
  if (bRouteObstacleClear && SensorMode==EWarehouseSensorMode::Autonomous)
  {
   // Follow the actual planned bends, rather than sweeping a straight beam
   // across the adjacent rack. Cover the whole next scan interval, existing
   // conservative braking distance and measurement margin at the wanted speed.
   const float Forecast=Desired*1.1f+Desired*Desired/100.f+20.f;
   float Covered=0;
   FTransform Previous=GetActorTransform();
   for (int32 I=RouteIndex;I<PlannedRoute.Num() && Covered<Forecast;++I)
   {
    const FTransform& End=PlannedRoute[I].Pose;
    const float Length=FVector::Dist2D(Previous.GetLocation(),End.GetLocation());
    const float Use=FMath::Min(Length,Forecast-Covered);
    const int32 Samples=FMath::Max(1,FMath::CeilToInt(Use/20.f));
    for (int32 Sample=1;Sample<=Samples;++Sample)
    {
     const float Alpha=Length>KINDA_SMALL_NUMBER ? (Use/Length)*(float(Sample)/Samples) : 1.f;
     const FTransform Candidate(FQuat::Slerp(Previous.GetRotation(),End.GetRotation(),Alpha),
      FMath::Lerp(Previous.GetLocation(),End.GetLocation(),Alpha));
     if (!NavigationClear(Candidate)) { bRouteObstacleClear=false;break; }
    }
    if (!bRouteObstacleClear) break;
    Covered+=Length;Previous=End;
   }
  }
  // Match the recovered alignment plan's short physical escape, measured
  // from its fixed start rather than granting another 60cm on every tick.
  if (!bRouteObstacleClear && AIState==EWarehouseAIState::AlignVehicle && bRetryInsertion &&
      ForkInsertionRetries>0 && !bSupportingPallet && FVector::DistSquared2D(Preview,RecoveryStart)<60.f*60.f)
   bRouteObstacleClear=PhysicalPoseClear(PreviewPose,true);
 }
 if (!bRouteObstacleClear) { StopFor(TEXT("OBSTACLE ON ROUTE")); return false; }
 TGuardValue<bool> ForecastFromRoute(bRouteSensorForecast,SensorMode==EWarehouseSensorMode::Autonomous);
 if (!CommandDrive(Sign*Desired,Curvature,Dt)) return false;
 WaitSeconds=0;
 return false;
}
bool AWarehouseForklift::MovePrecise(FVector Destination,float Speed,float Dt)
{
 return MoveToLine(Destination,Speed,Dt);
}
bool AWarehouseForklift::SeePallet(FTransform& Pose) const
{
 if (SensorMode==EWarehouseSensorMode::Autonomous)
 {
  if (!AutonomousSensorsReady() || !bObservedTargetValid) return false;
  Pose=SensorObservedTarget;return true;
 }
 if (!IsValid(TargetPallet) || !bPalletSensorHealthy) return false;
 const FVector Delta=TargetPallet->GetActorLocation()-GetActorLocation();
 if (Delta.Size()>PalletDetectionRangeCm || FVector::DotProduct(Delta.GetSafeNormal2D(),GetActorForwardVector())<.5f) return false;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(PalletCamera),true,this);
 const FVector Origin=GetActorLocation()+GetActorForwardVector()*10+FVector(0,0,90);
 bool Seen=false;
 for (float Y : {-40.f,0.f,40.f})
 {
  const FVector Point=TargetPallet->GetActorTransform().TransformPosition(FVector(-53,Y,8));
  FHitResult Hit;
  if (GetWorld()->LineTraceSingleByChannel(Hit,Origin,Point,ECC_Visibility,Params) && Hit.GetActor()==TargetPallet) Seen=true;
 }
 if (!Seen) return false;
 Pose=TargetPallet->GetActorTransform();
 Pose.AddToTranslation(PalletPositionBiasCm);
 FRotator Rotation=Pose.Rotator(); Rotation.Yaw+=PalletYawBiasDegrees; Pose.SetRotation(Rotation.Quaternion());
 return true;
}
namespace
{
 bool FloorFootprintFits(const TArray<AActor*>& Loads,const FTransform& Slot,float EdgeMargin=0.f)
 {
  const float HalfExtent=FMath::Max(0.f,AWarehouseForklift::FloorSlotHalfExtentCm-EdgeMargin);
  for (AActor* Actor : Loads)
  {
   if (!IsValid(Actor) || !Actor->GetActorEnableCollision()) return false;
   TArray<UStaticMeshComponent*> Parts;Actor->GetComponents(Parts);
   bool Found=false;
   for (auto* Part : Parts) if (WarehousePhysics::IsPhysicalContact(Part))
   {
    const auto* Setup=Part->GetPhysicsBodySetup();
    if (!Setup) return false;
    const FBox Local=Setup->AggGeom.CalcAABB(FTransform::Identity);
    if (!Local.IsValid) return false;
    Found=true;
    // Transform the native shape's local bounds once. Rotating a world AABB
    // again would spuriously enlarge a square load on a diagonal floor lane.
    for (int32 Corner=0;Corner<8;++Corner)
    {
     const FVector Point=Slot.InverseTransformPosition(Part->GetComponentTransform().TransformPosition(FVector(
      Corner&1 ? Local.Max.X : Local.Min.X,Corner&2 ? Local.Max.Y : Local.Min.Y,Corner&4 ? Local.Max.Z : Local.Min.Z)));
     if (FMath::Abs(Point.X)>HalfExtent+.01f || FMath::Abs(Point.Y)>HalfExtent+.01f) return false;
    }
   }
   if (!Found) return false;
  }
  return !Loads.IsEmpty();
 }
}

bool AWarehouseForklift::IsFloorFootprintValid(AWarehousePallet* Pallet,const FTransform& Reference,bool Refresh) const
{
 auto* Strength=AWarehouseDamageSystem::Find(this);
 if (!IsValid(Pallet) || !Strength || Reference.ContainsNaN() || !Reference.GetScale3D().Equals(FVector::OneVector)) return false;
 TArray<AActor*> Loads{Pallet};
 for (AActor* Actor : Strength->GetSupportedActors(Pallet,Refresh)) if (Cast<AWarehouseCargo>(Actor)) Loads.Add(Actor);
 return FloorFootprintFits(Loads,Reference);
}

bool AWarehouseForklift::IsFloorPlacementValid(AWarehousePallet* Pallet,const FTransform& Destination,bool bForksStillInserted,bool Refresh) const
{
 auto* Strength=AWarehouseDamageSystem::Find(this);
 if (!IsValid(Pallet) || !Strength || Strength->HasFailed(Pallet) || Destination.ContainsNaN() ||
     !Destination.GetScale3D().Equals(FVector::OneVector) || FMath::Abs(Destination.Rotator().Pitch)>.1f ||
     FMath::Abs(Destination.Rotator().Roll)>.1f || Pallet->GetAttachParentActor()) return false;
 TArray<AActor*> Loads{Pallet};
 for (AActor* Actor : Strength->GetSupportedActors(Pallet,Refresh)) if (Cast<AWarehouseCargo>(Actor)) Loads.Add(Actor);
 if (!FloorFootprintFits(Loads,Destination)) return false;
 const FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(Pallet);
 // Solver contact slop is not a floating load or a fall through the slab.
 if (!Bounds.IsValid || FMath::Abs(Bounds.Min.Z-Destination.GetLocation().Z)>.5f ||
     FMath::Abs(Pallet->GetActorRotation().Pitch)>2.f || FMath::Abs(Pallet->GetActorRotation().Roll)>2.f) return false;
 FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(FloorHandoffSupport),false);
 FloorParams.AddIgnoredActors(Loads);FloorParams.AddIgnoredActor(this);
 FComponentQueryParams ContactParams(SCENE_QUERY_STAT(FloorHandoffContact));
 ContactParams.AddIgnoredActors(Loads);
 if (bForksStillInserted) ContactParams.AddIgnoredActor(this);
 for (float X : {-40.f,40.f}) for (float Y : {-40.f,40.f})
 {
  FVector Point=Pallet->GetActorTransform().TransformPosition(FVector(X,Y,0));Point.Z=Destination.GetLocation().Z;
  FHitResult Floor;
  if (!GetWorld()->LineTraceSingleByChannel(Floor,Point+FVector(0,0,1),Point-FVector(0,0,2),ECC_Visibility,FloorParams) ||
      Floor.ImpactNormal.Z<.98f || FMath::Abs(Floor.ImpactPoint.Z-Point.Z)>.5f ||
      !Floor.GetComponent() || Floor.GetComponent()->IsSimulatingPhysics()) return false;
  // Ignore only the traced bearing component, never every part of its actor.
  ContactParams.AddIgnoredComponent(Floor.GetComponent());
 }
 for (AActor* Actor : Loads)
 {
  if (Strength->HasFailed(Actor) || Actor->GetAttachParentActor()) return false;
  TArray<UStaticMeshComponent*> Parts;Actor->GetComponents(Parts);
  for (auto* Part : Parts) if (WarehousePhysics::IsPhysicalContact(Part))
  {
   if (!Part->IsSimulatingPhysics() || Part->GetPhysicsLinearVelocity().Size()>5.f ||
       Part->GetPhysicsAngularVelocityInDegrees().Size()>5.f) return false;
   TArray<FOverlapResult> Hits;
   GetWorld()->ComponentOverlapMultiByChannel(Hits,Part,Part->GetComponentLocation(),Part->GetComponentQuat(),ECC_Visibility,ContactParams);
   for (const auto& Hit : Hits) if (Hit.bBlockingHit && Hit.GetComponent())
   {
    const FBox Own=Part->Bounds.GetBox(),Other=Hit.GetComponent()->Bounds.GetBox();
    const FVector Depth(FMath::Min(Own.Max.X,Other.Max.X)-FMath::Max(Own.Min.X,Other.Min.X),
     FMath::Min(Own.Max.Y,Other.Max.Y)-FMath::Max(Own.Min.Y,Other.Min.Y),
     FMath::Min(Own.Max.Z,Other.Max.Z)-FMath::Max(Own.Min.Z,Other.Min.Z));
    if (Depth.X>.05f && Depth.Y>.05f && Depth.Z>.05f) return false;
   }
  }
 }
 return true;
}

bool AWarehouseForklift::DestinationClear(const TArray<AActor*>* Traffic) const
{
 const FTransform& Pose=ActiveJob.Destination;
 const bool MechanicalHandover=AIState==EWarehouseAIState::WithdrawFork || AIState==EWarehouseAIState::VerifyUnload;
 if (SensorMode==EWarehouseSensorMode::Autonomous && !MechanicalHandover)
 {
  // The WMS reserves a slot, not omniscient knowledge of every object at it.
  // Actual beam/floor support is still verified during the physical handover.
  TArray<AActor*> Ignored=GetPhysicalLoads();
  if (IsValid(TargetPallet)) Ignored.Add(TargetPallet);
  if (Traffic) Ignored.Append(*Traffic);
  // Before pickup only the standard pallet footprint/height is known here.
  // Do not invent a tall load and reject a valid lower rack slot because the
  // observed shelf above intersects that guess. Delivery rechecks the measured
  // supported unit's complete height below, then retains physical handover tests.
  float Height=16.f;
  if (bSupportingPallet)
  {
   FBox LoadBounds(ForceInit);
   for (AActor* Load : GetPhysicalLoads()) LoadBounds+=AWarehouseDamageSystem::GetPhysicalBounds(Load);
   if (LoadBounds.IsValid) Height=FMath::Max(16.f,float(LoadBounds.GetSize().Z));
  }
  // A measured support plane has range noise. Keep its 3cm uncertainty band
  // out of the reservation volume (plus the sensor's 2cm point padding),
  // without ignoring a rack/actor: higher points and real contacts still count.
  constexpr float SupportPlaneNoiseCm=3.f,PointPaddingCm=2.f;
  return PerceivedBoxClear(Pose,FBox(FVector(-FloorSlotHalfExtentCm,-FloorSlotHalfExtentCm,SupportPlaneNoiseCm+PointPaddingCm),
   FVector(FloorSlotHalfExtentCm,FloorSlotHalfExtentCm,Height+2)),Ignored);
 }
 const bool Floor=FMath::Abs(Pose.GetLocation().Z-FloorBase(Pose.GetLocation().Z))<=5.f;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(DropReservation),false,this);
 if (Traffic) Params.AddIgnoredActors(*Traffic);
 if (IsValid(TargetPallet)) Params.AddIgnoredActor(TargetPallet);
 for (AActor* Cargo : CarriedCargo) if (IsValid(Cargo)) Params.AddIgnoredActor(Cargo);
 if (Floor)
 {
  auto* Strength=AWarehouseDamageSystem::Find(this);
  if (!IsValid(TargetPallet) || !Strength) return false;
  TArray<AActor*> Loads{TargetPallet};FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(TargetPallet);
  // Use live transforms of tracked loads; only before insertion do we need
  // the existing 10Hz snapshot. This 30Hz path never rebuilds the world graph.
  TArray<AActor*> Cargo;
  if (!CarriedCargo.IsEmpty()) for (AActor* Actor : CarriedCargo) Cargo.Add(Actor);
  else for (AActor* Actor : Strength->GetSupportedActors(TargetPallet,false)) if (Cast<AWarehouseCargo>(Actor)) Cargo.Add(Actor);
  for (AActor* Actor : Cargo) if (IsValid(Actor))
  { Loads.Add(Actor);Bounds+=AWarehouseDamageSystem::GetPhysicalBounds(Actor);Params.AddIgnoredActor(Actor); }
  if (!FloorFootprintFits(Loads,TargetPallet->GetActorTransform()) || !Bounds.IsValid) return false;
  const float Height=Bounds.GetSize().Z;
  if (GetWorld()->OverlapBlockingTestByChannel(Pose.TransformPosition(FVector(0,0,Height*.5f+2)),Pose.GetRotation(),ECC_Visibility,
      FCollisionShape::MakeBox(FVector(FloorSlotHalfExtentCm,FloorSlotHalfExtentCm,FMath::Max(1.f,Height*.5f-1))),Params)) return false;
 }
 const FVector Center=Pose.TransformPosition(FVector(0,0,8));
 if (!Floor && GetWorld()->OverlapBlockingTestByChannel(Center,Pose.GetRotation(),ECC_Visibility,FCollisionShape::MakeBox(FVector(54,54,6)),Params)) return false;
 // Rack shelves support pallets on two beams; the empty space under the centre is intentional.
 for (float X : {-40.f,40.f}) for (float Y : {-40.f,40.f})
 {
  const FVector Point=Pose.TransformPosition(FVector(X,Y,0));
  FHitResult Surface;
  if (!GetWorld()->LineTraceSingleByChannel(Surface,Point+FVector(0,0,1),Point-FVector(0,0,4),ECC_Visibility,Params) || Surface.ImpactNormal.Z<.98f) return false;
  if (Floor && (!Surface.GetComponent() || Surface.GetComponent()->IsSimulatingPhysics() || FMath::Abs(Surface.ImpactPoint.Z-Point.Z)>.5f)) return false;
 }
 return true;
}
bool AWarehouseForklift::WaitForDestinationClear(float Dt)
{
 if (DestinationClear()) { DestinationTrafficSeconds=0; return true; }
 TArray<AActor*> Traffic;
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) if (*It!=this)
 {
  Traffic.Add(*It);Traffic.Append(It->GetPhysicalLoads());
 }
 // Ignore fleet bodies only to classify the blocker. Actual unloading still
 // requires the original overlap, cargo-footprint and native support checks.
 if (Traffic.IsEmpty() || !DestinationClear(&Traffic))
 { FaultAI(TEXT("DESTINATION OCCUPIED / UNSUPPORTED")); return false; }
 BrakeDrive();DestinationTrafficSeconds+=Dt;
 if (DestinationTrafficSeconds>=30.f)
 {
  RecoveryFault(TEXT("DESTINATION TRAFFIC TIMEOUT - OPERATOR REQUIRED"),TEXT("하역 위치를 점유한 차량이 이동하지 않습니다. 안전 공간을 확인해 주세요."));
  return false;
 }
 if (FleetCoordinator && (AIState==EWarehouseAIState::AlignUnload || AIState==EWarehouseAIState::LowerLoad))
 {
  const FTransform Goal=PalletApproach(ActiveJob.Destination,65.f);
  if (!FleetCoordinator->Permit(this,&Goal)) return false;
 }
 SetStatus(TEXT("DESTINATION TRAFFIC - WAIT FOR CLEARANCE"));
 return false;
}
bool AWarehouseForklift::RackLoadAligned() const
{
 if (!IsValid(TargetPallet)) return false;
 const FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(TargetPallet);
 if (!Bounds.IsValid) return false;
 // Use the same reserved 120cm slot as unload verification. Vehicle origin
 // error alone cannot tell whether the sensed pallet is ready to lower.
 for (int32 Corner=0;Corner<8;++Corner)
 {
  const FVector Point=ActiveJob.Destination.InverseTransformPosition(FVector(
   Corner&1 ? Bounds.Max.X : Bounds.Min.X,Corner&2 ? Bounds.Max.Y : Bounds.Min.Y,Corner&4 ? Bounds.Max.Z : Bounds.Min.Z));
  if (FMath::Abs(Point.X)>60 || FMath::Abs(Point.Y)>60) return false;
 }
 return true;
}
void AWarehouseForklift::TrackCargo()
{
 CarriedCargo.Reset();
 if (auto* Strength=AWarehouseDamageSystem::Find(this))
  for (AActor* Actor : Strength->GetSupportedActors(TargetPallet))
   if (Cast<AWarehouseCargo>(Actor)) CarriedCargo.Add(Actor);
}

float AWarehouseForklift::FloorBase(float WorldZ) const
{
 if (IsValid(Elevator))
 {
  const int32 Floor=Elevator->FloorAtHeight(WorldZ);
  if (Floor!=INDEX_NONE) return Elevator->FloorHeight(Floor);
 }
 return GetActorLocation().Z;
}
bool AWarehouseForklift::BeginFloorTransfer(float TargetZ,EWarehouseAIState Resume)
{
 const float Base=FloorBase(TargetZ);
 if (FMath::Abs(Base-GetActorLocation().Z)<1.f) return false;
 if (!IsValid(Elevator) || Elevator->FloorAtHeight(TargetZ)==INDEX_NONE || Elevator->FloorAtHeight(GetActorLocation().Z)==INDEX_NONE)
 {
  Elevator=nullptr;
  for (TActorIterator<AWarehouseElevator> It(GetWorld());It;++It)
   if (It->FloorAtHeight(TargetZ)!=INDEX_NONE && It->FloorAtHeight(GetActorLocation().Z)!=INDEX_NONE) { Elevator=*It;break; }
 }
 if (!IsValid(Elevator)) { SetOperationError(TEXT("NO_ELEVATOR"),TEXT("지게차가 올라갈 수 없다: 해당 층을 연결하는 승강기가 없습니다."));FaultAI(Status);return true; }
 TransferFromFloor=Elevator->FloorAtHeight(GetActorLocation().Z);
 TransferToFloor=Elevator->FloorAtHeight(TargetZ);
 if (TransferFromFloor==INDEX_NONE || TransferToFloor==INDEX_NONE || !IsLiftAtTravelHeight())
 { FaultAI(TEXT("INVALID FLOOR / LOWER FORKS BEFORE TRANSFER")); return true; }
 if (GetTransferMassKg()>FMath::Min(2000.f,Elevator->RatedMassKg))
 { FaultAI(TEXT("ELEVATOR OVERLOAD (VEHICLE + LOAD)")); return true; }
 const FString Fault=Elevator->CheckInterlocks();
 if (!Fault.IsEmpty()) { FaultAI(Fault); return true; }
 if (!PlanAutonomousRoute(Elevator->WaitingPose(TransferFromFloor)))
 { StopFor(TEXT("NO CLEAR ROUTE TO ELEVATOR")); return true; }
 AfterElevator=Resume; bFloorTransfer=true;
 TransitionAI(EWarehouseAIState::ElevatorApproach,TEXT("NAVIGATE TO ELEVATOR WAITING POINT"));
 bRouteStarted=true; return true;
}
void AWarehouseForklift::AdvanceAutonomy(float Dt)
{
 AITotalSeconds+=Dt;
 if (!bPowered || Dt<=0) return;
 if (bWorkStopRequested && !bFloorTransfer)
 { StopCurrentWork();return; }
 if (SensorMode!=EWarehouseSensorMode::Autonomous)
 {
  bExperimentSensorWaiting=true;
  // Disabled/observation modes are diagnostics, never an omniscient legacy
  // autonomous fallback. The preceding sensor tick can still observe while parked.
  BrakeDrive();DesiredLift=LiftOffset;LiftMotorTarget=LiftOffset;
  SetStatus(SensorMode==EWarehouseSensorMode::ObserveOnly ?
   TEXT("SENSOR OBSERVATION ONLY - AUTONOMOUS DRIVE DISABLED") : TEXT("SENSORS DISABLED - AUTONOMOUS DRIVE DISABLED"));
  return;
 }
 AIElapsed+=Dt;
 const FString Fault=CheckSystems();
 if (!Fault.IsEmpty()) { FaultAI(Fault); return; }
 if (SensorMode==EWarehouseSensorMode::Autonomous && AIState!=EWarehouseAIState::ElevatorRide && !AutonomousSensorsReady())
 {
  bExperimentSensorWaiting=true;
  AIElapsed-=Dt;BrakeDrive();
  SetStatus(TEXT("SENSOR WAIT - FRESH SCAN / FLOOR MAP REQUIRED"));return;
 }
 if (bFloorTransfer)
 {
  if (!IsValid(Elevator)) { FaultAI(TEXT("ELEVATOR LOST")); return; }
  const FString LiftFault=Elevator->CheckInterlocks();
  if (!LiftFault.IsEmpty()) { FaultAI(LiftFault); return; }
 }
 if (AIState!=EWarehouseAIState::Charging) ConsumeEnergy(100.f*Dt/3600.f);
 if (BatteryPercent<=0 && AIState!=EWarehouseAIState::Charging) { FaultAI(TEXT("BATTERY EMPTY")); return; }
 if (!ActiveJob.JobId.IsEmpty() || bClearingForksForWork)
 {
  if (!IsValid(TargetPallet)) { SetOperationError(TEXT("NO_PALLET"),TEXT("운반할 수 없다: 대상 팔레트가 삭제되었습니다."));FaultAI(Status);return; }
  if (TargetPallet->ReceiptState==EWarehouseReceiptState::WrapTorn) { FaultAI(TEXT("PALLET WRAP TORN - REINSPECTION REQUIRED"));return; }
  if (ActiveJob.bRequiresDestinationResource && !IsValid(ActiveJob.DestinationResource))
  { SetOperationError(TEXT("NO_DESTINATION_RESOURCE"),TEXT("운반할 수 없다: 목적지 랙 또는 입출고 구역이 삭제되었습니다."));FaultAI(Status);return; }
  if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->HasFailed(TargetPallet)) { FaultAI(TEXT("PALLET DAMAGED")); return; }
  if (GetLoadMassKg()>FMath::Min(1400.f,RatedLoadKg)) { FaultAI(TEXT("OVERLOAD - REMOVE LOAD")); return; }
 }
 if (!UpdateLoadSupport(Dt)) { FaultAI(TEXT("LOAD SLIPPED / SUPPORT LOST")); return; }
 if (bOpportunityCharge && !PendingJobs.IsEmpty() && BatteryPercent>ChargeBelowPercent && !bChargeToFull)
 {
  bOpportunityCharge=false;bIdleChargeParking=false;bChargePending=false;
  if (AIState==EWarehouseAIState::Charging || AIState==EWarehouseAIState::DockCharge)
  { TransitionAI(EWarehouseAIState::LeaveCharger,TEXT("새 작업 수신 · 기회 충전을 종료하고 출차"));return; }
  if (AIState==EWarehouseAIState::PlanCharge || AIState==EWarehouseAIState::NavigateCharge || AIState==EWarehouseAIState::WaitingCharger)
  { if (IsValid(ChargingStation)) ChargingStation->Release(this);TransitionAI(EWarehouseAIState::Ready,TEXT("새 작업 수신 · 기회 충전 이동 취소"));return; }
 }
 if (FleetCoordinator && FleetCoordinator->AdvanceYield(this,Dt)) return;
 if (AdvanceIdleParking(Dt)) return;
 auto Next=[&](EWarehouseAIState Value,const TCHAR* Message) { TransitionAI(Value,Message); };
 auto Route=[&](const FTransform& Goal,EWarehouseAIState Done,const TCHAR* Message,float Speed)
 {
  if (SensorMode==EWarehouseSensorMode::Autonomous && AIState==EWarehouseAIState::NavigatePickup &&
      bHasRecognizedTargetPose && bRouteStarted && !PlannedRoute.IsEmpty() &&
      (FVector::Dist(Goal.GetLocation(),PlannedRoute.Last().Pose.GetLocation())>2.f ||
       FMath::Abs(FMath::FindDeltaAngleDegrees(Goal.Rotator().Yaw,PlannedRoute.Last().Pose.Rotator().Yaw))>1.5f))
  {
   // A new model observation can update the WMS last-known pickup pose.
   // Brake physically before replacing the old path; never silently chase a
   // new goal while the vehicle still follows a different planned endpoint.
   BrakeDrive();SetStatus(TEXT("OBSERVED PICKUP POSE UPDATED - BRAKE / REPLAN"));
   if (FMath::Abs(CurrentSpeedCm)>=2.f || ChassisBody->GetPhysicsAngularVelocityInDegrees().Size()>=.5f) return;
   if (!PlanAutonomousRoute(Goal)) { StopFor(TEXT("OBSTACLE / NO CLEAR ROUTE TO OBSERVED PALLET"));return; }
   bRouteStarted=true;
  }
  if (!bRouteStarted)
  {
   if (!PlanAutonomousRoute(Goal)) { StopFor(TEXT("OBSTACLE / NO CLEAR ROUTE")); return; }
   bRouteStarted=true;
  }
  if (FollowRoute(Speed,Dt)) Next(Done,Message);
 };
 const float Ground=GetActorLocation().Z;
 // Wheel contact tolerances can place the chassis pivot slightly above a floor datum.
 const float SourceHeight=FMath::Max(0.f,float(ObservedPallet.GetLocation().Z-Ground));
 const float DropHeight=FMath::Max(0.f,float(ActiveJob.Destination.GetLocation().Z-Ground));
 auto LiftTo=[&](float Height)
 {
  if (!MoveLift(Height)) return false;
  return FMath::Abs(LiftOffset-Height)<.4f && FMath::Abs(CarriageBody->GetPhysicsLinearVelocity().Z-ChassisBody->GetPhysicsLinearVelocity().Z)<1.f;
 };
 switch (AIState)
 {
 case EWarehouseAIState::SelfCheck:
  if (AIElapsed>=1.f)
  {
   const EWarehouseAIState Resume=bResumeAfterCheck && ResumeAI!=EWarehouseAIState::SelfCheck ? ResumeAI : EWarehouseAIState::Ready;
   bResumeAfterCheck=false;
   Next(Resume,TEXT("SELF CHECK PASSED"));
  }
  break;
 case EWarehouseAIState::Ready:
  if (PrepareForksForNextWork()) break;
  if (bChargePending || BatteryPercent<=ChargeBelowPercent) Next(EWarehouseAIState::PlanCharge,TEXT("PLAN CHARGING"));
  else if (!PendingJobs.IsEmpty())
  {
   // Leave a settled parking pose with native wheels before applying the wider
   // travel envelope. This is not the stricter electrical docking approval.
   if (IsValid(ChargingStation) && ActiveJob.JobId.IsEmpty() &&
       !bSupportingPallet && ChassisBody && FVector::Dist(GetActorLocation(),ChargingStation->GetActorLocation())<=5.f &&
       GetActorUpVector().Z>.999f && ChargingStation->GetActorUpVector().Z>.999f &&
       FVector::DotProduct(GetActorForwardVector(),ChargingStation->GetActorForwardVector())>=FMath::Cos(FMath::DegreesToRadians(2.f)))
   {
    BrakeDrive();
    if (ChassisBody->GetPhysicsLinearVelocity().Size()>=5.f || ChassisBody->GetPhysicsAngularVelocityInDegrees().Size()>=2.f) break;
    if (IsValid(ChargingStation->Occupant) && ChargingStation->Occupant!=this)
    { FaultAI(TEXT("CHARGER OCCUPIED BY ANOTHER VEHICLE"));break; }
    ChargeExitPose=ChargingStation->GetActorTransform();
    ChargeExitPose.AddToTranslation(ChargingStation->GetActorForwardVector()*120.f);
    Next(EWarehouseAIState::LeaveCharger,TEXT("WORK QUEUED - LEAVE PARKING DOCK"));
    break;
   }
   if (IsValid(WaitingBay)) WaitingBay->Release(this);
   ActiveJob=PendingJobs[0]; PendingJobs.RemoveAt(0); TargetPallet=ActiveJob.Pallet; bPalletReleased=false;
   if (!WorkOrderSourcePoses.Contains(ActiveJob.JobId) && IsValid(TargetPallet)) WorkOrderSourcePoses.Add(ActiveJob.JobId,TargetPallet->GetActorTransform());
   bObservedTargetValid=false;
   bHasRecognizedTargetPose=false;
   RecoveryAttempts=0;ForkInsertionRetries=0;ObservedRouteReplans=0;ObservedRouteProgress=GetActorLocation();
   WaitSeconds=0;ProgressLocation=GetActorLocation();
   Next(EWarehouseAIState::ValidateJob,TEXT("WORK ORDER RECEIVED")); ReportJob(TEXT("ACCEPTED"),ActiveJob.SourceSystem);
  }
  else if (AIElapsed>.5f && GetWorld()->GetTimeSeconds()>=NextResourceCheck)
  {
   NextResourceCheck=GetWorld()->GetTimeSeconds()+1.;
   if (bOpportunityChargingEnabled && AIElapsed>=OpportunityChargeIdleDelaySeconds && BatteryPercent<OpportunityChargeBelowPercent-1.f)
   {
    bOpportunityCharge=true;
    if (TryAcquireCharger()) { bChargePending=true;Next(EWarehouseAIState::PlanCharge,TEXT("작업 없음 · 가능한 동안 기회 충전")); }
    else { bOpportunityCharge=false;PlanIdleParking(false); }
   }
   else PlanIdleParking(false);
  }
  break;
 case EWarehouseAIState::ValidateJob:
 {
  const FString Resources=ValidateJobResources(ActiveJob);
  if (!Resources.IsEmpty()) { SetOperationError(Resources.Contains(TEXT("NO_ELEVATOR")) ? FName(TEXT("NO_ELEVATOR")) : Resources.Contains(TEXT("NO_DESTINATION_RESOURCE")) ? FName(TEXT("NO_DESTINATION_RESOURCE")) : FName(TEXT("NO_PALLET")),Resources);FaultAI(Status);break; }
  if (PalletClaimedByOther(TargetPallet)) { FaultAI(TEXT("PALLET RESERVED BY ANOTHER AGV")); break; }
  const float SourceFloor=FloorBase(TaskSourcePose().GetLocation().Z);
  const float SourceLevel=TaskSourcePose().GetLocation().Z-SourceFloor;
  bool SourceGroundContact=false;
  // A heavy rigid pallet can settle a few millimetres into its floor contact.
  // Validate the WMS location against the published floor here; actual pallet
  // contact is verified after sensing/engagement, never by hidden-source bounds.
  if (SourceLevel<-.1f && SourceLevel>=-.5f)
  {
   if (SensorMode==EWarehouseSensorMode::Autonomous)
   {
    SourceGroundContact=true;
    for (float X : {-40.f,40.f}) for (float Y : {-40.f,40.f}) if (SourceGroundContact)
    {
     FHitResult Surface;
     const FVector Foot=TaskSourcePose().TransformPosition(FVector(X,Y,0));
     SourceGroundContact=PerceivedFloorSupport(Foot,&Surface) && FMath::Abs(Surface.ImpactPoint.Z-SourceFloor)<=.5f;
    }
   }
   else
   {
    const FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(TargetPallet);
    SourceGroundContact=Bounds.IsValid && FMath::Abs(Bounds.Min.Z-SourceFloor)<=.5f;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(PalletSourceFloorContact),false,this);
    Params.AddIgnoredActor(TargetPallet);
    for (float X : {-40.f,40.f}) for (float Y : {-40.f,40.f}) if (SourceGroundContact)
    {
     const FVector Foot=TargetPallet->GetActorTransform().TransformPosition(FVector(X,Y,0));
     FHitResult Surface;
     SourceGroundContact=GetWorld()->LineTraceSingleByChannel(Surface,Foot+FVector(0,0,3),Foot-FVector(0,0,5),ECC_Visibility,Params) &&
      Surface.ImpactNormal.Z>.98f && FMath::Abs(Surface.ImpactPoint.Z-SourceFloor)<=.5f;
    }
   }
  }
  if (ActiveJob.JobId.IsEmpty() || CompletedJobIds.Contains(ActiveJob.JobId) || ActiveJob.Destination.ContainsNaN() ||
      !ActiveJob.Destination.GetScale3D().Equals(FVector::OneVector,.001f) ||
      ActiveJob.Destination.GetLocation().Z-FloorBase(ActiveJob.Destination.GetLocation().Z)<-.1f ||
      ActiveJob.Destination.GetLocation().Z-FloorBase(ActiveJob.Destination.GetLocation().Z)+20.f>FMath::Min(MaxForkHeightCm,160.f) ||
      (SourceLevel<-.1f && !SourceGroundContact) ||
      SourceLevel+20.f>FMath::Min(MaxForkHeightCm,160.f) ||
      FMath::Abs(ActiveJob.Destination.Rotator().Pitch)>.1f || FMath::Abs(ActiveJob.Destination.Rotator().Roll)>.1f)
  { FaultAI(TEXT("INVALID JOB / LIFT RANGE")); break; }
  if (!WaitForDestinationClear(Dt)) break;
  Next(EWarehouseAIState::PlanPickup,TEXT("PLAN PICKUP ROUTE"));
  break;
 }
 case EWarehouseAIState::PlanPickup:
  if (AIElapsed<=Dt*1.5f)
   UE_LOG(LogTemp,Log,TEXT("AGV_PICKUP_TASK_LOCATION %s job=%s source=%s approach=%s snapshotOnly=%d observedTargetValid=%d observedTarget=%s"),
    *VehicleName,*ActiveJob.JobId,*TaskSourcePose().ToString(),*PalletApproach(TaskSourcePose(),220).ToString(),SensorMode==EWarehouseSensorMode::Autonomous,
    bObservedTargetValid,bObservedTargetValid ? *SensorObservedTarget.ToString() : TEXT("NONE"));
  if (BeginFloorTransfer(TaskSourcePose().GetLocation().Z,EWarehouseAIState::PlanPickup)) break;
  if (PlanAutonomousRoute(PalletApproach(TaskSourcePose(),220)))
  { Next(EWarehouseAIState::NavigatePickup,TEXT("NAVIGATION TO PICKUP")); bRouteStarted=true; }
  else StopFor(TEXT("OBSTACLE / NO PICKUP ROUTE"));
  break;
 case EWarehouseAIState::NavigatePickup:
  Route(PalletApproach(TaskSourcePose(),220),EWarehouseAIState::SlowApproach,TEXT("PALLET APPROACH - LOW SPEED"),EmptyTravelSpeedCm);
  break;
 case EWarehouseAIState::SlowApproach:
  if (AIElapsed>=.25f) Next(EWarehouseAIState::DetectPallet,TEXT("DETECT PALLET"));
  break;
 case EWarehouseAIState::DetectPallet:
  if (SeePallet(ObservedPallet)) Next(EWarehouseAIState::EstimatePose,TEXT("ESTIMATE PALLET POSITION / ANGLE"));
  else if (AIElapsed>=3.f) FaultAI(TEXT("PALLET NOT VISIBLE"));
  break;
 case EWarehouseAIState::EstimatePose:
  // This stacker has no certified mast tilt actuator. Reject tilted pallets instead of inventing one.
  if (FMath::Abs(ObservedPallet.Rotator().Pitch)>.5f || FMath::Abs(ObservedPallet.Rotator().Roll)>.5f)
  { FaultAI(TEXT("PALLET TILT EXCEEDS FORK ALIGNMENT RANGE")); break; }
  Next(EWarehouseAIState::AlignVehicle,TEXT("ALIGN VEHICLE TO PALLET"));
  break;
 case EWarehouseAIState::AlignVehicle:
 {
  const FTransform AlignmentPose=PalletApproach(ObservedPallet,185);
  const FVector AlignmentOffset=AlignmentPose.GetLocation()-GetActorLocation();
  const float AlignmentYaw=FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,AlignmentPose.Rotator().Yaw));
  // A short, already aligned creep does not need an infeasible tiny S curve.
  // Full fork-hole/angle/support checks remain in VerifyInsertion.
  if (FMath::Abs(FVector::DotProduct(AlignmentOffset,GetActorRightVector()))<=5.f && AlignmentYaw<1.5f)
  {
   if (MovePrecise(AlignmentPose.GetLocation(),15,Dt)) Next(EWarehouseAIState::CorrectFork,TEXT("CORRECT FORK HEIGHT / CHECK ANGLE"));
  }
  else Route(AlignmentPose,EWarehouseAIState::CorrectFork,TEXT("CORRECT FORK HEIGHT / CHECK ANGLE"),15);
  break;
 }
 case EWarehouseAIState::CorrectFork:
  if (LiftTo(FMath::Max(0.f,SourceHeight))) Next(EWarehouseAIState::InsertFork,TEXT("FORK INSERT - CREEP"));
  break;
 case EWarehouseAIState::InsertFork:
  // Fully insert both tines with 5 cm end clearance for native tyre settling.
  if (MovePrecise(PalletApproach(ObservedPallet,65).GetLocation(),10,Dt)) Next(EWarehouseAIState::VerifyInsertion,TEXT("VERIFY BOTH TINES INSERTED"));
  break;
 case EWarehouseAIState::VerifyInsertion:
  if (!TargetPallet->CanEngage(Carriage->GetComponentTransform())) { BeginRecovery(AIState,TEXT("FORK INSERTION FAILED")); break; }
  PalletContactLiftCm=TargetPallet->GetSupportLiftOffset(Carriage->GetComponentTransform());
  if (PalletContactLiftCm<0) { BeginRecovery(AIState,TEXT("NO PALLET SUPPORT")); break; }
  Next(EWarehouseAIState::LiftLoad,TEXT("LIFT LOAD"));
  break;
 case EWarehouseAIState::LiftLoad:
 {
  // Native lifting may stall under contact/load. A moving 160cm stroke gets
  // its existing 11.5cm/s travel time plus 10s to settle; no physics is repaired.
  if (AIElapsed<=Dt*1.5f)
  {
   LiftInitialHeight=LiftProgressHeight=LiftOffset;
   LiftProgressAt=0;
  }
  if (!bSupportingPallet)
  {
   TrackCargo();
   bSupportingPallet=true;
  }
  const float GoalHeight=SourceHeight+10.5f;
  if (LiftTo(GoalHeight)) Next(EWarehouseAIState::VerifyLoad,TEXT("VERIFY LOAD PRESENT / MASS"));
  else if (bPowered && AIState==EWarehouseAIState::LiftLoad)
  {
   const float Direction=GoalHeight>=LiftInitialHeight ? 1.f : -1.f;
   // Count useful height progress, not contact jitter or motion away from goal.
   if ((LiftOffset-LiftProgressHeight)*Direction>=.2f)
   { LiftProgressHeight=LiftOffset;LiftProgressAt=AIElapsed; }
   const float Error=FMath::Abs(GoalHeight-LiftOffset);
   const bool Stalled=Error>.4f && AIElapsed-LiftProgressAt>=10.f;
   const bool TimedOut=AIElapsed>FMath::Abs(GoalHeight-LiftInitialHeight)/11.5f+10.f;
   if (Stalled || TimedOut)
   {
    UE_LOG(LogTemp,Warning,TEXT("AGV_LIFT_NO_PROGRESS %s actual=%.3f target=%.3f elapsed=%.2f lastProgress=%.2f mass=%.1f"),
     *VehicleName,LiftOffset,GoalHeight,AIElapsed,LiftProgressAt,GetLoadMassKg());
    RecoveryFault(Stalled ? TEXT("LIFT NO PHYSICAL PROGRESS - OPERATOR REQUIRED") : TEXT("LIFT HEIGHT / SETTLE TIMEOUT - OPERATOR REQUIRED"),
     TEXT("리프트(LIFT)가 목표 높이에서 안정되지 않았습니다. 적재 무게와 포크·팔레트 접촉 상태를 확인하세요."));
   }
  }
  break;
 }
 case EWarehouseAIState::VerifyLoad:
  if (AIElapsed<.3f) break; // Let the physical contact settle before reading the load sensor.
  if (!bSupportingPallet || !PalletOnForks() || GetLoadMassKg()<=0)
  { FaultAI(TEXT("LOAD VERIFICATION FAILED")); break; }
  RetractLocation=GetActorLocation()-GetActorForwardVector()*FMath::Max(130.f,FVector::Dist2D(GetActorLocation(),PalletApproach(ObservedPallet,220).GetLocation()));
  Next(EWarehouseAIState::DepartPickup,TEXT("WITHDRAW LOADED PALLET"));
  break;
 case EWarehouseAIState::DepartPickup:
  if (MovePrecise(RetractLocation,20,Dt)) Next(EWarehouseAIState::TravelHeight,TEXT("LOWER TO TRAVEL HEIGHT"));
  break;
 case EWarehouseAIState::TravelHeight:
  if (LiftTo(10.5f)) Next(EWarehouseAIState::PlanDelivery,TEXT("PLAN LOADED ROUTE"));
  break;
 case EWarehouseAIState::PlanDelivery:
  if (BeginFloorTransfer(ActiveJob.Destination.GetLocation().Z,EWarehouseAIState::PlanDelivery)) break;
  if (!WaitForDestinationClear(Dt)) break;
  if (PlanAutonomousRoute(PalletApproach(ActiveJob.Destination,220)))
  { Next(EWarehouseAIState::NavigateDelivery,TEXT("NAVIGATION TO DESTINATION")); bRouteStarted=true; }
  else StopFor(TEXT("OBSTACLE / NO DELIVERY ROUTE"));
  break;
 case EWarehouseAIState::NavigateDelivery:
  Route(PalletApproach(ActiveJob.Destination,220),EWarehouseAIState::AlignUnload,TEXT("ALIGN UNLOADING POSITION"),LoadedTravelSpeedCm);
  break;
 case EWarehouseAIState::AlignUnload:
 {
  if (!WaitForDestinationClear(Dt)) break;
  // Rotation and tiny replans are not unlimited progress toward a floor slot.
  // Preserve central passage waits; use the existing bounded physical retry.
  if (DropHeight<=5.f && AIElapsed>60.f && !bTrafficWaiting)
  {
   UE_LOG(LogTemp,Log,TEXT("AGV_FLOOR_ALIGN_TIMEOUT %s elapsed=%.2f pallet=%s slot=%s"),
    *VehicleName,AIElapsed,*TargetPallet->GetActorLocation().ToString(),*ActiveJob.Destination.GetLocation().ToString());
   BeginRecovery(EWarehouseAIState::AlignUnload,TEXT("FLOOR LOAD ALIGNMENT OUTSIDE SLOT"));
   break;
  }
  // Follow the sensed load position; contact can shift it along the tines.
  // Correct by driving the vehicle, without repositioning the physical pallet.
  if (!LiftTo(DropHeight+10.5f)) break;
  TArray<AActor*> FloorLoads{TargetPallet};
  for (AActor* Cargo : CarriedCargo) FloorLoads.Add(Cargo);
  // Accept the actual load, rather than chasing a stale relative-pose target.
  // Leave 2cm inside the reserved footprint for native contact settling below.
  const bool FloorInner=DropHeight<=5.f && FloorFootprintFits(FloorLoads,ActiveJob.Destination,2.f);
  const bool FloorAligned=FloorInner &&
   FMath::Abs(FMath::FindDeltaAngleDegrees(TargetPallet->GetActorRotation().Yaw,ActiveJob.Destination.Rotator().Yaw))<1.f;
  if (DropHeight<=5.f && FParse::Param(FCommandLine::Get(),TEXT("DriveDiagnostics")) &&
      FMath::FloorToInt(AIElapsed)>FMath::FloorToInt(FMath::Max(0.f,AIElapsed-Dt)))
  {
   const FTransform Goal=PlannedRoute.IsEmpty() ? FTransform::Identity : PlannedRoute.Last().Pose;
   const FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(TargetPallet);
   UE_LOG(LogTemp,Log,TEXT("AGV_FLOOR_ALIGN %s age=%.2f vehicle=%s yaw=%.3f pallet=%s yaw=%.3f slot=%s yaw=%.3f goal=%s yaw=%.3f route=%d/%d inner=%d aligned=%d bounds=%s..%s vehicleSpeed=%.3f angular=%.3f palletSpeed=%.3f angular=%.3f"),
    *VehicleName,AIElapsed,*GetActorLocation().ToString(),GetActorRotation().Yaw,
    *TargetPallet->GetActorLocation().ToString(),TargetPallet->GetActorRotation().Yaw,
    *ActiveJob.Destination.GetLocation().ToString(),ActiveJob.Destination.Rotator().Yaw,
    *Goal.GetLocation().ToString(),Goal.Rotator().Yaw,RouteIndex,PlannedRoute.Num(),FloorInner,FloorAligned,
    *Bounds.Min.ToString(),*Bounds.Max.ToString(),CurrentSpeedCm,ChassisBody->GetPhysicsAngularVelocityInDegrees().Size(),
    TargetPallet->GetPalletBody()->GetPhysicsLinearVelocity().Size(),TargetPallet->GetPalletBody()->GetPhysicsAngularVelocityInDegrees().Size());
  }
  if (FloorAligned)
  {
   BrakeDrive();
   if (FMath::Abs(CurrentSpeedCm)<1.f && ChassisBody->GetPhysicsAngularVelocityInDegrees().Size()<.5f &&
       TargetPallet->GetPalletBody()->GetPhysicsLinearVelocity().Size()<2.f &&
       TargetPallet->GetPalletBody()->GetPhysicsAngularVelocityInDegrees().Size()<1.f)
   {
    if (IsFloorFootprintValid(TargetPallet,ActiveJob.Destination))
     Next(EWarehouseAIState::LowerLoad,TEXT("LOAD INSIDE FLOOR SLOT - LOWER LOAD"));
    else BeginRecovery(EWarehouseAIState::AlignUnload,TEXT("FLOOR LOAD ALIGNMENT OUTSIDE SLOT"));
   }
   break;
  }
  if (ActiveJob.bRackDestination && DropHeight>5 && RackLoadAligned() &&
      FVector::Dist2D(TargetPallet->GetActorLocation(),ActiveJob.Destination.GetLocation())<=2.f)
  {
   BrakeDrive();
   if (FMath::Abs(CurrentSpeedCm)<1.f && ChassisBody->GetPhysicsAngularVelocityInDegrees().Size()<.5f)
    Next(EWarehouseAIState::LowerLoad,TEXT("PALLET ALIGNED IN RACK SLOT - LOWER LOAD"));
   break;
  }
  if (!bRouteStarted)
  {
   const FTransform Relative=TargetPallet->GetActorTransform().GetRelativeTransform(GetActorTransform());
   const FRotator Heading(0,ActiveJob.Destination.Rotator().Yaw-Relative.Rotator().Yaw,0);
   FVector Point=ActiveJob.Destination.GetLocation()-Heading.RotateVector(Relative.GetLocation());Point.Z=Ground;
   float Cost;
   if (!ConnectPoses(GetActorTransform(),FTransform(Heading,Point),PlannedRoute,Cost))
   { FaultAI(TEXT("NO CLEAR PRECISE RACK APPROACH"));break; }
   RouteIndex=0;bRouteStarted=true;
  }
  if (FollowRoute(10,Dt))
  {
   // Loose loads can shift during the final pivot after the relative pose was
   // measured. Reapproach before lowering; never release outside the slot.
   if (DropHeight<=5.f && (!FloorFootprintFits(FloorLoads,ActiveJob.Destination,2.f) ||
       !IsFloorFootprintValid(TargetPallet,ActiveJob.Destination)))
   {
    BeginRecovery(EWarehouseAIState::AlignUnload,TEXT("FLOOR LOAD ALIGNMENT OUTSIDE SLOT"));
    break;
   }
   UE_LOG(LogTemp,Log,TEXT("UNLOAD_BEFORE_LOWER pallet=%s yaw=%.2f target=%s"),
    *TargetPallet->GetActorLocation().ToString(),TargetPallet->GetActorRotation().Yaw,*ActiveJob.Destination.GetLocation().ToString());
   Next(EWarehouseAIState::LowerLoad,TEXT("LOWER LOAD ONTO SUPPORT"));
  }
  break;
 }
 case EWarehouseAIState::LowerLoad:
  if (bClearingForksForWork && AIElapsed>MaxForkHeightCm/11.5f+15.f)
  { RecoveryFault(TEXT("RETAINED LOAD LOWERING TIMEOUT"),TEXT("기존 짐을 안전하게 내리지 못해 새 작업을 대기합니다. G로 적재 상태를 확인하세요."));break; }
  if (!bPalletReleased)
  {
   if (!WaitForDestinationClear(Dt)) break;
   if (!LiftTo(DropHeight+PalletContactLiftCm)) break;
   bSupportingPallet=false; bPalletReleased=true; WaitSeconds=0;
  }
  if (LiftTo(DropHeight))
  {
   WaitSeconds+=Dt;
   if (WaitSeconds<.5f) break;
   // A floor handoff succeeds inside its reserved footprint. Do not repeatedly
   // lift a safely grounded loose load just to chase a centre-only tolerance.
   if (DropHeight<=5.f)
   {
    auto* Strength=AWarehouseDamageSystem::Find(this);
    bool CargoSafe=Strength && !Strength->HasFailed(TargetPallet);
    const TArray<AActor*> Supported=Strength ? Strength->GetSupportedActors(TargetPallet) : TArray<AActor*>();
    for (AActor* Cargo : CarriedCargo)
     if (!IsValid(Cargo) || !Supported.Contains(Cargo) || Strength->HasFailed(Cargo)) CargoSafe=false;
    if (!CargoSafe || !IsFloorPlacementValid(TargetPallet,ActiveJob.Destination,true,false))
    {
     BrakeDrive();
     if (CargoSafe && WaitSeconds<2.f) break;
     RecoveryFault(TEXT("FLOOR HANDOFF OUTSIDE SLOT / UNSUPPORTED / UNSTABLE - OPERATOR REQUIRED"),TEXT("하역한 팔레트·화물이 예약 공간 안에서 안정적으로 지지되지 않습니다. 적재 상태를 확인하세요."));
     break;
    }
   }
   if (ActiveJob.bRackDestination && DropHeight>5)
   {
    FHitResult Shelf;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(RackLoadHandoff),false,this);
    Params.AddIgnoredActor(TargetPallet);
    for (const auto& Cargo : CarriedCargo) if (IsValid(Cargo)) Params.AddIgnoredActor(Cargo.Get());
    const FVector Probe=ActiveJob.Destination.TransformPosition(FVector(40,40,0));
    auto* Strength=AWarehouseDamageSystem::Find(this);
    const bool Accepted=GetWorld()->LineTraceSingleByChannel(Shelf,Probe+FVector(0,0,1),Probe-FVector(0,0,4),ECC_Visibility,Params) &&
     Strength && Strength->GetSupportedActors(Shelf.GetActor()).Contains(TargetPallet);
    if (!Accepted)
    {
     if (AIElapsed>5.f)
     {
      UE_LOG(LogTemp,Warning,TEXT("RACK_HANDOFF_FAILED pallet=%s fork=%.2f contact=%.2f shelf=%s"),
       *TargetPallet->GetActorLocation().ToString(),LiftOffset,PalletContactLiftCm,*GetNameSafe(Shelf.GetActor()));
      FaultAI(TEXT("RACK DID NOT ACCEPT LOAD - FORKS RETAINED"));
     }
     break;
    }
   }
   // Clear the complete rack approach envelope before planning another job,
   // including its normal travel margin, rather than stopping inside it.
   UE_LOG(LogTemp,Log,TEXT("UNLOAD_BEFORE_WITHDRAW pallet=%s yaw=%.2f"),
    *TargetPallet->GetActorLocation().ToString(),TargetPallet->GetActorRotation().Yaw);
   RetractLocation=GetActorLocation()-GetActorForwardVector()*FMath::Max(130.f,FVector::Dist2D(GetActorLocation(),PalletApproach(ActiveJob.Destination,220).GetLocation())); Next(EWarehouseAIState::WithdrawFork,TEXT("FORK WITHDRAW"));
  }
  break;
 case EWarehouseAIState::WithdrawFork:
  if (MovePrecise(RetractLocation,15,Dt))
  {
   Next(EWarehouseAIState::VerifyUnload,TEXT("VERIFY UNLOAD COMPLETE"));
  }
  break;
 case EWarehouseAIState::VerifyUnload:
 {
  if (AIElapsed<.5f) break;
  bool SupportRefreshed=false;
  if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && !CarriedCargo.IsEmpty())
  {
   const TArray<AActor*> Supported=Strength->GetSupportedActors(TargetPallet);
   SupportRefreshed=true;
   for (AActor* Cargo : CarriedCargo)
    if (!IsValid(Cargo) || !Supported.Contains(Cargo) || Strength->HasFailed(Cargo))
    { FaultAI(TEXT("UNLOAD VERIFICATION FAILED - CARGO LOST / DAMAGED")); return; }
  }
  bool bPlaced=FVector::Dist(TargetPallet->GetActorLocation(),ActiveJob.Destination.GetLocation())<=2 &&
      FMath::Abs(FMath::FindDeltaAngleDegrees(TargetPallet->GetActorRotation().Yaw,ActiveJob.Destination.Rotator().Yaw))<=2;
  if (DropHeight<=5.f) bPlaced=IsFloorPlacementValid(TargetPallet,ActiveJob.Destination,false,!SupportRefreshed);
  if (ActiveJob.bRackDestination && DropHeight>5)
  {
   // 110cm pallets have a reserved 120cm footprint inside a 125cm pitch rack.
   // Judge real containment/support rather than faulting a harmless few degrees of yaw.
   const FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(TargetPallet);
   bPlaced=Bounds.IsValid && FMath::Abs(Bounds.Min.Z-ActiveJob.Destination.GetLocation().Z)<2 &&
    TargetPallet->GetPalletBody()->GetPhysicsLinearVelocity().Size()<5 &&
    TargetPallet->GetPalletBody()->GetPhysicsAngularVelocityInDegrees().Size()<5;
   bPlaced &= RackLoadAligned();
   FHitResult Shelf;
   FCollisionQueryParams Params(SCENE_QUERY_STAT(UnloadedRackSupport),false,this);
   Params.AddIgnoredActor(TargetPallet);
   for (const auto& Cargo : CarriedCargo) if (IsValid(Cargo)) Params.AddIgnoredActor(Cargo.Get());
   const FVector Probe=ActiveJob.Destination.TransformPosition(FVector(40,40,0));
   auto* Strength=AWarehouseDamageSystem::Find(this);
   // No physical step or transform change occurred since the cargo query.
   bPlaced &= GetWorld()->LineTraceSingleByChannel(Shelf,Probe+FVector(0,0,1),Probe-FVector(0,0,4),ECC_Visibility,Params) &&
    Strength && Strength->GetSupportedActors(Shelf.GetActor(),!SupportRefreshed).Contains(TargetPallet);
  }
  if (bSupportingPallet || TargetPallet->GetAttachParentActor()==this || !bPlaced || !DestinationClear())
  {
   UE_LOG(LogTemp,Warning,TEXT("UNLOAD_POSE actual=%s yaw=%.2f desired=%s yaw=%.2f"),
    *TargetPallet->GetActorLocation().ToString(),TargetPallet->GetActorRotation().Yaw,
    *ActiveJob.Destination.GetLocation().ToString(),ActiveJob.Destination.Rotator().Yaw);
   FaultAI(TEXT("UNLOAD VERIFICATION FAILED")); break;
  }
  if (LiftTo(0)) Next(EWarehouseAIState::ReportComplete,TEXT("REPORT WORK COMPLETE"));
  break;
 }
 case EWarehouseAIState::ReportComplete:
  if (bClearingForksForWork)
  {
   ReportJob(TEXT("FORKS_CLEARED"),TEXT("기존 짐 하역·포크 철수 완료 · 새 운반 지시 시작"));
   bClearingForksForWork=false;bWorkStopNeedsRecovery=false;NextForkClearanceCheck=-1;
   WorkStopStatus.Reset();LastErrorCode=NAME_None;LastErrorMessage.Reset();
   ActiveJob=FWarehouseWorkOrder();TargetPallet=nullptr;CarriedCargo.Reset();
   Next(EWarehouseAIState::Ready,TEXT("기존 짐 하역 완료 · 새 작업 준비"));
   break;
  }
  CompletedJobIds.AddUnique(ActiveJob.JobId); ReportJob(TEXT("COMPLETED"),TEXT("PALLET PLACED AND FORKS CLEAR"));
  ActiveJob=FWarehouseWorkOrder(); TargetPallet=nullptr; CarriedCargo.Reset();
  Next(EWarehouseAIState::Ready,TEXT("READY - WAITING FOR WORK ORDER"));
  break;
 case EWarehouseAIState::PlanCharge:
  if (bSupportingPallet) { FaultAI(TEXT("UNLOAD BEFORE CHARGING")); break; }
  if (!TryAcquireCharger()) { Next(EWarehouseAIState::WaitingCharger,TEXT("충전 자리 확인 · 일반 대기"));NextResourceCheck=-1;break; }
  if (!LiftTo(0)) break;
  if (BeginFloorTransfer(ChargingStation->GetActorLocation().Z,EWarehouseAIState::PlanCharge)) break;
  ChargeExitPose=ChargingStation->GetActorTransform(); ChargeExitPose.AddToTranslation(ChargingStation->GetActorForwardVector()*120);
  if (ChargingStation->IsDocked(this)) Next(EWarehouseAIState::Charging,TEXT("CHARGING"));
  else if (FVector::Dist2D(GetActorLocation(),ChargeExitPose.GetLocation())<5.f &&
           FMath::Abs(GetActorLocation().Z-ChargeExitPose.GetLocation().Z)<1.f &&
           FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,ChargeExitPose.Rotator().Yaw))<1.f &&
           ChassisBody && ChassisBody->GetPhysicsLinearVelocity().Size()<1.f &&
           ChassisBody->GetPhysicsAngularVelocityInDegrees().Size()<.5f && NavigationClear(ChargeExitPose))
   Next(EWarehouseAIState::DockCharge,TEXT("AT CHARGER APPROACH - PRECISE DOCKING"));
  else if (PlanAutonomousRoute(ChargeExitPose)) { Next(EWarehouseAIState::NavigateCharge,TEXT("NAVIGATION TO CHARGER")); bRouteStarted=true; }
  else StopFor(TEXT("OBSTACLE / NO CHARGER ROUTE"));
  break;
 case EWarehouseAIState::NavigateCharge:
  if (!IsValid(ChargingStation)) { SetOperationError(TEXT("NO_CHARGER"),TEXT("충전할 수 없다: 충전기가 삭제되었습니다."));TransitionAI(EWarehouseAIState::WaitingCharger,Status);break; }
  // A route endpoint is a capture region. The slow docking controller handles
  // residual offset; native 1cm / 0.5-degree electrical contact remains strict.
  if (FVector::Dist2D(GetActorLocation(),ChargeExitPose.GetLocation())<ChargingCaptureDistanceCm &&
      FMath::Abs(GetActorLocation().Z-ChargeExitPose.GetLocation().Z)<1.f &&
      FMath::Abs(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,ChargeExitPose.Rotator().Yaw))<ChargingCaptureHeadingDegrees &&
      FMath::Abs(FVector::DotProduct(ChargingStation->GetActorLocation()-GetActorLocation(),GetActorRightVector()))<=ChargingDockLateralLimitCm &&
      ChassisBody && ChassisBody->GetPhysicsAngularVelocityInDegrees().Size()<.5f &&
      FMath::Abs(CurrentSpeedCm)<5.f && NavigationClear(GetActorTransform()))
  { Next(EWarehouseAIState::DockCharge,TEXT("접근 구간 확인 · 저속 도킹 보정"));break; }
  Route(ChargeExitPose,EWarehouseAIState::DockCharge,TEXT("PRECISE CHARGER DOCKING"),EmptyTravelSpeedCm);
  break;
 case EWarehouseAIState::DockCharge:
 {
  if (!IsValid(ChargingStation) || !ChargingStation->bMainsPower || ChargingStation->Occupant!=this) { SetOperationError(TEXT("NO_CHARGER"),TEXT("충전할 수 없다: 충전기가 삭제되거나 전원이 꺼졌습니다."));TransitionAI(EWarehouseAIState::WaitingCharger,Status);break; }
  if (AIElapsed>180.f) { FaultAI(TEXT("DOCK ALIGNMENT FAILED"));break; }
  const FTransform ActualPose=GetActorTransform();
  const FVector Dock=ChargingStation->GetActorLocation();
  const float Heading=FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,ChargingStation->GetActorRotation().Yaw));
  if (FParse::Param(FCommandLine::Get(),TEXT("DriveDiagnostics")) && FMath::FloorToInt(GetWorld()->GetTimeSeconds())!=FMath::FloorToInt(GetWorld()->GetTimeSeconds()-Dt))
   UE_LOG(LogTemp,Log,TEXT("AGV_DOCK_DIAG %s dt=%.5f pullout=%d turning=%d route=%d stable=%.3f along=%.3f side=%.3f heading=%.3f linear=%s angular=%s drive=%.3f turn=%.3f"),
    *VehicleName,Dt,bDockPullout,bRouteTurning,bRouteStarted,DockStableSeconds,
    FVector::DotProduct(Dock-GetActorLocation(),GetActorForwardVector()),FVector::DotProduct(Dock-GetActorLocation(),GetActorRightVector()),FMath::RadiansToDegrees(Heading),
    *ChassisBody->GetPhysicsLinearVelocity().ToString(),*ChassisBody->GetPhysicsAngularVelocityInDegrees().ToString(),DesiredDriveSpeed,DesiredTurnRate);
  if (bDockPullout)
  {
   // Correct with continuous low-speed steering on the dock line. A stationary
   // 90-degree steering slew applies a reaction to the chassis and can shift
   // the contact being aligned. Keep real sweeps/traffic/brakes throughout.
   if (MoveToLine(ChargeExitPose.GetLocation(),15,Dt,.35f)) bDockPullout=false;
   break;
  }
  if (DockStableSeconds<0)
  {
   if (!ChargingStation->IsDocked(this))
   {
    MoveToLine(Dock,10,Dt,.35f);
    if (AIState!=EWarehouseAIState::DockCharge || FMath::Abs(FVector::DotProduct(Dock-GetActorLocation(),GetActorForwardVector()))>=.35f) break;
   }
   if (!ChargingStation->IsDocked(this)) { bDockPullout=true;bRouteStarted=false;break; }
   DockStablePose=ActualPose;DockStableSeconds=0;
   BrakeDrive();
   break;
  }
  // Keep finite brakes applied without repeatedly chasing tyre-contact jitter.
  // Every sampled pose must remain inside this window and the strict contact.
  BrakeDrive();
  if (!ChargingStation->IsDocked(this) ||
      FVector::Dist(ActualPose.GetLocation(),DockStablePose.GetLocation())>.25f ||
      ActualPose.GetRotation().AngularDistance(DockStablePose.GetRotation())>FMath::DegreesToRadians(.15f))
  { DockStableSeconds=-1;break; }
  DockStableSeconds+=Dt;
  if (DockStableSeconds>=1.f) Next(EWarehouseAIState::Charging,TEXT("CHARGING"));
  break;
 }
 case EWarehouseAIState::Charging:
 {
  if (!IsValid(ChargingStation) || !ChargingStation->bMainsPower || ChargingStation->PowerKw<=0 || ChargingStation->Efficiency<=0 || ChargingStation->Occupant!=this || !ChargingStation->IsDocked(this))
  { SetOperationError(TEXT("CHARGER_UNAVAILABLE"),TEXT("충전할 수 없다: 충전기 연결·전원·예약을 확인하세요."));TransitionAI(EWarehouseAIState::WaitingCharger,Status);break; }
  if (bIdleChargeParking && BatteryPercent>ChargeBelowPercent)
  {
   bIdleChargeParking=false;bChargePending=false;bOpportunityCharge=false;ChargingStation->Release(this);
   Next(EWarehouseAIState::Ready,TEXT("충전 자리에서 다음 작업 대기 · 충전 필요 차량에 양보"));break;
  }
  bIdleChargeParking=false;
  const float Target=bChargeToFull ? 100.f : bOpportunityCharge ? FMath::Clamp(OpportunityChargeBelowPercent,ChargeBelowPercent+1.f,100.f) : FMath::Clamp(ResumeAtPercent,75.f,100.f);
  if (BatteryPercent<Target) BatteryPercent=FMath::Min(Target,BatteryPercent+100.f*ChargingStation->PowerKw*1000.f*FMath::Clamp(ChargingStation->Efficiency,0.f,1.f)*Dt*FMath::Clamp(BatteryTimeScale,1.f,3600.f)/(3600.f*BatteryVoltage*BatteryCapacityAh));
  ChargingStation->ShowCharge(BatteryPercent,true);
  if (BatteryPercent>=Target)
  {
   bChargePending=false; bChargeToFull=false;bOpportunityCharge=false; ChargingStation->ShowCharge(BatteryPercent,false);
   bool OtherNeedsCharge=false,FreeWaitingBay=false;
   for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
    if (*It!=this && It->bPowered && (It->bChargePending || It->BatteryPercent<=It->ChargeBelowPercent || It->bOpportunityCharge)) { OtherNeedsCharge=true;break; }
   for (TActorIterator<AWarehouseWaitingBay> It(GetWorld());It;++It)
    if ((!IsValid(It->Occupant) || It->Occupant==this) && FMath::Abs(It->GetActorLocation().Z-GetActorLocation().Z)<5) { FreeWaitingBay=true;break; }
   if (bIdleParkingEnabled && PendingJobs.IsEmpty() && !OtherNeedsCharge && !FreeWaitingBay)
   {
    ChargingStation->Release(this);Next(EWarehouseAIState::Ready,TEXT("충전 완료 · 충전 자리에서 다음 지시 대기"));break;
   }
   Next(EWarehouseAIState::LeaveCharger,TEXT("CHARGE COMPLETE - LEAVE DOCK"));
  }
  break;
 }
 case EWarehouseAIState::LeaveCharger:
  if (MovePrecise(ChargeExitPose.GetLocation(),15,Dt))
  { if (IsValid(ChargingStation)) ChargingStation->Release(this); Next(EWarehouseAIState::Ready,TEXT("READY - WAITING FOR WORK ORDER")); }
  break;
 case EWarehouseAIState::ElevatorApproach:
  Route(Elevator->WaitingPose(TransferFromFloor),EWarehouseAIState::ElevatorCall,TEXT("FMS CALL ELEVATOR"),LoadedTravelSpeedCm);
  break;
 case EWarehouseAIState::ElevatorCall:
  // Continue physical low-speed alignment after route braking settles.
  if (AIElapsed>180.f) { FaultAI(TEXT("ELEVATOR RESERVATION TIMEOUT"));break; }
  if (!MovePrecise(Elevator->WaitingPose(TransferFromFloor).GetLocation(),5,Dt)) break;
  if (Elevator->RequestTransfer(this,TransferFromFloor,TransferToFloor)) Next(EWarehouseAIState::ElevatorWait,TEXT("WAIT FOR ARRIVAL / OPEN DOOR / ENTRY PERMISSION"));
  break;
 case EWarehouseAIState::ElevatorWait:
  if (Elevator->CanEnter(this)) Next(EWarehouseAIState::ElevatorBoard,TEXT("ENTER PLATFORM - PRECISE STOP"));
  else if (AIElapsed>180.f) FaultAI(TEXT("ELEVATOR ARRIVAL TIMEOUT"));
  break;
 case EWarehouseAIState::ElevatorBoard:
  if (!Elevator->CanEnter(this)) { StopFor(TEXT("OBSTACLE IN ELEVATOR / ENTRY INTERLOCK")); break; }
  if (MovePrecise(Elevator->BoardingPose(TransferFromFloor).GetLocation(),15,Dt))
  {
   if (!Elevator->ConfirmBoarded(this)) { FaultAI(TEXT("ELEVATOR BOARDING POSE / LOAD ENVELOPE FAILED")); break; }
   Next(EWarehouseAIState::ElevatorRide,TEXT("BOARDING CONFIRMED - WAIT FOR CLOSED DOORS / FLOOR TRANSFER"));
  }
  break;
 case EWarehouseAIState::ElevatorRide:
  if (Elevator->CanExit(this)) Next(EWarehouseAIState::ElevatorExit,TEXT("DESTINATION FLOOR / DOOR OPEN - EXIT"));
  // A human-requested stop is a normal part of the shared trip, not failed
  // vehicle motion. Keep the default brake and exclude its open-door dwell.
  else if (Elevator->State==EWarehouseElevatorState::AwaitExit && Elevator->CurrentFloor!=TransferToFloor) AIElapsed-=Dt;
  else if (AIElapsed>180.f) FaultAI(TEXT("ELEVATOR TRANSFER TIMEOUT"));
  break;
 case EWarehouseAIState::ElevatorExit:
  if (!Elevator->CanExit(this)) { StopFor(TEXT("ELEVATOR EXIT INTERLOCK")); break; }
  if (MovePrecise(Elevator->WaitingPose(TransferToFloor).GetLocation(),20,Dt))
  {
   if (!Elevator->ConfirmExited(this)) { FaultAI(TEXT("ELEVATOR EXIT CONFIRMATION FAILED")); break; }
   bFloorTransfer=false;
   Next(AfterElevator,TEXT("FLOOR MAP SELECTED - RESUME TRANSPORT"));
  }
  break;
 case EWarehouseAIState::WaitingObstacle:
  WaitSeconds+=Dt;
  if (WaitSeconds>=30.f) { ResumeAI=WaitResumeAI; FaultAI(TEXT("OBSTACLE TIMEOUT - CLEAR ROUTE")); ResumeAI=WaitResumeAI; }
  else if (AIElapsed>=1.f)
  {
   if (WaitObstacleReason.Contains(TEXT("PERSON")) || bFloorTransfer)
    Next(WaitResumeAI,TEXT("RECHECK PATH / SAFETY ZONE"));
   else BeginRecovery(WaitResumeAI,WaitObstacleReason);
  }
  break;
 case EWarehouseAIState::RecoverLower:
 case EWarehouseAIState::RecoverWithdraw:
 case EWarehouseAIState::RecoverReplan:
  AdvanceRecovery(Dt);
  break;
 default: break;
 }
 CheckDriveProgress(Dt);
}
