#pragma once
#include "CoreMinimal.h"

class AActor;

/** Runtime floor transaction; every shaft shares consistent openings on all served floors. */
namespace WarehouseElevatorLayout
{
 // Compatibility entry point: current actor poses and per-lift opening sizes
 // rebuild every shaft. No saved global shaft coordinate is used.
 // ValidatePlacement=false is only for restoring a previously accepted opening;
 // payloads restored to the old shaft must never prevent transaction rollback.
 MSC_VR_API bool SetOpening(AActor* Context,FVector ShaftDeltaFromSavedPosition,bool ValidatePlacement=true);
 MSC_VR_API bool Initialize(AActor* Context);
 MSC_VR_API bool Rebuild(AActor* Context,bool ValidatePlacement=true,const TArray<AActor*>& Removed={});
 // Restore initial native floor actors and remove every temporary replacement.
 MSC_VR_API void RestoreInitial(AActor* Context);
}
