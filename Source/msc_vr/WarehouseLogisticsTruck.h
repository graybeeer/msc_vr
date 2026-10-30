#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseLogisticsTruck.generated.h"

class AWarehousePallet;
class UPrimitiveComponent;
class UPhysicsConstraintComponent;
class ACharacter;
class UPoseableMeshComponent;

UENUM(BlueprintType)
enum class EWarehouseTruckState : uint8 { AtDock, Loading, Departing, Departed, Unavailable, Returning };

UENUM(BlueprintType)
enum class EWarehouseDockServiceState : uint8 { Awaiting, Securing, Ready, Clearing, Released };

/** Deliberately cinematic dock handling. Warehouse units retain their actors, recipes and damage. */
UCLASS()
class MSC_VR_API AWarehouseLogisticsTruck : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseLogisticsTruck();
 virtual void OnConstruction(const FTransform& Transform) override;
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics") FString TruckId;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics") FString DockId;
 // Presentation only: seat origin is the driver's pelvis, +X faces forward.
 // Empty class uses this world's player character, with the project BP fallback.
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics|Driver") bool bShowDriver=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics|Driver") TSubclassOf<ACharacter> DriverCharacterClass;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics|Driver") FTransform DriverSeatPose=FTransform(FVector(235,-55,140));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics|Driver") FVector DriverSteeringWheelCentreCm=FVector(42,0,35);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Logistics|Driver") bool bDriverPoseReady=false;
 UFUNCTION(BlueprintCallable,Category="Logistics|Driver") void UpdateDriverPresentation();
 UFUNCTION(BlueprintPure,Category="Logistics|Driver") UPoseableMeshComponent* GetDriverMesh() const { return DriverMesh; }
 // Chassis, panels, open doors and work lights; never include the fixed dock leveller.
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Logistics") TArray<TObjectPtr<AActor>> TruckParts;
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Logistics") TObjectPtr<AActor> DockAnchor;
 // Pallet bottom-centre poses relative to this actor. Size is in each slot's local axes.
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics") TArray<FTransform> LoadSlots;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics") FVector LoadSlotSizeCm=FVector(160,114,210);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics") TArray<FVector> DepartureRoute;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics",meta=(ClampMin="1")) float RatedPayloadKg=6000;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics",meta=(ClampMin="0.1")) float TransferIntervalSeconds=3;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics",meta=(ClampMin="1")) float DepartureSpeedCm=300;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Logistics") EWarehouseTruckState State=EWarehouseTruckState::AtDock;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Logistics") FString Status;
 // Educational service-time assumptions, not certified dock safety control.
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics|Dock",meta=(ClampMin="0")) float DockPreparationSeconds=3;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Logistics|Dock",meta=(ClampMin="0")) float DockClearanceSeconds=2;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Logistics|Dock") EWarehouseDockServiceState DockServiceState=EWarehouseDockServiceState::Awaiting;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Logistics|Dock") FString DockServiceStatus;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Logistics|Dock") float DockServiceRemainingSeconds=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Logistics|Dock") bool bVehicleRestrained=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Logistics|Dock") bool bDockDoorOpen=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Logistics|Dock") bool bLevellerReady=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Logistics|Visit") int32 VisitNumber=1;
 UFUNCTION(BlueprintPure,Category="Logistics|Visit") FString GetVisitId() const;
 UFUNCTION(BlueprintCallable,Category="Logistics|Dock") bool PrepareDock();
 UFUNCTION(BlueprintPure,Category="Logistics|Dock") bool IsDockPrepared() const;
 UFUNCTION(BlueprintCallable,Category="Logistics|Dock") bool PrepareDeparture();
 UFUNCTION(BlueprintPure,Category="Logistics|Dock") bool IsDeparturePrepared() const;
 // Reuse only the departed vehicle presentation. Previous cargo is archived,
 // never returned to warehouse stock or used to seed the next visit.
 UFUNCTION(BlueprintCallable,Category="Logistics|Visit") bool ReturnForNextVisit();
 UFUNCTION(BlueprintCallable,Category="Logistics|Visit") bool QueueArrivalManifest(const TArray<AWarehousePallet*>& Pallets);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Logistics") TArray<TObjectPtr<AWarehousePallet>> ManifestPallets;
 // Authored stock for a new visit; live manifest remains read-only and verified.
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Logistics") TArray<TObjectPtr<AWarehousePallet>> InitialPallets;
 UFUNCTION(BlueprintPure,Category="Logistics") bool IsReady() const;
 UFUNCTION(BlueprintCallable,Category="Logistics") bool TransferUnit(AWarehousePallet* Pallet,int32 SlotIndex,bool IntoTruck,FTransform WorldDestination);
 UFUNCTION(BlueprintCallable,Category="Logistics") bool Depart();
 UFUNCTION(BlueprintPure,Category="Logistics") bool IsUnitOnTruck(AWarehousePallet* Pallet) const;
 UFUNCTION(BlueprintPure,Category="Logistics") float PayloadMassKg() const;
 UFUNCTION(BlueprintPure,Category="Logistics") TArray<AActor*> GetUnitCargo(AWarehousePallet* Pallet) const;
 // IsReady is also used by the internal startup importer; consumers must check both.
 UFUNCTION(BlueprintPure,Category="Logistics") bool IsManifestReady() const { return InitialManifest.IsEmpty() && !bInitialManifestError; }
 UFUNCTION(BlueprintCallable,Category="Logistics") bool RetryInitialManifest();
private:
 UPROPERTY(VisibleAnywhere,Category="Logistics|Driver") TObjectPtr<UPoseableMeshComponent> DriverMesh;
 struct FBodyState
 {
  TWeakObjectPtr<UPrimitiveComponent> Body;
  bool Simulated=false,Gravity=true;
  ECollisionEnabled::Type Collision=ECollisionEnabled::NoCollision;
 };
 struct FUnitMember
 {
  TWeakObjectPtr<AActor> Actor;
  FTransform RelativePose;
  bool bHidden=false,bCollisionEnabled=true;
 };
 struct FUnit
 {
  TWeakObjectPtr<AWarehousePallet> Pallet;
  int32 Slot=INDEX_NONE;
  float MassKg=0;
  TArray<FUnitMember> Members;
  TArray<FBodyState> Bodies;
  struct FJointState
  {
   TWeakObjectPtr<UPhysicsConstraintComponent> Joint;
   FTransform Frame1,Frame2;
  };
  TArray<FJointState> Joints;
  FTransform LocalPalletPose;
 };
 TArray<FUnit> Units;
 TArray<FUnit> ArchivedUnits;
 TArray<TWeakObjectPtr<AWarehousePallet>> InitialManifest;
 TArray<FUnitMember> DockParts;
 TArray<FUnitMember> DepartureParts;
 FTransform LastPose;
 float MeasuredSpeedCm=0,TransferRemaining=0;
 int32 RouteIndex=0;
 TArray<FVector> ReturnRoute;
 FTransform ReturnDockPose;
 float DockServiceElapsed=0,DockServiceDuration=0;
 bool bConfigurationUnavailable=false;
 EWarehouseTruckState StateBeforeConfigurationFailure=EWarehouseTruckState::AtDock;
 bool bInitialManifestError=false;
 bool PartsValid() const;
 bool Fail(const FString& Message);
 bool Transfer(AWarehousePallet* Pallet,int32 SlotIndex,bool IntoTruck,FTransform WorldDestination,const TArray<AActor*>* InitialCargo=nullptr);
 static void MoveUnit(FUnit& Unit,const FTransform& PalletPose);
 static void RestoreUnit(FUnit& Unit);
 static FBox UnitBounds(const FUnit& Unit,const FTransform& PalletPose,const FTransform& Reference);
 bool FitsSlot(const FUnit& Unit,const FTransform& PalletPose,int32 Slot) const;
 void ResetDockService();
 void AdvanceDockService(float DeltaSeconds);
};
