#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "WarehousePlayerMovement.generated.h"

/** Responsive player controls with Unreal's normal swept character collision.
 * Forklifts and warehouse cargo continue to use their own physical movement.
 */
UCLASS()
class MSC_VR_API UWarehousePlayerMovement : public UCharacterMovementComponent
{
 GENERATED_BODY()
public:
 UWarehousePlayerMovement(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());
 virtual void CalcVelocity(float DeltaTime,float Friction,bool bFluid,float BrakingDeceleration) override;
 virtual void ApplyAccumulatedForces(float DeltaSeconds) override;
 virtual void PostPhysicsTickComponent(float DeltaTime,FCharacterMovementComponentPostPhysicsTickFunction& ThisTickFunction) override;
};
