#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseAutonomyTypes.h"
#include "WarehouseForklift.generated.h"

class AWarehousePallet;
class UPointLightComponent;
class UTextRenderComponent;
class UStaticMeshComponent;
class AWarehouseChargingStation;
class AWarehouseWaitingBay;
class AWarehouseElevator;
class UPhysicsConstraintComponent;
class AWarehouseFleetCoordinator;
class UAgvShadowSensorsComponent;
class UPrimitiveComponent;
struct FWarehouseNavigationQuery;

UENUM(BlueprintType)
enum class EWarehouseSensorMode : uint8 { Disabled, ObserveOnly, Autonomous };

UENUM(BlueprintType)
enum class EWarehouseCycle : uint8 { Idle, Approach, Lift, TravelLower, Reverse, Lower, Withdraw, Complete, ToCharger, Docking, Charging, Returning };

UCLASS()
class MSC_VR_API AWarehouseForklift : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseForklift();
 virtual void BeginPlay() override;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autonomy") bool bAutoStart=false;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Training") FString VehicleName=TEXT("무인 지게차");
 UFUNCTION(BlueprintCallable, Category="Remote") bool BeginRemoteControl(AActor* Operator);
 UFUNCTION(BlueprintCallable, Category="Remote") void EndRemoteControl(AActor* Operator);
 UFUNCTION(BlueprintCallable, Category="Remote") void SetRemoteInput(float Forward,float Steering,float Lift);
 UFUNCTION(BlueprintPure, Category="Remote") bool IsRemoteControlled() const { return IsValid(RemoteOperator); }
 UFUNCTION(BlueprintPure, Category="Remote") AActor* GetRemoteOperator() const { return RemoteOperator; }
 UFUNCTION(BlueprintPure, Category="Remote") float GetForkHeightCm() const { return LiftOffset+9.5f; }
 UFUNCTION(BlueprintPure, Category="Layout") AWarehousePallet* GetSupportedPallet() const { return bSupportingPallet ? TargetPallet.Get() : nullptr; }
 UFUNCTION(BlueprintCallable, Category="Editor") static void RebuildEditedMesh(UStaticMesh* Mesh);
 UFUNCTION(BlueprintCallable, Category="Editor") static void ConfigureForkCollision(UStaticMesh* Mesh);
 UFUNCTION(BlueprintCallable, Category="Editor") static void ConfigureWheelCollision(UStaticMesh* Mesh);
 UFUNCTION(BlueprintPure, Category="Physics") float GetPhysicalMassKg() const;
 UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Elevator") TObjectPtr<AWarehouseElevator> Elevator;
 UFUNCTION(BlueprintPure, Category="Elevator") float GetTransferMassKg() const { return VehicleMassKg+(bSupportingPallet ? GetLoadMassKg() : 0.f); }
 UFUNCTION(BlueprintPure, Category="Elevator") bool IsLiftAtTravelHeight() const { return LiftOffset<=20.f; }
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autonomy") bool bAutonomousMode = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autonomy") TArray<FWarehouseWorkOrder> PendingJobs;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Autonomy") TArray<FTransform> NavigationAnchors;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Check") bool bSafetyScannerHealthy = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Check") bool bEStopReleased = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Check") bool bBrakeHealthy = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Check") bool bSteeringHealthy = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Check") bool bForkSensorHealthy = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Check") bool bLocalizationHealthy = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Check") bool bBatteryHealthy = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Check") bool bPalletSensorHealthy = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Self Check") bool bLoadSensorHealthy = true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perception") FVector PalletPositionBiasCm = FVector::ZeroVector;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perception") float PalletYawBiasDegrees = 0;
 // Staged sensor rollout: observation never changes the existing controller.
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perception") EWarehouseSensorMode SensorMode=EWarehouseSensorMode::Autonomous;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perception", meta=(ClampMin="100",ClampMax="1000")) float PalletDetectionRangeCm = 400;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Perception") int32 ObstacleCheckCount = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Perception") double LastObstacleCheckSeconds = -1;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Recovery") int32 RecoveryCount = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Recovery") int32 ForkInsertionRetries = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Recovery") int32 DetourCount = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Recovery") bool bEmergencyBlocked = false;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Recovery") FString EmergencyReason;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Autonomy") EWarehouseAIState AIState = EWarehouseAIState::Off;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Autonomy") FWarehouseWorkOrder ActiveJob;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Autonomy") TArray<FWarehouseRoutePoint> PlannedRoute;
 // AGV LiDAR localization selected from feature/forklift-navigation (main integration 2026-10-31).
 // Enabled for autonomous route travel; remote input never uses this estimate.
 // Route travel and the planning start use the estimate; precise alignment, docking and the last 80cm keep the exact pose.
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV Localization") bool bUseAgvLocalization = true;
 /** Pose for route travel decisions: the LiDAR estimate when bUseAgvLocalization, else the actual pose. */
 UFUNCTION(BlueprintPure, Category="AGV Localization") FTransform NavigationTransform() const;
 /** True when bUseAgvLocalization and the estimate is lost or not yet available (OutReason says which). */
 bool AgvLocalizationBlocked(FString& OutReason) const;
 /** True when route travel actually runs on the estimate (switch on and a map to localize against). */
 bool AgvEstimateInUse() const;
 // Read-only presentation of the remaining plan; does not plan, sense or command motion.
 UFUNCTION(BlueprintPure, Category="Warehouse|Display") TArray<FVector> GetDisplayRoute() const;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Autonomy") TArray<FWarehouseJobReport> JobReports;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Autonomy") TArray<FString> CompletedJobIds;
 UPROPERTY(BlueprintAssignable, Category="Autonomy") FWarehouseReportEvent OnJobReport;
 UFUNCTION(BlueprintCallable, Category="Autonomy") bool SubmitJob(const FWarehouseWorkOrder& Job);
 // Cancel work rather than pausing it. A boarded elevator transfer finishes its safe exit first.
 UFUNCTION(BlueprintCallable, Category="Autonomy") bool StopCurrentWork();
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Autonomy") bool bWorkStopRequested=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Autonomy") bool bWorkStopNeedsRecovery=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Autonomy") bool bClearingForksForWork=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Autonomy") FString WorkStopStatus;
 UFUNCTION(BlueprintPure,Category="Autonomy") bool IsWorkDeferred(AWarehousePallet* Pallet) const;
 // Floor handoff uses a reserved 120cm square, not a centre-only tolerance.
 static constexpr float FloorSlotHalfExtentCm = 60.f;
 UFUNCTION(BlueprintPure, Category="Autonomy") bool IsFloorFootprintValid(AWarehousePallet* Pallet,const FTransform& Reference,bool Refresh=true) const;
 UFUNCTION(BlueprintPure, Category="Autonomy") bool IsFloorPlacementValid(AWarehousePallet* Pallet,const FTransform& Destination,bool bForksStillInserted=false,bool Refresh=true) const;
 UFUNCTION(BlueprintCallable, Category="Autonomy") bool PlanAutonomousRoute(const FTransform& Goal);
 UFUNCTION(BlueprintCallable, Category="Layout") void RefreshAfterLayout(bool PublishTaskLocation=true);
 // Administrative editing only: retain unrelated work; report cancellation for removed resources.
 void SuspendForBuildingEdit(const TArray<AActor*>& Removed);
 UFUNCTION(BlueprintPure, Category="Autonomy") FString CheckSystems(bool bIncludeAutonomySensors=true) const;
 UFUNCTION(BlueprintPure, Category="Perception") bool NavigationClearForDiagnostics(const FTransform& Pose) const;
 UFUNCTION(BlueprintPure, Category="Perception") bool IsSensorSamplingSuppressed() const;
 bool GetPerceivedActorBounds(const AActor* Actor,FBox& Bounds) const;

 UFUNCTION(BlueprintCallable, Category="Strength") void SetMechanicalFailure();
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Strength") bool bMechanicalFailure = false;
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UFUNCTION(BlueprintCallable, Category="Training") void TogglePower();
 UFUNCTION(BlueprintCallable, Category="Training") void AdvanceSimulation(float DeltaSeconds);
 UFUNCTION(BlueprintCallable, Category="Battery") void RequestCharging(bool ToFull = false);
 UFUNCTION(BlueprintPure, Category="Battery") float GetBatteryEnergyWh() const;
 UFUNCTION(BlueprintPure, Category="Training") float GetLoadMassKg() const;
 UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Battery") TObjectPtr<AWarehouseChargingStation> ChargingStation;
 UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Parking") TObjectPtr<AWarehouseWaitingBay> WaitingBay;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Parking") bool bIdleParkingEnabled=true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battery") bool bOpportunityChargingEnabled=true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battery",meta=(ClampMin="30",ClampMax="100")) float OpportunityChargeBelowPercent=80;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battery",meta=(ClampMin="0")) float OpportunityChargeIdleDelaySeconds=10;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Battery|Docking",meta=(ClampMin=".2",ClampMax="20")) float ChargingCaptureDistanceCm=20.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Battery|Docking",meta=(ClampMin=".1",ClampMax="10")) float ChargingCaptureHeadingDegrees=3.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Battery|Docking",meta=(ClampMin="5",ClampMax="30")) float ChargingDockLateralLimitCm=15;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Battery|Docking",meta=(ClampMin="10",ClampMax="150")) float ChargingLineLookaheadCm=50;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Errors") FName LastErrorCode;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Errors") FString LastErrorMessage;
 UFUNCTION(BlueprintPure,Category="Autonomy") FString ValidateJobResources(const FWarehouseWorkOrder& Job) const;
 void SetOperationError(FName Code,const FString& Message);
 bool TryAcquireCharger();
 bool IsIdleForLayout() const;
 UFUNCTION(BlueprintPure,Category="Autonomy") bool CanAcceptNewWork() const;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battery", meta=(ClampMin="0", ClampMax="100")) float BatteryPercent = 100;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battery", meta=(ClampMin="1")) float BatteryVoltage = 24;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battery", meta=(ClampMin="1")) float BatteryCapacityAh = 180;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battery", meta=(ClampMin="0", ClampMax="70")) float ChargeBelowPercent = 30;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battery", meta=(ClampMin="75", ClampMax="100")) float ResumeAtPercent = 80;
 // Training acceleration affects battery time only, never movement or collision steps.
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Battery", meta=(ClampMin="1", ClampMax="3600")) float BatteryTimeScale = 1;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Battery") bool bChargePending = false;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Battery") float EnergyConsumedWh = 0;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Specification", meta=(ClampMin="1", ClampMax="1400")) float RatedLoadKg = 1400;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Specification", meta=(ClampMin="1")) float VehicleMassKg = 1000;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Specification", meta=(ClampMin="10", ClampMax="160")) float MaxForkHeightCm = 160;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Training", meta=(ClampMin="12", ClampMax="160")) float TaskForkHeightCm = 20;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Specification", meta=(ClampMin="1", ClampMax="180")) float EmptyTravelSpeedCm = 180;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Specification", meta=(ClampMin="1", ClampMax="100")) float LoadedTravelSpeedCm = 100;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Specification", meta=(ClampMin="1", ClampMax="30")) float ForkLeadingSpeedCm = 30;
 // Finite training actuator limits; these are tuning assumptions, not manufacturer data.
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics|Actuators", meta=(ClampMin="1")) float DriveTorqueNm = 400;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics|Actuators", meta=(ClampMin="1")) float DrivePowerWatts = 3500;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics|Actuators", meta=(ClampMin="1")) float BrakeTorqueNm = 532;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics|Actuators", meta=(ClampMin="1")) float DriveAccelerationCm = 240;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics|Actuators", meta=(ClampMin="1")) float LiftForceNewtons = 20000;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Physics|Actuators", meta=(ClampMin="1")) float MastForceNewtons = 10000;
 // Minimum curvature radius used by the autonomous pose graph planner.
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Specification") float MinimumTurningRadiusCm = 117.3f;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Training") float CurrentSpeedCm = 0;
 UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Training") TObjectPtr<AWarehousePallet> TargetPallet;
 TArray<AActor*> GetPhysicalLoads() const;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Training") bool bPowered = false;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Training") EWarehouseCycle State = EWarehouseCycle::Idle;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Training") FString Status = TEXT("E : START");
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Traffic") bool bTrafficWaiting=false;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Traffic") bool bTrafficYielding=false;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Traffic") FString TrafficStatus;
private:
 friend class FWarehouseExperimentMetrics;
 // Observation only: set by the existing gate; never used to decide motion.
 bool bExperimentSensorWaiting=false;
 friend class AWarehouseFleetCoordinator;
 friend class AWarehouseLayoutEditor;
 UPROPERTY(Transient) TObjectPtr<UAgvShadowSensorsComponent> SensorKit;
 bool bSensorSuppressed=false;
 bool bRouteSensorForecast=false;
 float SensorPrecisionDistanceCm=MAX_flt;
 EWarehouseSensorMode LastSensorMode=EWarehouseSensorMode::Disabled;
 bool bObservedTargetValid=false;
 bool bHasRecognizedTargetPose=false;
 FTransform SensorObservedTarget;
 TMap<FString,FTransform> WorkOrderSourcePoses;
 void UpdateSensorSnapshot();
 void InvalidateSensorSnapshot(const FString& Reason);
 bool AutonomousSensorsReady() const;
 bool ConsumeObstacleSnapshot(int32& LastConsumedSnapshot);
 bool PerceivedBoxClear(const FTransform& Pose,const FBox& LocalBox,const TArray<AActor*>& Ignored,bool PeopleOnly=false) const;
 bool PerceivedPartClear(UStaticMeshComponent* Part,const FTransform& Pose,const TArray<AActor*>& Ignored,const UPrimitiveComponent* Bearing=nullptr) const;
 bool PerceivedMotionClear(UStaticMeshComponent* Part,FVector Delta,const TArray<AActor*>& Ignored) const;
 bool PerceivedFloorSupport(FVector Point,FHitResult* Surface=nullptr) const;
 FTransform TaskSourcePose() const;
 UPROPERTY(Transient) TObjectPtr<AWarehouseFleetCoordinator> FleetCoordinator;
 bool bPlanningRoute=false,bAvoidTrafficReservations=false,bTrafficEscape=false;
 // Valid only within one synchronous game-thread PlanAutonomousRoute call.
 TOptional<bool> PlanningSensorReadiness;
 bool bObservedPickupEscape=false;
 bool bEndpointDriving=false,bEndpointOrienting=false,bRouteTurning=false;
 int32 EndpointSegment=-1;
 FBox NavigationEnvelope() const;
 // Commands are actuator targets. Telemetry and fork pose always come from Chaos.
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ChassisBody;
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> SteeringBody;
 UPROPERTY(Transient) TObjectPtr<UPhysicsConstraintComponent> SteeringJoint;
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> CarriageBody;
 UPROPERTY(Transient) TObjectPtr<UPhysicsConstraintComponent> CarriageJoint;
 UPROPERTY(Transient) TObjectPtr<UPhysicsConstraintComponent> StageJoint;
 UPROPERTY(Transient) TArray<TObjectPtr<UPhysicsConstraintComponent>> WheelJoints;
 float DesiredDriveSpeed=0, DesiredCurvature=0, DriveSpeedTarget=0;
 float DesiredTurnRate=0;
 float DesiredLift=0, LiftMotorTarget=0;
 float SteeringAngle=0;
 bool bPhysicsReady=false;
 void InitializePhysicalRig();
 void UpdatePhysicalRig(float Dt);
 bool CommandDrive(float Speed,float Curvature,float Dt);
 bool CommandTurn(float YawRate,float Dt);
 void BrakeDrive();
 UPROPERTY(Transient) TObjectPtr<AActor> RemoteOperator;
 float RemoteForward=0,RemoteSteering=0,RemoteLift=0;
 double LastRemoteInputTime=0;
 bool bReplanAfterRemote=false;
 void AdvanceRemote(float Dt);
 bool RemotePoseClear(const FTransform& Pose);
 bool PhysicalPoseClear(const FTransform& Pose,bool CheckTranslation=false) const;
 bool WheelBearingOnFloor(UStaticMeshComponent* Wheel,UPrimitiveComponent* Surface,
  const FTransform& Candidate,const TArray<AActor*>& Ignored) const;
 bool PalletClaimedByOther(AWarehousePallet* Pallet) const;
 bool UpdateLoadSupport(float Dt);
 bool BeginFloorTransfer(float TargetZ, EWarehouseAIState Resume);
 float FloorBase(float WorldZ) const;
 EWarehouseAIState AfterElevator = EWarehouseAIState::Ready;
 int32 TransferFromFloor=0;
 int32 TransferToFloor=0;
 bool bFloorTransfer=false;
 void AdvanceAutonomy(float Dt);
 void ToggleAutonomy();
 void TransitionAI(EWarehouseAIState Next,const FString& Message);
 void FaultAI(const FString& Reason);
 void ReportJob(const FString& Result,const FString& Detail);
 bool TryDeferCurrentWork(const FString& Reason,const FString& PlayerMessage=TEXT(""));
 bool WorkStopCanResume();
 bool HasRetainedForkContact() const;
 bool PrepareForksForNextWork();
 bool FindRetainedLoadSupport(FTransform& Destination) const;
 double NextForkClearanceCheck=-1;
 TMap<TWeakObjectPtr<AWarehousePallet>,double> DeferredWorkUntil;
 bool RackLoadAligned() const;
 FWarehouseNavigationQuery BuildNavigationQuery() const;
 bool bUseCurrentRackWaypoints=false;
 bool NavigationClear(const FTransform& Pose,const FWarehouseNavigationQuery* Query=nullptr) const;
 bool ConnectPoses(const FTransform& From,const FTransform& To,TArray<FWarehouseRoutePoint>& Route,float& Cost,const FWarehouseNavigationQuery* Query=nullptr) const;
 bool ConnectLanePoses(const FTransform& From,const FTransform& To,TArray<FWarehouseRoutePoint>& Route,float& Cost,const FWarehouseNavigationQuery* Query=nullptr) const;
 bool FollowRoute(float Speed,float Dt);
 bool MovePrecise(FVector Destination,float Speed,float Dt);
 bool SeePallet(FTransform& Pose) const;
 bool DestinationClear(const TArray<AActor*>* Traffic=nullptr) const;
 bool WaitForDestinationClear(float Dt);
 float DestinationTrafficSeconds=0;
 void TrackCargo();
 bool PalletOnForks() const;
 float LostSupportSeconds = 0;
 float LiftInitialHeight=0,LiftProgressHeight=0,LiftProgressAt=0;
 FTransform PalletApproach(const FTransform& PalletPose,float Distance) const;
 EWarehouseAIState ResumeAI = EWarehouseAIState::Ready;
 EWarehouseAIState WaitResumeAI = EWarehouseAIState::Ready;
 UPROPERTY(Transient) TArray<TObjectPtr<AActor>> CarriedCargo;
 FTransform ObservedPallet;
 FVector RetractLocation = FVector::ZeroVector;
 FTransform ChargeExitPose;
 FTransform IdlePose;
 bool bOpportunityCharge=false;
 bool bIdleChargeParking=false;
 double NextResourceCheck=-1;
 bool AdvanceIdleParking(float Dt);
 bool PlanIdleParking(bool WaitingForCharge);
 FTransform DockStablePose;
 float DockStableSeconds = -1; // Negative until the brake-only settling window starts.
 bool bDockPullout=false;
 float AIElapsed = 0;
 float AITotalSeconds = 0;
 float WaitSeconds = 0;
 bool bRouteStarted = false;
 bool bResumeAfterCheck = false;
 bool bPalletReleased = false;
 bool IsRecovering() const;
 void RecoveryFault(const TCHAR* Reason,const TCHAR* PlayerMessage);
 bool BeginRecovery(EWarehouseAIState From,const FString& Reason);
 void AdvanceRecovery(float Dt);
 void CheckDriveProgress(float Dt);
 bool PlanRecoveryDetour(const FTransform& Goal);
 EWarehouseAIState RecoveryResume = EWarehouseAIState::Ready;
 FTransform RecoveryGoal;
 FVector RecoveryStart = FVector::ZeroVector;
 FVector EmergencyLocation = FVector::ZeroVector;
 FVector ProgressLocation = FVector::ZeroVector;
 float LastDriveDirection = 1, RecoveryDirection = -1, RecoveryDistance = 80;
 float StalledSeconds = 0;
 int32 RecoveryAttempts = 0;
 int32 ObservedRouteReplans = 0;
 FVector ObservedRouteProgress = FVector::ZeroVector;
 bool bRetryInsertion = false, bRecoveryNeedsRoute = false;
 FString WaitObstacleReason;
 int32 RouteIndex = 0;
 UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Carriage;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> LiftStage;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> LiftChains;
 UPROPERTY(VisibleAnywhere, Transient) TArray<TObjectPtr<UStaticMeshComponent>> Wheels;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Beacon;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Display;
 FVector StartLocation = FVector::ZeroVector;
 FVector WithdrawStart = FVector::ZeroVector;
 FVector ReturnLocation = FVector::ZeroVector;
 EWarehouseCycle ResumeState = EWarehouseCycle::Idle;
 bool bChargeToFull = false;
 float LastDisplayedBattery = -1;
 float LiftOffset = 0;
 float PalletContactLiftCm = 0;
 bool bSupportingPallet = false;
 bool ClearToMove(FVector Delta, bool LiftOnly);
 // One sampling frame each game second; movement/route/turn/lift share that frame.
 bool RefreshObstacleChecks();
 uint64 ObstacleCheckFrame=MAX_uint64;
 bool bMovementObstacleClear=true, bLiftObstacleClear=true;
 bool bRouteObstacleClear=true, bTurnObstacleClear=true;
 FString MovementObstacleReason, LiftObstacleReason;
 int32 MovementObstacleSnapshot=INDEX_NONE,LiftObstacleSnapshot=INDEX_NONE;
 int32 RouteObstacleSnapshot=INDEX_NONE,TurnObstacleSnapshot=INDEX_NONE;
 FVector LastMovementScanDirection=FVector::ZeroVector,LastLiftScanDirection=FVector::ZeroVector;
 float LastTurnScanDirection=0;
 bool MoveVehicle(FVector Delta);
 bool MoveToLine(FVector Destination, float Speed, float Dt, float ArrivalTolerance=1.f);
 bool BeginChargeTrip();
 void ConsumeEnergy(float Wh);
 void RefreshDisplay();
 bool MoveLift(float Height);
 void StopFor(const FString& Reason);
 void SetStatus(const FString& Message);
};
