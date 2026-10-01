#include "AgvLidarComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"

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

	FCollisionQueryParams Query(SCENE_QUERY_STAT(AgvLidar), true, GetOwner());
	const FTransform Sensor = GetComponentTransform();
	const FVector Origin = Sensor.GetLocation();
	LastStepBeams = 0;
	for (int32 Index = FMath::FloorToInt32(Column); Index < FMath::FloorToInt32(Target); ++Index)
	{
		const int32 Wrapped = Index % Columns;
		// Columns sweep the field of view from one edge; a full circle starts straight ahead.
		const double Start = HorizontalFovDeg >= 360.f ? 0.0 : -HorizontalFovDeg * 0.5;
		const double Azimuth = FMath::DegreesToRadians(Start + (Wrapped + 0.5) * HorizontalFovDeg / Columns);
		for (int32 Channel = 0; Channel < Channels; ++Channel)
		{
			++LastStepBeams;
			const double Elevation = FMath::DegreesToRadians(Channels == 1 ? VerticalMinDeg : FMath::Lerp((double)VerticalMinDeg, (double)VerticalMaxDeg, Channel / double(Channels - 1)));
			const FVector Local(FMath::Cos(Elevation) * FMath::Cos(Azimuth), FMath::Cos(Elevation) * FMath::Sin(Azimuth), FMath::Sin(Elevation));
			const FVector Direction = Sensor.TransformVectorNoScale(Local);
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, Origin, Origin + Direction * MaxRangeCm, ECC_Visibility, Query))
			{
				continue;
			}
			if (Noise.FRand() < DropoutProbability)
			{
				continue;
			}
			// Box-Muller: Gaussian range error.
			const double Gauss = FMath::Sqrt(-2.0 * FMath::Loge(FMath::Max(Noise.FRand(), 1e-12f))) * FMath::Cos(2.0 * UE_DOUBLE_PI * Noise.FRand());
			const double Range = Hit.Distance + Gauss * RangeNoiseCm;
			if (Range < MinRangeCm || Range > MaxRangeCm)
			{
				continue;
			}
			Points.Add(Local * Range);
			if (bDrawPoints)
			{
				DrawDebugPoint(World, Origin + Direction * Range, 4.f, FColor::Red, false, 1.f / RotationHz);
			}
		}
		if (Wrapped == Columns - 1)
		{
			++RevolutionCount;
		}
	}
	Column = FMath::Fmod(Target, (double)Columns);
	LastStepMilliseconds = (float)((FPlatformTime::Seconds() - StartTime) * 1000.0);
}
