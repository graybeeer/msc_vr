#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseAutonomyTypes.h"
#include "WarehouseComputer.generated.h"
class AWarehouseCargo;
class AWarehousePallet;
class AWarehouseForklift;

USTRUCT(BlueprintType)
struct FWarehouseInventoryItem
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) TObjectPtr<AWarehouseCargo> Cargo;
 UPROPERTY(BlueprintReadOnly) TObjectPtr<AWarehousePallet> Pallet;
 UPROPERTY(BlueprintReadOnly) FString CargoId;
 UPROPERTY(BlueprintReadOnly) FString Product;
 UPROPERTY(BlueprintReadOnly) FString Location;
 UPROPERTY(BlueprintReadOnly) float MassKg=0;
 UPROPERTY(BlueprintReadOnly) bool bReserved=false;
};
USTRUCT(BlueprintType)
struct FWarehouseComputerTask
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FString JobId;
 UPROPERTY(BlueprintReadOnly) TObjectPtr<AWarehousePallet> Pallet;
 UPROPERTY(BlueprintReadOnly) TObjectPtr<AWarehouseForklift> Vehicle;
 UPROPERTY(BlueprintReadOnly) FString Source;
 UPROPERTY(BlueprintReadOnly) FString Destination;
 UPROPERTY(BlueprintReadOnly) FString State;
 UPROPERTY(BlueprintReadOnly) bool bCompleted=false;
 UPROPERTY(BlueprintReadOnly) bool bCancelled=false;
 UPROPERTY(BlueprintReadOnly) bool bSubmissionTimeKnown=false;
 UPROPERTY(BlueprintReadOnly) double SubmittedSimulationSeconds=0;
 UPROPERTY(BlueprintReadOnly) double FinishedSimulationSeconds=-1;
 UPROPERTY(BlueprintReadOnly) double ElapsedSimulationSeconds=0;
};

/** Session-local warehouse console. The existing AGV AI executes every accepted job. */
UCLASS()
class MSC_VR_API AWarehouseComputer : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseComputer();
 virtual void BeginPlay() override;
 virtual void Tick(float Dt) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Console") FVector OutboundCentre=FVector(-1000,-1640,0);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Console") FVector OutboundHalfSize=FVector(350,310,80);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Console") TArray<FTransform> OutboundDestinations;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Console") TArray<FWarehouseInventoryItem> Inventory;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Console") TArray<FWarehouseComputerTask> Tasks;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Console") TArray<TObjectPtr<AWarehouseForklift>> Fleet;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Console") FString LastMessage;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Console") int32 RefreshCount=0;
 UFUNCTION(BlueprintCallable,Category="Console") void RefreshState();
 void RefreshLayoutDestinations();
 // Called at successful queue submission, including inbound and external FMS orders.
 void RecordJobSubmitted(AWarehouseForklift* Vehicle,const FWarehouseWorkOrder& Job);
 UFUNCTION(BlueprintPure,Category="Console") double GetTaskElapsedSeconds(const FString& JobId) const;
 // Selecting one parcel always transports its entire supporting pallet, never a partial picking order.
 UFUNCTION(BlueprintCallable,Category="Console") bool IssueOutbound(AWarehouseCargo* Cargo,AWarehouseForklift* Vehicle,int32 DestinationIndex);
 UFUNCTION(BlueprintCallable,Category="Console") bool SetVehicleRunning(AWarehouseForklift* Vehicle,bool Running);
 UFUNCTION(BlueprintCallable,Category="Console") bool CancelQueuedJob(const FString& JobId);
 UFUNCTION(BlueprintCallable,Category="Console") bool CreateHeldCargo(FName Id,FText Product,float MassKg,FVector SizeCm);
 UFUNCTION(BlueprintCallable,Category="Console") bool DeleteHeldCargo(FName Id);
 UFUNCTION(BlueprintPure,Category="Console") bool CanUse() const;
private:
 int32 NextJob=1;
 void RememberJob(AWarehouseForklift* Vehicle,const FWarehouseWorkOrder& Job,bool SubmissionTimeKnown);
 UFUNCTION() void ReceiveJobReport(const FWarehouseJobReport& Report);
 FString Locate(AActor* Object) const;
 bool Claimed(AWarehousePallet* Pallet) const;
};
