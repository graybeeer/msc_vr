#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseDamageSystem.generated.h"

class UStaticMesh;
class UPrimitiveComponent;
class UTextRenderComponent;

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
 UFUNCTION(BlueprintCallable, Category="Strength") void AdvanceStrength(float Seconds);
 UFUNCTION(BlueprintCallable, Category="Strength") void ApplyImpact(AActor* Object, float EnergyJ);
 UFUNCTION(BlueprintPure, Category="Strength") float GetSupportedMass(AActor* Object) const;
 UFUNCTION(BlueprintPure, Category="Strength") bool HasFailed(AActor* Object) const;
 UFUNCTION(BlueprintCallable, Category="Strength") TArray<AActor*> GetSupportedActors(AActor* Object);
 static AWarehouseDamageSystem* Find(const AActor* Context);
 static void ReportContact(AActor* Mover, const FHitResult& Hit, FVector VelocityCm, float MassKg);
private:
 TMap<TWeakObjectPtr<AActor>, int32> Lookup;
 TMap<uint64, double> LastContact;
 TMap<TWeakObjectPtr<UPrimitiveComponent>, FVector> PrePhysicsVelocity;
 TArray<TArray<int32>> Supports;
 TArray<FBox> Bounds;
 UPROPERTY(Transient) TArray<TObjectPtr<UTextRenderComponent>> Labels;
 bool bInitialized = false;
 float SinceScan = 0;
 int32 IndexOf(const AActor* Actor) const;
 float PhysicalMass(int32 Index) const;
 void UpdateLoads();
 void Fail(int32 Index);
 void Release(int32 Index);
 void ShowDamage(int32 Index);
 void Contact(AActor* A, AActor* B, float EnergyJ);
 UFUNCTION() void OnHit(UPrimitiveComponent* Part, AActor* Other, UPrimitiveComponent* OtherPart, FVector Impulse, const FHitResult& Hit);
};
