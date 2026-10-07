#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehousePallet.generated.h"

/** Existing Fab wooden pallet, fitted to 110 x 110 x 15 cm. */
UCLASS()
class MSC_VR_API AWarehousePallet : public AActor
{
 GENERATED_BODY()
public:
 AWarehousePallet();
 virtual void BeginPlay() override;
 class UStaticMeshComponent* GetPalletBody() const { return Body; }
 UFUNCTION(BlueprintCallable, Category="Editor") static void TransformCollision(class UStaticMesh* Mesh, FTransform Transform);
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Load", meta=(ClampMin="0")) float PayloadMassKg = 0;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Load", meta=(ClampMin="1")) float PalletMassKg = 25;
 UFUNCTION(BlueprintCallable, Category="Training")
 bool CanEngage(const FTransform& ForkFrame) const;
 float GetSupportLiftOffset(const FTransform& ForkFrame) const;
 UFUNCTION(BlueprintCallable, Category="Training")
 static void FitCollisionBounds(class UStaticMesh* Mesh);
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<class UStaticMeshComponent> Body;
};
