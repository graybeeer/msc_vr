#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseChargingStation.generated.h"

class AWarehouseForklift;
class UTextRenderComponent;

/** Root transform is the vehicle docking pose; +X points out of the charging bay. */
UCLASS()
class MSC_VR_API AWarehouseChargingStation : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseChargingStation();
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UFUNCTION(BlueprintCallable, Category="Charging") void RequestManualCharge();
 UFUNCTION(BlueprintPure, Category="Charging") bool IsDocked(const AWarehouseForklift* Vehicle) const;
 UFUNCTION(BlueprintPure, Category="Layout") TArray<AWarehouseForklift*> GetLinkedVehicles() const;
 bool TryReserve(AWarehouseForklift* Vehicle);
 void Release(AWarehouseForklift* Vehicle);
 void ShowCharge(float Percent, bool Charging);
 UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Charging") TObjectPtr<AWarehouseForklift> AssignedVehicle;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charging") bool bMainsPower = true;
 // Contact tolerance; existing maps keep 1cm, prototype maps allow 2cm of chassis settling.
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charging", meta=(ClampMin="0.1", ClampMax="5")) float DockPositionToleranceCm = 1.f;
 // Training assumption: 2.4 kW input x 90% efficiency = 2.16 kW into a 4.32 kWh pack.
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charging", meta=(ClampMin="0.1")) float PowerKw = 2.4f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charging", meta=(ClampMin="0.01", ClampMax="1")) float Efficiency = .9f;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Charging") TObjectPtr<AWarehouseForklift> Occupant;
 // Derived installation presentation; charging availability retains its explicit bMainsPower setting.
 UPROPERTY(Transient,VisibleAnywhere,BlueprintReadOnly,Category="Charging|Wiring") TObjectPtr<AActor> SupplyCabinet;
 UPROPERTY(Transient,VisibleAnywhere,BlueprintReadOnly,Category="Charging|Wiring") TArray<FVector> SupplyCableRoute;
 UPROPERTY(Transient,VisibleAnywhere,BlueprintReadOnly,Category="Charging|Wiring") FString SupplyCableStatus;
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Display;
};
