#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "AgvLidarComponent.generated.h"

class UPrimitiveComponent;

/** A measured return; the component identifies the reflector, not its hidden shape or live pose. */
struct FAgvLidarReturn
{
	TWeakObjectPtr<UPrimitiveComponent> Component;
	FVector SensorPoint = FVector::ZeroVector;
};

/**
 * Spinning multi-channel LiDAR, attached at its real mount. Each step it traces only the columns the head sweeps
 * through in that time, so a scan taken while driving is distorted exactly like a real one. Ranges get Gaussian
 * noise and random dropouts. Points are reported in this component's frame (cm, X = 0 deg azimuth, Z up).
 * Beams hit anything solid (traced by object type: people's Pawn capsules ignore the Visibility channel), not
 * overlap-only volumes. The owning vehicle's own body blocks beams (self-occlusion) and its returns are dropped.
 */
UCLASS(ClassGroup=(Agv), meta=(BlueprintSpawnableComponent))
class MSC_VR_API UAgvLidarComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UAgvLidarComponent();

	/** Advances the head by Dt; called by the owning vehicle after the drive has moved it. */
	void Step(float Dt);

	/** One instantaneous revolution, called only by the warehouse's shared one-second observation frame. */
	void CaptureSnapshot();
	void DiscardSnapshot();
	const TArray<FAgvLidarReturn>& GetLastReturns() const { return LastReturns; }

	/** Moves the points measured since the last call into Out (appended). */
	void TakePoints(TArray<FVector>& Out);

	/** Increments every time the head completes a revolution (a full scan). */
	UFUNCTION(BlueprintPure, Category="AGV|LiDAR")
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

	/** Own-vehicle surfaces closer than this are the sensor's housing and window, which the beam passes through. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|LiDAR", meta=(ClampMin="0"))
	float HousingRadiusCm = 15.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Debug")
	bool bDrawPoints = false;

	/** Beams traced in the last step and the time it took (performance check). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|LiDAR")
	int32 LastStepBeams = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|LiDAR")
	float LastStepMilliseconds = 0.f;

	/** Last completed scan: beams fired, points returned, beams blocked by the vehicle's own body. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|LiDAR")
	int32 LastScanBeams = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|LiDAR")
	int32 LastScanPoints = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|LiDAR")
	int32 LastScanSelfBlocked = 0;

private:
	struct FBeam
	{
		FVector Local;
		int32 Column;
		double Distance=0.;
		uint8 Result=0;
		TWeakObjectPtr<UPrimitiveComponent> Component;
	};
	void EnsureBeamDirections(int32 Columns);
	TArray<FVector> BeamDirections;
	TArray<FBeam> BeamScratch;
	int32 CachedColumns=0,CachedChannels=0;
	float CachedFov=0,CachedVerticalMin=0,CachedVerticalMax=0;
	friend class FAgvLidarDirectionCacheTest;
	TArray<FVector> Points;
	TArray<FAgvLidarReturn> LastReturns;
	FRandomStream Noise;
	bool bNoiseSeeded = false;
	/** Head position in columns, fractional. */
	double Column = 0.0;
	int32 RevolutionCount = 0;
	int32 ScanBeams = 0;
	int32 ScanPoints = 0;
	int32 ScanSelfBlocked = 0;
};
