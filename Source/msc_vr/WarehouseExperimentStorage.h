#pragma once
#include "CoreMinimal.h"

class AActor;
class UWorld;
class FJsonObject;

/** Initial-condition snapshots only. Apply before BeginPlay, on a freshly loaded map. */
namespace WarehouseExperimentStorage
{
 inline constexpr int32 MaxSavedLayouts=6;
 struct FLayoutInfo
 {
  FString Id,Name,MapPackage,Hash,CargoFingerprint,SavedAt;
  int32 ActorCount=0,CargoCount=0,HeldCargoCount=0;
 };
 MSC_VR_API FString StableActorId(const AActor* Actor);
 MSC_VR_API AActor* ResolveActorId(UWorld* World,const FString& Id);
 MSC_VR_API FString MapIdentity(const UWorld* World);
 MSC_VR_API FString LayoutDirectory();
 MSC_VR_API bool CanCapture(UWorld* World,FString& Error);
 MSC_VR_API bool Capture(UWorld* World,const FString& Name,TSharedPtr<FJsonObject>& Snapshot,FString& Error);
 MSC_VR_API bool Validate(UWorld* World,const TSharedPtr<FJsonObject>& Snapshot,FString& Error);
 // On failure the caller must reload the pristine map before allowing simulation to run.
 MSC_VR_API bool ApplyBeforeBeginPlay(UWorld* World,const TSharedPtr<FJsonObject>& Snapshot,FString& Error);
 // Rebuild confirmed pallet film after all actors have initialized their native physics.
 MSC_VR_API bool FinalizeAfterBeginPlay(UWorld* World,const TSharedPtr<FJsonObject>& Snapshot,FString& Error);
 // Initial layouts carry no momentum. Call once before pausing an ordinary load.
 MSC_VR_API void ResetInitialVelocities(UWorld* World);
 MSC_VR_API bool Write(const TSharedPtr<FJsonObject>& Snapshot,FLayoutInfo& Info,FString& Error,const FString& ReplaceId=FString());
 // Remove an active slot without invalidating references in historical experiments.
 MSC_VR_API bool Archive(const FString& Id,FString& Error);
 MSC_VR_API bool Read(const FString& Id,TSharedPtr<FJsonObject>& Snapshot,FString& Error);
 MSC_VR_API TArray<FLayoutInfo> List();
 MSC_VR_API FLayoutInfo Describe(const TSharedPtr<FJsonObject>& Snapshot);
 MSC_VR_API FString CargoFingerprint(const TSharedPtr<FJsonObject>& Snapshot);
}
