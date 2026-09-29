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
 UFUNCTION(BlueprintCallable, Category="Charging") void RequestManualCharge();
 UFUNCTION(BlueprintPure, Category="Charging") bool IsDocked(const AWarehouseForklift* Vehicle) const;
 bool TryReserve(AWarehouseForklift* Vehicle);
 void Release(AWarehouseForklift* Vehicle);
 void ShowCharge(float Percent, bool Charging);
 UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Charging") TObjectPtr<AWarehouseForklift> AssignedVehicle;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charging") bool bMainsPower = true;
 // Training assumption: 2.4 kW input x 90% efficiency = 2.16 kW into a 4.32 kWh pack.
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charging", meta=(ClampMin="0.1")) float PowerKw = 2.4f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Charging", meta=(ClampMin="0.01", ClampMax="1")) float Efficiency = .9f;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Charging") TObjectPtr<AWarehouseForklift> Occupant;
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Display;
};
