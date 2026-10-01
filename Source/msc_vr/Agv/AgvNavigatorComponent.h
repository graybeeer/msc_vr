#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgvPath.h"
#include "AgvNavigatorComponent.generated.h"

class UAgvDriveComponent;
class UAgvLocalizerComponent;

UENUM(BlueprintType)
enum class EAgvNavState : uint8 { Idle, Pivot, Follow, Arrived, Fault };

/**
 * Turns "go to node X" into speed / yaw-rate commands: route search on the node network,
 * line + arc path, speed profile and path tracking. Knows nothing about wheels.
 */
UCLASS(ClassGroup=(Agv), meta=(BlueprintSpawnableComponent))
class MSC_VR_API UAgvNavigatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgvNavigatorComponent();

	/** Plans over the AAgvRouteNode network and starts driving. False (see StatusText) when rejected. */
	UFUNCTION(BlueprintCallable, Category="AGV|Navigation")
	bool GoToNode(FName NodeId);

	/** Drives through world points directly, without the node network. */
	UFUNCTION(BlueprintCallable, Category="AGV|Navigation")
	bool FollowPoints(const TArray<FVector>& Points, float CornerRadiusCm = 120.f);

	/** Commands a stop and drops the path. */
	UFUNCTION(BlueprintCallable, Category="AGV|Navigation")
	void Cancel();

	UFUNCTION(BlueprintPure, Category="AGV|Navigation")
	bool IsNavigating() const { return State == EAgvNavState::Pivot || State == EAgvNavState::Follow; }

	/** One control cycle; called by the owning vehicle before the drive is stepped. */
	void Step(float Dt);

	// Limits. Defaults follow VNSL14_SPEC.md; wheel-dependent values stay configurable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits", meta=(ClampMin="1"))
	float MaxSpeedCm = 130.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits", meta=(ClampMin="1"))
	float AccelCm = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits", meta=(ClampMin="1"))
	float DecelCm = 50.f;

	/** Sideways acceleration allowed in a curve; sets the cornering speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits", meta=(ClampMin="1"))
	float MaxLateralAccelCm = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits", meta=(ClampMin="1"))
	float MaxYawRateDeg = 45.f;

	/** The vehicle's local -X leads (VNSL14 main travel direction: forks trailing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits")
	bool bDriveReversed = true;

	// Path shape.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Path", meta=(ClampMin="0"))
	float DefaultCornerRadiusCm = 120.f;

	/** A corner whose arc cannot reach this radius is taken by stopping and pivoting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Path", meta=(ClampMin="1"))
	float MinCornerRadiusCm = 60.f;

	/** Within this distance of a lane the vehicle joins the lane; farther away it first drives to the nearest node. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Path", meta=(ClampMin="0"))
	float NetworkSnapCm = 50.f;

	// Tracking.
	/** Travel distance over which a path error is steered out. Shorter = stiffer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Tracking", meta=(ClampMin="5"))
	float ConvergenceLengthCm = 60.f;

	/** Tightest curve the tracking correction may request. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Tracking", meta=(ClampMin="1"))
	float MinTrackingRadiusCm = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Tracking", meta=(ClampMin="0"))
	float DeviationWarnCm = 10.f;

	/** Estimated sideways error that stops the vehicle with a PATH DEVIATION fault. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Tracking", meta=(ClampMin="0"))
	float DeviationStopCm = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Tracking", meta=(ClampMin="0.1"))
	float StopToleranceCm = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Tracking", meta=(ClampMin="0.05"))
	float HeadingToleranceDeg = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Debug")
	bool bDrawDebug = true;

	// Status.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Status")
	EAgvNavState State = EAgvNavState::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Status")
	FString StatusText = TEXT("IDLE");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Status")
	TArray<FName> PlannedNodeIds;

	/** Sideways / heading error as the vehicle believes them (from the localizer). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Status")
	float CrossTrackErrorCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Status")
	float HeadingErrorDeg = 0.f;

	/** Actual sideways error, for evaluation only; the controller never reads it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Status")
	float TrueCrossTrackErrorCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Status")
	float MaxTrueCrossTrackErrorCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Status")
	float RemainingCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Status")
	bool bDeviationWarning = false;

private:
	bool ResolveParts();
	bool StartPath();
	void BeginLeg();
	void BeginFollow();
	void StepPivot(double Yaw);
	void StepFollow(const FVector2D& Position, double Yaw, float Dt);
	void Finish(EAgvNavState NewState, const FString& Message);
	double ProfileSpeed(double S) const;
	void DrawPath() const;

	UPROPERTY(Transient)
	TObjectPtr<UAgvDriveComponent> Drive;

	UPROPERTY(Transient)
	TObjectPtr<UAgvLocalizerComponent> Localizer;

	FAgvPath Path;
	int32 LegIndex = 0;
	double LegS = 0.0;
	double CommandSpeed = 0.0;
	double PivotTargetYaw = 0.0;
	bool bFinalPivot = false;
	/** Speed allowed at every ProfileStepCm along the current leg, already limited for braking. */
	TArray<double> SpeedProfile;
};
