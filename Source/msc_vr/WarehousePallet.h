#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehousePallet.generated.h"

/** 120 x 100 cm pallet. Visible boards are also its collision geometry. */
UCLASS()
class MSC_VR_API AWarehousePallet : public AActor
{
 GENERATED_BODY()
public:
 AWarehousePallet();
 UFUNCTION(BlueprintCallable, Category="Training")
 bool CanEngage(const FTransform& ForkFrame) const;
};
