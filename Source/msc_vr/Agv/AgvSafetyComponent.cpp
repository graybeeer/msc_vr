#include "AgvSafetyComponent.h"
#include "AgvDriveComponent.h"
#include "AgvLidarComponent.h"
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
	}
	if (!Drive || !Scanner)
	{
		return;
	}

	// Field set for the current motion; the scanner only guards travel with the body leading (and turning on the spot).
	const double Speed = FMath::Abs(Drive->SpeedCmS);
	bActive = Drive->SpeedCmS < 1.f;
	const double Band = FMath::CeilToDouble(FMath::Max(Speed, (double)StartSpeedCm) / SpeedBandCm) * SpeedBandCm;
	ProtectiveLengthCm = (float)FMath::Max(Band * ResponseSeconds + Band * Band / (2.0 * FieldDecelerationCm) + FieldMarginCm, (double)MinProtectiveLengthCm);
	WarningLengthCm = FMath::Max(WarningFieldLengthCm, ProtectiveLengthCm);

	Scanner->TakePoints(ScanPoints);
	if (Scanner->GetRevolutionCount() != SeenRevolution)
	{
		SeenRevolution = Scanner->GetRevolutionCount();
		Evaluate();
		ScanPoints.Reset();
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
		UE_LOG(LogAgvSafety, Log, TEXT("AGV_SAFETY %s: %s (speed %.0f cm/s, protective %.0f cm, nearest %.0f cm)"), *GetOwner()->GetName(),
			State == EAgvSafetyState::Stop ? TEXT("STOP") : State == EAgvSafetyState::Warning ? TEXT("WARNING") : TEXT("CLEAR"), Speed, ProtectiveLengthCm, NearestObjectCm);
	}
	StatusText = State == EAgvSafetyState::Stop ? TEXT("SAFETY STOP") : State == EAgvSafetyState::Warning ? TEXT("WARNING FIELD") : TEXT("CLEAR");
	Drive->SetSafetySpeedLimit(State == EAgvSafetyState::Stop ? 0.f : State == EAgvSafetyState::Warning ? WarningSpeedCm : TNumericLimits<float>::Max());

	if (bDrawFields)
	{
		DrawFields();
	}
}

void UAgvSafetyComponent::Evaluate()
{
	const FTransform Mount = Scanner->GetComponentTransform().GetRelativeTransform(GetOwner()->GetActorTransform());
	const double Protective = BodyHalfWidthCm + SideMarginCm;
	const double Warning = Protective + WarningExtraWidthCm;
	int32 ProtectiveHits = 0, WarningHits = 0;
	NearestObjectCm = -1.f;
	for (const FVector& Point : ScanPoints)
	{
		const FVector Vehicle = Mount.TransformPosition(Point);
		const double Ahead = BodyFrontXCm - Vehicle.X;
		const double Side = FMath::Abs(Vehicle.Y);
		if (Ahead <= 0.0 || Side > Warning)
		{
			continue;
		}
		if (Ahead <= WarningLengthCm)
		{
			++WarningHits;
			NearestObjectCm = NearestObjectCm < 0.f ? (float)Ahead : FMath::Min(NearestObjectCm, (float)Ahead);
			ProtectiveHits += Ahead <= ProtectiveLengthCm && Side <= Protective;
		}
	}
	bProtectiveHit = bActive && ProtectiveHits >= MinObjectPoints;
	bWarningHit = bActive && WarningHits >= MinObjectPoints;
}

void UAgvSafetyComponent::DrawFields() const
{
	const AActor* Owner = GetOwner();
	const FTransform Actor = Owner->GetActorTransform();
	const double Z = 17.0;
	auto Box = [&](double Length, double HalfWidth, const FColor& Color)
	{
		const FVector Corners[4] = { FVector(BodyFrontXCm, -HalfWidth, Z), FVector(BodyFrontXCm - Length, -HalfWidth, Z),
			FVector(BodyFrontXCm - Length, HalfWidth, Z), FVector(BodyFrontXCm, HalfWidth, Z) };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			DrawDebugLine(GetWorld(), Actor.TransformPosition(Corners[Index]), Actor.TransformPosition(Corners[(Index + 1) % 4]), Color, false, -1.f, 0, 2.f);
		}
	};
	if (bActive)
	{
		Box(WarningLengthCm, BodyHalfWidthCm + SideMarginCm + WarningExtraWidthCm, State == EAgvSafetyState::Warning ? FColor::Orange : FColor::Yellow);
		Box(ProtectiveLengthCm, BodyHalfWidthCm + SideMarginCm, State == EAgvSafetyState::Stop ? FColor::Red : FColor(255, 120, 120));
	}
}
