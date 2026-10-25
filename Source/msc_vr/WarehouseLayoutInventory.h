#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseCargo.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseLayoutEditor.h"
#include "WarehouseInboundZone.h"
#include "WarehouseLayoutInventory.generated.h"
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FWarehouseHeldCargo
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FName CargoId;
 UPROPERTY(BlueprintReadOnly) FText Product;
 UPROPERTY(BlueprintReadOnly) float MassKg=12;
 UPROPERTY(BlueprintReadOnly) FVector SizeCm=FVector(40,30,25);
 UPROPERTY() FWarehouseCargoRecipe Packing;
 UPROPERTY() TObjectPtr<UStaticMesh> Mesh;
 UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> Materials;
 UPROPERTY() FVector ActorScale=FVector::OneVector;
 UPROPERTY() FTransform BodyPose;
 UPROPERTY() FWarehouseStrength Strength;
};
USTRUCT()
struct FWarehouseLayoutPartTemplate
{
 GENERATED_BODY()
 UPROPERTY() FName Name;
 UPROPERTY() FTransform LocalPose;
 UPROPERTY() TObjectPtr<UStaticMesh> Mesh;
 UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> Materials;
 UPROPERTY() TEnumAsByte<ECollisionEnabled::Type> Collision=ECollisionEnabled::QueryAndPhysics;
 UPROPERTY() FCollisionResponseContainer Responses;
};
USTRUCT()
struct FWarehouseLayoutActorTemplate
{
 GENERATED_BODY()
 UPROPERTY() TSubclassOf<AActor> Class;
 UPROPERTY() FTransform LocalPose;
 UPROPERTY() TArray<FName> Tags;
 UPROPERTY() TMap<FName,FString> Properties;
 UPROPERTY() TArray<FWarehouseLayoutPartTemplate> Parts;
};
USTRUCT()
struct FWarehouseLayoutProfileTemplate
{
 GENERATED_BODY()
 UPROPERTY() FWarehouseStrength Strength;
 UPROPERTY() TArray<int32> Members;
};
USTRUCT()
struct FWarehouseLayoutPrototype
{
 GENERATED_BODY()
 UPROPERTY() FString Name;
 UPROPERTY() EWarehouseLayoutCategory Category=EWarehouseLayoutCategory::Other;
 UPROPERTY() TArray<FWarehouseLayoutActorTemplate> Actors;
 UPROPERTY() TArray<FWarehouseLayoutProfileTemplate> Profiles;
 UPROPERTY() TArray<FWarehouseRackSlot> RackSlots;
 UPROPERTY() FVector ZoneHalfSize=FVector::ZeroVector;
 UPROPERTY() bool bOutbound=false;
 UPROPERTY() TArray<FTransform> ZoneDestinations;
};

/** Session data; removed parcels have no live world actor and cannot enter inventory or jobs. */
UCLASS()
class MSC_VR_API AWarehouseLayoutInventory : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseLayoutInventory();
 static AWarehouseLayoutInventory* Find(const AActor* Context);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Held cargo") TArray<FWarehouseHeldCargo> HeldCargo;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Held cargo") FString LastMessage;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Held cargo") int32 Revision=0;
 UPROPERTY(EditAnywhere,Category="Held cargo") TSoftObjectPtr<UStaticMesh> DefaultCargoMesh;
 UPROPERTY(EditAnywhere,Category="Held cargo") FWarehouseStrength NewCargoStrength;
 UFUNCTION(BlueprintCallable,Category="Held cargo") bool CreateHeldCargo(FName Id,FText Product,float MassKg,FVector SizeCm);
 UFUNCTION(BlueprintCallable,Category="Held cargo") bool DeleteHeldCargo(FName Id);
 UFUNCTION(BlueprintCallable,Category="Held cargo") bool HoldCargo(AWarehouseCargo* Cargo);
 bool ConsumeHeldCargo(FName Id);
 AWarehouseCargo* PreviewHeldCargo(FVector Point);
 void CapturePrototypes(AWarehouseLayoutEditor* Layout);
 TArray<FString> Choices(EWarehouseLayoutCategory Category) const;
 FVector PreviewZoneSize(const TArray<AActor*>& Members) const;
 TArray<AActor*> SpawnPrototype(EWarehouseLayoutCategory Category,int32 Choice,FVector Point);
 void CommitPrototype(const TArray<AActor*>& Members);
 void ForgetActors(const TArray<AActor*>& Members);
 void RestoreExperimentCatalogue() { bCaptured=!Prototypes.IsEmpty();PreviewPrototypes.Reset();++Revision; }
private:
 UPROPERTY() TArray<FWarehouseLayoutPrototype> Prototypes;
 // Local rack poses survive deletion of every inbound dispatcher and follow
 // the rack transform automatically when that rack is rearranged.
 UPROPERTY() TArray<FWarehouseRackSlot> RackCatalogue;
 TMap<TWeakObjectPtr<AActor>,int32> PreviewPrototypes;
 bool bCaptured=false;
 void Capture(EWarehouseLayoutCategory Category,const FString& Name,const TArray<AActor*>& Members,FVector Origin,
              const FWarehouseLayoutZone* Zone=nullptr);
};
