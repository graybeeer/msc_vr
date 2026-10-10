#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseChargingStation.h"
#include "WarehouseElevator.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"

FString AWarehouseForklift::CheckSystems() const
{
 if (bMechanicalFailure) return TEXT("MECHANICAL FAILURE");
 if (!bSafetyScannerHealthy) return TEXT("SAFETY SCANNER FAULT");
 if (!bEStopReleased) return TEXT("E-STOP ACTIVE");
 if (!bBrakeHealthy) return TEXT("BRAKE FAULT");
 if (!bSteeringHealthy) return TEXT("STEERING FAULT");
 if (!bForkSensorHealthy) return TEXT("FORK / MAST SENSOR FAULT");
 if (!bLocalizationHealthy) return TEXT("LOCALIZATION LOST");
 if (!bBatteryHealthy || BatteryVoltage<=0 || BatteryCapacityAh<=0) return TEXT("BATTERY FAULT");
 if (BatteryPercent<=0 && !(IsValid(ChargingStation) && ChargingStation->IsDocked(this))) return TEXT("BATTERY EMPTY - RECOVERY REQUIRED");
 if (!bPalletSensorHealthy) return TEXT("PALLET SENSOR FAULT");
 if (!bLoadSensorHealthy) return TEXT("LOAD SENSOR FAULT");
 return FString();
}
void AWarehouseForklift::ReportJob(const FString& Result,const FString& Detail)
{
 FWarehouseJobReport Report;
 Report.JobId=ActiveJob.JobId; Report.Result=Result; Report.Detail=Detail; Report.SimulationSeconds=AITotalSeconds;
 JobReports.Add(Report);
 if (JobReports.Num()>100) JobReports.RemoveAt(0);
 OnJobReport.Broadcast(Report);
 UE_LOG(LogTemp,Log,TEXT("AGV_REPORT [%s] %s: %s"),*Report.JobId,*Result,*Detail);
}
void AWarehouseForklift::TransitionAI(EWarehouseAIState Next,const FString& Message)
{
 AIState=Next; AIElapsed=0; bRouteStarted=false; BrakeDrive();
 SetStatus(Message);
 ReportJob(TEXT("STATE"),Message);
}
void AWarehouseForklift::FaultAI(const FString& Reason)
{
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
 if (bPowered)
 {
  if (AIState!=EWarehouseAIState::SelfCheck || !bResumeAfterCheck) ResumeAI=AIState;
  bPowered=false;
  TransitionAI(EWarehouseAIState::Paused,TEXT("PAUSED - E: SELF CHECK / RESUME"));
  if (IsValid(ChargingStation)) ChargingStation->ShowCharge(BatteryPercent,false);
  return;
 }
 bResumeAfterCheck=AIState==EWarehouseAIState::Paused || AIState==EWarehouseAIState::Fault;
 bPowered=true;
 TransitionAI(EWarehouseAIState::SelfCheck,TEXT("POWER ON - SELF CHECK"));
}
bool AWarehouseForklift::SubmitJob(const FWarehouseWorkOrder& Job)
{
 if (Job.JobId.IsEmpty() || !IsValid(Job.Pallet) || Job.Destination.ContainsNaN() ||
     !Job.Destination.GetScale3D().Equals(FVector::OneVector,.001f) || PendingJobs.Num()>=32) return false;
 if (ActiveJob.JobId==Job.JobId || CompletedJobIds.Contains(Job.JobId)) return false;
 for (const auto& Existing : PendingJobs) if (Existing.JobId==Job.JobId || Existing.Pallet==Job.Pallet) return false;
 if (!ActiveJob.JobId.IsEmpty() && ActiveJob.Pallet==Job.Pallet) return false;
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) if (*It!=this)
 {
  if (It->ActiveJob.JobId==Job.JobId || It->ActiveJob.Pallet==Job.Pallet ||
      (It->bSupportingPallet && It->TargetPallet==Job.Pallet)) return false;
  for (const auto& Other : It->PendingJobs)
   if (Other.JobId==Job.JobId || Other.Pallet==Job.Pallet) return false;
 }
 PendingJobs.Add(Job);
 return true;
}
FTransform AWarehouseForklift::PalletApproach(const FTransform& Pose,float Distance) const
{
 const FRotator Rotation(0,Pose.Rotator().Yaw,0);
 FVector Point=Pose.GetLocation()-Rotation.Vector()*Distance;
 Point.Z=FloorBase(Pose.GetLocation().Z);
 return FTransform(Rotation,Point);
}
bool AWarehouseForklift::NavigationClear(const FTransform& Pose) const
{
 FCollisionQueryParams Params(SCENE_QUERY_STAT(AGVNavigation),false,this);
 if (bSupportingPallet && IsValid(TargetPallet)) Params.AddIgnoredActor(TargetPallet);
 for (AActor* Load : CarriedCargo) if (IsValid(Load)) Params.AddIgnoredActor(Load);
 FBox Envelope(FVector(-126,-53,3),FVector(119,53,240));
 // Include the actual load overhang in turning clearance, not just the truck footprint.
 if (bSupportingPallet)
 {
  TArray<AActor*> Loads{TargetPallet};
  for (AActor* Cargo : CarriedCargo) Loads.Add(Cargo);
  for (AActor* Load : Loads) if (IsValid(Load))
  {
   FVector Center,Extent; Load->GetActorBounds(true,Center,Extent);
   for (float X : {-1.f,1.f}) for (float Y : {-1.f,1.f}) for (float Z : {-1.f,1.f})
    Envelope+=GetActorTransform().InverseTransformPosition(Center+Extent*FVector(X,Y,Z));
  }
 }
 const FVector Center=Pose.TransformPosition(Envelope.GetCenter());
 if (GetWorld()->OverlapBlockingTestByChannel(Center,Pose.GetRotation(),ECC_Visibility,FCollisionShape::MakeBox(Envelope.GetExtent()),Params))
 {
  if (UE_LOG_ACTIVE(LogTemp,Verbose))
  {
   TArray<FOverlapResult> Hits;
   GetWorld()->OverlapMultiByChannel(Hits,Center,Pose.GetRotation(),ECC_Visibility,FCollisionShape::MakeBox(Envelope.GetExtent()),Params);
   for (const auto& Hit : Hits) if (Hit.bBlockingHit) UE_LOG(LogTemp,Verbose,TEXT("AGV route blocked at %s by %s"),*Pose.GetLocation().ToString(),*GetNameSafe(Hit.GetActor()));
  }
  return false;
 }
 FHitResult Ground;
 const FVector P=Pose.GetLocation();
 const bool Supported=GetWorld()->LineTraceSingleByChannel(Ground,P+FVector(0,0,3),P-FVector(0,0,5),ECC_Visibility,Params) && Ground.ImpactNormal.Z>.98f;
 if (!Supported) UE_LOG(LogTemp,Verbose,TEXT("AGV route unsupported at %s, hit %s"),*P.ToString(),*GetNameSafe(Ground.GetActor()));
 return Supported;
}
bool AWarehouseForklift::ConnectPoses(const FTransform& From,const FTransform& To,TArray<FWarehouseRoutePoint>& Route,float& Cost) const
{
 Route.Reset(); Cost=FLT_MAX;
 const FVector A=From.GetLocation(),D=To.GetLocation();
 const float Distance=FVector::Dist2D(A,D);
 if (FMath::Abs(A.Z-D.Z)>1 || From.ContainsNaN() || To.ContainsNaN()) return false;
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
    if (Segment>10 || !NavigationClear(Step.Pose)) { Valid=false; break; }
    Length+=Segment;
    Candidate.Add(Step); Previous=Step.Pose;
   }
   const float Weighted=Length*(Direction<0 ? 1.15f : 1.f);
   if (Valid && Weighted<Cost) { Route=MoveTemp(Candidate); Cost=Weighted; }
  }
 return Cost<FLT_MAX;
}
bool AWarehouseForklift::PlanAutonomousRoute(const FTransform& Goal)
{
 PlannedRoute.Reset(); RouteIndex=0;
 float DirectCost;
 if (ConnectPoses(GetActorTransform(),Goal,PlannedRoute,DirectCost)) return true;
 // Pose graph: each edge is a swept, curvature-limited cubic. Anchors describe warehouse travel lanes.
 // ponytail: at most 24 lane poses; use incremental hybrid A* for a large unstructured warehouse.
 TArray<FTransform> Nodes{GetActorTransform(),Goal};
 for (int32 I=0; I<FMath::Min(24,NavigationAnchors.Num()); ++I)
  if (FMath::Abs(NavigationAnchors[I].GetLocation().Z-GetActorLocation().Z)<1.f && NavigationClear(NavigationAnchors[I])) Nodes.Add(NavigationAnchors[I]);
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
  if (Current==1) { PlannedRoute=MoveTemp(Paths[1]); return true; }
  Closed[Current]=true;
  for (int32 Next=1; Next<Nodes.Num(); ++Next) if (!Closed[Next])
  {
   TArray<FWarehouseRoutePoint> Edge; float EdgeCost;
   if (ConnectPoses(Nodes[Current],Nodes[Next],Edge,EdgeCost) && Costs[Current]+EdgeCost<Costs[Next])
   {
    Costs[Next]=Costs[Current]+EdgeCost;
    Paths[Next]=Paths[Current]; Paths[Next].Append(Edge);
   }
  }
 }
 return false;
}
bool AWarehouseForklift::FollowRoute(float Speed,float Dt)
{
 if (PlannedRoute.IsEmpty()) { BrakeDrive(); return true; }
 const auto& Goal=PlannedRoute.Last();
 const float Remaining=FVector::Dist2D(GetActorLocation(),Goal.Pose.GetLocation());
 const float HeadingError=FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(GetActorRotation().Yaw,Goal.Pose.Rotator().Yaw));
 if (Remaining<.8f && FMath::Abs(HeadingError)<FMath::DegreesToRadians(.7f))
 { BrakeDrive(); return FMath::Abs(CurrentSpeedCm)<1.f; }
 RouteIndex=FMath::Min(RouteIndex,PlannedRoute.Num()-1);
 while (RouteIndex+1<PlannedRoute.Num() && FVector::Dist2D(GetActorLocation(),PlannedRoute[RouteIndex].Pose.GetLocation())<30.f &&
        PlannedRoute[RouteIndex+1].bReverse==PlannedRoute[RouteIndex].bReverse) ++RouteIndex;
 const auto& Step=PlannedRoute[RouteIndex];
 const FVector Local=GetActorTransform().InverseTransformPosition(Step.Pose.GetLocation());
 const float Sign=Step.bReverse ? -1.f : 1.f;
 const float Limit=bSupportingPallet ? FMath::Min(100.f,LoadedTravelSpeedCm) : FMath::Min(180.f,EmptyTravelSpeedCm);
 float Desired=FMath::Min(FMath::Min(Speed,Limit),FMath::Sqrt(240.f*Remaining));
 if (Remaining<20.f) Desired=FMath::Min(Desired,Remaining*1.8f);
 float Curvature=2.f*Local.Y/FMath::Max(900.f,Local.SizeSquared2D());
 if (Remaining<40.f) Curvature+=Sign*HeadingError/40.f;
 const FVector Preview=GetActorLocation()+GetActorForwardVector()*Sign*Desired*Dt;
 if (!NavigationClear(FTransform(GetActorQuat(),Preview))) { StopFor(TEXT("OBSTACLE ON ROUTE")); return false; }
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
bool AWarehouseForklift::DestinationClear() const
{
 const FTransform& Pose=ActiveJob.Destination;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(DropReservation),false,this);
 if (IsValid(TargetPallet)) Params.AddIgnoredActor(TargetPallet);
 for (AActor* Cargo : CarriedCargo) if (IsValid(Cargo)) Params.AddIgnoredActor(Cargo);
 const FVector Center=Pose.TransformPosition(FVector(0,0,8));
 if (GetWorld()->OverlapBlockingTestByChannel(Center,Pose.GetRotation(),ECC_Visibility,FCollisionShape::MakeBox(FVector(54,54,6)),Params)) return false;
 FHitResult Surface;
 return GetWorld()->LineTraceSingleByChannel(Surface,Pose.GetLocation()+FVector(0,0,1),Pose.GetLocation()-FVector(0,0,4),ECC_Visibility,Params) && Surface.ImpactNormal.Z>.98f;
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
 if (!IsValid(Elevator)) { FaultAI(TEXT("NO ELEVATOR FOR TARGET FLOOR")); return true; }
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
 AIElapsed+=Dt;
 const FString Fault=CheckSystems();
 if (!Fault.IsEmpty()) { FaultAI(Fault); return; }
 if (bFloorTransfer)
 {
  if (!IsValid(Elevator)) { FaultAI(TEXT("ELEVATOR LOST")); return; }
  const FString LiftFault=Elevator->CheckInterlocks();
  if (!LiftFault.IsEmpty()) { FaultAI(LiftFault); return; }
 }
 if (AIState!=EWarehouseAIState::Charging) ConsumeEnergy(100.f*Dt/3600.f);
 if (BatteryPercent<=0 && AIState!=EWarehouseAIState::Charging) { FaultAI(TEXT("BATTERY EMPTY")); return; }
 if (!ActiveJob.JobId.IsEmpty())
 {
  if (!IsValid(TargetPallet)) { FaultAI(TEXT("TARGET LOST")); return; }
  if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->HasFailed(TargetPallet)) { FaultAI(TEXT("PALLET DAMAGED")); return; }
  if (GetLoadMassKg()>FMath::Min(1400.f,RatedLoadKg)) { FaultAI(TEXT("OVERLOAD - REMOVE LOAD")); return; }
 }
 if (!UpdateLoadSupport(Dt)) { FaultAI(TEXT("LOAD SLIPPED / SUPPORT LOST")); return; }
 auto Next=[&](EWarehouseAIState Value,const TCHAR* Message) { TransitionAI(Value,Message); };
 auto Route=[&](const FTransform& Goal,EWarehouseAIState Done,const TCHAR* Message,float Speed)
 {
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
  if (bChargePending || BatteryPercent<=ChargeBelowPercent) Next(EWarehouseAIState::PlanCharge,TEXT("PLAN CHARGING"));
  else if (!PendingJobs.IsEmpty())
  {
   ActiveJob=PendingJobs[0]; PendingJobs.RemoveAt(0); TargetPallet=ActiveJob.Pallet; bPalletReleased=false;
   Next(EWarehouseAIState::ValidateJob,TEXT("WORK ORDER RECEIVED")); ReportJob(TEXT("ACCEPTED"),ActiveJob.SourceSystem);
  }
  break;
 case EWarehouseAIState::ValidateJob:
  if (PalletClaimedByOther(TargetPallet)) { FaultAI(TEXT("PALLET RESERVED BY ANOTHER AGV")); break; }
  if (ActiveJob.JobId.IsEmpty() || CompletedJobIds.Contains(ActiveJob.JobId) || ActiveJob.Destination.ContainsNaN() ||
      !ActiveJob.Destination.GetScale3D().Equals(FVector::OneVector,.001f) ||
      ActiveJob.Destination.GetLocation().Z-FloorBase(ActiveJob.Destination.GetLocation().Z)<-.1f ||
      ActiveJob.Destination.GetLocation().Z-FloorBase(ActiveJob.Destination.GetLocation().Z)+20.f>FMath::Min(MaxForkHeightCm,160.f) ||
      TargetPallet->GetActorLocation().Z-FloorBase(TargetPallet->GetActorLocation().Z)<-.1f ||
      TargetPallet->GetActorLocation().Z-FloorBase(TargetPallet->GetActorLocation().Z)+20.f>FMath::Min(MaxForkHeightCm,160.f) ||
      FMath::Abs(ActiveJob.Destination.Rotator().Pitch)>.1f || FMath::Abs(ActiveJob.Destination.Rotator().Roll)>.1f)
  { FaultAI(TEXT("INVALID JOB / LIFT RANGE")); break; }
  if (!DestinationClear()) { FaultAI(TEXT("DESTINATION OCCUPIED / UNSUPPORTED")); break; }
  Next(EWarehouseAIState::PlanPickup,TEXT("PLAN PICKUP ROUTE"));
  break;
 case EWarehouseAIState::PlanPickup:
  if (BeginFloorTransfer(TargetPallet->GetActorLocation().Z,EWarehouseAIState::PlanPickup)) break;
  if (PlanAutonomousRoute(PalletApproach(TargetPallet->GetActorTransform(),220)))
  { Next(EWarehouseAIState::NavigatePickup,TEXT("NAVIGATION TO PICKUP")); bRouteStarted=true; }
  else StopFor(TEXT("OBSTACLE / NO PICKUP ROUTE"));
  break;
 case EWarehouseAIState::NavigatePickup:
  Route(PalletApproach(TargetPallet->GetActorTransform(),220),EWarehouseAIState::SlowApproach,TEXT("PALLET APPROACH - LOW SPEED"),EmptyTravelSpeedCm);
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
  Route(PalletApproach(ObservedPallet,185),EWarehouseAIState::CorrectFork,TEXT("CORRECT FORK HEIGHT / CHECK ANGLE"),15);
  break;
 case EWarehouseAIState::CorrectFork:
  if (LiftTo(FMath::Max(0.f,SourceHeight))) Next(EWarehouseAIState::InsertFork,TEXT("FORK INSERT - CREEP"));
  break;
 case EWarehouseAIState::InsertFork:
  // Fully insert both tines with 5 cm end clearance for native tyre settling.
  if (MovePrecise(PalletApproach(ObservedPallet,65).GetLocation(),10,Dt)) Next(EWarehouseAIState::VerifyInsertion,TEXT("VERIFY BOTH TINES INSERTED"));
  break;
 case EWarehouseAIState::VerifyInsertion:
  if (!TargetPallet->CanEngage(Carriage->GetComponentTransform())) { FaultAI(TEXT("FORK INSERTION FAILED")); break; }
  PalletContactLiftCm=TargetPallet->GetSupportLiftOffset(Carriage->GetComponentTransform());
  if (PalletContactLiftCm<0) { FaultAI(TEXT("NO PALLET SUPPORT")); break; }
  Next(EWarehouseAIState::LiftLoad,TEXT("LIFT LOAD"));
  break;
 case EWarehouseAIState::LiftLoad:
  if (!bSupportingPallet)
  {
   TrackCargo();
   bSupportingPallet=true;
  }
  if (LiftTo(SourceHeight+10.5f)) Next(EWarehouseAIState::VerifyLoad,TEXT("VERIFY LOAD PRESENT / MASS"));
  break;
 case EWarehouseAIState::VerifyLoad:
  if (AIElapsed<.3f) break; // Let the physical contact settle before reading the load sensor.
  if (!bSupportingPallet || !PalletOnForks() || GetLoadMassKg()<=0)
  { FaultAI(TEXT("LOAD VERIFICATION FAILED")); break; }
  RetractLocation=GetActorLocation()-GetActorForwardVector()*130;
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
  if (!DestinationClear()) { FaultAI(TEXT("DESTINATION OCCUPIED / UNSUPPORTED")); break; }
  if (PlanAutonomousRoute(PalletApproach(ActiveJob.Destination,220)))
  { Next(EWarehouseAIState::NavigateDelivery,TEXT("NAVIGATION TO DESTINATION")); bRouteStarted=true; }
  else StopFor(TEXT("OBSTACLE / NO DELIVERY ROUTE"));
  break;
 case EWarehouseAIState::NavigateDelivery:
  Route(PalletApproach(ActiveJob.Destination,220),EWarehouseAIState::AlignUnload,TEXT("ALIGN UNLOADING POSITION"),LoadedTravelSpeedCm);
  break;
 case EWarehouseAIState::AlignUnload:
 {
  if (!DestinationClear()) { FaultAI(TEXT("DESTINATION OCCUPIED / UNSUPPORTED")); break; }
  // Follow the sensed load position; contact can shift it along the tines.
  // Correct by driving the vehicle, without repositioning the physical pallet.
  const FVector Error=ActiveJob.Destination.GetLocation()-TargetPallet->GetActorLocation();
  const FVector Goal=GetActorLocation()+GetActorForwardVector()*FVector::DotProduct(Error,GetActorForwardVector());
  if (LiftTo(DropHeight+10.5f) && MovePrecise(Goal,10,Dt))
   Next(EWarehouseAIState::LowerLoad,TEXT("LOWER LOAD ONTO SUPPORT"));
  break;
 }
 case EWarehouseAIState::LowerLoad:
  if (!bPalletReleased)
  {
   if (!DestinationClear()) { FaultAI(TEXT("DESTINATION OCCUPIED / UNSUPPORTED")); break; }
   if (!LiftTo(DropHeight+PalletContactLiftCm)) break;
   bSupportingPallet=false; bPalletReleased=true;
  }
  if (LiftTo(DropHeight))
  { RetractLocation=GetActorLocation()-GetActorForwardVector()*130; Next(EWarehouseAIState::WithdrawFork,TEXT("FORK WITHDRAW")); }
  break;
 case EWarehouseAIState::WithdrawFork:
  if (MovePrecise(RetractLocation,15,Dt))
  {
   Next(EWarehouseAIState::VerifyUnload,TEXT("VERIFY UNLOAD COMPLETE"));
  }
  break;
 case EWarehouseAIState::VerifyUnload:
  if (AIElapsed<.5f) break;
  if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && !CarriedCargo.IsEmpty())
  {
   const TArray<AActor*> Supported=Strength->GetSupportedActors(TargetPallet);
   for (AActor* Cargo : CarriedCargo)
    if (!IsValid(Cargo) || !Supported.Contains(Cargo) || Strength->HasFailed(Cargo))
    { FaultAI(TEXT("UNLOAD VERIFICATION FAILED - CARGO LOST / DAMAGED")); return; }
  }
  if (bSupportingPallet || TargetPallet->GetAttachParentActor()==this ||
      FVector::Dist(TargetPallet->GetActorLocation(),ActiveJob.Destination.GetLocation())>2 ||
      FMath::Abs(FMath::FindDeltaAngleDegrees(TargetPallet->GetActorRotation().Yaw,ActiveJob.Destination.Rotator().Yaw))>2 || !DestinationClear())
  { FaultAI(TEXT("UNLOAD VERIFICATION FAILED")); break; }
  if (LiftTo(0)) Next(EWarehouseAIState::ReportComplete,TEXT("REPORT WORK COMPLETE"));
  break;
 case EWarehouseAIState::ReportComplete:
  CompletedJobIds.AddUnique(ActiveJob.JobId); ReportJob(TEXT("COMPLETED"),TEXT("PALLET PLACED AND FORKS CLEAR"));
  ActiveJob=FWarehouseWorkOrder(); TargetPallet=nullptr; CarriedCargo.Reset();
  Next(EWarehouseAIState::Ready,TEXT("READY - WAITING FOR WORK ORDER"));
  break;
 case EWarehouseAIState::PlanCharge:
  if (bSupportingPallet) { FaultAI(TEXT("UNLOAD BEFORE CHARGING")); break; }
  if (!IsValid(ChargingStation) || !ChargingStation->TryReserve(this)) { FaultAI(TEXT("CHARGER BUSY / OFFLINE")); break; }
  if (!LiftTo(0)) break;
  if (BeginFloorTransfer(ChargingStation->GetActorLocation().Z,EWarehouseAIState::PlanCharge)) break;
  ChargeExitPose=ChargingStation->GetActorTransform(); ChargeExitPose.AddToTranslation(ChargingStation->GetActorForwardVector()*120);
  if (ChargingStation->IsDocked(this)) Next(EWarehouseAIState::Charging,TEXT("CHARGING"));
  else if (PlanAutonomousRoute(ChargeExitPose)) { Next(EWarehouseAIState::NavigateCharge,TEXT("NAVIGATION TO CHARGER")); bRouteStarted=true; }
  else StopFor(TEXT("OBSTACLE / NO CHARGER ROUTE"));
  break;
 case EWarehouseAIState::NavigateCharge:
  Route(ChargeExitPose,EWarehouseAIState::DockCharge,TEXT("PRECISE CHARGER DOCKING"),EmptyTravelSpeedCm);
  break;
 case EWarehouseAIState::DockCharge:
  if (!IsValid(ChargingStation) || !ChargingStation->bMainsPower || ChargingStation->Occupant!=this) { FaultAI(TEXT("CHARGER LOST / OFFLINE")); break; }
  if (MovePrecise(ChargingStation->GetActorLocation(),10,Dt))
  {
   if (!ChargingStation->IsDocked(this)) { FaultAI(TEXT("DOCK ALIGNMENT FAILED")); break; }
   Next(EWarehouseAIState::Charging,TEXT("CHARGING"));
  }
  break;
 case EWarehouseAIState::Charging:
 {
  if (!IsValid(ChargingStation) || !ChargingStation->bMainsPower || ChargingStation->PowerKw<=0 || ChargingStation->Efficiency<=0 || ChargingStation->Occupant!=this || !ChargingStation->IsDocked(this))
  { FaultAI(TEXT("CHARGING CONTACT / POWER LOST")); break; }
  const float Target=bChargeToFull ? 100.f : FMath::Clamp(ResumeAtPercent,75.f,100.f);
  BatteryPercent=FMath::Min(Target,BatteryPercent+100.f*ChargingStation->PowerKw*1000.f*FMath::Clamp(ChargingStation->Efficiency,0.f,1.f)*Dt*FMath::Clamp(BatteryTimeScale,1.f,3600.f)/(3600.f*BatteryVoltage*BatteryCapacityAh));
  ChargingStation->ShowCharge(BatteryPercent,true);
  if (BatteryPercent>=Target)
  {
   bChargePending=false; bChargeToFull=false; ChargingStation->ShowCharge(BatteryPercent,false);
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
  if (Elevator->RequestTransfer(this,TransferFromFloor,TransferToFloor)) Next(EWarehouseAIState::ElevatorWait,TEXT("WAIT FOR ARRIVAL / OPEN DOOR / ENTRY PERMISSION"));
  else if (AIElapsed>180.f) FaultAI(TEXT("ELEVATOR RESERVATION TIMEOUT"));
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
  else if (AIElapsed>=1.f) Next(WaitResumeAI,TEXT("RECHECK PATH / SAFETY ZONE"));
  break;
 default: break;
 }
}
