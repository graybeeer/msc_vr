#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "WarehouseObserverWallFade.generated.h"

class APlayerController;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMeshComponent;

/** Cooked source-to-variant references; original wall assets remain unchanged. */
UCLASS()
class MSC_VR_API UWarehouseObserverWallPalette : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere, Category="Observer")
 TMap<UMaterialInterface*, UMaterialInterface*> Materials;
};

USTRUCT()
struct FWarehouseObserverWallFadeState
{
 GENERATED_BODY()
 UPROPERTY() TWeakObjectPtr<UMeshComponent> Mesh;
 UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> Originals;
 UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Instances;
 float Opacity=1.f;
 bool bWanted=false;
 bool bInstalled=false;
};

/** Local observer presentation only. Called by the controller, including while paused. */
UCLASS(ClassGroup=(Warehouse))
class MSC_VR_API UWarehouseObserverWallFade : public UActorComponent
{
 GENERATED_BODY()
public:
 UWarehouseObserverWallFade();
 UFUNCTION(BlueprintCallable, Category="Observer") void Update(APlayerController* PC, bool Enabled);
 UFUNCTION(BlueprintCallable, Category="Observer") void Restore();
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UPROPERTY(EditAnywhere, Category="Observer") TSoftObjectPtr<UWarehouseObserverWallPalette> PaletteAsset;
private:
 UPROPERTY(Transient) TObjectPtr<UWarehouseObserverWallPalette> Palette;
 UPROPERTY(Transient) TArray<FWarehouseObserverWallFadeState> Walls;
 double LastUpdate=0.;
 double NextMembership=0.;
 double NextPaletteLoad=0.;
 void Install(FWarehouseObserverWallFadeState& State);
 static void RestoreWall(FWarehouseObserverWallFadeState& State);
};
