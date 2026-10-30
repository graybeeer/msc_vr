#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WarehouseChargerWiring.generated.h"

class UStaticMesh;
class UMaterialInterface;

/** Derived electrical installation. No vehicle decisions or charging interlocks. */
UCLASS()
class MSC_VR_API UWarehouseChargerWiring : public UWorldSubsystem
{
 GENERATED_BODY()
public:
 UWarehouseChargerWiring();
 virtual void Deinitialize() override;
 UFUNCTION(BlueprintCallable,Category="Charging|Wiring",meta=(WorldContext="Context")) static void RefreshConnections(const UObject* Context);
 UFUNCTION(BlueprintCallable,Category="Charging|Wiring",meta=(WorldContext="Context")) static void RequestRefresh(const UObject* Context);
 UFUNCTION(BlueprintCallable,Category="Charging|Wiring",meta=(WorldContext="Context")) static void SetSuspended(const UObject* Context,bool Suspended);
 UPROPERTY(Transient,VisibleAnywhere,BlueprintReadOnly,Category="Charging|Wiring") int32 RebuildCount=0;
 UPROPERTY(Transient,VisibleAnywhere,BlueprintReadOnly,Category="Charging|Wiring") TArray<TObjectPtr<AActor>> GeneratedCables;
private:
 UPROPERTY() TObjectPtr<UStaticMesh> CableMesh;
 UPROPERTY() TObjectPtr<UMaterialInterface> CableMaterial;
 FTimerHandle PendingRefresh;
 bool bSuspended=false;
 void Rebuild();
 void Clear();
 static UWarehouseChargerWiring* Find(const UObject* Context);
};
