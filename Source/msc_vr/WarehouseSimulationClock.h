#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseSimulationClock.generated.h"

/** Same 1/30 world step at every speed, with the existing 1/120 Chaos substeps. */
UCLASS()
class MSC_VR_API AWarehouseSimulationClock : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseSimulationClock();
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UFUNCTION(BlueprintCallable,Category="Simulation") bool SetSpeed(int32 Multiplier);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Simulation") int32 RequestedSpeed=1;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Simulation") float AchievedSpeed=1;
 static constexpr double StepSeconds=1./30.;
 static AWarehouseSimulationClock* Find(const AActor* Context);
private:
 void PaceFrame();
 FDelegateHandle FrameHandle;
 TWeakObjectPtr<class AWorldSettings> FixedWorldSettings;
 float OldMinimumWorldStep=0,OldMaximumWorldStep=0;
 double OldFixedDelta=0,NextFrame=0,SampleWall=0,SampleSimulation=0;
 bool OldFixed=false;
 int32 OldVSync=0;
 float OldOverrideFPS=0;
};
