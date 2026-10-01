#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "AgvLidarComponent.generated.h"

/**
 * Spinning multi-channel LiDAR, attached at its real mount. Each step it traces only the columns the head sweeps
 * through in that time, so a scan taken while driving is distorted exactly like a real one. Ranges get Gaussian
 * noise and random dropouts. Points are reported in this component's frame (cm, X = 0 deg azimuth, Z up).
 * Traces ignore the owning vehicle: its own parts are removed by the receiver's self filter, as real drivers do.
 */
UCLASS(ClassGroup=(Agv), meta=(BlueprintSpawnableComponent))
class MSC_VR_API UAgvLidarComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UAgvLidarComponent();

	/** Advances the head by Dt; called by the owning vehicle after the drive has moved it. */
	void Step(float Dt);

	/** Moves the points measured since the last call into Out (appended). */
	void TakePoints(TArray<FVector>& Out);

	/** Increments every time the head completes a revolution (a full scan). */
	int32 GetRevolutionCount() const { return RevolutionCount; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|LiDAR")
	bool bUseForLocalization = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|LiDAR", meta=(ClampMin="1", ClampMax="360"))
	float HorizontalFovDeg = 360.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|LiDAR", meta=(ClampMin="0.05"))
	float HorizontalResolutionDeg = 0.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|LiDAR", meta=(ClampMin="1", ClampMax="128"))
	int32 Channels = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|LiDAR")
	float VerticalMinDeg = -15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|LiDAR")
	float VerticalMaxDeg = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|LiDAR", meta=(ClampMin="0.5"))
	float RotationHz = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|LiDAR", meta=(ClampMin="0"))
	float MinRangeCm = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|LiDAR", meta=(ClampMin="1"))
	float MaxRangeCm = 3000.f;

	/** Standard deviation of the range error. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|LiDAR", meta=(ClampMin="0"))
	float RangeNoiseCm = 2.f;

	/** Chance that a beam returns nothing (dark or glancing surfaces, dust). 1 = sensor blind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|LiDAR", meta=(ClampMin="0", ClampMax="1"))
	float DropoutProbability = 0.02f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|LiDAR")
	int32 NoiseSeed = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Debug")
	bool bDrawPoints = false;

	/** Beams traced in the last step and the time it took (performance check). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|LiDAR")
	int32 LastStepBeams = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|LiDAR")
	float LastStepMilliseconds = 0.f;

private:
	TArray<FVector> Points;
	FRandomStream Noise;
	bool bNoiseSeeded = false;
	/** Head position in columns, fractional. */
	double Column = 0.0;
	int32 RevolutionCount = 0;
};
