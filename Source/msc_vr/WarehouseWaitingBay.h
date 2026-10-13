#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseWaitingBay.generated.h"
class AWarehouseForklift;

/** A parking pose with no electrical charging equipment. */
UCLASS()
class MSC_VR_API AWarehouseWaitingBay : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseWaitingBay();
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Parking") FString BayName=TEXT("일반 대기 자리");
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Parking") FVector2D SizeCm=FVector2D(250,160);
 // Parking marks allow modest alignment error; electrical docks stay stricter.
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Parking",meta=(ClampMin="1",ClampMax="10")) float PositionToleranceCm=8;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Parking",meta=(ClampMin="1",ClampMax="5")) float HeadingToleranceDegrees=5;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Parking") TObjectPtr<AWarehouseForklift> Occupant;
 bool TryReserve(AWarehouseForklift* Vehicle);
 void Release(AWarehouseForklift* Vehicle);
 bool IsParked(const AWarehouseForklift* Vehicle) const;
 virtual void OnConstruction(const FTransform& Transform) override;
 virtual void BeginPlay() override;
};
