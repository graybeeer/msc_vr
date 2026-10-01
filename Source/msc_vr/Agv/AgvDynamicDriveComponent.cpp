#include "AgvDynamicDriveComponent.h"
#include "AgvPath.h"
#include "GameFramework/Actor.h"

namespace
{
	const double StandardGravity = 9.81;
	const int32 SolverIterations = 12;
	/** Time constant of the acceleration that shifts the wheel loads (keeps the load / grip loop from ringing). */
	const double LoadTransferLag = 0.02;

	double Cross(const FVector2D& A, const FVector2D& B) { return A.X * B.Y - A.Y * B.X; }
	/** Velocity of a point at R from the centre of mass due to the yaw rate. */
	FVector2D Spin(double YawRate, const FVector2D& R) { return FVector2D(-YawRate * R.Y, YawRate * R.X); }
}

UAgvDynamicDriveComponent::UAgvDynamicDriveComponent()
{
	// Refined orange AGV: fixed support wheels beside the mast.
	FAgvPassiveWheel Wheel;
	Wheel.PositionCm = FVector2D(27.0, 45.0);
	PassiveWheels.Add(Wheel);
	Wheel.PositionCm = FVector2D(27.0, -45.0);
	PassiveWheels.Add(Wheel);
}

UAgvDynamicDriveComponent::FMassProperties UAgvDynamicDriveComponent::MassProperties() const
{
	FMassProperties Body;
	const FVector Chassis = ChassisCenterOfMassCm / 100.0;
	const FVector Payload = PayloadCenterOfMassCm / 100.0;
	Body.Mass = (double)ChassisMassKg + PayloadMassKg;
	Body.CenterOfMass = (Chassis * ChassisMassKg + Payload * PayloadMassKg) / Body.Mass;
	Body.YawInertia = (double)ChassisYawInertiaKgM2 + PayloadYawInertiaKgM2
		+ ChassisMassKg * FVector2D(Chassis - Body.CenterOfMass).SizeSquared()
		+ PayloadMassKg * FVector2D(Payload - Body.CenterOfMass).SizeSquared();
	return Body;
}

void UAgvDynamicDriveComponent::SetPayload(float MassKg, FVector InCenterOfMassCm, FVector2D SizeCm)
{
	// The state is kept at the centre of mass; moving it must not change how the chassis is moving.
	const FVector Before = MassProperties().CenterOfMass;
	PayloadMassKg = FMath::Max(0.f, MassKg);
	PayloadCenterOfMassCm = InCenterOfMassCm;
	PayloadYawInertiaKgM2 = (float)(PayloadMassKg * (SizeCm / 100.0).SizeSquared() / 12.0);
	const FVector After = MassProperties().CenterOfMass;
	CenterOfMassCm = After * 100.0;
	const double Yaw = FMath::DegreesToRadians(GetOwner()->GetActorRotation().Yaw);
	Velocity += Spin(YawRate, AgvMath::Rotate(Yaw, FVector2D(After - Before)));
}

void UAgvDynamicDriveComponent::Halt()
{
	Super::Halt();
	Velocity = FVector2D::ZeroVector;
	BodyAcceleration = FVector2D::ZeroVector;
	YawRate = WheelSpin = SpeedIntegral = TimeDebt = 0.0;
	MotorTorqueNm = DriveWheelSlipCmS = 0.f;
	bTipOver = false;
}

void UAgvDynamicDriveComponent::Step(float Dt)
{
	if (Dt <= 0.f)
	{
		return;
	}
	AActor* Owner = GetOwner();
	const FVector Location = Owner->GetActorLocation();
	double Yaw = FMath::DegreesToRadians(Owner->GetActorRotation().Yaw);
	const FMassProperties Body = MassProperties();
	CenterOfMassCm = Body.CenterOfMass * 100.0;
	const FVector2D LocalCenter(Body.CenterOfMass);
	FVector2D Center = FVector2D(Location) / 100.0 + AgvMath::Rotate(Yaw, LocalCenter);

	// Fixed substeps carry the remainder to the next frame, so the frame rate does not change the result.
	TimeDebt += Dt;
	while (TimeDebt >= SubstepSeconds - 1e-9)
	{
		Substep(SubstepSeconds, Body, Center, Yaw);
		TimeDebt -= SubstepSeconds;
	}

	const FVector2D Origin = (Center - AgvMath::Rotate(Yaw, LocalCenter)) * 100.0;
	Owner->SetActorLocationAndRotation(FVector(Origin.X, Origin.Y, Location.Z), FRotator(0.0, FMath::RadiansToDegrees(Yaw), 0.0));

	const FVector2D Reference = AgvMath::Rotate(Yaw, FVector2D(ReferenceOffsetCm / 100.0, 0.0) - LocalCenter);
	SpeedCmS = (float)(FVector2D::DotProduct(Velocity + Spin(YawRate, Reference), AgvMath::Dir(Yaw)) * 100.0);
	YawRateDegS = (float)FMath::RadiansToDegrees(YawRate);
}

void UAgvDynamicDriveComponent::SolveWheelLoads(const FMassProperties& Body, const TArray<FVector2D>& Contacts, TArray<double>& OutLoads)
{
	// Three-point support: total load = weight, and the load moments balance the inertial force at the centre of
	// mass height (accelerating loads the rear, braking the front, cornering the outside). Each wheel's load is a
	// plane over the contacts; with more than three wheels this is the equal-stiffness solution. A wheel that would
	// need a negative load lifts off.
	const int32 Count = Contacts.Num();
	const double Weight = Body.Mass * StandardGravity;
	const double Height = Body.CenterOfMass.Z;
	double Rhs[3] = { Weight, -Body.Mass * BodyAcceleration.X * Height, -Body.Mass * BodyAcceleration.Y * Height };
	TArray<bool> Active;
	Active.Init(true, Count);
	OutLoads.Init(0.0, Count);
	bool bStable = false;
	for (int32 Pass = 0; Pass < Count; ++Pass)
	{
		double A[3][3] = {};
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Active[Index])
			{
				const double Row[3] = { 1.0, Contacts[Index].X, Contacts[Index].Y };
				for (int32 I = 0; I < 3; ++I)
				{
					for (int32 J = 0; J < 3; ++J)
					{
						A[I][J] += Row[I] * Row[J];
					}
				}
			}
		}
		double B[3] = { Rhs[0], Rhs[1], Rhs[2] };
		double Plane[3];
		if (!AgvMath::Solve3(A, B, Plane))
		{
			break;
		}
		int32 Lowest = INDEX_NONE;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			OutLoads[Index] = Active[Index] ? Plane[0] + Plane[1] * Contacts[Index].X + Plane[2] * Contacts[Index].Y : 0.0;
			if (Active[Index] && (Lowest == INDEX_NONE || OutLoads[Index] < OutLoads[Lowest]))
			{
				Lowest = Index;
			}
		}
		if (Pass == 0)
		{
			StabilityMargin = (float)(OutLoads[Lowest] / Weight);
		}
		if (OutLoads[Lowest] >= 0.0)
		{
			bStable = true;
			break;
		}
		Active[Lowest] = false;
	}
	if (!bStable)
	{
		// The centre of mass is outside the support: the vehicle tips. Keep it driveable with the loads clamped.
		bTipOver = true;
		double Sum = 0.0;
		for (double& Load : OutLoads)
		{
			Load = FMath::Max(Load, 0.0);
			Sum += Load;
		}
		for (double& Load : OutLoads)
		{
			Load = Sum > 0.0 ? Load * Weight / Sum : Weight / Count;
		}
	}
}

void UAgvDynamicDriveComponent::Substep(double H, const FMassProperties& Body, FVector2D& Center, double& Yaw)
{
	const double Mass = Body.Mass;
	const double Inertia = Body.YawInertia;
	const double Radius = DriveWheelRadiusCm / 100.0;
	const FVector2D LocalCenter(Body.CenterOfMass);

	// Contact points relative to the centre of mass, body frame; index 0 is the drive wheel.
	TArray<FVector2D> Contacts{ FVector2D(DriveWheelOffsetCm / 100.0, 0.0) - LocalCenter };
	for (const FAgvPassiveWheel& Wheel : PassiveWheels)
	{
		Contacts.Add(Wheel.PositionCm / 100.0 - LocalCenter);
	}

	// The AGV's controller: inverse kinematics, steering servo, wheel speed loop -> motor torque.
	double TargetSteer, TargetSpeedCm;
	WheelTarget(TargetSteer, TargetSpeedCm);
	StepSteering(TargetSteer, TargetSpeedCm, (float)H);
	const double TargetSpin = TargetSpeedCm / 100.0 / Radius;
	// Holding brake: engaged once the wheel has stopped with nothing to do (also while steering on the spot).
	const bool bBrake = TargetSpin == 0.0 && FMath::Abs(WheelSpin * Radius) < 0.005;
	double Torque = 0.0;
	if (bBrake)
	{
		WheelSpin = SpeedIntegral = 0.0;
	}
	else if (SafetySpeedLimitCmS <= 0.f)
	{
		// Safety stop: the brake clamps the wheel as hard as it can (never reversing it); the tyre decides how much of
		// that reaches the ground, so a lightly loaded wheel locks and skids.
		const double Stop = FMath::Min((double)SafetyBrakeTorqueNm, FMath::Abs(WheelSpin) * DriveInertiaKgM2 / H);
		Torque = -FMath::Sign(WheelSpin) * Stop;
		SpeedIntegral = 0.0;
		WheelSpin += Torque / DriveInertiaKgM2 * H;
	}
	else
	{
		// Gains are scaled by the empty vehicle's inertia at the wheel: the controller does not know the payload.
		const double Error = TargetSpin - WheelSpin;
		const double NominalInertia = DriveInertiaKgM2 + ChassisMassKg * Radius * Radius;
		const double Integral = SpeedIntegral + Error * H;
		Torque = NominalInertia * (SpeedLoopGain * Error + SpeedLoopIntegralGain * Integral);
		const double Limit = FMath::Min((double)MaxDriveTorqueNm, MaxDrivePowerW / FMath::Max(FMath::Abs(WheelSpin), 1e-3));
		if (FMath::Abs(Torque) > Limit)
		{
			Torque = FMath::Sign(Torque) * Limit; // saturated: hold the integral (anti-windup)
		}
		else
		{
			SpeedIntegral = Integral;
		}
		WheelSpin += Torque / DriveInertiaKgM2 * H;
	}
	MotorTorqueNm = (float)Torque;

	TArray<double> Loads;
	SolveWheelLoads(Body, Contacts, Loads);
	if (!bBrake)
	{
		const double Drag = RollingResistance * Loads[0] * Radius / DriveInertiaKgM2 * H;
		WheelSpin = FMath::Sign(WheelSpin) * FMath::Max(FMath::Abs(WheelSpin) - Drag, 0.0);
	}

	// Tyre contacts as velocity constraints (sequential impulses): each wheel removes its slip, but the impulse it can
	// pass is limited by friction x its load. Beyond that the wheel spins, skids or slides sideways.
	const int32 Count = Contacts.Num();
	TArray<FVector2D> Arms, Rolling, Lateral;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Arms.Add(AgvMath::Rotate(Yaw, Contacts[Index]));
		const double Heading = Yaw + (Index == 0 ? FMath::DegreesToRadians((double)SteerAngleDeg) : 0.0);
		Rolling.Add(AgvMath::Dir(Heading));
		Lateral.Add(AgvMath::Normal(Heading));
		if (Index > 0 && PassiveWheels[Index - 1].bCaster)
		{
			// A caster swivels into its direction of travel, so it only resists rolling.
			const FVector2D Motion = Velocity + Spin(YawRate, Arms[Index]);
			Rolling[Index] = Motion.Size() > 1e-4 ? Motion.GetSafeNormal() : FVector2D::ZeroVector;
		}
	}
	TArray<double> RollingImpulse, LateralImpulse;
	RollingImpulse.Init(0.0, Count);
	LateralImpulse.Init(0.0, Count);
	const FVector2D StartVelocity = Velocity;

	auto Apply = [&](double Impulse, const FVector2D& Direction, const FVector2D& Arm, bool bTurnsWheel)
	{
		Velocity += Direction * (Impulse / Mass);
		YawRate += Impulse * Cross(Arm, Direction) / Inertia;
		if (bTurnsWheel)
		{
			WheelSpin -= Impulse * Radius / DriveInertiaKgM2;
		}
	};
	auto EffectiveMass = [&](const FVector2D& Direction, const FVector2D& Arm, double Extra)
	{
		return 1.0 / (1.0 / Mass + FMath::Square(Cross(Arm, Direction)) / Inertia + Extra);
	};

	for (int32 Iteration = 0; Iteration < SolverIterations; ++Iteration)
	{
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FVector2D& Arm = Arms[Index];
			const FVector2D& Along = Rolling[Index];
			const FVector2D& Side = Lateral[Index];
			const double Grip = FrictionCoefficient * Loads[Index] * H;
			const FVector2D Point = Velocity + Spin(YawRate, Arm);
			if (Index == 0)
			{
				// Drive wheel: rolling slip couples to the wheel spin; both directions share one friction circle.
				const double Rim = bBrake ? 0.0 : WheelSpin * Radius;
				const double WheelTerm = bBrake ? 0.0 : Radius * Radius / DriveInertiaKgM2;
				FVector2D Total(RollingImpulse[0] - (FVector2D::DotProduct(Point, Along) - Rim) * EffectiveMass(Along, Arm, WheelTerm),
					LateralImpulse[0] - FVector2D::DotProduct(Point, Side) * EffectiveMass(Side, Arm, 0.0));
				if (Total.Size() > Grip)
				{
					Total *= Grip / Total.Size();
				}
				Apply(Total.X - RollingImpulse[0], Along, Arm, !bBrake);
				Apply(Total.Y - LateralImpulse[0], Side, Arm, false);
				RollingImpulse[0] = Total.X;
				LateralImpulse[0] = Total.Y;
				continue;
			}
			const double Roll = RollingResistance * Loads[Index] * H;
			if (!Along.IsZero())
			{
				const double Old = RollingImpulse[Index];
				const double New = FMath::Clamp(Old - FVector2D::DotProduct(Point, Along) * EffectiveMass(Along, Arm, 0.0), -Roll, PassiveWheels[Index - 1].bCaster ? 0.0 : Roll);
				Apply(New - Old, Along, Arm, false);
				RollingImpulse[Index] = New;
			}
			if (!PassiveWheels[Index - 1].bCaster)
			{
				const FVector2D Now = Velocity + Spin(YawRate, Arm);
				const double Old = LateralImpulse[Index];
				const double New = FMath::Clamp(Old - FVector2D::DotProduct(Now, Side) * EffectiveMass(Side, Arm, 0.0), -Grip, Grip);
				Apply(New - Old, Side, Arm, false);
				LateralImpulse[Index] = New;
			}
		}
	}

	// Integrate, and keep the (filtered) body-frame acceleration for the next load transfer.
	const FVector2D Acceleration = (Velocity - StartVelocity) / H;
	const FVector2D BodyNow(FVector2D::DotProduct(Acceleration, AgvMath::Dir(Yaw)), FVector2D::DotProduct(Acceleration, AgvMath::Normal(Yaw)));
	BodyAcceleration += (BodyNow - BodyAcceleration) * FMath::Min(1.0, H / LoadTransferLag);
	Center += Velocity * H;
	Yaw += YawRate * H;

	// Wheel state and odometry.
	const double Rim = WheelSpin * Radius;
	WheelSpeedCmS = (float)(Rim * 100.0);
	DriveWheelTravelCm += Rim * H * 100.0;
	DriveWheelLoadN = (float)Loads[0];
	DriveWheelSlipCmS = (float)((Rim - FVector2D::DotProduct(Velocity + Spin(YawRate, Arms[0]), Rolling[0])) * 100.0);
	for (int32 Index = 1; Index < Count; ++Index)
	{
		FAgvPassiveWheel& Wheel = PassiveWheels[Index - 1];
		const FVector2D Point = Velocity + Spin(YawRate, Arms[Index]);
		Wheel.LoadN = (float)Loads[Index];
		if (!Wheel.bCaster)
		{
			Wheel.TravelCm += FVector2D::DotProduct(Point, AgvMath::Dir(Yaw)) * H * 100.0;
		}
		else if (Point.Size() > 1e-3)
		{
			Wheel.TravelCm += Point.Size() * H * 100.0;
			Wheel.SwivelDeg = (float)FMath::RadiansToDegrees(AgvMath::Wrap(FMath::Atan2(Point.Y, Point.X) - Yaw));
		}
	}
}
