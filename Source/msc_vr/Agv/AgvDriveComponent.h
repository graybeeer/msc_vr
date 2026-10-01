#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgvDriveComponent.generated.h"

/**
 * Boundary between navigation and the vehicle. Navigation only asks for a travel speed and a yaw rate;
 * how wheels produce them (steer angle, drive force) belongs to the subclass.
 */
UCLASS(Abstract, ClassGroup=(Agv))
class MSC_VR_API UAgvDriveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgvDriveComponent();

	/** Speed along the actor's local +X (negative = local -X leads) and yaw rate about the reference point. */
	UFUNCTION(BlueprintCallable, Category="AGV|Drive")
	virtual void SetCommand(double SpeedCmS, double YawRateDegS);
	virtual void Step(float Dt) {}
	/** Stops at once without deceleration; for resets and teleports only. */
	virtual void Halt();

	/**
	 * Wheel speed cap from the safety system (0 = safety stop), applied below navigation like a safety circuit.
	 * Navigation reads it to plan within it.
	 */
	UFUNCTION(BlueprintCallable, Category="AGV|Drive")
	void SetSafetySpeedLimit(float LimitCmS) { SafetySpeedLimitCmS = FMath::Max(0.f, LimitCmS); }

	/** Actual pose of the reference point. Navigation must read it through the localizer, not from here. */
	void GetTruePose(FVector2D& OutPosition, double& OutYaw) const;

	/**
	 * Local X of the point that follows the path: the point with no sideways velocity.
	 * With fixed load wheels under the forks this is the middle of their axle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Drive")
	float ReferenceOffsetCm = 57.5f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Drive")
	float SpeedCmS = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Drive")
	float YawRateDegS = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Drive")
	float SafetySpeedLimitCmS = TNumericLimits<float>::Max();

protected:
	/** Moves the actor so the reference point travels at Speed with YawRate over Dt; also stores them as the actual values. */
	void MoveReference(double Speed, double YawRateDeg, float Dt);

	double CommandSpeedCmS = 0.0;
	double CommandYawRateDegS = 0.0;
};

/**
 * Placeholder vehicle: follows the command exactly within acceleration limits by moving the actor.
 * To be replaced by the force-driven wheel model once the wheel layout is final.
 */
UCLASS(ClassGroup=(Agv), meta=(BlueprintSpawnableComponent))
class MSC_VR_API UAgvKinematicDriveComponent : public UAgvDriveComponent
{
	GENERATED_BODY()

public:
	virtual void Step(float Dt) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Drive", meta=(ClampMin="1"))
	float MaxLinearAccelCm = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Drive", meta=(ClampMin="1"))
	float MaxYawAccelDeg = 180.f;
};

/**
 * Tricycle AGV: one steered drive wheel and a fixed support-wheel axle through the reference point.
 * The command is turned into a steer angle and wheel speed, both actuators are rate limited,
 * and the vehicle moves by what the wheel actually does (so steering lag shows up in the path).
 */
UCLASS(ClassGroup=(Agv), meta=(BlueprintSpawnableComponent))
class MSC_VR_API UAgvTricycleDriveComponent : public UAgvDriveComponent
{
	GENERATED_BODY()

public:
	virtual void Step(float Dt) override;
	virtual void Halt() override;

	/** Local X of the drive wheel's steer axis; the wheelbase is ReferenceOffsetCm minus this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Wheels")
	float DriveWheelOffsetCm = -40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Wheels", meta=(ClampMin="0"))
	float SupportWheelHalfTrackCm = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Wheels", meta=(ClampMin="1", ClampMax="90"))
	float MaxSteerAngleDeg = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Wheels", meta=(ClampMin="1"))
	float MaxSteerRateDeg = 90.f;

	/** Drive motor limit at the wheel rim. In curves the wheel runs faster than the reference point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Wheels", meta=(ClampMin="1"))
	float MaxWheelSpeedCm = 180.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Wheels", meta=(ClampMin="1"))
	float MaxWheelAccelCm = 100.f;

	/** Below this wheel speed the wheel waits until the steering is within SteerAlignToleranceDeg (stop and steer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Wheels", meta=(ClampMin="0"))
	float SteerHoldSpeedCm = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Wheels", meta=(ClampMin="0.1"))
	float SteerAlignToleranceDeg = 5.f;

	// Actual wheel state. Travel values are signed running totals (odometry); divide by a radius for the spin angle.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Wheels")
	float SteerAngleDeg = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Wheels")
	float WheelSpeedCmS = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Wheels")
	double DriveWheelTravelCm = 0.0;

	/** Support wheel on local +Y / -Y. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Wheels")
	double SupportWheelTravelPosYCm = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Wheels")
	double SupportWheelTravelNegYCm = 0.0;

protected:
	double WheelbaseCm() const { return FMath::Max(1.0, (double)ReferenceOffsetCm - DriveWheelOffsetCm); }

	/** Steer angle (rad) and signed wheel rim speed (cm/s) that realise the current command, before actuator limits. */
	void WheelTarget(double& OutSteer, double& OutSpeedCm) const;

	/** Turns SteerAngleDeg toward TargetSteer at the rate limit and reduces InOutSpeedCm while the steering is still off. */
	void StepSteering(double TargetSteer, double& InOutSpeedCm, float Dt);
};
