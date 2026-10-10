#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Blueprint/UserWidget.h"
#include "WarehouseWorker.generated.h"

/** Screen-space text uses the runtime font fallback, including Korean glyphs. */
UCLASS()
class MSC_VR_API UWarehouseRoleWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 FText Label;
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
};

/** Stationary warehouse worker. No controller, AI, navigation or task execution. */
UCLASS()
class MSC_VR_API AWarehouseWorker : public ACharacter
{
 GENERATED_BODY()
public:
 AWarehouseWorker();
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Worker") FText RoleName;
 UFUNCTION(BlueprintCallable, Category="Worker") bool CopyPlayerAppearance(TSubclassOf<ACharacter> PlayerClass);
 UFUNCTION(BlueprintPure, Category="Worker") bool HasStandingClearance() const;
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Worker|Physics") float HumanMassKg=80.f;
 UFUNCTION(BlueprintPure, Category="Worker|Physics") bool IsRagdoll() const { return bRagdoll; }
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<class UWidgetComponent> RoleLabel;
 bool bRagdoll=false;
 float LabelElapsed=0.f;
};

namespace WarehouseHumanPhysics
{
 MSC_VR_API bool DriveCapsule(ACharacter* Person,FVector DesiredVelocity,float MotorForceN=500.f,bool SupportLegs=true,float DesiredHalfHeight=-1.f,float* GroundDistance=nullptr,float ExtraSupportedMassKg=0.f,FVector LoadOffsetCm=FVector::ZeroVector);
 MSC_VR_API void StartRagdoll(ACharacter* Person,float MassKg);
}
