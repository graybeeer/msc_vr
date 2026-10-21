#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseConveyor.generated.h"

class AWarehousePallet;
class AWarehousePalletLift;
class UPhysicsConstraintComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EWarehouseConveyorKind : uint8 { PalletRoller, CartonBelt, RotaryTransfer };

UENUM(BlueprintType)
enum class EWarehouseRotaryPhase : uint8 { Empty, Centering, Rotating, Discharging, Resetting };

UENUM(BlueprintType)
enum class EWarehouseConveyorState : uint8
{
 Stopped, Running, DownstreamBlocked, PickupReady, Overloaded, LoadingLift, LiftTravelling, ReceivingLift, Fault
};

/** A one-load accumulation zone. Real motor-driven rollers carry independent Chaos loads. */
UCLASS(BlueprintType)
class MSC_VR_API AWarehouseConveyor : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseConveyor();
 virtual void OnConstruction(const FTransform& Transform) override;
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor") EWarehouseConveyorKind Kind=EWarehouseConveyorKind::PalletRoller;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor",meta=(ClampMin="125",ClampMax="1200")) float LengthCm=300.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor",meta=(ClampMin="50",ClampMax="200")) float WidthCm=125.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor",meta=(ClampMin="20",ClampMax="150")) float SurfaceHeightCm=40.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor",meta=(ClampMin="1",ClampMax="50")) float SpeedCm=20.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor",meta=(ClampMin="1",ClampMax="2000")) float RatedMassKg=2000.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor",meta=(ClampMin="0.1",ClampMax="100")) float RollerTorqueNm=15.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor") bool bEnabled=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor") bool bEStopReleased=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor") bool bPickupEnd=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor|Rotary") bool bTurnRight=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Conveyor|Rotary") EWarehouseRotaryPhase RotaryPhase=EWarehouseRotaryPhase::Empty;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Conveyor|Rotary") float CarrierYawDegrees=0.f;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Conveyor|Rotary") int32 CompletedRotaryTransfers=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Conveyor|Rotary") TObjectPtr<AActor> RotaryLoad;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Conveyor|Rotary") TObjectPtr<UStaticMeshComponent> RotaryCarrier;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor") TObjectPtr<AWarehouseConveyor> NextConveyor;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor|Lift") TObjectPtr<AWarehousePalletLift> Lift;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor|Lift") int32 LiftFloor=0;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor|Lift") int32 DestinationFloor=1;
 // The same physical port runs backwards when receiving from the lift.
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Conveyor|Lift") bool bLiftInfeed=true;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Conveyor") EWarehouseConveyorState State=EWarehouseConveyorState::Stopped;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Conveyor") FString Status=TEXT("READY / ONE LOAD PER ZONE");
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Conveyor") TObjectPtr<AWarehousePallet> EndPallet;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Conveyor") float MeasuredMassKg=0.f;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Conveyor") float ActualSurfaceSpeedCm=0.f;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Conveyor") int32 CompletedLiftTransfers=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Conveyor") TObjectPtr<UStaticMeshComponent> Frame;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Conveyor") TArray<TObjectPtr<UStaticMeshComponent>> Rollers;
 UFUNCTION(BlueprintCallable,Category="Conveyor") bool RebuildConveyor();
 UFUNCTION(BlueprintCallable,Category="Conveyor") bool ConfigureGeometry(float NewLengthCm,float NewWidthCm,float NewSurfaceHeightCm,float NewSpeedCm);
 UFUNCTION(BlueprintCallable,Category="Conveyor") bool SetConveyorPose(float YawDegrees,float InclineDegrees,float BaseHeightCm);
 UFUNCTION(BlueprintCallable,Category="Conveyor") bool SnapEntranceTo(AWarehouseConveyor* Previous);
 UFUNCTION(BlueprintCallable,Category="Conveyor|Rotary") bool SetTurnDirection(bool TurnRight);
 UFUNCTION(BlueprintPure,Category="Conveyor") bool IsIdleForLayout() const;
 UFUNCTION(BlueprintPure,Category="Conveyor") TArray<AActor*> GetPhysicalLoads() const;
 UFUNCTION(BlueprintPure,Category="Conveyor") bool IsHandlingCargo(AActor* Cargo) const;
 UFUNCTION(BlueprintPure,Category="Conveyor") FVector GetExitPosition() const;
 UFUNCTION(BlueprintPure,Category="Conveyor") FVector GetEntrancePosition() const;
 UFUNCTION(BlueprintPure,Category="Conveyor") FVector GetExitDirection() const;
 UFUNCTION(BlueprintPure,Category="Conveyor") bool CanReceiveLoad(AActor* Cargo) const;
private:
 UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> GeneratedParts;
 UPROPERTY(Transient) TArray<TObjectPtr<UPhysicsConstraintComponent>> RollerJoints;
 UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> Display;
 UPROPERTY(Transient) TObjectPtr<AWarehousePallet> TransferPallet;
 UPROPERTY(Transient) TObjectPtr<AWarehouseConveyor> ReceivingPort;
 UPROPERTY(Transient) TObjectPtr<AActor> ZoneLoad;
 UPROPERTY(Transient) TObjectPtr<UPhysicsConstraintComponent> RotaryJoint;
 UPROPERTY(Transient) TArray<TObjectPtr<AActor>> RotaryPayload;
 float RotaryTargetYaw=0.f;
 float RotaryRateDegrees=0.f;
 float RotaryStableSeconds=0.f;
 float RotaryNoProgressSeconds=0.f;
 float RotaryLastProgressYaw=0.f;
 float SinceSensors=.1f;
 float CommandSpeedCm=0.f;
 float RampedSpeedCm=0.f;
 float StableLoadSeconds=0.f;
 float MissingInfeedSeconds=0.f;
 int32 TransferStartCount=0;
 bool bReceivingFromLift=false;
 bool bTransferRequested=false;
 TArray<AActor*> ReadZone(bool& ForeignObject,int32& BaseCount,float& MassKg) const;
 bool HasBearing(UStaticMeshComponent* Body) const;
 bool PortAligned(const AWarehousePalletLift* CargoLift) const;
 AWarehouseConveyor* FindNext() const;
 AWarehouseConveyor* FindLiftReceiver() const;
 void UpdateSensors(float Seconds);
 void UpdateLift(float Seconds);
 bool AcknowledgeCompletedLift();
 bool CancelMissingInfeed(float Seconds);
 void SetStatus(EWarehouseConveyorState NewState,const FString& Message);
 void MakeRollerJoints();
 bool RebuildAtPose(const FTransform& Proposed);
 bool BuildRotaryConveyor();
 void MakeRotaryJoint();
 void UpdateRotary(float Seconds,const TArray<AActor*>& Loads,bool Foreign,int32 Count);
 void DriveRotary(float Dt);
 FTransform SurfaceFrame() const;
 float RollerRadius() const;
};
