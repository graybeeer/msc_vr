#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WarehouseBuildingLayout.h"
#include "WarehouseConstructionEditor.generated.h"

class APlayerController;

UENUM(BlueprintType)
enum class EWarehouseConstructionTarget:uint8 { Building, Floor2, Floor3, Floor4, Stairs };

UENUM(BlueprintType)
enum class EWarehouseConstructionBrush:uint8 { Rectangle, AddTiles, RemoveTiles };

/** Rectangle gestures retain release-to-apply; tile strokes wait for explicit confirmation. */
UCLASS(BlueprintType)
class MSC_VR_API UWarehouseConstructionEditor:public UObject
{
 GENERATED_BODY()
public:
 void Initialize(APlayerController* Controller);
 UFUNCTION(BlueprintCallable,Category="Building editor") void SetActive(bool Active);
 UFUNCTION(BlueprintCallable,Category="Building editor") void SetTarget(EWarehouseConstructionTarget Value);
 UFUNCTION(BlueprintCallable,Category="Building editor") void SetStairFloor(int32 Floor);
 UFUNCTION(BlueprintCallable,Category="Building editor") void SetBrushMode(EWarehouseConstructionBrush Value);
 UFUNCTION(BlueprintCallable,Category="Building editor") bool ApplyPendingTiles();
 UFUNCTION(BlueprintCallable,Category="Building editor") void CancelPendingTiles();
 UFUNCTION(BlueprintPure,Category="Building editor") int32 GetPreviewTileCount() const { return PreviewCells.Num(); }
 UFUNCTION(BlueprintCallable,Category="Building editor") bool ToggleSelectedFloor();
 UFUNCTION(BlueprintCallable,Category="Building editor") bool AdjustRoofHeight(float DeltaCm);
 UFUNCTION(BlueprintCallable,Category="Building editor") bool RotateStairs(float DeltaDegrees);
 UFUNCTION(BlueprintCallable,Category="Building editor") bool BeginGestureAt(FVector WorldPoint,FVector2D ResizeHandle);
 UFUNCTION(BlueprintCallable,Category="Building editor") void MoveGestureAt(FVector WorldPoint);
 UFUNCTION(BlueprintCallable,Category="Building editor") bool EndGesture(bool Commit=true);
 UFUNCTION(BlueprintCallable,Category="Building editor") bool PointerDown(FVector2D Pixels);
 UFUNCTION(BlueprintCallable,Category="Building editor") void PointerMove(FVector2D Pixels);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Building editor") bool bActive=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Building editor") bool bDragging=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Building editor") EWarehouseConstructionTarget Target=EWarehouseConstructionTarget::Building;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Building editor") int32 StairFloor=1;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Building editor") EWarehouseConstructionBrush BrushMode=EWarehouseConstructionBrush::Rectangle;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Building editor") bool bHasPendingTiles=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Building editor") FWarehouseBuildingSettings Draft;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Building editor") FString Status;
 FVector GetOrigin() const;
 float GetPlaneHeight() const;
 FBox GetTargetBounds() const;
 FBox GetBuildingBounds() const;
 FBox GetAtriumBounds() const;
 TArray<FVector> GetResizeHandles() const;
 FString GetReadout() const;
 bool IsFloorTarget() const;
 bool IsTileBrush() const { return IsFloorTarget() && BrushMode!=EWarehouseConstructionBrush::Rectangle; }
 const TArray<TPair<FVector2D,FVector2D>>& GetPreviewEdges() const { return PreviewEdges; }
 const TArray<FIntPoint>& GetAddedTiles() const { return AddedTiles; }
 const TArray<FIntPoint>& GetRemovedTiles() const { return RemovedTiles; }
private:
 UPROPERTY(Transient) TObjectPtr<AWarehouseBuildingLayout> Building;
 TWeakObjectPtr<APlayerController> OwnerController;
 FWarehouseBuildingSettings Start;
 FVector GrabPoint=FVector::ZeroVector;
 FVector2D Handle=FVector2D::ZeroVector;
 bool bChanged=false;
 bool bPendingBeforeStroke=false;
 FIntPoint LastPaintedCell=FIntPoint::ZeroValue;
 TSet<FIntPoint> PreviewCells;
 TSet<FIntPoint> OriginalCells;
 TArray<TPair<FVector2D,FVector2D>> PreviewEdges;
 TArray<FIntPoint> AddedTiles,RemovedTiles;
 void RefreshDraft();
 bool ApplyDraft();
 void RefreshTilePreview(bool ResetOriginal);
 void RebuildTilePreview();
 bool PaintTile(FIntPoint Cell);
 void PaintTo(FVector WorldPoint);
 bool CanApplyImmediateAction();
 bool ProjectPointer(FVector2D Pixels,FVector& World) const;
 FWarehouseMezzanineSettings& SelectedFloor(FWarehouseBuildingSettings& Settings) const;
 const FWarehouseMezzanineSettings& SelectedFloor(const FWarehouseBuildingSettings& Settings) const;
};
