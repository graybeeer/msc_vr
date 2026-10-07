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

	/**
	 * Reference-point poses (world X, Y and vehicle yaw in radians as Z) along the planned path from where the vehicle
	 * is now, every StepCm, up to DistanceCm or the next stop (a pivot or the destination). False when not following a path.
	 */
	bool GetPathAhead(double DistanceCm, double StepCm, TArray<FVector>& OutPoses) const;

	/** Whether the vehicle's local -X leads on the leg being driven (or about to be). */
	bool IsLegReversed() const { return Path.Legs.IsValidIndex(LegIndex) ? Path.Legs[LegIndex].bReverse : bDriveReversed; }

	/** While turning on the spot: the rotation still to go (deg, signed, + = left). False otherwise. */
	bool GetPivotRemainingDeg(double& OutDeg) const
	{
		OutDeg = HeadingErrorDeg;
		return State == EAgvNavState::Pivot;
	}

	// Limits. Defaults follow VNSL14_SPEC.md; wheel-dependent values stay configurable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits", meta=(ClampMin="1"))
	float MaxSpeedCm = 130.f;

	/** Travel speed limit while carrying a load (payload above LoadedThresholdKg). VNSL14: 1.0 m/s loaded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits", meta=(ClampMin="1"))
	float LoadedMaxSpeedCm = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits", meta=(ClampMin="0"))
	float LoadedThresholdKg = 5.f;

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

	/**
	 * Pick each leg's direction (local -X or +X leading) for the shortest estimated time: travel at MaxSpeedCm with -X
	 * (the body) leading or ForksFirstSpeedCm with the forks leading, plus the turns on the spot. A turn larger than
	 * PivotCheckMinDeg is only planned where the swept footprint (PivotFootprint + PivotClearanceCm) clears the world:
	 * a long vehicle cannot turn round in a narrow lane, so there it backs up instead (as real AGVs do). Off:
	 * every leg is driven with bDriveReversed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits")
	bool bChooseLegDirection = true;

	/** Travel speed with the forks (local +X) leading, for the direction choice (the safety system enforces it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits", meta=(ClampMin="1"))
	float ForksFirstSpeedCm = 30.f;

	/** Time a turn on the spot costs beyond the rotation itself (steering round, settling), for the direction choice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits", meta=(ClampMin="0"))
	float PivotOverheadSeconds = 2.f;

	/** Vehicle outline in the actor frame (cm) swept to check room for a turn on the spot. Empty: no check. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits")
	TArray<FBox2D> PivotFootprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits", meta=(ClampMin="0"))
	float PivotClearanceCm = 10.f;

	/** Turns up to this angle (alignments) are not checked for room. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits", meta=(ClampMin="0"))
	float PivotCheckMinDeg = 10.f;

	/** Heights above the floor (actor origin) the room check covers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Limits")
	FVector2D PivotCheckHeightCm = FVector2D(10.0, 220.0);

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

	/**
	 * The path curvature is read this far ahead in time, so the steering starts turning before an arc begins and
	 * reaches it about when the vehicle does (the steering takes time to swing). 0 = no preview.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Tracking", meta=(ClampMin="0"))
	float CurvaturePreviewSeconds = 0.4f;

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
	void ChooseLegDirections(FVector2D Position, double Yaw);
	/** Whether turning on the spot about the reference point at Position from FromYaw to ToYaw (shortest way) has room. */
	bool CanPivot(const FVector2D& Position, double FromYaw, double ToYaw) const;
	void BeginLeg();
	void BeginFollow();
	void StepPivot(double Yaw);
	void StepFollow(const FVector2D& Position, double Yaw, float Dt);
	void Finish(EAgvNavState NewState, const FString& Message);
	double ProfileSpeed(double S) const;
	/** MaxSpeedCm, or LoadedMaxSpeedCm while carrying a load. */
	double TravelMaxSpeed() const;
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
