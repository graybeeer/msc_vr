#include "WarehouseInboundZone.h"
#include "WarehousePerformance.h"
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
bool AWarehouseInboundZone::ContainsPallet(const AWarehousePallet* Pallet) const
{
 if (!IsValid(Pallet) || Pallet->ActorHasTag(TEXT("LogisticsInTransit"))) return false;
 const FVector Local=GetActorTransform().InverseTransformPosition(Pallet->GetActorLocation());
 return FMath::Abs(Local.X)<=HalfSizeCm.X-55 && FMath::Abs(Local.Y)<=HalfSizeCm.Y-55 && FMath::Abs(Local.Z)<=5;
}
bool AWarehouseInboundZone::IsBeingHandled(AWarehousePallet* Pallet,const TArray<AActor*>& Loads) const
{
 if (!IsValid(Pallet) || Pallet->ActorHasTag(TEXT("LogisticsInTransit"))) return true;
 for (TActorIterator<Amsc_vrCharacter> It(GetWorld());It;++It)
  if (It->GetHeldCargo()==Pallet || Loads.Contains(It->GetHeldCargo())) return true;
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
 {
  if (It->GetSupportedPallet()==Pallet || It->ActiveJob.Pallet==Pallet) return true;
  for (const auto& Job:It->PendingJobs) if (Job.Pallet==Pallet) return true;
 }
 return false;
}
FString AWarehouseInboundZone::CheckReceivingManifest(AWarehousePallet* Pallet,const TArray<AActor*>& Loads) const
{
 auto* Strength=AWarehouseDamageSystem::Find(this);
 if (!Strength || Strength->HasFailed(Pallet)) return TEXT("팔레트 파손 · 복구 후 재검수 필요 [RECEIVING_DAMAGED]");
 int32 Count=0;TMap<FString,int32> Actual;
 for (AActor* Load:Loads) if (auto* Box=Cast<AWarehouseCargo>(Load))
 {
  if (Strength->HasFailed(Box)) return TEXT("상자 파손 · 복구 후 재검수 필요 [RECEIVING_DAMAGED]");
  ++Count;++Actual.FindOrAdd(Box->CargoKind.ToString().TrimStartAndEnd());
 }
 if (Pallet->ExpectedCartonCount>0 && Pallet->ExpectedCartonCount!=Count)
  return FString::Printf(TEXT("예정 %d개 / 실제 %d개 · 수량 불일치 [RECEIVING_COUNT]"),Pallet->ExpectedCartonCount,Count);
 if (!Pallet->ExpectedCargoByKind.IsEmpty())
 {
  TMap<FString,int32> Expected;
  for (const auto& Item:Pallet->ExpectedCargoByKind)
  {
   const FString Kind=Item.Key.TrimStartAndEnd();
   if (Kind.IsEmpty() || Item.Value<=0) return TEXT("입고 예정 품목·수량 설정 오류 [RECEIVING_MANIFEST]");
   Expected.FindOrAdd(Kind)+=Item.Value;
  }
  if (Expected.Num()!=Actual.Num()) return TEXT("예정 명세와 실제 품목 불일치 [RECEIVING_KIND]");
  for (const auto& Item:Expected)
   if (Actual.FindRef(Item.Key)!=Item.Value) return TEXT("예정 명세와 실제 품목별 수량 불일치 [RECEIVING_KIND]");
 }
 return FString();
}
bool AWarehouseInboundZone::RequestReinspection(AWarehousePallet* Pallet)
{
 if (!HasAuthority() || !ContainsPallet(Pallet) || Pallet->bWrapping ||
     (Pallet->ReceivingState!=EWarehouseReceivingState::Quarantined && Pallet->ReceiptState!=EWarehouseReceiptState::WrapTorn)) return false;
 auto* Strength=AWarehouseDamageSystem::Find(this);
 if (!Strength) return false;
 const auto Loads=Strength->GetSupportedActors(Pallet,true);
 if (IsBeingHandled(Pallet,Loads) || Strength->HasFailed(Pallet) ||
     Loads.ContainsByPredicate([&](AActor* Load) { return Strength->HasFailed(Load); })) return false;
 // The request schedules a new inspection; it never accepts the manifest or repairs physical damage.
 StableLoads.Remove(Pallet);Pallet->ReceiptState=EWarehouseReceiptState::Pending;
 Pallet->ReceivingState=EWarehouseReceivingState::Awaiting;Pallet->ReceivingStatus=TEXT("재검수 요청 · 적재 안정 대기");
 return true;
}
bool AWarehouseInboundZone::SetExpectedManifest(AWarehousePallet* Pallet,int32 CartonCount,const TMap<FString,int32>& CargoByKind)
{
 if (!HasAuthority() || CartonCount<0 || !ContainsPallet(Pallet) || Pallet->bWrapping ||
     Pallet->ReceivingState!=EWarehouseReceivingState::Quarantined) return false;
 for (const auto& Item:CargoByKind) if (Item.Key.TrimStartAndEnd().IsEmpty() || Item.Value<=0) return false;
 auto* Strength=AWarehouseDamageSystem::Find(this);if (!Strength) return false;
 const auto Loads=Strength->GetSupportedActors(Pallet,true);
 if (IsBeingHandled(Pallet,Loads)) return false;
 Pallet->ExpectedCartonCount=CartonCount;Pallet->ExpectedCargoByKind=CargoByKind;
 Pallet->ReceivingStatus=TEXT("예정 명세 수정됨 · 재검수 요청 필요");
 return true;
}
void AWarehouseInboundZone::RecordInboundConfirmed(AWarehousePallet* Pallet)
{
 const auto* Pending=StableLoads.Find(Pallet);
 if (Pending && Pending->bWrapping && Pallet->ReceiptState==EWarehouseReceiptState::Confirmed)
 { StableLoads.Remove(Pallet);++ConfirmedCount; }
}
bool AWarehouseInboundZone::SlotClear(int32 Index,AWarehousePallet* Pallet,const TArray<AActor*>& Loads,float Height,float Mass) const
{
 const auto& Slot=RackSlots[Index];
 auto* Strength=AWarehouseDamageSystem::Find(this);
 if (!IsValid(Slot.Rack) || !Strength || Strength->HasFailed(Slot.Rack) || Height>Slot.ClearHeightCm || Mass>Slot.CapacityKg) return false;
 for (const auto& Entry : Reservations)
  if (Entry.Rack==Slot.Rack && Entry.Pose.Equals(Slot.Pose,.1f)) return false;
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
  if (!Entry.Pose.Equals(Slot.Pose,.01f) && Entry.Rack==Slot.Rack &&
      FMath::Abs(Entry.Pose.GetLocation().Z-Slot.Pose.GetLocation().Z)<5)
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
 CSV_SCOPED_TIMING_STAT(Warehouse,InboundScan);
 if (!HasAuthority()) return;
 auto* Strength=AWarehouseDamageSystem::Find(this);
 if (!Strength) { Status=TEXT("입고 대기 구역: 하중 검사 시스템 없음"); return; }
 Fleet.Reset();for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) Fleet.Add(*It);
 RackSlots.RemoveAll([](const auto& Slot) { return !IsValid(Slot.Rack); });
 for (int32 I=Reservations.Num()-1;I>=0;--I)
 {
  auto& Entry=Reservations[I];
  auto* Vehicle=Entry.Vehicle.Get();
  if (!Vehicle || !Entry.Pallet.IsValid()) { Reservations.RemoveAt(I); continue; }
  if (!Entry.Rack.IsValid() || !RackSlots.ContainsByPredicate([&](const auto& Slot) { return Slot.Rack==Entry.Rack; }))
  { Reservations.RemoveAt(I);continue; }
  if (Vehicle->CompletedJobIds.Contains(Entry.JobId))
  { ++CompletedCount; Reservations.RemoveAt(I); }
  else
  {
   if (Vehicle->ActiveJob.JobId==Entry.JobId) Entry.Pose=Vehicle->ActiveJob.Destination;
   else for (const auto& Job:Vehicle->PendingJobs) if (Job.JobId==Entry.JobId) { Entry.Pose=Job.Destination;break; }
  }
  // A paused/faulted job retains its destination. E/recovery can safely resume it.
 }
 const double Now=GetWorld()->GetTimeSeconds();
 TSet<TWeakObjectPtr<AWarehousePallet>> Present;
 int32 Confirmed=0;
 WaitingInspectionCount=0;ActiveInspectionCount=0;QuarantinedCount=0;
 bool RefreshLoads=true;
 TArray<AWarehousePallet*> Pallets;
 for (TActorIterator<AWarehousePallet> It(GetWorld());It;++It) if (ContainsPallet(*It)) Pallets.Add(*It);
 // Simultaneous arrivals use a stable identity tie-break; subsequent arrivals preserve their FIFO sequence.
 Pallets.Sort([](const AWarehousePallet& A,const AWarehousePallet& B) { return A.GetPathName()<B.GetPathName(); });
 for (auto* Pallet:Pallets)
 {
  Present.Add(Pallet);
  // Periodic scans reuse the shared 10Hz graph; physical acceptance is checked live below.
  if (RefreshLoads) Strength->EnsureSupportSnapshot();
  const TArray<AActor*> Loads=Strength->GetSupportedActors(Pallet,false);
  RefreshLoads=false;
  bool bCargo=false,bStable=Pallet->GetPalletBody()->GetPhysicsLinearVelocity().Size()<10 &&
   Pallet->GetPalletBody()->GetPhysicsAngularVelocityInDegrees().Size()<5 &&
   FMath::Abs(Pallet->GetActorRotation().Pitch)<2 && FMath::Abs(Pallet->GetActorRotation().Roll)<2;
  FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(Pallet);
  for (auto* Load : Loads) if (Cast<AWarehouseCargo>(Load))
  {
   bCargo=true; Bounds+=AWarehouseDamageSystem::GetPhysicalBounds(Load);
   auto* Part=Cast<UPrimitiveComponent>(Load->GetRootComponent());
   bStable &= Part && Part->GetPhysicsLinearVelocity().Size()<10 && Part->GetPhysicsAngularVelocityInDegrees().Size()<5;
  }
  const bool bHandled=IsBeingHandled(Pallet,Loads);
  if (Pallet->ReceivingState==EWarehouseReceivingState::Quarantined || Pallet->ReceiptState==EWarehouseReceiptState::WrapTorn)
  { StableLoads.Remove(Pallet);++QuarantinedCount;continue; }
  if (!bCargo || bHandled || !bStable)
  {
   StableLoads.Remove(Pallet);
   if (Pallet->ReceivingState==EWarehouseReceivingState::Queued || Pallet->ReceivingState==EWarehouseReceivingState::Inspecting)
   { Pallet->ReceivingState=EWarehouseReceivingState::Awaiting;Pallet->ReceivingStatus=TEXT("운반·적재 변경 · 검수 대기"); }
   continue;
  }
  if (Pallet->ReceiptState==EWarehouseReceiptState::Pending)
  {
   if (Pallet->bWrapping) continue;
   auto* Previous=StableLoads.Find(Pallet);
   bool Changed=!Previous || Previous->bWrapping || Previous->Cargo.Num()!=Loads.Num() ||
    FVector::Dist(Pallet->GetActorLocation(),Previous->Pallet.GetLocation())>2.f ||
    Pallet->GetActorQuat().AngularDistance(Previous->Pallet.GetRotation())>FMath::DegreesToRadians(2.f);
   if (Previous) for (auto* Load:Loads)
   {
    const auto* Pose=Previous->Cargo.Find(Load);
    Changed |= !Pose || FVector::Dist(Load->GetActorLocation(),Pose->GetLocation())>2.f ||
     Load->GetActorQuat().AngularDistance(Pose->GetRotation())>FMath::DegreesToRadians(2.f);
   }
   if (Changed)
   {
    FStableLoad Snapshot;Snapshot.Since=Now;Snapshot.Pallet=Pallet->GetActorTransform();
    for (auto* Load:Loads) Snapshot.Cargo.Add(Load,Load->GetActorTransform());
    StableLoads.Add(Pallet,MoveTemp(Snapshot));
   }
   auto& Inspection=StableLoads.FindChecked(Pallet);
   if (Changed)
   { Pallet->ReceivingState=EWarehouseReceivingState::Awaiting;Pallet->ReceivingStatus=TEXT("입고 검수 · 적재 안정 대기"); }
   if (Now-Inspection.Since<2.f) continue;
   if (!Inspection.QueueSequence) Inspection.QueueSequence=++NextInspectionSequence;
   if (Inspection.InspectionStarted<0)
   { Pallet->ReceivingState=EWarehouseReceivingState::Queued;Pallet->ReceivingStatus=TEXT("입고 NPC 검수 순서 대기");continue; }
   const double Remaining=FMath::Max(0.,double(FMath::Max(.1f,InspectionSeconds))-(Now-Inspection.InspectionStarted));
   Pallet->ReceivingState=EWarehouseReceivingState::Inspecting;
   Pallet->ReceivingStatus=FString::Printf(TEXT("입고 NPC 검수 중 · %.0f초"),FMath::CeilToDouble(Remaining));
   if (Remaining>0) continue;
   const FString Error=CheckReceivingManifest(Pallet,Loads);
   if (!Error.IsEmpty())
   {
    Pallet->ReceivingState=EWarehouseReceivingState::Quarantined;Pallet->ReceivingStatus=Error;
    StableLoads.Remove(Pallet);++QuarantinedCount;
    UE_LOG(LogTemp,Warning,TEXT("RECEIVING_QUARANTINED %s %s"),*Pallet->GetName(),*Error);
    continue;
   }
   if (Pallet->BeginInboundWrapping(Loads,this))
   { Inspection.bWrapping=true;Pallet->ReceivingStatus=TEXT("입고 검수 통과 · 랩 포장 중"); }
   continue;
  }
  ++Confirmed;
  const auto Wrapped=Pallet->GetWrappedCargo();
  if (Loads.ContainsByPredicate([&](AActor* A) { auto* Box=Cast<AWarehouseCargo>(A);return Box && !Wrapped.Contains(Box); }))
  { Pallet->TearWrap();--Confirmed;++QuarantinedCount;continue; }
  // Inspection/wrapping is independent of truck/rack/vehicle availability.
  // Only the shared pickup lane's dispatch remains serial.
  if (bExternalDispatchOnly || !Reservations.IsEmpty() || Fleet.IsEmpty() || RackSlots.IsEmpty()) continue;
  const float Mass=Pallet->PalletMassKg+Strength->GetSupportedMass(Pallet);
  const float Height=Bounds.Max.Z-Bounds.Min.Z;
  AWarehouseForklift* BestVehicle=nullptr;
  int32 BestSlot=INDEX_NONE; float BestCost=MAX_flt;
  for (const auto& VehiclePtr : Fleet)
  {
   auto* Vehicle=VehiclePtr.Get();
   if (!IsValid(Vehicle) || !Vehicle->CanAcceptNewWork() || !Vehicle->PendingJobs.IsEmpty() || !Vehicle->ActiveJob.JobId.IsEmpty() || Mass>Vehicle->RatedLoadKg) continue;
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
  Job.SourceSystem=TEXT("FMS-INBOUND-")+GetName(); Job.Pallet=Pallet; Job.Destination=RackSlots[BestSlot].Pose;
  Job.DestinationResource=RackSlots[BestSlot].Rack;Job.bRequiresDestinationResource=true;
  Job.bRackDestination=true;
  if (BestVehicle->SubmitJob(Job))
  {
   FReservation Entry; Entry.Vehicle=BestVehicle; Entry.Pallet=Pallet; Entry.JobId=Job.JobId; Entry.Rack=RackSlots[BestSlot].Rack;Entry.Pose=RackSlots[BestSlot].Pose;
   Reservations.Add(Entry); ++DispatchedCount;
   UE_LOG(LogTemp,Log,TEXT("INBOUND_DISPATCH %s pallet=%s rack=%s vehicle=%s mass=%.1f"),
    *Job.JobId,*Pallet->GetName(),*RackSlots[BestSlot].Name,*BestVehicle->VehicleName,Mass);
  }
 }
 TArray<AWarehousePallet*> Queue;
 for (auto It=StableLoads.CreateIterator();It;++It)
 {
  if (!Present.Contains(It.Key()) || !It.Key().IsValid() || It.Key()->ReceiptState!=EWarehouseReceiptState::Pending ||
      It.Key()->ReceivingState==EWarehouseReceivingState::Quarantined || (It.Key()->bWrapping && !It.Value().bWrapping))
  { It.RemoveCurrent();continue; }
  if (It.Value().bWrapping) continue;
  if (It.Value().InspectionStarted>=0) ++ActiveInspectionCount;
  else if (It.Key()->ReceivingState==EWarehouseReceivingState::Queued) Queue.Add(It.Key().Get());
 }
 Queue.Sort([this](const AWarehousePallet& A,const AWarehousePallet& B)
 { return StableLoads.FindChecked(const_cast<AWarehousePallet*>(&A)).QueueSequence<StableLoads.FindChecked(const_cast<AWarehousePallet*>(&B)).QueueSequence; });
 const int32 Workers=FMath::Clamp(InspectionWorkers,1,16);
 for (auto* Pallet:Queue)
 {
  if (ActiveInspectionCount>=Workers) { ++WaitingInspectionCount;continue; }
  StableLoads.FindChecked(Pallet).InspectionStarted=Now;++ActiveInspectionCount;
  Pallet->ReceivingState=EWarehouseReceivingState::Inspecting;Pallet->ReceivingStatus=TEXT("입고 NPC 검수 시작");
  UE_LOG(LogTemp,Log,TEXT("RECEIVING_STARTED %s duration=%.1f simulation seconds"),*Pallet->GetName(),FMath::Max(.1f,InspectionSeconds));
 }
 Status=FString::Printf(TEXT("입고 대기 구역 | 검수 대기 %d · 진행 %d/%d | 품질 격리 %d | 입고 확정 %d | 운반 중 %d | 완료 %d"),
  WaitingInspectionCount,ActiveInspectionCount,Workers,QuarantinedCount,Confirmed,Reservations.Num(),CompletedCount);
 if (Fleet.IsEmpty()) Status+=TEXT(" · 지게차 없음 [NO_FORKLIFT]");
 else if (RackSlots.IsEmpty()) Status+=TEXT(" · 적재할 랙 없음 [NO_RACK]");
}
