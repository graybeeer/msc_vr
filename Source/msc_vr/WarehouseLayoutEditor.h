#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarehouseLayoutEditor.generated.h"

UENUM(BlueprintType)
enum class EWarehouseLayoutCategory:uint8 { Rack, Forklift, Elevator, Zone, Other, Charger, WaitingBay, Cargo, Pallet };
UENUM(BlueprintType)
enum class EWarehouseLayoutAction:uint8 { Move, Delete, Add };

USTRUCT(BlueprintType)
struct FWarehouseLayoutZone
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FString Name;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector Centre=FVector::ZeroVector;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector HalfSize=FVector::ZeroVector;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<TObjectPtr<AActor>> Members;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FTransform> Destinations;
};

/** Runtime layout experiment transaction. Never writes the saved level. */
UCLASS()
class MSC_VR_API AWarehouseLayoutEditor:public AActor
{
 GENERATED_BODY()
public:
 AWarehouseLayoutEditor();
 static AWarehouseLayoutEditor* Find(const AActor* Context);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") TArray<FWarehouseLayoutZone> Zones;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") bool bEditing=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") EWarehouseLayoutCategory Category=EWarehouseLayoutCategory::Rack;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") EWarehouseLayoutAction Action=EWarehouseLayoutAction::Move;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") int32 AddChoice=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") FString Status;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") bool bDragging=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") int32 MovedCount=0;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") int32 SelectionCount=0;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") bool bEdgeSnapEnabled=true;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") bool bMarqueeSelecting=false;
 // Viewport pixels; the UI overlay applies its normal DPI conversion.
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") FVector2D MarqueeStart=FVector2D::ZeroVector;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Layout") FVector2D MarqueeEnd=FVector2D::ZeroVector;
 UFUNCTION(BlueprintCallable,Category="Layout") void SetEditing(bool Enabled,int32 Floor);
 UFUNCTION(BlueprintCallable,Category="Layout") void SetCategory(EWarehouseLayoutCategory Value);
 UFUNCTION(BlueprintCallable,Category="Layout") void SetAction(EWarehouseLayoutAction Value);
 UFUNCTION(BlueprintCallable,Category="Layout") void CycleAddChoice();
 UFUNCTION(BlueprintPure,Category="Layout") FString GetAddChoiceLabel() const;
 UFUNCTION(BlueprintCallable,Category="Layout") bool DeleteSelection();
 UFUNCTION(BlueprintCallable,Category="Layout") bool BeginAdd(FVector FloorPoint);
 UFUNCTION(BlueprintCallable,Category="Layout") void UpdateSurfaceDrag(FVector FloorPoint,AActor* Support,FVector SurfacePoint);
 UFUNCTION(BlueprintCallable,Category="Layout") void SetSelection(const TArray<AActor*>& Actors);
 UFUNCTION(BlueprintPure,Category="Layout") TArray<AActor*> GetSelection() const;
 FBox GetSelectionBounds(AActor* Actor) const { return GroupBounds(Actor); }
 UFUNCTION(BlueprintCallable,Category="Layout") void ClearSelection();
 UFUNCTION(BlueprintCallable,Category="Layout") bool SelectScreenRectangle(FVector2D Start,FVector2D End,bool Additive=false);
 UFUNCTION(BlueprintCallable,Category="Layout") bool BeginDrag(AActor* HitActor,FVector FloorPoint);
 UFUNCTION(BlueprintCallable,Category="Layout") void UpdateDrag(FVector FloorPoint);
 UFUNCTION(BlueprintCallable,Category="Layout") bool EndDrag(bool Commit=true);
 // Same viewport input path for Slate and Blueprint/Python validation tools.
 UFUNCTION(BlueprintCallable,Category="Layout") void PointerDown(class APlayerController* PC,FVector2D ViewportPixels,bool Additive);
 UFUNCTION(BlueprintCallable,Category="Layout") void PointerMove(class APlayerController* PC,FVector2D ViewportPixels);
 UFUNCTION(BlueprintCallable,Category="Layout") void PointerUp(bool Commit=true);
 void MouseTick(class APlayerController* PC);
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 struct FPart
 {
  TWeakObjectPtr<class USceneComponent> Component;
  FTransform Pose;
  TEnumAsByte<EComponentMobility::Type> Mobility;
  ECollisionEnabled::Type Collision=ECollisionEnabled::NoCollision;
  bool Simulated=false;
  bool Welded=false;
  TWeakObjectPtr<class USceneComponent> Parent;
  FName Socket;
  bool Joint=false;
  FTransform Frame1,Frame2;
  FVector Linear,Angular;
 };
 struct FMember { TWeakObjectPtr<AActor> Actor;FTransform Pose;TArray<FPart> Parts;bool Collision=true; };
 TArray<FMember> Moving;
 UPROPERTY(Transient) TArray<TObjectPtr<AActor>> SelectedActors;
 TArray<FBox> OriginalAreas;
 TArray<int32> MovingZones;
 FBox OriginalBounds{ForceInit};
 FBox OriginalSnapBounds{ForceInit};
 FVector GrabPoint,Delta;
 int32 FloorIndex=0;
 bool WasPaused=false,MouseWasDown=false,MarqueeAdditive=false;
 bool bAdding=false;
 UPROPERTY(Transient) TArray<TObjectPtr<AActor>> AddedActors;
 FName AddedCargoId;
 void RefreshAfterEdit();
 bool Eligible(AActor* Actor) const;
 bool ParkedAtCharger(class AWarehouseForklift* Vehicle,class AWarehouseChargingStation* Station) const;
 AActor* SelectionActor(AActor* HitActor) const;
 FBox GroupBounds(AActor* Actor) const;
 int32 ZoneForActor(AActor* Actor) const;
 int32 ZoneAt(FVector FloorPoint) const;
 bool IsSelected(AActor* Actor) const;
 void ToggleSelection(AActor* Actor);
 bool PlacementClear();
 void Restore(bool Rollback);
 void UpdateReferences();
};
