#include "AgvDriveComponent.h"
#include "AgvPath.h"
#include "GameFramework/Actor.h"

UAgvDriveComponent::UAgvDriveComponent()
{
	// Stepped by the owning vehicle so navigation and drive run in a fixed order.
	PrimaryComponentTick.bCanEverTick = false;
}

void UAgvDriveComponent::SetCommand(double InSpeedCmS, double InYawRateDegS)
{
	CommandSpeedCmS = InSpeedCmS;
	CommandYawRateDegS = InYawRateDegS;
}

void UAgvDriveComponent::Halt()
{
	CommandSpeedCmS = CommandYawRateDegS = 0.0;
	SpeedCmS = YawRateDegS = 0.f;
}

void UAgvDriveComponent::GetTruePose(FVector2D& OutPosition, double& OutYaw) const
{
	const AActor* Owner = GetOwner();
	OutYaw = FMath::DegreesToRadians(Owner->GetActorRotation().Yaw);
	OutPosition = FVector2D(Owner->GetActorLocation()) + AgvMath::Dir(OutYaw) * ReferenceOffsetCm;
}

void UAgvDriveComponent::MoveReference(double Speed, double YawRateDeg, float Dt)
{
	SpeedCmS = (float)Speed;
	YawRateDegS = (float)YawRateDeg;

	FVector2D Reference;
	double Yaw;
	GetTruePose(Reference, Yaw);
	const double NewYaw = Yaw + FMath::DegreesToRadians(YawRateDeg) * Dt;
	Reference += AgvMath::Dir((Yaw + NewYaw) * 0.5) * (Speed * Dt);
	const FVector2D Origin = Reference - AgvMath::Dir(NewYaw) * ReferenceOffsetCm;

	AActor* Owner = GetOwner();
	Owner->SetActorLocationAndRotation(FVector(Origin.X, Origin.Y, Owner->GetActorLocation().Z), FRotator(0.0, FMath::RadiansToDegrees(NewYaw), 0.0));
}

void UAgvKinematicDriveComponent::Step(float Dt)
{
	MoveReference(FMath::FInterpConstantTo(SpeedCmS, (float)CommandSpeedCmS, Dt, MaxLinearAccelCm),
		FMath::FInterpConstantTo(YawRateDegS, (float)CommandYawRateDegS, Dt, MaxYawAccelDeg), Dt);
}

void UAgvTricycleDriveComponent::Halt()
{
	Super::Halt();
	WheelSpeedCmS = 0.f;
}

void UAgvTricycleDriveComponent::WheelTarget(double& OutSteer, double& OutSpeedCm) const
{
	// Inverse kinematics: the drive wheel sits Wheelbase behind the reference point, so its velocity in the
	// vehicle frame is (v, -w * Wheelbase). Steer = its direction, wheel speed = its magnitude.
	const double MaxSteer = FMath::DegreesToRadians((double)MaxSteerAngleDeg);
	const double Steer = FMath::DegreesToRadians((double)SteerAngleDeg);
	const FVector2D WheelVelocity(CommandSpeedCmS, -FMath::DegreesToRadians(CommandYawRateDegS) * WheelbaseCm());
	OutSteer = Steer;
	OutSpeedCm = 0.0;
	if (WheelVelocity.Size() <= 0.01)
	{
		return;
	}
	// (angle, speed) and (angle + 180, -speed) are the same motion; take the one in range nearest the current steer.
	const double Direction = FMath::Atan2(WheelVelocity.Y, WheelVelocity.X);
	double Best = TNumericLimits<double>::Max();
	for (const double Sign : { 1.0, -1.0 })
	{
		const double Candidate = AgvMath::Wrap(Direction + (Sign < 0.0 ? UE_DOUBLE_PI : 0.0));
		const double Clamped = FMath::Clamp(Candidate, -MaxSteer, MaxSteer);
		const double Cost = FMath::Abs(Clamped - Candidate) * 10.0 + FMath::Abs(Clamped - Steer);
		if (Cost < Best)
		{
			Best = Cost;
			OutSteer = Clamped;
			OutSpeedCm = Sign * WheelVelocity.Size();
		}
	}
	// The motor and safety limits scale speed and yaw rate together, so the curve stays the same.
	const double Limit = FMath::Min((double)MaxWheelSpeedCm, (double)SafetySpeedLimitCmS);
	OutSpeedCm = FMath::Clamp(OutSpeedCm, -Limit, Limit);
}

void UAgvTricycleDriveComponent::StepSteering(double TargetSteer, double& InOutSpeedCm, float Dt)
{
	// The steering turns at a limited rate; the wheel only pushes along the part of the command it is
	// already pointing at, and from (near) standstill waits until the steering is aligned.
	const double Steer = FMath::DegreesToRadians((double)SteerAngleDeg);
	const double SteerStep = FMath::DegreesToRadians((double)MaxSteerRateDeg) * Dt;
	const double NewSteer = Steer + FMath::Clamp(TargetSteer - Steer, -SteerStep, SteerStep);
	const double SteerError = FMath::Abs(TargetSteer - NewSteer);
	InOutSpeedCm *= FMath::Max(0.0, FMath::Cos(SteerError));
	if (FMath::Abs(WheelSpeedCmS) < SteerHoldSpeedCm && SteerError > FMath::DegreesToRadians((double)SteerAlignToleranceDeg))
	{
		InOutSpeedCm = 0.0;
	}
	SteerAngleDeg = (float)FMath::RadiansToDegrees(NewSteer);
}

void UAgvTricycleDriveComponent::Step(float Dt)
{
	if (Dt <= 0.f)
	{
		return;
	}
	double TargetSteer, TargetSpeed;
	WheelTarget(TargetSteer, TargetSpeed);
	StepSteering(TargetSteer, TargetSpeed, Dt);
	WheelSpeedCmS = FMath::FInterpConstantTo(WheelSpeedCmS, (float)TargetSpeed, Dt, MaxWheelAccelCm);

	// Forward kinematics from what the wheel actually does.
	const double Steer = FMath::DegreesToRadians((double)SteerAngleDeg);
	const double Speed = WheelSpeedCmS * FMath::Cos(Steer);
	const double YawRate = -WheelSpeedCmS * FMath::Sin(Steer) / WheelbaseCm();
	DriveWheelTravelCm += WheelSpeedCmS * Dt;
	SupportWheelTravelPosYCm += (Speed - YawRate * SupportWheelHalfTrackCm) * Dt;
	SupportWheelTravelNegYCm += (Speed + YawRate * SupportWheelHalfTrackCm) * Dt;
	MoveReference(Speed, FMath::RadiansToDegrees(YawRate), Dt);
}
