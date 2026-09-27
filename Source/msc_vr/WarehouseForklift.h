#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseForklift.generated.h"

class AWarehousePallet;
class UPointLightComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EWarehouseCycle : uint8 { Idle, Approach, Lift, Reverse, Lower, Withdraw, Complete };

UCLASS()
class MSC_VR_API AWarehouseForklift : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseForklift();
 virtual void Tick(float DeltaSeconds) override;
 UFUNCTION(BlueprintCallable, Category="Training") void TogglePower();
 UFUNCTION(BlueprintCallable, Category="Training") void AdvanceSimulation(float DeltaSeconds);
 UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Training") TObjectPtr<AWarehousePallet> TargetPallet;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Training") bool bPowered = false;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Training") EWarehouseCycle State = EWarehouseCycle::Idle;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Training") FString Status = TEXT("E : START");
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Carriage;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Beacon;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UTextRenderComponent> Display;
 FVector StartLocation = FVector::ZeroVector;
 FVector WithdrawStart = FVector::ZeroVector;
 float LiftOffset = 0;
 bool bSupportingPallet = false;
 bool ClearToMove(FVector Delta, bool LiftOnly);
 bool MoveVehicle(FVector Delta);
 bool MoveLift(float Height);
 void StopFor(const FString& Reason);
 void SetStatus(const FString& Message);
};
