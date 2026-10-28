#include "AgvLidarComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Async/ParallelFor.h"
#include "WarehousePhysics.h"

namespace
{
	/**
	 * A laser returns from anything solid. Overlap-only volumes (triggers) are not solid; everything that blocks
	 * something physically is, including pawn capsules, which ignore the Visibility channel.
	 */
	bool IsSolid(const UPrimitiveComponent* Component)
	{
		return WarehousePhysics::IsPhysicalContact(Component);
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

void UAgvLidarComponent::DiscardSnapshot()
{
	Points.Reset();
	LastReturns.Reset();
	Column = 0.0;
	ScanBeams = ScanPoints = ScanSelfBlocked = 0;
}

void UAgvLidarComponent::CaptureSnapshot()
{
	DiscardSnapshot();
	// Preserve the standalone test vehicle's hardware-frequency setting. Warehouse sampling does not replay
	// the revolutions that would have happened between observations or create an independent sensor tick.
	const float SavedHz = RotationHz;
	RotationHz = 1.f;
	Step(1.f);
	RotationHz = SavedHz;
}

void UAgvLidarComponent::EnsureBeamDirections(int32 Columns)
{
	if (CachedColumns==Columns && CachedChannels==Channels && CachedFov==HorizontalFovDeg &&
		CachedVerticalMin==VerticalMinDeg && CachedVerticalMax==VerticalMaxDeg) return;
	CachedColumns=Columns;CachedChannels=Channels;CachedFov=HorizontalFovDeg;
	CachedVerticalMin=VerticalMinDeg;CachedVerticalMax=VerticalMaxDeg;
	BeamDirections.Reset();BeamDirections.Reserve(Columns*FMath::Max(0,Channels));
	const double FirstAzimuth=HorizontalFovDeg>=360.f ? 0.0 : -HorizontalFovDeg*0.5;
	for (int32 Wrapped=0;Wrapped<Columns;++Wrapped)
	{
		const double Azimuth=FMath::DegreesToRadians(FirstAzimuth+(Wrapped+0.5)*HorizontalFovDeg/Columns);
		for (int32 Channel=0;Channel<Channels;++Channel)
		{
			const double Elevation=FMath::DegreesToRadians(Channels==1 ? VerticalMinDeg : FMath::Lerp((double)VerticalMinDeg,(double)VerticalMaxDeg,Channel/double(Channels-1)));
			BeamDirections.Add(FVector(FMath::Cos(Elevation)*FMath::Cos(Azimuth),FMath::Cos(Elevation)*FMath::Sin(Azimuth),FMath::Sin(Elevation)));
		}
	}
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
	LastReturns.Reset();
	const int32 Columns = FMath::Max(1, FMath::RoundToInt32(HorizontalFovDeg / HorizontalResolutionDeg));
	const double Target = Column + Columns * RotationHz * Dt;

	// Reuse fixed ray directions and scratch storage. Firing order and the
	// per-scan result reset remain identical, including partial revolutions.
	EnsureBeamDirections(Columns);
	auto& Beams=BeamScratch;
	Beams.Reset();
	Beams.Reserve(FMath::Max(0,FMath::FloorToInt32(Target)-FMath::FloorToInt32(Column))*FMath::Max(0,Channels));
	for (int32 Index = FMath::FloorToInt32(Column); Index < FMath::FloorToInt32(Target); ++Index)
	{
		const int32 Wrapped = Index % Columns;
		for (int32 Channel = 0; Channel < Channels; ++Channel)
		{
			Beams.Add({BeamDirections[Wrapped*Channels+Channel],Wrapped});
		}
	}

	// Trace in parallel (read-only scene queries; each beam writes only its own slot). Traced by object type, not the
	// Visibility channel: people (Pawn capsules) ignore Visibility but reflect light. The owner is not ignored: its own
	// body blocks beams (a sensor behind the mast does not see through it).
	FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllObjects);
	// Selection surfaces never have physical contact; excluding their object type avoids retracing through them.
	Objects.RemoveObjectTypesToQuery(WarehousePhysics::QueryMarkerObject);
	Points.Reserve(Points.Num()+Beams.Num());
	LastReturns.Reserve(Beams.Num());
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
		UPrimitiveComponent* HitComponent = bHit ? Hit.GetComponent() : nullptr;
		bool bSolid = HitComponent && IsSolid(HitComponent);
		AActor* HitActor = bSolid ? Hit.GetActor() : nullptr;
		for (int32 Retry = 0; bHit && HitComponent && Retry < 4; ++Retry)
		{
			if (!bSolid)
			{
				Query.AddIgnoredComponent(HitComponent); // look through the trigger volume
			}
			else if (HitActor != Owner || Skip + Hit.Distance > HousingRadiusCm)
			{
				break;
			}
			else
			{
				Skip += Hit.Distance + 0.5; // the sensor's own housing / window: continue just past it
			}
			bHit = World->LineTraceSingleByObjectType(Hit, Origin + Direction * Skip, End, Objects, Query);
			HitComponent = bHit ? Hit.GetComponent() : nullptr;
			bSolid = HitComponent && IsSolid(HitComponent);
			HitActor = bSolid ? Hit.GetActor() : nullptr;
		}
		if (bHit && bSolid)
		{
			Beam.Result = HitActor == Owner ? 1 : 2;
			Beam.Distance = Skip + Hit.Distance;
			Beam.Component = HitComponent;
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
				LastReturns.Add({Beam.Component, Beam.Local * Range});
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

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgvLidarDirectionCacheTest,"Warehouse.Sensors.LidarDirectionCache",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAgvLidarDirectionCacheTest::RunTest(const FString& Parameters)
{
	UAgvLidarComponent* Sensor=NewObject<UAgvLidarComponent>();
	auto Check=[&](int32 Columns)
	{
		Sensor->EnsureBeamDirections(Columns);
		if (!TestEqual(TEXT("One direction per firing column/channel"),Sensor->BeamDirections.Num(),Columns*Sensor->Channels)) return false;
		const double FirstAzimuth=Sensor->HorizontalFovDeg>=360.f ? 0.0 : -Sensor->HorizontalFovDeg*0.5;
		// Reference is the original per-scan computation, including operation
		// order. No tolerance: a cache must not alter a traced ray or noise order.
		for (int32 Wrapped=0;Wrapped<Columns;++Wrapped) for (int32 Channel=0;Channel<Sensor->Channels;++Channel)
		{
			const double Azimuth=FMath::DegreesToRadians(FirstAzimuth+(Wrapped+0.5)*Sensor->HorizontalFovDeg/Columns);
			const double Elevation=FMath::DegreesToRadians(Sensor->Channels==1 ? Sensor->VerticalMinDeg : FMath::Lerp((double)Sensor->VerticalMinDeg,(double)Sensor->VerticalMaxDeg,Channel/double(Sensor->Channels-1)));
			const FVector Expected(FMath::Cos(Elevation)*FMath::Cos(Azimuth),FMath::Cos(Elevation)*FMath::Sin(Azimuth),FMath::Sin(Elevation));
			if (Sensor->BeamDirections[Wrapped*Sensor->Channels+Channel]!=Expected)
			{
				AddError(FString::Printf(TEXT("Cached ray differs from original at column %d channel %d"),Wrapped,Channel));
				return false;
			}
		}
		return true;
	};
	if (!Check(900)) return false;
	const FVector* Allocation=Sensor->BeamDirections.GetData();
	Sensor->EnsureBeamDirections(900);
	TestTrue(TEXT("Unchanged scan reuses direction storage"),Allocation==Sensor->BeamDirections.GetData());
	Sensor->HorizontalFovDeg=270.f;if (!Check(900)) return false;
	Sensor->VerticalMinDeg=-4.5f;if (!Check(900)) return false;
	Sensor->VerticalMaxDeg=23.f;if (!Check(900)) return false;
	Sensor->Channels=1;if (!Check(675)) return false;
	Sensor->Channels=8;Sensor->HorizontalFovDeg=360.f;if (!Check(37)) return false;
	// Partial revolutions wrap columns without changing channel/firing order.
	for (int32 I=35;I<41;++I) for (int32 C=0;C<Sensor->Channels;++C)
		Sensor->BeamScratch.Add({Sensor->BeamDirections[(I%37)*Sensor->Channels+C],I%37});
	TestEqual(TEXT("Partial sweep emits every requested beam"),Sensor->BeamScratch.Num(),6*Sensor->Channels);
	for (int32 I=0;I<Sensor->BeamScratch.Num();++I)
	{
		const auto& Beam=Sensor->BeamScratch[I];
		if (!TestEqual(TEXT("Firing column order"),Beam.Column,(35+I/Sensor->Channels)%37)) return false;
		if (!TestTrue(TEXT("A new beam has no previous hit"),Beam.Result==0 && Beam.Distance==0. && !Beam.Component.IsValid())) return false;
	}
	Sensor->BeamScratch[0].Result=2;Sensor->BeamScratch[0].Distance=123.;
	Sensor->BeamScratch.Reset();Sensor->BeamScratch.Add({FVector::ForwardVector,0});
	TestTrue(TEXT("Reused scratch clears the previous result"),Sensor->BeamScratch[0].Result==0 && Sensor->BeamScratch[0].Distance==0.);
	return true;
}
#endif
