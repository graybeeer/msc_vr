#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseChargingStation.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "CollisionQueryParams.h"
#include "UObject/ConstructorHelpers.h"

void AWarehouseForklift::RebuildEditedMesh(UStaticMesh* Mesh)
{
#if WITH_EDITOR
 if (Mesh) { Mesh->CommitMeshDescription(0); Mesh->PostEditChange(); Mesh->MarkPackageDirty(); }
#endif
}
void AWarehouseForklift::ConfigureForkCollision(UStaticMesh* Mesh)
{
#if WITH_EDITOR
 if (!Mesh || !Mesh->GetBodySetup()) return;
 auto* Setup=Mesh->GetBodySetup();
 Setup->AggGeom.EmptyElements();
 const FBox Bounds=Mesh->GetBoundingBox();
 FKBoxElem Blade; Blade.Center=FVector(57.5,Bounds.GetCenter().Y,6.5); Blade.X=115; Blade.Y=18; Blade.Z=6;
 Setup->AggGeom.BoxElems.Add(Blade);
 FKBoxElem Heel; Heel.Center=FVector(Bounds.Min.X*.5,Bounds.GetCenter().Y,(3.5+Bounds.Max.Z)*.5);
 Heel.X=-Bounds.Min.X; Heel.Y=18; Heel.Z=Bounds.Max.Z-3.5;
 Setup->AggGeom.BoxElems.Add(Heel);
 Setup->CollisionTraceFlag=CTF_UseSimpleAndComplex;
 Setup->InvalidatePhysicsData(); Setup->CreatePhysicsMeshes(); Mesh->MarkPackageDirty();
#endif
}
AWarehouseForklift::AWarehouseForklift()
{
 PrimaryActorTick.bCanEverTick = true;
 PrimaryActorTick.TickGroup=TG_PrePhysics;
 Tags.Add(TEXT("WarehousePhysicalVehicle"));
 RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
 Carriage = CreateDefaultSubobject<USceneComponent>(TEXT("CarriagePivot"));
 Carriage->SetupAttachment(RootComponent);
 // Refined FBX geometry is baked at real size; no nonuniform chassis scaling.
 auto Part = [&](const TCHAR* Name, USceneComponent* Parent, FVector Pivot=FVector::ZeroVector)
 {
  auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(Name);
  Mesh->SetupAttachment(Parent);
  const FString Path=FString::Printf(TEXT("/Game/Warehouse/AGV/Meshes/SM_Refined_AGV_%s"),Name);
  ConstructorHelpers::FObjectFinder<UStaticMesh> Asset(*Path);
  Mesh->SetStaticMesh(Asset.Object);
  Mesh->SetRelativeLocation(Pivot);
  Mesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
  return Mesh;
 };
 constexpr double Scale=215./282.55;
 Part(TEXT("Body"),RootComponent);
 Part(TEXT("DriveSteer"),RootComponent);
 // Seat the carrier behind the fork heels so it cannot enter the pallet's rear board.
 Part(TEXT("Carriage"),Carriage,FVector(-10,0,0));
 Part(TEXT("ForkL"),Carriage);
 Part(TEXT("ForkR"),Carriage);
 LiftStage=Part(TEXT("LiftStage"),RootComponent);
 Part(TEXT("LiftRam"),LiftStage);
 Part(TEXT("LiftPulley"),LiftStage);
 LiftChains=Part(TEXT("LiftChains"),RootComponent,FVector(0,0,3.5+15.1*Scale));
 LiftChains->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 Wheels.Add(Part(TEXT("DriveWheel"),RootComponent,FVector(-74*Scale,0,20.5*Scale)));
 Wheels.Add(Part(TEXT("LoadWheelL"),RootComponent,FVector(-7*Scale,49.7-6.15*Scale,10.5*Scale)));
 Wheels.Add(Part(TEXT("LoadWheelR"),RootComponent,FVector(-7*Scale,-49.7+6.15*Scale,10.5*Scale)));
 Beacon = CreateDefaultSubobject<UPointLightComponent>(TEXT("Beacon"));
 Beacon->SetupAttachment(RootComponent);
 Beacon->SetRelativeLocation(FVector(-25,0,213));
 Beacon->SetLightColor(FLinearColor(.03f,.35f,1.f));
 Beacon->SetIntensity(0);
 Beacon->SetAttenuationRadius(250);
 Display = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Status"));
 Display->SetupAttachment(RootComponent);
 Display->SetRelativeLocation(FVector(-86*Scale,0,158.5*Scale));
 Display->SetRelativeRotation(FRotator(0,180,0));
 Display->SetHorizontalAlignment(EHTA_Center);
 Display->SetWorldSize(.8f);
 Display->SetText(FText::FromString(Status));
}

void AWarehouseForklift::SetStatus(const FString& Message)
{
 Status = Message;
 RefreshDisplay();
 Beacon->SetIntensity(bPowered ? 1800 : 0);
}
void AWarehouseForklift::StopFor(const FString& Reason)
{
 if (IsRemoteControlled())
 {
  RemoteForward=RemoteSteering=RemoteLift=0; BrakeDrive();
  SetStatus(TEXT("REMOTE STOP: ")+Reason); return;
 }
 if (bAutonomousMode)
 {
  if (Reason.Contains(TEXT("OBSTACLE")) || Reason.Contains(TEXT("PERSON")))
  {
   WaitResumeAI=AIState;
   TransitionAI(EWarehouseAIState::WaitingObstacle,Reason);
  }
  else FaultAI(Reason);
  return;
 }
 bPowered=false;
 BrakeDrive();
 if (IsValid(ChargingStation)) ChargingStation->ShowCharge(BatteryPercent,false);
 SetStatus(Reason + TEXT("\nE : RETRY / RESUME"));
}
void AWarehouseForklift::SetMechanicalFailure()
{
 bMechanicalFailure=true;
 bChargePending=false;
 if (IsValid(ChargingStation)) ChargingStation->Release(this);
 StopFor(TEXT("MECHANICAL FAILURE - REPAIR REQUIRED"));
 SetStatus(TEXT("MECHANICAL FAILURE - REPAIR REQUIRED"));
 Beacon->SetLightColor(FLinearColor::Red);
 Beacon->SetIntensity(1800);
}
void AWarehouseForklift::TogglePower()
{
 if (IsRemoteControlled()) return;
 if (bReplanAfterRemote)
 {
  if (bSupportingPallet && (ActiveJob.JobId.IsEmpty() || TargetPallet!=ActiveJob.Pallet))
  { SetStatus(TEXT("MANUAL LOAD - UNLOAD BEFORE AUTO / G: REMOTE")); return; }
  if (!ActiveJob.JobId.IsEmpty())
  {
   TargetPallet=ActiveJob.Pallet;
   ResumeAI=bSupportingPallet ? EWarehouseAIState::PlanDelivery : EWarehouseAIState::PlanPickup;
   PlannedRoute.Reset(); bRouteStarted=false; bPalletReleased=false;
  }
  else ResumeAI=EWarehouseAIState::Ready;
  AIState=EWarehouseAIState::Paused; bReplanAfterRemote=false;
 }
 if (bAutonomousMode) { ToggleAutonomy(); return; }
 if (bMechanicalFailure) { SetMechanicalFailure(); return; }
 if (bPowered) { StopFor(TEXT("PAUSED")); return; }
 if (BatteryPercent<=0 && !(State==EWarehouseCycle::Charging && IsValid(ChargingStation) && ChargingStation->IsDocked(this))) { StopFor(TEXT("BATTERY EMPTY - RECOVERY REQUIRED")); return; }
 if ((State==EWarehouseCycle::Idle || State==EWarehouseCycle::Complete) && (BatteryPercent<=FMath::Clamp(ChargeBelowPercent,0.f,70.f) || bChargePending)) { BeginChargeTrip(); return; }
 if (!IsValid(TargetPallet) && State<=EWarehouseCycle::Complete) { StopFor(TEXT("NO TARGET PALLET")); return; }
 if (State==EWarehouseCycle::Complete) { SetStatus(TEXT("JOB COMPLETE - ASSIGN NEXT PALLET")); return; }
 if (State == EWarehouseCycle::Idle) { StartLocation=GetActorLocation(); State=EWarehouseCycle::Approach; }
 bPowered=true;
 SetStatus(TEXT("RUNNING - KEEP CLEAR\nE : PAUSE"));
}
void AWarehouseForklift::RefreshDisplay()
{
 LastDisplayedBattery=BatteryPercent;
 Display->SetText(FText::FromString(FString::Printf(TEXT("%s\nBAT %.1f%%  |  %.0f / 1400 kg%s"),*Status,BatteryPercent,GetLoadMassKg(),bChargePending ? TEXT("\nCHARGE QUEUED") : TEXT(""))));
}
float AWarehouseForklift::GetBatteryEnergyWh() const
{
 return FMath::Max(1.f,BatteryVoltage)*FMath::Max(1.f,BatteryCapacityAh)*FMath::Clamp(BatteryPercent,0.f,100.f)/100.f;
}
float AWarehouseForklift::GetLoadMassKg() const
{
 if (!IsValid(TargetPallet)) return 0.f;
 float Payload=FMath::Max(0.f,TargetPallet->PayloadMassKg);
 if (auto* Strength=AWarehouseDamageSystem::Find(this)) Payload=FMath::Max(Payload,Strength->GetSupportedMass(TargetPallet));
 return Payload+FMath::Max(0.f,TargetPallet->PalletMassKg);
}
TArray<AActor*> AWarehouseForklift::GetPhysicalLoads() const
{
 TArray<AActor*> Loads;
 if (!bSupportingPallet || !IsValid(TargetPallet)) return Loads;
 Loads.Add(TargetPallet);
 for (AActor* Cargo : CarriedCargo) if (IsValid(Cargo)) Loads.Add(Cargo);
 return Loads;
}
bool AWarehouseForklift::PalletOnForks() const
{
 if (!IsValid(TargetPallet)) return false;
 const FVector Local=Carriage->GetComponentTransform().InverseTransformPosition(TargetPallet->GetActorLocation());
 return Local.X>=35 && Local.X<=85 && FMath::Abs(Local.Y)<=25 &&
  FMath::Abs(Local.Z+PalletContactLiftCm)<=5 &&
  FVector::DotProduct(GetActorUpVector(),TargetPallet->GetActorUpVector())>.94f &&
  FVector::DotProduct(GetActorForwardVector(),TargetPallet->GetActorForwardVector())>.94f;
}
void AWarehouseForklift::ConsumeEnergy(float Wh)
{
 Wh=FMath::Max(0.f,Wh)*FMath::Clamp(BatteryTimeScale,1.f,3600.f);
 EnergyConsumedWh+=FMath::Min(Wh,GetBatteryEnergyWh());
 BatteryPercent=FMath::Clamp(BatteryPercent-100.f*Wh/(FMath::Max(1.f,BatteryVoltage)*FMath::Max(1.f,BatteryCapacityAh)),0.f,100.f);
 if (BatteryPercent<=FMath::Clamp(ChargeBelowPercent,0.f,70.f)) bChargePending=true;
}
void AWarehouseForklift::EndPlay(const EEndPlayReason::Type Reason)
{
 if (IsRemoteControlled()) EndRemoteControl(RemoteOperator);
 if (IsValid(ChargingStation)) ChargingStation->Release(this);
 Super::EndPlay(Reason);
}
void AWarehouseForklift::RequestCharging(bool ToFull)
{
 bChargePending=true;
 bChargeToFull=ToFull;
 if (IsRemoteControlled()) { RefreshDisplay(); return; }
 if (bAutonomousMode)
 {
  if (AIState==EWarehouseAIState::Off) ToggleAutonomy();
  RefreshDisplay(); return;
 }
 if (State==EWarehouseCycle::Idle || State==EWarehouseCycle::Complete) BeginChargeTrip();
 else RefreshDisplay();
}
bool AWarehouseForklift::BeginChargeTrip()
{
 if (bMechanicalFailure) { SetMechanicalFailure(); return false; }
 if (!IsValid(ChargingStation)) { StopFor(TEXT("NO CHARGING STATION")); return false; }
 if (bSupportingPallet || LiftOffset>.01f) { StopFor(TEXT("UNLOAD BEFORE CHARGING")); return false; }
 // ponytail: reserved straight charging aisle; use a route planner for other layouts.
 const FVector Offset=ChargingStation->GetActorLocation()-GetActorLocation();
 if (FMath::Abs(FVector::DotProduct(Offset,GetActorRightVector()))>1.f || FMath::Abs(Offset.Z)>1.f ||
     FVector::DotProduct(GetActorForwardVector(),ChargingStation->GetActorForwardVector())<.99996f)
 { StopFor(TEXT("CHARGER ROUTE MISALIGNED")); return false; }
 if (!ChargingStation->TryReserve(this)) { StopFor(TEXT("CHARGER BUSY / OFFLINE")); return false; }
 ReturnLocation=GetActorLocation();
 ResumeState=State;
 State=ChargingStation->IsDocked(this) ? EWarehouseCycle::Charging : EWarehouseCycle::ToCharger;
 bPowered=true;
 BrakeDrive();
 SetStatus(TEXT("TO CHARGER"));
 return true;
}
void AWarehouseForklift::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if (bPhysicsReady)
 {
  CurrentSpeedCm=FVector::DotProduct(ChassisBody->GetPhysicsLinearVelocity(),GetActorForwardVector());
  LiftOffset=GetActorTransform().InverseTransformPosition(Carriage->GetComponentLocation()).Z;
 }
 BrakeDrive();
 AdvanceSimulation(DeltaSeconds);
 UpdatePhysicalRig(FMath::Clamp(DeltaSeconds,0.f,.133333f));
}
bool AWarehouseForklift::ClearToMove(FVector Delta, bool LiftOnly)
{
 // Safety envelope includes both the chassis and exposed forks. Pawn includes players and human NPCs.
 FHitResult PersonHit;
 FCollisionObjectQueryParams People(ECC_Pawn); People.AddObjectTypesToQuery(ECC_PhysicsBody);
 FCollisionQueryParams SafetyParams(SCENE_QUERY_STAT(PersonSafety),false,this);
 const FVector SafetyCenter=GetActorLocation()+GetActorForwardVector()*30+FVector(0,0,110);
 const float LookAhead=FMath::Max(100.f,CurrentSpeedCm*CurrentSpeedCm/100.f+FMath::Abs(CurrentSpeedCm)*.2f+20.f);
 const FVector SafetyEnd=SafetyCenter+(LiftOnly ? FVector::ZeroVector : Delta+Delta.GetSafeNormal()*LookAhead);
 TArray<FHitResult> PeopleHits;
 GetWorld()->SweepMultiByObjectType(PeopleHits,SafetyCenter,SafetyEnd,GetActorQuat(),People,FCollisionShape::MakeBox(FVector(110,75,110)),SafetyParams);
 if (PeopleHits.ContainsByPredicate([](const FHitResult& Hit) { return Hit.GetActor() && (Hit.GetActor()->ActorHasTag(TEXT("WarehousePhysicalHuman")) || Hit.GetComponent()->GetCollisionObjectType()==ECC_Pawn); }))
 {
  StopFor(TEXT("PERSON IN SAFETY ZONE"));
  return false;
 }
 TArray<UStaticMeshComponent*> Parts;
 GetComponents(Parts);
 // A dynamic load settles against the floor independently of the actuator.
 // Sweeping it as a rigid extension of the lift falsely blocks normal lowering.
 if (!LiftOnly && bSupportingPallet && IsValid(TargetPallet))
 {
  TArray<UStaticMeshComponent*> LoadParts;
  TargetPallet->GetComponents(LoadParts);
  Parts.Append(LoadParts);
 }
 for (AActor* Load : CarriedCargo) if (!LiftOnly && bSupportingPallet && IsValid(Load))
 {
  TArray<UStaticMeshComponent*> LoadParts; Load->GetComponents(LoadParts); Parts.Append(LoadParts);
 }
 FComponentQueryParams Params(SCENE_QUERY_STAT(ForkliftSweep),this);
 for (AActor* Load : CarriedCargo) if (bSupportingPallet && IsValid(Load)) Params.AddIgnoredActor(Load);
 if (bSupportingPallet) Params.AddIgnoredActor(TargetPallet);
 for (auto* Part : Parts)
 {
  if (Part->GetCollisionEnabled()==ECollisionEnabled::NoCollision) continue;
  // A welded member shares its parent's complete Chaos shape set. Sweeping
  // that set again from the child's origin invents an offset obstacle.
  if (Part->BodyInstance.WeldParent) continue;
  FVector PartDelta=Delta;
  if (LiftOnly && Part->GetOwner()==this)
  {
   if (Part==LiftStage || Part->IsAttachedTo(LiftStage)) PartDelta*=.5f;
   else if (Part!=CarriageBody && !Part->IsAttachedTo(CarriageBody)) continue;
  }
  TArray<FHitResult> Hits;
  GetWorld()->ComponentSweepMulti(Hits,Part,Part->GetComponentLocation(),Part->GetComponentLocation()+PartDelta,Part->GetComponentQuat(),Params);
  for (const FHitResult& Hit : Hits)
  {
   if (Hit.bBlockingHit && (FVector::DotProduct(Delta,Hit.Normal)<-.001f || (Hit.bStartPenetrating && Hit.PenetrationDepth>.2f)))
   {
    UE_LOG(LogTemp,Verbose,TEXT("Forklift contact %s -> %s, lift %.3f, support %.3f, penetration %.3f, normal %s"),*Part->GetName(),*GetNameSafe(Hit.GetActor()),LiftOffset,PalletContactLiftCm,Hit.PenetrationDepth,*Hit.Normal.ToString());
    // Prediction only commands a brake. Damage is generated by actual Chaos contact impulses.
    StopFor(TEXT("OBSTACLE / CONTACT"));
    return false;
   }
  }
 }
 return true;
}
bool AWarehouseForklift::MoveVehicle(FVector Delta)
{
 return CommandDrive(FVector::DotProduct(Delta,GetActorForwardVector())/FMath::Max(.008f,GetWorld()->GetDeltaSeconds()),0,GetWorld()->GetDeltaSeconds());
}
bool AWarehouseForklift::MoveLift(float Height)
{
 if (Height<0 || Height>FMath::Clamp(MaxForkHeightCm,10.f,160.f)-9.5f+.001f) { StopFor(TEXT("LIFT HEIGHT LIMIT")); return false; }
 if (!ClearToMove(FVector(0,0,Height-LiftOffset),true)) return false;
 DesiredLift=Height;
 return true;
}
bool AWarehouseForklift::MoveToLine(FVector Destination,float Speed,float Dt)
{
 const FVector Offset=Destination-GetActorLocation();
 if (FMath::Abs(FVector::DotProduct(Offset,GetActorRightVector()))>5 || FMath::Abs(Offset.Z)>3)
 { StopFor(TEXT("ROUTE MISALIGNED")); return false; }
 const float Distance=FVector::DotProduct(Offset,GetActorForwardVector());
 if (FMath::Abs(Distance)<.5f) { BrakeDrive(); return FMath::Abs(CurrentSpeedCm)<1.f; }
 Speed=FMath::Min(Speed,bSupportingPallet ? FMath::Min(100.f,LoadedTravelSpeedCm) : FMath::Min(180.f,EmptyTravelSpeedCm));
 const float TargetSpeed=FMath::Sign(Distance)*FMath::Min(Speed,FMath::Sqrt(240.f*FMath::Abs(Distance)));
 const float Lateral=FVector::DotProduct(Offset,GetActorRightVector());
 const float Curvature=FMath::Clamp(2.f*Lateral/FMath::Max(1600.f,Offset.SizeSquared2D()),-1.f/117.3f,1.f/117.3f);
 CommandDrive(FMath::Sign(Distance)*FMath::Min(FMath::Abs(TargetSpeed),FMath::Abs(Distance)*2.f),Curvature,Dt);
 return false;
}
void AWarehouseForklift::AdvanceSimulation(float DeltaSeconds)
{
 if (FMath::Abs(BatteryPercent-LastDisplayedBattery)>=.1f) RefreshDisplay();
 if (RemoteOperator)
 {
  if (!IsValid(RemoteOperator)) { RemoteOperator=nullptr; bPowered=false; BrakeDrive(); }
  else { AdvanceRemote(FMath::Clamp(DeltaSeconds,0.f,.133333f)); return; }
 }
 if (bAutonomousMode) { AdvanceAutonomy(FMath::Clamp(DeltaSeconds,0.f,.133333f)); return; }
 if (bMechanicalFailure || !bPowered) return;
 const float Dt=FMath::Clamp(DeltaSeconds,0.f,.133333f);
 if (Dt<=0) return;
 if (State==EWarehouseCycle::Charging)
 {
  if (!IsValid(ChargingStation) || !ChargingStation->bMainsPower || ChargingStation->PowerKw<=0 || ChargingStation->Efficiency<=0 || ChargingStation->Occupant!=this || !ChargingStation->IsDocked(this))
  { StopFor(TEXT("CHARGING CONTACT / POWER LOST")); return; }
  if (!ClearToMove(FVector::ZeroVector,true)) return;
  const float Target=bChargeToFull ? 100.f : FMath::Clamp(ResumeAtPercent,75.f,100.f);
  BatteryPercent=FMath::Min(Target,BatteryPercent+100.f*FMath::Max(0.f,ChargingStation->PowerKw)*1000.f*FMath::Clamp(ChargingStation->Efficiency,0.f,1.f)*Dt*FMath::Clamp(BatteryTimeScale,1.f,3600.f)/(3600.f*FMath::Max(1.f,BatteryVoltage)*FMath::Max(1.f,BatteryCapacityAh)));
  ChargingStation->ShowCharge(BatteryPercent,true);
  if (BatteryPercent>=Target) { bChargePending=false; bChargeToFull=false; State=EWarehouseCycle::Returning; SetStatus(TEXT("CHARGE COMPLETE - RETURNING")); ChargingStation->ShowCharge(BatteryPercent,false); }
  return;
 }
 if (BatteryPercent<=0) { StopFor(TEXT("BATTERY EMPTY - RECOVERY REQUIRED")); return; }
 ConsumeEnergy(100.f*Dt/3600.f);
 if (BatteryPercent<=0) { StopFor(TEXT("BATTERY EMPTY - RECOVERY REQUIRED")); return; }
 if (State==EWarehouseCycle::ToCharger || State==EWarehouseCycle::Docking || State==EWarehouseCycle::Returning)
 {
  if (!IsValid(ChargingStation)) { StopFor(TEXT("CHARGER LOST")); return; }
  if (State!=EWarehouseCycle::Returning && (!ChargingStation->bMainsPower || ChargingStation->Occupant!=this)) { StopFor(TEXT("CHARGER BUSY / OFFLINE")); return; }
  const FVector Dock=ChargingStation->GetActorLocation();
  const FVector Approach=Dock+ChargingStation->GetActorForwardVector()*120.f;
  if (State==EWarehouseCycle::ToCharger && MoveToLine(Approach,EmptyTravelSpeedCm,Dt)) { State=EWarehouseCycle::Docking; SetStatus(TEXT("DOCKING")); }
  else if (State==EWarehouseCycle::Docking && MoveToLine(Dock,10.f,Dt))
  {
   if (!ChargingStation->IsDocked(this)) { StopFor(TEXT("DOCK ALIGNMENT FAILED")); return; }
   State=EWarehouseCycle::Charging; SetStatus(TEXT("CHARGING"));
  }
  else if (State==EWarehouseCycle::Returning && MoveToLine(ReturnLocation,ForkLeadingSpeedCm,Dt))
  {
   ChargingStation->Release(this);
   State=ResumeState;
   if (State==EWarehouseCycle::Idle) { StartLocation=GetActorLocation(); State=EWarehouseCycle::Approach; SetStatus(TEXT("WORK RESUMED")); }
   else { bPowered=false; SetStatus(TEXT("JOB COMPLETE - READY")); }
  }
  return;
 }
 if (!IsValid(TargetPallet)) { StopFor(TEXT("TARGET LOST")); return; }
 if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->HasFailed(TargetPallet)) { StopFor(TEXT("PALLET DAMAGED - REPLACE")); return; }
 if (GetLoadMassKg()>FMath::Min(1400.f,RatedLoadKg)) { StopFor(TEXT("OVERLOAD - REMOVE LOAD")); return; }
 if (TaskForkHeightCm<12 || TaskForkHeightCm>FMath::Min(160.f,MaxForkHeightCm)) { StopFor(TEXT("TASK EXCEEDS LIFT LIMIT")); return; }
 const FVector Forward=GetActorForwardVector();
 switch(State)
 {
 case EWarehouseCycle::Approach:
 {
  const FVector Offset=TargetPallet->GetActorLocation()-GetActorLocation();
  if (FMath::Abs(FVector::DotProduct(Offset,GetActorRightVector()))>5 ||
      FVector::DotProduct(Forward,TargetPallet->GetActorForwardVector())<FMath::Cos(FMath::DegreesToRadians(2.f)))
  { StopFor(TEXT("ALIGN PALLET")); break; }
  const float Remaining=FVector::DotProduct(Offset,Forward)-65;
  if (Remaining < -1) { StopFor(TEXT("TARGET TOO CLOSE")); break; }
  if (Remaining>.5f) { MoveToLine(TargetPallet->GetActorLocation()-Forward*65,ForkLeadingSpeedCm,Dt); break; }
  BrakeDrive();
  if (!TargetPallet->CanEngage(Carriage->GetComponentTransform())) { StopFor(TEXT("FORK INSERTION FAILED")); break; }
  PalletContactLiftCm=TargetPallet->GetSupportLiftOffset(Carriage->GetComponentTransform());
  if (PalletContactLiftCm<0) { StopFor(TEXT("NO PALLET SUPPORT")); break; }
  State=EWarehouseCycle::Lift;
  SetStatus(TEXT("LIFTING"));
  break;
 }
 case EWarehouseCycle::Lift:
  if (!bSupportingPallet)
  {
   // Both tines must be fully inserted before the underside contact is accepted.
   if (!TargetPallet->CanEngage(Carriage->GetComponentTransform())) { StopFor(TEXT("FORK INSERTION FAILED")); break; }
   TrackCargo(); bSupportingPallet=true;
  }
  if (MoveLift(TaskForkHeightCm-9.5f) && FMath::Abs(LiftOffset-(TaskForkHeightCm-9.5f))<.4f) { State=EWarehouseCycle::TravelLower; SetStatus(TEXT("LOWERING TO TRAVEL HEIGHT")); }
  break;
 case EWarehouseCycle::TravelLower:
  if (MoveLift(FMath::Min(TaskForkHeightCm,20.f)-9.5f) && LiftOffset<=10.9f) { State=EWarehouseCycle::Reverse; SetStatus(TEXT("REVERSING")); }
  break;
 case EWarehouseCycle::Reverse:
 {
  if (MoveToLine(StartLocation,LoadedTravelSpeedCm,Dt)) { State=EWarehouseCycle::Lower; SetStatus(TEXT("LOWERING")); }
  break;
 }
 case EWarehouseCycle::Lower:
  if (bSupportingPallet)
  {
   if (!MoveLift(PalletContactLiftCm)) break;
   if (LiftOffset<=PalletContactLiftCm+.4f) { bSupportingPallet=false; }
  }
  else if (MoveLift(0.f) && FMath::Abs(LiftOffset)<.4f)
  { State=EWarehouseCycle::Withdraw; WithdrawStart=GetActorLocation(); SetStatus(TEXT("WITHDRAWING")); }
  break;
 case EWarehouseCycle::Withdraw:
 {
  if (MoveToLine(WithdrawStart-Forward*130,30.f,Dt))
  {
   State=EWarehouseCycle::Complete;
   if (bChargePending) BeginChargeTrip();
   else { bPowered=false; SetStatus(TEXT("CYCLE COMPLETE")); }
  }
  break;
 }
 default: break;
 }
}
