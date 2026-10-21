#include "WarehouseFleetCoordinator.h"
#include "WarehousePerformance.h"
#include "WarehouseForklift.h"
#include "WarehouseChargingStation.h"
#include "WarehouseWaitingBay.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/SceneComponent.h"

AWarehouseFleetCoordinator::AWarehouseFleetCoordinator()
{
 PrimaryActorTick.bCanEverTick=false;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Coordinator")));
}

AWarehouseFleetCoordinator* AWarehouseFleetCoordinator::Find(const AActor* Context)
{
 if (!Context || !Context->HasAuthority() || !Context->GetWorld()->IsGameWorld()) return nullptr;
 for (TActorIterator<AWarehouseFleetCoordinator> It(Context->GetWorld());It;++It) return *It;
 return Context->GetWorld()->SpawnActor<AWarehouseFleetCoordinator>();
}

FBox AWarehouseFleetCoordinator::Footprint(const AWarehouseForklift* V,const FTransform& Pose) const
{
 return V->NavigationEnvelope().TransformBy(Pose.ToMatrixWithScale()).ExpandBy(FVector(5,5,0));
}

TArray<FBox> AWarehouseFleetCoordinator::Corridor(const AWarehouseForklift* V) const
{
 TArray<FBox> Result;
 FTransform Previous=V->GetActorTransform();
 FBox Last=Footprint(V,Previous);
 Result.Add(Last);
 for (int32 I=V->RouteIndex;I<V->PlannedRoute.Num();++I)
 {
  const auto& Pose=V->PlannedRoute[I].Pose;
  if (I+1<V->PlannedRoute.Num() && FVector::Dist2D(Previous.GetLocation(),Pose.GetLocation())<45 &&
      Previous.GetRotation().AngularDistance(Pose.GetRotation())<FMath::DegreesToRadians(8.f)) continue;
  FBox Next=Footprint(V,Pose);
  Result.Add(Last+Next);Last=Next;Previous=Pose;
 }
 return Result;
}

bool AWarehouseFleetCoordinator::Intersects(const TArray<FBox>& A,const TArray<FBox>& B)
{
 for (const auto& X : A) for (const auto& Y : B) if (X.Intersect(Y)) return true;
 return false;
}

bool AWarehouseFleetCoordinator::PlanningClear(const AWarehouseForklift* V,const FBox& Box) const
{
 if (PlanningObstacle.IsValid && PlanningObstacle.Intersect(Box)) return false;
 // Keep a buffer for the swept corridor between successive planning samples,
 // not only the instantaneous truck footprint at each sample.
 for (const auto& Pair : ClosedSections) if (Pair.Value.Intersect(Box.ExpandBy(FVector(50,50,0)))) return false;
 if (!V->bAvoidTrafficReservations) return true;
 for (const auto& Pair : Requests) if (Pair.Key.Get()!=V && Pair.Value.Granted)
  for (const auto& Cell : Pair.Value.Corridor) if (Box.Intersect(Cell)) return false;
 for (const auto& Pair : Yields) if (Pair.Key.Get()!=V)
  for (const auto& Cell : Pair.Value.Escape) if (Box.Intersect(Cell)) return false;
 return true;
}

void AWarehouseFleetCoordinator::CloseSection(const FString& Id,FVector Min,FVector Max,const FString& Reason)
{
 if (!HasAuthority() || Id.IsEmpty() || Min.ContainsNaN() || Max.ContainsNaN() || Min.X>=Max.X || Min.Y>=Max.Y || Min.Z>=Max.Z) return;
 ClosedSections.Add(Id,FBox(Min,Max));ClosedSectionCount=ClosedSections.Num();Status=TEXT("관제: 구간 사용 중지 · ")+Reason;
 UE_LOG(LogTemp,Log,TEXT("FLEET_SECTION_CLOSED %s: %s"),*Id,*Reason);
}

void AWarehouseFleetCoordinator::ReopenSection(const FString& Id)
{
 if (!HasAuthority()) return;
 ClosedSections.Remove(Id);ClosedSectionCount=ClosedSections.Num();
}

void AWarehouseFleetCoordinator::Release(AWarehouseForklift* V)
{
 Requests.Remove(V);V->bTrafficWaiting=false;V->TrafficStatus.Reset();
 Yields.Remove(V);V->bTrafficYielding=false;
}

void AWarehouseFleetCoordinator::Waiting(AWarehouseForklift* V,const FString& Reason)
{
 if (!V->bTrafficWaiting) { ++WaitCount;UE_LOG(LogTemp,Log,TEXT("FLEET_WAIT %s: %s"),*V->VehicleName,*Reason); }
 V->bTrafficWaiting=true;V->TrafficStatus=Reason;V->BrakeDrive();V->SetStatus(Reason);
 if (auto* Request=Requests.Find(V))
 {
  Request->Granted=false;
  if (Request->BlockedSince<0) Request->BlockedSince=GetWorld()->GetTimeSeconds();
 }
 Status=Reason;
}

bool AWarehouseFleetCoordinator::Retreat(AWarehouseForklift* V,AWarehouseForklift* Leader,const TArray<FBox>& Passage)
{
 if (Yields.Contains(V)) return true;
 if (!V->bPowered || !V->bAutonomousMode || V->IsRemoteControlled() || V->bMechanicalFailure ||
     V->bFloorTransfer || V->LiftOffset>20 || !V->CheckSystems().IsEmpty()) return false;
 if (V->AIState!=EWarehouseAIState::Ready && V->AIState!=EWarehouseAIState::NavigatePickup &&
     V->AIState!=EWarehouseAIState::NavigateDelivery && V->AIState!=EWarehouseAIState::PlanPickup &&
     V->AIState!=EWarehouseAIState::PlanDelivery && V->AIState!=EWarehouseAIState::WaitingObstacle &&
     V->AIState!=EWarehouseAIState::PlanParking && V->AIState!=EWarehouseAIState::NavigateParking && V->AIState!=EWarehouseAIState::NavigateCharge) return false;
 const auto Saved=V->PlannedRoute;const int32 SavedIndex=V->RouteIndex;
 const auto Start=V->GetActorTransform();
 TGuardValue<bool> Escape(V->bTrafficEscape,true);
 for (float Side : {0.f,-220.f,220.f}) for (float Back : {180.f,280.f,420.f,-180.f,-280.f,-420.f})
 {
  FVector P=Start.TransformPosition(FVector(-Back,Side,0));P.Z=V->FloorBase(Start.GetLocation().Z);
  const FTransform Goal(FRotator(0,Start.Rotator().Yaw,0),P);
  if (Intersects({Footprint(V,Goal).ExpandBy(FVector(20,20,0))},Passage) || !V->PlanAutonomousRoute(Goal)) continue;
  const auto Path=Corridor(V);
  bool Blocked=false;
  for (const auto& Pair : Yields) if (Intersects(Path,Pair.Value.Escape)) { Blocked=true;break; }
  if (Blocked) continue;
  FYield Ticket;Ticket.Leader=Leader;Ticket.Escape=Path;Ticket.Clearance=Footprint(V,Start);Ticket.Goal=Goal;Ticket.Started=GetWorld()->GetTimeSeconds();
  Ticket.Resume=V->AIState==EWarehouseAIState::WaitingObstacle ? V->WaitResumeAI : V->AIState;
  if (!Saved.IsEmpty()) Ticket.ResumeGoal=Saved.Last().Pose;
  Yields.Add(V,Ticket);Requests.Remove(V);++YieldCount;
  V->bTrafficYielding=true;V->bTrafficWaiting=false;V->TrafficStatus=Back>0 ? TEXT("관제: 퇴로 확보 · 안전 대기 위치로 후퇴") : TEXT("관제: 이동 공간 확보 · 앞쪽 안전 대기 위치로 이동");
  UE_LOG(LogTemp,Log,TEXT("FLEET_YIELD %s for %s goal=%s loaded=%d"),*V->VehicleName,*Leader->VehicleName,*P.ToString(),V->bSupportingPallet);
  return true;
 }
 V->PlannedRoute=Saved;V->RouteIndex=SavedIndex;
 return false;
}

bool AWarehouseFleetCoordinator::Permit(AWarehouseForklift* V,const FTransform* PreciseGoal)
{
 CSV_SCOPED_TIMING_STAT(Warehouse,TrafficPermit);
 if (V->bTrafficEscape || V->IsRemoteControlled()) return true;
 auto& Request=Requests.FindOrAdd(V);
 if (!Request.Order) { Request.Order=++NextOrder;Request.Since=GetWorld()->GetTimeSeconds(); }
 Request.Corridor=PreciseGoal ? TArray<FBox>{Footprint(V,V->GetActorTransform())+Footprint(V,*PreciseGoal)} : Corridor(V);
 for (const auto& Section : ClosedSections) if (Intersects(Request.Corridor,{Section.Value}))
 {
  const double Now=GetWorld()->GetTimeSeconds();
  if (!PreciseGoal && Now-Request.LastDetour>5 && !V->PlannedRoute.IsEmpty())
  {
   Request.LastDetour=Now;const auto Saved=V->PlannedRoute;const int32 Index=V->RouteIndex;
   TGuardValue<bool> Avoid(V->bAvoidTrafficReservations,true);
   if (V->PlanRecoveryDetour(Saved.Last().Pose))
   { ++DetourCount;Request.Corridor=Corridor(V);return Permit(V); }
   V->PlannedRoute=Saved;V->RouteIndex=Index;
  }
  Waiting(V,TEXT("관제: 사용 중지 구간 · 우회 경로 확인 중"));
  if (Now-Request.BlockedSince>10) V->RecoveryFault(TEXT("CLOSED SECTION - NO SAFE DETOUR"),TEXT("사용 중지 구간을 우회할 안전 공간이 없습니다. 작업자 복구가 필요합니다."));
  return false;
 }
 for (auto It=Requests.CreateIterator();It;++It)
  if (!It.Key().IsValid() || !It.Key()->bPowered || It.Key()->IsRemoteControlled()) It.RemoveCurrent();
 for (const auto& Pair : Yields) if (Pair.Key.Get()!=V && Intersects(Request.Corridor,Pair.Value.Escape))
 {
  const double Now=GetWorld()->GetTimeSeconds();
  if (Pair.Value.Arrived && !PreciseGoal && Now-Request.LastDetour>5 && !V->PlannedRoute.IsEmpty())
  {
   Request.LastDetour=Now;const auto Saved=V->PlannedRoute;const int32 Index=V->RouteIndex;
   TGuardValue<bool> Avoid(V->bAvoidTrafficReservations,true);
   if (V->PlanRecoveryDetour(Saved.Last().Pose))
   { ++DetourCount;Request.Corridor=Corridor(V);V->BrakeDrive();return false; }
   V->PlannedRoute=Saved;V->RouteIndex=Index;
  }
  Waiting(V,TEXT("관제: 다른 차량의 퇴로 확보 중 · 진입 대기"));
  if (Pair.Value.Arrived && Now-Request.BlockedSince>10)
   V->RecoveryFault(TEXT("TRAFFIC NO SAFE PASSAGE - OPERATOR REQUIRED"),TEXT("대기 차량을 지나갈 안전 공간이 없습니다. 정지 유지 · 작업자 복구를 요청합니다."));
  return false;
 }
 for (const auto& Pair : Requests) if (Pair.Key.Get()!=V &&
     ((Pair.Value.Granted && (!Request.Granted || Pair.Value.Order<Request.Order)) ||
      (!Request.Granted && !Pair.Value.Granted && Pair.Value.Order<Request.Order)) && Intersects(Request.Corridor,Pair.Value.Corridor))
 {
  const double Now=GetWorld()->GetTimeSeconds();
  if (Now-Request.LastDetour>5 && !V->PlannedRoute.IsEmpty())
  {
   Request.LastDetour=Now;const auto Saved=V->PlannedRoute;const int32 Index=V->RouteIndex;
   const FTransform Goal=Saved.Last().Pose;
   TGuardValue<bool> Avoid(V->bAvoidTrafficReservations,true);
   if (V->PlanRecoveryDetour(Goal) && !Intersects(Corridor(V),Pair.Value.Corridor))
   { ++DetourCount;Request.Corridor=Corridor(V);break; }
   V->PlannedRoute=Saved;V->RouteIndex=Index;
  }
  Waiting(V,FString::Printf(TEXT("관제: %s 통과 우선 · 통로 진입 대기"),*Pair.Key->VehicleName));return false;
 }
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) if (*It!=V)
 {
  // Reservations above are reported intent. Discovering a parked/unknown
  // vehicle is perception: never ask the world for its hidden live pose.
  FBox VehicleBounds(ForceInit);
  if (V->SensorMode==EWarehouseSensorMode::Autonomous)
  {
   if (!V->GetPerceivedActorBounds(*It,VehicleBounds)) continue;
  }
  else VehicleBounds=Footprint(*It,It->GetActorTransform());
  if (!Intersects(Request.Corridor,{VehicleBounds})) continue;
  if (Yields.Contains(*It)) { Waiting(V,TEXT("관제: 앞 차량 후퇴 완료 대기"));return false; }
  if (auto* Other=Requests.Find(*It);Other && Other->Granted)
  { Waiting(V,TEXT("관제: 진입 차량 통과 대기"));return false; }
  if (GetWorld()->GetTimeSeconds()-Request.LastEscape>2)
  {
   Request.LastEscape=GetWorld()->GetTimeSeconds();
   if (Retreat(*It,V,Request.Corridor))
   { Waiting(V,TEXT("관제: 퇴로 확보 → 앞 차량 후퇴 → 통과"));return false; }
  }
  // A parked healthy vehicle has no active lease. If its retreat is not
  // feasible, plan around its real body instead of ignoring it as moving traffic.
  const double Now=GetWorld()->GetTimeSeconds();
  if (Now-Request.LastDetour>5 && !V->PlannedRoute.IsEmpty())
  {
   Request.LastDetour=Now;const auto Saved=V->PlannedRoute;const int32 Index=V->RouteIndex;
   TGuardValue<bool> PhysicalTraffic(V->bTrafficEscape,true);
   TGuardValue<bool> Avoid(V->bAvoidTrafficReservations,true);
   TGuardValue<FBox> Blocker(PlanningObstacle,VehicleBounds);
   if (V->PlanRecoveryDetour(Saved.Last().Pose) && !Intersects(Corridor(V),{VehicleBounds}))
   {
    ++DetourCount;Request.Corridor=Corridor(V);
    UE_LOG(LogTemp,Log,TEXT("FLEET_PARKED_VEHICLE_DETOUR %s around %s"),*V->VehicleName,*It->VehicleName);
    V->BrakeDrive();
    return false;
   }
   V->PlannedRoute=Saved;V->RouteIndex=Index;
  }
  Waiting(V,TEXT("관제: 통로 점유 · 안전 공간 확인 중"));
  if (GetWorld()->GetTimeSeconds()-Request.BlockedSince>10)
   V->RecoveryFault(TEXT("TRAFFIC NO SAFE ESCAPE - OPERATOR REQUIRED"),TEXT("통로를 점유한 차량의 안전한 퇴로가 없습니다. 작업자 복구가 필요합니다."));
  return false;
 }
 const bool Resumed=V->bTrafficWaiting;Request.Granted=true;Request.BlockedSince=-1;V->bTrafficWaiting=false;V->TrafficStatus=TEXT("관제: 통로 예약 · 운행 허가");
 if (Resumed) V->SetStatus(TEXT("관제: 통과 허가 · 기존 작업 진행"));
 return true;
}

bool AWarehouseFleetCoordinator::AdvanceYield(AWarehouseForklift* V,float Dt)
{
 auto* Ticket=Yields.Find(V);
 if (!Ticket) return false;
 V->BrakeDrive();
 if (!V->bPowered || V->bMechanicalFailure || V->IsRemoteControlled())
 { Yields.Remove(V);V->bTrafficYielding=false;return false; }
 if (!Ticket->Arrived)
 {
  for (const auto& Section : ClosedSections) if (Intersects(Ticket->Escape,{Section.Value}))
  {
   Yields.Remove(V);V->bTrafficYielding=false;
   V->RecoveryFault(TEXT("RETREAT SECTION CLOSED"),TEXT("후퇴 경로가 사용 중지되었습니다. 정지 유지 · 작업자 복구를 요청합니다."));
   return true;
  }
  TGuardValue<bool> Escape(V->bTrafficEscape,true);
  V->SetStatus(V->TrafficStatus);
  const double Now=GetWorld()->GetTimeSeconds();
  if (!Ticket->Separating && (!V->bRouteObstacleClear || !V->bMovementObstacleClear) &&
      !V->MovementObstacleReason.Contains(TEXT("PERSON")) && Ticket->Replans<3 && Now-Ticket->LastReplan>3)
  {
   Ticket->LastReplan=Now;++Ticket->Replans;
   const auto Saved=V->PlannedRoute;const int32 Index=V->RouteIndex;
   TGuardValue<bool> Avoid(V->bAvoidTrafficReservations,true);
   // A geometrically clear curve may still start inward at a contact while
   // the finite steering spindle slews. First separate along the current
   // heading, without asking the chassis to turn beside the obstacle.
   if (!Ticket->Separated && V->MovementObstacleReason.Contains(TEXT("CONTACT")))
   {
    TGuardValue<bool> Planning(V->bPlanningRoute,true);
    const FTransform From=V->GetActorTransform();
    for (float Direction : {-V->LastDriveDirection,V->LastDriveDirection})
    {
     const FTransform Back(FRotator(0,From.Rotator().Yaw,0),From.GetLocation()+V->GetActorForwardVector().GetSafeNormal2D()*Direction*60.f);
     TArray<FWarehouseRoutePoint> Path;float Cost;
     if (!V->ConnectPoses(From,Back,Path,Cost) || Path.IsEmpty() || Path.ContainsByPredicate([&](const FWarehouseRoutePoint& P){return P.bPivot || P.bReverse!=(Direction<0);})) continue;
     V->PlannedRoute=Path;V->RouteIndex=0;
     const auto Cells=Corridor(V);bool Clear=true;
     for (const auto& Other : Yields) if (Other.Key.Get()!=V && Intersects(Cells,Other.Value.Escape)) Clear=false;
     if (!Clear) continue;
     V->EndpointSegment=-1;V->bRouteTurning=false;V->bEndpointDriving=false;V->bEndpointOrienting=false;
     Ticket->Escape.Append(Cells);Ticket->Separating=true;Ticket->Separated=true;Ticket->Started=Now;
     V->TrafficStatus=TEXT("관제: 접촉 방향에서 짧게 분리 이동 → 퇴로 재계획");
     UE_LOG(LogTemp,Log,TEXT("FLEET_CONTACT_SEPARATE %s direction=%.0f"),*V->VehicleName,Direction);
     return true;
    }
    V->PlannedRoute=Saved;V->RouteIndex=Index;
   }
   bool Clear=V->PlanRecoveryDetour(Ticket->Goal);
   const auto Path=Clear ? Corridor(V) : TArray<FBox>{};
   for (const auto& Other : Yields) if (Other.Key.Get()!=V && Intersects(Path,Other.Value.Escape)) Clear=false;
   if (Clear)
   {
    // Replace the reservation before resuming physical movement from the
    // actual pose. Never reposition the truck or bypass its safety scanner.
    Ticket->Escape=Path;Ticket->Started=Now;
    UE_LOG(LogTemp,Log,TEXT("FLEET_ESCAPE_REPLAN %s attempt=%d"),*V->VehicleName,Ticket->Replans);
    return true;
   }
   V->PlannedRoute=Saved;V->RouteIndex=Index;
  }
  if (V->FollowRoute(35.f,Dt))
  {
   if (Ticket->Separating)
   {
    Ticket->Separating=false;
    TGuardValue<bool> Avoid(V->bAvoidTrafficReservations,true);
    if (V->PlanRecoveryDetour(Ticket->Goal))
    {
     Ticket->Escape=Corridor(V);Ticket->Started=Now;
     V->TrafficStatus=TEXT("관제: 접촉 분리 완료 · 안전 대기 위치로 이동");
     UE_LOG(LogTemp,Log,TEXT("FLEET_SEPARATED_REPLAN %s"),*V->VehicleName);
    }
    else
    {
     Yields.Remove(V);V->bTrafficYielding=false;
     V->RecoveryFault(TEXT("TRAFFIC ESCAPE REPLAN FAILED"),TEXT("접촉 위치를 벗어났지만 안전한 퇴로가 없습니다. 정지 유지 · 작업자 복구를 요청합니다."));
    }
   }
   else
   {
    Ticket->Arrived=true;Ticket->Escape={Footprint(V,V->GetActorTransform())};
    // The bounded retry counter now belongs to the arrived waiting phase.
    Ticket->Replans=0;Ticket->LastReplan=GetWorld()->GetTimeSeconds();
    V->TrafficStatus=TEXT("관제: 대기 위치 도착 · 우선 차량 통과 대기");
   }
  }
  else if (Now-Ticket->Started>(Ticket->Replans ? 90 : 45))
  {
   Yields.Remove(V);V->bTrafficYielding=false;
   V->RecoveryFault(TEXT("TRAFFIC RETREAT BLOCKED"),TEXT("후퇴 경로가 막혔습니다. 정지 유지 · 작업자 복구를 요청합니다."));
  }
  return true;
 }
 auto* Leader=Ticket->Leader.Get();
 Ticket->Escape={Footprint(V,V->GetActorTransform())};
 bool Occupied=false;
 FBox ObservedLeader(ForceInit);
 const bool HasObservedLeader=Leader && V->SensorMode==EWarehouseSensorMode::Autonomous && V->GetPerceivedActorBounds(Leader,ObservedLeader);
 if (Leader)
 {
  Occupied=V->SensorMode==EWarehouseSensorMode::Autonomous ?
   HasObservedLeader && ObservedLeader.Intersect(Ticket->Clearance) :
   Footprint(Leader,Leader->GetActorTransform()).Intersect(Ticket->Clearance);
 }
 if (const auto* Lease=Requests.Find(Leader)) Occupied|=Intersects(Lease->Corridor,{Ticket->Clearance});
 if (Occupied)
 {
  // A leader may finish parking in the old passage instead of travelling
  // through it. Its reported idle state ends the passage lease, but does not
  // erase the measured vehicle. Resume only on a newly validated detour.
  const double Now=GetWorld()->GetTimeSeconds();
  if (HasObservedLeader && Ticket->ResumeGoal.IsSet() && Leader->AIState==EWarehouseAIState::Ready &&
      Leader->IsIdleForLayout() && !Leader->bTrafficYielding && !Requests.Contains(Leader) &&
      Now-Ticket->LastReplan>=5.f)
  {
   Ticket->LastReplan=Now;++Ticket->Replans;
   const auto Saved=V->PlannedRoute;const int32 SavedIndex=V->RouteIndex,SavedEndpoint=V->EndpointSegment;
   const bool SavedDriving=V->bEndpointDriving,SavedOrienting=V->bEndpointOrienting,SavedTurning=V->bRouteTurning;
   TGuardValue<bool> PhysicalTraffic(V->bTrafficEscape,true);
   TGuardValue<bool> Avoid(V->bAvoidTrafficReservations,true);
   TGuardValue<FBox> Blocker(PlanningObstacle,ObservedLeader);
   const bool Found=V->PlanRecoveryDetour(Ticket->ResumeGoal.GetValue()) && !Intersects(Corridor(V),{ObservedLeader});
   if (Found)
   {
    const auto Resume=Ticket->Resume;
    Yields.Remove(V);V->bTrafficYielding=false;
    V->TransitionAI(Resume,TEXT("관제: 관측된 대기 차량 우회 · 기존 작업 재개"));V->bRouteStarted=true;
    V->bTrafficWaiting=false;V->TrafficStatus=TEXT("관제: 관측된 대기 차량 우회 · 기존 작업 재개");
    UE_LOG(LogTemp,Log,TEXT("FLEET_IDLE_LEADER_DETOUR %s around %s"),*V->VehicleName,*Leader->VehicleName);
    return true;
   }
   V->PlannedRoute=Saved;V->RouteIndex=SavedIndex;V->EndpointSegment=SavedEndpoint;
   V->bEndpointDriving=SavedDriving;V->bEndpointOrienting=SavedOrienting;V->bRouteTurning=SavedTurning;
   UE_LOG(LogTemp,Log,TEXT("FLEET_IDLE_LEADER_REPLAN_FAILED %s around %s attempt=%d/3"),*V->VehicleName,*Leader->VehicleName,Ticket->Replans);
   if (Ticket->Replans>=3)
   {
    Yields.Remove(V);V->bTrafficYielding=false;
    V->RecoveryFault(TEXT("TRAFFIC IDLE LEADER - NO SAFE DETOUR"),TEXT("대기 차량을 우회할 안전한 경로를 3회 찾지 못했습니다. 작업자 복구가 필요합니다."));
    return true;
   }
  }
  V->SetStatus(V->TrafficStatus);return true;
 }
 const auto Resume=Ticket->Resume;
 Yields.Remove(V);V->bTrafficYielding=false;V->PlannedRoute.Reset();V->RouteIndex=0;
 V->TransitionAI(Resume,TEXT("관제: 통과 확인 · 기존 작업 재개"));
 V->TrafficStatus=TEXT("관제: 통과 확인 · 기존 작업 재개");V->SetStatus(V->TrafficStatus);
 UE_LOG(LogTemp,Log,TEXT("FLEET_RESUME %s"),*V->VehicleName);
 return true;
}
