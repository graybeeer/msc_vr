#include "WarehouseObserverWallFade.h"
#include "WarehousePerformance.h"

#include "Camera/CameraComponent.h"
#include "Components/MeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
const FName FadeWallTag(TEXT("ObserverFadeWall"));
const FName OpacityParameter(TEXT("ObserverWallOpacity"));
constexpr float NearDistance=150.f;
constexpr float ForwardDistance=600.f;
constexpr float FadedOpacity=.2f;
constexpr float FadeSeconds=.15f;

bool VisibleWall(const UMeshComponent* Mesh)
{
 return IsValid(Mesh) && IsValid(Mesh->GetOwner()) &&
  Mesh->GetOwner()->ActorHasTag(FadeWallTag) && !Mesh->GetOwner()->IsHidden() &&
  Mesh->IsVisible() && !Mesh->bHiddenInGame;
}
}

UWarehouseObserverWallFade::UWarehouseObserverWallFade()
{
 PrimaryComponentTick.bCanEverTick=false;
 PaletteAsset=TSoftObjectPtr<UWarehouseObserverWallPalette>(FSoftObjectPath(
  TEXT("/Game/Warehouse/Materials/ObserverWalls/DA_ObserverWallPalette.DA_ObserverWallPalette")));
}

void UWarehouseObserverWallFade::Install(FWarehouseObserverWallFadeState& State)
{
 UMeshComponent* Mesh=State.Mesh.Get();
 if (!Mesh || !Palette) return;
 const int32 Count=Mesh->GetNumMaterials();
 State.Originals.SetNum(Count);
 State.Instances.SetNum(Count);
 bool Any=false;
 for (int32 Slot=0;Slot<Count;++Slot)
 {
  UMaterialInterface* Original=Mesh->GetMaterial(Slot);
  if (State.Originals[Slot]!=Original)
  {
   State.Originals[Slot]=Original;
   State.Instances[Slot]=nullptr;
  }
  UMaterialInterface* const* Variant=Palette->Materials.Find(Original);
  if (!Variant || !IsValid(*Variant)) continue;
  if (!State.Instances[Slot]) State.Instances[Slot]=UMaterialInstanceDynamic::Create(*Variant,this);
  if (UMaterialInstanceDynamic* Instance=State.Instances[Slot])
  {
   Instance->SetScalarParameterValue(OpacityParameter,State.Opacity);
   Mesh->SetMaterial(Slot,Instance);
   Any=true;
  }
 }
 State.bInstalled=Any;
}

void UWarehouseObserverWallFade::RestoreWall(FWarehouseObserverWallFadeState& State)
{
 if (State.bInstalled) if (UMeshComponent* Mesh=State.Mesh.Get())
  for (int32 Slot=0;Slot<State.Originals.Num();++Slot) Mesh->SetMaterial(Slot,State.Originals[Slot]);
 State.bInstalled=false;
 State.bWanted=false;
 State.Opacity=1.f;
}

void UWarehouseObserverWallFade::Restore()
{
 for (FWarehouseObserverWallFadeState& State:Walls) RestoreWall(State);
 Walls.RemoveAll([](const FWarehouseObserverWallFadeState& State){return !State.Mesh.IsValid();});
 LastUpdate=0.;
 NextMembership=0.;
}
UMaterialInterface* UWarehouseObserverWallFade::GetOriginalMaterial(const UMeshComponent* Mesh,int32 Slot) const
{
 for (const auto& State:Walls) if (State.Mesh.Get()==Mesh && State.bInstalled && State.Originals.IsValidIndex(Slot)) return State.Originals[Slot];
 return Mesh ? Mesh->GetMaterial(Slot) : nullptr;
}

void UWarehouseObserverWallFade::Update(APlayerController* PC, bool Enabled)
{
 CSV_SCOPED_TIMING_STAT(Warehouse,ObserverWallFade);
 if (!Enabled || !IsValid(PC) || !PC->GetWorld()) { Restore();return; }
 const double Now=FPlatformTime::Seconds();
 const float Dt=LastUpdate>0. ? static_cast<float>(FMath::Max(0.,Now-LastUpdate)) : 0.f;
 LastUpdate=Now;
 if (!Palette && Now>=NextPaletteLoad)
 {
  // The editor may create the palette after this class's CDO has already been loaded.
  Palette=PaletteAsset.LoadSynchronous();
  NextPaletteLoad=Now+1.;
 }
 if (!Palette) return;
 if (Now>=NextMembership)
 {
  NextMembership=Now+.1;
  FVector Eye;FRotator Rotation;
  AActor* View=PC->GetViewTarget();
  if (UCameraComponent* Camera=IsValid(View) ? View->FindComponentByClass<UCameraComponent>() : nullptr)
  {
   Eye=Camera->GetComponentLocation();Rotation=Camera->GetComponentRotation();
  }
  else PC->GetPlayerViewPoint(Eye,Rotation);
  const FVector Delta=Rotation.Vector()*ForwardDistance;
  for (FWarehouseObserverWallFadeState& State:Walls) State.bWanted=false;
  for (TActorIterator<AActor> It(PC->GetWorld());It;++It)
  {
   if (!It->ActorHasTag(FadeWallTag) || It->IsHidden()) continue;
   TInlineComponentArray<UMeshComponent*> Meshes(*It);
   for (UMeshComponent* Mesh:Meshes)
   {
    if (!VisibleWall(Mesh)) continue;
    const FBox Bounds=Mesh->Bounds.GetBox();
    if (Bounds.ComputeSquaredDistanceToPoint(Eye)>FMath::Square(NearDistance) ||
        !FMath::LineBoxIntersection(Bounds,Eye,Eye+Delta,Delta)) continue;
    FWarehouseObserverWallFadeState* State=Walls.FindByPredicate(
     [Mesh](const FWarehouseObserverWallFadeState& Saved){return Saved.Mesh==Mesh;});
    if (!State) { State=&Walls.AddDefaulted_GetRef();State->Mesh=Mesh; }
    State->bWanted=true;
   }
  }
 }
 for (FWarehouseObserverWallFadeState& State:Walls)
 {
  if (!VisibleWall(State.Mesh.Get())) { RestoreWall(State);continue; }
  if (State.bWanted && !State.bInstalled) Install(State);
  if (!State.bInstalled) continue;
  State.Opacity=FMath::FInterpConstantTo(State.Opacity,State.bWanted ? FadedOpacity : 1.f,
   Dt,(1.f-FadedOpacity)/FadeSeconds);
  for (UMaterialInstanceDynamic* Instance:State.Instances)
   if (Instance) Instance->SetScalarParameterValue(OpacityParameter,State.Opacity);
  if (!State.bWanted && State.Opacity>=1.f) RestoreWall(State);
 }
 Walls.RemoveAll([](const FWarehouseObserverWallFadeState& State){return !State.Mesh.IsValid();});
}

void UWarehouseObserverWallFade::EndPlay(const EEndPlayReason::Type Reason)
{
 Restore();
 Super::EndPlay(Reason);
}
