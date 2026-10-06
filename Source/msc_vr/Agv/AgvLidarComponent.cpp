#include "AgvLidarComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Async/ParallelFor.h"

namespace
{
	/**
	 * A laser returns from anything solid. Overlap-only volumes (triggers) are not solid; everything that blocks
	 * something physically is, including pawn capsules, which ignore the Visibility channel.
	 */
	bool IsSolid(const UPrimitiveComponent* Component)
	{
		for (const ECollisionChannel Channel : { ECC_WorldStatic, ECC_WorldDynamic, ECC_Pawn, ECC_PhysicsBody })
		{
			if (Component->GetCollisionResponseToChannel(Channel) == ECR_Block)
			{
				return true;
			}
		}
		return false;
	}
}

UAgvLidarComponent::UAgvLidarComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UAgvLidarComponent::TakePoints(TArray<FVector>& Out)
{
	Out.Append(Points);
	Points.Reset();
}

void UAgvLidarComponent::Step(float Dt)
{
	UWorld* World = GetWorld();
	if (!World || Dt <= 0.f)
	{
		return;
	}
	if (!bNoiseSeeded)
	{
		Noise.Initialize(NoiseSeed);
		bNoiseSeeded = true;
	}
	const double StartTime = FPlatformTime::Seconds();
	const int32 Columns = FMath::Max(1, FMath::RoundToInt32(HorizontalFovDeg / HorizontalResolutionDeg));
	const double Target = Column + Columns * RotationHz * Dt;

	// Beams swept this step, in firing order. Columns sweep the field of view from one edge; a full circle starts
	// straight ahead.
	struct FBeam
	{
		FVector Local;
		int32 Column;
		double Distance = 0.0;
		uint8 Result = 0; // 0 nothing, 1 own body, 2 return
	};
	TArray<FBeam> Beams;
	const double FirstAzimuth = HorizontalFovDeg >= 360.f ? 0.0 : -HorizontalFovDeg * 0.5;
	for (int32 Index = FMath::FloorToInt32(Column); Index < FMath::FloorToInt32(Target); ++Index)
	{
		const int32 Wrapped = Index % Columns;
		const double Azimuth = FMath::DegreesToRadians(FirstAzimuth + (Wrapped + 0.5) * HorizontalFovDeg / Columns);
		for (int32 Channel = 0; Channel < Channels; ++Channel)
		{
			const double Elevation = FMath::DegreesToRadians(Channels == 1 ? VerticalMinDeg : FMath::Lerp((double)VerticalMinDeg, (double)VerticalMaxDeg, Channel / double(Channels - 1)));
			Beams.Add({ FVector(FMath::Cos(Elevation) * FMath::Cos(Azimuth), FMath::Cos(Elevation) * FMath::Sin(Azimuth), FMath::Sin(Elevation)), Wrapped });
		}
	}

	// Trace in parallel (read-only scene queries; each beam writes only its own slot). Traced by object type, not the
	// Visibility channel: people (Pawn capsules) ignore Visibility but reflect light. The owner is not ignored: its own
	// body blocks beams (a sensor behind the mast does not see through it).
	const FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
	const FTransform Sensor = GetComponentTransform();
	const FVector Origin = Sensor.GetLocation();
	const AActor* Owner = GetOwner();
	ParallelFor(Beams.Num(), [&](int32 Index)
	{
		FBeam& Beam = Beams[Index];
		FCollisionQueryParams Query(SCENE_QUERY_STAT(AgvLidar), true);
		const FVector Direction = Sensor.TransformVectorNoScale(Beam.Local);
		const FVector End = Origin + Direction * MaxRangeCm;
		FHitResult Hit;
		double Skip = 0.0;
		bool bHit = World->LineTraceSingleByObjectType(Hit, Origin, End, Objects, Query);
		for (int32 Retry = 0; bHit && Hit.GetComponent() && Retry < 4; ++Retry)
		{
			if (!IsSolid(Hit.GetComponent()))
			{
				Query.AddIgnoredComponent(Hit.GetComponent()); // look through the trigger volume
			}
			else if (Hit.GetActor() != Owner || Skip + Hit.Distance > HousingRadiusCm)
			{
				break;
			}
			else
			{
				Skip += Hit.Distance + 0.5; // the sensor's own housing / window: continue just past it
			}
			bHit = World->LineTraceSingleByObjectType(Hit, Origin + Direction * Skip, End, Objects, Query);
		}
		if (bHit && Hit.GetComponent() && IsSolid(Hit.GetComponent()))
		{
			Beam.Result = Hit.GetActor() == Owner ? 1 : 2;
			Beam.Distance = Skip + Hit.Distance;
		}
	});

	// Noise, dropouts and bookkeeping in firing order, so the result does not depend on thread timing.
	LastStepBeams = Beams.Num();
	for (int32 Index = 0; Index < Beams.Num(); ++Index)
	{
		const FBeam& Beam = Beams[Index];
		++ScanBeams;
		ScanSelfBlocked += Beam.Result == 1; // self returns are filtered out, as drivers do
		if (Beam.Result == 2 && Noise.FRand() >= DropoutProbability)
		{
			// Box-Muller: Gaussian range error.
			const double Gauss = FMath::Sqrt(-2.0 * FMath::Loge(FMath::Max(Noise.FRand(), 1e-12f))) * FMath::Cos(2.0 * UE_DOUBLE_PI * Noise.FRand());
			const double Range = Beam.Distance + Gauss * RangeNoiseCm;
			if (Range >= MinRangeCm && Range <= MaxRangeCm)
			{
				Points.Add(Beam.Local * Range);
				++ScanPoints;
				if (bDrawPoints)
				{
					DrawDebugPoint(World, Origin + Sensor.TransformVectorNoScale(Beam.Local) * Range, 4.f, FColor::Red, false, 1.f / RotationHz);
				}
			}
		}
		const bool bColumnEnd = Index + 1 == Beams.Num() || Beams[Index + 1].Column != Beam.Column;
		if (bColumnEnd && Beam.Column == Columns - 1)
		{
			++RevolutionCount;
			LastScanBeams = ScanBeams;
			LastScanPoints = ScanPoints;
			LastScanSelfBlocked = ScanSelfBlocked;
			ScanBeams = ScanPoints = ScanSelfBlocked = 0;
		}
	}
	Column = FMath::Fmod(Target, (double)Columns);
	LastStepMilliseconds = (float)((FPlatformTime::Seconds() - StartTime) * 1000.0);
}
