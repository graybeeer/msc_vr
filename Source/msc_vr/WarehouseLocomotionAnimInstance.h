#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "WarehouseLocomotionAnimInstance.generated.h"

class UBlendSpace;
class UAnimSequence;
class UAnimationAsset;
class USkeleton;

/** Game Animation Sample poses driven by the warehouse's actual Chaos motion. */
UCLASS(Transient)
class MSC_VR_API UWarehouseLocomotionAnimInstance : public UAnimInstance
{
 GENERATED_BODY()
public:
 virtual void NativeInitializeAnimation() override;
 /** Native editor operations that Unreal's Python asset API does not expose. */
 UFUNCTION(BlueprintCallable, Category="Editor", meta=(DevelopmentOnly))
 static bool PrepareMotionAsset(UAnimationAsset* Asset,USkeleton* Skeleton);
 UPROPERTY(Transient, BlueprintReadOnly, Category="Locomotion") bool ShouldMove=false;
 UPROPERTY(Transient, BlueprintReadOnly, Category="Locomotion") float GroundSpeed=0;
 UPROPERTY(Transient, BlueprintReadOnly, Category="Locomotion") bool InAir=false;
 UPROPERTY(Transient, BlueprintReadOnly, Category="Locomotion") bool Crouching=false;
 UPROPERTY(Transient, BlueprintReadOnly, Category="Locomotion") bool Landing=false;
 UPROPERTY(Transient) TObjectPtr<UBlendSpace> StandingMotion;
 UPROPERTY(Transient) TObjectPtr<UBlendSpace> CrouchingMotion;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> JumpStartMotion;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> FallMotion;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> LandingMotion;
protected:
 virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
 virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
