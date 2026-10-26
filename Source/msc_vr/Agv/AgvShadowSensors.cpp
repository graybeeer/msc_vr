#include "AgvShadowSensors.h"
#include "AgvLidarComponent.h"
#include "AgvDriveComponent.h"
#include "AgvTestVehicle.h"
#include "WarehouseForklift.h"
#include "WarehousePhysics.h"
#include "WarehouseCargo.h"
#include "WarehousePallet.h"
#include "WarehouseWorker.h"
#include "WarehouseChargingStation.h"
#include "WarehouseWaitingBay.h"
#include "WarehouseElevator.h"
#include "WarehouseConveyor.h"
#include "WarehouseLogisticsTruck.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/PlatformTime.h"

UAgvShadowSensorsComponent::UAgvShadowSensorsComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UAgvShadowSensorsComponent::BuildSensors()
{
	if (Lidars.Num() == 5) return true;
	AActor* Vehicle = GetOwner();
	if (!Vehicle || !Vehicle->GetRootComponent()) return false;
	auto Add = [&](const TCHAR* Name, FVector Location, FRotator Rotation)
	{
		auto* Sensor = NewObject<UAgvLidarComponent>(Vehicle, Name);
		Sensor->SetupAttachment(Vehicle->GetRootComponent());
		Sensor->SetRelativeLocationAndRotation(Location, Rotation);
		Sensor->RotationHz = 1.f;
		Sensor->bUseForLocalization = false;
		Sensor->RegisterComponent();
		Lidars.Add(Sensor);
		return Sensor;
	};
	// Same real model mounts and fields of view as the reviewed sensor branch. Sampling is one full snapshot/sec.
	auto* Top = Add(TEXT("AgvShadowTopLidar"), AAgvTestVehicle::Refined(-36,-10,272), FRotator::ZeroRotator);
	Top->HorizontalResolutionDeg = .8f;
	for (double Side : {1.,-1.})
	{
		auto* Sensor = Add(Side > 0 ? TEXT("AgvShadowScannerL") : TEXT("AgvShadowScannerR"),
			AAgvTestVehicle::Refined(-73,47*Side,27), FRotator(0,135*Side,0));
		Sensor->HorizontalFovDeg = 270;
		Sensor->HorizontalResolutionDeg = 1;
		Sensor->Channels = 1;
		Sensor->VerticalMinDeg = Sensor->VerticalMaxDeg = 0;
		Sensor->MinRangeCm = 5;
		Sensor->MaxRangeCm = 550;
		Sensor->RangeNoiseCm = 1;
		Sensor->DropoutProbability = .005f;
		Sensor->NoiseSeed = Side > 0 ? 23 : 29;
	}
	for (double Y : {26.,-46.})
	{
		auto* Sensor = Add(Y > 0 ? TEXT("AgvShadowForkSensorL") : TEXT("AgvShadowForkSensorR"),
			AAgvTestVehicle::Refined(-22,Y,235), FRotator(-30,0,0));
		Sensor->HorizontalFovDeg = 80;
		Sensor->HorizontalResolutionDeg = 3;
		Sensor->Channels = 15;
		Sensor->VerticalMinDeg = -25;
		Sensor->VerticalMaxDeg = 25;
		Sensor->MinRangeCm = 20;
		Sensor->MaxRangeCm = 600;
		Sensor->RangeNoiseCm = 1;
		Sensor->DropoutProbability = .01f;
		Sensor->NoiseSeed = Y > 0 ? 31 : 37;
	}
	return true;
}

bool UAgvShadowSensorsComponent::IsBuildingMapComponent(const UPrimitiveComponent* Component)
{
	if (!WarehousePhysics::IsPhysicalContact(Component) || !Component->IsQueryCollisionEnabled()) return false;
	const AActor* Actor = Component->GetOwner();
	// A conveyor roller rotates on a fixed machine axle. Its mounted envelope
	// belongs to the facility map; independent cargo never inherits that owner.
	if (Component->IsSimulatingPhysics() && (!Actor || !Actor->IsA<AWarehouseConveyor>())) return false;
	if (!Actor || (!Actor->ActorHasTag(TEXT("WarehouseMapStructure")) && !Component->ComponentHasTag(TEXT("WarehouseMapStructure")) &&
		!Actor->ActorHasTag(TEXT("LayoutType:Rack")))) return false;
	if (Actor->IsA<APawn>() || Actor->IsA<AWarehouseWorker>() || Actor->IsA<AWarehouseCargo>() ||
		Actor->IsA<AWarehousePallet>() || Actor->IsA<AWarehouseForklift>() || Actor->IsA<AWarehouseChargingStation>() ||
		Actor->IsA<AWarehouseWaitingBay>() || Actor->IsA<AWarehouseElevator>() || Actor->IsA<AWarehouseLogisticsTruck>() ||
		Actor->FindComponentByClass<UAgvDriveComponent>()) return false;
	for (FName Tag : Actor->Tags)
	{
		const FString Name = Tag.ToString();
		const bool Facility = Name == TEXT("LayoutType:Forklift") ||
			Name == TEXT("LayoutType:Elevator") || Name == TEXT("LayoutType:Zone") || Name == TEXT("LayoutType:Charger") ||
			Name == TEXT("LayoutType:WaitingBay") || Name == TEXT("LayoutType:Cargo") || Name == TEXT("LayoutType:Pallet");
		if (Facility || Name.StartsWith(TEXT("LayoutGroup:TruckDock")) ||
			Name.StartsWith(TEXT("Logistics")) || Name == TEXT("TruckCargo") || Name == TEXT("WarehousePhysicalHuman")) return false;
	}
	return true;
}

void UAgvShadowSensorsComponent::BuildKnownMap()
{
	KnownObstacles.Reset();
	KnownComponents.Reset();
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		TArray<UPrimitiveComponent*> Components;
		It->GetComponents(Components);
		for (UPrimitiveComponent* Part : Components) if (IsBuildingMapComponent(Part))
		{
			FAgvObservedObstacle& Entry = KnownObstacles.AddDefaulted_GetRef();
			Entry.Actor = *It;
			Entry.Component = Part;
			Entry.ObservedTransform = Part->GetComponentTransform();
			Entry.WorldBounds = Part->Bounds.GetBox();
			Entry.LocalBounds = Part->CalcBounds(FTransform::Identity).GetBox();
			KnownComponents.Add(Part);
		}
	}
	bMapDirty = false;
	++MapRevision;
}

void UAgvShadowSensorsComponent::InvalidateSnapshot(const FString& Reason)
{
	bSnapshotValid = false;
	ObservedObstacles.Reset();
	KnownObstacles.Reset();
	KnownComponents.Reset();
	ObservedObstacleCount = 0;
	bMapDirty = true;
	SensorStatus = Reason;
	for (UAgvLidarComponent* Sensor : Lidars) if (Sensor) Sensor->DiscardSnapshot();
}

bool UAgvShadowSensorsComponent::IsSnapshotFresh(double SimNow) const
{
	return bSnapshotValid && LastSnapshotSeconds >= 0 && SimNow >= LastSnapshotSeconds && SimNow-LastSnapshotSeconds <= 1.25;
}

bool UAgvShadowSensorsComponent::IsKnownComponent(const UPrimitiveComponent* Component) const
{
	return Component && KnownComponents.Contains(TWeakObjectPtr<UPrimitiveComponent>(const_cast<UPrimitiveComponent*>(Component)));
}

bool UAgvShadowSensorsComponent::HasObservedActor(const AActor* Actor) const
{
	return Actor && ObservedObstacles.ContainsByPredicate([&](const FAgvObservedObstacle& Entry) { return Entry.Actor.Get() == Actor; });
}

bool UAgvShadowSensorsComponent::RefreshSnapshot(double SimNow, double FloorZCm)
{
	const auto* Forklift = Cast<AWarehouseForklift>(GetOwner());
	if (!Forklift || Forklift->IsRemoteControlled() || Forklift->SensorMode == EWarehouseSensorMode::Disabled ||
		!Forklift->bPowered || !Forklift->bAutonomousMode || Forklift->AIState == EWarehouseAIState::ElevatorRide ||
		!Forklift->bSafetyScannerHealthy || !Forklift->bForkSensorHealthy)
	{
		InvalidateSnapshot(TEXT("SAMPLING SUSPENDED"));
		return false;
	}
	if (!FMath::IsFinite(SimNow) || !FMath::IsFinite(FloorZCm) || !BuildSensors()) return false;
	if (bSnapshotValid && FMath::Abs(SnapshotFloorZCm-FloorZCm) < 50 && SimNow-LastSnapshotSeconds < 1.-KINDA_SMALL_NUMBER) return true;
	const double Start = FPlatformTime::Seconds();
	// A removed or physically released rack is no longer fixed map geometry.
	// Check metadata on the ordinary one-second frame; moving debris still requires sensor returns.
	bMapDirty = bMapDirty || KnownObstacles.ContainsByPredicate([](const FAgvObservedObstacle& Entry)
	{ return !IsBuildingMapComponent(Entry.Component.Get()); });
	if (bMapDirty || FMath::Abs(SnapshotFloorZCm-FloorZCm) > 50) BuildKnownMap();
	SnapshotFloorZCm = FloorZCm;
	ObservedObstacles.Reset();
	LastBeamCount = 0;
	TMap<TWeakObjectPtr<UPrimitiveComponent>,int32> Indices;
	TMap<TWeakObjectPtr<UPrimitiveComponent>,TSet<FIntVector>> Voxels;
	for (UAgvLidarComponent* Sensor : Lidars)
	{
		Sensor->CaptureSnapshot();
		LastBeamCount += Sensor->LastStepBeams;
		const FTransform Mount = Sensor->GetComponentTransform();
		for (const FAgvLidarReturn& Return : Sensor->GetLastReturns())
		{
			UPrimitiveComponent* Part = Return.Component.Get();
			if (!Part || IsKnownComponent(Part)) continue;
			// Position comes from noisy range + beam angle + mount/current navigation pose. We do not copy the
			// reflector's transform or entire bounds from UE: unobserved faces and objects remain unknown.
			const FVector Point = Mount.TransformPosition(Return.SensorPoint);
			// Keep high returns: a raised pallet can extend above the chassis. Queries use their actual
			// vehicle/load height to select relevant points; the sensor must not erase an overhead load.
			if (Point.Z < FloorZCm+5) continue;
			const FIntVector Voxel(FMath::FloorToInt(Point.X/8), FMath::FloorToInt(Point.Y/8), FMath::FloorToInt(Point.Z/8));
			auto& Seen = Voxels.FindOrAdd(Part);
			if (Seen.Contains(Voxel)) continue;
			Seen.Add(Voxel);
			int32* Found = Indices.Find(Part);
			if (!Found)
			{
				const int32 Index = ObservedObstacles.AddDefaulted();
				Indices.Add(Part,Index);
				Found = Indices.Find(Part);
				auto& Entry = ObservedObstacles[*Found];
				Entry.Component = Part;
				Entry.Actor = Part->GetOwner();
				Entry.bPerson = Part->GetCollisionObjectType() == ECC_Pawn || (Part->GetOwner() && Part->GetOwner()->ActorHasTag(TEXT("WarehousePhysicalHuman")));
			}
			auto& Entry = ObservedObstacles[*Found];
			Entry.WorldPoints.Add(Point);
			Entry.WorldBounds += Point;
		}
	}
	for (auto& Entry : ObservedObstacles)
	{
		Entry.WorldBounds = Entry.WorldBounds.ExpandBy(2.);
		Entry.ObservedTransform = FTransform(Entry.WorldBounds.GetCenter());
		Entry.LocalBounds = Entry.WorldBounds.ShiftBy(-Entry.WorldBounds.GetCenter());
	}
	LastSnapshotSeconds = SimNow;
	++SnapshotCount;
	bSnapshotValid = true;
	ObservedObstacleCount = ObservedObstacles.Num();
	LastSensorMilliseconds = float((FPlatformTime::Seconds()-Start)*1000.);
	SensorStatus = TEXT("FRESH / 1 SIM SECOND");
	return true;
}
