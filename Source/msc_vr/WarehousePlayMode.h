#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WarehouseLayoutEditor.h"
#include "WarehousePlayMode.generated.h"

class AWarehouseInboundZone;
class AWarehouseCargo;
class UStaticMesh;
class Amsc_vrPlayerController;
class SWidget;

UCLASS()
class MSC_VR_API UWarehousePlaySave : public USaveGame
{
 GENERATED_BODY()
public:
 UPROPERTY(SaveGame,BlueprintReadOnly) int32 UnlockedStage=1;
 UPROPERTY(SaveGame,BlueprintReadOnly) TArray<int32> CompletedStages;
};

/** Campaign progress only. Stage restarts deliberately reset stock, budget and equipment. */
UCLASS()
class MSC_VR_API UWarehousePlayProgress : public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 UPROPERTY(BlueprintReadOnly) int32 UnlockedStage=1;
 UPROPERTY(BlueprintReadOnly) TArray<int32> CompletedStages;
 bool RecordCompletion(int32 Stage);
 static FString SaveSlot();
};

/** Optional, map-authored prototype rules. Absent in the existing administrator map. */
UCLASS(BlueprintType)
class MSC_VR_API AWarehousePlayStage : public AActor
{
 GENERATED_BODY()
public:
 AWarehousePlayStage();
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 static AWarehousePlayStage* Find(const UObject* Context);
 static bool IsPlay(const UObject* Context);
 static bool IsMenu(const UObject* Context);
 static FString StageMap(int32 Stage);
 static FString StageTitle(int32 Stage);
 static FString UnlockDescription(int32 Stage);
 static int32 EquipmentCost(EWarehouseLayoutCategory Category);
 static int32 RequiredStage(EWarehouseLayoutCategory Category);
 UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 StageIndex=0;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 StartingBudgetWon=20000000;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 DeliveryGoal=2;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FVector2D HalfSizeCm=FVector2D(1800,1400);
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float CeilingCm=600;
 UPROPERTY(EditAnywhere) TObjectPtr<AWarehouseInboundZone> Inbound;
 UPROPERTY(EditAnywhere) TArray<FVector> ArrivalPoints;
 UPROPERTY(EditAnywhere) TObjectPtr<UStaticMesh> CartonMesh;
 UPROPERTY(BlueprintReadOnly) int32 BudgetWon=0;
 UPROPERTY(BlueprintReadOnly) int32 SpentWon=0;
 UPROPERTY(BlueprintReadOnly) int32 Arrived=0;
 UPROPERTY(BlueprintReadOnly) int32 Delivered=0;
 UPROPERTY(BlueprintReadOnly) bool bOperating=false;
 UPROPERTY(BlueprintReadOnly) bool bCompleted=false;
 UPROPERTY(BlueprintReadOnly) FString Status;
 UFUNCTION(BlueprintCallable) bool StartOperation();
 UFUNCTION(BlueprintPure) bool CanPurchase(EWarehouseLayoutCategory Category) const;
 UFUNCTION(BlueprintPure) FString PurchaseDescription(EWarehouseLayoutCategory Category) const;
 bool ChargePurchase(EWarehouseLayoutCategory Category);
 bool CanEditActor(const AActor* Actor) const;
 bool Contains(const FBox& Bounds) const;
private:
 double NextArrivalSeconds=0;
 bool SpawnArrival();
};

MSC_VR_API TSharedRef<SWidget> MakeWarehousePlayPanel(Amsc_vrPlayerController* Controller,AWarehousePlayStage* Stage);
