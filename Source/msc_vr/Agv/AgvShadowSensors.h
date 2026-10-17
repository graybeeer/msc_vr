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

	/** Error of the pose the forklift drives by (GetEstimatedActorTransform) against the actual pose. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	float MaxNavErrorCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	float MaxNavHeadingErrorDeg = 0.f;

	/** Turning on the spot, how often the turn changed direction (left <-> right): a vehicle hunting for a heading. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	int32 TurnReversals = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	FString LocalizerStatus;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Shadow")
	FString SafetyStatus;

	UFUNCTION(BlueprintPure, Category="AGV|Shadow")
	FString Summary() const;

	/** Speed cap while the localizer runs on odometry only (scans not matching for a while). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Shadow")
	float OdometryOnlySpeedCm = 50.f;

	/**
	 * The pose the forklift drives by follows the measured motion (encoder, gyro) every frame and takes each scan's
	 * correction in gradually, at most these rates: a scan match moves the estimate by a few millimetres or a few tenths
	 * of a degree at 5 Hz, and a controller aligning to 1 deg chased those steps left and right on the spot. Turning on
	 * the spot the heading is the gyro's with only a slow trim. Beyond the snap limits (a relocalization) it jumps.
	 */
	/** The vehicle counts as following its planned route while its reference point is this close to it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Shadow")
	float OnRouteToleranceCm = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Shadow")
	float NavCorrectionCmPerS = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Shadow")
	float NavCorrectionDegPerS = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Shadow")
	float NavTurningCorrectionDegPerS = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Shadow")
	float NavSnapCm = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Shadow")
	float NavSnapDeg = 5.f;

	/** The safety system's current speed cap (cm/s; 0 = protective stop). */
	float GetSafetySpeedLimitCm() const;

	/** The actor transform the LiDAR estimate implies (x, y, yaw estimated; height, pitch and roll as they are). */
	bool GetEstimatedActorTransform(FTransform& Out) const;

	void SetDrawFields(bool bDraw);

private:
	void Build();
	void UpdateNavigationPose(float Dt);

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
	bool bNavValid = false;
	FVector2D NavPosition = FVector2D::ZeroVector;
	double NavYaw = 0.0;
	FVector2D LastOdomPosition = FVector2D::ZeroVector;
	double LastOdomYaw = 0.0;
	int32 LastTurnSign = 0;
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

	/** Draws (or stops drawing) every kit's safety fields. */
	void SetDrawFields(bool bDraw);

private:
	UPROPERTY(Transient) TArray<TObjectPtr<UAgvShadowSensorsComponent>> Kits;
	float SinceAttachCheck = 0.f;
	bool bAttached = false;
	float SinceReport = 0.f;
};
