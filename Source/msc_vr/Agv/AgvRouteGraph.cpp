#include "AgvRouteGraph.h"
#include "AgvRouteNode.h"
#include "EngineUtils.h"

bool AgvRouteGraph::FindRoute(UWorld* World, const FVector2D& From, FName GoalId, double SnapDistanceCm, FAgvRoute& Out, FString& OutError)
{
	Out = FAgvRoute();
	TArray<AAgvRouteNode*> Nodes;
	if (World)
	{
		for (TActorIterator<AAgvRouteNode> It(World); It; ++It)
		{
			Nodes.Add(*It);
		}
	}
	const int32 Goal = Nodes.IndexOfByPredicate([GoalId](const AAgvRouteNode* Node) { return Node->NodeId == GoalId; });
	if (Goal == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("UNKNOWN NODE %s"), *GoalId.ToString());
		return false;
	}

	TArray<TArray<int32>> Adjacent;
	Adjacent.SetNum(Nodes.Num());
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		for (AAgvRouteNode* Link : Nodes[Index]->Links)
		{
			const int32 Other = Nodes.Find(Link);
			if (Other != INDEX_NONE && Other != Index)
			{
				Adjacent[Index].AddUnique(Other);
				Adjacent[Other].AddUnique(Index);
			}
		}
	}

	const double Unreached = TNumericLimits<double>::Max();
	TArray<double> Cost;
	Cost.Init(Unreached, Nodes.Num());
	TArray<int32> Previous;
	Previous.Init(INDEX_NONE, Nodes.Num());

	// Join on the nearest lane so a vehicle stopped mid-lane does not first return to a node.
	double LaneDistance = Unreached;
	int32 LaneA = INDEX_NONE, LaneB = INDEX_NONE;
	FVector2D LanePoint = From;
	int32 Nearest = 0;
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (FVector2D::DistSquared(From, Nodes[Index]->GetPoint()) < FVector2D::DistSquared(From, Nodes[Nearest]->GetPoint()))
		{
			Nearest = Index;
		}
		for (int32 Other : Adjacent[Index])
		{
			const FVector2D Point = FMath::ClosestPointOnSegment2D(From, Nodes[Index]->GetPoint(), Nodes[Other]->GetPoint());
			const double Distance = FVector2D::Distance(From, Point);
			if (Index < Other && Distance < LaneDistance)
			{
				LaneDistance = Distance;
				LaneA = Index;
				LaneB = Other;
				LanePoint = Point;
			}
		}
	}
	if (LaneA != INDEX_NONE && LaneDistance <= SnapDistanceCm)
	{
		Out.Start = LanePoint;
		Cost[LaneA] = FVector2D::Distance(LanePoint, Nodes[LaneA]->GetPoint());
		Cost[LaneB] = FVector2D::Distance(LanePoint, Nodes[LaneB]->GetPoint());
	}
	else
	{
		const double Distance = FVector2D::Distance(From, Nodes[Nearest]->GetPoint());
		Out.Start = Distance <= SnapDistanceCm ? Nodes[Nearest]->GetPoint() : From;
		Cost[Nearest] = Distance;
	}

	// Dijkstra; a network is tens of nodes, so a linear scan for the minimum is enough.
	TArray<bool> Done;
	Done.Init(false, Nodes.Num());
	for (;;)
	{
		int32 Current = INDEX_NONE;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (!Done[Index] && Cost[Index] < Unreached && (Current == INDEX_NONE || Cost[Index] < Cost[Current]))
			{
				Current = Index;
			}
		}
		if (Current == INDEX_NONE || Current == Goal)
		{
			break;
		}
		Done[Current] = true;
		for (int32 Other : Adjacent[Current])
		{
			const double Candidate = Cost[Current] + FVector2D::Distance(Nodes[Current]->GetPoint(), Nodes[Other]->GetPoint());
			if (Candidate < Cost[Other])
			{
				Cost[Other] = Candidate;
				Previous[Other] = Current;
			}
		}
	}
	if (Cost[Goal] == Unreached)
	{
		OutError = FString::Printf(TEXT("NO ROUTE TO %s"), *GoalId.ToString());
		return false;
	}
	for (int32 Index = Goal; Index != INDEX_NONE; Index = Previous[Index])
	{
		Out.Nodes.Insert(Nodes[Index], 0);
	}
	// Starting on a node joins whichever of its lanes rounding picks; drop that node so the route does not depend on it.
	if (Out.Nodes.Num() > 1 && FVector2D::Distance(Out.Start, Out.Nodes[0]->GetPoint()) < 1.0)
	{
		Out.Nodes.RemoveAt(0);
	}
	return true;
}
