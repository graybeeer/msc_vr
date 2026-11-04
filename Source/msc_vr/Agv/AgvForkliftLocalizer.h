#pragma once

#include "CoreMinimal.h"
#include "AgvLocalizerComponent.h"
#include "AgvScanMatching.h"
#include "Subsystems/WorldSubsystem.h"
#include "AgvForkliftLocalizer.generated.h"

class AWarehouseForklift;
class UAgvShadowSensorsComponent;
class UAgvLidarComponent;
class UPrimitiveComponent;
class UCanvas;
class APlayerController;
class UTexture2D;

/**
 * LiDAR localization for the main warehouse forklift. It reads only what the
 * vehicle's hardware would give it and never moves anything:
 *  - odometry: the drive wheel's rim travel (wheel spin counts as travel), the steering angle and a yaw-rate gyro,
 *    with calibration errors and noise;
 *  - scans: the returns of the vehicle's own one-second LiDAR snapshot (UAgvShadowSensorsComponent);
 *  - map: the sensor kit's known map (building structure and fixed racks), rebuilt when that map changes.
 * Raw wheel/gyro samples are buffered, then integrated at the configured prediction rate (50 Hz target,
 * capped by the game frame rate). Scans remain one second and are matched at their capture time.
 * Each new snapshot is matched to the 2D distance map and the odometry is corrected. The estimate is compared with
 * the exact pose to measure the error. Autonomous route travel uses the estimate; precise docking stays native.
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

	/**
	 * The actor transform the estimate implies (x, y, yaw estimated; height, pitch and roll as they are). This is the
	 * driving estimate: bounded corrections are applied on prediction updates and held until the next. False when lost,
	 * unless bEvenIfLost (the dead-reckoned guess, for planning a stop).
	 */
	bool GetEstimatedActorTransform(FTransform& Out, bool bEvenIfLost = false) const;

	bool IsLost() const { return bInitialized && !bNoMap && (bLost || !bAvailable); }
	/** No known structure to localize against (only in test scenes): travel falls back to the exact pose. */
	bool HasNoMap() const { return bNoMap; }

	UFUNCTION(BlueprintPure, Category="AGV|Localization")
	FString Summary() const;

	// What the vehicle believes, for the view panel.
	TSharedPtr<const FAgvDistanceMap> GetMap() const { return Map; }
	/** Last matched snapshot's points placed with the estimate (map frame, cm) and whether each lies on the map. */
	const TArray<FVector2D>& GetLastScanInMap() const { return LastScanInMap; }
	const TArray<bool>& GetLastScanMapped() const { return LastScanMapped; }
	/** The vehicle body's outline in the actor frame (cm). */
	const FBox2D& GetVehicleBox() const { return VehicleBox; }
	AWarehouseForklift* GetForklift() const { return Forklift; }
	/** Last matched scan in 3D (map frame x, y; world height) and whether each point lies on the map. */
	const TArray<FVector>& GetLastScan3D() const { return LastScan3D; }
	const TArray<bool>& GetLastScan3DMapped() const { return LastScan3DMapped; }
	/** Where each point's beam left its scanner (map frame x, y; world height). */
	const TArray<FVector>& GetLastScan3DOrigins() const { return LastScan3DOrigins; }
	/** Counts matched scans, so a viewer can tell a new one arrived. */
	int32 GetScanSerial() const { return ScanSerial; }
	UAgvShadowSensorsComponent* GetSensorKit() const { return SensorKit; }

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

	/** Travel from encoders on the two free-rolling load wheels (off = the drive wheel's encoder). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry")
	bool bLoadWheelEncoders = true;

	/** Side slip of the load-wheel axle from drive wheel heading and gyro (off = the axle never slips sideways). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry")
	bool bModelSideSlip = true;

	/** Zero-velocity update: no encoder travel and a gyro reading below this (deg/s) holds the heading. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry", meta=(ClampMin="0"))
	float StandstillGyroDegS = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Odometry")
	int32 NoiseSeed = 11;

	/**
	 * Lowest return used for matching, above the floor. Side-scanner localization scans (5 Hz, 2026-10-27) were removed
	 * on 2026-10-28: after the load-wheel encoders they only pulled the estimate sideways into unmapped cargo.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Map")
	float PointMinZCm = 10.f;

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

	/** Ordinary point floor. A fully constrained sparse scan can use half, with extra geometry checks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="3"))
	int32 MinMatchPoints = 50;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="1"))
	int32 MatchIterations = 20;

	/**
	 * Share of each accepted match applied at once (1 = take the match). With one scan a second, 0.5 left about the
	 * odometry drift of a second uncorrected and doubled the >10 cm episodes when driving on the estimate (2026-10-27).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="0.05", ClampMax="1"))
	float CorrectionGain = 1.f;

	/** How fast an accepted scan correction reaches the driving estimate (cm/s, deg/s of simulated time). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="1"))
	float CorrectionRateCmS = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Matching", meta=(ClampMin="0.1"))
	float CorrectionRateDegS = 3.f;

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

	/** Supported pallet+payload, sampled from the existing load measurement once per localization snapshot. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	float SampledLoadKg = 0.f;

	/** Bounded load/slip model for translation uncertainty; never scales measured travel or gyro uncertainty. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	float LoadUncertaintyScale = 1.f;

	/** Scan correction rejected for contradicting the wheel/gyro prediction. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	int32 InnovationRejections = 0;

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
	/** Published odometry/scan updates. Raw encoder/gyro readings are buffered between updates. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	int32 PredictionUpdates = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	double LastPredictionSeconds = -1.0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Status")
	float LastPredictionDeltaSeconds = 0.f;

	/** Measured / actual reference-point travel (least squares). About 1 when the encoder model is right. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	float EncoderScale = 0.f;

	/** Modelled / actual side travel of the axle (least squares) and the accumulated root-sum-square difference (cm). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	float SideSlipScale = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Localization|Error")
	float SideSlipResidualCm = 0.f;

private:
	bool ResolveParts();
	struct FOdometrySample
	{
		float Dt;
		double RimRate, Steer, LoadRate, GyroRate;
		double TrueTravel, TrueSide; // Diagnostics only; never used to predict the pose.
	};
	void CaptureOdometry(float Dt);
	void StepOdometry(const FOdometrySample& Sample);
	void CollectAndMatchScan();
	bool EnsureMap();
	void Measure();
	void ToMap(const FVector2D& Position, double Yaw, FVector2D& OutPosition, double& OutYaw) const;
	void SlewDrivingCorrection(float Dt);
	double Gauss();

	UPROPERTY(Transient) TObjectPtr<AWarehouseForklift> Forklift;
	UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> Body;
	UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> DriveWheel;
	UPROPERTY(Transient) TArray<TObjectPtr<UPrimitiveComponent>> LoadWheels;
	double LoadWheelRadiusCm = 8.0;
	UPROPERTY(Transient) TObjectPtr<UAgvShadowSensorsComponent> SensorKit;

	TSharedPtr<const FAgvDistanceMap> Map;
	uint64 MapKey = 0;
	int32 SeenMapRevision = -1;
	int32 SeenSnapshot = -1;

	FRandomStream Noise;
	bool bInitialized = false;
	bool bLost = false;
	bool bRiding = false;
	bool bWasRemote = false;
	/** A match ran this frame; its log line is written after the error is measured. */
	bool bMatchToLog = false;
	bool bHighErrorLogged = false;
	float MatchAlongCm = 0.f;
	float MatchAcrossCm = 0.f;
	bool bNoMap = false;
	float LastDt = 0.f;
	TArray<FOdometrySample> PendingOdometry;
	double PendingOdometrySeconds = 0.0;
	double PredictionClockSeconds = 0.0;
	double WheelRadiusCm = 15.f;
	double WheelDisagreementCm = 0.0;

	/** Fixed load-wheel axle middle in the actor frame (cm): the point that moves without side slip. */
	FVector2D ReferenceOffset = FVector2D::ZeroVector;
	/** Vehicle-frame box (cm) whose returns are the vehicle itself or its load. */
	FBox2D SelfFilter = FBox2D(ForceInit);
	FBox2D VehicleBox = FBox2D(ForceInit);
	TArray<FVector2D> LastScanInMap;
	TArray<FVector> LastScan3D;
	TArray<bool> LastScan3DMapped;
	TArray<FVector> LastScan3DOrigins;
	int32 ScanSerial = 0;
	TArray<bool> LastScanMapped;

	// Odometry: reference-point pose in the odometry frame (cm, rad).
	FVector2D OdomPosition = FVector2D::ZeroVector;
	double OdomYaw = 0.0;
	double TurnSinceMatchRad = 0.0;

	// Correction map <- odometry: map = Rotate(CorrectionYaw) * odometry + CorrectionOffset.
	FVector2D CorrectionOffset = FVector2D::ZeroVector;
	double CorrectionYaw = 0.0;
	/** The correction the driving estimate uses: follows CorrectionOffset/Yaw at the correction rates. */
	FVector2D DrivingOffset = FVector2D::ZeroVector;
	double DrivingYaw = 0.0;

	FVector LastTrueLocation = FVector::ZeroVector;
	double ErrorSum = 0.0;
	TArray<int32> ErrorHistogram;
	double EncoderMeasuredTrue = 0.0;
	double EncoderTrueSquared = 0.0;
	double SideMeasuredTrue = 0.0;
	double SideTrueSquared = 0.0;
	double SideResidualSquared = 0.0;
	/** Load-wheel axle to drive wheel along the vehicle (cm, positive). */
	double DriveWheelbaseCm = 0.0;
	double MatchMillisecondsSum = 0.0;
};

/**
 * Attaches the forklift localizer to every main warehouse forklift when any of them drives on it
 * (AWarehouseForklift::bUseAgvLocalization, on by default), when -AgvLocShadow / -AgvLocDrive is given, or in play
 * with the console command agv.loc. Also keeps the distance maps so vehicles on the same floor share one.
 */
/**
 * What one vehicle's LiDAR has seen so far, for its top-view minimap: a 10cm occupancy grid. A cell a beam ends in gains
 * evidence of being occupied, a cell a beam passes through loses it, so a person or pallet that moves away fades out again.
 */
struct FAgvPanelGrid
{
	FVector2D Origin = FVector2D::ZeroVector;
	int32 Width = 0;
	int32 Height = 0;
	double CellCm = 10.0;
	/** Above 0 occupied, below 0 free, 0 never seen. */
	TArray<float> Evidence;
	/** Where the vehicle stood at the last added scan; each cell is shaded by its distance from there. */
	FVector2D Sensor = FVector2D::ZeroVector;
	/** The last hit lay on the prior map (green) or not (orange). */
	TArray<uint8> Mapped;
	int32 Serial = -1;
	double FloorZ = 0.0;
	bool bDirty = false;
};

/**
 * What the fleet's LiDARs have seen off the prior map on one floor, for route planning: a 10cm grid shared by all vehicles.
 * A hit raises a cell's evidence; a beam that later passes through the cell at a height where something was hit lowers it, so
 * a pallet carried away or a person who left fades out, while what stands behind a rack the vehicle now faces is kept.
 */
struct FAgvObstacleMemory
{
	FVector2D Origin = FVector2D::ZeroVector;
	int32 Width = 0;
	int32 Height = 0;
	double CellCm = 10.0;
	double FloorZ = 0.0;
	TArray<float> Evidence;
	/** Lowest and highest hit above the floor (cm), the band a passing beam has to cross to clear the cell. */
	TArray<float> MinZ;
	TArray<float> MaxZ;
	/** World time of the last hit. */
	TArray<float> LastHit;
};

UCLASS()
class MSC_VR_API UAgvLocalizationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	/** Adds the localizer to every forklift present and to those spawned later. */
	void Enable();
	/** Route travel on the estimate (AWarehouseForklift::bUseAgvLocalization) for every forklift, now and spawned later. */
	void SetDriveOnEstimate(bool bOn);
	bool IsEnabled() const { return bEnabled; }
	FString Report() const;

	/** A map for this key, building it when no vehicle has built it yet. */
	TSharedPtr<const FAgvDistanceMap> FindOrBuildMap(uint64 Key, TFunctionRef<TSharedPtr<const FAgvDistanceMap>()> Build);

	/** The top-left views of what forklifts 1 and 2 sense right now: their last scan and route. Number is 1 or 2. */
	void SetVehiclePanelVisible(int32 Number, bool bVisible) { if (Number >= 1 && Number <= 2) bShowVehicle[Number - 1] = bVisible; }
	bool IsVehiclePanelVisible(int32 Number) const { return Number >= 1 && Number <= 2 && bShowVehicle[Number - 1]; }
	/** Console shortcut (agv.loc view): forklift 1's view. */
	void SetPanelVisible(bool bVisible) { bShowVehicle[0] = bVisible; }
	/** 3D = forklift 1's first-person view of what it believes (default); off = the 2D top view. */
	void SetPanel3D(bool b3D) { bPanel3D = b3D; }
	bool IsPanel3D() const { return bPanel3D; }
	bool IsPanelVisible() const { return bShowVehicle[0]; }
	/** Off (default): sensor points only. On: also the prior map and the actual pose, for comparison. */
	void SetPanelMap(bool bShow) { bPanelMap = bShow; }
	bool IsPanelMap() const { return bPanelMap; }
	/** First-person eye at the fork end looking ahead (true) or at the body end looking back (false, default). */
	void SetPanelFront(bool bFront) { bPanelFront = bFront; }
	/** Top view: accumulate everything the LiDAR has seen into a minimap (default), or show only the current scan. */
	void SetPanelAccumulate(bool bAccumulate) { bPanelAccumulate = bAccumulate; }

	/** Adds the vehicle's newest matched scan to the fleet's obstacle memory of its floor. */
	void AddToObstacleMemory(UAgvForkliftLocalizerComponent* Vehicle);
	/**
	 * Cells of the floor's obstacle memory inside Area that hold something between ZMin and ZMax above the floor, seen at
	 * least twice and within MaxAgeSeconds. Returns the cell size, 0 when the floor has no memory.
	 */
	double GatherRememberedPoints(double FloorZ, const FBox2D& Area, double ZMin, double ZMax, double MaxAgeSeconds, TArray<FVector2D>& Out) const;
	/** Forgets what the memory holds inside Box, as where a pallet was lifted from. */
	void ForgetObstacles(double FloorZ, const FBox2D& Box);

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void Attach(AActor* Actor);
	void OnActorSpawned(AActor* Actor);
	void DrawPanel(UCanvas* Canvas, APlayerController* Player);
	/** One vehicle's view with its top-left corner at Origin; returns the height it used, captions included. */
	double DrawTopPanel(UCanvas* Canvas, UAgvForkliftLocalizerComponent* Vehicle, FVector2D Origin, double Size, int32 Number);
	double DrawFirstPersonPanel(UCanvas* Canvas, UAgvForkliftLocalizerComponent* Vehicle, FVector2D Origin, double Width, int32 Number);
	/** The localizer of forklift 1 or 2 by its name, else by actor name order. */
	UAgvForkliftLocalizerComponent* FindPanelVehicle(int32 Number) const;
	void UpdateMapTexture(const TSharedPtr<const FAgvDistanceMap>& Map);
	/** Adds the vehicle's newest scan to its minimap grid and refreshes the grid's texture. */
	void UpdatePanelGrid(int32 Number, UAgvForkliftLocalizerComponent* Vehicle);

	bool bEnabled = false;
	bool bDriveOnEstimate = false;
	/** Set by -AgvLocDrive or agv.loc drive: applies bDriveOnEstimate to vehicles spawned later as well. */
	bool bDriveOverride = false;
	bool bShowVehicle[2] = { false, false };
	bool bPanel3D = true;
	bool bPanelMap = false;
	bool bPanelFront = false;
	bool bPanelAccumulate = true;
	/** Keyed by the floor height in metres. */
	TMap<int32, FAgvObstacleMemory> ObstacleMemory;
	/** The pallet each vehicle carried at its previous scan, to forget where a newly lifted one stood. */
	TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<AActor>> CarriedAtLastScan;
	FAgvPanelGrid PanelGrids[2];
	UPROPERTY(Transient) TArray<TObjectPtr<UTexture2D>> PanelGridTextures;
	FDelegateHandle SpawnHandle;
	FDelegateHandle DrawHandle;
	TMap<uint64, TSharedPtr<const FAgvDistanceMap>> Maps;

	UPROPERTY(Transient) TObjectPtr<UTexture2D> MapTexture;
	/** Held so the texture is never matched to a different map at the same address. */
	TSharedPtr<const FAgvDistanceMap> TextureMap;
};
