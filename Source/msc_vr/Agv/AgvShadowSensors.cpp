#include "AgvShadowSensors.h"
#include "AgvLidarComponent.h"
#include "AgvDriveComponent.h"
#include "AgvTestVehicle.h"
#include "WarehouseForklift.h"
#include "WarehousePhysics.h"
#include "WarehousePerformance.h"
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
#include "HAL/IConsoleManager.h"
#include "Algo/Sort.h"

namespace
{
TAutoConsoleVariable<int32> CVarWarehousePerceptionSpatialIndex(
	TEXT("wh.PerceptionSpatialIndex"),1,
	TEXT("Use the published sensor/map broad phase. 0 retains the exhaustive reference path for diagnostics."));

bool IndexableBounds(const FBox& Bounds)
{
	return Bounds.IsValid && !Bounds.Min.ContainsNaN() && !Bounds.Max.ContainsNaN();
}

void ReferenceCandidates(TConstArrayView<FAgvObservedObstacle> Obstacles,const FBox& Bounds,FAgvObstacleIndices& Out)
{
	Out.Reset();
	for (int32 I=0;I<Obstacles.Num();++I)
		if (!IndexableBounds(Bounds) || !IndexableBounds(Obstacles[I].WorldBounds) || Obstacles[I].WorldBounds.Intersect(Bounds)) Out.Add(I);
}
}

void FAgvObstacleSpatialIndex::Reset()
{
	Nodes.Reset();PublishedBounds.Reset();Entries.Reset();Unindexed.Reset();
}

void FAgvObstacleSpatialIndex::Build(TConstArrayView<FAgvObservedObstacle> Obstacles)
{
	Reset();
	PublishedBounds.Reserve(Obstacles.Num());Entries.Reserve(Obstacles.Num());
	for (int32 I=0;I<Obstacles.Num();++I)
	{
		PublishedBounds.Add(Obstacles[I].WorldBounds);
		if (IndexableBounds(Obstacles[I].WorldBounds)) Entries.Add(I);
		else Unindexed.Add(I); // Malformed entries still reach the original narrow phase.
	}
	Nodes.Reserve(Entries.Num()*2);
	if (!Entries.IsEmpty()) BuildNode(0,Entries.Num());
}

int32 FAgvObstacleSpatialIndex::BuildNode(int32 Begin,int32 Count)
{
	const int32 Node=Nodes.AddDefaulted();
	FBox Centers(ForceInit),Bounds(ForceInit);
	for (int32 I=Begin;I<Begin+Count;++I)
	{
		const FBox& Entry=PublishedBounds[Entries[I]];
		Bounds+=Entry;Centers+=Entry.GetCenter();
	}
	Nodes[Node].Bounds=Bounds;
	if (Count<=8)
	{
		Nodes[Node].Begin=Begin;Nodes[Node].Count=Count;
		return Node;
	}
	const FVector Span=Centers.GetSize();
	const int32 Axis=Span.X>=Span.Y && Span.X>=Span.Z ? 0 : Span.Y>=Span.Z ? 1 : 2;
	Algo::Sort(MakeArrayView(Entries.GetData()+Begin,Count),[&](int32 A,int32 B)
	{
		const double X=PublishedBounds[A].GetCenter()[Axis],Y=PublishedBounds[B].GetCenter()[Axis];
		return X==Y ? A<B : X<Y;
	});
	const int32 Left=BuildNode(Begin,Count/2);
	const int32 Right=BuildNode(Begin+Count/2,Count-Count/2);
	Nodes[Node].Left=Left;Nodes[Node].Right=Right;
	return Node;
}

void FAgvObstacleSpatialIndex::Query(const FBox& Bounds,FAgvObstacleIndices& Out) const
{
	Out.Reset();
	if (!IndexableBounds(Bounds))
	{
		for (int32 I=0;I<PublishedBounds.Num();++I) Out.Add(I);
		return;
	}
	Out.Append(Unindexed);
	TArray<int32,TInlineAllocator<32>> Stack;
	if (!Nodes.IsEmpty()) Stack.Add(0);
	while (!Stack.IsEmpty())
	{
		const FNode& Node=Nodes[Stack.Pop(EAllowShrinking::No)];
		if (!Node.Bounds.Intersect(Bounds)) continue;
		if (Node.Count)
		{
			for (int32 I=Node.Begin;I<Node.Begin+Node.Count;++I)
				if (PublishedBounds[Entries[I]].Intersect(Bounds)) Out.Add(Entries[I]);
		}
		else { Stack.Add(Node.Left);Stack.Add(Node.Right); }
	}
	Out.Sort();
}

void UAgvShadowSensorsComponent::FindKnownCandidates(const FBox& Bounds,FAgvObstacleIndices& Out) const
{
	if (CVarWarehousePerceptionSpatialIndex.GetValueOnGameThread()) KnownIndex.Query(Bounds,Out);
	else ReferenceCandidates(KnownObstacles,Bounds,Out);
}

void UAgvShadowSensorsComponent::FindObservedCandidates(const FBox& Bounds,FAgvObstacleIndices& Out) const
{
	if (CVarWarehousePerceptionSpatialIndex.GetValueOnGameThread()) ObservedIndex.Query(Bounds,Out);
	else ReferenceCandidates(ObservedObstacles,Bounds,Out);
}

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
	CSV_SCOPED_TIMING_STAT(Warehouse, KnownMapBuild);
	CSV_CUSTOM_STAT(Warehouse, KnownMapBuilds, 1, ECsvCustomStatOp::Accumulate);
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
	KnownIndex.Build(KnownObstacles);
	bMapDirty = false;
	++MapRevision;
	CSV_CUSTOM_STAT(Warehouse, KnownMapComponents, KnownObstacles.Num(), ECsvCustomStatOp::Accumulate);
}

void UAgvShadowSensorsComponent::InvalidateSnapshot(const FString& Reason)
{
	bSnapshotValid = false;
	ObservedObstacles.Reset();
	KnownObstacles.Reset();
	KnownComponents.Reset();
	KnownIndex.Reset();ObservedIndex.Reset();
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
	CSV_SCOPED_TIMING_STAT(Warehouse, SensorSnapshot);
	CSV_CUSTOM_STAT(Warehouse, SensorSnapshots, 1, ECsvCustomStatOp::Accumulate);
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
	ObservedIndex.Build(ObservedObstacles);
	LastSnapshotSeconds = SimNow;
	++SnapshotCount;
	bSnapshotValid = true;
	ObservedObstacleCount = ObservedObstacles.Num();
	LastSensorMilliseconds = float((FPlatformTime::Seconds()-Start)*1000.);
	CSV_CUSTOM_STAT(Warehouse, SensorBeams, LastBeamCount, ECsvCustomStatOp::Accumulate);
	SensorStatus = TEXT("FRESH / 1 SIM SECOND");
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAgvPublishedObstacleIndexTest,"Warehouse.Navigation.PublishedObstacleIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FAgvPublishedObstacleIndexTest::RunTest(const FString& Parameters)
{
	TArray<FAgvObservedObstacle> Obstacles;
	auto Add=[&](const FBox& Bounds) { Obstacles.AddDefaulted_GetRef().WorldBounds=Bounds; };
	// Many separate rack/column parts, a floor spanning the warehouse and a
	// column crossing floors exercise overlapping nodes and very large leaves.
	for (int32 X=-32;X<32;++X) for (int32 Y=-32;Y<32;++Y)
	{
		const FVector P(X*150.,Y*150.,((X+Y+64)%4)*400.);
		Add(FBox(P-FVector(20,30,0),P+FVector(20,30,200)));
	}
	Add(FBox(FVector(-6000,-6000,-20),FVector(6000,6000,0)));
	Add(FBox(FVector(195,-5,0),FVector(205,5,1600)));
	Add(FBox(FVector::ZeroVector,FVector::ZeroVector));
	Add(FBox(ForceInit)); // Even malformed published data must not be pruned.
	FAgvObstacleSpatialIndex Index;
	Index.Build(Obstacles);
	auto Check=[&](const FBox& Query)
	{
		FAgvObstacleIndices Actual,Expected;
		Index.Query(Query,Actual);ReferenceCandidates(Obstacles,Query,Expected);
		if (Actual!=Expected)
		{
			AddError(FString::Printf(TEXT("Spatial candidates differ from exhaustive published bounds for %s (%d vs %d)"),
				*Query.ToString(),Actual.Num(),Expected.Num()));
			return false;
		}
		return true;
	};
	FRandomStream Random(142807);
	for (int32 I=0;I<2000;++I)
	{
		const FVector P(Random.FRandRange(-6500,6500),Random.FRandRange(-6500,6500),Random.FRandRange(-30,1650));
		const FVector Half(Random.FRandRange(0,300),Random.FRandRange(0,300),Random.FRandRange(0,300));
		if (!Check(FBox(P-Half,P+Half))) return false;
	}
	for (const FVector& P : {FVector(20,30,200),FVector(6000,6000,0),FVector(200,0,800),FVector::ZeroVector})
		if (!Check(FBox(P,P))) return false; // Inclusive face/edge/corner contact.
	if (!Check(FBox(FVector(-10000),FVector(10000))) || !Check(FBox(ForceInit))) return false;
	FAgvObstacleIndices Before,After;
	const FBox Query(FVector(-10,-10,-1),FVector(10,10,1));
	Index.Query(Query,Before);
	TestTrue(TEXT("Small local query prunes most of 4096 fixed parts"),Before.Num()<20);
	// Source actors may move between publications. The index must continue to
	// expose the immutable old snapshot until an explicit rebuild, like sensors.
	for (auto& Entry:Obstacles) if (Entry.WorldBounds.IsValid) Entry.WorldBounds=Entry.WorldBounds.ShiftBy(FVector(20000,0,0));
	Index.Query(Query,After);
	TestTrue(TEXT("Unpublished source motion cannot change candidates"),Before==After);
	Index.Build(Obstacles);
	if (!Check(Query)) return false;
	Obstacles.RemoveAt(0,Obstacles.Num()/2);
	Index.Build(Obstacles);
	if (!Check(FBox(FVector(-10000),FVector(30000)))) return false;
	Index.Reset();Index.Query(Query,After);
	TestTrue(TEXT("Invalidating the snapshot removes all candidates"),After.IsEmpty());
	return true;
}
#endif
