#include "WarehouseChargerWiring.h"
#include "WarehousePerformance.h"
#include "WarehouseChargingStation.h"
#include "WarehousePhysics.h"
#include "Agv/AgvShadowSensors.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
 constexpr double Radius=1.2,WallOffset=2.;
 struct FWall { FVector A,B; };
 struct FPanel { AActor* Actor;UStaticMeshComponent* Mesh; };
 bool Active(const AActor* Actor)
 { return IsValid(Actor) && !Actor->IsActorBeingDestroyed() && Actor->GetActorEnableCollision(); }
 bool PhysicalStructurePresent(const AActor* Actor)
 { return IsValid(Actor) && !Actor->IsActorBeingDestroyed() && Actor->GetActorEnableCollision(); }
 bool IsPanel(const UStaticMeshComponent* Part)
 { return Part && Part->GetStaticMesh() && Part->GetStaticMesh()->GetName()==TEXT("SM_WS_DistributionCabinet"); }
 bool IsArchitecture(const AActor* Actor)
 {
  return Actor && (Actor->ActorHasTag(TEXT("ObserverFadeWall")) || Actor->ActorHasTag(TEXT("WarehouseMapStructure")) ||
   Actor->ActorHasTag(TEXT("WarehouseServices")) || Actor->ActorHasTag(TEXT("LayoutType:Rack")));
 }
 // Construction sees fixed architecture, not an instantaneous passing vehicle or parcel.
 bool ClearSegment(UWorld* World,const FVector& A,const FVector& B,const AWarehouseChargingStation* Charger,const AActor* Panel)
 {
  if (A.Equals(B,.1)) return true;
  FCollisionQueryParams Params(SCENE_QUERY_STAT(ChargerWiring),true);
  Params.AddIgnoredActor(Charger);Params.AddIgnoredActor(Panel);
  TArray<FHitResult> Hits;
  World->SweepMultiByObjectType(Hits,A,B,FQuat::Identity,FCollisionObjectQueryParams::AllObjects,
   FCollisionShape::MakeSphere(float(Radius+.4)),Params);
  for (const auto& Hit:Hits)
  {
   const auto* Part=Hit.GetComponent();const auto* Actor=Hit.GetActor();
   if (!PhysicalStructurePresent(Actor) || Actor->ActorHasTag(TEXT("ChargerSupplyCable")) || !WarehousePhysics::IsPhysicalContact(Part)) continue;
   if (IsArchitecture(Actor) || Part->Mobility==EComponentMobility::Static) return false;
  }
  return true;
 }
 FVector Project(const FVector& P,const FWall& Wall)
 { return FMath::ClosestPointOnSegment(P,Wall.A,Wall.B); }
 void Append(TArray<FVector>& Points,const FVector& Point)
 { if (Points.IsEmpty() || !Points.Last().Equals(Point,.1)) Points.Add(Point); }
 // Actual local wall faces, including rotated rectangular walls. Door gaps do
 // not become walls: only pieces physically spanning this elevation contribute.
 TArray<FWall> WallFaces(const TArray<UStaticMeshComponent*>& Walls,const FVector& Interior,double Z)
 {
  TArray<FWall> Result;
  for (const auto* Part:Walls)
  {
   const FBox World=Part->Bounds.GetBox();
   if (Z<World.Min.Z+Radius+.1 || Z>World.Max.Z-Radius-.1) continue;
   FVector Min,Max;Part->GetLocalBounds(Min,Max);const auto& Pose=Part->GetComponentTransform();
   const FVector Size=(Max-Min)*Pose.GetScale3D().GetAbs();
   const bool AlongX=Size.X>=Size.Y;
   if (FMath::Max(Size.X,Size.Y)<100 || FMath::Min(Size.X,Size.Y)>200) continue;
   FVector Normal=Pose.TransformVectorNoScale(AlongX ? FVector::YAxisVector : FVector::XAxisVector).GetSafeNormal2D();
   const FVector Centre=Pose.TransformPosition((Min+Max)*.5);
   const double Sign=FVector::DotProduct(Interior-Centre,Normal)>=0 ? 1. : -1.;Normal*=Sign;
   FVector A=(Min+Max)*.5,B=A;
   if (AlongX) { A.X=Min.X;B.X=Max.X;A.Y=B.Y=Sign>0 ? Max.Y : Min.Y; }
   else { A.Y=Min.Y;B.Y=Max.Y;A.X=B.X=Sign>0 ? Max.X : Min.X; }
   A=Pose.TransformPosition(A)+Normal*WallOffset;B=Pose.TransformPosition(B)+Normal*WallOffset;
   // Stop just short of wall ends: the neighboring wall has finite thickness.
   // Short clear bridges join these inset corner points along the floor.
   const FVector Along=(B-A).GetSafeNormal2D();const double Inset=FMath::Min(65.,FVector::Distance(A,B)*.2);
   A+=Along*Inset;B-=Along*Inset;
   A.Z=B.Z=Z;Result.Add({A,B});
   if (Result.Num()>=32) break; // Bounded installation planning even for imported decorative shells.
  }
  return Result;
 }
 struct FNode { FVector P;int32 Wall=INDEX_NONE; };
 bool HorizontalRoute(UWorld* World,const FVector& A,const FVector& B,const TArray<FWall>& Walls,
  const AWarehouseChargingStation* Charger,const AActor* Panel,bool AllowInteriorSpan,TArray<FVector>& Route,double& Cost)
 {
  TArray<FNode> Nodes={{A,INDEX_NONE},{B,INDEX_NONE}};
  for (int32 I=0;I<Walls.Num();++I)
  {
   const auto& Wall=Walls[I];Nodes.Add({Wall.A,I});Nodes.Add({Wall.B,I});
   Nodes.Add({Project(A,Wall),I});Nodes.Add({Project(B,Wall),I});
  }
  // Visibility edges permit short corner bridges and a floor feeder from an
  // island charger. Wall-edge runs are preferred to interior floor spans.
  const int32 N=Nodes.Num();
  const int32 Pass=AllowInteriorSpan ? 1 : 0;
   TArray<double> Distance;Distance.Init(MAX_dbl,N);TArray<int32> Parent;Parent.Init(INDEX_NONE,N);
   TArray<bool> Done;Done.Init(false,N);Distance[0]=0;
   for (int32 Count=0;Count<N;++Count)
   {
    int32 U=INDEX_NONE;for (int32 I=0;I<N;++I) if (!Done[I] && (U==INDEX_NONE || Distance[I]<Distance[U])) U=I;
    if (U==INDEX_NONE || Distance[U]==MAX_dbl) break;
    if (U==1) break;Done[U]=true;
    for (int32 V=0;V<N;++V) if (!Done[V] && U!=V)
    {
     const double Length=FVector::Distance(Nodes[U].P,Nodes[V].P);
     const bool OnWall=Nodes[U].Wall!=INDEX_NONE && Nodes[U].Wall==Nodes[V].Wall;
     const bool Corner=Nodes[U].Wall!=INDEX_NONE && Nodes[V].Wall!=INDEX_NONE && Length<140;
     // First try perpendicular feeders and a continuous wall route. Only if
     // that is unavailable permit a clear floor span through the interior.
     if (Pass==0 && ((U==0 && (V<2 || (V-2)%4!=2)) ||
       (V==1 && (U<2 || (U-2)%4!=3)) || (U>=2 && V>=2 && !OnWall && !Corner))) continue;
     if (Pass==0 && U==0 && Nodes[V].Wall!=INDEX_NONE)
     {
      const FWall& Wall=Walls[Nodes[V].Wall];
      if (FMath::Abs(FVector::DotProduct(A-Nodes[V].P,(Wall.B-Wall.A).GetSafeNormal()))>1.) continue;
     }
     if (Pass==0 && V==1 && Nodes[U].Wall!=INDEX_NONE)
     {
      const FWall& Wall=Walls[Nodes[U].Wall];
      if (FMath::Abs(FVector::DotProduct(B-Nodes[U].P,(Wall.B-Wall.A).GetSafeNormal()))>1.) continue;
     }
     const double Edge=Length*(OnWall || Corner ? 1. : 5.);
     if (Distance[U]+Edge>=Distance[V] || !ClearSegment(World,Nodes[U].P,Nodes[V].P,Charger,Panel)) continue;
     Distance[V]=Distance[U]+Edge;Parent[V]=U;
    }
   }
   if (Distance[1]==MAX_dbl) return false;
   TArray<FVector> Reverse;for (int32 I=1;I!=INDEX_NONE;I=Parent[I]) Reverse.Add(Nodes[I].P);
   for (int32 I=Reverse.Num()-1;I>=0;--I) Append(Route,Reverse[I]);Cost=Distance[1];return true;
 }
 FVector ChargerPort(AWarehouseChargingStation* Charger)
 {
  // Rear of the real cabinet, away from the contact plate and docking vehicle.
  const auto* Part=FindObject<UStaticMeshComponent>(Charger,TEXT("Cabinet"));
  if (!Part) return Charger->GetActorTransform().TransformPosition(FVector(-144,0,90));
  FVector Min,Max;Part->GetLocalBounds(Min,Max);const FTransform Pose=Part->GetComponentTransform();
  FBox Local(ForceInit);
  for (int32 I=0;I<8;++I) Local+=Charger->GetActorTransform().InverseTransformPosition(Pose.TransformPosition(
   FVector(I&1 ? Max.X:Min.X,I&2 ? Max.Y:Min.Y,I&4 ? Max.Z:Min.Z)));
  return Charger->GetActorTransform().TransformPosition(
   FVector(Local.Min.X-Radius-.8,Local.GetCenter().Y,Local.Min.Z+10));
 }
 bool SupportedFloor(UWorld* World,const FVector& Point,const AWarehouseChargingStation* Charger,const AActor* Panel)
 {
  FCollisionQueryParams Query(SCENE_QUERY_STAT(ChargerCableFloor),true);
  Query.AddIgnoredActor(Charger);Query.AddIgnoredActor(Panel);
  TArray<FHitResult> Hits;
  World->LineTraceMultiByObjectType(Hits,Point+FVector(0,0,1),Point-FVector(0,0,Radius+2),FCollisionObjectQueryParams::AllObjects,Query);
  for (const auto& Hit:Hits)
   if (Hit.ImpactNormal.Z>.9 && WarehousePhysics::IsPhysicalContact(Hit.GetComponent()) &&
       !Hit.GetActor()->ActorHasTag(TEXT("ChargerSupplyCable"))) return true;
  return false;
 }
 bool MakeRoute(UWorld* World,AWarehouseChargingStation* Charger,const FPanel& Panel,
  const TArray<UStaticMeshComponent*>& Walls,const FVector& Interior,TArray<FVector>& Best)
 {
  const FBox Box=Panel.Mesh->Bounds.GetBox();const FVector End(Box.GetCenter().X,Box.GetCenter().Y,Box.Min.Z+.2);
  const double Floor=FMath::FloorToDouble((Charger->GetActorLocation().Z+20.)/400.)*400.;
  // All horizontal conductors lie just above the actual floor. Only cabinet
  // terminal drops are vertical; never fall back to a floating overhead span.
  for (bool InteriorSpan:{false,true})
  {
   const FVector Start=ChargerPort(Charger);
   const double Z=Floor+Radius+.9;
   FVector HighStart(Start.X,Start.Y,Z),HighEnd(End.X,End.Y,Z);
   if (!ClearSegment(World,Start,HighStart,Charger,Panel.Actor) || !ClearSegment(World,HighEnd,End,Charger,Panel.Actor)) continue;
   auto Faces=WallFaces(Walls,Interior,Z);TArray<FVector> Path;double Cost;
   if (!HorizontalRoute(World,HighStart,HighEnd,Faces,Charger,Panel.Actor,InteriorSpan,Path,Cost)) continue;
   bool Grounded=true;
   for (int32 I=1;I<Path.Num() && Grounded;++I)
   {
    const int32 Samples=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(Path[I-1],Path[I])/60.));
    for (int32 Sample=0;Sample<=Samples;++Sample)
     if (!SupportedFloor(World,FMath::Lerp(Path[I-1],Path[I],double(Sample)/Samples),Charger,Panel.Actor)) {Grounded=false;break;}
   }
   if (!Grounded) continue;
   Best.Reset();Append(Best,Start);for (const auto& P:Path) Append(Best,P);Append(Best,End);
   for (int32 I=Best.Num()-2;I>0;--I)
    if (FVector::DotProduct((Best[I]-Best[I-1]).GetSafeNormal(),(Best[I+1]-Best[I]).GetSafeNormal())>.99999) Best.RemoveAt(I);
   return Best.Num()>=2;
  }
  return false;
 }
}

UWarehouseChargerWiring::UWarehouseChargerWiring()
{
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder"));
 static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Material(TEXT("/Game/Warehouse/AGV/Materials/M_Graphite_powder-coated_steel"));
 CableMesh=Cylinder.Object;CableMaterial=Material.Get();
}
UWarehouseChargerWiring* UWarehouseChargerWiring::Find(const UObject* Context)
{
 UWorld* World=GEngine ? GEngine->GetWorldFromContextObject(Context,EGetWorldErrorMode::ReturnNull) : nullptr;
 return World && World->IsGameWorld() && !World->bIsTearingDown ? World->GetSubsystem<UWarehouseChargerWiring>() : nullptr;
}
void UWarehouseChargerWiring::RequestRefresh(const UObject* Context)
{
 if (auto* System=Find(Context);System && !System->bSuspended && !System->PendingRefresh.IsValid())
  System->PendingRefresh=System->GetWorld()->GetTimerManager().SetTimerForNextTick(System,&UWarehouseChargerWiring::Rebuild);
}
void UWarehouseChargerWiring::RefreshConnections(const UObject* Context)
{ if (auto* System=Find(Context)) System->Rebuild(); }
void UWarehouseChargerWiring::SetSuspended(const UObject* Context,bool Suspended)
{
 if (auto* System=Find(Context))
 {
  if (System->bSuspended==Suspended) return;
  System->bSuspended=Suspended;
  if (Suspended) { System->GetWorld()->GetTimerManager().ClearTimer(System->PendingRefresh);System->Clear(); }
  else System->Rebuild();
 }
}
void UWarehouseChargerWiring::Clear()
{
 for (AActor* Actor:GeneratedCables) if (IsValid(Actor)) Actor->Destroy();GeneratedCables.Reset();
}
void UWarehouseChargerWiring::Deinitialize()
{
 GetWorld()->GetTimerManager().ClearTimer(PendingRefresh);Clear();Super::Deinitialize();
}
void UWarehouseChargerWiring::Rebuild()
{
 GetWorld()->GetTimerManager().ClearTimer(PendingRefresh);PendingRefresh.Invalidate();
 if (bSuspended || GetWorld()->bIsTearingDown) return;
 CSV_SCOPED_TIMING_STAT(Warehouse,ChargerWiring);
 ++RebuildCount;Clear();
 TArray<FPanel> Panels;TArray<UStaticMeshComponent*> Walls;TArray<UAgvShadowSensorsComponent*> SensorMaps;
 for (TActorIterator<AActor> It(GetWorld());It;++It)
 {
  AActor* Actor=*It;if (!PhysicalStructurePresent(Actor)) continue;
  if (auto* Sensor=Actor->FindComponentByClass<UAgvShadowSensorsComponent>()) SensorMaps.Add(Sensor);
  TInlineComponentArray<UStaticMeshComponent*> Parts(Actor);
  for (auto* Part:Parts) if (WarehousePhysics::IsPhysicalContact(Part))
  {
   if (IsPanel(Part) && Active(Actor)) Panels.Add({Actor,Part});
   if (Actor->ActorHasTag(TEXT("ObserverFadeWall"))) { Walls.Add(Part); }
  }
 }
 for (TActorIterator<AWarehouseChargingStation> It(GetWorld());It;++It)
 {
  auto* Charger=*It;Charger->SupplyCabinet=nullptr;Charger->SupplyCableRoute.Reset();
  if (!Active(Charger)) { Charger->SupplyCableStatus=TEXT("충전기 비활성 · 전선 연결 없음");continue; }
  // Observer floor/roof hiding is cosmetic. Retired building services disable
  // collision, so only physical presence affects the electrical installation.
  const auto* Cabinet=FindObject<UStaticMeshComponent>(Charger,TEXT("Cabinet"));
  const FVector SourceCentre=Cabinet ? Cabinet->Bounds.Origin : Charger->GetActorLocation();
  const FPanel* Nearest=nullptr;double Best=MAX_dbl;
  for (const auto& Panel:Panels)
  {
   const double Distance=FVector::DistSquared(SourceCentre,Panel.Mesh->Bounds.Origin);
   if (Distance<Best-.01 || (FMath::IsNearlyEqual(Distance,Best,.01) && Nearest && Panel.Actor->GetPathName()<Nearest->Actor->GetPathName()))
   { Nearest=&Panel;Best=Distance; }
  }
  if (!Nearest) { Charger->SupplyCableStatus=TEXT("배전함 없음 · 전선 연결 대기");continue; }
  Charger->SupplyCabinet=Nearest->Actor;
  if (!MakeRoute(GetWorld(),Charger,*Nearest,Walls,Charger->GetActorLocation(),Charger->SupplyCableRoute))
  { Charger->SupplyCableStatus=TEXT("연속된 바닥 배선 경로 없음 · 배치 확인 필요");continue; }
  FActorSpawnParameters Params;Params.Owner=Charger;Params.ObjectFlags|=RF_Transient;
  AActor* Cable=GetWorld()->SpawnActor<AActor>(FVector::ZeroVector,FRotator::ZeroRotator,Params);
  if (!Cable) { Charger->SupplyCableRoute.Reset();Charger->SupplyCableStatus=TEXT("전선 생성 실패");continue; }
  Cable->Tags={TEXT("LayoutFixed"),TEXT("WarehouseVisualOnly"),TEXT("ChargerSupplyCable"),
   FName(*FString::Printf(TEXT("WarehouseFloor%d"),FMath::Clamp(FMath::FloorToInt((Charger->GetActorLocation().Z+20)/400.),0,3)))};
  auto* Root=NewObject<USceneComponent>(Cable,TEXT("CableRoot"));Cable->SetRootComponent(Root);Root->SetMobility(EComponentMobility::Static);Root->RegisterComponent();
  const auto& Points=Charger->SupplyCableRoute;
  for (int32 I=1;I<Points.Num();++I)
  {
   const FVector Delta=Points[I]-Points[I-1];if (Delta.Size()<.1) continue;
   // Supply wires are visual only. Their bounds must never reserve the bay or
   // enter sensor, physical-contact or layout-obstruction queries.
   auto* Segment=NewObject<UStaticMeshComponent>(Cable,*FString::Printf(TEXT("Conduit%d"),I));Segment->SetupAttachment(Root);
   Segment->SetMobility(EComponentMobility::Movable);Segment->SetStaticMesh(CableMesh);
   if (CableMaterial) Segment->SetMaterial(0,CableMaterial);
   Segment->SetWorldTransform(FTransform(FRotationMatrix::MakeFromZ(Delta).ToQuat(),(Points[I]+Points[I-1])*.5,FVector(Radius/50.,Radius/50.,Delta.Size()/100.)));
   Segment->SetCollisionProfileName(TEXT("NoCollision"));Segment->SetCollisionResponseToAllChannels(ECR_Ignore);
   Segment->SetGenerateOverlapEvents(false);Segment->SetCanEverAffectNavigation(false);
   Segment->SetCastShadow(false);Segment->SetCullDistance(15000);Segment->RegisterComponent();
   Segment->SetMobility(EComponentMobility::Static);
  }
  GeneratedCables.Add(Cable);
  Charger->SupplyCableStatus=FString::Printf(TEXT("%s 연결 · 바닥 전선관"),*Nearest->Actor->GetActorNameOrLabel());
 }
 // Only invalidate the fixed map. Sampling remains on the existing one-second
 // autonomous schedule, and remote control performs no sensor/AI work.
 for (auto* Sensor:SensorMaps) Sensor->MarkBuildingMapDirty();
}
