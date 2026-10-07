#include "WarehouseCargo.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "EngineUtils.h"

namespace
{
 struct FCargoProfile
 {
  const TCHAR* Kind;
  FVector ShapeCm;
  float PackedDensity;
  bool Fragile;
 };
 // Editable training assumptions. Packed bulk density includes product voids and protective packing.
 const FCargoProfile CargoProfiles[] = {
  {TEXT("의류 / 면 티셔츠"), {48,34,32}, 100, false},
  {TEXT("서적 / 단행본"), {36,28,22}, 550, false},
  {TEXT("식품 / 건면"), {40,30,28}, 230, false},
  {TEXT("음료 / 생수"), {40,30,27}, 650, false},
  {TEXT("생활용품 / 세제"), {36,26,30}, 470, false},
  {TEXT("주방용품 / 식기"), {50,36,32}, 160, true},
  {TEXT("전자제품 / 공유기"), {46,32,26}, 110, true},
  {TEXT("기계부품 / 베어링"), {34,26,20}, 1100, false},
  {TEXT("전기자재 / 케이블"), {38,28,23}, 450, false},
  {TEXT("화장품 / 스킨케어"), {30,22,18}, 330, true},
  {TEXT("문구 / 복사용지"), {34,25,24}, 650, false},
  {TEXT("신발 / 운동화"), {52,34,28}, 85, false},
  {TEXT("식품 / 농산물"), {40,30,25}, 360, true},
  {TEXT("생활용품 / 수건"), {44,32,30}, 75, false},
  {TEXT("전자부품 / 센서"), {32,24,20}, 150, true},
  {TEXT("식품 / 통조림"), {36,28,24}, 700, false},
 };
}

int32 AWarehouseCargo::GetCargoProfileCount() { return UE_ARRAY_COUNT(CargoProfiles); }

FWarehouseCargoRecipe AWarehouseCargo::GenerateCargoRecipe(int32 DeliverySeed, int32 ItemIndex, int32 ProductIndex, FVector MaxSizeCm, float AmountScale)
{
 FWarehouseCargoRecipe Result;
 if (ItemIndex < 0 || ProductIndex < -1 || ProductIndex >= GetCargoProfileCount() ||
     MaxSizeCm.ContainsNaN() || MaxSizeCm.GetMin() < 5 || MaxSizeCm.GetMax() > 300 ||
     !FMath::IsFinite(AmountScale) || AmountScale <= 0 || AmountScale > 10) return Result;
 // Explicit fixed integer mixing keeps each item independent of generation order and other deliveries.
 uint32 Seed = static_cast<uint32>(DeliverySeed) ^ (static_cast<uint32>(ItemIndex) * 0x9e3779b9u);
 Seed ^= Seed >> 16; Seed *= 0x7feb352du; Seed ^= Seed >> 15; Seed *= 0x846ca68bu; Seed ^= Seed >> 16;
 FRandomStream Random(static_cast<int32>(Seed));
 const int32 Selected = Random.RandRange(0, GetCargoProfileCount()-1);
 Result.ProductIndex = ProductIndex < 0 ? Selected : ProductIndex;
 const FCargoProfile& Profile = CargoProfiles[Result.ProductIndex];
 Result.DeliverySeed = DeliverySeed;
 Result.ItemIndex = ItemIndex;
 Result.SizeLimitCm = MaxSizeCm;
 Result.AmountScale = AmountScale;
 Result.ProductKind = FText::FromString(Profile.Kind);
 Result.bFragile = Profile.Fragile;
 Result.PackedDensityKgM3 = Profile.PackedDensity * Random.FRandRange(.92f,1.08f);
 const double MaxFactor = FMath::Min(1.25, (MaxSizeCm / Profile.ShapeCm).GetMin());
 const double ReferenceVolume = Profile.ShapeCm.X * Profile.ShapeCm.Y * Profile.ShapeCm.Z / 1000000.;
 const double AvailableNetMass = Result.PackedDensityKgM3 * ReferenceVolume * FMath::Pow(MaxFactor,3.);
 const double DesiredNetMass = FMath::Min(22.,AvailableNetMass) * Random.FRandRange(.45f,1.f) * AmountScale;
 // When space is limited reduce the contents as well as the box, never shrink a fixed-mass package.
 const double Factor = FMath::Min(MaxFactor,FMath::Pow(DesiredNetMass / (Result.PackedDensityKgM3 * ReferenceVolume),1./3.));
 Result.SizeCm = Profile.ShapeCm * Factor;
 const FVector M = Result.SizeCm / 100.;
 Result.NetMassKg = Result.PackedDensityKgM3 * M.X * M.Y * M.Z;
 const double SurfaceM2 = 2 * (M.X*M.Y + M.X*M.Z + M.Y*M.Z);
 Result.PackagingMassKg = FMath::Max(.1 - Result.NetMassKg,.08 + SurfaceM2 * (Profile.Fragile ? .70 : .45));
 Result.GrossMassKg = Result.NetMassKg + Result.PackagingMassKg;
 Result.bValid = true;
 return Result;
}

bool AWarehouseCargo::ApplyCargoRecipe(const FWarehouseCargoRecipe& Recipe)
{
 if (!Recipe.bValid || Recipe.RuleVersion != 1 || Recipe.SizeCm.ContainsNaN() || Recipe.SizeCm.GetMin() <= 0 ||
     !FMath::IsFinite(Recipe.GrossMassKg) || Recipe.GrossMassKg < .1f || !Body->GetStaticMesh() || GetAttachParentActor()) return false;
 const FVector MeshSize = Body->GetStaticMesh()->GetBoundingBox().GetSize();
 if (MeshSize.GetMin() <= UE_SMALL_NUMBER) return false;
 const FBox Before = Body->Bounds.GetBox();
 const bool Simulating = Body->IsSimulatingPhysics();
 Body->SetSimulatePhysics(false);
 SetActorScale3D(Recipe.SizeCm / MeshSize);
 Body->UpdateBounds();
 const FBox After = Body->Bounds.GetBox();
 AddActorWorldOffset(FVector(Before.GetCenter().X-After.GetCenter().X,Before.GetCenter().Y-After.GetCenter().Y,Before.Min.Z-After.Min.Z),false,nullptr,ETeleportType::TeleportPhysics);
 Packing = Recipe;
 CargoKind = Recipe.ProductKind;
 SetGrossMassKg(Recipe.GrossMassKg);
 Body->SetSimulatePhysics(Simulating);
 if (Simulating) Body->WakeAllRigidBodies();
 return true;
}

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
 if (Packing.bValid)
  return FText::FromString(FString::Printf(TEXT("%s  |  %.2f kg\n%.0f × %.0f × %.0f cm · %s\n화물 %s · 포장 포함 무게"),
   *CargoKind.ToString(),GrossMassKg,Packing.SizeCm.X,Packing.SizeCm.Y,Packing.SizeCm.Z,
   Packing.bFragile ? TEXT("취급 주의") : TEXT("일반 포장"),*CargoId.ToString()));
 return FText::FromString(FString::Printf(TEXT("%s  |  %.2f kg\n화물 %s · 포장 포함 무게"),*CargoKind.ToString(),GrossMassKg,*CargoId.ToString()));
}
void AWarehouseCargo::BeginPlay()
{
 Super::BeginPlay();
 ConfigureCarryPhysics(Body,GrossMassKg);
 SetGrossMassKg(GrossMassKg);
 // Placed cargo used to remain kinematic until its first pickup/drop.
 if (!GetAttachParentActor())
 {
  Body->SetEnableGravity(true);
  Body->SetUseCCD(true);
  Body->SetSimulatePhysics(true);
 }
}
void AWarehouseCargo::ConfigureCarryPhysics(UStaticMeshComponent* Component, float MassKg)
{
 Component->SetCollisionProfileName(TEXT("PhysicsActor"));
 Component->SetEnableGravity(true); Component->SetUseCCD(true);
 Component->SetLinearDamping(.2f); Component->SetAngularDamping(.7f);
 Component->SetMassOverrideInKg(NAME_None,FMath::Max(.1f,MassKg));
 Component->BodyInstance.SetOverrideIterationCounts(true);
 Component->BodyInstance.SetPositionSolverIterationCount(16);
 Component->BodyInstance.SetVelocitySolverIterationCount(8);
 Component->BodyInstance.SetProjectionSolverIterationCount(4);
 Component->BodyInstance.SetMaxDepenetrationVelocity(100.f);
 auto* Material=NewObject<UPhysicalMaterial>(Component);
 Material->Friction=.65f; Material->Restitution=0.f;
 Material->bOverrideRestitutionCombineMode=true;
 Material->RestitutionCombineMode=EFrictionCombineMode::Min;
 Component->SetPhysMaterialOverride(Material);
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
