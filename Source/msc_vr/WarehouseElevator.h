#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseElevator.generated.h"

class AWarehouseForklift;
class UStaticMeshComponent;
class UTextRenderComponent;

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
 virtual void Tick(float DeltaSeconds) override;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elevator") bool bPowerAvailable=true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elevator") bool bEStopReleased=true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elevator") bool bDoorSensorsHealthy=true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elevator") bool bLevelSensorHealthy=true;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elevator", meta=(ClampMin="1",ClampMax="2000")) float RatedMassKg=2000;
 // Project assumption; actual manufacturer speed depends on the supplied configuration.
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elevator", meta=(ClampMin="1",ClampMax="50")) float TravelSpeedCm=35;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") EWarehouseElevatorState State=EWarehouseElevatorState::Idle;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") TObjectPtr<AWarehouseForklift> ReservedVehicle;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") int32 CurrentFloor=0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") int32 DestinationFloor=0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") int32 CompletedTransfers=0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elevator") FString Status=TEXT("READY / 2000 KG MAX");
 UFUNCTION(BlueprintPure, Category="Elevator") int32 FloorAtHeight(float WorldZ) const;
 UFUNCTION(BlueprintPure, Category="Elevator") float FloorHeight(int32 Floor) const;
 UFUNCTION(BlueprintPure, Category="Elevator") FTransform WaitingPose(int32 Floor) const;
 UFUNCTION(BlueprintPure, Category="Elevator") FTransform BoardingPose(int32 Floor) const;
 UFUNCTION(BlueprintPure, Category="Elevator") FString CheckInterlocks() const;
 UFUNCTION(BlueprintPure, Category="Elevator") bool CanEnter(AWarehouseForklift* Vehicle) const;
 UFUNCTION(BlueprintPure, Category="Elevator") bool CanExit(AWarehouseForklift* Vehicle) const;
 UFUNCTION(BlueprintCallable, Category="Elevator") bool RequestTransfer(AWarehouseForklift* Vehicle,int32 FromFloor,int32 ToFloor);
 UFUNCTION(BlueprintCallable, Category="Elevator") bool ConfirmBoarded(AWarehouseForklift* Vehicle);
 UFUNCTION(BlueprintCallable, Category="Elevator") bool ConfirmExited(AWarehouseForklift* Vehicle);
 UFUNCTION(BlueprintCallable, Category="Elevator") void AdvanceElevator(float Seconds);
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Platform;
 UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Gates;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Display;
 float PlatformHeight=0;
 float DoorOpening=0;
 int32 EntryFloor=0;
 bool bBoarded=false;
 bool PortalClear() const;
 bool CabinClear() const;
 bool FitsInside(AWarehouseForklift* Vehicle) const;
 bool SetDoors(bool Open,float Dt);
 bool MovePlatform(int32 Floor,float Dt);
 void SetState(EWarehouseElevatorState Next,const TCHAR* Message);
};
