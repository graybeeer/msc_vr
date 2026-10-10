#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseChargingStation.h"
#include "WarehouseElevator.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"

void AWarehouseForklift::BeginPlay()
{
 Super::BeginPlay();
 InitializePhysicalRig();
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
 if (!IsValid(Operator) || FVector::Dist(Operator->GetActorLocation(),GetActorLocation())>350) return false;
 if (IsRemoteControlled()) return RemoteOperator==Operator;
 if (!CheckSystems().IsEmpty()) { SetStatus(TEXT("REMOTE UNAVAILABLE: ")+CheckSystems()); return false; }
 if (bFloorTransfer || (IsValid(Elevator) && Elevator->ReservedVehicle==this))
 { SetStatus(TEXT("FINISH ELEVATOR TRANSFER BEFORE REMOTE")); return false; }
 if (bAutonomousMode)
 {
  if (AIState!=EWarehouseAIState::SelfCheck || !bResumeAfterCheck) ResumeAI=AIState;
  TransitionAI(EWarehouseAIState::Paused,TEXT("AUTONOMY PAUSED FOR SMARTPHONE REMOTE"));
 }
 else ResumeState=State;
 if (IsValid(ChargingStation)) ChargingStation->Release(this);
 RemoteOperator=Operator; bPowered=true; bReplanAfterRemote=true; bRemoteLoadFault=false;
 RemoteForward=RemoteSteering=RemoteLift=0; BrakeDrive(); RemoteCheckSeconds=0;
 LastRemoteInputTime=GetWorld()->GetRealTimeSeconds();
 SetStatus(TEXT("REMOTE SELF CHECK / G: DISCONNECT"));
 return true;
}

void AWarehouseForklift::EndRemoteControl(AActor* Operator)
{
 if (!RemoteOperator || RemoteOperator!=Operator) return;
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
 if (!RefreshObstacleChecks()) return bTurnObstacleClear;
 bTurnObstacleClear=PhysicalPoseClear(Pose);
 return bTurnObstacleClear;
}
bool AWarehouseForklift::PhysicalPoseClear(const FTransform& Pose) const
{
 FCollisionQueryParams FloorParams(SCENE_QUERY_STAT(RemoteFloor),false,this);
 FloorParams.AddIgnoredActors(GetPhysicalLoads());
 FHitResult Ground;
 const FVector P=Pose.GetLocation();
 if (!GetWorld()->LineTraceSingleByChannel(Ground,P+FVector(0,0,3),P-FVector(0,0,5),ECC_Visibility,FloorParams) ||
     Ground.ImpactNormal.Z<.98f) return false;
 // Linear movement uses ClearToMove's swept contact test. A snapshot overlap
 // cannot distinguish bearing contact from a jam on a continuously dynamic load.
 if (Pose.GetRotation().Equals(GetActorQuat(),1.e-5f)) return true;
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
  if (!Part->IsQueryCollisionEnabled() || Part->BodyInstance.WeldParent) continue;
  const FTransform Candidate=Part->GetComponentTransform().GetRelativeTransform(GetActorTransform())*Pose;
  TArray<FOverlapResult> Hits;
  GetWorld()->ComponentOverlapMultiByChannel(Hits,Part,Candidate.GetLocation(),Candidate.GetRotation(),Part->GetCollisionObjectType(),Params);
  for (const auto& Hit : Hits) if (Hit.bBlockingHit)
  {
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
 const FString Fault=CheckSystems();
 if (!Fault.IsEmpty()) { StopFor(Fault); return; }
 if (bRemoteLoadFault) { StopFor(TEXT("LOAD LOST - G: DISCONNECT AND RECHECK")); return; }
 if (GetWorld()->GetRealTimeSeconds()-LastRemoteInputTime>.25)
 { StopFor(TEXT("REMOTE INPUT TIMEOUT")); return; }
 const bool WasChecking=RemoteCheckSeconds<1.f;
 RemoteCheckSeconds+=Dt;
 if (RemoteCheckSeconds<1.f) { BrakeDrive(); return; }
 if (WasChecking) SetStatus(TEXT("SMARTPHONE REMOTE READY / G: DISCONNECT"));
 if (!UpdateLoadSupport(Dt))
 {
  bSupportingPallet=false; CarriedCargo.Reset(); bRemoteLoadFault=true;
  StopFor(TEXT("LOAD SLIPPED / SUPPORT LOST")); return;
 }
 if (bSupportingPallet && GetLoadMassKg()>FMath::Min(1400.f,RatedLoadKg))
 { StopFor(TEXT("OVERLOAD - REMOVE LOAD")); return; }
 if (bSupportingPallet)
  if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->HasFailed(TargetPallet))
  { StopFor(TEXT("PALLET DAMAGED")); return; }
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
    if (GetLoadMassKg()>FMath::Min(1400.f,RatedLoadKg)) { StopFor(TEXT("OVERLOAD - REMOVE LOAD")); return; }
    // Reserve only after both full tines pass the hole/angle checks. This
    // exempts expected bearing contact from the predictive brake, while the
    // independent pallet and forks retain all actual Chaos contacts.
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
  // Keep loads low during travel; lifting and driving are separate remote actions.
  if (LiftOffset>20) { StopFor(TEXT("LOWER FORKS TO TRAVEL HEIGHT")); return; }
  if (FMath::IsNearlyZero(RemoteForward))
  {
   if (!CommandTurn(RemoteSteering*FMath::DegreesToRadians(bSupportingPallet ? 20.f : 30.f),Dt)) return;
  }
  else
  {
   const float Limit=bSupportingPallet ? FMath::Min(100.f,LoadedTravelSpeedCm) : FMath::Min(180.f,EmptyTravelSpeedCm);
   const float Distance=RemoteForward*Limit*Dt;
   const float Angle=FMath::RadiansToDegrees(Distance*RemoteSteering/FMath::Max(117.3f,MinimumTurningRadiusCm));
   FRotator Heading=GetActorRotation(); Heading.Yaw+=Angle;
   if (!RemotePoseClear(FTransform(Heading,GetActorLocation()+GetActorForwardVector()*Distance)))
   { StopFor(TEXT("OBSTACLE / TURN CLEARANCE")); return; }
   if (!CommandDrive(RemoteForward*Limit,RemoteSteering/FMath::Max(117.3f,MinimumTurningRadiusCm),Dt)) return;
  }
 }
 else BrakeDrive();
 if (!FMath::IsNearlyZero(RemoteForward) || !FMath::IsNearlyZero(RemoteSteering) || !FMath::IsNearlyZero(RemoteLift))
  SetStatus(TEXT("SMARTPHONE REMOTE / G: DISCONNECT"));
}
