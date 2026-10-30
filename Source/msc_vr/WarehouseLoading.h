#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WarehouseLoading.generated.h"

class SWidget;
class UGameViewportClient;
class APlayerController;
struct FWarehouseLoadingPaintState;
struct FWorldContext;

/** A presentation-only curtain. It never changes simulation time or performs work. */
UCLASS()
class MSC_VR_API UWarehouseLoadingSubsystem : public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 static UWarehouseLoadingSubsystem* Find(const UObject* Context);
 // Tokens make nested operations and every early-return path safe.
 UFUNCTION(BlueprintCallable,Category="Warehouse|Loading") int32 BeginTask(FText Message);
 UFUNCTION(BlueprintCallable,Category="Warehouse|Loading") void EndTask(int32 Token);
 UFUNCTION(BlueprintPure,Category="Warehouse|Loading") bool IsLoading() const { return !ActiveTokens.IsEmpty(); }
 UFUNCTION(BlueprintPure,Category="Warehouse|Loading") int32 GetActiveTaskCount() const { return ActiveTokens.Num(); }
 UFUNCTION(BlueprintPure,Category="Warehouse|Loading") int32 GetCurrentPaintCount() const;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Warehouse|Loading") FText ActiveMessage;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Warehouse|Loading") FString LastPresentation;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Warehouse|Loading") int32 LastPaintCount=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Warehouse|Loading") int32 StartedScreens=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Warehouse|Loading") int32 CompletedScreens=0;
private:
 void Show();
 void Hide();
 void MapLoading(const FWorldContext& Context,const FString& MapName);
 void MapLoaded(UWorld* World);
 void WorldInitialized(const UWorld::FActorsInitializedParams& Params);
 void WorldTick(UWorld* World,ELevelTick Tick,float DeltaSeconds);
 TArray<int32> ActiveTokens;
 int32 NextToken=0,MapToken=0,SceneToken=0;
 bool bOwnsMovie=false,bRedrawing=false;
 TWeakObjectPtr<UWorld> SceneWorld;
 TWeakObjectPtr<UGameViewportClient> Viewport;
 TArray<TWeakObjectPtr<APlayerController>> InputOwners;
 TWeakPtr<SWidget> PreviousFocus;
 TSharedPtr<SWidget> Widget;
 TSharedPtr<FWarehouseLoadingPaintState,ESPMode::ThreadSafe> PaintState;
 FDelegateHandle PreMapHandle,PostMapHandle,WorldInitHandle,WorldTickHandle;
};

namespace WarehouseLoading
{
 class MSC_VR_API FScopedTask
 {
 public:
  FScopedTask(const UObject* Context,const TCHAR* Message);
  ~FScopedTask();
  FScopedTask(const FScopedTask&)=delete;
  FScopedTask& operator=(const FScopedTask&)=delete;
 private:
  TWeakObjectPtr<UWarehouseLoadingSubsystem> Owner;
  int32 Token=0;
 };
}
