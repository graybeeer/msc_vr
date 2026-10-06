#include "AgvSafetyComponent.h"
#include "AgvDriveComponent.h"
#include "AgvLidarComponent.h"
#include "AgvLocalizerComponent.h"
#include "AgvNavigatorComponent.h"
#include "AgvPath.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"

DEFINE_LOG_CATEGORY_STATIC(LogAgvSafety, Log, All);

UAgvSafetyComponent::UAgvSafetyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UAgvSafetyComponent::Step(float Dt)
{
	if (!Drive)
	{
		Drive = GetOwner()->FindComponentByClass<UAgvDriveComponent>();
		Navigator = GetOwner()->FindComponentByClass<UAgvNavigatorComponent>();
		Localizer = GetOwner()->FindComponentByClass<UAgvLocalizerComponent>();
	}
	if (!Drive)
	{
		return;
	}
	if (!bEnabled)
	{
		State = EAgvSafetyState::Clear;
		StatusText = TEXT("DISABLED");
		Drive->SetSafetySpeedLimit(TNumericLimits<float>::Max());
		return;
	}
	PendingPoints.SetNum(Scanners.Num());
	LatestPoints.SetNum(Scanners.Num());
	SeenRevolutions.SetNum(Scanners.Num());

	// Collect each sensor's points in the vehicle frame; a completed scan replaces that sensor's previous one.
	const FTransform Actor = GetOwner()->GetActorTransform();
	bool bNewScan = false;
	TArray<FVector> Local;
	for (int32 Index = 0; Index < Scanners.Num(); ++Index)
	{
		UAgvLidarComponent* Scanner = Scanners[Index];
		if (!Scanner)
		{
			continue;
		}
		const FTransform Mount = Scanner->GetComponentTransform().GetRelativeTransform(Actor);
		Local.Reset();
		Scanner->TakePoints(Local);
		for (const FVector& Point : Local)
		{
			const FVector Vehicle = Mount.TransformPosition(Point);
			if (Vehicle.Z >= MinObstacleHeightCm && Vehicle.Z <= MaxObstacleHeightCm)
			{
				PendingPoints[Index].Add(Vehicle);
			}
		}
		if (Scanner->GetRevolutionCount() != SeenRevolutions[Index])
		{
			SeenRevolutions[Index] = Scanner->GetRevolutionCount();
			LatestPoints[Index] = MoveTemp(PendingPoints[Index]);
			PendingPoints[Index].Reset();
			bNewScan = true;
		}
	}

	BuildSweep();
	if (bNewScan)
	{
		Evaluate();
	}

	// Stop latches until the protective field has stayed clear for the restart delay.
	ClearSeconds = bProtectiveHit ? 0.0 : ClearSeconds + Dt;
	WarningClearSeconds = bWarningHit ? 0.0 : WarningClearSeconds + Dt;
	const bool bWarning = bWarningHit || WarningClearSeconds < WarningReleaseSeconds;
	const EAgvSafetyState Previous = State;
	if (bProtectiveHit)
	{
		State = EAgvSafetyState::Stop;
	}
	else if (State != EAgvSafetyState::Stop || ClearSeconds >= RestartDelaySeconds)
	{
		State = bWarning ? EAgvSafetyState::Warning : EAgvSafetyState::Clear;
	}
	if (State != Previous)
	{
		SafetyStops += State == EAgvSafetyState::Stop;
		UE_LOG(LogAgvSafety, Log, TEXT("AGV_SAFETY %s: %s (%s, speed %.0f cm/s, protective %.0f, nearest %.0f)"), *GetOwner()->GetName(),
			State == EAgvSafetyState::Stop ? TEXT("STOP") : State == EAgvSafetyState::Warning ? TEXT("WARNING") : TEXT("CLEAR"),
			*FieldSet, FMath::Abs(Drive->SpeedCmS), ProtectiveLengthCm, NearestObjectCm);
	}
	StatusText = State == EAgvSafetyState::Stop ? TEXT("SAFETY STOP") : State == EAgvSafetyState::Warning ? TEXT("WARNING FIELD") : TEXT("CLEAR");

	// Warning: slow down to the warning speed at the field's design deceleration (no jolt); stop: at once.
	if (State == EAgvSafetyState::Warning)
	{
		WarningRampCm = FMath::Max((double)WarningSpeedCm, FMath::Min(WarningRampCm, (double)FMath::Abs(Drive->SpeedCmS)) - FieldDecelerationCm * Dt);
	}
	else
	{
		WarningRampCm = TNumericLimits<float>::Max();
	}
	float Limit = State == EAgvSafetyState::Stop ? 0.f : State == EAgvSafetyState::Warning ? (float)WarningRampCm : TNumericLimits<float>::Max();
	if (FieldSet == TEXT("FORKS FIRST"))
	{
		Limit = FMath::Min(Limit, ForksFirstMaxSpeedCm);
	}
	Drive->SetSafetySpeedLimit(Limit);

	if (bDrawFields)
	{
		DrawFields();
	}
}

void UAgvSafetyComponent::BuildSweep()
{
	// Field switching on the intended motion: the controller's command picks the direction (so the right field is
	// active before the vehicle starts), the actual speed picks the band, the steering encoder picks the curve.
	// With no motion requested the last field set stays (a stopped vehicle still guards where it is about to go).
	const double Speed = Drive->SpeedCmS;
	const double Command = Drive->GetCommandSpeedCmS();
	const double Turn = Drive->GetCommandYawRateDegS();
	if (FMath::Abs(Speed) > 2.0 || FMath::Abs(Command) > 0.5)
	{
		FieldSet = (FMath::Abs(Speed) > 2.0 ? Speed : Command) > 0.0 ? TEXT("FORKS FIRST") : TEXT("BODY FIRST");
	}
	else if (FMath::Abs(Turn) > 0.5)
	{
		FieldSet = Turn > 0.0 ? TEXT("ROTATE LEFT") : TEXT("ROTATE RIGHT");
	}
	bRotating = FieldSet.StartsWith(TEXT("ROTATE"));

	const UAgvTricycleDriveComponent* Tricycle = Cast<UAgvTricycleDriveComponent>(Drive);
	ReferenceX = Drive->ReferenceOffsetCm;
	const double Band = FMath::CeilToDouble(FMath::Max(FMath::Abs(Speed), (double)StartSpeedCm) / SpeedBandCm) * SpeedBandCm;
	ProtectiveLengthCm = (float)FMath::Max(Band * ResponseSeconds + Band * Band / (2.0 * FieldDecelerationCm) + FieldMarginCm, (double)MinProtectiveLengthCm);
	WarningLengthCm = FMath::Max(WarningFieldLengthCm, ProtectiveLengthCm);

	Sweep.Reset();
	if (bRotating)
	{
		const double Sign = FieldSet == TEXT("ROTATE LEFT") ? 1.0 : -1.0;
		for (double Angle = 0.0; Angle <= WarningRotationDeg + 1e-3; Angle += 3.0)
		{
			Sweep.Add({ FVector2D::ZeroVector, Sign * FMath::DegreesToRadians(Angle), Angle });
		}
		return;
	}

	// Along the planned path when there is one, as the vehicle controller would select the field set for the coming
	// segment: the field bends with the path and ends where the vehicle will stop, so a wall behind the goal or a
	// rack beyond a corner does not slow it. Poses are taken relative to where the vehicle believes it is.
	TArray<FVector> Ahead;
	FVector2D Here;
	double HereYaw;
	if (bFieldsFollowPath && Navigator && Localizer && Navigator->GetPathAhead(WarningLengthCm, 10.0, Ahead) && Ahead.Num() > 0 && Localizer->GetPose(Here, HereYaw))
	{
		double Along = 0.0;
		for (int32 Index = 0; Index < Ahead.Num(); ++Index)
		{
			if (Index > 0)
			{
				Along += FVector2D::Distance(FVector2D(Ahead[Index]), FVector2D(Ahead[Index - 1]));
			}
			Sweep.Add({ AgvMath::Rotate(-HereYaw, FVector2D(Ahead[Index]) - Here), AgvMath::Wrap(Ahead[Index].Z - HereYaw), Along });
		}
		return;
	}

	// Otherwise the arc of the reference point for the banded steering angle (tricycle: heading change per distance =
	// -tan(steer) / L).
	const double Direction = FieldSet == TEXT("FORKS FIRST") ? 1.0 : -1.0;
	double Curvature = 0.0;
	if (Tricycle)
	{
		const double Steer = FMath::Clamp(FMath::RoundToDouble(Tricycle->SteerAngleDeg / SteerBandDeg) * SteerBandDeg, -80.0, 80.0);
		const double Wheelbase = FMath::Max(1.0, (double)Tricycle->ReferenceOffsetCm - Tricycle->DriveWheelOffsetCm);
		Curvature = -FMath::Tan(FMath::DegreesToRadians(Steer)) / Wheelbase;
	}
	const double StepCm = FMath::Min(10.0, FMath::DegreesToRadians(5.0) / FMath::Max(FMath::Abs(Curvature), 1e-6));
	FVector2D Position = FVector2D::ZeroVector;
	double Yaw = 0.0;
	for (double Along = 0.0; Along <= WarningLengthCm + StepCm; Along += StepCm)
	{
		Sweep.Add({ Position, Yaw, Along });
		const double Bend = Curvature * Direction * StepCm;
		Position += AgvMath::Dir(Yaw + Bend * 0.5) * (Direction * StepCm);
		Yaw += Bend;
	}
}

void UAgvSafetyComponent::Evaluate()
{
	// A point is in a field when some pose of the swept footprint (grown by the margins) contains it. Points inside
	// the vehicle's own outline right now are the vehicle or its load.
	// Margins on the sides and the leading end only (all round when turning on the spot): what the vehicle has
	// already passed must not stop it.
	auto Grow = [&](double Margin)
	{
		FBox2D Box = Footprint.ExpandBy(Margin);
		if (FieldSet == TEXT("BODY FIRST"))
		{
			Box.Max.X = Footprint.Max.X;
		}
		else if (FieldSet == TEXT("FORKS FIRST"))
		{
			Box.Min.X = Footprint.Min.X;
		}
		return Box;
	};
	const FBox2D Protective = Grow(SideMarginCm);
	const FBox2D Warning = Grow(SideMarginCm + WarningExtraWidthCm);
	const double ProtectiveReach = bRotating ? RotationLookaheadDeg : ProtectiveLengthCm;
	int32 ProtectiveHits = 0, WarningHits = 0;
	FVector FirstProtective = FVector::ZeroVector;
	// Where the vehicle believes it is, to look warning points up in the map.
	FVector2D Reference;
	double Yaw = 0.0;
	const bool bMapFilter = bWarningIgnoresMappedStructure && Localizer && Localizer->GetPose(Reference, Yaw);
	const FVector2D Origin = bMapFilter ? Reference - AgvMath::Dir(Yaw) * ReferenceX : FVector2D::ZeroVector;
	MappedPointsIgnored = 0;
	NearestObjectCm = -1.f;
	for (const TArray<FVector>& Points : LatestPoints)
	{
		for (const FVector& Point : Points)
		{
			const FVector2D Vehicle(Point);
			if (Footprint.IsInside(Vehicle))
			{
				continue;
			}
			// The two fields are judged separately: the wider warning outline reaches a point one pose earlier than the
			// protective one, so stopping at the first warning pose would miss the protective field.
			const FVector2D FromReference = Vehicle - FVector2D(ReferenceX, 0.0);
			bool bInWarning = false, bInProtective = false;
			for (const FSweepPose& Pose : Sweep)
			{
				const FVector2D InPose = AgvMath::Rotate(-Pose.Yaw, FromReference - Pose.Position) + FVector2D(ReferenceX, 0.0);
				if (!bInWarning && Warning.IsInside(InPose))
				{
					bInWarning = true;
					NearestObjectCm = NearestObjectCm < 0.f ? (float)Pose.Along : FMath::Min(NearestObjectCm, (float)Pose.Along);
				}
				bInProtective = Pose.Along <= ProtectiveReach && Protective.IsInside(InPose);
				if (bInProtective || Pose.Along > ProtectiveReach && bInWarning)
				{
					break;
				}
			}
			if (bInWarning && !bInProtective && bMapFilter
				&& Localizer->IsMappedStructure(Origin + AgvMath::Rotate(Yaw, Vehicle), MappedStructureToleranceCm))
			{
				bInWarning = false;
				++MappedPointsIgnored;
			}
			WarningHits += bInWarning;
			if (bInProtective && ProtectiveHits++ == 0)
			{
				FirstProtective = Point;
			}
		}
	}
	bProtectiveHit = ProtectiveHits >= MinObjectPoints;
	if (bProtectiveHit && State != EAgvSafetyState::Stop)
	{
		UE_LOG(LogAgvSafety, Verbose, TEXT("AGV_SAFETY %s: protective hit by %d points, first at vehicle (%.0f, %.0f, %.0f)"), *GetOwner()->GetName(), ProtectiveHits, FirstProtective.X, FirstProtective.Y, FirstProtective.Z);
	}
	bWarningHit = WarningHits >= MinObjectPoints;
}

void UAgvSafetyComponent::DrawFields() const
{
	const FTransform Actor = GetOwner()->GetActorTransform();
	const double ProtectiveReach = bRotating ? RotationLookaheadDeg : ProtectiveLengthCm;
	auto Outline = [&](const FSweepPose& Pose, const FBox2D& Box, const FColor& Color)
	{
		const FVector2D Corners[4] = { Box.Min, FVector2D(Box.Max.X, Box.Min.Y), Box.Max, FVector2D(Box.Min.X, Box.Max.Y) };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			auto World = [&](const FVector2D& C)
			{
				const FVector2D Local = AgvMath::Rotate(Pose.Yaw, C - FVector2D(ReferenceX, 0.0)) + Pose.Position + FVector2D(ReferenceX, 0.0);
				return Actor.TransformPosition(FVector(Local, 17.0));
			};
			DrawDebugLine(GetWorld(), World(Corners[Index]), World(Corners[(Index + 1) % 4]), Color, false, -1.f, 0, 2.f);
		}
	};
	const FColor Red = State == EAgvSafetyState::Stop ? FColor::Red : FColor(255, 120, 120);
	const FColor Yellow = State == EAgvSafetyState::Warning ? FColor::Orange : FColor::Yellow;
	for (int32 Index = 0; Index < Sweep.Num(); Index += 6)
	{
		const bool bProtective = Sweep[Index].Along <= ProtectiveReach;
		Outline(Sweep[Index], Footprint.ExpandBy(SideMarginCm + (bProtective ? 0.0 : WarningExtraWidthCm)), bProtective ? Red : Yellow);
	}
}
