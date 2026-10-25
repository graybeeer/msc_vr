#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "Engine/World.h"
#include "WarehouseExperimentStorage.h"
#include "WarehouseExperimentMetrics.h"
#include "WarehouseExperimentSubsystem.generated.h"

class FJsonObject;

USTRUCT(BlueprintType)
struct FWarehouseExperimentOrder
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FString OrderId;
 UPROPERTY(BlueprintReadOnly) FString PalletId;
 UPROPERTY(BlueprintReadOnly) FString DestinationId;
 UPROPERTY(BlueprintReadOnly) FString VehicleId;
 UPROPERTY(BlueprintReadOnly) double ReleaseSeconds=0;
};

struct FWarehouseExperimentChoice { FString Id,Label; };
struct FWarehouseExperimentResult
{
 FWarehouseExperimentRunInfo Info;
 FWarehouseExperimentSummary Summary;
 FString LayoutName,Status;
 double AchievedSpeed=0;
};

/** Replays saved initial conditions in fresh worlds. Never serializes running Chaos state. */
UCLASS()
class MSC_VR_API UWarehouseExperimentSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
 GENERATED_BODY()
public:
 static constexpr int32 MaxSavedLayouts=WarehouseExperimentStorage::MaxSavedLayouts;
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 virtual void Tick(float DeltaTime) override;
 virtual TStatId GetStatId() const override;
 virtual bool IsTickable() const override;
 virtual bool IsTickableWhenPaused() const override { return true; }
 virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }

 UPROPERTY(BlueprintReadOnly) FString LastMessage;
 UPROPERTY(BlueprintReadOnly) FString LastSavedLayoutId;
 UPROPERTY(BlueprintReadOnly) TArray<FWarehouseExperimentOrder> Orders;
 UPROPERTY(BlueprintReadOnly) bool bBatchRunning=false;
 UPROPERTY(BlueprintReadOnly) bool bLoading=false;
 UPROPERTY(BlueprintReadOnly) int32 CurrentRun=0;
 UPROPERTY(BlueprintReadOnly) int32 TotalRuns=0;
 // UI drafts survive the fresh-level reset between repetitions.
 TArray<FString> SelectedLayoutIds;
 int32 DraftRepeats=3,DraftSeed=42,DraftSpeed=4;
 double DraftDurationSeconds=300;

 TArray<WarehouseExperimentStorage::FLayoutInfo> GetLayouts() const;
 TArray<FWarehouseExperimentChoice> GetOrderSources() const;
 TArray<FWarehouseExperimentChoice> GetDestinations() const;
 const TArray<FWarehouseExperimentResult>& GetResults() const { return Results; }
 FWarehouseExperimentSummary GetLiveSummary() const;
 UFUNCTION(BlueprintCallable) bool SaveLayout(const FString& Name);
 UFUNCTION(BlueprintCallable) bool ReplaceLayout(const FString& Id,const FString& Name);
 UFUNCTION(BlueprintCallable) bool RemoveLayout(const FString& Id);
 UFUNCTION(BlueprintCallable) bool LoadLayout(const FString& Id);
 UFUNCTION(BlueprintCallable) bool AddOrder(const FString& PalletId,const FString& DestinationId,double ReleaseSeconds,const FString& VehicleId=TEXT(""));
 UFUNCTION(BlueprintCallable) bool RemoveOrder(int32 Index);
 UFUNCTION(BlueprintCallable) void ClearOrders();
 UFUNCTION(BlueprintCallable) bool SaveWorkload(const FString& Name);
 UFUNCTION(BlueprintCallable) bool LoadWorkload(const FString& Id);
 TArray<FWarehouseExperimentChoice> GetWorkloads() const;
 UFUNCTION(BlueprintCallable) bool StartBatch(const TArray<FString>& LayoutIds,int32 Repeats,double DurationSeconds,int32 Seed,int32 Speed);
 UFUNCTION(BlueprintCallable) void StopBatch();
 UFUNCTION(BlueprintCallable) bool ExportCSV();
 UFUNCTION(BlueprintPure) FString GetStatusJson() const;
 UFUNCTION(BlueprintPure) FString GetOutputDirectory() const;

private:
 struct FRun { FString LayoutId; int32 Repeat=1; };
 struct FDestination { FString Id,Label; FTransform Pose; TWeakObjectPtr<AActor> Resource; bool bRack=false; };
 TArray<FDestination> Destinations() const;
 TArray<FWarehouseExperimentResult> Results;
 TArray<FRun> Runs;
 TArray<FWarehouseExperimentOrder> RunOrders;
 TArray<int32> OrderPredecessors;
 TArray<TWeakObjectPtr<class AWarehousePallet>> RunPallets;
 TArray<bool> Released;
 TUniquePtr<FWarehouseExperimentMetrics> Metrics;
 TSharedPtr<FJsonObject> PendingSnapshot;
 WarehouseExperimentStorage::FLayoutInfo ActiveLayout;
 FDelegateHandle InitWorldHandle;
 TWeakObjectPtr<UWorld> AppliedWorld;
 TMap<TWeakObjectPtr<AActor>,bool> PreviousTicks;
 TMap<TWeakObjectPtr<class AWarehouseForklift>,bool> RestoreAutoStart;
 FString WorkloadId,WorkloadHash,BatchId,BaseMap,RestoreError;
 double Duration=300,NextDispatch=0,RunWallStart=0,PrepareTime=0;
 int32 Seed=42,Speed=4,PrepareFrames=0;
 bool bPrepared=false,bNeedRollback=false,bOpenPanel=false,bStartNext=false;
 void WorldInitialized(const UWorld::FActorsInitializedParams& Params);
 bool QueueLoad(const FString& Id);
 void BeginNextRun();
 void BeginRun();
 bool EndRun(const FString& Status);
 void DispatchOrders();
 void DisableUnscheduledWork(UWorld* World);
 void RestoreUnscheduledWork();
 void ReadResults();
 bool CheckWorkload(const TArray<FString>& LayoutIds,FString& Error);
 bool WriteIndex();
 TSharedRef<FJsonObject> WorkloadJson(const FString& Name) const;
};
