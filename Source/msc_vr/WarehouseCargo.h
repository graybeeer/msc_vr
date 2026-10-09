#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseCargo.generated.h"
class UStaticMesh;
class UStaticMeshComponent;

/** Generated packing specification. Densities include air/protection between products, not raw material density. */
USTRUCT(BlueprintType)
struct FWarehouseCargoRecipe
{
 GENERATED_BODY()
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bValid = false;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 RuleVersion = 1;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 DeliverySeed = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 ItemIndex = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) int32 ProductIndex = -1;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector SizeLimitCm = FVector::ZeroVector;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float AmountScale = 1;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FText ProductKind;
 // Local mesh X/Y/Z dimensions, centimetres, including packaging.
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FVector SizeCm = FVector::ZeroVector;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float PackedDensityKgM3 = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float NetMassKg = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float PackagingMassKg = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float GrossMassKg = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bFragile = false;
};

UCLASS()
class MSC_VR_API AWarehouseCargo : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseCargo();
 // Runtime active parcels retain their collision geometry; replace only during initialization/editor setup.
 UFUNCTION(BlueprintCallable, Category="Cargo") void SetCargoMesh(UStaticMesh* Mesh);
 UFUNCTION(BlueprintCallable, Category="Cargo") static void ConfigureMeshCollision(UStaticMesh* Mesh, bool Complex);
 static void ConfigureCarryPhysics(UStaticMeshComponent* Component, float MassKg);
 UStaticMeshComponent* GetCargoBody() const { return Body; }
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cargo") FName CargoId;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Cargo") FText CargoKind = FText::FromString(TEXT("일반 상품"));
 // Gross mass includes the contents and their packaging, in kilograms.
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cargo", meta=(ClampMin="0.1")) float GrossMassKg = 12.f;
 UFUNCTION(BlueprintCallable, Category="Cargo") void SetGrossMassKg(float MassKg);
 // ProductIndex=-1 selects a product deterministically. Invalid inputs return bValid=false.
 // Save the recipe (including seed, item index, size limit, amount, version) with deliveries, not the arrival time.
 UFUNCTION(BlueprintPure, Category="Cargo|Generation") static FWarehouseCargoRecipe GenerateCargoRecipe(int32 DeliverySeed, int32 ItemIndex, int32 ProductIndex, FVector MaxSizeCm, float AmountScale = 1.f);
 UFUNCTION(BlueprintPure, Category="Cargo|Generation") static int32 GetCargoProfileCount();
 // Configure before BeginPlay (or on a deferred spawn). Active physics parcels cannot be resized/teleported.
 UFUNCTION(BlueprintCallable, Category="Cargo|Generation") bool ApplyCargoRecipe(const FWarehouseCargoRecipe& Recipe);
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Cargo|Generation") FWarehouseCargoRecipe Packing;
 UFUNCTION(BlueprintPure, Category="Cargo") FText GetCargoDescription() const;
 virtual void OnConstruction(const FTransform& Transform) override;
 virtual void BeginPlay() override;
 void WakeStackAbove();
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
};
