#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WarehouseTransportVisualizer.generated.h"

class AWarehouseForklift;
class UPrimitiveComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UPostProcessComponent;

/** Local presentation of existing job/route data. No collision or autonomous decisions. */
UCLASS()
class MSC_VR_API UWarehouseTransportVisualizer : public UActorComponent
{
 GENERATED_BODY()
public:
 UWarehouseTransportVisualizer();
 void Update();
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 struct FDepthState { bool bEnabled; int32 Stencil; uint8 WriteMask; };
 struct FVehicleVisual
 {
  int32 ColorIndex=0;
  TWeakObjectPtr<UInstancedStaticMeshComponent> Arrows;
  TArray<FVector> Route;
 };
 TMap<TWeakObjectPtr<UPrimitiveComponent>,FDepthState> OriginalDepth;
 TMap<TWeakObjectPtr<AWarehouseForklift>,FVehicleVisual> Vehicles;
 UPROPERTY() TObjectPtr<UMaterialInterface> OutlineMaterial;
 UPROPERTY() TObjectPtr<UMaterialInterface> ArrowMaterial;
 UPROPERTY() TObjectPtr<UStaticMesh> ArrowBar;
 UPROPERTY(Transient) TObjectPtr<UPostProcessComponent> Effect;
 UPROPERTY(Transient) TObjectPtr<AActor> DisplayActor;
 void UpdateArrows(AWarehouseForklift* Vehicle,FVehicleVisual& Visual);
 static void RestoreDepth(UPrimitiveComponent* Part,const FDepthState& State);
};
