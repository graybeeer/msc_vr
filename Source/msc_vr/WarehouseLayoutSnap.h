#pragma once
#include "CoreMinimal.h"

class AActor;

/** XY guides for paused rack/lift layout edits; the caller retains placement validation. */
namespace WarehouseLayoutSnap
{
 // OriginalSnapBounds is the saved physical frame bounds, excluding signs,
 // lanes and carried goods. Targets are same-floor rack groups and real walls.
 // Returns a 10cm grid delta, or a face/edge alignment within 20cm with 0.1cm gap.
 MSC_VR_API FVector Compute(const AActor* Context,const FBox& OriginalSnapBounds,FVector RawDelta,
  const TArray<AActor*>& MovingIgnoredActors,int32 Floor,FString& OutHint);
}
