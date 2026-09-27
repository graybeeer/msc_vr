#include "WarehouseCargo.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
AWarehouseCargo::AWarehouseCargo()
{
 Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CargoBody"));
 RootComponent=Body;
 Body->SetMobility(EComponentMobility::Movable);
 Body->SetCollisionProfileName(TEXT("PhysicsActor"));
 Tags.Add(TEXT("Carryable"));
}
void AWarehouseCargo::SetCargoMesh(UStaticMesh* Mesh)
{
 if (Mesh) Body->SetStaticMesh(Mesh);
}
void AWarehouseCargo::ConfigureMeshCollision(UStaticMesh* Mesh, bool Complex)
{
#if WITH_EDITOR
 if (Mesh && Mesh->GetBodySetup())
 {
  Mesh->GetBodySetup()->CollisionTraceFlag=Complex ? CTF_UseComplexAsSimple : CTF_UseSimpleAndComplex;
  Mesh->GetBodySetup()->InvalidatePhysicsData();
  Mesh->GetBodySetup()->CreatePhysicsMeshes();
  Mesh->MarkPackageDirty();
 }
#endif
}
