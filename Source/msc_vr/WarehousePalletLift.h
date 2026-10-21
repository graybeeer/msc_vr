#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehousePalletLift.generated.h"

class AWarehousePallet;
class UPhysicsConstraintComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EWarehousePalletLiftState : uint8
{
 Idle, ClosingDoors, Moving, OpeningDoors, AwaitUnload, Fault
};

/** One-pallet freight lift. Separate from the forklift/FMS passenger elevator. */
UCLASS(BlueprintType)
class MSC_VR_API AWarehousePalletLift : public AActor
{
 GENERATED_BODY()
public:
 AWarehousePalletLift();
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pallet Lift") bool bPowerAvailable=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pallet Lift") bool bEStopReleased=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pallet Lift") bool bDoorSensorsHealthy=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pallet Lift") bool bLevelSensorHealthy=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pallet Lift",meta=(ClampMin="1",ClampMax="2000")) float RatedMassKg=2000.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pallet Lift",meta=(ClampMin="1",ClampMax="50")) float TravelSpeedCm=35.f;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Pallet Lift") float ConveyorHeightCm=40.f;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Pallet Lift") EWarehousePalletLiftState State=EWarehousePalletLiftState::Idle;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Pallet Lift") FString Status=TEXT("READY / ONE PALLET / 2000 KG");
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Pallet Lift") int32 CurrentFloor=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Pallet Lift") int32 DestinationFloor=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Pallet Lift") int32 CompletedTransfers=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Pallet Lift") TObjectPtr<AWarehousePallet> AcceptedPallet;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Pallet Lift") float PayloadMassKg=0.f;
 // Authoring cutout origin; runtime BeginPlay captures the installed transform.
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Layout") FVector InitialShaftPosition;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") FVector2D OpeningSizeCm=FVector2D(180,180);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") FVector2D LandingLocalMin=FVector2D(-250,-90);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") FVector2D LandingLocalMax=FVector2D(-90,90);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") FVector ShaftClearanceHalf=FVector(80,80,560);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") FVector ShaftClearanceCenter=FVector(0,0,560);
 UFUNCTION(BlueprintPure,Category="Pallet Lift") float FloorHeight(int32 Floor) const;
 UFUNCTION(BlueprintPure,Category="Pallet Lift") bool IsIdleForLayout() const;
 UFUNCTION(BlueprintPure,Category="Pallet Lift") bool IsHandlingCargo(AActor* Cargo) const;
 UFUNCTION(BlueprintPure,Category="Pallet Lift") FText GetTypeLabel() const;
 UFUNCTION(BlueprintPure,Category="Pallet Lift") FText GetInteractionText(int32 PlayerFloor) const;
 UFUNCTION(BlueprintCallable,Category="Pallet Lift") bool RequestTransfer(AWarehousePallet* Pallet,int32 FromFloor,int32 ToFloor);
 UFUNCTION(BlueprintCallable,Category="Pallet Lift") bool CallToFloor(int32 Floor);
 UFUNCTION(BlueprintCallable,Category="Pallet Lift") bool TryInteract(int32 PlayerFloor);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Pallet Lift") TObjectPtr<UStaticMeshComponent> Frame;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Pallet Lift") TObjectPtr<UStaticMeshComponent> Platform;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Pallet Lift") TArray<TObjectPtr<UStaticMeshComponent>> Gates;
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Display;
 UPROPERTY(Transient) TObjectPtr<UPhysicsConstraintComponent> PlatformJoint;
 UPROPERTY(Transient) TArray<TObjectPtr<UPhysicsConstraintComponent>> GateJoints;
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> GuideAnchor;
 UPROPERTY(Transient) TArray<TObjectPtr<AActor>> Payload;
 float PlatformTopOffsetCm=5.f;
 float PlatformHeightCm=40.f;
 float PlatformTargetCm=40.f;
 float DoorOpeningCm=0.f;
 float SincePayloadCheck=0.f;
 float StationarySeconds=0.f;
 bool bLoadedTransfer=false;
 FString LoadInterlock;
 FString ValidateLoad(AWarehousePallet* Pallet,TArray<AActor*>& Loads,float& MassKg) const;
 bool HasPlatformSupport(AWarehousePallet* Pallet) const;
 bool PortalClear() const;
 bool CabinFreeOfPeopleAndVehicles() const;
 bool AllDoorsClosed() const;
 bool SetDoors(bool Open,float Dt);
 bool MovePlatform(float Dt);
 void HoldPlatform();
 void SetDeckLock(bool Locked);
 void SetState(EWarehousePalletLiftState Next,const FString& Message);
 void ShowStatus(const FString& Message);
 FString CheckInterlocks() const;
};
