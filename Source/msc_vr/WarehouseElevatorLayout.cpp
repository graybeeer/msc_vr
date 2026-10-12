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
 FBox2D Opening(const AWarehouseElevator* Lift,int32 Floor,bool Initial=false)
 {
  const FVector Position=Initial ? Lift->InitialShaftPosition : Lift->GetActorLocation();
  const FVector2D Centre=FVector2D(Position)+(Floor==0 ? FVector2D::ZeroVector : Lift->UpperOpeningOffsetCm);
  const FVector2D Half=(Floor==0 ? Lift->GroundOpeningSizeCm : Lift->UpperOpeningSizeCm)*.5;
  return FBox2D(Centre-Half,Centre+Half);
 }
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
  if (State.Sources.IsEmpty()) return false;
  for (TActorIterator<AWarehouseElevator> Lift(World);Lift;++Lift) for (int32 Floor=0;Floor<3;++Floor)
  {
   const int32 Source=State.Sources.IndexOfByPredicate([Floor](const FSource& S){return S.Floor==Floor;});
   if (Source==INDEX_NONE) return false;
   const FBox& Bounds=State.Sources[Source].Bounds;
   TArray<FBox2D> Missing={Opening(*Lift,Floor,true)};
   for (const auto& Existing:State.Base) if (Existing.Floor==Floor)
   { TArray<FBox2D> Next;for (const auto& Piece:Missing) Next.Append(Subtract(Piece,Existing.Rect));Missing=MoveTemp(Next); }
   for (const auto& Rect:Missing) State.Base.Add({Rect,Bounds.Min.Z,Bounds.Max.Z,Source,Floor});
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
{ return !Delta.ContainsNaN() && FMath::Abs(Delta.Z)<.01 && Rebuild(Context,ValidatePlacement); }
bool WarehouseElevatorLayout::Initialize(AActor* Context)
{
 if (!IsValid(Context) || !Context->GetWorld() || !Context->GetWorld()->IsGameWorld()) return false;
 const TWeakObjectPtr<UWorld> Key(Context->GetWorld());if (States.Contains(Key)) return true;
 FState State;if (!Capture(Context->GetWorld(),State)) return false;States.Add(Key,MoveTemp(State));return true;
}
bool WarehouseElevatorLayout::Rebuild(AActor* Context,bool ValidatePlacement,const TArray<AActor*>& Removed)
{
 UWorld* World=IsValid(Context) ? Context->GetWorld() : nullptr;
 if (!World || !World->IsGameWorld() || !Initialize(Context)) return false;
 const TWeakObjectPtr<UWorld> Key(World);
 FState* State=States.Find(Key);
 TArray<AWarehouseElevator*> Lifts;TSet<FName> Groups;
 for (TActorIterator<AWarehouseElevator> It(World);It;++It) if (!Removed.Contains(*It))
 { Lifts.Add(*It);for (FName Tag:It->Tags) if (Tag.ToString().StartsWith(TEXT("LayoutGroup:"))) Groups.Add(Tag); }
 // Keep the same U-gallery/atrium and require a full supported AGV approach
 // (350cm wide, from its 5m wait pose to the door), on every served floor.
 if (ValidatePlacement)
 {
  for (AWarehouseElevator* Lift:Lifts) for (int32 Floor=0;Floor<3;++Floor)
  {
   const FVector2D Centre(Lift->GetActorLocation());
   const FBox2D Landing(Centre+FVector2D(-175,-625),Centre+FVector2D(175,-190));
   const FBox2D Hole=Opening(Lift,Floor);
   if (!Covered(*State,Floor,Hole) || !Covered(*State,Floor,Landing)) return false;
   for (AWarehouseElevator* Other:Lifts) if (Other!=Lift && Intersection(Hole,Opening(Other,Floor)).bIsValid) return false;
  }
  // The presently parked platform cannot reveal a beam crossing its future
  // stroke. Check its entire native-sized travel prism without excluding real
  // beams, columns, rails, furniture or goods at the proposed shaft position.
  FCollisionQueryParams Params(SCENE_QUERY_STAT(ElevatorLayoutStroke),false,Context);
  for (const FSource& Source:State->Sources) if (Source.Actor.IsValid()) Params.AddIgnoredActor(Source.Actor.Get());
  for (auto Actor:State->Replacements) if (Actor.IsValid()) Params.AddIgnoredActor(Actor.Get());
  for (TActorIterator<AActor> It(World);It;++It)
   if (It->IsA<AWarehouseElevator>() || It->Tags.ContainsByPredicate([&](FName Tag) { return Groups.Contains(Tag); }) || Removed.Contains(*It)) Params.AddIgnoredActor(*It);
  for (AWarehouseElevator* Lift:Lifts)
  {
   const FVector Centre=Lift->GetActorLocation()+FVector(0,0,395);
   const FVector Half(Lift->GroundOpeningSizeCm.X*.5-21.5,Lift->GroundOpeningSizeCm.Y*.5-1.5,404.9);
   if (World->OverlapBlockingTestByChannel(Centre,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeBox(Half),Params)) return false;
  }
 }
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
 if (!Cube) return false;
 TArray<TWeakObjectPtr<AStaticMeshActor>> Next;
 TArray<TPair<AStaticMeshActor*,AStaticMeshActor*>> Members;
 TArray<FBox2D> ColumnOpenings;
 for (AWarehouseElevator* Lift:Lifts)
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
  TArray<FBox2D> Pieces={Region.Rect};
  for (AWarehouseElevator* Lift:Lifts)
  { TArray<FBox2D> Remaining;for (const auto& Piece:Pieces) Remaining.Append(Subtract(Piece,Opening(Lift,Region.Floor)));Pieces=MoveTemp(Remaining); }
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
 UE_LOG(LogTemp,Log,TEXT("ELEVATOR_LAYOUT_FLOORS shafts=%d pieces=%d; atrium and source materials preserved"),Lifts.Num(),State->Replacements.Num());
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
