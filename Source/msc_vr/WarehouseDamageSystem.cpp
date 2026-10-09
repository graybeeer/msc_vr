#include "WarehouseDamageSystem.h"
#include "WarehouseCargo.h"
#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "WarehouseChargingStation.h"
#include "WarehousePhysics.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
 bool OwnsPhysicalMass(const AActor* Actor)
 {
  return Actor && (Actor->ActorHasTag(TEXT("WarehousePhysicalVehicle")) || Actor->ActorHasTag(TEXT("WarehousePhysicalHuman")) || Actor->ActorHasTag(TEXT("WarehousePhysicalRig")));
 }
 UPrimitiveComponent* PhysicalComponent(UPrimitiveComponent* Part)
 {
  if (Part)
  {
   if (const auto* Body=Part->GetBodyInstance(NAME_None,true); Body && Body->OwnerComponent.IsValid()) return Body->OwnerComponent.Get();
  }
  return Part;
 }
 uint32 ContactBodyId(UPrimitiveComponent* Part,const AActor* Actor)
 {
  // Welded collision shapes describe one body, independent wheels remain separate.
  if (auto* Body=PhysicalComponent(Part)) return Body->GetUniqueID();
  return Actor->GetUniqueID();
 }
 double CollisionVolume(UPrimitiveComponent* Part)
 {
  double Volume=Part->GetPhysicsBodySetup() ? Part->GetPhysicsBodySetup()->GetScaledVolume(Part->GetComponentScale()) : 0;
  if (!FMath::IsFinite(Volume) || Volume<=0)
  {
   const FVector Extent=Part->Bounds.BoxExtent;
   Volume=8.*Extent.X*Extent.Y*Extent.Z;
  }
  return FMath::IsFinite(Volume) ? FMath::Max(.001,Volume) : .001;
 }
 void DistributeMass(const TArray<UPrimitiveComponent*>& Parts,float TotalKg)
 {
  double TotalVolume=0;
  for (auto* Part : Parts) TotalVolume+=CollisionVolume(Part);
  if (TotalVolume<=0) return;
  for (auto* Part : Parts)
   Part->SetMassOverrideInKg(NAME_None,float(TotalKg*CollisionVolume(Part)/TotalVolume));
 }
}

AWarehouseDamageSystem::AWarehouseDamageSystem()
{
 PrimaryActorTick.bCanEverTick = true;
 PrimaryActorTick.TickGroup=TG_PrePhysics;
 RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}
AWarehouseDamageSystem* AWarehouseDamageSystem::Find(const AActor* Context)
{
 if (Context) for (TActorIterator<AWarehouseDamageSystem> It(Context->GetWorld()); It; ++It) return *It;
 return nullptr;
}
int32 AWarehouseDamageSystem::IndexOf(const AActor* Actor) const
{
 const int32* Index = Lookup.Find(Actor);
 return Index ? *Index : INDEX_NONE;
}
float AWarehouseDamageSystem::PhysicalMass(int32 Index) const
{
 float Mass=FMath::Max(.1f,Objects[Index].MassKg);
 for (AActor* Actor : Objects[Index].Members)
  if (auto* Pallet=Cast<AWarehousePallet>(Actor)) Mass+=FMath::Max(0.f,Pallet->PayloadMassKg);
 return Mass;
}
bool AWarehouseDamageSystem::HasFailed(AActor* Object) const
{
 const int32 Index = IndexOf(Object);
 return Index != INDEX_NONE && Objects[Index].bFailed;
}
FBox AWarehouseDamageSystem::GetPhysicalBounds(AActor* Actor)
{
 FBox Bounds(ForceInit);
 if (!IsValid(Actor) || !Actor->GetActorEnableCollision()) return Bounds;
 TSet<const FBodyInstance*> Seen;
 auto AddBody=[&](FBodyInstance* Body)
 {
  while (Body && Body->WeldParent) Body=Body->WeldParent;
  if (!Body || Seen.Contains(Body) || !Body->IsValidBodyInstance()) return;
  Seen.Add(Body);
  const FBox NativeBounds=Body->GetBodyBounds();
  if (NativeBounds.IsValid) Bounds+=NativeBounds;
 };
 TArray<UPrimitiveComponent*> Parts; Actor->GetComponents(Parts);
 for (auto* Part : Parts)
 {
  if (!Part->IsCollisionEnabled() || Part->GetCollisionProfileName()==TEXT("Trigger")) continue;
  if (auto* Mesh=Cast<USkeletalMeshComponent>(Part))
   for (auto* Body : Mesh->Bodies) AddBody(Body);
  else AddBody(Part->GetBodyInstance(NAME_None,true));
 }
 return Bounds;
}
float AWarehouseDamageSystem::GetSupportedMass(AActor* Object) const
{
 const int32 Index = IndexOf(Object);
 return Index == INDEX_NONE ? 0.f : Objects[Index].SupportedKg;
}
void AWarehouseDamageSystem::BeginPlay()
{
 Super::BeginPlay();
 InitializeStrength();
}
AWarehouseDamageSystem::FBodyMotion AWarehouseDamageSystem::ReadMotion(UPrimitiveComponent* Part)
{
 FBodyMotion Result;
 Part=PhysicalComponent(Part);
 if (!Part) return Result;
 Result.Center=Part->GetComponentLocation();
 Result.Linear=Part->GetComponentVelocity();
 if (Part->IsSimulatingPhysics())
 {
  Result.Center=Part->GetCenterOfMass();
  Result.Linear=Part->GetPhysicsLinearVelocity();
  Result.Angular=Part->GetPhysicsAngularVelocityInRadians();
 }
 return Result;
}
void AWarehouseDamageSystem::RegisterContact(UPrimitiveComponent* Part)
{
 if (!WarehousePhysics::IsPhysicalContact(Part)) return;
 auto* Body=PhysicalComponent(Part);
 PrePhysicsMotion.Add(Body,ReadMotion(Body));
 Part->SetNotifyRigidBodyCollision(true);
 Part->OnComponentHit.AddUniqueDynamic(this,&AWarehouseDamageSystem::OnHit);
}
void AWarehouseDamageSystem::RegisterPhysicalContacts(AActor* Actor)
{
 if (!IsValid(Actor)) return;
 TArray<UPrimitiveComponent*> Parts; Actor->GetComponents(Parts);
 for (auto* Part : Parts) RegisterContact(Part);
}
void AWarehouseDamageSystem::InitializeStrength()
{
 Lookup.Reset();
 PrePhysicsMotion.Reset();
 Supports.SetNum(Objects.Num());
 Bounds.SetNum(Objects.Num());
 Labels.SetNum(Objects.Num());
 for (int32 I=0; I<Objects.Num(); ++I)
 {
  auto& Spec=Objects[I];
  TArray<UPrimitiveComponent*> AllParts;
  for (AActor* Actor : Spec.Members) if (IsValid(Actor))
  {
   Lookup.Add(Actor,I);
   TArray<UPrimitiveComponent*> Parts;
   Actor->GetComponents(Parts);
   if (!OwnsPhysicalMass(Actor))
    for (auto* Part : Parts) if (WarehousePhysics::IsPhysicalContact(Part)) AllParts.Add(Part);
   if (auto* Vehicle=Cast<AWarehouseForklift>(Actor)) { Spec.MassKg=Vehicle->VehicleMassKg; Spec.RatedLoadKg=Vehicle->RatedLoadKg; }
   if (auto* Pallet=Cast<AWarehousePallet>(Actor)) Spec.MassKg=Pallet->PalletMassKg;
   if (auto* Cargo=Cast<AWarehouseCargo>(Actor)) Spec.MassKg=Cargo->GrossMassKg;
  }
  DistributeMass(AllParts,PhysicalMass(I));
  for (int32 M=0;M<Spec.Members.Num();++M)
  {
   AActor* Actor=Spec.Members[M];
   if (!IsValid(Actor) || !Actor->ActorHasTag(TEXT("WarehousePortable"))) continue;
   TArray<UStaticMeshComponent*> Parts; Actor->GetComponents(Parts);
   for (auto* Part : Parts) if (WarehousePhysics::IsPhysicalContact(Part) && !Part->IsSimulatingPhysics())
   {
    UStaticMesh* Prepared=Part->GetStaticMesh();
    if (Spec.PhysicsMeshes.IsValidIndex(M) && Spec.PhysicsMeshes[M]) Prepared=Spec.PhysicsMeshes[M].Get();
    const UBodySetup* Setup=Prepared ? Prepared->GetBodySetup() : nullptr;
    if (!Setup || Setup->CollisionTraceFlag==CTF_UseComplexAsSimple || Setup->AggGeom.GetElementCount()==0)
    {
     UE_LOG(LogTemp,Error,TEXT("WAREHOUSE_PORTABLE_COLLISION_MISSING %s: prepare a simple physics mesh before play"),*Actor->GetName());
     continue;
    }
    // A loose object's saved placement is retained; Chaos settles its real contacts.
    Part->SetStaticMesh(Prepared);
    Part->SetMobility(EComponentMobility::Movable);
    Part->SetCollisionObjectType(ECC_PhysicsBody);
    WarehousePhysics::ConfigureContact(Part,WarehousePhysics::SurfaceFor(Part));
    Part->SetSimulatePhysics(true);
   }
  }
 }
 // Register contact on real primitives, including an owner's capsule/box physics root.
 // Render-only meshes, widgets, triggers and intentional NoCollision decorations stay untouched.
 for (TActorIterator<AActor> It(GetWorld());It;++It)
 {
  TArray<UPrimitiveComponent*> Parts; It->GetComponents(Parts);
  for (auto* Part : Parts) if (WarehousePhysics::IsPhysicalContact(Part))
  {
   if (!OwnsPhysicalMass(*It)) WarehousePhysics::ConfigureContact(Part,WarehousePhysics::SurfaceFor(Part));
   RegisterContact(Part);
  }
 }
 bInitialized=true;
 UpdateLoads();
}
void AWarehouseDamageSystem::Tick(float Dt)
{
 Super::Tick(Dt);
 for (auto& Entry : PrePhysicsMotion) if (auto* Part=Entry.Key.Get()) Entry.Value=ReadMotion(Part);
 AdvanceStrength(Dt);
}
void AWarehouseDamageSystem::UpdateLoads()
{
 TArray<int32> Order;
 TArray<TArray<float>> Levels;
 Levels.SetNum(Objects.Num());
 for (int32 I=0; I<Objects.Num(); ++I)
 {
  auto& Spec=Objects[I];
  Spec.SupportedKg=0; Spec.PeakLevelKg=0;
  Levels[I].SetNumZeroed(Spec.ShelfHeightsCm.Num());
  Bounds[I]=FBox(ForceInit);
  Supports[I].Reset();
  for (AActor* Actor : Spec.Members) if (IsValid(Actor))
  {
   if (auto* Cargo=Cast<AWarehouseCargo>(Actor)) Spec.MassKg=Cargo->GrossMassKg;
   TArray<UPrimitiveComponent*> Parts;
   Actor->GetComponents(Parts);
   for (auto* Part : Parts) if (WarehousePhysics::IsPhysicalContact(Part)) Bounds[I]+=Part->Bounds.GetBox();
   if (auto* Pallet=Cast<AWarehousePallet>(Actor))
   {
    Spec.MassKg=FMath::Max(1.f,Pallet->PalletMassKg);
    Spec.SupportedKg+=FMath::Max(0.f,Pallet->PayloadMassKg);
    // Virtual contents belong to the pallet body; visible cartons remain separate masses.
    auto* Body=Pallet->GetPalletBody();
    const float Mass=FMath::Max(1.f,Pallet->PalletMassKg)+FMath::Max(0.f,Pallet->PayloadMassKg);
    if (Body->IsSimulatingPhysics() && FMath::Abs(Body->GetMass()-Mass)>.1f) Body->SetMassOverrideInKg(NAME_None,Mass);
   }
  }
  if (Bounds[I].IsValid) Order.Add(I);
 }
 // Higher objects propagate their full load once: box stack -> pallet -> rack level -> floor.
 Order.Sort([&](int32 A,int32 B){return Bounds[A].Min.Z>Bounds[B].Min.Z;});
 for (int32 I : Order)
 {
  auto& Spec=Objects[I];
  const FBox& Box=Bounds[I];
  FCollisionQueryParams Params(SCENE_QUERY_STAT(WarehouseSupport),true);
  for (AActor* Actor : Spec.Members) if (IsValid(Actor)) Params.AddIgnoredActor(Actor);
  // ponytail: sampled support at 10 Hz; use Chaos contact manifolds for arbitrary rolling/tilting stacks.
  // Start inside the footprint; an open crate can support a box only along its narrow rim.
  auto Sample=[&](float Inset)
  {
   for (float X : {Inset,.5f,1.f-Inset}) for (float Y : {Inset,.5f,1.f-Inset})
   {
    FVector Point(FMath::Lerp(Box.Min.X,Box.Max.X,X),FMath::Lerp(Box.Min.Y,Box.Max.Y,Y),Box.Min.Z+1);
    FHitResult Hit;
    if (GetWorld()->LineTraceSingleByChannel(Hit,Point,Point-FVector(0,0,4),ECC_Visibility,Params))
    {
     const int32 Parent=IndexOf(Hit.GetActor());
     if (Parent!=INDEX_NONE && Parent!=I && Bounds[Parent].IsValid && Bounds[Parent].Min.Z<Box.Min.Z-.05f)
      Supports[I].AddUnique(Parent);
    }
   }
  };
  Sample(.13f);
  if (Supports[I].IsEmpty()) Sample(.015f);
  const float Share=(FMath::Max(.1f,Spec.MassKg)+Spec.SupportedKg)/FMath::Max(1,Supports[I].Num());
  for (int32 Parent : Supports[I])
  {
   Objects[Parent].SupportedKg+=Share;
   auto& Rack=Objects[Parent];
   if (Levels[Parent].Num())
   {
    int32 Nearest=0;
    for (int32 L=1; L<Levels[Parent].Num(); ++L)
     if (FMath::Abs(Rack.ShelfHeightsCm[L]-Box.Min.Z)<FMath::Abs(Rack.ShelfHeightsCm[Nearest]-Box.Min.Z)) Nearest=L;
    Levels[Parent][Nearest]+=Share;
    Rack.PeakLevelKg=FMath::Max(Rack.PeakLevelKg,Levels[Parent][Nearest]);
   }
  }
 }
}
void AWarehouseDamageSystem::AdvanceStrength(float Seconds)
{
 if (!FMath::IsFinite(Seconds) || Seconds<=0) return;
 if (!bInitialized) InitializeStrength();
 const float Dt=FMath::Min(Seconds,.1f);
 SinceScan+=Dt;
 if (SinceScan>=.099f) { UpdateLoads(); SinceScan=0; }
 for (int32 I=0; I<Objects.Num(); ++I)
 {
  auto& Spec=Objects[I];
  if (Spec.bFailed) continue;
  const float Capacity=FMath::Max(1.f,Spec.RatedLoadKg)*(1.f-.5f*Spec.Damage);
  float Ratio=Spec.SupportedKg/Capacity;
  if (Spec.LevelCapacityKg>0) Ratio=FMath::Max(Ratio,Spec.PeakLevelKg/(Spec.LevelCapacityKg*(1.f-.5f*Spec.Damage)));
  if (Ratio>1.f)
  {
   Spec.Damage=FMath::Clamp(Spec.Damage+FMath::Square(Ratio-1.f)*Dt/FMath::Max(.1f,Spec.OverloadSeconds),0.f,1.f);
   ShowDamage(I);
   if (Spec.Damage>=1.f) Fail(I);
  }
 }
}
void AWarehouseDamageSystem::ApplyImpact(AActor* Object,float EnergyJ)
{
 if (!FMath::IsFinite(EnergyJ) || EnergyJ<=0) return;
 if (!bInitialized) InitializeStrength();
 const int32 I=IndexOf(Object);
 if (I==INDEX_NONE || Objects[I].bFailed) return;
 auto& Spec=Objects[I];
 const float Yield=FMath::Max(1.f,Spec.ImpactYieldJ);
 if (EnergyJ<=Yield) return;
 Spec.Damage=FMath::Clamp(Spec.Damage+(EnergyJ-Yield)/FMath::Max(1.f,Spec.ImpactFailureJ-Yield),0.f,1.f);
 ShowDamage(I);
 if (Spec.Damage>=1.f) Fail(I);
}
void AWarehouseDamageSystem::Contact(AActor* A,AActor* B,float EnergyJ,UPrimitiveComponent* PartA,UPrimitiveComponent* PartB)
{
 if (!A || !B || A==B) return;
 // Both hit delegates (and substeps) describe one contact; don't charge the damage twice.
 uint32 X=ContactBodyId(PartA,A),Y=ContactBodyId(PartB,B);
 const uint64 Key=(uint64(FMath::Min(X,Y))<<32)|FMath::Max(X,Y);
 const double Now=GetWorld()->GetTimeSeconds();
 if (const double* Last=LastContact.Find(Key); Last && Now-*Last<.08) return;
 LastContact.Add(Key,Now);
 ApplyImpact(A,EnergyJ);
 ApplyImpact(B,EnergyJ);
}
void AWarehouseDamageSystem::OnHit(UPrimitiveComponent* Part,AActor* Other,UPrimitiveComponent* OtherPart,FVector Impulse,const FHitResult& Hit)
{
 if (!Part || !OtherPart || Impulse.IsNearlyZero()) return;
 auto* BodyA=PhysicalComponent(Part);
 auto* BodyB=PhysicalComponent(OtherPart);
 const float A=BodyA->IsSimulatingPhysics() ? FMath::Max(.1f,BodyA->GetMass()) : 0;
 const float B=BodyB->IsSimulatingPhysics() ? FMath::Max(.1f,BodyB->GetMass()) : 0;
 const float Reduced=(A>0 && B>0) ? A*B/(A+B) : FMath::Max(A,B);
 if (Reduced<=0) return;
 // UE impulse is kg*cm/s. Approximate dissipated normal energy, not a measured material stress.
 const float J=Impulse.Size()*.01f;
 const auto* BeforeA=PrePhysicsMotion.Find(BodyA);
 const auto* BeforeB=PrePhysicsMotion.Find(BodyB);
 const FVector Point=Hit.ImpactPoint;
 const FVector RelativeVelocity=(BeforeA ? *BeforeA : ReadMotion(BodyA)).At(Point)-(BeforeB ? *BeforeB : ReadMotion(BodyB)).At(Point);
 const float Speed=FMath::Abs(FVector::DotProduct(RelativeVelocity,Hit.ImpactNormal))*.01f;
 // Solver depenetration/resting support impulses are not new kinetic energy.
 const float Energy=FMath::Min(J*J/(2.f*Reduced),.5f*Reduced*Speed*Speed);
 UE_LOG(LogTemp,Verbose,TEXT("WAREHOUSE_IMPACT %s -> %s: %.2f J, %.2f m/s"),*GetNameSafe(Part->GetOwner()),*GetNameSafe(Other),Energy,Speed);
 Contact(Part->GetOwner(),Other,Energy,Part,OtherPart);
}
void AWarehouseDamageSystem::Release(int32 Index)
{
 auto& Spec=Objects[Index];
 if (Spec.Failure==EWarehouseFailure::Structure || Spec.Failure==EWarehouseFailure::Machine) return;
 TArray<UPrimitiveComponent*> Released;
 for (int32 M=0; M<Spec.Members.Num(); ++M) if (AActor* Actor=Spec.Members[M]; IsValid(Actor))
 {
  Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
  TArray<UStaticMeshComponent*> Parts;
  Actor->GetComponents(Parts);
  for (auto* Part : Parts) if (WarehousePhysics::IsPhysicalContact(Part))
  {
   Part->SetMobility(EComponentMobility::Movable);
   // Preserve an existing dynamic body's geometry and momentum when it loses strength.
   if (!Part->IsSimulatingPhysics() && Spec.PhysicsMeshes.IsValidIndex(M) && Spec.PhysicsMeshes[M]) Part->SetStaticMesh(Spec.PhysicsMeshes[M]);
   Part->SetCollisionObjectType(ECC_PhysicsBody);
   WarehousePhysics::ConfigureContact(Part,WarehousePhysics::SurfaceFor(Part));
   Released.Add(Part);
  }
 }
 DistributeMass(Released,PhysicalMass(Index));
 for (auto* Part : Released)
 {
  if (!Part->IsSimulatingPhysics()) Part->SetSimulatePhysics(true);
  Part->WakeAllRigidBodies();
  RegisterContact(Part);
 }
}
void AWarehouseDamageSystem::Fail(int32 Index)
{
 auto& Spec=Objects[Index];
 if (Spec.bFailed) return;
 Spec.bFailed=true;
 for (AActor* Actor : Spec.Members) if (auto* Station=Cast<AWarehouseChargingStation>(Actor)) Station->bMainsPower=false;
 if (Spec.Failure==EWarehouseFailure::Machine)
 {
  for (AActor* Actor : Spec.Members) if (auto* Vehicle=Cast<AWarehouseForklift>(Actor)) Vehicle->SetMechanicalFailure();
 }
 else if (Spec.Failure!=EWarehouseFailure::Structure)
 {
  // Remove the rack's joints: its existing beams/uprights become separate physical bodies.
  Release(Index);
  // Release every stacked dependent, not just the first pallet. Their next impacts can cause a cascade.
  TArray<int32> Falling; Falling.Add(Index);
  for (int32 At=0; At<Falling.Num(); ++At)
   for (int32 Child=0; Child<Supports.Num(); ++Child)
    if (!Falling.Contains(Child) && Supports[Child].Contains(Falling[At])) { Falling.Add(Child); Release(Child); }
 }
 ShowDamage(Index);
 UE_LOG(LogTemp,Warning,TEXT("WAREHOUSE_FAILURE %s: %.1f kg, level %.1f kg"),*Spec.Name,Spec.SupportedKg,Spec.PeakLevelKg);
}
void AWarehouseDamageSystem::ShowDamage(int32 Index)
{
 auto& Spec=Objects[Index];
 if (!Bounds.IsValidIndex(Index) || !Bounds[Index].IsValid) return;
 if (!Labels[Index])
 {
  auto* Text=NewObject<UTextRenderComponent>(this);
  Text->SetupAttachment(RootComponent);
  Text->SetHorizontalAlignment(EHTA_Center);
  Text->SetWorldSize(9);
  Text->RegisterComponent();
  Labels[Index]=Text;
 }
 auto* Text=Labels[Index].Get();
 Text->SetWorldLocation(Bounds[Index].GetCenter()+FVector(0,0,Bounds[Index].GetExtent().Z+12));
 Text->SetTextRenderColor(Spec.bFailed ? FColor::Red : FColor::Orange);
 Text->SetText(FText::FromString(FString::Printf(TEXT("%s\n%s %.0f%% | %.0f / %.0f kg"),*Spec.Name,Spec.bFailed ? TEXT("FAILED") : TEXT("DAMAGE"),Spec.Damage*100,Spec.SupportedKg,Spec.RatedLoadKg)));
}

TArray<AActor*> AWarehouseDamageSystem::GetSupportedActors(AActor* Object)
{
 if (!bInitialized) InitializeStrength();
 UpdateLoads();
 TArray<AActor*> Result;
 TArray<int32> Stack;
 const int32 Root=IndexOf(Object);
 if (Root==INDEX_NONE) return Result;
 Stack.Add(Root);
 for (int32 At=0; At<Stack.Num(); ++At)
  for (int32 Child=0; Child<Supports.Num(); ++Child)
   if (!Stack.Contains(Child) && Supports[Child].Contains(Stack[At]))
   {
    Stack.Add(Child);
    for (AActor* Actor : Objects[Child].Members) if (IsValid(Actor)) Result.Add(Actor);
   }
 return Result;
}
