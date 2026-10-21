#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "WarehouseBuildingLayout.generated.h"

class AStaticMeshActor;
class UMaterialInterface;
class UStaticMesh;

USTRUCT(BlueprintType)
struct FWarehouseMezzanineSettings
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bEnabled=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector2D OffsetCm=FVector2D(-400,0);
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector2D SizeCm=FVector2D(3600,3600);
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector2D AtriumSizeCm=FVector2D(1000,2600);
 // Flight centre in building-local XY. Yaw zero climbs toward local +X.
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector2D StairOffsetCm=FVector2D(-2280,-800);
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float StairYawDegrees=90.f;
};

USTRUCT(BlueprintType)
struct FWarehouseBuildingSettings
{
 GENERATED_BODY()
 FWarehouseBuildingSettings() { Floor4.bEnabled=false; }
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float WidthCm=4800.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float DepthCm=4000.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) float RoofHeightCm=1200.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FWarehouseMezzanineSettings Floor2;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FWarehouseMezzanineSettings Floor3;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FWarehouseMezzanineSettings Floor4;
};

/** Session-only building transaction. Merely opening its editor never changes the saved shell. */
UCLASS(BlueprintType)
class MSC_VR_API AWarehouseBuildingLayout : public AActor
{
 GENERATED_BODY()
public:
 AWarehouseBuildingLayout();
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 static AWarehouseBuildingLayout* Find(const AActor* Context);
 UFUNCTION(BlueprintCallable,Category="Building") void CaptureExistingLayout();
 UFUNCTION(BlueprintCallable,Category="Building") bool ApplySettings(const FWarehouseBuildingSettings& Proposed);
 UFUNCTION(BlueprintCallable,CallInEditor,Category="Building") bool RebuildGeometry();
 UFUNCTION(BlueprintPure,Category="Building") FWarehouseBuildingSettings GetSettings() const { return Settings; }
 UFUNCTION(BlueprintPure,Category="Building") FBox GetFootprint() const;
 FBox2D GetSourceFootprint() const { return CapturedFootprint; }
 UFUNCTION(BlueprintPure,Category="Building") bool ValidateSettings(const FWarehouseBuildingSettings& Proposed,FString& Reason) const;
 UFUNCTION(BlueprintPure,Category="Building") static FVector2D GetMinimumSizeCm() { return FVector2D(1600,1600); }
 UFUNCTION(BlueprintPure,Category="Building") static FVector2D GetMaximumSizeCm() { return FVector2D(9600,8000); }
 UFUNCTION(BlueprintPure,Category="Building") static float GetMaximumRoofHeightCm() { return 2000.f; }
 UFUNCTION(BlueprintPure,Category="Building") static int32 GetMaximumFloorCount() { return 4; }
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Building") FWarehouseBuildingSettings Settings;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Building") FString Status=TEXT("기존 건물 유지 · 1m 타일 단위 편집");
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Building") bool bHasAppliedChanges=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Building") int32 Revision=0;
 // Only architectural pieces belong here; racks, equipment, stock and people never do.
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Building|Sources") TArray<TObjectPtr<AActor>> ReplacedStructure;
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Building|Sources") TObjectPtr<AStaticMeshActor> FloorTemplate;
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Building|Sources") TObjectPtr<AStaticMeshActor> WallTemplate;
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Building|Sources") TObjectPtr<AStaticMeshActor> RoofTemplate;
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Building|Sources") TObjectPtr<AStaticMeshActor> ColumnTemplate;
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Building|Sources") TObjectPtr<AStaticMeshActor> BeamTemplate;
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Building|Sources") TObjectPtr<AStaticMeshActor> RailTemplate;
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Building|Sources") TObjectPtr<AStaticMeshActor> PipeTemplate;
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Building|Sources") TObjectPtr<AStaticMeshActor> TrayTemplate;
 UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Building|Sources") TObjectPtr<AStaticMeshActor> LightTemplate;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Building") TArray<TObjectPtr<AStaticMeshActor>> GeneratedStructure;
private:
 struct FOriginalState
 {
  TWeakObjectPtr<AActor> Actor;
  bool Hidden=false,Collision=true;
  struct FPart { TWeakObjectPtr<class UPrimitiveComponent> Part;bool Visible=true;ECollisionEnabled::Type Collision=ECollisionEnabled::QueryAndPhysics; };
  TArray<FPart> Parts;
 };
 TArray<FOriginalState> OriginalStates;
 UPROPERTY(Transient) TArray<TObjectPtr<AActor>> GeneratedLights;
 struct FExteriorContact
 {
  TWeakObjectPtr<AStaticMeshActor> Source;
  TWeakObjectPtr<class UStaticMeshComponent> Other;
  TWeakObjectPtr<UStaticMesh> SourceMesh,OtherMesh;
  FTransform SourceTransform,OtherTransform;
  FBox OtherBounds;
 };
 TArray<FExteriorContact> ExteriorContacts;
 FBox2D CapturedFootprint=FBox2D(FVector2D(-2400,-2000),FVector2D(2400,2000));
 bool bCaptured=false;
 bool ExteriorActuallyOverlaps(const AStaticMeshActor* Source,const AActor* Other,const FBox& ProposedWorld) const;
 bool PreservesExistingExteriorContact(const AStaticMeshActor* Source,const AActor* Other,const FBox& ProposedWorld,const FBox& OtherWorld) const;
 void HideOriginalStructure();
 void RestoreOriginalStructure();
};
