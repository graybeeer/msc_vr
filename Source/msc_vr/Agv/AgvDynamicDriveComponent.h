#pragma once

#include "CoreMinimal.h"
#include "AgvDriveComponent.h"
#include "AgvDynamicDriveComponent.generated.h"

class UPrimitiveComponent;

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

	/** Vertical stiffness relative to the other wheels; spring-loaded stabiliser casters carry less of the weight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Wheels", meta=(ClampMin="0.01"))
	float Stiffness = 1.f;

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
 * Chaos rigid-body tricycle. Actual gravity, roll/pitch, impacts and motion belong to the engine.
 * An explicit contact tyre approximation applies bounded motor and lateral forces at real ground hits,
 * with equal opposite forces on dynamic supporting bodies. Welded tyre colliders supply normal contact only;
 * their native shear friction is zero to avoid counting tyre grip twice. No actor pose or velocity is assigned.
 */
UCLASS(ClassGroup=(Agv), meta=(BlueprintSpawnableComponent))
class MSC_VR_API UAgvDynamicDriveComponent : public UAgvTricycleDriveComponent
{
	GENERATED_BODY()

public:
	UAgvDynamicDriveComponent();

	/** Apply one controller update; requires real world physics ticks to advance motion. */
	UFUNCTION(BlueprintCallable, Category="AGV|Dynamics")
	virtual void Step(float Dt) override;
	UFUNCTION(BlueprintCallable, Category="AGV|Dynamics")
	virtual void Halt() override;
	virtual double GetPayloadKg() const override { return PayloadMassKg; }

	void SetPhysicsBody(UPrimitiveComponent* InBody);
	UFUNCTION(BlueprintPure, Category="AGV|Dynamics") UPrimitiveComponent* GetPhysicsBody() const { return PhysicsBody; }

	/** Physical load metadata for navigation/grip estimates. The load must exist as its own body; never adds its mass to the chassis. */
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

	/** Vertical stiffness of the drive wheel relative to the passive wheels (matters with more than three wheels). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="0.01"))
	float DriveWheelStiffness = 1.f;

	// Wheel speed controller, tuned for the empty vehicle: bandwidth (1/s) and integral gain (1/s^2).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="0"))
	float SpeedLoopGain = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="0"))
	float SpeedLoopIntegralGain = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics")
	TArray<FAgvPassiveWheel> PassiveWheels;

	/** Prototype rating; independent of the main WarehouseForklift's 1,400 kg rating. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Dynamics", meta=(ClampMin="0"))
	float RatedPayloadKg = 250.f;

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

	/** Wheel-speed command minus measured contact-point speed; the welded tyre has no independently simulated rotor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Dynamics|Status")
	float DriveWheelSlipCmS = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Dynamics|Status")
	float MotorTorqueNm = 0.f;

	/** Smallest wheel load as a fraction of the weight; below 0 a wheel would lift (the vehicle tips). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Dynamics|Status")
	float StabilityMargin = 0.f;

	/** Latched when the real body tips or its estimated support margin is negative. Chaos keeps simulating it. */
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
	void SolveWheelLoads(const FMassProperties& Body, const TArray<FVector2D>& Contacts, const TArray<double>& Stiffness, TArray<double>& OutLoads);
	void ReadPhysicsTelemetry();
	void ApplyContactForces(float Dt, const FMassProperties& Body);

	UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> PhysicsBody;
	double SpeedIntegral = 0.0;
	/** Body-frame acceleration of the centre of mass, filtered; shifts the wheel loads. */
	FVector2D BodyAcceleration = FVector2D::ZeroVector;
	FVector WorldAccelerationM = FVector::ZeroVector;
	FVector LastPhysicsVelocity = FVector::ZeroVector;
	bool bHaveVelocitySample = false;
};
