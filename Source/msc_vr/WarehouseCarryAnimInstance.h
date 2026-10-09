#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "WarehouseCarryAnimInstance.generated.h"

/** Copies locomotion and poses both hands around a load or the remote-control phone. */
UCLASS(Transient)
class MSC_VR_API UWarehouseCarryAnimInstance : public UAnimInstance
{
 GENERATED_BODY()
protected:
 virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
 virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
