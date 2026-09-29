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

UENUM(BlueprintType)
enum class EWarehouseCycle : uint8 { Idle, Approach, Lift, TravelLower, Reverse, Lower, Withdraw, Complete, ToCharger, Docking, Charging, Returning };

UCLASS()
class MSC_VR_API AWarehouseForklift : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseForklift();
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
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perception", meta=(ClampMin="100",ClampMax="1000")) float PalletDetectionRangeCm = 400;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Autonomy") EWarehouseAIState AIState = EWarehouseAIState::Off;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Autonomy") FWarehouseWorkOrder ActiveJob;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Autonomy") TArray<FWarehouseRoutePoint> PlannedRoute;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Autonomy") TArray<FWarehouseJobReport> JobReports;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Autonomy") TArray<FString> CompletedJobIds;
 UPROPERTY(BlueprintAssignable, Category="Autonomy") FWarehouseReportEvent OnJobReport;
 UFUNCTION(BlueprintCallable, Category="Autonomy") bool SubmitJob(const FWarehouseWorkOrder& Job);
 UFUNCTION(BlueprintCallable, Category="Autonomy") bool PlanAutonomousRoute(const FTransform& Goal);
 UFUNCTION(BlueprintPure, Category="Autonomy") FString CheckSystems() const;

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
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Specification", meta=(ClampMin="1", ClampMax="130")) float EmptyTravelSpeedCm = 130;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Specification", meta=(ClampMin="1", ClampMax="100")) float LoadedTravelSpeedCm = 100;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Specification", meta=(ClampMin="1", ClampMax="30")) float ForkLeadingSpeedCm = 30;
 // Minimum curvature radius used by the autonomous pose graph planner.
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Specification") float MinimumTurningRadiusCm = 117.3f;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Training") float CurrentSpeedCm = 0;
 UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Training") TObjectPtr<AWarehousePallet> TargetPallet;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Training") bool bPowered = false;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Training") EWarehouseCycle State = EWarehouseCycle::Idle;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Training") FString Status = TEXT("E : START");
private:
 void AdvanceAutonomy(float Dt);
 void ToggleAutonomy();
 void TransitionAI(EWarehouseAIState Next,const FString& Message);
 void FaultAI(const FString& Reason);
 void ReportJob(const FString& Result,const FString& Detail);
 bool NavigationClear(const FTransform& Pose) const;
 bool ConnectPoses(const FTransform& From,const FTransform& To,TArray<FWarehouseRoutePoint>& Route,float& Cost) const;
 bool FollowRoute(float Speed,float Dt);
 bool MovePrecise(FVector Destination,float Speed,float Dt);
 bool SeePallet(FTransform& Pose) const;
 bool DestinationClear() const;
 void HoldCargo(bool Attach);
 FTransform PalletApproach(const FTransform& PalletPose,float Distance) const;
 EWarehouseAIState ResumeAI = EWarehouseAIState::Ready;
 EWarehouseAIState WaitResumeAI = EWarehouseAIState::Ready;
 UPROPERTY(Transient) TArray<TObjectPtr<AActor>> CarriedCargo;
 FTransform ObservedPallet;
 FVector RetractLocation = FVector::ZeroVector;
 FTransform ChargeExitPose;
 float AIElapsed = 0;
 float AITotalSeconds = 0;
 float WaitSeconds = 0;
 bool bRouteStarted = false;
 bool bResumeAfterCheck = false;
 bool bPalletReleased = false;
 int32 RouteIndex = 0;
 UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Carriage;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> LiftStage;
 UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Wheels;
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
 bool MoveVehicle(FVector Delta);
 bool MoveToLine(FVector Destination, float Speed, float Dt);
 bool BeginChargeTrip();
 void ConsumeEnergy(float Wh);
 void RefreshDisplay();
 bool MoveLift(float Height);
 void StopFor(const FString& Reason);
 void SetStatus(const FString& Message);
};
