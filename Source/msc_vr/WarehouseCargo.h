#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseCargo.generated.h"

class UBoxComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** A warehouse load with a simple collision body, independent of the Fab mesh. */
UCLASS()
class MSC_VR_API AWarehouseCargo : public AActor
{
	GENERATED_BODY()

public:
	AWarehouseCargo();

	UFUNCTION(BlueprintCallable, Category = "Cargo")
	void SetCargoMesh(UStaticMesh* Mesh);

	UBoxComponent* GetCargoBody() const { return Body; }

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> Body;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Visual;
};
