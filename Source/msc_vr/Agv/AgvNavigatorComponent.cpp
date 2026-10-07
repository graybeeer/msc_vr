#include "AgvNavigatorComponent.h"
#include "AgvDriveComponent.h"
#include "AgvLocalizerComponent.h"
#include "AgvRouteGraph.h"
#include "AgvRouteNode.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogAgvNav, Log, All);

namespace
{
	const double ProfileStepCm = 5.0;
	const double CreepSpeedCm = 3.0;
	const double PivotGainPerSecond = 3.0;

	// Usage in the console: agv.goto <NodeId>
	FAutoConsoleCommandWithWorldAndArgs GoToCommand(
		TEXT("agv.goto"),
		TEXT("Send the first AGV navigator in the world to a route node: agv.goto <NodeId>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World || Args.IsEmpty())
			{
				return;
			}
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (UAgvNavigatorComponent* Navigator = It->FindComponentByClass<UAgvNavigatorComponent>())
				{
					Navigator->GoToNode(FName(*Args[0]));
					return;
				}
			}
		}));
}

UAgvNavigatorComponent::UAgvNavigatorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UAgvNavigatorComponent::ResolveParts()
{
	if (!Drive || !Localizer)
	{
		Drive = GetOwner()->FindComponentByClass<UAgvDriveComponent>();
		Localizer = GetOwner()->FindComponentByClass<UAgvLocalizerComponent>();
	}
	return Drive && Localizer;
}

void UAgvNavigatorComponent::Finish(EAgvNavState NewState, const FString& Message)
{
	State = NewState;
	StatusText = Message;
	CommandSpeed = 0.0;
	if (Drive)
	{
		Drive->SetCommand(0.0, 0.0);
	}
	UE_LOG(LogAgvNav, Log, TEXT("AGV_NAV %s: %s"), *GetOwner()->GetName(), *Message);
}

void UAgvNavigatorComponent::Cancel()
{
	Path = FAgvPath();
	PlannedNodeIds.Reset();
	Finish(EAgvNavState::Idle, TEXT("IDLE"));
}

bool UAgvNavigatorComponent::GoToNode(FName NodeId)
{
	FVector2D Position;
	double Yaw;
	if (!ResolveParts() || !Localizer->GetPose(Position, Yaw))
	{
		Finish(EAgvNavState::Fault, TEXT("LOCALIZATION LOST"));
		return false;
	}
	FAgvRoute Route;
	FString Error;
	if (!AgvRouteGraph::FindRoute(GetWorld(), Position, NodeId, NetworkSnapCm, Route, Error))
	{
		// A rejected order leaves the current state alone apart from the message.
		StatusText = Error;
		UE_LOG(LogAgvNav, Warning, TEXT("AGV_NAV %s: %s"), *GetOwner()->GetName(), *Error);
		return false;
	}

	TArray<FVector2D> Points{ Route.Start };
	TArray<double> Radii{ 0.0 };
	PlannedNodeIds.Reset();
	for (const AAgvRouteNode* Node : Route.Nodes)
	{
		Points.Add(Node->GetPoint());
		Radii.Add(Node->CornerRadiusCm < 0.f ? DefaultCornerRadiusCm : Node->CornerRadiusCm);
		PlannedNodeIds.Add(Node->NodeId);
	}
	FAgvPath::Build(Points, Radii, MinCornerRadiusCm, bDriveReversed, Path);
	const AAgvRouteNode* Goal = Route.Nodes.Last();
	Path.bHasFinalYaw = Goal->bAlignOnArrival;
	Path.FinalYaw = FMath::DegreesToRadians(Goal->GetActorRotation().Yaw);
	return StartPath();
}

bool UAgvNavigatorComponent::FollowPoints(const TArray<FVector>& Points, float CornerRadiusCm)
{
	if (!ResolveParts())
	{
		return false;
	}
	TArray<FVector2D> Flat;
	TArray<double> Radii;
	for (const FVector& Point : Points)
	{
		Flat.Add(FVector2D(Point));
		Radii.Add(CornerRadiusCm);
	}
	PlannedNodeIds.Reset();
	FAgvPath::Build(Flat, Radii, MinCornerRadiusCm, bDriveReversed, Path);
	return StartPath();
}

bool UAgvNavigatorComponent::StartPath()
{
	FVector2D Position;
	double Yaw;
	if (bChooseLegDirection && Localizer && Localizer->GetPose(Position, Yaw))
	{
		ChooseLegDirections(Position, Yaw);
	}
	LegIndex = 0;
	MaxTrueCrossTrackErrorCm = TrueCrossTrackErrorCm = CrossTrackErrorCm = 0.f;
	bDeviationWarning = false;
	BeginLeg();
	return true;
}

double UAgvNavigatorComponent::TravelMaxSpeed() const
{
	return Drive && Drive->GetPayloadKg() > LoadedThresholdKg ? FMath::Min(MaxSpeedCm, LoadedMaxSpeedCm) : (double)MaxSpeedCm;
}

void UAgvNavigatorComponent::ChooseLegDirections(FVector2D Position, double Yaw)
{
	// Shortest time over both directions of every leg (dynamic programming over the leg sequence): Best[d] is the time
	// to finish the legs so far with the last one driven in direction d (1 = local -X leads). A turn on the spot that
	// has no room is not allowed; if nothing is allowed, the room check is dropped (the safety fields still guard).
	const int32 Count = Path.Legs.Num();
	if (Count == 0)
	{
		return;
	}
	const double Rate = FMath::DegreesToRadians(FMath::Max(1.0, (double)MaxYawRateDeg));
	const double CheckMin = FMath::DegreesToRadians((double)PivotCheckMinDeg);
	const double Accel = FMath::Max(1.0, (double)AccelCm);
	const double Infinity = TNumericLimits<double>::Max();
	auto TurnCost = [&](const FVector2D& At, double From, double To, bool bCheckRoom)
	{
		const double Angle = FMath::Abs(AgvMath::Wrap(To - From));
		if (Angle <= CheckMin)
		{
			return Angle / Rate;
		}
		return bCheckRoom && !CanPivot(At, From, To) ? Infinity : Angle / Rate + PivotOverheadSeconds;
	};
	auto Solve = [&](bool bCheckRoom, TArray<uint8>& OutChoice)
	{
		TArray<TStaticArray<double, 2>> Best;
		TArray<TStaticArray<uint8, 2>> From;
		Best.SetNum(Count);
		From.SetNum(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FAgvLeg& Leg = Path.Legs[Index];
			const double Start = Leg.Segments[0].StartHeading;
			const double End = Leg.Sample(Leg.Length).Heading;
			for (int32 Dir = 0; Dir < 2; ++Dir)
			{
				const double Speed = Dir ? TravelMaxSpeed() : FMath::Min((double)ForksFirstSpeedCm, TravelMaxSpeed());
				double Travel = Leg.Length / Speed + Speed / Accel;
				const double LegYaw = Start + (Dir ? UE_DOUBLE_PI : 0.0);
				if (Index == Count - 1 && Path.bHasFinalYaw)
				{
					const FVector2D EndPoint = Leg.Sample(Leg.Length).Position;
					const double Final = TurnCost(EndPoint, End + (Dir ? UE_DOUBLE_PI : 0.0), Path.FinalYaw, bCheckRoom);
					Travel = Final == Infinity ? Infinity : Travel + Final;
				}
				Best[Index][Dir] = Infinity;
				From[Index][Dir] = 0;
				for (int32 Prev = 0; Prev < (Index == 0 ? 1 : 2); ++Prev)
				{
					const double PrevYaw = Index == 0 ? Yaw : Path.Legs[Index - 1].Sample(Path.Legs[Index - 1].Length).Heading + (Prev ? UE_DOUBLE_PI : 0.0);
					const double Before = Index == 0 ? 0.0 : Best[Index - 1][Prev];
					if (Before == Infinity || Travel == Infinity)
					{
						continue;
					}
					const double Turn = TurnCost(Leg.Segments[0].Start, PrevYaw, LegYaw, bCheckRoom);
					// Ties keep bDriveReversed.
					const double Total = Turn == Infinity ? Infinity : Before + Turn + Travel - (Dir == (bDriveReversed ? 1 : 0) ? 1e-3 : 0.0);
					if (Total < Best[Index][Dir])
					{
						Best[Index][Dir] = Total;
						From[Index][Dir] = (uint8)Prev;
					}
				}
			}
		}
		uint8 Dir = Best[Count - 1][1] < Best[Count - 1][0] ? 1 : 0;
		if (Best[Count - 1][Dir] == Infinity)
		{
			return false;
		}
		OutChoice.SetNum(Count);
		for (int32 Index = Count - 1; Index >= 0; --Index)
		{
			OutChoice[Index] = Dir;
			Dir = From[Index][Dir];
		}
		return true;
	};
	TArray<uint8> Choice;
	if (!Solve(PivotFootprint.Num() > 0, Choice) && !Solve(false, Choice))
	{
		return;
	}
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Path.Legs[Index].bReverse = Choice[Index] != 0;
	}
}

bool UAgvNavigatorComponent::CanPivot(const FVector2D& Position, double FromYaw, double ToYaw) const
{
	const UWorld* World = GetWorld();
	if (!World || PivotFootprint.IsEmpty() || !Drive)
	{
		return true;
	}
	// The footprint turns about the reference point; check it every few degrees along the turn (the shortest way, as
	// StepPivot turns). Only solid things count (as for the LiDAR: overlap-only triggers are not obstacles); people are
	// not in the query (they move; the rotation field guards them).
	const double Sweep = AgvMath::Wrap(ToYaw - FromYaw);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt32(FMath::Abs(Sweep) / FMath::DegreesToRadians(5.0)));
	const double Z = GetOwner()->GetActorLocation().Z + 0.5 * (PivotCheckHeightCm.X + PivotCheckHeightCm.Y);
	const double HalfHeight = 0.5 * (PivotCheckHeightCm.Y - PivotCheckHeightCm.X);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(AgvPivotRoom), false, GetOwner());
	TArray<FOverlapResult> Overlaps;
	for (int32 I = 0; I <= Steps; ++I)
	{
		const double Yaw = FromYaw + Sweep * I / Steps;
		const FVector2D Origin = Position - AgvMath::Rotate(Yaw, FVector2D(Drive->ReferenceOffsetCm, 0.0));
		const FQuat Rotation(FVector::UpVector, Yaw);
		for (const FBox2D& Box : PivotFootprint)
		{
			const FVector2D Center = Origin + AgvMath::Rotate(Yaw, Box.GetCenter());
			const FVector2D Half = Box.GetExtent() + FVector2D(PivotClearanceCm, PivotClearanceCm);
			Overlaps.Reset();
			World->OverlapMultiByObjectType(Overlaps, FVector(Center, Z), Rotation, Objects,
				FCollisionShape::MakeBox(FVector(Half, HalfHeight)), Query);
			for (const FOverlapResult& Overlap : Overlaps)
			{
				const UPrimitiveComponent* Component = Overlap.GetComponent();
				for (const ECollisionChannel Channel : { ECC_WorldStatic, ECC_WorldDynamic, ECC_Pawn, ECC_PhysicsBody })
				{
					if (Component && Component->GetCollisionResponseToChannel(Channel) == ECR_Block)
					{
						return false;
					}
				}
			}
		}
	}
	return true;
}

void UAgvNavigatorComponent::BeginLeg()
{
	CommandSpeed = 0.0;
	if (Path.Legs.IsValidIndex(LegIndex))
	{
		const FAgvLeg& Leg = Path.Legs[LegIndex];
		PivotTargetYaw = Leg.Segments[0].StartHeading + (Leg.bReverse ? UE_DOUBLE_PI : 0.0);
		bFinalPivot = false;
		State = EAgvNavState::Pivot;
		StatusText = TEXT("ALIGN TO PATH");
	}
	else if (Path.bHasFinalYaw)
	{
		PivotTargetYaw = Path.FinalYaw;
		bFinalPivot = true;
		State = EAgvNavState::Pivot;
		StatusText = TEXT("ALIGN AT DESTINATION");
	}
	else
	{
		Finish(EAgvNavState::Arrived, TEXT("ARRIVED"));
	}
}

void UAgvNavigatorComponent::BeginFollow()
{
	const FAgvLeg& Leg = Path.Legs[LegIndex];
	LegS = 0.0;
	CommandSpeed = 0.0;

	// Backward pass: never enter a point faster than braking to the next limit (or the stop) allows.
	const int32 Count = FMath::CeilToInt32(Leg.Length / ProfileStepCm) + 1;
	SpeedProfile.SetNum(Count);
	SpeedProfile[Count - 1] = 0.0;
	for (int32 Index = Count - 2; Index >= 0; --Index)
	{
		const double Curvature = FMath::Abs(Leg.Sample(Index * ProfileStepCm).Curvature);
		const double Limit = Curvature > UE_DOUBLE_SMALL_NUMBER ? FMath::Min(TravelMaxSpeed(), FMath::Sqrt(MaxLateralAccelCm / Curvature)) : TravelMaxSpeed();
		const double Braking = FMath::Sqrt(FMath::Square(SpeedProfile[Index + 1]) + 2.0 * DecelCm * ProfileStepCm);
		SpeedProfile[Index] = FMath::Min(Limit, Braking);
	}
	State = EAgvNavState::Follow;
	StatusText = TEXT("FOLLOW PATH");
}

bool UAgvNavigatorComponent::GetPathAhead(double DistanceCm, double StepCm, TArray<FVector>& OutPoses) const
{
	OutPoses.Reset();
	if (State != EAgvNavState::Follow || !Path.Legs.IsValidIndex(LegIndex) || StepCm <= 0.0)
	{
		return false;
	}
	const FAgvLeg& Leg = Path.Legs[LegIndex];
	const double End = FMath::Min(Leg.Length, LegS + DistanceCm);
	for (double S = LegS;; S += StepCm)
	{
		const FAgvPathSample Sample = Leg.Sample(FMath::Min(S, End));
		OutPoses.Add(FVector(Sample.Position, Sample.Heading + (Leg.bReverse ? UE_DOUBLE_PI : 0.0)));
		if (S >= End)
		{
			break;
		}
	}
	return true;
}

double UAgvNavigatorComponent::ProfileSpeed(double S) const
{
	const double Scaled = FMath::Clamp(S / ProfileStepCm, 0.0, double(SpeedProfile.Num() - 1));
	const int32 Index = FMath::Min(FMath::FloorToInt32(Scaled), SpeedProfile.Num() - 2);
	const double Speed = FMath::Lerp(SpeedProfile[Index], SpeedProfile[Index + 1], Scaled - Index);
	// The profile reaches zero exactly at the end; keep creeping until the stop tolerance is met.
	return FMath::Max(Speed, CreepSpeedCm);
}

void UAgvNavigatorComponent::Step(float Dt)
{
	if (!IsNavigating() || Dt <= 0.f || !ResolveParts())
	{
		return;
	}
	FVector2D Position;
	double Yaw;
	if (!Localizer->GetPose(Position, Yaw))
	{
		Finish(EAgvNavState::Fault, TEXT("LOCALIZATION LOST"));
		return;
	}
	if (State == EAgvNavState::Pivot)
	{
		StepPivot(Yaw);
	}
	else
	{
		StepFollow(Position, Yaw, Dt);
	}
	if (bDrawDebug)
	{
		DrawPath();
	}
}

void UAgvNavigatorComponent::StepPivot(double Yaw)
{
	const double ErrorDeg = FMath::RadiansToDegrees(AgvMath::Wrap(PivotTargetYaw - Yaw));
	HeadingErrorDeg = (float)ErrorDeg;
	if (FMath::Abs(ErrorDeg) <= HeadingToleranceDeg && FMath::Abs(Drive->YawRateDegS) < 1.f)
	{
		Drive->SetCommand(0.0, 0.0);
		if (bFinalPivot)
		{
			Finish(EAgvNavState::Arrived, TEXT("ARRIVED"));
		}
		else
		{
			BeginFollow();
		}
		return;
	}
	Drive->SetCommand(0.0, FMath::Clamp(ErrorDeg * PivotGainPerSecond, -(double)MaxYawRateDeg, (double)MaxYawRateDeg));
}

void UAgvNavigatorComponent::StepFollow(const FVector2D& Position, double Yaw, float Dt)
{
	const FAgvLeg& Leg = Path.Legs[LegIndex];
	LegS = Leg.Project(Position, LegS, 30.0, 150.0);
	const FAgvPathSample Target = Leg.Sample(LegS);

	// Errors in the path frame: sideways offset and the angle between travel direction and path tangent.
	const double Lateral = FVector2D::DotProduct(Position - Target.Position, AgvMath::Normal(Target.Heading));
	const double Heading = AgvMath::Wrap(Yaw + (Leg.bReverse ? UE_DOUBLE_PI : 0.0) - Target.Heading);
	CrossTrackErrorCm = (float)Lateral;
	HeadingErrorDeg = (float)FMath::RadiansToDegrees(Heading);
	bDeviationWarning = FMath::Abs(Lateral) > DeviationWarnCm;

	const double Remaining = Leg.Length - LegS;
	RemainingCm = (float)Remaining;
	for (int32 Index = LegIndex + 1; Index < Path.Legs.Num(); ++Index)
	{
		RemainingCm += (float)Path.Legs[Index].Length;
	}

	FVector2D TruePosition;
	double TrueYaw;
	Drive->GetTruePose(TruePosition, TrueYaw);
	const FAgvPathSample TrueTarget = Leg.Sample(Leg.Project(TruePosition, LegS, 100.0, 150.0));
	TrueCrossTrackErrorCm = (float)FVector2D::DotProduct(TruePosition - TrueTarget.Position, AgvMath::Normal(TrueTarget.Heading));
	MaxTrueCrossTrackErrorCm = FMath::Max(MaxTrueCrossTrackErrorCm, FMath::Abs(TrueCrossTrackErrorCm));

	if (FMath::Abs(Lateral) > DeviationStopCm)
	{
		Finish(EAgvNavState::Fault, TEXT("PATH DEVIATION"));
		return;
	}
	if (Remaining <= StopToleranceCm)
	{
		CommandSpeed = 0.0;
		Drive->SetCommand(0.0, 0.0);
		if (FMath::Abs(Drive->SpeedCmS) < 0.5f)
		{
			++LegIndex;
			BeginLeg();
		}
		return;
	}

	// Slow down while pointing away from the path, then ramp up no faster than the acceleration limit.
	// The safety system's cap (obstacle in a field) also holds the ramp, so the restart is smooth.
	// TravelMaxSpeed: a load picked up after the leg was planned still slows the vehicle.
	const double Allowed = FMath::Min3(ProfileSpeed(LegS) * FMath::Clamp(FMath::Cos(Heading), 0.2, 1.0), (double)Drive->SafetySpeedLimitCmS, TravelMaxSpeed());
	CommandSpeed = FMath::Min(Allowed, CommandSpeed + AccelCm * Dt);

	// Path curvature (read slightly ahead) plus a critically damped correction: both errors decay over
	// ConvergenceLengthCm of travel.
	const double Length = ConvergenceLengthCm;
	const double MaxCurvature = 1.0 / MinTrackingRadiusCm;
	const double Preview = FMath::Min(Leg.Length, LegS + CommandSpeed * CurvaturePreviewSeconds);
	const double Curvature = FMath::Clamp(Leg.Sample(Preview).Curvature - Lateral / (Length * Length) - 2.0 / Length * FMath::Sin(Heading), -MaxCurvature, MaxCurvature);
	double YawRate = FMath::RadiansToDegrees(CommandSpeed * Curvature);
	if (FMath::Abs(YawRate) > MaxYawRateDeg)
	{
		CommandSpeed = FMath::DegreesToRadians((double)MaxYawRateDeg) / FMath::Abs(Curvature);
		YawRate = FMath::Sign(YawRate) * MaxYawRateDeg;
	}
	Drive->SetCommand(Leg.bReverse ? -CommandSpeed : CommandSpeed, YawRate);
}

void UAgvNavigatorComponent::DrawPath() const
{
	const UWorld* World = GetWorld();
	const double Z = GetOwner()->GetActorLocation().Z + 4.0;
	for (int32 Index = LegIndex; Index < Path.Legs.Num(); ++Index)
	{
		const FAgvLeg& Leg = Path.Legs[Index];
		FVector Previous(Leg.Sample(0.0).Position, Z);
		for (double S = 10.0; S < Leg.Length + 10.0; S += 10.0)
		{
			const FVector Point(Leg.Sample(S).Position, Z);
			DrawDebugLine(World, Previous, Point, bDeviationWarning ? FColor::Orange : FColor::Green, false, -1.f, 0, 3.f);
			Previous = Point;
		}
	}
	FVector2D Position;
	double Yaw;
	Drive->GetTruePose(Position, Yaw);
	DrawDebugSphere(World, FVector(Position, Z), 6.f, 8, FColor::Yellow);
}
