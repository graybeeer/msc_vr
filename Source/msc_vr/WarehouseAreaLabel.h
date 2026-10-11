#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseAreaLabel.generated.h"

/** Nonphysical, camera-facing area sign. Floor filters also apply to its screen-space widget. */
UCLASS()
class MSC_VR_API AWarehouseAreaLabel : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseAreaLabel();
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Area") FText AreaName;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Area", meta=(ClampMin="0",ClampMax="2")) int32 FloorIndex=0;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Area", meta=(ClampMin="100")) float ViewDistanceCm=2500.f;
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 bool ReplacesWorkerLabel(FVector Head) const;
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<class UWidgetComponent> AreaLabel;
};
