#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "WarehouseCarryAnimInstance.generated.h"

/** Copies the existing locomotion pose and adds two-handed support of the load. */
UCLASS(Transient)
class MSC_VR_API UWarehouseCarryAnimInstance : public UAnimInstance
{
 GENERATED_BODY()
protected:
 virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
 virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
