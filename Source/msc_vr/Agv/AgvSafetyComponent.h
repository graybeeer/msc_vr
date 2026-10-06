#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgvSafetyComponent.generated.h"

class UAgvDriveComponent;
class UAgvLidarComponent;
class UAgvLocalizerComponent;
class UAgvNavigatorComponent;

UENUM(BlueprintType)
enum class EAgvSafetyState : uint8 { Clear, Warning, Stop };

/**
 * Safety field evaluation over all safety sensors, switched like a safety controller's field sets on the intended
 * travel direction, speed band and steering band. The protective field is the area the vehicle footprint sweeps
 * along the planned path (cut at the next stop; the current steering arc when no path is being followed) over the
 * stopping distance (response + braking + margin, never shorter than
 * MinProtectiveLengthCm); turning on the spot sweeps the footprint through RotationLookaheadDeg (a planned turn: only
 * through the rotation still to go plus its stopping angle). An object in it ->
 * stop (and full braking in the drive); in the longer warning sweep -> slow. After a stop the vehicle restarts on its
 * own once the field has stayed clear for RestartDelaySeconds. Forks-first travel is capped at ForksFirstMaxSpeedCm.
 * What a sensor cannot see (blocked by the vehicle itself, below its beam) it cannot protect.
 */
UCLASS(ClassGroup=(Agv), meta=(BlueprintSpawnableComponent))
class MSC_VR_API UAgvSafetyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgvSafetyComponent();

	/** Evaluates the sensors' completed scans; called by the owning vehicle after the sensors have stepped. */
	void Step(float Dt);

	/** Forget the scans and any stop: the vehicle was put down somewhere else (an operator relocating it). */
	UFUNCTION(BlueprintCallable, Category="AGV|Safety")
	void ResetScans();

	/** Off only for tests of the bare vehicle (no fields, no limits). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety")
	bool bEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety")
	TArray<TObjectPtr<UAgvLidarComponent>> Scanners;

	/** Sweep the fields along the planned path (cut at the next stop). Off = along the current steering arc only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety")
	bool bFieldsFollowPath = true;

	/**
	 * The warning field (slow down, not a safety function) ignores points on mapped static structures: racks, walls and
	 * pillars the route was laid out around. People, protruding loads and fallen cargo are not in the map and still
	 * count. The protective field always counts every point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety")
	bool bWarningIgnoresMappedStructure = true;

	/** How close to a mapped surface a point must be to count as that structure (localization error + map cell). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float MappedStructureToleranceCm = 15.f;

	/** Whole vehicle outline in the actor frame (body + forks); the field is this outline swept along the path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety")
	FBox2D Footprint = FBox2D(FVector2D(-94.0, -61.0), FVector2D(182.0, 61.0));

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float SideMarginCm = 10.f;

	/** Scanner + controller + brake reaction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float ResponseSeconds = 0.2f;

	/** Braking the fields are designed for: the worst case (full load), not the best. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="1"))
	float FieldDecelerationCm = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float FieldMarginCm = 50.f;

	/** The protective field is never shorter than this, so even a slow approach stops well clear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float MinProtectiveLengthCm = 100.f;

	/** Field sets switch in these speed and steering steps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="1"))
	float SpeedBandCm = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="1"))
	float SteerBandDeg = 10.f;

	/** Smallest field: the one needed to start moving again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float StartSpeedCm = 30.f;

	/**
	 * Fixed, so slowing down does not shrink it off the object (which would make the vehicle speed up again).
	 * Must reach beyond the protective field at full speed plus the distance to slow to WarningSpeedCm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float WarningFieldLengthCm = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float WarningExtraWidthCm = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float WarningSpeedCm = 30.f;

	/** Turning on the spot: how far ahead the sweep looks (protective / warning). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="1", ClampMax="180"))
	float RotationLookaheadDeg = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="1", ClampMax="180"))
	float WarningRotationDeg = 60.f;

	/**
	 * With bFieldsFollowPath, a planned turn on the spot cuts both rotation fields at the rotation still to go plus the
	 * angle to stop from the current yaw rate (ResponseSeconds, RotationDecelerationDeg) plus this margin, as the
	 * travel fields end at the next stop: a 1 deg alignment beside a column does not guard 30 deg of swing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float RotationMarginDeg = 3.f;

	/** Yaw deceleration the stopping angle assumes (deg/s^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="1"))
	float RotationDecelerationDeg = 90.f;

	/** Forks-first travel has less sensor coverage (and usually a load in the way), so it is slowed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="1"))
	float ForksFirstMaxSpeedCm = 50.f;

	/** Points between these heights above the floor are obstacles (below: the floor; above: clears the vehicle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety")
	float MinObstacleHeightCm = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety")
	float MaxObstacleHeightCm = 220.f;

	/** Beams that must hit inside a field in one evaluation (object resolution; ignores single noisy returns). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="1"))
	int32 MinObjectPoints = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float RestartDelaySeconds = 2.f;

	/** The warning field must stay clear this long before full speed is allowed again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float WarningReleaseSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Debug")
	bool bDrawFields = true;

	// Status.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	EAgvSafetyState State = EAgvSafetyState::Clear;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	FString StatusText = TEXT("CLEAR");

	/** Active field set: BODY FIRST / FORKS FIRST / ROTATE LEFT / ROTATE RIGHT. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	FString FieldSet = TEXT("BODY FIRST");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	float ProtectiveLengthCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	float WarningLengthCm = 0.f;

	/** Path length (cm, or degrees when rotating) to the nearest object inside the warning sweep; -1 = none. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	float NearestObjectCm = -1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	int32 SafetyStops = 0;

	/** Warning-field points dropped in the last evaluation because they lie on mapped structure. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	int32 MappedPointsIgnored = 0;

private:
	/** A footprint pose along the swept path, relative to the current reference point (cm, rad), and its path length. */
	struct FSweepPose
	{
		FVector2D Position;
		double Yaw;
		double Along;
	};
	void BuildSweep();
	void Evaluate();
	void DrawFields() const;

	UPROPERTY(Transient)
	TObjectPtr<UAgvDriveComponent> Drive;

	UPROPERTY(Transient)
	TObjectPtr<UAgvNavigatorComponent> Navigator;

	UPROPERTY(Transient)
	TObjectPtr<UAgvLocalizerComponent> Localizer;

	TArray<TArray<FVector>> PendingPoints;
	TArray<TArray<FVector>> LatestPoints;
	TArray<int32> SeenRevolutions;
	TArray<FSweepPose> Sweep;
	double ReferenceX = 0.0;
	/** Protective reach of the rotation sweep this step (deg). */
	double ProtectiveRotationDeg = 0.0;
	double ClearSeconds = 0.0;
	double WarningClearSeconds = 1e9; // starts released
	/** Speed cap while slowing down in the warning field (ramps at FieldDecelerationCm). */
	double WarningRampCm = TNumericLimits<float>::Max();
	bool bProtectiveHit = false;
	bool bWarningHit = false;
	bool bRotating = false;
};
