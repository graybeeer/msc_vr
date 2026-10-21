#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseAutonomyTypes.h"
#include "WarehouseFleetCoordinator.generated.h"

class AWarehouseForklift;
class AWarehouseChargingStation;

/** Server-owned passage leases. Local safety scanners and Chaos remain authoritative for contact. */
UCLASS()
class MSC_VR_API AWarehouseFleetCoordinator : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseFleetCoordinator();
 static AWarehouseFleetCoordinator* Find(const AActor* Context);
 bool Permit(AWarehouseForklift* Vehicle,const FTransform* PreciseGoal=nullptr);
 bool AdvanceYield(AWarehouseForklift* Vehicle,float Dt);
 bool PlanningClear(const AWarehouseForklift* Vehicle,const FBox& Box) const;
 void Release(AWarehouseForklift* Vehicle);
 AWarehouseChargingStation* AllocateCharger(AWarehouseForklift* Vehicle);
 UFUNCTION(BlueprintCallable,Category="Traffic") void CloseSection(const FString& Id,FVector Min,FVector Max,const FString& Reason);
 UFUNCTION(BlueprintCallable,Category="Traffic") void ReopenSection(const FString& Id);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Traffic") int32 WaitCount=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Traffic") int32 YieldCount=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Traffic") int32 DetourCount=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Traffic") int32 ClosedSectionCount=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Traffic") FString Status;
private:
 struct FRequest
 {
  TArray<FBox> Corridor;
  uint64 Order=0;
  double Since=0,BlockedSince=-1,LastDetour=-100,LastEscape=-100;
  bool Granted=false;
 };
 struct FYield
 {
  TWeakObjectPtr<AWarehouseForklift> Leader;
  TArray<FBox> Escape;
  FBox Clearance;
  FTransform Goal;
  TOptional<FTransform> ResumeGoal;
  double Started=0,LastReplan=-100;
  int32 Replans=0;
  bool Arrived=false;
  bool Separating=false,Separated=false;
  EWarehouseAIState Resume=EWarehouseAIState::Ready;
 };
 TMap<TWeakObjectPtr<AWarehouseForklift>,FRequest> Requests;
 TMap<TWeakObjectPtr<AWarehouseForklift>,FYield> Yields;
 TMap<FString,FBox> ClosedSections;
 FBox PlanningObstacle{ForceInit};
 uint64 NextOrder=0;
 TArray<FBox> Corridor(const AWarehouseForklift* Vehicle) const;
 FBox Footprint(const AWarehouseForklift* Vehicle,const FTransform& Pose) const;
 bool Retreat(AWarehouseForklift* Vehicle,AWarehouseForklift* Leader,const TArray<FBox>& Passage);
 static bool Intersects(const TArray<FBox>& A,const TArray<FBox>& B);
 void Waiting(AWarehouseForklift* Vehicle,const FString& Reason);
};
