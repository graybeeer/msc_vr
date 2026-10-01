#pragma once

#include "CoreMinimal.h"
#include "AgvDriveComponent.h"
#include "AgvDynamicDriveComponent.generated.h"

/** A wheel nobody drives: fixed (rolls along the chassis X axis, resists sliding sideways) or a free-swivel caster. */
USTRUCT(BlueprintType)
struct FAgvPassiveWheel
{
	GENERATED_BODY()

	/** Contact point in the actor's local frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Wheels")
	FVector2D PositionCm = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Wheels", meta=(ClampMin="1"))
	float RadiusCm = 10.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Wheels")
	bool bCaster = false;

	/** Signed distance rolled (odometry); divide by the radius for the spin angle. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Wheels")
	double TravelCm = 0.0;

	/** Caster direction relative to the chassis. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Wheels")
	float SwivelDeg = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Wheels")
	float LoadN = 0.f;
};

/**
 * Force-driven tricycle AGV. The vehicle is a rigid body (chassis + payload mass, centre of mass, yaw inertia);
 * it moves only through tyre forces. The drive motor produces torque under torque / power limits, a wheel speed
 * controller (the AGV's own controller, which does not know the payload) sets that torque, and each wheel's grip
 * is limited by friction x its share of the weight, which shifts with the centre of mass and with acceleration.
 * Integrates at a fixed substep, so results do not depend on the frame rate.
 */
UCLASS(ClassGroup=(Agv), meta=(BlueprintSpawnableComponent))
class MSC_VR_API UAgvDynamicDriveComponent : public UAgvTricycleDriveComponent
{
	GENERATED_BODY()

public:
	UAgvDynamicDriveComponent();

	virtual void Step(float Dt) override;
	virtual void Halt() override;

	/** Load carried by the vehicle; CenterOfMassCm in the actor's local frame, SizeCm its footprint for the yaw inertia. */
	UFUNCTION(BlueprintCallable, Category="AGV|Dynamics")
	void SetPayload(float MassKg, FVector CenterOfMassCm, FVector2D SizeCm = FVector2D(110.0, 110.0));

	UFUNCTION(BlueprintCallable, Category="AGV|Dynamics")
	void ClearPayload() { SetPayload(0.f, FVector::ZeroVector, FVector2D::ZeroVector); }

	// Chassis (VNSL14 1,000 kg; centre of mass and inertia are estimates).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="1"))
	float ChassisMassKg = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics")
	FVector ChassisCenterOfMassCm = FVector(-20.0, 0.0, 70.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="1"))
	float ChassisYawInertiaKgM2 = 260.f;

	// Tyres (polyurethane on dry concrete).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="0.01"))
	float FrictionCoefficient = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="0"))
	float RollingResistance = 0.015f;

	// Drive motor, at the wheel (after the gearbox).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="1"))
	float MaxDriveTorqueNm = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="1"))
	float MaxDrivePowerW = 2200.f;

	/** Holding brake on the drive wheel, applied at full force on a safety stop (category 1 stop). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="0"))
	float SafetyBrakeTorqueNm = 700.f;

	/** Wheel plus motor rotor seen through the gearbox. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="0.01"))
	float DriveInertiaKgM2 = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="1"))
	float DriveWheelRadiusCm = 20.5f;

	// Wheel speed controller, tuned for the empty vehicle: bandwidth (1/s) and integral gain (1/s^2).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="0"))
	float SpeedLoopGain = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="0"))
	float SpeedLoopIntegralGain = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics")
	TArray<FAgvPassiveWheel> PassiveWheels;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="0.0005", ClampMax="0.01"))
	float SubstepSeconds = 0.002f;

	// Payload (set through SetPayload).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Dynamics")
	float PayloadMassKg = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Dynamics")
	FVector PayloadCenterOfMassCm = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Dynamics")
	float PayloadYawInertiaKgM2 = 0.f;

	// Status.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Dynamics|Status")
	float DriveWheelLoadN = 0.f;

	/** Rim speed minus ground speed along the drive wheel; non-zero = wheel spin or skid. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Dynamics|Status")
	float DriveWheelSlipCmS = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Dynamics|Status")
	float MotorTorqueNm = 0.f;

	/** Smallest wheel load as a fraction of the weight; below 0 a wheel would lift (the vehicle tips). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Dynamics|Status")
	float StabilityMargin = 0.f;

	/** Latched when a wheel lifts; the motion after that is not simulated (no tipping dynamics yet). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Dynamics|Status")
	bool bTipOver = false;

	/** Combined centre of mass (chassis + payload), actor-local. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Dynamics|Status")
	FVector CenterOfMassCm = FVector::ZeroVector;

private:
	struct FMassProperties
	{
		double Mass = 1.0;
		double YawInertia = 1.0;
		FVector CenterOfMass = FVector::ZeroVector; // metres, actor-local
	};
	FMassProperties MassProperties() const;
	void Substep(double H, const FMassProperties& Body, FVector2D& CenterOfMass, double& Yaw);
	void SolveWheelLoads(const FMassProperties& Body, const TArray<FVector2D>& Contacts, TArray<double>& OutLoads);

	// Rigid-body state in SI units: centre-of-mass velocity (world), yaw rate, drive wheel spin.
	FVector2D Velocity = FVector2D::ZeroVector;
	double YawRate = 0.0;
	double WheelSpin = 0.0;
	double SpeedIntegral = 0.0;
	/** Body-frame acceleration of the centre of mass, filtered; shifts the wheel loads. */
	FVector2D BodyAcceleration = FVector2D::ZeroVector;
	double TimeDebt = 0.0;
};
