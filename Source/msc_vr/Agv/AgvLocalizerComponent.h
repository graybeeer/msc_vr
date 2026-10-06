#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgvLocalizerComponent.generated.h"

/**
 * Where the vehicle believes it is. Navigation steers on this estimate only.
 * This base class reports the true pose plus an injected error; a LiDAR localizer will override GetPose.
 */
UCLASS(ClassGroup=(Agv), meta=(BlueprintSpawnableComponent))
class MSC_VR_API UAgvLocalizerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgvLocalizerComponent();

	/** Estimated reference-point position and vehicle yaw (radians). False when no estimate is available. */
	virtual bool GetPose(FVector2D& OutPosition, double& OutYaw) const;

	/** One update; called by the owning vehicle after the drive and sensors have stepped. */
	virtual void Step(float Dt) {}

	/** Sets the estimate to the vehicle's actual pose, as an operator does when placing the AGV. */
	virtual void InitializePose() {}

	/** True when a world point (cm) lies on a mapped static structure (rack, wall, pillar). False without a map. */
	virtual bool IsMappedStructure(const FVector2D& WorldPointCm, double ToleranceCm) const { return false; }

	/** GetPose for Blueprint / Python: reference point (cm) and yaw (degrees). */
	UFUNCTION(BlueprintCallable, Category="AGV|Localization")
	bool GetEstimatedPose(FVector2D& PositionCm, float& YawDeg) const;

	/** Training fault: the estimate is lost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Localization")
	bool bAvailable = true;

	/** Training fault: world-space offset added to the estimate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Localization")
	FVector2D PositionBiasCm = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Localization")
	float YawBiasDeg = 0.f;
};
