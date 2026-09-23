#include "WarehouseCargo.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

AWarehouseCargo::AWarehouseCargo()
{
	PrimaryActorTick.bCanEverTick = false;
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("CargoBody"));
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetCollisionProfileName(TEXT("PhysicsActor"));
	Body->SetSimulatePhysics(false);
	RootComponent = Body;

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CargoVisual"));
	Visual->SetupAttachment(Body);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Tags.Add(TEXT("Carryable"));
}

void AWarehouseCargo::SetCargoMesh(UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return;
	}
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const FVector WorldCenter = GetActorTransform().TransformPosition(Bounds.Origin);
	Visual->SetStaticMesh(Mesh);
	Visual->SetRelativeLocation(-Bounds.Origin);
	Body->SetBoxExtent(Bounds.BoxExtent);
	SetActorLocation(WorldCenter);
}
