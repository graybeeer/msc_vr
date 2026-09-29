#include "WarehouseDamageSystem.h"
#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "WarehouseChargingStation.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"

AWarehouseDamageSystem::AWarehouseDamageSystem()
{
 PrimaryActorTick.bCanEverTick = true;
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
void AWarehouseDamageSystem::InitializeStrength()
{
 Lookup.Reset();
 PrePhysicsVelocity.Reset();
 Supports.SetNum(Objects.Num());
 Bounds.SetNum(Objects.Num());
 Labels.SetNum(Objects.Num());
 for (int32 I=0; I<Objects.Num(); ++I)
 {
  auto& Spec=Objects[I];
  TArray<UStaticMeshComponent*> AllParts;
  for (AActor* Actor : Spec.Members) if (IsValid(Actor))
  {
   Lookup.Add(Actor,I);
   TArray<UStaticMeshComponent*> Parts;
   Actor->GetComponents(Parts);
   AllParts.Append(Parts);
   if (auto* Vehicle=Cast<AWarehouseForklift>(Actor)) { Spec.MassKg=Vehicle->VehicleMassKg; Spec.RatedLoadKg=Vehicle->RatedLoadKg; }
   if (auto* Pallet=Cast<AWarehousePallet>(Actor)) Spec.MassKg=Pallet->PalletMassKg;
  }
  for (auto* Part : AllParts)
  {
   Part->SetMassOverrideInKg(NAME_None,PhysicalMass(I)/FMath::Max(1,AllParts.Num()));
   PrePhysicsVelocity.Add(Part,Part->GetComponentVelocity());
   Part->SetNotifyRigidBodyCollision(true);
   Part->OnComponentHit.AddUniqueDynamic(this,&AWarehouseDamageSystem::OnHit);
  }
 }
 bInitialized=true;
 UpdateLoads();
}
void AWarehouseDamageSystem::Tick(float Dt)
{
 Super::Tick(Dt);
 for (auto& Entry : PrePhysicsVelocity) if (auto* Part=Entry.Key.Get())
  Entry.Value=Part->IsSimulatingPhysics() ? Part->GetPhysicsLinearVelocity() : Part->GetComponentVelocity();
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
   TArray<UStaticMeshComponent*> Parts;
   Actor->GetComponents(Parts);
   for (auto* Part : Parts) if (Part->IsVisible() && Part->IsCollisionEnabled()) Bounds[I]+=Part->Bounds.GetBox();
   if (auto* Pallet=Cast<AWarehousePallet>(Actor))
   {
    Spec.SupportedKg+=FMath::Max(0.f,Pallet->PayloadMassKg);
    for (auto* Part : Parts)
     if (FMath::Abs(Part->GetMass()-PhysicalMass(I))>.1f) Part->SetMassOverrideInKg(NAME_None,PhysicalMass(I));
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
  if (Spec.bFailed) continue;
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
     if (Parent!=INDEX_NONE && Parent!=I && !Objects[Parent].bFailed && Bounds[Parent].IsValid && Bounds[Parent].Min.Z<Box.Min.Z-.05f)
      Supports[I].AddUnique(Parent);
    }
   }
  };
  Sample(.13f);
  if (Supports[I].IsEmpty()) Sample(.015f);
  // A mechanically attached training pallet transfers load to the vehicle even without a trace contact.
  for (AActor* Actor : Spec.Members) if (IsValid(Actor) && Actor->GetAttachParentActor())
  {
   const int32 Parent=IndexOf(Actor->GetAttachParentActor());
   if (Parent!=INDEX_NONE) { Supports[I].Reset(); Supports[I].Add(Parent); break; }
  }
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
void AWarehouseDamageSystem::Contact(AActor* A,AActor* B,float EnergyJ)
{
 if (!A || !B || A==B) return;
 // Both hit delegates (and substeps) describe one contact; don't charge the damage twice.
 uint32 X=A->GetUniqueID(),Y=B->GetUniqueID();
 const uint64 Key=(uint64(FMath::Min(X,Y))<<32)|FMath::Max(X,Y);
 const double Now=GetWorld()->GetTimeSeconds();
 if (const double* Last=LastContact.Find(Key); Last && Now-*Last<.08) return;
 LastContact.Add(Key,Now);
 ApplyImpact(A,EnergyJ);
 ApplyImpact(B,EnergyJ);
}
void AWarehouseDamageSystem::ReportContact(AActor* Mover,const FHitResult& Hit,FVector VelocityCm,float MassKg)
{
 auto* System=Find(Mover);
 if (!System || Hit.bStartPenetrating || !Hit.GetActor()) return;
 if (!System->bInitialized) System->InitializeStrength();
 const float Speed=FMath::Max(0.f,-FVector::DotProduct(VelocityCm,Hit.ImpactNormal))*.01f;
 float ReducedMass=FMath::Max(.1f,MassKg);
 if (Hit.GetComponent() && Hit.GetComponent()->IsSimulatingPhysics())
 {
  const float OtherMass=FMath::Max(.1f,Hit.GetComponent()->GetMass());
  ReducedMass=ReducedMass*OtherMass/(ReducedMass+OtherMass);
 }
 System->Contact(Mover,Hit.GetActor(),.5f*ReducedMass*Speed*Speed);
}
void AWarehouseDamageSystem::OnHit(UPrimitiveComponent* Part,AActor* Other,UPrimitiveComponent* OtherPart,FVector Impulse,const FHitResult& Hit)
{
 if (!Part || !OtherPart || Impulse.IsNearlyZero()) return;
 const float A=Part->IsSimulatingPhysics() ? FMath::Max(.1f,Part->GetMass()) : 0;
 const float B=OtherPart->IsSimulatingPhysics() ? FMath::Max(.1f,OtherPart->GetMass()) : 0;
 const float Reduced=(A>0 && B>0) ? A*B/(A+B) : FMath::Max(A,B);
 if (Reduced<=0) return;
 // UE impulse is kg*cm/s. Approximate dissipated normal energy, not a measured material stress.
 const float J=Impulse.Size()*.01f;
 const FVector RelativeVelocity=PrePhysicsVelocity.FindRef(Part)-(PrePhysicsVelocity.Contains(OtherPart) ? PrePhysicsVelocity.FindRef(OtherPart) : OtherPart->GetComponentVelocity());
 const float Speed=FMath::Abs(FVector::DotProduct(RelativeVelocity,Hit.ImpactNormal))*.01f;
 // Solver depenetration/resting support impulses are not new kinetic energy.
 const float Energy=FMath::Min(J*J/(2.f*Reduced),.5f*Reduced*Speed*Speed);
 UE_LOG(LogTemp,Verbose,TEXT("WAREHOUSE_IMPACT %s -> %s: %.2f J, %.2f m/s"),*GetNameSafe(Part->GetOwner()),*GetNameSafe(Other),Energy,Speed);
 Contact(Part->GetOwner(),Other,Energy);
}
void AWarehouseDamageSystem::Release(int32 Index)
{
 auto& Spec=Objects[Index];
 if (Spec.Failure==EWarehouseFailure::Structure || Spec.Failure==EWarehouseFailure::Machine) return;
 for (int32 M=0; M<Spec.Members.Num(); ++M) if (AActor* Actor=Spec.Members[M]; IsValid(Actor))
 {
  Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
  TArray<UStaticMeshComponent*> Parts;
  Actor->GetComponents(Parts);
  for (auto* Part : Parts)
  {
   Part->SetMobility(EComponentMobility::Movable);
   if (Spec.PhysicsMeshes.IsValidIndex(M) && Spec.PhysicsMeshes[M]) Part->SetStaticMesh(Spec.PhysicsMeshes[M]);
   if (Spec.Failure==EWarehouseFailure::Rack && M<6)
   {
    // Buckled beams shorten enough to clear their original interlocking mounting plates.
    FVector Scale=Part->GetRelativeScale3D();
    const FVector Size=Part->GetStaticMesh()->GetBoundingBox().GetSize()*Scale.GetAbs();
    const int32 Axis=Size.X>Size.Y ? (Size.X>Size.Z ? 0:2) : (Size.Y>Size.Z ? 1:2);
    Scale[Axis]*=.94f;
    Part->SetRelativeScale3D(Scale);
   }
   if (Spec.Failure==EWarehouseFailure::Rack && M>=8) Part->AddWorldOffset(FVector(0,0,2));
   Part->SetCollisionProfileName(TEXT("PhysicsActor"));
   Part->SetMassOverrideInKg(NAME_None,PhysicalMass(Index)/FMath::Max(1,Spec.Members.Num()*Parts.Num()));
   Part->SetSimulatePhysics(true);
   Part->WakeAllRigidBodies();
  }
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
  if (Spec.Failure==EWarehouseFailure::Crush)
   for (AActor* Actor : Spec.Members) if (IsValid(Actor))
   {
    FVector Scale=Actor->GetActorScale3D(); Scale.Z*=.6f; Actor->SetActorScale3D(Scale);
   }
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
