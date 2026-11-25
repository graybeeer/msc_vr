#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WarehouseOperationsSettings.generated.h"

/** Local workflow preference, independent of a layout or campaign save. */
UCLASS(Config=Game)
class MSC_VR_API UWarehouseOperationsSettings : public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 static UWarehouseOperationsSettings* Find(const UObject* Context);
 UFUNCTION(BlueprintPure) bool IsManualOperations() const { return bManualOperations; }
 // Tests may change a session without overwriting the player's saved preference.
 UFUNCTION(BlueprintCallable) void SetManualOperations(bool bManual,bool bPersist=true);
 UFUNCTION(BlueprintPure,Category="Truck Visits") int32 GetTruckVisitLevel(bool bInbound) const;
 UFUNCTION(BlueprintCallable,Category="Truck Visits") void SetTruckVisitLevel(bool bInbound,int32 Level,bool bPersist=true);
private:
 UPROPERTY(Config) bool bManualOperations=false;
 UPROPERTY(Config) int32 InboundTruckVisitLevel=3;
 UPROPERTY(Config) int32 OutboundTruckVisitLevel=3;
};
