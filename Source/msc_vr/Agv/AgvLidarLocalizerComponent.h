#pragma once

#include "CoreMinimal.h"
#include "AgvLocalizerComponent.h"
#include "AgvLidarLocalizerComponent.generated.h"

class UAgvLidarComponent;
class UAgvTricycleDriveComponent;

/** 2D map the vehicle localizes against: distance (cm) from each cell to the nearest obstacle, capped. */
struct FAgvDistanceMap
{
	FVector2D Origin = FVector2D::ZeroVector; // world position of cell (0, 0)'s corner
	double CellCm = 5.0;
	int32 Width = 0;
	int32 Height = 0;
	double CapCm = 50.0;
	int32 OccupiedCells = 0;
	TArray<float> Distance;

	/** Bilinear distance at a world point; CapCm outside the map. */
	double Sample(const FVector2D& Point) const;
};

/**
 * Pose from what a real AGV has: wheel odometry (drive wheel encoder + steering encoder, with calibration errors and
 * noise; wheel spin goes straight into it) corrected by matching full LiDAR revolutions to a prebuilt 2D map.
 * The map is built once from the static level geometry in a height band. The correction is kept as a map <- odometry
 * transform, so the estimate stays smooth between scans and scan distortion is undone with the odometry.
 */
UCLASS(ClassGroup=(Agv), meta=(BlueprintSpawnableComponent))
class MSC_VR_API UAgvLidarLocalizerComponent : public UAgvLocalizerComponent
{
	GENERATED_BODY()

public:
	virtual bool GetPose(FVector2D& OutPosition, double& OutYaw) const override;
	virtual void Step(float Dt) override;
	virtual void InitializePose() override;
	virtual bool IsMappedStructure(const FVector2D& WorldPointCm, double ToleranceCm) const override;

	/** Shifts the estimate (world cm, degrees about the reference point): a wrong initial pose, a bump. */
	UFUNCTION(BlueprintCallable, Category="AGV|Localization")
	void OffsetEstimate(FVector2D OffsetCm, float YawDeg);

	/** Reports the actual pose instead (to test navigation on its own). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Localization")
	bool bIdealPose = false;

	/** Off = odometry only (for comparison). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Localization")
	bool bUseLidar = true;

	// Odometry errors: the controller's wheel radius and steering zero differ slightly from the real ones.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry")
	float WheelRadiusErrorPercent = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry")
	float SteerOffsetDeg = 0.2f;

	/** Standard deviation of the per-step travel error, percent of that step's travel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry", meta=(ClampMin="0"))
	float EncoderNoisePercent = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry")
	int32 NoiseSeed = 11;

	// Map: static geometry between these heights above the floor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Map", meta=(ClampMin="1"))
	float MapCellCm = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Map")
	float MapMinZCm = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Map")
	float MapMaxZCm = 300.f;

	// Scan matching.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="1"))
	float MatchVoxelCm = 10.f;

	/** Points farther than this from any mapped obstacle (people, pallets, unmapped things) do not pull the match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="5"))
	float MatchDistanceCapCm = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="1"))
	float InlierDistanceCm = 10.f;

	/** A match is accepted when at least this share of the points lies on the map. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="0", ClampMax="1"))
	float MinInlierRatio = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="3"))
	int32 MinMatchPoints = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="1"))
	int32 MatchIterations = 15;

	/** Share of each accepted match applied at once (smooths noise; 1 = jump to the match). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="0.05", ClampMax="1"))
	float CorrectionGain = 0.5f;

	/** Vehicle-frame box (cm) whose points are the vehicle itself or its load. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching")
	FBox2D SelfFilter = FBox2D(FVector2D(-110.0, -70.0), FVector2D(200.0, 70.0));

	/** Distance driven without an accepted match after which the estimate is declared lost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="1"))
	float LostAfterTravelCm = 1000.f;

	// Status.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	FString StatusText = TEXT("NOT INITIALIZED");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	int32 AcceptedMatches = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	int32 RejectedMatches = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	int32 LastMatchPoints = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	float LastInlierRatio = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	float LastMatchMilliseconds = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	float TravelSinceMatchCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	int32 MapOccupiedCells = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	float MapBuildMilliseconds = 0.f;

private:
	bool ResolveParts();
	void BuildMap();
	void MatchScan();
	/** Reference-point pose in the map from the odometry pose and the current correction. */
	void ToMap(const FVector2D& OdomPosition, double OdomYaw, FVector2D& OutPosition, double& OutYaw) const;

	UPROPERTY(Transient)
	TObjectPtr<UAgvTricycleDriveComponent> Drive;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAgvLidarComponent>> Lidars;

	TArray<int32> SeenRevolutions;
	TSharedPtr<FAgvDistanceMap> Map;
	FRandomStream Noise;
	bool bNoiseSeeded = false;
	bool bInitialized = false;
	bool bLost = false;

	// Odometry: reference-point pose in the odometry frame (cm, rad) and the last encoder reading.
	FVector2D OdomPosition = FVector2D::ZeroVector;
	double OdomYaw = 0.0;
	double LastWheelTravelCm = 0.0;

	// Correction map <- odometry: map = Rotate(CorrectionYaw) * odometry + CorrectionOffset.
	FVector2D CorrectionOffset = FVector2D::ZeroVector;
	double CorrectionYaw = 0.0;

	/** Points of the scan in progress, odometry frame (cm). */
	TArray<FVector2D> ScanPoints;
};
