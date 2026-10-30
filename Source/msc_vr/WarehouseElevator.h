#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseElevator.generated.h"

class AWarehouseForklift;
class ACharacter;
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;
class UMaterialInterface;
class UTextRenderComponent;
class UPhysicsConstraintComponent;

UENUM(BlueprintType)
enum class EWarehouseElevatorState : uint8
{
 Idle, ClosingForCall, Calling, OpeningEntry, AwaitBoarding,
 ClosingLoaded, Travelling, OpeningExit, AwaitExit, ClosingEmpty
};

/** Educational FMS/PLC handshake, custom 350 x 240 cm platform; not a certified PLC. */
UCLASS()
class MSC_VR_API AWarehouseElevator : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseElevator();
 virtual void OnConstruction(const FTransform& Transform) override;
 // Model replacement is an editor operation; live guides keep their native bodies.
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator|Model") bool bUseCargoModel=false;
 UFUNCTION(BlueprintCallable, CallInEditor, Category="Elevator|Model") void ApplyCargoModel();
 UFUNCTION(BlueprintCallable, Category="Elevator|Model") static void ConfigureModelCollision(UStaticMesh* Mesh,FName Part);
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elevator") bool bPowerAvailable=true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elevator") bool bEStopReleased=true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elevator") bool bDoorSensorsHealthy=true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elevator") bool bLevelSensorHealthy=true;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Elevator", meta=(ClampMin="1",ClampMax="4")) int32 FloorCount=3;
 UFUNCTION(BlueprintCallable, Category="Elevator") bool SetFloorCount(int32 Count);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elevator", meta=(ClampMin="1",ClampMax="2000")) float RatedMassKg=2000;
 // Project assumption; actual manufacturer speed depends on the supplied configuration.
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elevator", meta=(ClampMin="1",ClampMax="50")) float TravelSpeedCm=35;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") FVector2D GroundOpeningSizeCm=FVector2D(282,352);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") FVector2D UpperOpeningSizeCm=FVector2D(282,376);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") FVector2D UpperOpeningOffsetCm=FVector2D(0,12);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") FVector InitialShaftPosition;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") EWarehouseElevatorState State=EWarehouseElevatorState::Idle;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") TObjectPtr<AWarehouseForklift> ReservedVehicle;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") int32 CurrentFloor=0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") int32 DestinationFloor=0;
 // The next platform stop is independent of the reserved vehicle's exit floor.
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") int32 VehicleDestinationFloor=INDEX_NONE;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") TArray<int32> PendingStops;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") int32 CompletedTransfers=0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") FString Status=TEXT("READY / 2000 KG MAX");
 UFUNCTION(BlueprintPure, Category="Elevator") int32 FloorAtHeight(float WorldZ) const;
 UFUNCTION(BlueprintPure, Category="Elevator") float FloorHeight(int32 Floor) const;
 UFUNCTION(BlueprintPure, Category="Elevator") FTransform WaitingPose(int32 Floor) const;
 UFUNCTION(BlueprintPure, Category="Elevator") FTransform BoardingPose(int32 Floor) const;
 UFUNCTION(BlueprintPure, Category="Elevator") FString CheckInterlocks() const;
 UFUNCTION(BlueprintPure, Category="Elevator") bool CanEnter(AWarehouseForklift* Vehicle) const;
 UFUNCTION(BlueprintPure, Category="Elevator") bool CanExit(AWarehouseForklift* Vehicle) const;
 UFUNCTION(BlueprintPure, Category="Elevator") bool AreLandingDoorsClosed() const;
 // PLC-authorized deck support only; never a world/unknown-ground query.
 bool TraceCertifiedPlatformSupport(AWarehouseForklift* Vehicle,FVector Point,FHitResult& Support) const;
 bool IsCertifiedLandingSeam(AWarehouseForklift* Vehicle,const UStaticMeshComponent* Wheel,const FHitResult& Contact) const;
 UFUNCTION(BlueprintCallable, Category="Elevator") bool RequestTransfer(AWarehouseForklift* Vehicle,int32 FromFloor,int32 ToFloor);
 UFUNCTION(BlueprintCallable, Category="Elevator") bool ConfirmBoarded(AWarehouseForklift* Vehicle);
 UFUNCTION(BlueprintCallable, Category="Elevator") bool ConfirmExited(AWarehouseForklift* Vehicle);
 UFUNCTION(BlueprintCallable, Category="Elevator") bool CancelTransfer(AWarehouseForklift* Vehicle);
 UFUNCTION(BlueprintCallable, Category="Elevator") void AdvanceElevator(float Seconds);
 UFUNCTION(BlueprintCallable, Category="Elevator|Human") bool RequestHumanCall(ACharacter* Person,int32 Floor);
 UFUNCTION(BlueprintCallable, Category="Elevator|Human") bool RequestHumanDestination(ACharacter* Person,int32 Floor);
 UFUNCTION(BlueprintPure, Category="Elevator|Human") int32 HumanLandingFloor(const ACharacter* Person) const;
 UFUNCTION(BlueprintPure, Category="Elevator|Human") bool IsHumanInside(const ACharacter* Person) const;
 UFUNCTION(BlueprintPure, Category="Elevator|Human") bool CanUseHumanPanel(const ACharacter* Person) const;
 UFUNCTION(BlueprintPure, Category="Elevator|Human") int32 HumanButtonAtSight(const ACharacter* Person) const;
 UFUNCTION(BlueprintPure, Category="Elevator|Human") bool IsDestinationButtonLit(int32 Floor) const;
 UFUNCTION(BlueprintPure, Category="Elevator|Human") bool IsCallButtonLit(int32 Floor) const;
 UFUNCTION(BlueprintPure, Category="Elevator|Human") FString GetPositionIndicatorText() const;
 UFUNCTION(BlueprintPure, Category="Elevator|Human") FString GetHumanInteractionText(const ACharacter* Person) const;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Elevator|Human") bool bHumanService=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Elevator|Human") FString HumanStatus;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Elevator|Human") float HumanPayloadMassKg=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Elevator|Human") int32 CompletedHumanTransfers=0;
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Platform;
 UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Gates;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Display;
 UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> CargoParts;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> CargoCabin;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> CargoDeckVisual;
 UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> CargoDeckSupports;
 UPROPERTY(Transient) TObjectPtr<UPhysicsConstraintComponent> PlatformJoint;
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> GuideAnchor;
 UPROPERTY(Transient) TArray<TObjectPtr<UPhysicsConstraintComponent>> DoorJoints;
 UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> HumanCallPanels;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> HumanCabinPanel;
 UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> HumanPanelAnchor;
 UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> HumanFloorButtons;
 UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UTextRenderComponent>> HumanFloorLabels;
 UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> HumanCallLamps;
 UPROPERTY() TObjectPtr<UMaterialInterface> IndicatorOnMaterial;
 UPROPERTY() TObjectPtr<UMaterialInterface> IndicatorOffMaterial;
 UPROPERTY(Transient) TArray<int32> HumanDestinationRequests;
 UPROPERTY(Transient) TArray<int32> HumanCallRequests;
 UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UTextRenderComponent>> HumanPanelLabels;
 TArray<TWeakObjectPtr<AActor>> HumanPayload;
 float HumanDoorIdleSeconds=0;
 int32 TravelDirection=0;
 float PlatformTarget=0;
 float PlatformHeight=0;
 float DoorOpening=0;
 int32 EntryFloor=0;
 bool bBoarded=false;
 bool bCargoModelApplied=false;
 void UpdateCargoModelFloors();
 void UpdateHumanIndicators();
 void QueueStop(int32 Floor);
 void StartNextStop();
 void ServeCurrentFloor();
 void ReleaseVehicleReservation();
 int32 NextStop() const;
 float CabinPayloadMassKg() const;
 void ConfigureFloors();
 float StrokeHalfHeight() const { return (FloorCount-1)*200.f; }
 bool PortalClear() const;
 bool CabinClear(bool IgnoreHumanPayload=true) const;
 bool CaptureHumanPayload();
 bool HumanPayloadFits(AActor* Actor) const;
 FVector HumanCabinPanelPosition() const;
 bool FitsInside(AWarehouseForklift* Vehicle) const;
 bool SetDoors(bool Open,float Dt);
 float ActualDoorOpening() const;
 void SetDeckLock(bool Locked);
 bool MovePlatform(int32 Floor,float Dt);
 void SetState(EWarehouseElevatorState Next,const TCHAR* Message);
};
