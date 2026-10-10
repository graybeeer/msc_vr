#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseInboundZone.generated.h"

class AWarehouseForklift;
class AWarehousePallet;

USTRUCT(BlueprintType)
struct FWarehouseRackSlot
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<AActor> Rack;
 // Pallet bottom centre, in world centimetres; yaw is the fork insertion direction.
 UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform Pose;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float ClearHeightCm=110;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) float CapacityKg=2000;
};

/** FMS staging dispatcher. Physical contacts and existing vehicle AI perform all transport. */
UCLASS()
class MSC_VR_API AWarehouseInboundZone : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseInboundZone();
 virtual void Tick(float DeltaSeconds) override;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inbound") FVector HalfSizeCm=FVector(190,270,80);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inbound") TArray<TObjectPtr<AWarehouseForklift>> Fleet;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inbound") TArray<FWarehouseRackSlot> RackSlots;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inbound") int32 DispatchedCount=0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inbound") int32 CompletedCount=0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inbound") FString Status;
 UFUNCTION(BlueprintCallable, Category="Inbound") void ScanInbound();
private:
 struct FReservation
 {
  TWeakObjectPtr<AWarehouseForklift> Vehicle;
  TWeakObjectPtr<AWarehousePallet> Pallet;
  FString JobId;
  int32 Slot=INDEX_NONE;
 };
 TArray<FReservation> Reservations;
 TMap<TWeakObjectPtr<AWarehousePallet>, double> StableSince;
 TMap<TWeakObjectPtr<AWarehousePallet>, FTransform> StablePose;
 bool SlotClear(int32 Index, AWarehousePallet* Pallet, const TArray<AActor*>& Loads, float Height, float Mass) const;
};
