#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseDamageSystem.generated.h"

class UStaticMesh;
class UPrimitiveComponent;
class UTextRenderComponent;
class UBodySetup;

UENUM(BlueprintType)
enum class EWarehouseFailure : uint8 { Machine, Rack, Crush, Rigid, Structure };

/** Editable training assumptions, not certified material/structural limits. Units: kg, joules, seconds. */
USTRUCT(BlueprintType)
struct FWarehouseStrength
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Name;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<AActor>> Members;
 // Parallel to Members; only used when releasing a previously static object into Chaos.
 UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TObjectPtr<UStaticMesh>> PhysicsMeshes;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) EWarehouseFailure Failure = EWarehouseFailure::Rigid;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1")) float MassKg = 20;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) float RatedLoadKg = 100;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float LevelCapacityKg = 0;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<float> ShelfHeightsCm;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) float ImpactYieldJ = 100;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) float ImpactFailureJ = 1000;
 // Nominal fatigue time; weakening accelerates damage as capacity is lost.
 UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1")) float OverloadSeconds = 10;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float SupportedKg = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float PeakLevelKg = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) float Damage = 0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly) bool bFailed = false;
};

UCLASS()
class MSC_VR_API AWarehouseDamageSystem : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseDamageSystem();
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Strength") TArray<FWarehouseStrength> Objects;
 UFUNCTION(BlueprintCallable, Category="Strength") void InitializeStrength();
 // Rebind layout floor replacements without resetting any mass, damage or body motion.
 void RefreshLayoutBindings();
 void RemoveLayoutActors(const TArray<AActor*>& Actors);
 void RegisterLayoutProfile(const FWarehouseStrength& Profile);
 // Call after a runtime physics activation (for example a humanoid ragdoll). No mass/profile changes.
 UFUNCTION(BlueprintCallable, Category="Strength") void RegisterPhysicalContacts(AActor* Actor);
 UFUNCTION(BlueprintCallable, Category="Strength") void AdvanceStrength(float Seconds);
 UFUNCTION(BlueprintCallable, Category="Strength") void ApplyImpact(AActor* Object, float EnergyJ);
 UFUNCTION(BlueprintPure, Category="Strength") float GetSupportedMass(AActor* Object) const;
 UFUNCTION(BlueprintPure, Category="Strength") bool HasFailed(AActor* Object) const;
 // Actual native collision bounds, including every skeletal body and each welded body once.
 // Returns an invalid box when the actor has no live collidable physics representation.
 UFUNCTION(BlueprintPure, Category="Physics") static FBox GetPhysicalBounds(AActor* Actor);
 // Expose the same contact predicate to Blueprint/import/validation tools.
 UFUNCTION(BlueprintPure, Category="Physics") static bool IsPhysicalContact(UPrimitiveComponent* Component);
 // Bulk inventory reads reuse one support snapshot instead of rescanning the scene for every pallet.
 UFUNCTION(BlueprintCallable, Category="Strength") TArray<AActor*> GetSupportedActors(AActor* Object,bool Refresh=true);
 // Periodic readers share the 10Hz snapshot. Explicit Refresh=true remains unconditional.
 UFUNCTION(BlueprintCallable, Category="Strength") void EnsureSupportSnapshot();
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Performance") int32 SupportSnapshotVersion=0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Performance") int32 LastSupportTraceCount=0;
 UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Performance") int32 LastSupportRelationsReused=0;
 static AWarehouseDamageSystem* Find(const AActor* Context);
private:
 struct FBodyMotion
 {
  FVector Linear=FVector::ZeroVector;
  FVector Angular=FVector::ZeroVector; // radians/s
  FVector Center=FVector::ZeroVector;
  FVector At(FVector Point) const { return Linear+FVector::CrossProduct(Angular,Point-Center); }
 };
 TMap<TWeakObjectPtr<AActor>, int32> Lookup;
 TMap<uint64, double> LastContact;
 TMap<TWeakObjectPtr<UPrimitiveComponent>, FBodyMotion> PrePhysicsMotion;
 TArray<TArray<int32>> Supports;
 // Parent -> children in profile index order, rebuilt with each exact support snapshot.
 TArray<TArray<int32>> SupportedChildren;
 TArray<FBox> Bounds;
 // Only rigid, unchanged query geometry may retain a sampled bearing relation.
 // Old/new bounds of every visibility blocker (including unregistered cargo)
 // invalidate nearby query footprints; explicit fresh reads still trace fully.
 struct FSupportGeometry
 {
  FTransform Transform;
  FBox Box=FBox(ForceInit);
  TWeakObjectPtr<UBodySetup> BodySetup;
  TWeakObjectPtr<UStaticMesh> Mesh;
  FGuid BodyGuid;
  uint64 Seen=0;
  uint8 TraceFlag=0;
 };
 TMap<TWeakObjectPtr<UPrimitiveComponent>,FSupportGeometry> SupportGeometry;
 TArray<TArray<TWeakObjectPtr<AActor>>> SupportMembers;
 TArray<TArray<int32>> BearingSupports;
 TArray<FBox> BearingBounds;
 uint64 SupportGeometryGeneration=0;
 UPROPERTY(Transient) TArray<TObjectPtr<UTextRenderComponent>> Labels;
 bool bInitialized = false;
 float SinceScan = 0;
 double LastSupportScanSeconds=-1;
 uint64 LastSupportScanFrame=MAX_uint64;
 int32 IndexOf(const AActor* Actor) const;
 float PhysicalMass(int32 Index) const;
 static FBodyMotion ReadMotion(UPrimitiveComponent* Part);
 void RegisterContact(UPrimitiveComponent* Part);
 bool RefreshSupportGeometry(TArray<FBox>& ChangedGeometry);
 void UpdateLoads(bool ForceFullTrace=false);
 void Fail(int32 Index);
 void Release(int32 Index);
 void ShowDamage(int32 Index);
 void Contact(AActor* A, AActor* B, float EnergyJ, UPrimitiveComponent* PartA=nullptr, UPrimitiveComponent* PartB=nullptr);
 UFUNCTION() void OnHit(UPrimitiveComponent* Part, AActor* Other, UPrimitiveComponent* OtherPart, FVector Impulse, const FHitResult& Hit);
};
