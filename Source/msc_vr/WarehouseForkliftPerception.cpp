#include "WarehouseForklift.h"
#include "Agv/AgvShadowSensors.h"
#include "WarehouseLayoutEditor.h"
#include "WarehousePhysics.h"
#include "WarehousePallet.h"
#include "WarehouseFleetCoordinator.h"
#include "WarehouseElevator.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
// The broad phase uses the saved observation only. Projecting the query back
// through the saved and live frames cancels the latter mathematically: native
// fixed-building geometry is tested at its published map pose, not a hidden
// actor's new position. Unknown objects never enter this native-shape path.
FTransform AtPublishedPose(const FTransform& Query,const FAgvObservedObstacle& Obstacle)
{
 return Query*Obstacle.ObservedTransform.Inverse()*Obstacle.Component->GetComponentTransform();
}

bool PointCloudOverlaps(const FAgvObservedObstacle& Obstacle,const FTransform& Pose,const FBox& LocalBox,FVector* BlockingPoint=nullptr)
{
 if (!Obstacle.WorldBounds.Intersect(LocalBox.TransformBy(Pose.ToMatrixWithScale()))) return false;
 const FBox Padded=LocalBox.ExpandBy(FVector(2));
 for (const FVector& Point : Obstacle.WorldPoints)
  if (Padded.IsInsideOrOn(Pose.InverseTransformPosition(Point)))
  { if (BlockingPoint) *BlockingPoint=Point;return true; }
 return false;
}

bool PointCloudOverlapsPart(const FAgvObservedObstacle& Obstacle,UStaticMeshComponent* Part,const FTransform& Pose,FVector* BlockingPoint=nullptr)
{
 const auto* Setup=Part->GetPhysicsBodySetup();
 if (!Setup) return PointCloudOverlaps(Obstacle,Pose,Part->GetStaticMesh()->GetBoundingBox(),BlockingPoint);
 if (!Obstacle.WorldBounds.Intersect(Setup->AggGeom.CalcAABB(Pose))) return false;
 for (const FVector& WorldPoint : Obstacle.WorldPoints)
  if (Setup->GetShortestDistanceToPoint(WorldPoint,Pose,true)<=2.f)
  { if (BlockingPoint) *BlockingPoint=WorldPoint;return true; }
 return false;
}

bool SeparatingFromMeasuredPoint(const UBodySetup* Shape,FVector Point,const FTransform& Start,FVector Delta)
{
 if (Delta.IsNearlyZero()) return true;
 FVector Closest=FVector::ZeroVector,Normal=FVector::ZeroVector;
 const float InitialDistance=Shape->GetClosestPointAndNormal(Point,Start,Closest,Normal,true);
 if (!FMath::IsFinite(InitialDistance) || InitialDistance==MAX_flt) return false;
 // UE returns no normal inside a box. Probe at most 2cm through our own
 // shape's contact skin to obtain its native surface normal, never another
 // object's geometry. Ambiguous/deep overlaps remain blocked.
 if (Normal.IsNearlyZero()) for (float Radius : {.25f,.5f,1.f,2.f})
 {
  float BestDistance=0;
  for (FVector Axis : {FVector(1,0,0),FVector(-1,0,0),FVector(0,1,0),FVector(0,-1,0),FVector(0,0,1),FVector(0,0,-1)})
  {
   FVector P=FVector::ZeroVector,N=FVector::ZeroVector;
   const float Distance=Shape->GetClosestPointAndNormal(Point+Axis*Radius,Start,P,N,true);
   if (FMath::IsFinite(Distance) && Distance<MAX_flt && Distance>BestDistance && !N.IsNearlyZero()) { BestDistance=Distance;Normal=N; }
  }
  if (!Normal.IsNearlyZero()) break;
 }
 return !Normal.IsNearlyZero() && FVector::DotProduct(Delta,Normal)<=.001f;
}

bool CatalogBoxesOverlap(const FTransform& A,const FBox& BoxA,const FTransform& B,const FBox& BoxB)
{
 const FVector AxesA[]{A.GetUnitAxis(EAxis::X),A.GetUnitAxis(EAxis::Y),A.GetUnitAxis(EAxis::Z)};
 const FVector AxesB[]{B.GetUnitAxis(EAxis::X),B.GetUnitAxis(EAxis::Y),B.GetUnitAxis(EAxis::Z)};
 const FVector HalfA=BoxA.GetExtent()*A.GetScale3D().GetAbs(),HalfB=BoxB.GetExtent()*B.GetScale3D().GetAbs();
 const FVector Difference=A.TransformPosition(BoxA.GetCenter())-B.TransformPosition(BoxB.GetCenter());
 auto Separated=[&](FVector Axis)
 {
  double Radius=0;
  for (int32 I=0;I<3;++I) Radius+=HalfA[I]*FMath::Abs(FVector::DotProduct(AxesA[I],Axis))+HalfB[I]*FMath::Abs(FVector::DotProduct(AxesB[I],Axis));
  return FMath::Abs(FVector::DotProduct(Difference,Axis))>Radius+KINDA_SMALL_NUMBER;
 };
 for (FVector Axis : AxesA) if (Separated(Axis)) return false;
 for (FVector Axis : AxesB) if (Separated(Axis)) return false;
 for (FVector X : AxesA) for (FVector Y : AxesB) if (Separated(FVector::CrossProduct(X,Y))) return false;
 return true;
}
}

bool AWarehouseForklift::IsSensorSamplingSuppressed() const
{
 if (SensorMode==EWarehouseSensorMode::Disabled || !bPowered || IsRemoteControlled() ||
     AIState==EWarehouseAIState::ElevatorRide) return true;
 for (TActorIterator<AWarehouseLayoutEditor> It(GetWorld());It;++It) if (It->bEditing) return true;
 return false;
}

void AWarehouseForklift::InvalidateSensorSnapshot(const FString& Reason)
{
 bObservedTargetValid=false;
 bHasRecognizedTargetPose=false;
 bObservedPickupEscape=false;
 if (SensorKit) SensorKit->InvalidateSnapshot(Reason);
 if (SensorMode!=EWarehouseSensorMode::Autonomous) return;
 bMovementObstacleClear=bLiftObstacleClear=bRouteObstacleClear=bTurnObstacleClear=false;
 MovementObstacleSnapshot=LiftObstacleSnapshot=RouteObstacleSnapshot=TurnObstacleSnapshot=INDEX_NONE;
 MovementObstacleReason=LiftObstacleReason=TEXT("SENSOR SNAPSHOT REQUIRED");
}

bool AWarehouseForklift::AutonomousSensorsReady() const
{
 return SensorMode!=EWarehouseSensorMode::Autonomous ||
  (SensorKit && !IsSensorSamplingSuppressed() && SensorKit->IsSnapshotFresh(GetWorld()->GetTimeSeconds()));
}

bool AWarehouseForklift::ConsumeObstacleSnapshot(int32& LastConsumedSnapshot)
{
 if (SensorMode!=EWarehouseSensorMode::Autonomous) return RefreshObstacleChecks();
 if (!SensorKit || !AutonomousSensorsReady() || LastConsumedSnapshot==SensorKit->SnapshotCount) return false;
 // Sampling and consuming are different events. A state may first need its
 // route/turn/lift prediction several frames after the shared scan was taken.
 LastConsumedSnapshot=SensorKit->SnapshotCount;
 return true;
}

void AWarehouseForklift::UpdateSensorSnapshot()
{
 const bool Suppressed=IsSensorSamplingSuppressed();
 const bool Changed=LastSensorMode!=SensorMode;
 if (Changed || Suppressed!=bSensorSuppressed)
 {
  // A manual session, layout edit, floor transfer or mode change invalidates
  // old clear results. A fresh ordinary one-second frame must precede motion.
  InvalidateSensorSnapshot(Suppressed ? TEXT("SENSOR SAMPLING SUSPENDED") : TEXT("SENSOR REACQUISITION"));
  LastSensorMode=SensorMode;bSensorSuppressed=Suppressed;
 }
 if (Changed && SensorMode!=EWarehouseSensorMode::Autonomous)
 {
  PlannedRoute.Reset();RouteIndex=0;bRouteStarted=false;
  BrakeDrive();DesiredLift=LiftOffset;LiftMotorTarget=LiftOffset;
  if (FleetCoordinator) FleetCoordinator->Release(this);
 }
 if (Suppressed) return;
 if (!SensorKit)
 {
  SensorKit=FindComponentByClass<UAgvShadowSensorsComponent>();
  if (!SensorKit)
  {
   SensorKit=NewObject<UAgvShadowSensorsComponent>(this,TEXT("AgvShadowSensors"));
   SensorKit->RegisterComponent();
  }
 }
 if (!RefreshObstacleChecks()) return;
 for (auto It=WorkOrderSourcePoses.CreateIterator();It;++It)
  if (It.Key()!=ActiveJob.JobId && !PendingJobs.ContainsByPredicate([&](const FWarehouseWorkOrder& Job) { return Job.JobId==It.Key(); })) It.RemoveCurrent();
 bObservedTargetValid=false;
 if (!SensorKit->RefreshSnapshot(GetWorld()->GetTimeSeconds(),FloorBase(GetActorLocation().Z))) return;
 // Existing close-range pallet-model recognition remains a dedicated sensor:
 // range, FOV, actual visibility and the common scan must all confirm it.
 // Its model-fit pose is cached only on this frame, never polled while hidden.
 if (!IsValid(TargetPallet) || !bPalletSensorHealthy || !SensorKit->HasObservedActor(TargetPallet)) return;
 const FVector Delta=TargetPallet->GetActorLocation()-GetActorLocation();
 if (Delta.Size()>PalletDetectionRangeCm || FVector::DotProduct(Delta.GetSafeNormal2D(),GetActorForwardVector())<.5f) return;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(PalletModelRecognition),true,this);
 const FVector Origin=GetActorLocation()+GetActorForwardVector()*10+FVector(0,0,90);
 for (float Y : {-40.f,0.f,40.f})
 {
  const FVector Point=TargetPallet->GetActorTransform().TransformPosition(FVector(-53,Y,8));
  FHitResult Hit;
  if (GetWorld()->LineTraceSingleByChannel(Hit,Origin,Point,ECC_Visibility,Params) && Hit.GetActor()==TargetPallet)
  { bObservedTargetValid=true;break; }
 }
 if (!bObservedTargetValid) return;
 SensorObservedTarget=TargetPallet->GetActorTransform();
 SensorObservedTarget.AddToTranslation(PalletPositionBiasCm);
 FRotator Heading=SensorObservedTarget.Rotator();Heading.Yaw+=PalletYawBiasDegrees;
 SensorObservedTarget.SetRotation(Heading.Quaternion());
 bHasRecognizedTargetPose=true;
 if (!bSupportingPallet && ActiveJob.Pallet==TargetPallet && !ActiveJob.JobId.IsEmpty())
  WorkOrderSourcePoses.Add(ActiveJob.JobId,SensorObservedTarget);
}

FTransform AWarehouseForklift::TaskSourcePose() const
{
 if (SensorMode==EWarehouseSensorMode::Autonomous)
 {
  if (const auto* Pose=WorkOrderSourcePoses.Find(ActiveJob.JobId)) return *Pose;
  return FTransform::Identity;
 }
 return IsValid(TargetPallet) ? TargetPallet->GetActorTransform() : FTransform::Identity;
}

bool AWarehouseForklift::GetPerceivedActorBounds(const AActor* Actor,FBox& Bounds) const
{
 Bounds=FBox(ForceInit);
 if (!Actor || !SensorKit || !AutonomousSensorsReady()) return false;
 auto Collect=[&](const TArray<FAgvObservedObstacle>& Obstacles)
 {
  for (const auto& Obstacle : Obstacles) if (Obstacle.Actor.Get()==Actor) Bounds+=Obstacle.WorldBounds;
 };
 Collect(SensorKit->GetKnownObstacles());Collect(SensorKit->GetObservedObstacles());
 return Bounds.IsValid!=0;
}

bool AWarehouseForklift::PerceivedBoxClear(const FTransform& Pose,const FBox& LocalBox,
 const TArray<AActor*>& Ignored,bool PeopleOnly) const
{
 if (!SensorKit || !AutonomousSensorsReady()) return false;
 auto Ignore=[&](const FAgvObservedObstacle& Obstacle)
 {
  return Obstacle.Actor.Get()==this || Ignored.Contains(Obstacle.Actor.Get()) ||
   (PeopleOnly && !Obstacle.bPerson);
 };
 auto LogBlock=[&](const FAgvObservedObstacle& Obstacle,const TCHAR* Kind,FVector MeasuredPoint)
 {
  if (bPlanningRoute) return;
  UE_LOG(LogTemp,Log,TEXT("AGV_PERCEIVED_BOX_BLOCK %s kind=%s actor=%s component=%s candidate=%s box=%s savedPoint=%s savedBounds=%s mapRevision=%d snapshot=%d taskSource=%s observedTargetValid=%d observedTarget=%s routeGoal=%s"),
   *VehicleName,Kind,*GetNameSafe(Obstacle.Actor.Get()),*GetNameSafe(Obstacle.Component.Get()),*Pose.ToString(),*LocalBox.ToString(),
   *MeasuredPoint.ToString(),*Obstacle.WorldBounds.ToString(),SensorKit->MapRevision,SensorKit->SnapshotCount,*TaskSourcePose().ToString(),
   bObservedTargetValid,bObservedTargetValid ? *SensorObservedTarget.ToString() : TEXT("NONE"),
   PlannedRoute.IsEmpty() ? TEXT("NONE") : *PlannedRoute.Last().Pose.ToString());
 };
 if (!PeopleOnly && !bSupportingPallet && bHasRecognizedTargetPose && IsValid(TargetPallet) && !Ignored.Contains(TargetPallet))
 {
  // A positively recognized standard pallet supplies catalog dimensions, not
  // hidden engine bounds. Its pose remains the last sensor model fit while
  // occluded. Precision insertion still uses real fork/hole contact geometry.
  const FBox Catalog(FVector(-55,-55,0),FVector(55,55,16));
  if (CatalogBoxesOverlap(Pose,LocalBox,SensorObservedTarget,Catalog))
  {
   FAgvObservedObstacle Model;Model.Actor=TargetPallet.Get();Model.WorldBounds=Catalog.TransformBy(SensorObservedTarget.ToMatrixWithScale());
   LogBlock(Model,TEXT("recognized-target-catalog"),FVector::ZeroVector);
   return false;
  }
 }
 const FBox Broad=LocalBox.TransformBy(Pose.ToMatrixWithScale());
 if (!PeopleOnly) for (const auto& Obstacle : SensorKit->GetKnownObstacles())
 {
  if (Ignore(Obstacle) || !Obstacle.WorldBounds.Intersect(Broad)) continue;
  if (Obstacle.Component.IsValid())
  {
   const FTransform Test=AtPublishedPose(FTransform(Pose.GetRotation(),Pose.TransformPosition(LocalBox.GetCenter())),Obstacle);
   if (Obstacle.Component->OverlapComponent(Test.GetLocation(),Test.GetRotation(),FCollisionShape::MakeBox(LocalBox.GetExtent())))
   {
    LogBlock(Obstacle,TEXT("published-building"),FVector::ZeroVector);
    return false;
   }
  }
  else return false; // Published geometry stays conservative until the next revision.
 }
 for (const auto& Obstacle : SensorKit->GetObservedObstacles())
 {
  FVector Point;
  if (!Ignore(Obstacle) && PointCloudOverlaps(Obstacle,Pose,LocalBox,&Point))
  {
   LogBlock(Obstacle,TEXT("observed-point"),Point);
   return false;
  }
 }
 return true;
}

bool AWarehouseForklift::PerceivedPartClear(UStaticMeshComponent* Part,const FTransform& Pose,
 const TArray<AActor*>& Ignored,const UPrimitiveComponent* Bearing) const
{
 if (!Part || !Part->GetStaticMesh() || !SensorKit || !AutonomousSensorsReady()) return false;
 auto Reject=[&](const FAgvObservedObstacle& Obstacle,const TCHAR* Kind,FVector Point)
 {
  if (FParse::Param(FCommandLine::Get(),TEXT("DriveDiagnostics")))
   UE_LOG(LogTemp,Log,TEXT("AGV_PERCEIVED_POSE_BLOCK %s kind=%s part=%s actor=%s component=%s candidate=%s savedPoint=%s savedBounds=%s snapshot=%d planning=%d"),
    *VehicleName,Kind,*Part->GetName(),*GetNameSafe(Obstacle.Actor.Get()),*GetNameSafe(Obstacle.Component.Get()),
    *Pose.ToString(),*Point.ToString(),*Obstacle.WorldBounds.ToString(),SensorKit->SnapshotCount,bPlanningRoute);
  return false;
 };
 const auto* Setup=Part->GetPhysicsBodySetup();
 const FBox Local=Setup ? Setup->AggGeom.CalcAABB(FTransform::Identity) : Part->GetStaticMesh()->GetBoundingBox();
 FBox Broad=Local.TransformBy(Pose.ToMatrixWithScale());
 if (!Part->BodyInstance.WeldParent && Part->BodyInstance.IsValidBodyInstance())
  Broad=Part->BodyInstance.GetBodyBounds().TransformBy(Part->GetComponentTransform().ToInverseMatrixWithScale()*Pose.ToMatrixWithScale());
 if (!Part->BodyInstance.WeldParent) for (const auto& Obstacle : SensorKit->GetKnownObstacles())
 {
  if (Obstacle.Component.Get()==Bearing || Obstacle.Actor.Get()==this || Ignored.Contains(Obstacle.Actor.Get()) || !Obstacle.WorldBounds.Intersect(Broad)) continue;
  if (!Obstacle.Component.IsValid()) return Reject(Obstacle,TEXT("published-building-missing"),FVector::ZeroVector);
  const FTransform Test=AtPublishedPose(Pose,Obstacle);
  if (Obstacle.Component->ComponentOverlapComponent(Part,Test.GetLocation(),Test.GetRotation(),FCollisionQueryParams::DefaultQueryParam))
   return Reject(Obstacle,TEXT("published-building"),FVector::ZeroVector);
 }
 for (const auto& Obstacle : SensorKit->GetObservedObstacles())
 {
  FVector Point=FVector::ZeroVector;
  if (Obstacle.Component.Get()!=Bearing && Obstacle.Actor.Get()!=this && !Ignored.Contains(Obstacle.Actor.Get()) && PointCloudOverlapsPart(Obstacle,Part,Pose,&Point))
   return Reject(Obstacle,TEXT("observed-point"),Point);
 }
 return true;
}

bool AWarehouseForklift::PerceivedMotionClear(UStaticMeshComponent* Part,FVector Delta,
 const TArray<AActor*>& Ignored) const
{
 if (!Part || !SensorKit || !AutonomousSensorsReady()) return false;
 // At the one-second decision boundary sweep the cached measured surfaces.
 // Actual contact continues at every Chaos substep between these predictions.
 const auto* Shape=Part->GetPhysicsBodySetup();
 if (!Shape) return false;
 const FTransform Start=Part->GetComponentTransform();
 FTransform End=Start;End.AddToTranslation(Delta);
 const FBox Swept=(Shape->AggGeom.CalcAABB(Start)+Shape->AggGeom.CalcAABB(End)).ExpandBy(2.);
 const int32 Steps=FMath::Clamp(FMath::CeilToInt(Delta.Size()/2.f),1,320);
 auto Reject=[&](const FAgvObservedObstacle& Obstacle,FVector Point,const TCHAR* Reason,float InitialDistance,float Distance,const FTransform& Candidate)
 {
  UE_LOG(LogTemp,Log,TEXT("AGV_PERCEIVED_PART_BLOCK %s part=%s actor=%s component=%s savedPoint=%s start=%s end=%s candidate=%s initialDistance=%.4f distance=%.4f reason=%s snapshot=%d"),
   *VehicleName,*GetNameSafe(Part),*GetNameSafe(Obstacle.Actor.Get()),*GetNameSafe(Obstacle.Component.Get()),*Point.ToString(),
   *Start.ToString(),*End.ToString(),*Candidate.ToString(),InitialDistance,Distance,Reason,SensorKit->SnapshotCount);
  return false;
 };
 for (const auto& Obstacle : SensorKit->GetObservedObstacles())
 {
  if (Obstacle.Actor.Get()==this || Ignored.Contains(Obstacle.Actor.Get()) || !Obstacle.WorldBounds.Intersect(Swept)) continue;
  for (const FVector& Point : Obstacle.WorldPoints) if (Swept.IsInsideOrOn(Point))
  {
   const float InitialDistance=Shape->GetShortestDistanceToPoint(Point,Start,true);
   float LastDistance=InitialDistance;
   const bool InitiallyOverlapping=LastDistance<=2.f;
   if (InitiallyOverlapping && !SeparatingFromMeasuredPoint(Shape,Point,Start,Delta))
    return Reject(Obstacle,Point,TEXT("INITIAL_POINT_NOT_SEPARATING"),InitialDistance,Shape->GetShortestDistanceToPoint(Point,End,true),End);
   bool Cleared=!InitiallyOverlapping;
   for (int32 I=1;I<=Steps;++I)
   {
    FTransform Pose=Start;Pose.AddToTranslation(Delta*(float(I)/Steps));
    const float Distance=Shape->GetShortestDistanceToPoint(Point,Pose,true);
    // Separation is scoped to this exact initial measured point. It cannot
    // exempt a new point/obstacle, deepen contact, or permit re-entry later.
    if (Distance<=2.f && (Cleared || Distance<LastDistance-.05f))
     return Reject(Obstacle,Point,!InitiallyOverlapping ? TEXT("NEW_MEASURED_POINT") :
      Cleared ? TEXT("REENTERED_INITIAL_POINT") : TEXT("DEEPENING_INITIAL_POINT"),InitialDistance,Distance,Pose);
    if (Distance>2.f) Cleared=true;
    LastDistance=Distance;
   }
  }
 }
 return true;
}

bool AWarehouseForklift::NavigationClearForDiagnostics(const FTransform& Pose) const
{
 if (IsRemoteControlled()) return true;
 return NavigationClear(Pose);
}

bool AWarehouseForklift::PerceivedFloorSupport(FVector Point,FHitResult* Surface) const
{
 if (!SensorKit || !AutonomousSensorsReady()) return false;
 const FVector Start=Point+FVector(0,0,3),End=Point-FVector(0,0,5);
 FCollisionQueryParams Params(SCENE_QUERY_STAT(PublishedFloorSupport),false,this);
 for (const auto& Obstacle : SensorKit->GetKnownObstacles())
 {
  if (!Obstacle.Component.IsValid() || !Obstacle.WorldBounds.ExpandBy(5.).IsInsideOrOn(Point)) continue;
  const FTransform Live=Obstacle.Component->GetComponentTransform();
  const auto Remap=[&](FVector P) { return Live.TransformPosition(Obstacle.ObservedTransform.InverseTransformPosition(P)); };
  FHitResult Hit;
  if (!Obstacle.Component->LineTraceComponent(Hit,Remap(Start),Remap(End),Params)) continue;
  Hit.ImpactPoint=Obstacle.ObservedTransform.TransformPosition(Live.InverseTransformPosition(Hit.ImpactPoint));
  Hit.ImpactNormal=Obstacle.ObservedTransform.TransformVectorNoScale(Live.InverseTransformVectorNoScale(Hit.ImpactNormal));
  if (Hit.ImpactNormal.Z<.98f) continue;
  if (Surface) *Surface=Hit;
  return true;
 }
 FHitResult Deck;
 if (IsValid(Elevator) && Elevator->TraceCertifiedPlatformSupport(const_cast<AWarehouseForklift*>(this),Point,Deck))
 {
  if (Surface) *Surface=Deck;
  return true;
 }
 if (!bPlanningRoute) UE_LOG(LogTemp,Log,TEXT("AGV_PERCEIVED_FLOOR_UNSUPPORTED %s candidate=%s mapRevision=%d snapshot=%d floor=%.3f"),
  *VehicleName,*Point.ToString(),SensorKit->MapRevision,SensorKit->SnapshotCount,SensorKit->SnapshotFloorZCm);
 return false;
}
