#include "WarehousePhysics.h"
#include "WarehouseCargo.h"
#include "WarehousePallet.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/StaticMesh.h"

bool WarehousePhysics::IsPhysicalContact(const UPrimitiveComponent* Component)
{
 return Component && Component->GetCollisionEnabled()!=ECollisionEnabled::NoCollision &&
  Component->GetCollisionProfileName()!=TEXT("Trigger") &&
  !Component->IsA<UWidgetComponent>() && !Component->IsA<UTextRenderComponent>() &&
  Component->GetPhysicsBodySetup()!=nullptr;
}

EWarehouseSurface WarehousePhysics::SurfaceFor(const UPrimitiveComponent* Component)
{
 if (!Component) return EWarehouseSurface::Steel;
 const AActor* Owner=Component->GetOwner();
 if (Cast<AWarehouseCargo>(Owner)) return EWarehouseSurface::Cardboard;
 if (Cast<AWarehousePallet>(Owner)) return EWarehouseSurface::Wood;
 FString Name=Component->GetName();
 if (Owner) Name+=Owner->GetActorNameOrLabel();
 if (const auto* Mesh=Cast<UStaticMeshComponent>(Component); Mesh && Mesh->GetStaticMesh()) Name+=Mesh->GetStaticMesh()->GetName();
 if (Name.Contains(TEXT("Wheel")) || Name.Contains(TEXT("Roller")) || Name.Contains(TEXT("Rubber"))) return EWarehouseSurface::Rubber;
 if (Name.Contains(TEXT("Cardboard")) || Name.Contains(TEXT("Carton"))) return EWarehouseSurface::Cardboard;
 if (Name.Contains(TEXT("Wood")) || Name.Contains(TEXT("Tree")) || Name.Contains(TEXT("Beech")) || Name.Contains(TEXT("Shrub"))) return EWarehouseSurface::Wood;
 if (Name.Contains(TEXT("Floor")) || Name.Contains(TEXT("Ground")) || Name.Contains(TEXT("Deck")) || Name.Contains(TEXT("Concrete")) || Name.Contains(TEXT("Road"))) return EWarehouseSurface::Concrete;
 return EWarehouseSurface::Steel;
}

void WarehousePhysics::ConfigureContact(UPrimitiveComponent* Component, EWarehouseSurface Surface, float MassKg)
{
 if (!IsPhysicalContact(Component)) return;
 // Keep Pawn/Vehicle/PhysicsBody identity: sensors depend on the owner-selected object type.
 Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
 for (ECollisionChannel Channel : {ECC_WorldStatic,ECC_WorldDynamic,ECC_Pawn,ECC_PhysicsBody,ECC_Vehicle,ECC_Destructible,ECC_Visibility})
  Component->SetCollisionResponseToChannel(Channel,ECR_Block);
 Component->SetEnableGravity(true);
 Component->SetUseCCD(true);
 Component->SetLinearDamping(.03f);
 Component->SetAngularDamping(.08f);
 if (FMath::IsFinite(MassKg) && MassKg>0) Component->SetMassOverrideInKg(NAME_None,FMath::Max(.1f,MassKg));
 Component->BodyInstance.SetOverrideIterationCounts(true);
 Component->BodyInstance.SetPositionSolverIterationCount(24);
 Component->BodyInstance.SetVelocitySolverIterationCount(12);
 Component->BodyInstance.SetProjectionSolverIterationCount(4);
 // Bound numerical penetration recovery only; ordinary free motion has no speed clamp.
 Component->BodyInstance.SetMaxDepenetrationVelocity(100.f);

 float Friction=.35f,Restitution=.05f;
 switch (Surface)
 {
 case EWarehouseSurface::Cardboard: Friction=.5f; Restitution=.08f; break;
 case EWarehouseSurface::Wood: Friction=.55f; Restitution=.06f; break;
 case EWarehouseSurface::Rubber: Friction=.9f; Restitution=.15f; break;
 case EWarehouseSurface::Concrete: Friction=.65f; Restitution=.02f; break;
 default: break;
 }
 // Chaos caches the material handle. Set final coefficients before its first use.
 auto* Material=NewObject<UPhysicalMaterial>(Component);
 Material->Friction=Friction;
 Material->StaticFriction=Friction*1.15f;
 Material->Restitution=Restitution;
 Material->bOverrideFrictionCombineMode=true;
 Material->FrictionCombineMode=EFrictionCombineMode::Average;
 Material->bOverrideRestitutionCombineMode=true;
 Material->RestitutionCombineMode=EFrictionCombineMode::Average;
 Component->SetPhysMaterialOverride(Material);
}
