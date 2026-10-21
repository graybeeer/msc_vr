#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehousePallet.generated.h"

class AWarehouseCargo;
class UPhysicsConstraintComponent;
class USplineMeshComponent;
class UMaterialInterface;

UENUM(BlueprintType)
enum class EWarehouseReceiptState : uint8 { Pending, Confirmed, WrapTorn };

/** Existing Fab wooden pallet, fitted to 110 x 110 x 15 cm. */
UCLASS()
class MSC_VR_API AWarehousePallet : public AActor
{
 GENERATED_BODY()
public:
 AWarehousePallet();
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Inbound") EWarehouseReceiptState ReceiptState=EWarehouseReceiptState::Pending;
 // Training assumptions for one carton-to-pallet film connection, not certified film data.
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Inbound|Wrap",meta=(ClampMin="1")) float WrapBreakForceN=1500;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Inbound|Wrap",meta=(ClampMin="1")) float WrapBreakTorqueNm=250;
 UPROPERTY(EditAnywhere,Category="Inbound|Wrap") TSoftObjectPtr<UMaterialInterface> WrapMaterial;
 UFUNCTION(BlueprintPure,Category="Inbound") FText GetReceiptDescription() const;
 UFUNCTION(BlueprintPure,Category="Inbound") TArray<AWarehouseCargo*> GetWrappedCargo() const;
 bool ConfirmInbound(const TArray<AActor*>& Loads);
 // External removal/damage opens the whole unit. NPC scans never silently reseal torn film.
 UFUNCTION(BlueprintCallable,Category="Inbound") void TearWrap();
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
 UPROPERTY(Transient) TArray<TObjectPtr<AWarehouseCargo>> WrappedCargo;
 UPROPERTY(Transient) TArray<TObjectPtr<UPhysicsConstraintComponent>> WrapJoints;
 UPROPERTY(Transient) TArray<TObjectPtr<USplineMeshComponent>> WrapFilm;
 bool bWrapBreakPending=false;
 UFUNCTION() void OnWrapJointBroken(int32 ConstraintIndex);
 void BuildWrapFilm();
};
