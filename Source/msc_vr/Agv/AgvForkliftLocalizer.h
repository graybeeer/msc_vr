#pragma once

#include "CoreMinimal.h"
#include "AgvLocalizerComponent.h"
#include "AgvScanMatching.h"
#include "Subsystems/WorldSubsystem.h"
#include "AgvForkliftLocalizer.generated.h"

class AWarehouseForklift;
class UAgvShadowSensorsComponent;
class UPrimitiveComponent;

/**
 * LiDAR localization for the main warehouse forklift (2026-10-27, stage 1: shadow mode). It reads only what the
 * vehicle's hardware would give it and never moves anything:
 *  - odometry: the drive wheel's rim travel (wheel spin counts as travel), the steering angle and a yaw-rate gyro,
 *    with calibration errors and noise;
 *  - scans: the returns of the vehicle's own one-second LiDAR snapshot (UAgvShadowSensorsComponent);
 *  - map: the sensor kit's known map (building structure and fixed racks), rebuilt when that map changes.
 * Each new snapshot is matched to the 2D distance map and the odometry is corrected. The estimate is compared with
 * the exact pose to measure the error; travel still uses the exact pose until the estimate is accepted for control.
 */
UCLASS(ClassGroup=(Agv))
class MSC_VR_API UAgvForkliftLocalizerComponent : public UAgvLocalizerComponent
{
	GENERATED_BODY()

public:
	UAgvForkliftLocalizerComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Reference point (middle of the fixed load-wheel axle, world cm) and yaw (radians). */
	virtual bool GetPose(FVector2D& OutPosition, double& OutYaw) const override;
	virtual void InitializePose() override;
	virtual bool IsMappedStructure(const FVector2D& WorldPointCm, double ToleranceCm) const override;

	/** The actor transform the estimate implies (x, y, yaw estimated; height, pitch and roll as they are). */
	bool GetEstimatedActorTransform(FTransform& Out) const;

	UFUNCTION(BlueprintPure, Category="AGV|Localization")
	FString Summary() const;

	UFUNCTION(BlueprintCallable, Category="AGV|Localization")
	void ResetStatistics();

	// Odometry errors (same defaults as the test vehicle's localizer).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry")
	float WheelRadiusErrorPercent = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry")
	float SteerOffsetDeg = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry", meta=(ClampMin="0"))
	float EncoderNoisePercent = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry")
	float GyroBiasDegS = 0.01f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry", meta=(ClampMin="0"))
	float GyroNoiseDegSqrtS = 0.02f;

	/** Zero-velocity update: no encoder travel and a gyro reading below this (deg/s) holds the heading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry", meta=(ClampMin="0"))
	float StandstillGyroDegS = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry")
	int32 NoiseSeed = 11;

	// Map: known structure between these heights above the floor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Map", meta=(ClampMin="1"))
	float MapCellCm = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Map")
	float MapMinZCm = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Map")
	float MapMaxZCm = 300.f;

	// Scan matching.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="1"))
	float MatchVoxelCm = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="5"))
	float MatchDistanceCapCm = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="1"))
	float InlierDistanceCm = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="1"))
	float RobustScaleCm = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="0", ClampMax="1"))
	float MinInlierRatio = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="3"))
	int32 MinMatchPoints = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="1"))
	int32 MatchIterations = 20;

	/** Share of each accepted match applied at once (1 = jump to the match). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="0.05", ClampMax="1"))
	float CorrectionGain = 0.5f;

	/** Distance driven without an accepted match after which the estimate is declared lost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="1"))
	float LostAfterTravelCm = 1000.f;

	/** Writes one AGV_LOC_SHADOW log line per matched snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Localization")
	bool bLogMatches = true;

	// Status.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	FString StatusText = TEXT("NOT INITIALIZED");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	int32 AcceptedMatches = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	int32 RejectedMatches = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	int32 RecoveredMatches = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	int32 LastMatchPoints = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	float LastInlierRatio = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	float LastMatchMilliseconds = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	float LastCorrectionCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	float TravelSinceMatchCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	int32 MapBuilds = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	float MapBuildMilliseconds = 0.f;

	// Error against the exact pose (shadow measurement).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	float ErrorCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	float HeadingErrorDeg = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	float MaxErrorCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	float MeanErrorCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	float P95ErrorCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	float MaxHeadingErrorDeg = 0.f;

	/** Frames measured (estimate available) and frames the estimate was lost. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	int32 ErrorSamples = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	int32 LostFrames = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	int32 LostEvents = 0;

	/** Re-initialisations to the exact pose (elevator ride, relocation by the layout editor). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	int32 Reinitializations = 0;

	/** Measured / actual reference-point travel (least squares). About 1 when the encoder model is right. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	float EncoderScale = 0.f;

private:
	bool ResolveParts();
	void StepOdometry(float Dt);
	void CollectAndMatchScan();
	bool EnsureMap();
	void Measure();
	void ToMap(const FVector2D& Position, double Yaw, FVector2D& OutPosition, double& OutYaw) const;
	double Gauss();

	UPROPERTY(Transient) TObjectPtr<AWarehouseForklift> Forklift;
	UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> Body;
	UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> DriveWheel;
	UPROPERTY(Transient) TObjectPtr<UAgvShadowSensorsComponent> SensorKit;

	TSharedPtr<const FAgvDistanceMap> Map;
	uint64 MapKey = 0;
	int32 SeenMapRevision = -1;
	int32 SeenSnapshot = -1;

	FRandomStream Noise;
	bool bInitialized = false;
	bool bLost = false;
	bool bRiding = false;
	/** A match ran this frame; its log line is written after the error is measured. */
	bool bMatchToLog = false;
	float LastDt = 0.f;
	double WheelRadiusCm = 15.f;

	/** Fixed load-wheel axle middle in the actor frame (cm): the point that moves without side slip. */
	FVector2D ReferenceOffset = FVector2D::ZeroVector;
	/** Vehicle-frame box (cm) whose returns are the vehicle itself or its load. */
	FBox2D SelfFilter = FBox2D(ForceInit);

	// Odometry: reference-point pose in the odometry frame (cm, rad).
	FVector2D OdomPosition = FVector2D::ZeroVector;
	double OdomYaw = 0.0;

	// Correction map <- odometry: map = Rotate(CorrectionYaw) * odometry + CorrectionOffset.
	FVector2D CorrectionOffset = FVector2D::ZeroVector;
	double CorrectionYaw = 0.0;

	FVector LastTrueLocation = FVector::ZeroVector;
	double ErrorSum = 0.0;
	TArray<int32> ErrorHistogram;
	double EncoderMeasuredTrue = 0.0;
	double EncoderTrueSquared = 0.0;
	double MatchMillisecondsSum = 0.0;
};

/**
 * Attaches the forklift localizer to every main warehouse forklift when shadow localization is enabled: by the
 * command line (-AgvLocShadow) or in play with the console command agv.loc. Off by default: without either, nothing
 * is added and the game runs exactly as before. Also keeps the distance maps so vehicles on the same floor share one.
 */
UCLASS()
class MSC_VR_API UAgvLocalizationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Adds the localizer to every forklift present and to those spawned later. */
	void Enable();
	bool IsEnabled() const { return bEnabled; }
	FString Report() const;

	/** A map for this key, building it when no vehicle has built it yet. */
	TSharedPtr<const FAgvDistanceMap> FindOrBuildMap(uint64 Key, TFunctionRef<TSharedPtr<const FAgvDistanceMap>()> Build);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void Attach(AActor* Actor);

	bool bEnabled = false;
	FDelegateHandle SpawnHandle;
	TMap<uint64, TSharedPtr<const FAgvDistanceMap>> Maps;
};
