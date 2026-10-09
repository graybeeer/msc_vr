#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehousePhysics.h"
#include "WarehouseDamageSystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "CollisionQueryParams.h"
#include "PhysicsEngine/BodySetup.h"
#include "UObject/ConstructorHelpers.h"

AWarehousePallet::AWarehousePallet()
{
 RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Pallet(TEXT("/Game/Warehouse/Physics/SM_Ind_War_Storage_Pallet_Wood_Worn_01"));
 Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PalletMesh"));
 Body->SetupAttachment(RootComponent);
 Body->SetCollisionProfileName(TEXT("PhysicsActor"));
 Body->SetStaticMesh(Pallet.Object);
 if (auto* Prepared=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/Warehouse/Physics/SM_Pallet_110")))
 {
  Body->SetStaticMesh(Prepared);
 }
 else if (Pallet.Succeeded())
 {
  const FBox Bounds=Pallet.Object->GetBoundingBox();
  const FVector Scale=FVector(110,110,15)/Bounds.GetSize();
  Body->SetRelativeScale3D(Scale);
  Body->SetRelativeLocation(FVector(-Bounds.GetCenter().X,-Bounds.GetCenter().Y,-Bounds.Min.Z)*Scale);
 }
}
void AWarehousePallet::BeginPlay()
{
 Super::BeginPlay();
 // The physical mesh must also be the actor root, so navigation reads its actual pose.
 USceneComponent* PreviousRoot=RootComponent;
 Body->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
 SetRootComponent(Body);
 if (PreviousRoot!=Body) PreviousRoot->AttachToComponent(Body,FAttachmentTransformRules::KeepWorldTransform);
 WarehousePhysics::ConfigureContact(Body,EWarehouseSurface::Wood,FMath::Max(1.f,PalletMassKg)+FMath::Max(0.f,PayloadMassKg));
 if (!GetAttachParentActor()) Body->SetSimulatePhysics(true);
}
void AWarehousePallet::TransformCollision(UStaticMesh* Mesh,FTransform Transform)
{
#if WITH_EDITOR
 if (!Mesh || !Mesh->GetBodySetup()) return;
 for (auto& Hull : Mesh->GetBodySetup()->AggGeom.ConvexElems)
 {
  for (auto& Vertex : Hull.VertexData) Vertex=Transform.TransformPosition(Vertex);
  Hull.UpdateElemBox();
 }
 Mesh->GetBodySetup()->InvalidatePhysicsData(); Mesh->GetBodySetup()->CreatePhysicsMeshes(); Mesh->MarkPackageDirty();
#endif
}

void AWarehousePallet::FitCollisionBounds(UStaticMesh* Mesh)
{
#if WITH_EDITOR
 if (!Mesh || !Mesh->GetBodySetup()) return;
 const FBox Bounds=Mesh->GetBoundingBox();
 auto* Setup=Mesh->GetBodySetup();
 for (auto& Hull : Setup->AggGeom.ConvexElems)
 {
  // Voxel decomposition adds padding below worn feet; keep the floor contact exact.
  for (FVector& Vertex : Hull.VertexData) Vertex=Vertex.BoundToBox(Bounds.Min,Bounds.Max);
  Hull.UpdateElemBox();
 }
 Setup->InvalidatePhysicsData();
 Setup->CreatePhysicsMeshes();
 Mesh->MarkPackageDirty();
#endif
}

float AWarehousePallet::GetSupportLiftOffset(const FTransform& Frame) const
{
 // Sweep each full tine against this mesh; point samples miss worn board edges.
 float Contact=FLT_MAX;
 for (float Side : {-1.f,1.f})
 {
  const FVector Start=Frame.TransformPosition(FVector(57.5f,Side*25.f,6.5f));
  FHitResult Hit;
  if (!Body->SweepComponent(Hit,Start,Start+Frame.GetUnitAxis(EAxis::Z)*6.f,Frame.GetRotation(),FCollisionShape::MakeBox(FVector(57.5f,9.f,3.f)),false) || Hit.bStartPenetrating)
   return -1.f;
  Contact=FMath::Min(Contact,Hit.Time*6.f);
 }
 return Contact==FLT_MAX ? -1.f : FMath::Max(0.f,Contact-.05f);
}

bool AWarehousePallet::CanEngage(const FTransform& Frame) const
{
 if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->HasFailed(const_cast<AWarehousePallet*>(this))) return false;
 if (!Frame.GetScale3D().Equals(FVector::OneVector, .001f) || !GetActorScale3D().Equals(FVector::OneVector, .001f)) return false;
 if (FVector::DotProduct(Frame.GetUnitAxis(EAxis::X), GetActorForwardVector()) < FMath::Cos(FMath::DegreesToRadians(2.f)) ||
     FVector::DotProduct(Frame.GetUnitAxis(EAxis::Z), GetActorUpVector()) < FMath::Cos(FMath::DegreesToRadians(1.f))) return false;
 auto Local = [&](FVector P) { return GetActorTransform().InverseTransformPosition(Frame.TransformPosition(P)); };
 for (float Side : {-1.f, 1.f})
 {
  if (Local(FVector(0,Side*25,6.5)).X > -55 || Local(FVector(115,Side*25,6.5)).X < 40 || Local(FVector(115,Side*25,6.5)).X > 55.25) return false;
  for (float X : {0.f,115.f}) for (float Y : {-9.f,9.f}) for (float Z : {-3.f,3.f})
  {
   const FVector P = Local(FVector(X,Side*25+Y,6.5+Z));
   if (Side*P.Y < 6.25f || Side*P.Y > 42.75f || P.Z < 3.25f || P.Z > 10.25f) return false;
  }
 }
 return GetSupportLiftOffset(Frame)>=0;
}
