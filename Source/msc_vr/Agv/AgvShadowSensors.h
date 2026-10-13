#pragma once

#include "CoreMinimal.h"
#include "AgvDriveComponent.h"
#include "GameFramework/Actor.h"
#include "AgvShadowSensors.generated.h"

class UAgvLidarComponent;
class UAgvLidarLocalizerComponent;
class UAgvSafetyComponent;
class UPrimitiveComponent;

/**
 * Read-only drive for a vehicle someone else drives (the main warehouse forklift): it never moves anything. Each step
 * it reads the actual body motion and presents it the way the AGV localizer and safety system expect from their own
 * drive: travel of the reference point (the encoder, here the ground speed at the support-wheel axle), the yaw rate
 * (the gyro) and the steer angle that motion implies (for the safety fields' arc). The motion is also mirrored as the
 * "command", so the safety system picks the field set of the motion that is happening. Its speed limit is not obeyed.
 */
UCLASS(ClassGroup=(Agv))
class MSC_VR_API UAgvExternalDriveComponent : public UAgvTricycleDriveComponent
{
	GENERATED_BODY()

public:
	virtual void Step(float Dt) override;
	virtual void Halt() override {}

	/** The simulating rigid body whose motion is read (the main forklift's welded chassis). */
	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> Body;
};

/**
 * Shadow mode (2026-10-13): the AGV module's sensors on a vehicle driven by other code. Adds an external drive, the
 * five LiDARs at the test vehicle's positions (same model and origin), the LiDAR localizer and the safety system, and
 * runs them every frame without touching the vehicle's control: what would they have estimated and asked for while
 * the vehicle does its normal work? Measures the localization error against the actual pose, the time the safety
 * fields spend in warning / stop while moving, and the frame cost. Sensors sleep while the vehicle is parked.
 */
UCLASS(ClassGroup=(Agv))
class MSC_VR_API UAgvShadowSensorsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgvShadowSensorsComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Cargo may stand out over the carried pallet's edges by this much (cm); it is part of the load outline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Shadow")
	float LoadOverhangCm = 5.f;

	/** Draw the safety fields (like the test vehicle) for watching in Play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Shadow")
	bool bDrawFields = false;

	// Results since the start.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	float MaxErrorCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	float MeanErrorCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	float MaxHeadingErrorDeg = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	float MovingSeconds = 0.f;

	/** While moving: seconds the safety system would have slowed (warning) / stopped the vehicle. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	float WarningSeconds = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	float StopSeconds = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	int32 StopEvents = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	float MeanSensorMsMoving = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	FString LocalizerStatus;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	FString SafetyStatus;

	UFUNCTION(BlueprintPure, Category="AGV|Shadow")
	FString Summary() const;

	/** Speed cap while the localizer runs on odometry only (scans not matching for a while). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Shadow")
	float OdometryOnlySpeedCm = 50.f;

	/** The safety system's current speed cap (cm/s; 0 = protective stop). */
	float GetSafetySpeedLimitCm() const;

	/** The actor transform the LiDAR estimate implies (x, y, yaw estimated; height, pitch and roll as they are). */
	bool GetEstimatedActorTransform(FTransform& Out) const;

private:
	void Build();

	UPROPERTY(Transient) TObjectPtr<UAgvExternalDriveComponent> Drive;
	UPROPERTY(Transient) TArray<TObjectPtr<UAgvLidarComponent>> Lidars;
	UPROPERTY(Transient) TObjectPtr<UAgvLidarLocalizerComponent> Localizer;
	UPROPERTY(Transient) TObjectPtr<UAgvSafetyComponent> Safety;

	bool bBuilt = false;
	bool bAsleep = false;
	float ParkedSeconds = 0.f;
	double ErrorSum = 0.0;
	int64 ErrorSamples = 0;
	double SensorMsMovingSum = 0.0;
	int64 MovingFrames = 0;
	bool bWasStopped = false;
};

/** Puts shadow sensors on every main warehouse forklift when play starts (spawned by agv.shadow or a test). */
UCLASS()
class MSC_VR_API AAgvShadowManager : public AActor
{
	GENERATED_BODY()

public:
	AAgvShadowManager();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Shadow")
	bool bDrawFields = false;

	/** Logs every vehicle's Summary() at this interval (s); 0 = never. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Shadow")
	float ReportIntervalSeconds = 30.f;

	UFUNCTION(BlueprintCallable, Category="AGV|Shadow")
	FString Report() const;

private:
	UPROPERTY(Transient) TArray<TObjectPtr<UAgvShadowSensorsComponent>> Kits;
	bool bAttached = false;
	float SinceReport = 0.f;
};
