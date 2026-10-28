#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseFocusDirector.generated.h"

class AWarehousePallet;
class AWarehouseShippingSystem;
class AWarehouseLogisticsTruck;
class AWarehouseDamageSystem;

/** Background office/NPC coordination. The existing forklift and shipping systems execute the work. */
UCLASS()
class MSC_VR_API AWarehouseFocusDirector : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseFocusDirector();
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 static AWarehouseFocusDirector* Find(const UObject* Context);
 static bool IsActive(const UObject* Context);

 UFUNCTION(BlueprintCallable,Category="Operations") void SetManualOperations(bool bManual,bool bPersist=true);
 UFUNCTION(BlueprintPure,Category="Operations") bool IsManualOperations() const { return bManualOperations; }
 UFUNCTION(BlueprintCallable,Category="Operations") bool SetFleetRunning(bool bRunning);
 UFUNCTION(BlueprintCallable,Category="Operations") bool RequestOutbound(AWarehousePallet* Pallet);
 UFUNCTION(BlueprintPure,Category="Operations") bool IsOutboundPending(AWarehousePallet* Pallet) const;
 UFUNCTION(BlueprintPure,Category="Operations") FString GetSummary() const;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Operations") FString LastMessage;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Operations") int32 PendingOutboundCount=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Operations") int32 AutomaticOrderCount=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Operations") int32 QuarantinedCount=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Operations") bool bWorkflowStarted=false;

private:
 bool bManualOperations=false;
 FString OperationsStatus;
 FString SupplyStatus;
 FString DispatchStatus;
 struct FPendingOrder { TWeakObjectPtr<AWarehousePallet> Pallet; bool bUserRequested=false; };
 TArray<FPendingOrder> PendingOrders;
 TMap<TWeakObjectPtr<AWarehousePallet>,double> StoredSince;
 TMap<TWeakObjectPtr<AWarehouseLogisticsTruck>,double> ReturnAt;
 TWeakObjectPtr<AWarehouseLogisticsTruck> InboundTruck;
 double NextAutomaticOrderAt=0;

 bool Paused(FString& Reason) const;
 bool FleetRunning() const;
 bool Eligible(AWarehousePallet* Pallet,AWarehouseShippingSystem* Shipping,AWarehouseDamageSystem* Strength,FString& Reason) const;
 void UpdateSupply(AWarehouseShippingSystem* Shipping);
 void UpdateOutbound(AWarehouseShippingSystem* Shipping,AWarehouseDamageSystem* Strength);
 void UpdateInspection(AWarehouseDamageSystem* Strength);
 bool DispatchPending(AWarehouseShippingSystem* Shipping,AWarehouseDamageSystem* Strength);
 bool IsInboundTruck(AWarehouseLogisticsTruck* Truck,const AWarehouseShippingSystem* Shipping) const;
};
