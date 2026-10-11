#pragma once
#include "CoreMinimal.h"

class AActor;

/** Runtime floor transaction for the saved warehouse's one XY-translated lift. */
namespace WarehouseElevatorLayout
{
 // Delta is absolute from the saved (1000,1600,0) shaft, not the previous drag.
 // Preserves the existing 352cm ground pit and 376cm upper shaft openings.
 // ValidatePlacement=false is only for restoring a previously accepted opening;
 // payloads restored to the old shaft must never prevent transaction rollback.
 MSC_VR_API bool SetOpening(AActor* Context,FVector ShaftDeltaFromSavedPosition,bool ValidatePlacement=true);
 // Restore initial native floor actors and remove every temporary replacement.
 MSC_VR_API void RestoreInitial(AActor* Context);
}
