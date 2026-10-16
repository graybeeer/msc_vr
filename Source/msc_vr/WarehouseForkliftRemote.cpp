#include "WarehouseForklift.h"
#include "WarehousePhysics.h"
#include "WarehouseFleetCoordinator.h"
#include "WarehousePallet.h"
#include "WarehouseChargingStation.h"
#include "WarehouseElevator.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
// A tyre can bear on two adjacent slabs while the chassis floor trace sees
// only one. Prove the other contact using its native upward surface; dynamic
// objects, raised obstacles and deeper penetrations remain blocking hits.
bool WheelBearingOnCoplanarFloor(UWorld* World,const AActor* Vehicle,UStaticMeshComponent* Wheel,
 UPrimitiveComponent* Surface,const FTransform& Candidate,const TArray<AActor*>& Loads)
{
 if (!WarehousePhysics::IsPhysicalContact(Wheel) || !WarehousePhysics::IsPhysicalContact(Surface) || Surface->IsSimulatingPhysics() || !Surface->IsQueryCollisionEnabled() ||
     Surface->GetCollisionResponseToChannel(ECC_Visibility)!=ECR_Block) return false;
 const auto* Body=Wheel->GetBodyInstance();
 if (!Body || !Body->IsValidBodyInstance()) return false;
 // Use the current cooked Chaos shape bounds. Rotating the asset's coarse
 // convex element box can overestimate a circular tyre's vertical extent.
 const FBox Native=Body->GetBodyBounds();
 if (!Native.IsValid) return false;
 const FBox Bounds=Native.TransformBy(Wheel->GetComponentTransform().ToInverseMatrixWithScale()*Candidate.ToMatrixWithScale());
 if (!Bounds.IsValid) return false;
 FCollisionQueryParams TyreQuery(SCENE_QUERY_STAT(TurningTyreTread),false);
 FHitResult Tread;
 const FVector CurrentCenter=Native.GetCenter();
 if (!Wheel->LineTraceComponent(Tread,FVector(CurrentCenter.X,CurrentCenter.Y,Native.Min.Z-5.f),CurrentCenter,TyreQuery) ||
     Tread.ImpactNormal.Z>=-.98f) return false;
 const FVector Contact=Candidate.TransformPosition(Wheel->GetComponentTransform().InverseTransformPosition(Tread.ImpactPoint));
 FCollisionQueryParams Query(SCENE_QUERY_STAT(TurningTyreSupport),false,Vehicle);
 Query.AddIgnoredActors(Loads);
 const FVector Start=Bounds.GetCenter(),End(Start.X,Start.Y,Bounds.Min.Z-5.f);
 FHitResult Below;
 if (!World->LineTraceSingleByChannel(Below,Start,End,ECC_Visibility,Query) ||
     !Below.GetComponent() || Below.GetComponent()->IsSimulatingPhysics() || Below.ImpactNormal.Z<=.98f ||
     FMath::Abs(Contact.Z-Below.ImpactPoint.Z)>.5f) return false;
 FHitResult Bearing;
 if (Surface->LineTraceComponent(Bearing,Start,End,Query))
  return Bearing.ImpactNormal.Z>.98f && FMath::Abs(Bearing.ImpactPoint.Z-Below.ImpactPoint.Z)<.5f;
 // A centre outside the neighbouring slab still permits tread at the seam.
 // Probe at its closest bounds point, only 1mm inside, then require the native
 // surface itself and the candidate tyre bounds to confirm that contact.
 FVector Point=Surface->Bounds.GetBox().GetClosestPointTo(Start);
 Point+=(Surface->Bounds.Origin-Point).GetSafeNormal2D()*.1f;
 if (Point.X<Bounds.Min.X-.1f || Point.X>Bounds.Max.X+.1f ||
     Point.Y<Bounds.Min.Y-.1f || Point.Y>Bounds.Max.Y+.1f) return false;
 Point.Z=Below.ImpactPoint.Z;
 return Surface->LineTraceComponent(Bearing,Point+FVector(0,0,2),Point-FVector(0,0,2),Query) &&
  Bearing.ImpactNormal.Z>.98f && FMath::Abs(Bearing.ImpactPoint.Z-Below.ImpactPoint.Z)<.5f;
}
}

bool AWarehouseForklift::WheelBearingOnFloor(UStaticMeshComponent* Wheel,UPrimitiveComponent* Surface,
 const FTransform& Candidate,const TArray<AActor*>& Ignored) const
{
 return WheelBearingOnCoplanarFloor(GetWorld(),this,Wheel,Surface,Candidate,Ignored);
}

void AWarehouseForklift::BeginPlay()
{
 Super::BeginPlay();
 InitializePhysicalRig();
 FleetCoordinator=AWarehouseFleetCoordinator::Find(this);
 if (bAutoStart)
 {
  FTimerHandle Handle;
  GetWorldTimerManager().SetTimer(Handle,FTimerDelegate::CreateWeakLambda(this,[this]
  {
   if (!bPowered && !IsRemoteControlled() && !bReplanAfterRemote &&
       (bAutonomousMode ? AIState==EWarehouseAIState::Off : State==EWarehouseCycle::Idle)) TogglePower();
  }),1.f,false);
 }
}

bool AWarehouseForklift::PalletClaimedByOther(AWarehousePallet* Pallet) const
{
 if (!IsValid(Pallet)) return false;
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) if (*It!=this)
 {
  if ((!It->ActiveJob.JobId.IsEmpty() && It->ActiveJob.Pallet==Pallet) ||
      (It->bSupportingPallet && It->TargetPallet==Pallet)) return true;
  for (const auto& Job : It->PendingJobs) if (Job.Pallet==Pallet) return true;
 }
 return false;
}

bool AWarehouseForklift::BeginRemoteControl(AActor* Operator)
{
 if (!IsValid(Operator) || Operator==this) return false;
 FVector Center,Extent;GetActorBounds(true,Center,Extent);
 // The character selects the visible vehicle outline, not its origin behind
 // the forks. Use the same physical extent for the remote proximity guard.
 if (FVector::Dist(Operator->GetActorLocation(),FBox(Center-Extent,Center+Extent).GetClosestPointTo(Operator->GetActorLocation()))>350) return false;
 if (IsRemoteControlled()) return RemoteOperator==Operator;
 const FString Fault=CheckSystems(false);
 if (!Fault.IsEmpty()) { SetStatus(TEXT("REMOTE UNAVAILABLE: ")+Fault); return false; }
 if (bFloorTransfer || (IsValid(Elevator) && Elevator->ReservedVehicle==this))
 { SetStatus(TEXT("FINISH ELEVATOR TRANSFER BEFORE REMOTE")); return false; }
 if (bAutonomousMode)
 {
  if (AIState!=EWarehouseAIState::SelfCheck || !bResumeAfterCheck) ResumeAI=AIState;
  TransitionAI(EWarehouseAIState::Paused,TEXT("AUTONOMY PAUSED FOR SMARTPHONE REMOTE"));
 }
 else ResumeState=State;
 if (IsValid(ChargingStation)) ChargingStation->Release(this);
 RemoteOperator=Operator; bPowered=true; bReplanAfterRemote=true;
 // Poll the operator before consuming its heartbeat, including a slow frame.
 AddTickPrerequisiteActor(Operator);
 RemoteForward=RemoteSteering=RemoteLift=0; BrakeDrive();
 LastRemoteInputTime=GetWorld()->GetRealTimeSeconds();
 SetStatus(TEXT("SMARTPHONE REMOTE READY / G: DISCONNECT"));
 return true;
}

void AWarehouseForklift::EndRemoteControl(AActor* Operator)
{
 if (!RemoteOperator || RemoteOperator!=Operator) return;
 RemoveTickPrerequisiteActor(Operator);
 RemoteForward=RemoteSteering=RemoteLift=0; BrakeDrive(); bPowered=false;
 RemoteOperator=nullptr;
 if (bAutonomousMode) TransitionAI(EWarehouseAIState::Paused,TEXT("REMOTE ENDED / E: SELF CHECK AND REPLAN"));
 else SetStatus(TEXT("REMOTE ENDED / E: SELF CHECK / G: REMOTE"));
}

void AWarehouseForklift::SetRemoteInput(float Forward,float Steering,float Lift)
{
 if (!IsRemoteControlled()) return;
 if (!FMath::IsFinite(Forward) || !FMath::IsFinite(Steering) || !FMath::IsFinite(Lift))
 { StopFor(TEXT("INVALID REMOTE INPUT")); return; }
 RemoteForward=FMath::Clamp(Forward,-1.f,1.f);
 RemoteSteering=FMath::Clamp(Steering,-1.f,1.f);
 RemoteLift=FMath::Clamp(Lift,-1.f,1.f);
 LastRemoteInputTime=GetWorld()->GetRealTimeSeconds();
 // Releasing the drive button engages the brake; payload inertia stays physical.
 if (FMath::IsNearlyZero(RemoteForward) && FMath::IsNearlyZero(RemoteSteering)) BrakeDrive();
}

bool AWarehouseForklift::UpdateLoadSupport(float Dt)
{
 if (!bSupportingPallet) { LostSupportSeconds=0; return true; }
 if (!IsValid(TargetPallet)) return false;
 bool Lost=!PalletOnForks();
 for (AActor* Cargo : CarriedCargo)
 {
  if (!IsValid(Cargo)) { Lost=true; continue; }
  FVector Center,Extent; Cargo->GetActorBounds(false,Center,Extent);
  const FVector Local=TargetPallet->GetActorTransform().InverseTransformPosition(Center);
  if (FMath::Abs(Local.X)>65 || FMath::Abs(Local.Y)>65 || Local.Z<10) Lost=true;
 }
 LostSupportSeconds=Lost ? LostSupportSeconds+Dt : 0.f;
 return LostSupportSeconds<=.5f;
}

bool AWarehouseForklift::RemotePoseClear(const FTransform& Pose)
{
 if (IsRemoteControlled()) return true;
 if (!RefreshObstacleChecks()) return bTurnObstacleClear;
 bTurnObstacleClear=PhysicalPoseClear(Pose);
 return bTurnObstacleClear;
}
bool AWarehouseForklift::PhysicalPoseClear(const FTransform& Pose,bool CheckTranslation) const
{
 FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(RemoteFloor),false,this);
 FloorParams.AddIgnoredActors(GetPhysicalLoads());
 FHitResult Ground;
 const FVector P=Pose.GetLocation();
 if (!GetWorld()->LineTraceSingleByChannel(Ground,P+FVector(0,0,3),P-FVector(0,0,5),ECC_Visibility,FloorParams) ||
     Ground.ImpactNormal.Z<.98f) return false;
 // Linear movement uses ClearToMove's swept contact test. A snapshot overlap
 // cannot distinguish bearing contact from a jam on a continuously dynamic load.
 if (!CheckTranslation && Pose.GetRotation().Equals(GetActorQuat(),1.e-5f)) return true;
 FComponentQueryParams Params(SCENE_QUERY_STAT(RemoteTurn),this);
 Params.AddIgnoredComponent(Ground.GetComponent());
 const TArray<AActor*> Loads=GetPhysicalLoads();
 Params.AddIgnoredActors(Loads);
 TArray<UStaticMeshComponent*> Parts; GetComponents(Parts);
 for (AActor* Load : Loads) if (IsValid(Load))
 {
  TArray<UStaticMeshComponent*> LoadParts;Load->GetComponents(LoadParts);Parts.Append(LoadParts);
 }
 for (auto* Part : Parts)
 {
  if (!WarehousePhysics::IsPhysicalContact(Part) || !Part->IsQueryCollisionEnabled() || Part->BodyInstance.WeldParent) continue;
  const FTransform Candidate=Part->GetComponentTransform().GetRelativeTransform(GetActorTransform())*Pose;
  TArray<FOverlapResult> Hits;
  GetWorld()->ComponentOverlapMultiByChannel(Hits,Part,Candidate.GetLocation(),Candidate.GetRotation(),Part->GetCollisionObjectType(),Params);
  for (const auto& Hit : Hits) if (Hit.bBlockingHit)
  {
   if (Wheels.Contains(Part) && WheelBearingOnCoplanarFloor(GetWorld(),this,
       Part,Hit.GetComponent(),Candidate,Loads)) continue;
   if (FParse::Param(FCommandLine::Get(),TEXT("DriveDiagnostics")))
    UE_LOG(LogTemp,Log,TEXT("AGV_TURN_OVERLAP_REJECT %s part=%s surface=%s/%s candidate=%s"),
     *VehicleName,*Part->GetName(),*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*Candidate.GetLocation().ToString());
   // Forecast truck and supported loads together; native loose-load contacts
   // still determine whether the pallet follows, jams, slips or falls.
   return false;
  }
 }
 return true;
}

void AWarehouseForklift::AdvanceRemote(float Dt)
{
 if (Dt<=0) return;
 const FString Fault=CheckSystems(false);
 if (!Fault.IsEmpty()) { StopFor(Fault); return; }
 if (GetWorld()->GetRealTimeSeconds()-LastRemoteInputTime>.25)
 { StopFor(TEXT("REMOTE INPUT TIMEOUT")); return; }
 if (!UpdateLoadSupport(Dt))
 {
  // Lost physical support changes load telemetry, not the operator's command.
  bSupportingPallet=false; CarriedCargo.Reset();
 }
 ConsumeEnergy(100.f*Dt/3600.f);
 if (BatteryPercent<=0) { StopFor(TEXT("BATTERY EMPTY")); return; }

 if (!FMath::IsNearlyZero(RemoteLift))
 {
  const float Next=FMath::Clamp(DesiredLift+RemoteLift*(RemoteLift>0 ? 11.5f : 16.f)*Dt,0.f,FMath::Max(0.f,FMath::Min(160.f,MaxForkHeightCm)-9.5f));
  if (RemoteLift>0 && !bSupportingPallet)
  {
   for (TActorIterator<AWarehousePallet> It(GetWorld());It;++It)
   {
    if (PalletClaimedByOther(*It) || !It->CanEngage(Carriage->GetComponentTransform())) continue;
    const float Contact=It->GetSupportLiftOffset(Carriage->GetComponentTransform());
    if (Contact<0) continue;
    const float Gap=Contact-Carriage->GetComponentTransform().InverseTransformPosition(It->GetActorLocation()).Z;
    TargetPallet=*It;
    // Track support only after both full tines pass the hole/angle checks.
    // The independent pallet and forks retain all actual Chaos contacts.
    PalletContactLiftCm=Gap; TrackCargo(); bSupportingPallet=true; break;
   }
  }
  if (!MoveLift(Next)) return;
  if (RemoteLift<0 && bSupportingPallet)
  {
   FCollisionQueryParams SupportParams(SCENE_QUERY_STAT(RemoteUnload),false,this);
   SupportParams.AddIgnoredActors(GetPhysicalLoads());
   FHitResult Floor;
   const FVector Base=TargetPallet->GetActorLocation();
   if (GetWorld()->LineTraceSingleByChannel(Floor,Base+FVector(0,0,1),Base-FVector(0,0,2),ECC_Visibility,SupportParams) && Floor.ImpactNormal.Z>.98f)
    bSupportingPallet=false;
  }
 }
 if (!FMath::IsNearlyZero(RemoteForward) || !FMath::IsNearlyZero(RemoteSteering))
 {
  if (FMath::IsNearlyZero(RemoteForward))
  {
   if (!CommandTurn(RemoteSteering*FMath::DegreesToRadians(bSupportingPallet ? 20.f : 30.f),Dt)) return;
  }
  else
  {
   const float Limit=bSupportingPallet ? FMath::Min(100.f,LoadedTravelSpeedCm) : FMath::Min(180.f,EmptyTravelSpeedCm);
   if (!CommandDrive(RemoteForward*Limit,RemoteSteering/FMath::Max(117.3f,MinimumTurningRadiusCm),Dt)) return;
  }
 }
 else BrakeDrive();
 if (!FMath::IsNearlyZero(RemoteForward) || !FMath::IsNearlyZero(RemoteSteering) || !FMath::IsNearlyZero(RemoteLift))
  SetStatus(TEXT("SMARTPHONE REMOTE / G: DISCONNECT"));
}
