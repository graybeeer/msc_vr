// Time-optimal route search for the forklift: a hybrid A* over arcs, straights and turns on the spot, in place of the fixed
// lane anchors of PlanAutonomousRoute. Switch with Warehouse.Nav.HybridPlanner. See LOCALIZATION_HANDOFF.md 9-6.
#include "WarehouseForklift.h"
#include "WarehouseForkliftNavQuery.h"
#include "Algo/Reverse.h"
#include "WarehouseFleetCoordinator.h"
#include "WarehousePallet.h"
#include "AgvShadowSensors.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarHybridMemory(TEXT("Warehouse.Nav.HybridMemory"),1,TEXT("1: routes also keep off pallets the fleet saw earlier but cannot see now."));

namespace AgvHybrid
{
	constexpr double CellCm = 25.0;
	constexpr double HeadingDeg = 15.0;
	constexpr int32 HeadingBins = 24;
	constexpr double ArcRadiusTight = 130.0; // the planner's curvature limit is 117.3 cm
	constexpr double ArcRadiusWide = 260.0;
	constexpr double StepCm = 40.0;
	constexpr double LongStepCm = 120.0;
	constexpr double LongStepClearanceCm = 450.0;
	constexpr double HeuristicInflateCm = 55.0;
	constexpr double HeuristicFreeNearCm = 250.0;
	constexpr double ReversePenalty = 1.05;
	constexpr double SearchWeight = 1.2;
	constexpr double AnalyticRangeCm = 1200.0;
	constexpr int32 MaxExpansions = 12000;
	constexpr int32 MaxAnalyticTries = 15;
	constexpr int32 MaxNearAnalyticTries = 60;
	constexpr int32 ExtraExpansionsAfterFound = 2500;
	constexpr int32 MaxSearchQueries = 220000; // about 0.5s
	constexpr int32 MaxImproveQueries = 70000;
	constexpr int32 MaxSearches = 3;
	constexpr float Unreached = 1e9f;

	enum EMode : uint8 { MoveForward, MoveReverse, MoveTurn, MoveRest };

	/** Time per route element, from the speed limits of the route follower. Limits themselves stay in FollowRoute. */
	struct FSpec
	{
		double Speed = 180.0;
		double LateralAccel = 80.0;
		double TurnRate = 0.4;
		double StopGo = 2.0;
		double TurnEntry = 3.0;

		double Travel(double Length, double Curvature, bool bReverse) const
		{
			const double Limit = FMath::Abs(Curvature) > 1e-6 ? FMath::Min(Speed, FMath::Sqrt(LateralAccel / FMath::Abs(Curvature))) : Speed;
			return Length / Limit * (bReverse ? ReversePenalty : 1.0);
		}
	};

	/** Obstacle grid of one plan: clearance for cheap collision checks, drive time to the goal as the heuristic. */
	struct FGrid
	{
		FVector2D Origin = FVector2D::ZeroVector;
		int32 Width = 0, Height = 0;
		TArray<uint8> Occupied;
		TArray<float> Clearance;
		TArray<float> GoalTime;
		bool bGoalTimeUsable = false;

		bool Cell(double X, double Y, int32& OutX, int32& OutY) const
		{
			OutX = FMath::FloorToInt32((X - Origin.X) / CellCm);
			OutY = FMath::FloorToInt32((Y - Origin.Y) / CellCm);
			return OutX >= 0 && OutY >= 0 && OutX < Width && OutY < Height;
		}
		int32 Index(int32 X, int32 Y) const { return Y * Width + X; }
		float ClearanceAt(double X, double Y) const
		{
			int32 CX, CY;
			return Cell(X, Y, CX, CY) ? Clearance[Index(CX, CY)] : 0.f;
		}
	};

	struct FNode
	{
		double X = 0, Y = 0, Yaw = 0;
		float G = 0;
		int32 Parent = INDEX_NONE;
		double Curvature = 0, Length = 0, TurnAngle = 0;
		uint8 Mode = MoveRest;
		int8 TurnSign = 0;
	};

	struct FOpen
	{
		float F;
		int32 Node;
		bool operator<(const FOpen& Other) const { return F < Other.F; }
	};

	static void Rasterize(FGrid& Field, const FAgvObservedObstacle& Entry)
	{
		const FBox2D World(FVector2D(Entry.WorldBounds.Min), FVector2D(Entry.WorldBounds.Max));
		int32 X0, Y0, X1, Y1;
		Field.Cell(World.Min.X, World.Min.Y, X0, Y0);
		Field.Cell(World.Max.X, World.Max.Y, X1, Y1);
		X0 = FMath::Clamp(X0, 0, Field.Width - 1); X1 = FMath::Clamp(X1, 0, Field.Width - 1);
		Y0 = FMath::Clamp(Y0, 0, Field.Height - 1); Y1 = FMath::Clamp(Y1, 0, Field.Height - 1);
		const bool bLocal = Entry.LocalBounds.IsValid != 0;
		for (int32 Y = Y0; Y <= Y1; ++Y)
		{
			for (int32 X = X0; X <= X1; ++X)
			{
				const FVector Centre(Field.Origin.X + (X + 0.5) * CellCm, Field.Origin.Y + (Y + 0.5) * CellCm, 0.0);
				bool bInside = true;
				if (bLocal)
				{
					const FVector Local = Entry.ObservedTransform.InverseTransformPosition(FVector(Centre.X, Centre.Y, Entry.ObservedTransform.GetLocation().Z));
					bInside = Local.X >= Entry.LocalBounds.Min.X - CellCm && Local.X <= Entry.LocalBounds.Max.X + CellCm &&
						Local.Y >= Entry.LocalBounds.Min.Y - CellCm && Local.Y <= Entry.LocalBounds.Max.Y + CellCm;
				}
				if (bInside)
				{
					Field.Occupied[Field.Index(X, Y)] = 1;
				}
			}
		}
	}

	/** Two-pass chamfer distance in cm. Overestimates true distance by up to 8%; the caller keeps that as margin. */
	static void BuildClearance(FGrid& Field)
	{
		const float Straight = (float)CellCm, Diagonal = (float)(CellCm * 1.4142);
		Field.Clearance.Init(Unreached, Field.Occupied.Num());
		for (int32 I = 0; I < Field.Occupied.Num(); ++I)
		{
			if (Field.Occupied[I])
			{
				Field.Clearance[I] = 0.f;
			}
		}
		auto Relax = [&](int32 X, int32 Y, int32 DX, int32 DY, float Step)
		{
			const int32 NX = X + DX, NY = Y + DY;
			if (NX >= 0 && NY >= 0 && NX < Field.Width && NY < Field.Height)
			{
				float& Value = Field.Clearance[Field.Index(X, Y)];
				Value = FMath::Min(Value, Field.Clearance[Field.Index(NX, NY)] + Step);
			}
		};
		for (int32 Y = 0; Y < Field.Height; ++Y)
		{
			for (int32 X = 0; X < Field.Width; ++X)
			{
				Relax(X, Y, -1, 0, Straight); Relax(X, Y, 0, -1, Straight); Relax(X, Y, -1, -1, Diagonal); Relax(X, Y, 1, -1, Diagonal);
			}
		}
		for (int32 Y = Field.Height - 1; Y >= 0; --Y)
		{
			for (int32 X = Field.Width - 1; X >= 0; --X)
			{
				Relax(X, Y, 1, 0, Straight); Relax(X, Y, 0, 1, Straight); Relax(X, Y, 1, 1, Diagonal); Relax(X, Y, -1, 1, Diagonal);
			}
		}
	}

	/** Drive time from every cell to the goal around inflated obstacles. Reports whether the start can reach the goal. */
	static void BuildGoalTime(FGrid& Field, const FVector2D& Start, const FVector2D& Goal, double Speed)
	{
		Field.GoalTime.Init(Unreached, Field.Occupied.Num());
		int32 GX, GY, SX, SY;
		if (!Field.Cell(Goal.X, Goal.Y, GX, GY) || !Field.Cell(Start.X, Start.Y, SX, SY))
		{
			return;
		}
		struct FItem { float T; int32 I; bool operator<(const FItem& O) const { return T < O.T; } };
		TArray<FItem> Heap;
		Field.GoalTime[Field.Index(GX, GY)] = 0.f;
		Heap.HeapPush({ 0.f, Field.Index(GX, GY) });
		const float Straight = (float)(CellCm / Speed), Diagonal = (float)(CellCm * 1.4142 / Speed);
		const double NearSquared = HeuristicFreeNearCm * HeuristicFreeNearCm;
		while (Heap.Num())
		{
			FItem Item;
			Heap.HeapPop(Item, EAllowShrinking::No);
			if (Item.T > Field.GoalTime[Item.I])
			{
				continue;
			}
			const int32 CX = Item.I % Field.Width, CY = Item.I / Field.Width;
			for (int32 DY = -1; DY <= 1; ++DY)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					const int32 NX = CX + DX, NY = CY + DY;
					if ((!DX && !DY) || NX < 0 || NY < 0 || NX >= Field.Width || NY >= Field.Height)
					{
						continue;
					}
					const int32 Next = Field.Index(NX, NY);
					if (Field.Clearance[Next] < HeuristicInflateCm)
					{
						const FVector2D P(Field.Origin.X + (NX + 0.5) * CellCm, Field.Origin.Y + (NY + 0.5) * CellCm);
						if (FVector2D::DistSquared(P, Goal) > NearSquared && FVector2D::DistSquared(P, Start) > NearSquared)
						{
							continue;
						}
					}
					const float T = Item.T + (DX && DY ? Diagonal : Straight);
					if (T < Field.GoalTime[Next])
					{
						Field.GoalTime[Next] = T;
						Heap.HeapPush({ T, Next });
					}
				}
			}
		}
		Field.bGoalTimeUsable = Field.GoalTime[Field.Index(SX, SY)] < Unreached;
	}

	/** Point at signed arc length S along a constant-curvature move. */
	static FORCENOINLINE void ArcPoint(double X, double Y, double Yaw, double Curvature, double S, double& OutX, double& OutY, double& OutYaw)
	{
		const double Theta = FMath::DegreesToRadians(Yaw);
		if (FMath::Abs(Curvature) < 1e-6)
		{
			OutX = X + S * FMath::Cos(Theta);
			OutY = Y + S * FMath::Sin(Theta);
			OutYaw = Yaw;
			return;
		}
		const double Next = Theta + Curvature * S;
		OutX = X + (FMath::Sin(Next) - FMath::Sin(Theta)) / Curvature;
		OutY = Y - (FMath::Cos(Next) - FMath::Cos(Theta)) / Curvature;
		OutYaw = FMath::RadiansToDegrees(Next);
	}

	static double Snap(double Yaw)
	{
		return FMath::GridSnap(Yaw, HeadingDeg);
	}

	static uint64 Key(double X, double Y, double Yaw, uint8 Mode)
	{
		const int32 CX = FMath::FloorToInt32(X / CellCm) + 100000, CY = FMath::FloorToInt32(Y / CellCm) + 100000;
		const int32 Bin = FMath::RoundToInt32(FRotator::ClampAxis(Yaw) / HeadingDeg) % HeadingBins;
		return (uint64(CX) << 36) | (uint64(CY) << 8) | (uint64(Bin) << 2) | Mode;
	}
}

float AWarehouseForklift::RouteTimeSeconds(const TArray<FWarehouseRoutePoint>& Route, const FTransform& Start, float* OutLength, float* OutTurnDegrees, int32* OutFlips) const
{
	using namespace AgvHybrid;
	const FSpec Spec = bSupportingPallet ? FSpec{ FMath::Min(100.0, (double)LoadedTravelSpeedCm), 40.0, 0.3, 2.0, 4.0 } : FSpec{ FMath::Min(180.0, (double)EmptyTravelSpeedCm), 80.0, 0.4, 2.5, 5.0 };
	double Time = 0, Length = 0, Turned = 0;
	int32 Flips = 0;
	uint8 Mode = MoveRest;
	FTransform Previous = Start;
	for (const FWarehouseRoutePoint& Point : Route)
	{
		const double DeltaYaw = FMath::Abs(FMath::FindDeltaAngleDegrees(Previous.Rotator().Yaw, Point.Pose.Rotator().Yaw));
		if (Point.bPivot)
		{
			Time += FMath::DegreesToRadians(DeltaYaw) / Spec.TurnRate + (Mode == MoveTurn ? 0.0 : Spec.TurnEntry);
			Turned += DeltaYaw;
			Mode = MoveTurn;
		}
		else
		{
			const double Segment = FVector::Dist2D(Previous.GetLocation(), Point.Pose.GetLocation());
			if (Segment > 0.01)
			{
				const uint8 Move = Point.bReverse ? MoveReverse : MoveForward;
				if (Mode != Move && Mode != MoveRest && Mode != MoveTurn)
				{
					Time += Spec.StopGo;
					++Flips;
				}
				Mode = Move;
				Time += Spec.Travel(Segment, FMath::DegreesToRadians(DeltaYaw) / Segment, Point.bReverse);
				Length += Segment;
				Turned += DeltaYaw;
			}
		}
		Previous = Point.Pose;
	}
	if (OutLength) *OutLength = (float)Length;
	if (OutTurnDegrees) *OutTurnDegrees = (float)Turned;
	if (OutFlips) *OutFlips = Flips;
	return (float)Time;
}

bool AWarehouseForklift::PlanHybridRoute(const FTransform& Start, const FTransform& FinalGoal, const FWarehouseNavigationQuery& InQuery, bool bStraightIn, TArray<FWarehouseRoutePoint>& OutRoute, FString* OutReport) const
{
	using namespace AgvHybrid;
	OutRoute.Reset();
	if (!SensorKit || !InQuery.bHasPivot || FMath::Abs(Start.GetLocation().Z - FinalGoal.GetLocation().Z) > 1.0)
	{
		return false;
	}
	FWarehouseNavigationQuery Query = InQuery;
	// The approach pose stands just clear of the pallet, closer than the travel margin allows, so the margin would call the goal
	// itself blocked. Near its own target the vehicle uses the catalog box of the pallet below instead of the scan of it.
	const bool bGuardTarget = !bSupportingPallet && bHasRecognizedTargetPose && IsValid(TargetPallet) && !Query.PerceptionIgnored.Contains(TargetPallet);
	const FTransform TargetPose = bGuardTarget ? SensorObservedTarget : FTransform::Identity;
	if (bGuardTarget && FVector::Dist2D(FinalGoal.GetLocation(), TargetPose.GetLocation()) < 350.0)
	{
		for (const FAgvObservedObstacle& Entry : SensorKit->GetObservedObstacles())
		{
			if (Entry.Actor.Get() == TargetPallet.Get())
			{
				Query.PerceptionIgnored.AddUnique(Entry.Actor.Get());
			}
		}
	}
	// Arrive straight along the goal's heading: a final turn on the spot would shift the vehicle sideways just before fork alignment.
	FTransform Goal = FinalGoal;
	TArray<FWarehouseRoutePoint> Tail;
	if (bStraightIn)
	{
		constexpr double TailCm = 120.0;
		const FVector Back = FinalGoal.GetUnitAxis(EAxis::X);
		bool bTailClear = true;
		for (double D = TailCm; D > 0.0; D -= 5.0)
		{
			FWarehouseRoutePoint Step;
			Step.Pose = FTransform(FinalGoal.GetRotation(), FinalGoal.GetLocation() - Back * (D - 5.0));
			bTailClear = bTailClear && NavigationClear(Step.Pose, &Query);
			Tail.Add(Step);
		}
		const FTransform PreGoal(FinalGoal.GetRotation(), FinalGoal.GetLocation() - Back * TailCm);
		if (bTailClear && NavigationClear(PreGoal, &Query))
		{
			Goal = PreGoal;
		}
		else
		{
			Tail.Reset();
		}
	}
	// A blocked goal has no route under any search. Say so at once: this is what the fleet's detour retries hit while another vehicle stands there.
	if (!NavigationClear(FinalGoal, &Query))
	{
		bHybridGoalBlocked = true;
		if (OutReport)
		{
			FString Names;
			const FBox Box = Query.Envelope.TransformBy(FinalGoal.ToMatrixWithScale());
			for (const FAgvObservedObstacle& Entry : SensorKit->GetObservedObstacles())
			{
				if (Entry.Actor.Get() != this && !Query.PerceptionIgnored.Contains(Entry.Actor.Get()) && Entry.WorldBounds.Intersect(Box))
				{
					Names += FString::Printf(TEXT("%s@%s "), *GetNameSafe(Entry.Actor.Get()), *Entry.WorldBounds.GetCenter().ToCompactString());
				}
			}
			*OutReport = FString::Printf(TEXT("hybrid goal blocked by: %s fleet_clear=%d"), *Names, !FleetCoordinator || FleetCoordinator->PlanningClear(this, Box));
		}
		return false;
	}
	const double StartedAt = FPlatformTime::Seconds();
	const double BaseZ = Start.GetLocation().Z;
	const FSpec Spec = bSupportingPallet ? FSpec{ FMath::Min(100.0, (double)LoadedTravelSpeedCm), 40.0, 0.3, 2.0, 4.0 } : FSpec{ FMath::Min(180.0, (double)EmptyTravelSpeedCm), 80.0, 0.4, 2.5, 5.0 };
	const FVector2D StartXY(Start.GetLocation()), GoalXY(Goal.GetLocation());
	const auto Ignored = [&](const FAgvObservedObstacle& Entry)
	{
		return Entry.Actor.Get() == this || Query.PerceptionIgnored.Contains(Entry.Actor.Get());
	};

	// The obstacle grid.
	const auto RelevantKnown = [&](const FAgvObservedObstacle& Entry)
	{
		if (!Entry.WorldBounds.IsValid || Ignored(Entry) || Entry.WorldBounds.Max.Z <= BaseZ + Query.Envelope.Min.Z || Entry.WorldBounds.Min.Z >= BaseZ + Query.Envelope.Max.Z)
		{
			return false;
		}
		const FVector Size = Entry.WorldBounds.GetSize();
		return Size.X * Size.Y < 400.0 * 10000.0; // a building-sized part is not a footprint; NavigationClear still tests its real shape
	};
	FBox2D Bounds(ForceInit);
	Bounds += StartXY;
	Bounds += GoalXY;
	for (const FAgvObservedObstacle& Entry : SensorKit->GetKnownObstacles())
	{
		if (RelevantKnown(Entry))
		{
			Bounds += FVector2D(Entry.WorldBounds.Min);
			Bounds += FVector2D(Entry.WorldBounds.Max);
		}
	}
	FGrid Field;
	Field.Origin = Bounds.Min - FVector2D(600.0);
	Field.Width = FMath::CeilToInt32((Bounds.Max.X - Bounds.Min.X + 1200.0) / CellCm);
	Field.Height = FMath::CeilToInt32((Bounds.Max.Y - Bounds.Min.Y + 1200.0) / CellCm);
	if ((int64)Field.Width * Field.Height > 2000000)
	{
		return false;
	}
	Field.Occupied.Init(0, Field.Width * Field.Height);
	for (const FAgvObservedObstacle& Entry : SensorKit->GetKnownObstacles())
	{
		if (RelevantKnown(Entry))
		{
			Rasterize(Field, Entry);
		}
	}
	for (const FAgvObservedObstacle& Entry : SensorKit->GetObservedObstacles())
	{
		if (Ignored(Entry))
		{
			continue;
		}
		for (const FVector& Point : Entry.WorldPoints)
		{
			int32 CX, CY;
			if (Point.Z > BaseZ + Query.Envelope.Min.Z && Point.Z < BaseZ + Query.Envelope.Max.Z && Field.Cell(Point.X, Point.Y, CX, CY))
			{
				Field.Occupied[Field.Index(CX, CY)] = 1;
			}
		}
	}
	// What the fleet's sensors saw earlier and no longer see: a pallet does not vanish because a rack hides it.
	constexpr double SeenForSeconds = 90.0;
	TArray<FBox2D> Remembered;
	if (FleetCoordinator && CVarHybridMemory.GetValueOnGameThread() != 0)
	{
		const double Now = GetWorld()->GetTimeSeconds();
		for (auto It = FleetCoordinator->SeenObstacles.CreateIterator(); It; ++It)
		{
			if (!It.Key().IsValid() || Now - It.Value().Seen > SeenForSeconds)
			{
				It.RemoveCurrent();
				continue;
			}
			const FBox& Box = It.Value().Bounds;
			if (Query.PerceptionIgnored.Contains(It.Key().Get()) || Box.Max.Z <= BaseZ + Query.Envelope.Min.Z || Box.Min.Z >= BaseZ + Query.Envelope.Max.Z)
			{
				continue;
			}
			// Already in the live scan: that copy is the current one.
			if (SensorKit->HasObservedActor(It.Key().Get()))
			{
				continue;
			}
			Remembered.Add(FBox2D(FVector2D(Box.Min), FVector2D(Box.Max)));
			int32 X0, Y0, X1, Y1;
			Field.Cell(Box.Min.X, Box.Min.Y, X0, Y0);
			Field.Cell(Box.Max.X, Box.Max.Y, X1, Y1);
			for (int32 Y = FMath::Max(0, Y0); Y <= FMath::Min(Field.Height - 1, Y1); ++Y)
			{
				for (int32 X = FMath::Max(0, X0); X <= FMath::Min(Field.Width - 1, X1); ++X)
				{
					Field.Occupied[Field.Index(X, Y)] = 1;
				}
			}
		}
	}
	// Receiving/job records do not reveal the current shape or pose of unseen freight.
	// Only published structures, measured points and the fleet's last observations enter the field.
	BuildClearance(Field);
	BuildGoalTime(Field, StartXY, GoalXY, Spec.Speed);
	const double BuildMs = (FPlatformTime::Seconds() - StartedAt) * 1000.0;

	// Only a positively recognized target supplies this cached catalog footprint.
	// Two rectangles in the plane: the vehicle body with 3cm to spare, not the 25cm travel margin, and the load's footprint.
	// The planning envelope carries the travel margin (25cm, or 10/3cm when a narrow aisle is retried): take it off to get the body.
	const double Margin = AIState == EWarehouseAIState::AlignVehicle ? 0.0 : PlanningClearanceCm;
	const FBox TargetEnvelope = Query.Envelope.ExpandBy(FVector(3.0 - Margin, 3.0 - Margin, 0.0));
	constexpr double TargetHalf = 95.0;
	// The follower stops for a person inside a box swept ahead of the vehicle, far wider than the collision envelope, so a route that
	// merely misses them would stop anyway. Keep well clear, but always allow moving away from where the vehicle stands now.
	TArray<FVector2D> People;
	for (const FAgvObservedObstacle& Entry : SensorKit->GetObservedObstacles())
	{
		if (Entry.bPerson && Entry.WorldBounds.IsValid)
		{
			People.Add(FVector2D(Entry.WorldBounds.GetCenter()));
		}
	}
	constexpr double PersonKeepOutCm = 300.0;
	const FVector2D BodyExtent(TargetEnvelope.GetExtent());
	const auto BodyOverlaps = [&](const FTransform& Pose, const FVector2D& Centre, const FVector2D& AxisX, const FVector2D& AxisY, const FVector2D& Half)
	{
		const FVector2D BodyCentre(Pose.TransformPosition(TargetEnvelope.GetCenter()));
		const FVector2D BodyX(Pose.GetUnitAxis(EAxis::X)), BodyY(Pose.GetUnitAxis(EAxis::Y));
		const FVector2D Between = Centre - BodyCentre;
		for (const FVector2D& Axis : { BodyX, BodyY, AxisX, AxisY })
		{
			const double Reach = BodyExtent.X * FMath::Abs(Axis | BodyX) + BodyExtent.Y * FMath::Abs(Axis | BodyY) +
				Half.X * FMath::Abs(Axis | AxisX) + Half.Y * FMath::Abs(Axis | AxisY);
			if (FMath::Abs(Between | Axis) > Reach)
			{
				return false;
			}
		}
		return true;
	};
	const auto HitsTarget = [&](const FTransform& Pose)
	{
		const FVector2D At(Pose.GetLocation());
		for (const FVector2D& Person : People)
		{
			const double Distance = FVector2D::Distance(At, Person);
			if (Distance < PersonKeepOutCm && Distance < FVector2D::Distance(StartXY, Person) - 5.0)
			{
				return true;
			}
		}
		for (const FBox2D& Box : Remembered)
		{
			if (BodyOverlaps(Pose, Box.GetCenter(), FVector2D(1.0, 0.0), FVector2D(0.0, 1.0), Box.GetExtent()))
			{
				return true;
			}
		}
		return bGuardTarget && BodyOverlaps(Pose, FVector2D(TargetPose.GetLocation()), FVector2D(TargetPose.GetUnitAxis(EAxis::X)), FVector2D(TargetPose.GetUnitAxis(EAxis::Y)), FVector2D(TargetHalf));
	};
	// A vehicle stopped close to something, as after a withdrawal, has to leave it: the first stretch only needs the real body clear.
	FWarehouseNavigationQuery EscapeQuery = Query;
	EscapeQuery.Envelope = Query.Envelope.ExpandBy(FVector(5.0 - Margin, 5.0 - Margin, 0.0));
	const bool bStartBlocked = !NavigationClear(Start, &Query);
	const bool bLoadedDeparture = bStartBlocked && bSupportingPallet &&
		(AIState==EWarehouseAIState::PlanDelivery || AIState==EWarehouseAIState::NavigateDelivery);
	constexpr double EscapeCm = FWarehouseRoutePoint::LoadedEscapeRadiusCm;
	const auto FullyClear = [&](const FTransform& Pose)
	{
		const bool bEscape = bStartBlocked && FVector2D::DistSquared(FVector2D(Pose.GetLocation()), StartXY) < EscapeCm * EscapeCm;
		if (bEscape && bLoadedDeparture)
		{
			return (!FleetCoordinator || FleetCoordinator->PlanningClear(this, Query.Envelope.TransformBy(Pose.ToMatrixWithScale()))) &&
				PhysicalPoseClear(Pose, true);
		}
		// The tip of a fork that stopped beside something can stay inside even the reduced box while backing off: then the real parts decide.
		return NavigationClear(Pose, bEscape ? &EscapeQuery : &Query) || (bEscape && PhysicalPoseClear(Pose, true));
	};

	// Collision: far from every obstacle only the floor and traffic need asking; otherwise the same query as the old planner.
	const double Circumradius = FVector2D(FMath::Max(FMath::Abs(Query.Envelope.Min.X), FMath::Abs(Query.Envelope.Max.X)),
		FMath::Max(FMath::Abs(Query.Envelope.Min.Y), FMath::Abs(Query.Envelope.Max.Y))).Size();
	const float FreeClearance = (float)(Circumradius * 1.1 + CellCm * 1.5);
	TMap<int64, bool> FloorCache;
	int32 NativeQueries = 0;
	const auto PoseClear = [&](double X, double Y, double Yaw)
	{
		const FTransform Pose(FRotator(0.0, Yaw, 0.0), FVector(X, Y, BaseZ));
		const bool bNearStart = FVector2D::DistSquared(FVector2D(X, Y), StartXY) < 60.0 * 60.0;
		if (!bNearStart && HitsTarget(Pose))
		{
			return false;
		}
		if (Field.ClearanceAt(X, Y) > FreeClearance)
		{
			if (FleetCoordinator && !FleetCoordinator->PlanningClear(this, Query.Envelope.TransformBy(Pose.ToMatrixWithScale())))
			{
				return false;
			}
			const int64 FloorKey = (int64(FMath::FloorToInt32(X / 50.0)) << 32) ^ int64(uint32(FMath::FloorToInt32(Y / 50.0)));
			if (const bool* Known = FloorCache.Find(FloorKey))
			{
				return *Known;
			}
			const bool bFloor = PerceivedFloorSupport(FVector(X, Y, BaseZ));
			FloorCache.Add(FloorKey, bFloor);
			return bFloor;
		}
		++NativeQueries;
		return FullyClear(Pose);
	};
	const auto SweepClear = [&](double X, double Y, double Yaw, double Curvature, double Signed)
	{
		const int32 Samples = FMath::Max(1, FMath::CeilToInt32(FMath::Abs(Signed) / 15.0));
		for (int32 I = 1; I <= Samples; ++I)
		{
			const double S = Signed * I / Samples;
			double PX, PY, PYaw;
			ArcPoint(X, Y, Yaw, Curvature, S, PX, PY, PYaw);
			if (!PoseClear(PX, PY, PYaw))
			{
				return false;
			}
		}
		return true;
	};
	// Curves come from the lane connector, which knows nothing of the target's footprint.
	const auto EdgeClearOfTarget = [&](const TArray<FWarehouseRoutePoint>& Edge)
	{
		for (const FWarehouseRoutePoint& Point : Edge)
		{
			if (FVector2D::DistSquared(FVector2D(Point.Pose.GetLocation()), StartXY) >= 60.0 * 60.0 && HitsTarget(Point.Pose))
			{
				return false;
			}
		}
		return true;
	};
	const auto Axle = [&](double X, double Y, double Yaw)
	{
		return FVector2D(X, Y) + FVector2D(FRotator(0.0, Yaw, 0.0).RotateVector(Query.Pivot));
	};

	int32 Expansions = 0, AnalyticTries = 0, NearAnalyticTries = 0, Searches = 0;
	double ClosestToGoal = TNumericLimits<float>::Max();
	FString FailReport;
	TArray<FNode> Nodes;
	TArray<FWarehouseRoutePoint> BestRoute;
	bool bValid = false;
	bool bTimedOut = false;
	for (; Searches < MaxSearches && !bValid && !bTimedOut; ++Searches)
	{
		Nodes.Reset();
		FNode Root;
		Root.X = StartXY.X; Root.Y = StartXY.Y; Root.Yaw = Start.Rotator().Yaw;
		Nodes.Add(Root);
		const auto Heuristic = [&](double X, double Y)
		{
			if (Field.bGoalTimeUsable)
			{
				int32 CX, CY;
				if (Field.Cell(X, Y, CX, CY) && Field.GoalTime[Field.Index(CX, CY)] < Unreached)
				{
					return (double)Field.GoalTime[Field.Index(CX, CY)];
				}
				return (double)Unreached;
			}
			return FVector2D::Distance(FVector2D(X, Y), GoalXY) / Spec.Speed;
		};
		TArray<FOpen> Open;
		Open.HeapPush({ (float)(SearchWeight * FVector2D::Distance(StartXY, GoalXY) / Spec.Speed), 0 });
		TMap<uint64, float> Best;
		TSet<uint64> Tried;
		double BestTotal = TNumericLimits<float>::Max();
		int32 BestNode = INDEX_NONE;
		TArray<FWarehouseRoutePoint> BestEdge;
		int32 ExpansionLimit = MaxExpansions;
		int32 Local = 0;
		const auto PoseOf = [&](const FNode& Node) { return FTransform(FRotator(0.0, Node.Yaw, 0.0), FVector(Node.X, Node.Y, BaseZ)); };
		while (Open.Num() && Local < ExpansionLimit)
		{
			FOpen Top;
			Open.HeapPop(Top, EAllowShrinking::No);
			const FNode Node = Nodes[Top.Node];
			if (BestNode != INDEX_NONE && Node.G + FVector2D::Distance(FVector2D(Node.X, Node.Y), GoalXY) / Spec.Speed >= BestTotal)
			{
				continue;
			}
			// Moves are checked here, when the search reaches them, not when they are proposed: most proposals are never reached,
			// and checking them all spent the whole query budget a few metres into a search (2026-10-31).
			if (Node.Parent != INDEX_NONE)
			{
				const FNode& From = Nodes[Node.Parent];
				bool bMoveClear = true;
				if (Node.Mode == MoveTurn)
				{
					const FVector2D Wheel = Axle(0.0, 0.0, From.Yaw) + FVector2D(From.X, From.Y);
					const int32 Samples = FMath::Max(1, FMath::CeilToInt32(FMath::Abs(Node.TurnAngle) / 5.0));
					for (int32 I = 1; I <= Samples && bMoveClear; ++I)
					{
						const double Yaw = From.Yaw + Node.TurnAngle * I / Samples;
						const FVector2D At = Wheel - Axle(0.0, 0.0, Yaw);
						bMoveClear = PoseClear(At.X, At.Y, Yaw);
					}
				}
				else
				{
					bMoveClear = SweepClear(From.X, From.Y, From.Yaw, Node.Curvature, (Node.Mode == MoveForward ? 1.0 : -1.0) * Node.Length);
				}
				if (!bMoveClear)
				{
					// Another way into the same cell may still be clear.
					Best.Remove(Key(Node.X, Node.Y, Node.Yaw, Node.Mode));
					continue;
				}
			}
			++Local;
			if ((Local & 255) == 0)
			{
				// Planning stalls the game thread. With a route in hand, stop improving it soon; without one, give it a little longer.
				// Counted in queries, not clock time, so that a run does not depend on what else the computer is doing.
				if (BestNode != INDEX_NONE && NativeQueries > MaxImproveQueries)
				{
					break;
				}
				if (NativeQueries > MaxSearchQueries)
				{
					bTimedOut = true;
					break;
				}
			}
			// Try to finish with a curve or turn-drive-turn onto the exact goal pose.
			const double ToGoal = FVector2D::Distance(FVector2D(Node.X, Node.Y), GoalXY);
			ClosestToGoal = FMath::Min(ClosestToGoal, ToGoal);
			// On the goal in all but the last few cm, with no room for a curve or reason to turn: the follower closes that gap.
			if (ToGoal < 15.0 && FMath::Abs(FMath::FindDeltaAngleDegrees(Node.Yaw, Goal.Rotator().Yaw)) < 10.0 && Node.G < BestTotal)
			{
				BestTotal = Node.G;
				BestNode = Top.Node;
				BestEdge.Reset();
				FWarehouseRoutePoint Last;
				Last.Pose = Goal;
				Last.bReverse = FVector::DotProduct(Goal.GetLocation() - FVector(Node.X, Node.Y, BaseZ), Goal.GetUnitAxis(EAxis::X)) < 0.0;
				BestEdge.Add(Last);
				break;
			}
			const bool bNearGoal = ToGoal < 400.0;
			if (ToGoal < AnalyticRangeCm && (bNearGoal ? NearAnalyticTries < MaxNearAnalyticTries : AnalyticTries < MaxAnalyticTries))
			{
				const double CoarseCell = bNearGoal ? 50.0 : 100.0, CoarseYaw = bNearGoal ? 15.0 : 45.0;
				const uint64 Coarse = (uint64(FMath::FloorToInt32(Node.X / CoarseCell) + 100000) << 32) | (uint64(FMath::FloorToInt32(Node.Y / CoarseCell) + 100000) << 8) |
					(uint64(FMath::RoundToInt32(FRotator::ClampAxis(Node.Yaw) / CoarseYaw)) & 31);
				if (!Tried.Contains(Coarse))
				{
					Tried.Add(Coarse);
					++(bNearGoal ? NearAnalyticTries : AnalyticTries);
					TArray<FWarehouseRoutePoint> Edge;
					float Unused;
					if (ConnectLanePoses(PoseOf(Node), Goal, Edge, Unused, &Query) && EdgeClearOfTarget(Edge))
					{
						const double Total = Node.G + RouteTimeSeconds(Edge, PoseOf(Node));
						if (Total < BestTotal)
						{
							if (BestNode == INDEX_NONE)
							{
								ExpansionLimit = FMath::Min(ExpansionLimit, Local + ExtraExpansionsAfterFound);
							}
							BestTotal = Total;
							BestNode = Top.Node;
							BestEdge = MoveTemp(Edge);
						}
					}
				}
			}
			const double Clearance = Field.ClearanceAt(Node.X, Node.Y);
			const auto Push = [&](double X, double Y, double Yaw, double Curvature, double Length, uint8 Mode, int8 TurnSign, double Cost, double TurnAngle = 0.0)
			{
				const double H = Heuristic(X, Y);
				if (H >= Unreached)
				{
					return;
				}
				const float G = (float)(Node.G + Cost);
				const uint64 NodeKey = Key(X, Y, Yaw, Mode);
				if (const float* Seen = Best.Find(NodeKey))
				{
					if (*Seen <= G)
					{
						return;
					}
				}
				Best.Add(NodeKey, G);
				FNode Child;
				Child.X = X; Child.Y = Y; Child.Yaw = Yaw; Child.G = G; Child.Parent = Top.Node;
				Child.Curvature = Curvature; Child.Length = Length; Child.Mode = Mode; Child.TurnSign = TurnSign; Child.TurnAngle = TurnAngle;
				const int32 Index = Nodes.Add(Child);
				Open.HeapPush({ (float)(G + SearchWeight * H), Index });
			};
			for (const uint8 Mode : { (uint8)MoveForward, (uint8)MoveReverse })
			{
				const double Sign = Mode == MoveForward ? 1.0 : -1.0;
				const double Change = (Node.Mode == MoveForward || Node.Mode == MoveReverse) && Node.Mode != Mode ? Spec.StopGo : 0.0;
				const double Step = Clearance > LongStepClearanceCm ? LongStepCm : StepCm;
				const double Theta = FMath::DegreesToRadians(Node.Yaw);
				Push(Node.X + Sign * Step * FMath::Cos(Theta), Node.Y + Sign * Step * FMath::Sin(Theta), Node.Yaw, 0.0, Step, Mode, 0, Change + Spec.Travel(Step, 0.0, Mode == MoveReverse));
				for (const double Radius : { ArcRadiusTight, ArcRadiusWide })
				{
					for (const double Side : { 1.0, -1.0 })
					{
						// Arcs end on a whole heading step, so straight runs line up with the warehouse aisles.
						const double NextYaw = Snap(Node.Yaw + Side * HeadingDeg);
						const double Sweep = FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(Node.Yaw, NextYaw));
						if (FMath::Abs(Sweep) < FMath::DegreesToRadians(3.0))
						{
							continue;
						}
						const double Length = FMath::Abs(Sweep) * Radius;
						const double Curvature = Sign * Sweep / Length;
						const double Signed = Sign * Length;
						const double Next = Theta + Curvature * Signed;
						Push(Node.X + (FMath::Sin(Next) - FMath::Sin(Theta)) / Curvature, Node.Y - (FMath::Cos(Next) - FMath::Cos(Theta)) / Curvature, NextYaw, Curvature, Length, Mode, 0,
							Change + Spec.Travel(Length, Curvature, Mode == MoveReverse));
					}
				}
			}
			for (const double Delta : { 15.0, -15.0, 45.0, -45.0, 90.0, -90.0 })
			{
				const int8 TurnSign = Delta > 0 ? 1 : -1;
				if (Node.Mode == MoveTurn && Node.TurnSign == -TurnSign)
				{
					continue;
				}
				const double NextYaw = Snap(Node.Yaw + Delta);
				const double Angle = FMath::FindDeltaAngleDegrees(Node.Yaw, NextYaw);
				if (FMath::Abs(Angle) < 3.0)
				{
					continue;
				}
				const FVector2D Wheel = Axle(0.0, 0.0, Node.Yaw) + FVector2D(Node.X, Node.Y);
				const FVector2D End = Wheel - Axle(0.0, 0.0, Node.Yaw + Angle);
				Push(End.X, End.Y, NextYaw, 0.0, 0.0, MoveTurn, TurnSign, FMath::DegreesToRadians(FMath::Abs(Angle)) / Spec.TurnRate + (Node.Mode == MoveTurn ? 0.0 : Spec.TurnEntry), Angle);
			}
		}
		Expansions += Local;
		if (BestNode == INDEX_NONE)
		{
			break;
		}

		// The node chain, start first.
		TArray<int32> Chain;
		for (int32 I = BestNode; I != INDEX_NONE; I = Nodes[I].Parent)
		{
			Chain.Add(I);
		}
		Algo::Reverse(Chain);

		// Build the route, replacing stretches of the lattice with a single smooth connection where that is faster. Heading
		// steps of 15 degrees cannot follow a diagonal: left as they are, they would make the vehicle slow for every little arc.
		TArray<FWarehouseRoutePoint> Route;
		int32 Shortcuts = 0;
		for (int32 At = 0; At + 1 < Chain.Num();)
		{
			const FNode& From = Nodes[Chain[At]];
			bool bJumped = false;
			for (int32 Reach = FMath::Min(Chain.Num() - 1 - At, 64); Reach >= 4 && Shortcuts < 30; Reach /= 2)
			{
				const FNode& To = Nodes[Chain[At + Reach]];
				++Shortcuts;
				TArray<FWarehouseRoutePoint> Edge;
				float Unused;
				if (ConnectLanePoses(PoseOf(From), PoseOf(To), Edge, Unused, &Query) && EdgeClearOfTarget(Edge) && RouteTimeSeconds(Edge, PoseOf(From)) + 0.05 < To.G - From.G)
				{
					Route.Append(Edge);
					At += Reach;
					bJumped = true;
					break;
				}
			}
			if (bJumped)
			{
				continue;
			}
			const FNode& To = Nodes[Chain[At + 1]];
			if (To.Mode == MoveTurn)
			{
				TArray<FWarehouseRoutePoint> Edge;
				float Unused;
				if (!ConnectPoses(PoseOf(From), PoseOf(To), Edge, Unused, &Query))
				{
					FWarehouseRoutePoint Step;
					Step.Pose = PoseOf(To);
					Step.bPivot = true;
					Route.Add(Step);
				}
				else
				{
					Route.Append(Edge);
				}
			}
			else
			{
				const double Sign = To.Mode == MoveForward ? 1.0 : -1.0;
				const double Theta = FMath::DegreesToRadians(From.Yaw);
				const int32 Samples = FMath::Max(1, FMath::CeilToInt32(To.Length / 5.0));
				for (int32 I = 1; I <= Samples; ++I)
				{
					const double S = Sign * To.Length * I / Samples;
					FWarehouseRoutePoint Step;
					Step.bReverse = To.Mode == MoveReverse;
					if (FMath::Abs(To.Curvature) < 1e-6)
					{
						Step.Pose = FTransform(FRotator(0.0, From.Yaw, 0.0), FVector(From.X + S * FMath::Cos(Theta), From.Y + S * FMath::Sin(Theta), BaseZ));
					}
					else
					{
						const double Next = Theta + To.Curvature * S;
						Step.Pose = FTransform(FRotator(0.0, FMath::RadiansToDegrees(Next), 0.0),
							FVector(From.X + (FMath::Sin(Next) - FMath::Sin(Theta)) / To.Curvature, From.Y - (FMath::Cos(Next) - FMath::Cos(Theta)) / To.Curvature, BaseZ));
					}
					Route.Add(Step);
				}
			}
			++At;
		}
		Route.Append(BestEdge);
		Route.Append(Tail);
		// One turn, not a stop after every heading step: the follower settles after each and 60 degrees in steps took 20s.
		for (int32 I = Route.Num() - 1; I > 0; --I)
		{
			if (Route[I].bPivot && Route[I - 1].bPivot)
			{
				Route.RemoveAt(I - 1);
			}
		}
		// Stopping for a few degrees costs about 6s; path tracking takes up the same error while driving.
		for (int32 I = Route.Num() - 2; I >= 0; --I)
		{
			if (Route[I].bPivot && !Route[I + 1].bPivot)
			{
				const FTransform& Before = I > 0 ? Route[I - 1].Pose : Start;
				if (FMath::Abs(FMath::FindDeltaAngleDegrees(Before.Rotator().Yaw, Route[I].Pose.Rotator().Yaw)) < 12.0)
				{
					Route.RemoveAt(I);
				}
			}
		}

		// Every point meets the full query, whatever the cheap check assumed. A blocked point goes into the grid for the next search.
		bValid = true;
		TBitArray<> Checked(false, Route.Num());
		for (int32 Stride = 32; Stride >= 1 && bValid; Stride /= 2)
		{
			for (int32 I = Stride - 1; I < Route.Num() && bValid; I += Stride)
			{
				if (!Checked[I])
				{
					Checked[I] = true;
					const FTransform& Pose = Route[I].Pose;
					const bool bNearStart = FVector2D::DistSquared(FVector2D(Pose.GetLocation()), StartXY) < 60.0 * 60.0;
					if (!FullyClear(Pose) || (!bNearStart && HitsTarget(Pose)))
					{
						bValid = false;
						FailReport += FString::Printf(TEXT(" fail[%d/%d pivot=%d rev=%d pose=(%.0f,%.0f,%.0f) full=%d target=%d chain=%d edge=%d]"), I, Route.Num(), Route[I].bPivot, Route[I].bReverse,
							Pose.GetLocation().X, Pose.GetLocation().Y, Pose.Rotator().Yaw, FullyClear(Pose), HitsTarget(Pose), Chain.Num(), BestEdge.Num());
						int32 CX, CY;
						if (Field.Cell(Pose.GetLocation().X, Pose.GetLocation().Y, CX, CY))
						{
							// Something the grid does not hold is here. Ask the full query all around it from now on.
							const int32 Reach = FMath::CeilToInt32((FreeClearance + CellCm) / CellCm);
							for (int32 NY = FMath::Max(0, CY - Reach); NY <= FMath::Min(Field.Height - 1, CY + Reach); ++NY)
							{
								for (int32 NX = FMath::Max(0, CX - Reach); NX <= FMath::Min(Field.Width - 1, CX + Reach); ++NX)
								{
									Field.Clearance[Field.Index(NX, NY)] = 0.f;
								}
							}
						}
					}
				}
			}
		}
		if (bValid)
		{
			BestRoute = MoveTemp(Route);
		}
	}
	const double TotalMs = (FPlatformTime::Seconds() - StartedAt) * 1000.0;
	if (OutReport)
	{
		*OutReport = FString::Printf(TEXT("hybrid_ms=%.0f grid_ms=%.0f expansions=%d native=%d analytic=%d/%d searches=%d timeout=%d reachable=%d start_blocked=%d straight=%d goal_clear=%d closest=%.0f%s"),
			TotalMs, BuildMs, Expansions, NativeQueries, AnalyticTries, NearAnalyticTries, Searches, bTimedOut, Field.bGoalTimeUsable, bStartBlocked, Tail.Num() > 0, NavigationClear(Goal, &Query), ClosestToGoal, *FailReport);
	}
	if (!bValid)
	{
		return false;
	}
	OutRoute = MoveTemp(BestRoute);
	if (bLoadedDeparture && !OutRoute.IsEmpty()) OutRoute[0].LoadedEscapeOrigin=Start.GetLocation();
	return true;
}
