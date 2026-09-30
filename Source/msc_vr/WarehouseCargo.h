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
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cargo") FName CargoId;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cargo") FText CargoKind = FText::FromString(TEXT("일반 상품"));
 // Gross mass includes the contents and their packaging, in kilograms.
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cargo", meta=(ClampMin="0.1")) float GrossMassKg = 12.f;
 UFUNCTION(BlueprintCallable, Category="Cargo") void SetGrossMassKg(float MassKg);
 UFUNCTION(BlueprintPure, Category="Cargo") FText GetCargoDescription() const;
 virtual void OnConstruction(const FTransform& Transform) override;
 virtual void BeginPlay() override;
 void WakeStackAbove();
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
};
