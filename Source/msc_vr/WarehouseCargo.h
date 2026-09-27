#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseCargo.generated.h"
class UStaticMesh;
class UStaticMeshComponent;
UCLASS()
class MSC_VR_API AWarehouseCargo : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseCargo();
 UFUNCTION(BlueprintCallable, Category="Cargo") void SetCargoMesh(UStaticMesh* Mesh);
 UFUNCTION(BlueprintCallable, Category="Cargo") static void ConfigureMeshCollision(UStaticMesh* Mesh, bool Complex);
 UStaticMeshComponent* GetCargoBody() const { return Body; }
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
};
