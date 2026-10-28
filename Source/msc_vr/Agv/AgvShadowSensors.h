#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgvShadowSensors.generated.h"

class UAgvLidarComponent;
class UPrimitiveComponent;

/** Fixed building/rack map entries have a published shape and pose. Other entries contain measured surface points only. */
USTRUCT(BlueprintType)
struct FAgvObservedObstacle
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TWeakObjectPtr<AActor> Actor;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TWeakObjectPtr<UPrimitiveComponent> Component;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FTransform ObservedTransform = FTransform::Identity;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FBox WorldBounds{ForceInit};
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FBox LocalBounds{ForceInit};
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TArray<FVector> WorldPoints;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bPerson = false;
};

using FAgvObstacleIndices = TArray<int32,TInlineAllocator<32>>;

/** Broad phase of one published snapshot. Never reads actors or live physics. */
class FAgvObstacleSpatialIndex
{
public:
	void Build(TConstArrayView<FAgvObservedObstacle> Obstacles);
	void Reset();
	void Query(const FBox& Bounds,FAgvObstacleIndices& Out) const;
private:
	struct FNode
	{
		FBox Bounds{ForceInit};
		int32 Left=INDEX_NONE,Right=INDEX_NONE,Begin=0,Count=0;
	};
	int32 BuildNode(int32 Begin,int32 Count);
	TArray<FNode> Nodes;
	TArray<FBox> PublishedBounds;
	TArray<int32> Entries,Unindexed;
};

/**
 * The sensor mounts selected from feature/forklift-navigation. No independent tick, drive, navigator, localization
 * feedback, or safety speed cap: the existing warehouse controller owns the single one-second observation frame.
 */
UCLASS(ClassGroup=(Agv), meta=(BlueprintSpawnableComponent))
class MSC_VR_API UAgvShadowSensorsComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UAgvShadowSensorsComponent();
	bool RefreshSnapshot(double SimNow, double FloorZCm);
	UFUNCTION(BlueprintCallable, Category="Warehouse|Sensors") void InvalidateSnapshot(const FString& Reason);
	UFUNCTION(BlueprintPure, Category="Warehouse|Sensors") bool IsSnapshotFresh(double SimNow) const;
	UFUNCTION(BlueprintPure, Category="Warehouse|Sensors") const TArray<FAgvObservedObstacle>& GetObservedObstacles() const { return ObservedObstacles; }
	UFUNCTION(BlueprintPure, Category="Warehouse|Sensors") const TArray<FAgvObservedObstacle>& GetKnownObstacles() const { return KnownObstacles; }
	UFUNCTION(BlueprintPure, Category="Warehouse|Sensors") bool IsKnownComponent(const UPrimitiveComponent* Component) const;
	UFUNCTION(BlueprintPure, Category="Warehouse|Sensors") bool HasObservedActor(const AActor* Actor) const;
	// Sorted original indices preserve the existing first-hit/diagnostic order.
	void FindKnownCandidates(const FBox& Bounds,FAgvObstacleIndices& Out) const;
	void FindObservedCandidates(const FBox& Bounds,FAgvObstacleIndices& Out) const;

	/** Explicit building roles and fixed rack parts are known; vehicles, cargo and moving facilities require observations. */
	static bool IsBuildingMapComponent(const UPrimitiveComponent* Component);
	// Derived fixed installations may be added after the first cached map. No scan is triggered here.
	void MarkBuildingMapDirty() { bMapDirty = true; }
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Warehouse|Sensors") TArray<TObjectPtr<UAgvLidarComponent>> Lidars;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Warehouse|Sensors") int32 SnapshotCount = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Warehouse|Sensors") double LastSnapshotSeconds = -1;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Warehouse|Sensors") bool bSnapshotValid = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Warehouse|Sensors") double SnapshotFloorZCm = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Warehouse|Sensors") int32 MapRevision = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Warehouse|Sensors") float LastSensorMilliseconds = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Warehouse|Sensors") int32 LastBeamCount = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Warehouse|Sensors") int32 ObservedObstacleCount = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Warehouse|Sensors") FString SensorStatus = TEXT("NO SNAPSHOT");

private:
	bool BuildSensors();
	void BuildKnownMap();
	UPROPERTY(Transient) TArray<FAgvObservedObstacle> ObservedObstacles;
	UPROPERTY(Transient) TArray<FAgvObservedObstacle> KnownObstacles;
	TSet<TWeakObjectPtr<UPrimitiveComponent>> KnownComponents;
	FAgvObstacleSpatialIndex KnownIndex,ObservedIndex;
	bool bMapDirty = true;
};
