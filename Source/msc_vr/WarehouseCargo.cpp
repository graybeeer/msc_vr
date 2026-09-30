#include "WarehouseCargo.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "EngineUtils.h"
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
void AWarehouseCargo::SetGrossMassKg(float MassKg)
{
 GrossMassKg=FMath::IsFinite(MassKg) ? FMath::Max(.1f,MassKg) : 12.f;
 if (Body->GetStaticMesh()) Body->SetMassOverrideInKg(NAME_None,GrossMassKg);
}
void AWarehouseCargo::OnConstruction(const FTransform& Transform)
{
 Super::OnConstruction(Transform);
 SetGrossMassKg(GrossMassKg);
}
FText AWarehouseCargo::GetCargoDescription() const
{
 return FText::FromString(FString::Printf(TEXT("%s  |  %.2f kg\n화물 %s · 포장 포함 무게"),*CargoKind.ToString(),GrossMassKg,*CargoId.ToString()));
}
void AWarehouseCargo::BeginPlay()
{
 Super::BeginPlay();
 SetGrossMassKg(GrossMassKg);
 // Placed cargo used to remain kinematic until its first pickup/drop.
 if (!GetAttachParentActor())
 {
  Body->SetEnableGravity(true);
  Body->SetUseCCD(true);
  Body->SetSimulatePhysics(true);
 }
}
void AWarehouseCargo::WakeStackAbove()
{
 const FBox Removed=Body->Bounds.GetBox();
 for (TActorIterator<AWarehouseCargo> It(GetWorld()); It; ++It)
 {
  if (*It==this || It->GetAttachParentActor()) continue;
  auto* Other=It->GetCargoBody();
  const FBox Box=Other->Bounds.GetBox();
  if (Box.Max.Z>=Removed.Max.Z-2 && Box.Min.X<Removed.Max.X && Box.Max.X>Removed.Min.X &&
      Box.Min.Y<Removed.Max.Y && Box.Max.Y>Removed.Min.Y)
   Other->WakeAllRigidBodies();
 }
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
