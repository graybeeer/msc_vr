#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseForklift.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UPointLightComponent;

UCLASS()
class MSC_VR_API AWarehouseForklift : public AActor
{
	GENERATED_BODY()

public:
	AWarehouseForklift();

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> CollisionBox;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> Beacon;

	UStaticMeshComponent* AddPart(FName Name, FVector Location, FVector Scale, FRotator Rotation = FRotator::ZeroRotator);
};
