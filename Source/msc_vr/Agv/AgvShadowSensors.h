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

	/** Explicit building roles and fixed rack parts are known; vehicles, cargo and moving facilities require observations. */
	static bool IsBuildingMapComponent(const UPrimitiveComponent* Component);
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
	bool bMapDirty = true;
};
