#include "WarehouseTransportVisualizer.h"
#include "WarehouseForklift.h"
#include "WarehouseElevator.h"
#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehouseDamageSystem.h"
#include "Components/PostProcessComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

TArray<FVector> AWarehouseForklift::GetDisplayRoute() const
{
 TArray<FVector> Result;
 if (!bPowered || !bAutonomousMode || IsRemoteControlled() ||
     AIState==EWarehouseAIState::Paused || AIState==EWarehouseAIState::Fault) return Result;
 // Boarding/exiting use the existing precise actuator, not PlannedRoute.
 // Display that actuator's actual endpoint only while its door interlock permits travel.
 if (AIState==EWarehouseAIState::ElevatorBoard || AIState==EWarehouseAIState::ElevatorExit)
 {
  if (!IsValid(Elevator)) return Result;
  const bool Boarding=AIState==EWarehouseAIState::ElevatorBoard;
  if (Boarding ? !Elevator->CanEnter(const_cast<AWarehouseForklift*>(this)) :
      !Elevator->CanExit(const_cast<AWarehouseForklift*>(this))) return Result;
  const int32 Level=Boarding ? TransferFromFloor : TransferToFloor;
  const FVector Goal=(Boarding ? Elevator->BoardingPose(Level) : Elevator->WaitingPose(Level)).GetLocation();
  const float Ground=Elevator->FloorHeight(Level)+.8f;
  if (FVector::DistSquared2D(GetActorLocation(),Goal)>1.f)
  {
   Result.Add(FVector(GetActorLocation().X,GetActorLocation().Y,Ground));
   Result.Add(FVector(Goal.X,Goal.Y,Ground));
  }
  return Result;
 }
 if (AIState==EWarehouseAIState::ElevatorCall || AIState==EWarehouseAIState::ElevatorWait ||
     AIState==EWarehouseAIState::ElevatorRide || (!bRouteStarted && !bTrafficYielding)) return Result;
 const float Floor=FloorBase(GetActorLocation().Z);
 Result.Add(FVector(GetActorLocation().X,GetActorLocation().Y,Floor+.8f));
 for (int32 I=RouteIndex;I<PlannedRoute.Num();++I)
 {
  const FVector Point=PlannedRoute[I].Pose.GetLocation();
  if (FMath::Abs(FloorBase(Point.Z)-Floor)>5.f) break; // Never draw a route through a floor/shaft.
  const FVector Ground(Point.X,Point.Y,Floor+.8f);
  if (FVector::DistSquared2D(Result.Last(),Ground)>1.f) Result.Add(Ground);
 }
 if (Result.Num()<2) Result.Reset();
 return Result;
}

UWarehouseTransportVisualizer::UWarehouseTransportVisualizer()
{
 PrimaryComponentTick.bCanEverTick=false;
 // Hard references keep the project's generated materials and engine mesh in cooked builds.
 static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Outline(TEXT("/Game/Warehouse/Materials/M_TransportOutline"));
 static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Arrow(TEXT("/Game/Warehouse/Materials/M_RouteArrow"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Bar(TEXT("/Engine/BasicShapes/Cube"));
 OutlineMaterial=Outline.Get();ArrowMaterial=Arrow.Get();ArrowBar=Bar.Object;
}

void UWarehouseTransportVisualizer::RestoreDepth(UPrimitiveComponent* Part,const FDepthState& State)
{
 Part->SetCustomDepthStencilValue(State.Stencil);
 Part->SetCustomDepthStencilWriteMask(static_cast<ERendererStencilMask>(State.WriteMask));
 Part->SetRenderCustomDepth(State.bEnabled);
}

void UWarehouseTransportVisualizer::Update()
{
 if (!OutlineMaterial || !ArrowMaterial || !ArrowBar) return;
 if (!Effect)
 {
  // Controllers are hidden actors; their own scene components would not render.
  FActorSpawnParameters Params;Params.Owner=GetOwner();Params.ObjectFlags|=RF_Transient;
  DisplayActor=GetWorld()->SpawnActor<AActor>(Params);
  if (!DisplayActor) return;
  DisplayActor->Tags.Add(TEXT("TransportDisplay"));
  Effect=NewObject<UPostProcessComponent>(DisplayActor,TEXT("TransportOutlineEffect"));
  DisplayActor->SetRootComponent(Effect);
  Effect->bUnbound=true;Effect->Settings.AddBlendable(OutlineMaterial,1.f);
  Effect->BlendWeight=1.f;
  Effect->RegisterComponent();
 }
 for (auto It=Vehicles.CreateIterator();It;++It) if (!It.Key().IsValid())
 {
  if (auto* Arrows=It.Value().Arrows.Get()) Arrows->DestroyComponent();
  It.RemoveCurrent();
 }
 TArray<AWarehouseForklift*> Ordered;
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) Ordered.Add(*It);
 Ordered.Sort([](const AWarehouseForklift& A,const AWarehouseForklift& B)
 { const int32 Names=A.VehicleName.Compare(B.VehicleName);return Names==0 ? A.GetName()<B.GetName() : Names<0; });
 // Indices remain stable while the vehicle exists, including pause/replan/reposition.
 for (auto* Vehicle:Ordered) if (!Vehicles.Contains(Vehicle))
 {
  TSet<int32> Used;
  for (const auto& Pair:Vehicles) Used.Add(Pair.Value.ColorIndex);
  int32 Index=0;
  while (Index<63 && Used.Contains(Index)) ++Index;
  if (Index<63) Vehicles.Add(Vehicle).ColorIndex=Index;
 }
 TMap<UPrimitiveComponent*,int32> Wanted;
 auto* Supports=AWarehouseDamageSystem::Find(GetOwner());
 bool SnapshotReady=false,HasArrows=false;
 for (auto* Vehicle:Ordered) if (auto* Visual=Vehicles.Find(Vehicle))
 {
  auto AddJob=[&](const FWarehouseWorkOrder& Job)
  {
   // TargetPallet alone can be a stale/default pointer with no assigned work.
   if (Job.JobId.IsEmpty() || Vehicle->CompletedJobIds.Contains(Job.JobId) || !IsValid(Job.Pallet) || Job.Pallet->IsHidden()) return;
   Wanted.FindOrAdd(Job.Pallet->GetPalletBody(),192+Visual->ColorIndex);
   if (!Supports) return;
   if (!SnapshotReady) { Supports->EnsureSupportSnapshot();SnapshotReady=true; }
   for (auto* Actor:Supports->GetSupportedActors(Job.Pallet,false))
    if (auto* Cargo=Cast<AWarehouseCargo>(Actor);IsValid(Cargo) && !Cargo->IsHidden())
     Wanted.FindOrAdd(Cargo->GetCargoBody(),192+Visual->ColorIndex);
  };
  AddJob(Vehicle->ActiveJob);
  for (const auto& Job:Vehicle->PendingJobs) AddJob(Job);
  UpdateArrows(Vehicle,*Visual);
  if (auto* Arrows=Visual->Arrows.Get()) HasArrows|=Arrows->IsVisible() && Arrows->GetInstanceCount()>0;
 }
 for (auto It=OriginalDepth.CreateIterator();It;++It)
  if (!It.Key().IsValid() || !Wanted.Contains(It.Key().Get()))
  {
   if (auto* Part=It.Key().Get()) RestoreDepth(Part,It.Value());
   It.RemoveCurrent();
  }
 for (const auto& Pair:Wanted) if (auto* Part=Pair.Key;IsValid(Part))
 {
  if (!OriginalDepth.Contains(Part)) OriginalDepth.Add(Part,{bool(Part->bRenderCustomDepth),Part->CustomDepthStencilValue,uint8(Part->CustomDepthStencilWriteMask)});
  if (!Part->bRenderCustomDepth || Part->CustomDepthStencilValue!=Pair.Value || Part->CustomDepthStencilWriteMask!=ERendererStencilMask::ERSM_Default)
  {
   Part->SetCustomDepthStencilValue(Pair.Value);
   Part->SetCustomDepthStencilWriteMask(ERendererStencilMask::ERSM_Default);
   Part->SetRenderCustomDepth(true);
  }
 }
 Effect->bEnabled=!OriginalDepth.IsEmpty() || HasArrows;
}

void UWarehouseTransportVisualizer::UpdateArrows(AWarehouseForklift* Vehicle,FVehicleVisual& Visual)
{
 const auto Route=Vehicle->GetDisplayRoute();
 const auto* Viewer=Cast<APlayerController>(GetOwner());
 const bool Visible=!Vehicle->IsHidden() && (!Viewer || !Viewer->HiddenActors.Contains(Vehicle));
 if (auto* Arrows=Visual.Arrows.Get();Arrows && Arrows->IsVisible()!=Visible) Arrows->SetVisibility(Visible);
 bool Changed=Route.Num()!=Visual.Route.Num();
 if (!Changed) for (int32 I=0;I<Route.Num();++I) if (!Route[I].Equals(Visual.Route[I],4.f)) { Changed=true;break; }
 if (!Changed) return;
 Visual.Route=Route;
 auto* Arrows=Visual.Arrows.Get();
 if (!Arrows && Route.Num()>1)
 {
  Arrows=NewObject<UInstancedStaticMeshComponent>(DisplayActor);
  Arrows->ComponentTags.Add(TEXT("TransportRouteVisual"));
  Arrows->SetStaticMesh(ArrowBar);Arrows->SetMaterial(0,ArrowMaterial);
  Arrows->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Arrows->SetGenerateOverlapEvents(false);Arrows->SetCanEverAffectNavigation(false);
  Arrows->SetCastShadow(false);Arrows->bReceivesDecals=false;
  Arrows->SetCustomDepthStencilValue(128+Visual.ColorIndex);
  Arrows->SetVisibility(Visible);
  Arrows->SetRenderCustomDepth(true);Arrows->RegisterComponent();Visual.Arrows=Arrows;
 }
 if (!Arrows) return;
 Arrows->ClearInstances();
 TArray<FTransform> Bars;
 float UntilNext=100.f;
 for (int32 I=1;I<Route.Num();++I)
 {
  const FVector Delta=Route[I]-Route[I-1];const float Length=Delta.Size2D();
  if (Length<1.f) continue;
  const FVector Forward=Delta/Length,Side(-Forward.Y,Forward.X,0);
  for (;UntilNext<Length;UntilNext+=140.f)
  {
   const FVector Center=Route[I-1]+Forward*UntilNext;
   for (float Sign:{-1.f,1.f})
   {
    const FVector Tip=Center+Forward*15.f,Tail=Center-Forward*15.f+Side*Sign*22.f;
    Bars.Add(FTransform((Tip-Tail).Rotation(),(Tip+Tail)*.5f,FVector(FVector::Dist(Tip,Tail)/100.f,.045f,.008f)));
   }
  }
  UntilNext-=Length;
 }
 Arrows->AddInstances(Bars,false,true);
}

void UWarehouseTransportVisualizer::EndPlay(const EEndPlayReason::Type Reason)
{
 for (const auto& Pair:OriginalDepth) if (auto* Part=Pair.Key.Get()) RestoreDepth(Part,Pair.Value);
 OriginalDepth.Reset();
 for (const auto& Pair:Vehicles) if (auto* Arrows=Pair.Value.Arrows.Get()) Arrows->DestroyComponent();
 Vehicles.Reset();
 if (Effect) Effect->DestroyComponent();
 if (IsValid(DisplayActor)) DisplayActor->Destroy();
 Super::EndPlay(Reason);
}
