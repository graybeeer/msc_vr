#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseShippingSystem.generated.h"

class AWarehousePallet;
class AWarehouseForklift;
class AWarehouseLogisticsTruck;

UENUM(BlueprintType)
enum class EWarehouseShipmentDirection : uint8 { Outbound, Inbound };
UENUM(BlueprintType)
enum class EWarehouseShipmentState : uint8 { Preparing, WaitingTruck, Transferring, ReadyToDepart, Departing, Completed, Cancelled };

USTRUCT(BlueprintType)
struct FWarehouseShippingZone
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FString ZoneId;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bInbound=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<AActor> Anchor;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector HalfSize=FVector(350,310,80);
 // Pallet base poses relative to the movable zone anchor, never fixed world coordinates.
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FTransform> Slots;
};

USTRUCT(BlueprintType)
struct FWarehouseShipmentLine
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) TObjectPtr<AWarehousePallet> Pallet;
 UPROPERTY(BlueprintReadOnly) FString PalletLabel;
 UPROPERTY(BlueprintReadOnly) TArray<TObjectPtr<AActor>> Cargo;
 UPROPERTY(BlueprintReadOnly) FString JobId;
 UPROPERTY(BlueprintReadOnly) TObjectPtr<AWarehouseForklift> Vehicle;
 UPROPERTY(BlueprintReadOnly) int32 StagingSlot=INDEX_NONE;
 UPROPERTY(BlueprintReadOnly) int32 TruckSlot=INDEX_NONE;
 UPROPERTY(BlueprintReadOnly) bool bArrived=false;
 UPROPERTY(BlueprintReadOnly) bool bLoaded=false;
 bool bDeliveryReported=false;
 UPROPERTY(BlueprintReadOnly) FString Status;
 double StableSince=-1;
};

USTRUCT(BlueprintType)
struct FWarehouseShipment
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FString ShipmentId;
 UPROPERTY(BlueprintReadOnly) FString Name;
 UPROPERTY(BlueprintReadOnly) FString ZoneId;
 UPROPERTY(BlueprintReadOnly) EWarehouseShipmentDirection Direction=EWarehouseShipmentDirection::Outbound;
 UPROPERTY(BlueprintReadOnly) EWarehouseShipmentState State=EWarehouseShipmentState::Preparing;
 UPROPERTY(BlueprintReadOnly) TObjectPtr<AWarehouseLogisticsTruck> Truck;
 UPROPERTY(BlueprintReadOnly) TArray<FWarehouseShipmentLine> Lines;
 UPROPERTY(BlueprintReadOnly) bool bCargoReady=false;
 UPROPERTY(BlueprintReadOnly) bool bTruckReady=false;
 UPROPERTY(BlueprintReadOnly) FString Status;
 UPROPERTY(BlueprintReadOnly) FString WaitReason;
 UPROPERTY(BlueprintReadOnly) double StartedAt=0;
 UPROPERTY(BlueprintReadOnly) double FinishedAt=-1;
 UPROPERTY(BlueprintReadOnly) double ElapsedSeconds=0;
 double NextTransferAt=0;
 double NextDispatchAt=0;
};

/** Manifest and handover control. Only truck handling is a cinematic shortcut. */
UCLASS()
class MSC_VR_API AWarehouseShippingSystem : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseShippingSystem();
 virtual void Tick(float Dt) override;
 static AWarehouseShippingSystem* Find(const AActor* Context);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shipping") TArray<FWarehouseShippingZone> Zones;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Shipping") TArray<TObjectPtr<AWarehouseLogisticsTruck>> Trucks;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Shipping") TArray<FWarehouseShipment> Shipments;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Shipping") FString LastMessage;
 UFUNCTION(BlueprintCallable,Category="Shipping") bool CreateOutbound(const FString& Name,const TArray<AWarehousePallet*>& Pallets,const FString& ZoneId,AWarehouseLogisticsTruck* Truck);
 UFUNCTION(BlueprintCallable,Category="Shipping") bool StartInbound(AWarehouseLogisticsTruck* Truck,const FString& ZoneId);
 UFUNCTION(BlueprintCallable,Category="Shipping") bool CancelShipment(const FString& ShipmentId);
 UFUNCTION(BlueprintPure,Category="Shipping") FString GetPalletShipment(AWarehousePallet* Pallet) const;
 UFUNCTION(BlueprintPure,Category="Shipping") FString LocatePallet(AWarehousePallet* Pallet) const;
 UFUNCTION(BlueprintCallable,Category="Shipping") void RefreshShipping();
private:
 int32 NextShipment=1;
 bool Create(const FString& Name,const TArray<AWarehousePallet*>& Pallets,const FString& ZoneId,AWarehouseLogisticsTruck* Truck,EWarehouseShipmentDirection Direction);
 bool HasHandover(const FWarehouseShipmentLine& Line,const FWarehouseShippingZone& Zone,FString& Reason) const;
 bool ZonePlacementClear(const FWarehouseShippingZone& Zone,int32 Slot,AWarehousePallet* Pallet,FString& Reason) const;
 void Advance(FWarehouseShipment& Shipment,const FWarehouseShippingZone& Zone);
};
