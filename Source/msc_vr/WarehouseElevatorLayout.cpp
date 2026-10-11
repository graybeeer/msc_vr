#include "WarehouseElevatorLayout.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseElevator.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
 const FVector2D SavedCentre(1000,1600);
 const FBox2D GroundOpening(FVector2D(859,1424),FVector2D(1141,1776));
 const FBox2D UpperOpening(FVector2D(859,1424),FVector2D(1141,1800));
 struct FSource
 {
  TWeakObjectPtr<AStaticMeshActor> Actor;
  TWeakObjectPtr<UStaticMeshComponent> Mesh;
  FBox Bounds;
  int32 Floor=0;
  bool Visible=true,Hidden=false,Collision=true;
  ECollisionEnabled::Type Mode=ECollisionEnabled::QueryAndPhysics;
 };
 struct FRegion
 {
  FBox2D Rect;
  double Bottom=0,Top=0;
  int32 Source=0;
  int32 Floor=0;
 };
 struct FState
 {
  TArray<FSource> Sources;
  TArray<FRegion> Base;
  TArray<TWeakObjectPtr<AStaticMeshActor>> Replacements;
 };
 TMap<TWeakObjectPtr<UWorld>,FState> States;

 FString SourceName(const AActor* Actor)
 {
  for (FName Tag:Actor->Tags)
  {
   const FString Value=Tag.ToString();
   if (Value.StartsWith(TEXT("ElevatorFloorSource:"))) return Value.Mid(20);
  }
  return Actor->GetActorNameOrLabel();
 }
 bool SourceFloor(const FString& Name,int32& Floor)
 {
  if (Name==TEXT("Floor") || Name==TEXT("WH_InboundFloor") || Name==TEXT("MZ_GroundRearWest") ||
      Name==TEXT("MZ_GroundRearEast") || Name==TEXT("MZ_GroundRear")) { Floor=0;return true; }
  for (int32 Level=2;Level<=3;++Level)
   for (const TCHAR* Side:{TEXT("Left"),TEXT("Right"),TEXT("RearWest"),TEXT("RearEast"),TEXT("North"),TEXT("South")})
    if (Name==FString::Printf(TEXT("MZ_L%d_Deck_%s"),Level,Side)) { Floor=Level-1;return true; }
  return false;
 }
 double Area(const FBox2D& Rect)
 { const FVector2D Size=Rect.GetSize();return FMath::Max(0.,Size.X)*FMath::Max(0.,Size.Y); }
 FBox2D Intersection(const FBox2D& A,const FBox2D& B)
 {
  const FVector2D Low(FMath::Max(A.Min.X,B.Min.X),FMath::Max(A.Min.Y,B.Min.Y));
  const FVector2D High(FMath::Min(A.Max.X,B.Max.X),FMath::Min(A.Max.Y,B.Max.Y));
  return High.X>Low.X && High.Y>Low.Y ? FBox2D(Low,High) : FBox2D(ForceInit);
 }
 TArray<FBox2D> Subtract(const FBox2D& Rect,const FBox2D& Hole)
 {
  const FBox2D Cut=Intersection(Rect,Hole);
  if (!Cut.bIsValid) return {Rect};
  TArray<FBox2D> Parts;
  auto Add=[&](double X0,double Y0,double X1,double Y1)
  { if (X1-X0>.01 && Y1-Y0>.01) Parts.Emplace(FVector2D(X0,Y0),FVector2D(X1,Y1)); };
  // Nonoverlapping rectangle difference, at most four pieces per source region.
  Add(Rect.Min.X,Rect.Min.Y,Cut.Min.X,Rect.Max.Y);
  Add(Cut.Max.X,Rect.Min.Y,Rect.Max.X,Rect.Max.Y);
  Add(Cut.Min.X,Rect.Min.Y,Cut.Max.X,Cut.Min.Y);
  Add(Cut.Min.X,Cut.Max.Y,Cut.Max.X,Rect.Max.Y);
  return Parts;
 }
 bool Covered(const FState& State,int32 Floor,const FBox2D& Rect)
 {
  double Sum=0;
  for (const FRegion& Region:State.Base) if (Region.Floor==Floor)
  { const FBox2D Shared=Intersection(Rect,Region.Rect);if (Shared.bIsValid) Sum+=Area(Shared); }
  return Area(Rect)>0 && FMath::Abs(Sum-Area(Rect))<1.;
 }
 bool Capture(UWorld* World,FState& State)
 {
  for (TActorIterator<AStaticMeshActor> It(World);It;++It)
  {
   int32 Floor=0;
   const bool Tagged=It->ActorHasTag(TEXT("ElevatorLayoutFloor"));
   if (!Tagged && !SourceFloor(SourceName(*It),Floor)) continue;
   auto* Mesh=It->GetStaticMeshComponent();
   if (!Mesh || !Mesh->GetStaticMesh() || !Mesh->GetUpVector().Equals(FVector::UpVector,.001)) return false;
   const FBox Bounds=Mesh->Bounds.GetBox();
   if (Tagged) Floor=FMath::RoundToInt(Bounds.Max.Z/400.);
   if (Floor<0 || Floor>2) return false;
   if (FMath::Abs(Bounds.Max.Z-Floor*400.)>.1 || Bounds.GetSize().Z<=0) return false;
   FSource Source;Source.Actor=*It;Source.Mesh=Mesh;Source.Bounds=Bounds;Source.Floor=Floor;
   Source.Visible=Mesh->IsVisible();Source.Hidden=It->IsHidden();Source.Collision=It->GetActorEnableCollision();Source.Mode=Mesh->GetCollisionEnabled();
   const int32 Index=State.Sources.Add(Source);
   State.Base.Add({FBox2D(FVector2D(Bounds.Min),FVector2D(Bounds.Max)),Bounds.Min.Z,Bounds.Max.Z,Index,Floor});
  }
  if (State.Sources.Num()!=17) return false; // 5 ground pieces, 6 gallery pieces per upper floor.
  for (int32 Floor=0;Floor<3;++Floor)
  {
   const int32 Source=State.Sources.IndexOfByPredicate([Floor](const FSource& S){return S.Floor==Floor;});
   if (Source==INDEX_NONE) return false;
   const FBox& Bounds=State.Sources[Source].Bounds;
   State.Base.Add({Floor==0 ? GroundOpening : UpperOpening,Bounds.Min.Z,Bounds.Max.Z,Source,Floor});
  }
  return true;
 }
 void Destroy(TArray<TWeakObjectPtr<AStaticMeshActor>>& Actors)
 { for (auto Actor:Actors) if (Actor.IsValid()) Actor->Destroy();Actors.Reset(); }
 void RemoveRuntimeMembers(AWarehouseDamageSystem* Strength)
 {
  if (!Strength) return;
  for (auto& Spec:Strength->Objects)
   for (int32 I=Spec.Members.Num()-1;I>=0;--I)
    if (Spec.Members[I] && Spec.Members[I]->ActorHasTag(TEXT("ElevatorRuntimeFloor")))
    {
     Spec.Members.RemoveAt(I);
     if (Spec.PhysicsMeshes.IsValidIndex(I)) Spec.PhysicsMeshes.RemoveAt(I);
    }
 }
}

bool WarehouseElevatorLayout::SetOpening(AActor* Context,FVector Delta,bool ValidatePlacement)
{
 UWorld* World=IsValid(Context) ? Context->GetWorld() : nullptr;
 if (!World || !World->IsGameWorld() || Delta.ContainsNaN() || FMath::Abs(Delta.Z)>.01) return false;
 const TWeakObjectPtr<UWorld> Key(World);
 FState* State=States.Find(Key);
 FState Initial;
 if (!State)
 {
  if (!Capture(World,Initial)) return false;
  State=&Initial;
 }
 const FVector2D Shift(Delta.X,Delta.Y);
 // Keep the same U-gallery/atrium and require a full supported AGV approach
 // (350cm wide, from its 5m wait pose to the door), on every served floor.
 if (ValidatePlacement)
 {
  const FBox2D Landing(SavedCentre+Shift+FVector2D(-175,-625),SavedCentre+Shift+FVector2D(175,-190));
  for (int32 Floor=0;Floor<3;++Floor)
  {
   const FBox2D Old=Floor==0 ? GroundOpening : UpperOpening;
   const FBox2D Hole(Old.Min+Shift,Old.Max+Shift);
   if (!Covered(*State,Floor,Hole) || !Covered(*State,Floor,Landing)) return false;
  }
  // The presently parked platform cannot reveal a beam crossing its future
  // stroke. Check its entire native-sized travel prism without excluding real
  // beams, columns, rails, furniture or goods at the proposed shaft position.
  FCollisionQueryParams Params(SCENE_QUERY_STAT(ElevatorLayoutStroke),false,Context);
  for (const FSource& Source:State->Sources) if (Source.Actor.IsValid()) Params.AddIgnoredActor(Source.Actor.Get());
  for (auto Actor:State->Replacements) if (Actor.IsValid()) Params.AddIgnoredActor(Actor.Get());
  for (TActorIterator<AActor> It(World);It;++It)
   if (It->IsA<AWarehouseElevator>() || It->ActorHasTag(TEXT("LayoutGroup:Elevator")) || It->ActorHasTag(TEXT("ElevatorLayoutPit"))) Params.AddIgnoredActor(*It);
  if (World->OverlapBlockingTestByChannel(FVector(SavedCentre.X+Shift.X,SavedCentre.Y+Shift.Y,395),FQuat::Identity,ECC_Visibility,
      FCollisionShape::MakeBox(FVector(119.5,174.5,404.9)),Params)) return false;
 }
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
 if (!Cube) return false;
 TArray<TWeakObjectPtr<AStaticMeshActor>> Next;
 TArray<TPair<AStaticMeshActor*,AStaticMeshActor*>> Members;
 TArray<FBox2D> ColumnOpenings;
 for (TActorIterator<AWarehouseElevator> Lift(World);Lift;++Lift)
 {
  TArray<UStaticMeshComponent*> Parts;Lift->GetComponents(Parts);
  for (auto* Part:Parts) if (Part->GetName().StartsWith(TEXT("Column_")))
  {
   const FBox Box=Part->Bounds.GetBox();
   ColumnOpenings.Emplace(FVector2D(Box.Min)-FVector2D(.05,.05),FVector2D(Box.Max)+FVector2D(.05,.05));
  }
 }
 for (const FRegion& Region:State->Base)
 {
  const FSource& Source=State->Sources[Region.Source];
  auto* Original=Source.Mesh.Get();
  if (!Original) { Destroy(Next);return false; }
  const FBox2D Old=Region.Floor==0 ? GroundOpening : UpperOpening;
  TArray<FBox2D> Pieces=Subtract(Region.Rect,FBox2D(Old.Min+Shift,Old.Max+Shift));
  // The fixed shaft posts pass through upper slabs outside the moving deck's
  // opening. Cut their actual footprints too; retain the door landing between
  // the posts rather than making one oversized rectangular pit.
  if (Region.Floor>0) for (const FBox2D& Column:ColumnOpenings)
  {
   TArray<FBox2D> Remaining;for (const FBox2D& Piece:Pieces) Remaining.Append(Subtract(Piece,Column));
   Pieces=MoveTemp(Remaining);
  }
  for (const FBox2D& Rect:Pieces)
  {
   const FVector2D Centre=Rect.GetCenter(),Size=Rect.GetSize();
   FActorSpawnParameters SpawnParams;SpawnParams.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
   SpawnParams.ObjectFlags|=RF_Transient;
   auto* Actor=World->SpawnActor<AStaticMeshActor>(FVector(Centre.X,Centre.Y,(Region.Bottom+Region.Top)*.5),FRotator::ZeroRotator,SpawnParams);
   if (!Actor) { Destroy(Next);return false; }
   Next.Add(Actor);
   Members.Emplace(Actor,Source.Actor.Get());
   Actor->Tags={TEXT("LayoutFixed"),TEXT("ElevatorRuntimeFloor"),FName(*FString::Printf(TEXT("WarehouseFloor%d"),Region.Floor))};
   if (Region.Floor==0) Actor->Tags.Add(TEXT("ObserverArea"));
   auto* Mesh=Actor->GetStaticMeshComponent();
   Mesh->SetMobility(EComponentMobility::Movable);
   Mesh->SetStaticMesh(Cube);
   Actor->SetActorScale3D(FVector(Size.X,Size.Y,Region.Top-Region.Bottom)/100.);
   Mesh->SetMaterial(0,Original->GetMaterial(0));
   Mesh->SetCollisionProfileName(Original->GetCollisionProfileName());
   Mesh->SetCollisionResponseToChannels(Original->GetCollisionResponseToChannels());
   Mesh->SetCollisionEnabled(Source.Mode);
   Actor->SetActorEnableCollision(Source.Collision);
   if (const auto* Body=Original->GetBodyInstance()) Mesh->SetPhysMaterialOverride(Body->GetSimplePhysicalMaterial());
   Mesh->SetMobility(EComponentMobility::Static);
  }
 }
 // New parts exist before the previous floor is removed; the transaction runs
 // while layout mode pauses simulation, so no gravity frame sees a missing slab.
 if (State==&Initial) State=&States.Add(Key,MoveTemp(Initial));
 for (const FSource& Source:State->Sources)
 {
  if (auto* Actor=Source.Actor.Get()) { Actor->SetActorHiddenInGame(true);Actor->SetActorEnableCollision(false); }
  if (auto* Mesh=Source.Mesh.Get()) { Mesh->SetVisibility(false);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision); }
 }
 Destroy(State->Replacements);State->Replacements=MoveTemp(Next);
 if (auto* Strength=AWarehouseDamageSystem::Find(Context))
 {
  RemoveRuntimeMembers(Strength);
  // Replacement floor stays part of the original structural strength profile.
  // Existing damage/capacity values survive the native contact lookup rebuild.
  for (const auto& Pair:Members)
   for (auto& Spec:Strength->Objects) if (Spec.Members.Contains(Pair.Value))
   {
    Spec.PhysicsMeshes.SetNum(Spec.Members.Num());
    Spec.Members.Add(Pair.Key);Spec.PhysicsMeshes.Add(nullptr);break;
   }
  for (const auto& Pair:Members) Strength->RegisterPhysicalContacts(Pair.Key);
  Strength->RefreshLayoutBindings();
 }
 UE_LOG(LogTemp,Log,TEXT("ELEVATOR_LAYOUT_FLOORS delta=(%.0f,%.0f) pieces=%d; atrium and source materials preserved"),Delta.X,Delta.Y,State->Replacements.Num());
 return true;
}

void WarehouseElevatorLayout::RestoreInitial(AActor* Context)
{
 if (!IsValid(Context) || !Context->GetWorld()) return;
 const TWeakObjectPtr<UWorld> Key(Context->GetWorld());
 if (FState* State=States.Find(Key))
 {
  for (const FSource& Source:State->Sources)
  {
   if (auto* Actor=Source.Actor.Get()) { Actor->SetActorHiddenInGame(Source.Hidden);Actor->SetActorEnableCollision(Source.Collision); }
   if (auto* Mesh=Source.Mesh.Get()) { Mesh->SetVisibility(Source.Visible);Mesh->SetCollisionEnabled(Source.Mode); }
  }
  Destroy(State->Replacements);States.Remove(Key);
  if (auto* Strength=AWarehouseDamageSystem::Find(Context))
  { RemoveRuntimeMembers(Strength);Strength->RefreshLayoutBindings(); }
 }
}
