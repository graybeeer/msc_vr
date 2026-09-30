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
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<class UWidgetComponent> RoleLabel;
};
