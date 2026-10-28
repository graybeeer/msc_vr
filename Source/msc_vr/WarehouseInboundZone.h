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
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inbound") int32 ConfirmedCount=0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inbound") FString Status;
 // Training assumptions for an abstract receiving worker; seconds use the simulation clock.
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inbound|Inspection",meta=(ClampMin="0.1")) float InspectionSeconds=3.f;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Inbound|Inspection",meta=(ClampMin="1",ClampMax="16")) int32 InspectionWorkers=1;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inbound|Inspection") int32 WaitingInspectionCount=0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inbound|Inspection") int32 ActiveInspectionCount=0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Inbound|Inspection") int32 QuarantinedCount=0;
 UFUNCTION(BlueprintCallable, Category="Inbound|Inspection") bool RequestReinspection(AWarehousePallet* Pallet);
 UFUNCTION(BlueprintCallable, Category="Inbound|Inspection") bool SetExpectedManifest(AWarehousePallet* Pallet,int32 CartonCount,const TMap<FString,int32>& CargoByKind);
 UFUNCTION(BlueprintCallable, Category="Inbound") void ScanInbound();
 UFUNCTION(BlueprintPure, Category="Inbound") bool ContainsPallet(const AWarehousePallet* Pallet) const;
 void RecordInboundConfirmed(AWarehousePallet* Pallet);
 // Repeat experiments supply the orders; receiving inspection and wrapping still run normally.
 bool bExternalDispatchOnly=false;
private:
 struct FReservation
 {
  TWeakObjectPtr<AWarehouseForklift> Vehicle;
  TWeakObjectPtr<AWarehousePallet> Pallet;
  FString JobId;
  TWeakObjectPtr<AActor> Rack;
  FTransform Pose;
 };
 TArray<FReservation> Reservations;
 struct FStableLoad
 {
  double Since=0;
  FTransform Pallet;
  TMap<TWeakObjectPtr<AActor>,FTransform> Cargo;
  bool bWrapping=false;
  int64 QueueSequence=0;
  double InspectionStarted=-1;
 };
 TMap<TWeakObjectPtr<AWarehousePallet>, FStableLoad> StableLoads;
 int64 NextInspectionSequence=0;
 bool IsBeingHandled(AWarehousePallet* Pallet,const TArray<AActor*>& Loads) const;
 FString CheckReceivingManifest(AWarehousePallet* Pallet,const TArray<AActor*>& Loads) const;
 bool SlotClear(int32 Index, AWarehousePallet* Pallet, const TArray<AActor*>& Loads, float Height, float Mass) const;
};
