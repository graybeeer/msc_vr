#include "AgvLocalizerComponent.h"
#include "AgvDriveComponent.h"
#include "GameFramework/Actor.h"

UAgvLocalizerComponent::UAgvLocalizerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UAgvLocalizerComponent::GetPose(FVector2D& OutPosition, double& OutYaw) const
{
	const UAgvDriveComponent* Drive = GetOwner()->FindComponentByClass<UAgvDriveComponent>();
	if (!bAvailable || !Drive)
	{
		return false;
	}
	Drive->GetTruePose(OutPosition, OutYaw);
	OutPosition += PositionBiasCm;
	OutYaw += FMath::DegreesToRadians((double)YawBiasDeg);
	return true;
}

bool UAgvLocalizerComponent::GetEstimatedPose(FVector2D& PositionCm, float& YawDeg) const
{
	double Yaw = 0.0;
	const bool bValid = GetPose(PositionCm, Yaw);
	YawDeg = (float)FMath::RadiansToDegrees(Yaw);
	return bValid;
}
