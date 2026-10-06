#pragma once

#include "CoreMinimal.h"

class AAgvRouteNode;
class UWorld;

struct FAgvRoute
{
	/** Where the vehicle joins the network: on the nearest lane, or its own position when off the network. */
	FVector2D Start = FVector2D::ZeroVector;
	TArray<AAgvRouteNode*> Nodes;
};

namespace AgvRouteGraph
{
	/** Shortest route over the placed AAgvRouteNode network from a position to the node named GoalId. */
	bool FindRoute(UWorld* World, const FVector2D& From, FName GoalId, double SnapDistanceCm, FAgvRoute& Out, FString& OutError);
}
