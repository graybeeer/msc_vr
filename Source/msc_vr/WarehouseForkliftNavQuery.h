#pragma once

#include "CoreMinimal.h"
#include "CollisionQueryParams.h"

class AActor;

// Each synchronous plan owns its query setup; every native collision check stays live.
struct FWarehouseNavigationQuery
{
 FCollisionQueryParams Params;
 FBox Envelope;
 FVector Pivot;
 bool bHasPivot;
 TArray<AActor*> PerceptionIgnored;
 FVector DiagnosticGoal=FVector::ZeroVector;
 bool bDiagnostics=false;
 bool bInsertionRetryEscape=false;
 bool bObservedPickupEscape=false;
 mutable uint8 GoalBlockReports=0;
 bool bGeometryFirst=false;
 mutable uint32 GeometryRejectedCandidates=0;
};
