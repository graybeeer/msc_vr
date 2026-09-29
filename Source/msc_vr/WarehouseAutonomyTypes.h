#pragma once
#include "CoreMinimal.h"
#include "WarehouseAutonomyTypes.generated.h"
class AWarehousePallet;

UENUM(BlueprintType)
enum class EWarehouseAIState : uint8
{
 Off, SelfCheck, Ready, ValidateJob, PlanPickup, NavigatePickup, SlowApproach,
 DetectPallet, EstimatePose, AlignVehicle, CorrectFork, InsertFork, VerifyInsertion,
 LiftLoad, VerifyLoad, DepartPickup, TravelHeight, PlanDelivery, NavigateDelivery,
 AlignUnload, LowerLoad, WithdrawFork, VerifyUnload, ReportComplete,
 PlanCharge, NavigateCharge, DockCharge, Charging, LeaveCharger,
 WaitingObstacle, Paused, Fault
};

USTRUCT(BlueprintType)
struct FWarehouseWorkOrder
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite) FString JobId;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) FString SourceSystem = TEXT("FMS-SIM");
 UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<AWarehousePallet> Pallet;
 // Desired pallet BASE pose, not the vehicle pose. UE centimetres.
 UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform Destination;
};

USTRUCT(BlueprintType)
struct FWarehouseJobReport
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString JobId;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString Result;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FString Detail;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float SimulationSeconds = 0;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWarehouseReportEvent, const FWarehouseJobReport&, Report);

USTRUCT(BlueprintType)
struct FWarehouseRoutePoint
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FTransform Pose;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bReverse = false;
};
