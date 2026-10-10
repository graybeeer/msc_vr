#include "WarehouseInboundZone.h"
#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehouseDamageSystem.h"
#include "msc_vrCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"

AWarehouseInboundZone::AWarehouseInboundZone()
{
 PrimaryActorTick.bCanEverTick=true;
 PrimaryActorTick.TickInterval=1.f;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Zone")));
}
void AWarehouseInboundZone::Tick(float Dt)
{
 Super::Tick(Dt);
 ScanInbound();
}
bool AWarehouseInboundZone::SlotClear(int32 Index,AWarehousePallet* Pallet,const TArray<AActor*>& Loads,float Height,float Mass) const
{
 const auto& Slot=RackSlots[Index];
 auto* Strength=AWarehouseDamageSystem::Find(this);
 if (!IsValid(Slot.Rack) || !Strength || Strength->HasFailed(Slot.Rack) || Height>Slot.ClearHeightCm || Mass>Slot.CapacityKg) return false;
 for (const auto& Entry : Reservations)
  if (Entry.Slot==Index || (RackSlots.IsValidIndex(Entry.Slot) && RackSlots[Entry.Slot].Rack==Slot.Rack)) return false;
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
 {
  const FVector Approach=Slot.Pose.GetLocation()-Slot.Pose.GetUnitAxis(EAxis::X)*220;
  if (FVector::Dist2D(It->GetActorLocation(),Approach)<100 &&
      FMath::Abs(It->GetActorLocation().Z-GetActorLocation().Z)<5) return false;
  auto Occupies=[&Slot](const FWarehouseWorkOrder& Job)
  { return !Job.JobId.IsEmpty() && FVector::Dist2D(Job.Destination.GetLocation(),Slot.Pose.GetLocation())<110 &&
      FMath::Abs(Job.Destination.GetLocation().Z-Slot.Pose.GetLocation().Z)<5; };
  if (Occupies(It->ActiveJob)) return false;
  for (const auto& Job : It->PendingJobs) if (Occupies(Job)) return false;
 }
 // Shared rack level capacity, including the other pallet and in-flight reservations.
 float Used=0;
 for (TActorIterator<AWarehousePallet> It(GetWorld());It;++It)
  if (*It!=Pallet && FVector::Dist2D(It->GetActorLocation(),Slot.Pose.GetLocation())<150 &&
      FMath::Abs(It->GetActorLocation().Z-Slot.Pose.GetLocation().Z)<5)
   Used+=It->PalletMassKg+Strength->GetSupportedMass(*It);
 for (const auto& Entry : Reservations)
  if (Entry.Slot!=Index && RackSlots.IsValidIndex(Entry.Slot) &&
      RackSlots[Entry.Slot].Rack==Slot.Rack &&
      FMath::Abs(RackSlots[Entry.Slot].Pose.GetLocation().Z-Slot.Pose.GetLocation().Z)<5)
   if (auto* Load=Entry.Pallet.Get()) Used+=Load->PalletMassKg+Strength->GetSupportedMass(Load);
 if (Used+Mass>Slot.CapacityKg) return false;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(InboundRackSlot),false,this);
 Params.AddIgnoredActor(Pallet);
 for (auto* Load : Loads) Params.AddIgnoredActor(Load);
 const FVector Point=Slot.Pose.GetLocation();
 if (GetWorld()->OverlapBlockingTestByChannel(Point+FVector(0,0,Height*.5f+2),Slot.Pose.GetRotation(),
     ECC_Visibility,FCollisionShape::MakeBox(FVector(54,54,FMath::Max(1.f,Height*.5f-1))),Params)) return false;
 // The centre can be a hole in real wooden pallets/shelves: require four supporting corners.
 for (float X : {-40.f,40.f}) for (float Y : {-40.f,40.f})
 {
  const FVector Probe=Point+Slot.Pose.GetRotation().RotateVector(FVector(X,Y,0));
  FHitResult Hit;
  if (!GetWorld()->LineTraceSingleByChannel(Hit,Probe+FVector(0,0,1),Probe-FVector(0,0,4),ECC_Visibility,Params) ||
      Hit.ImpactNormal.Z<.98f) return false;
 }
 return true;
}
void AWarehouseInboundZone::ScanInbound()
{
 auto* Strength=AWarehouseDamageSystem::Find(this);
 if (!Strength) { Status=TEXT("입고: 하중 검사 시스템 없음"); return; }
 for (int32 I=Reservations.Num()-1;I>=0;--I)
 {
  const auto& Entry=Reservations[I];
  auto* Vehicle=Entry.Vehicle.Get();
  if (!Vehicle || !Entry.Pallet.IsValid()) { Reservations.RemoveAt(I); continue; }
  if (Vehicle->CompletedJobIds.Contains(Entry.JobId))
  { ++CompletedCount; Reservations.RemoveAt(I); }
  // A paused/faulted job retains its destination. E/recovery can safely resume it.
 }
 const double Now=GetWorld()->GetTimeSeconds();
 TSet<TWeakObjectPtr<AWarehousePallet>> Present;
 int32 Loaded=0;
 for (TActorIterator<AWarehousePallet> It(GetWorld());It;++It)
 {
  auto* Pallet=*It;
  const FVector Local=GetActorTransform().InverseTransformPosition(Pallet->GetActorLocation());
  if (FMath::Abs(Local.X)>HalfSizeCm.X-55 || FMath::Abs(Local.Y)>HalfSizeCm.Y-55 ||
      FMath::Abs(Local.Z)>5 || Strength->HasFailed(Pallet)) continue;
  Present.Add(Pallet);
  const TArray<AActor*> Loads=Strength->GetSupportedActors(Pallet);
  bool bCargo=false,bHeld=false,bStable=Pallet->GetPalletBody()->GetPhysicsLinearVelocity().Size()<10 &&
   Pallet->GetPalletBody()->GetPhysicsAngularVelocityInDegrees().Size()<5 &&
   FMath::Abs(Pallet->GetActorRotation().Pitch)<2 && FMath::Abs(Pallet->GetActorRotation().Roll)<2;
  FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(Pallet);
  for (auto* Load : Loads) if (Cast<AWarehouseCargo>(Load))
  {
   bCargo=true; Bounds+=AWarehouseDamageSystem::GetPhysicalBounds(Load);
   auto* Part=Cast<UPrimitiveComponent>(Load->GetRootComponent());
   bStable &= Part && Part->GetPhysicsLinearVelocity().Size()<10 && !Strength->HasFailed(Load);
  }
  for (TActorIterator<Amsc_vrCharacter> Player(GetWorld());Player;++Player)
   bHeld |= Player->GetHeldCargo()==Pallet || Loads.Contains(Player->GetHeldCargo());
  if (!bCargo || bHeld || !bStable)
  { StableSince.Remove(Pallet); StablePose.Remove(Pallet); continue; }
  ++Loaded;
  // The marked inbound approach is one shared handoff lane. Clear it before
  // dispatching the next pallet; the two vehicles remain available for other jobs.
  if (!Reservations.IsEmpty()) continue;
  const auto* Previous=StablePose.Find(Pallet);
  if (!Previous || FVector::Dist(Pallet->GetActorLocation(),Previous->GetLocation())>2.f ||
      Pallet->GetActorQuat().AngularDistance(Previous->GetRotation())>FMath::DegreesToRadians(2.f))
  { StablePose.Add(Pallet,Pallet->GetActorTransform()); StableSince.Add(Pallet,Now); }
  // Contact jitter is evaluated separately from cumulative movement; exact transforms are too strict.
  if (Now-StableSince.FindChecked(Pallet)<2.f) continue;
  bool bClaimed=false;
  for (TActorIterator<AWarehouseForklift> Vehicle(GetWorld());Vehicle;++Vehicle)
  {
   bClaimed |= Vehicle->ActiveJob.Pallet==Pallet;
   for (const auto& Job : Vehicle->PendingJobs) bClaimed |= Job.Pallet==Pallet;
  }
  if (bClaimed) continue;
  const float Mass=Pallet->PalletMassKg+Strength->GetSupportedMass(Pallet);
  const float Height=Bounds.Max.Z-Bounds.Min.Z;
  AWarehouseForklift* BestVehicle=nullptr;
  int32 BestSlot=INDEX_NONE; float BestCost=MAX_flt;
  for (const auto& VehiclePtr : Fleet)
  {
   auto* Vehicle=VehiclePtr.Get();
   if (!IsValid(Vehicle) || !Vehicle->bAutonomousMode || !Vehicle->bPowered || Vehicle->IsRemoteControlled() ||
       Vehicle->AIState!=EWarehouseAIState::Ready || !Vehicle->PendingJobs.IsEmpty() || !Vehicle->ActiveJob.JobId.IsEmpty() ||
       Vehicle->bChargePending || Vehicle->BatteryPercent<=Vehicle->ChargeBelowPercent || Mass>Vehicle->RatedLoadKg) continue;
   for (int32 Slot=0;Slot<RackSlots.Num();++Slot)
   {
    if (RackSlots[Slot].Pose.GetLocation().Z-GetActorLocation().Z+20>Vehicle->MaxForkHeightCm || !SlotClear(Slot,Pallet,Loads,Height,Mass)) continue;
    const FVector Pickup=Pallet->GetActorLocation()-Pallet->GetActorForwardVector()*220;
    const float Cost=FVector::Dist2D(Vehicle->GetActorLocation(),Pickup)+
       FVector::Dist2D(Pallet->GetActorLocation(),RackSlots[Slot].Pose.GetLocation())+RackSlots[Slot].Pose.GetLocation().Z;
    if (Cost<BestCost) { BestCost=Cost; BestSlot=Slot; BestVehicle=Vehicle; }
   }
  }
  if (!BestVehicle) continue;
  FWarehouseWorkOrder Job;
  Job.JobId=FString::Printf(TEXT("INBOUND-%s-%06d"),*GetName(),DispatchedCount+1);
  Job.SourceSystem=TEXT("FMS-INBOUND-ZONE-1"); Job.Pallet=Pallet; Job.Destination=RackSlots[BestSlot].Pose;
  if (BestVehicle->SubmitJob(Job))
  {
   FReservation Entry; Entry.Vehicle=BestVehicle; Entry.Pallet=Pallet; Entry.JobId=Job.JobId; Entry.Slot=BestSlot;
   Reservations.Add(Entry); ++DispatchedCount;
   UE_LOG(LogTemp,Log,TEXT("INBOUND_DISPATCH %s pallet=%s rack=%s vehicle=%s mass=%.1f"),
    *Job.JobId,*Pallet->GetName(),*RackSlots[BestSlot].Name,*BestVehicle->VehicleName,Mass);
  }
 }
 for (auto It=StableSince.CreateIterator();It;++It) if (!Present.Contains(It.Key())) { StablePose.Remove(It.Key()); It.RemoveCurrent(); }
 Status=FString::Printf(TEXT("입고 1 | 적재 대기 %d | 운반 중 %d | 완료 %d"),Loaded,Reservations.Num(),CompletedCount);
}
