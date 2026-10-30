#include "WarehouseConstructionEditor.h"
#include "GameFramework/PlayerController.h"

void UWarehouseConstructionEditor::Initialize(APlayerController* Controller) { OwnerController=Controller; }
void UWarehouseConstructionEditor::RefreshDraft()
{
 if (!IsValid(Building) && OwnerController.IsValid()) Building=AWarehouseBuildingLayout::Find(OwnerController.Get());
 if (Building) Draft=Building->GetSettings();
 bHasPendingTiles=false;RefreshTilePreview(true);
}
void UWarehouseConstructionEditor::SetActive(bool Active)
{
 if (Active==bActive) return;
 CancelPendingTiles();bActive=Active;
 if (Active) { RefreshDraft();Status=TEXT("1m 격자 · 크기·위치는 드래그 · 자유형상 타일은 여러 번 그린 뒤 편집 적용"); }
}
void UWarehouseConstructionEditor::SetTarget(EWarehouseConstructionTarget Value)
{
 if (int32(Value)>int32(EWarehouseConstructionTarget::Stairs)) return;
 if (Value==Target) return;
 const bool Discarded=bHasPendingTiles;CancelPendingTiles();Target=Value;RefreshDraft();
 if (Value>=EWarehouseConstructionTarget::Floor2 && Value<=EWarehouseConstructionTarget::Floor4) StairFloor=int32(Value);
 Status=Value==EWarehouseConstructionTarget::Building ? TEXT("건물 외곽의 네모 손잡이를 드래그하세요. 최대 96 × 80m, 높이 20m입니다.") :
  Value==EWarehouseConstructionTarget::Stairs ? TEXT("계단 범위를 잡아 이동하세요. 선택층의 계단만 이동하며 회전 버튼으로 방향을 바꿉니다.") :
  TEXT("복층 크기·위치는 드래그, 자유형상은 타일 추가·제거로 그린 뒤 편집 적용을 누르세요.");
 if (Discarded) Status=TEXT("미확정 타일 편집 취소 · ")+Status;
}
void UWarehouseConstructionEditor::SetStairFloor(int32 Floor)
{ CancelPendingTiles();StairFloor=FMath::Clamp(Floor,1,3);RefreshDraft(); }
bool UWarehouseConstructionEditor::IsFloorTarget() const
{ return Target>=EWarehouseConstructionTarget::Floor2 && Target<=EWarehouseConstructionTarget::Floor4; }
void UWarehouseConstructionEditor::SetBrushMode(EWarehouseConstructionBrush Value)
{
 if (int32(Value)>int32(EWarehouseConstructionBrush::RemoveTiles) || !bActive || !IsFloorTarget()) return;
 EndGesture(false);
 if (Value==EWarehouseConstructionBrush::Rectangle && bHasPendingTiles)
 { Status=TEXT("타일 편집을 적용하거나 취소한 뒤 크기·위치를 바꾸세요.");return; }
 BrushMode=Value;
 if (!bHasPendingTiles) RefreshDraft();
 Status=Value==EWarehouseConstructionBrush::Rectangle ? TEXT("직사각형은 크기·위치 조절, 자유형상은 모양을 유지하며 이동합니다.") :
  TEXT("1m 타일을 드래그하여 그리세요 · 초록은 추가, 빨강은 제거 · 편집 적용 전에는 실제 구조가 바뀌지 않습니다.");
}
void UWarehouseConstructionEditor::CancelPendingTiles()
{
 bDragging=false;bChanged=false;bPendingBeforeStroke=false;RefreshDraft();
 Status=TEXT("미확정 타일 편집 취소 · 기존 바닥·난간·장식 유지");
}
bool UWarehouseConstructionEditor::ApplyPendingTiles()
{
 if (!bActive || !IsFloorTarget() || !IsValid(Building)) return false;
 if (bDragging) EndGesture(true);
 if (!bHasPendingTiles) { Status=TEXT("아직 적용할 타일 변경이 없습니다.");return false; }
 const bool Accepted=Building->ApplySettings(Draft);Status=Building->Status;
 // A rejected transaction keeps the editable draft, so the user can repair it.
 if (Accepted) RefreshDraft();
 return Accepted;
}
bool UWarehouseConstructionEditor::CanApplyImmediateAction()
{
 EndGesture(false);
 if (!bHasPendingTiles) return true;
 Status=TEXT("현재 타일 편집을 먼저 적용하거나 취소하세요.");return false;
}
FWarehouseMezzanineSettings& UWarehouseConstructionEditor::SelectedFloor(FWarehouseBuildingSettings& Settings) const
{
 const int32 Floor=Target==EWarehouseConstructionTarget::Stairs ? StairFloor : int32(Target);
 return Floor==2 ? Settings.Floor3 : Floor==3 ? Settings.Floor4 : Settings.Floor2;
}
const FWarehouseMezzanineSettings& UWarehouseConstructionEditor::SelectedFloor(const FWarehouseBuildingSettings& Settings) const
{
 const int32 Floor=Target==EWarehouseConstructionTarget::Stairs ? StairFloor : int32(Target);
 return Floor==2 ? Settings.Floor3 : Floor==3 ? Settings.Floor4 : Settings.Floor2;
}
bool UWarehouseConstructionEditor::ApplyDraft()
{
 if (!bActive || !IsValid(Building)) return false;
 const bool Accepted=Building->ApplySettings(Draft);Status=Building->Status;RefreshDraft();return Accepted;
}
bool UWarehouseConstructionEditor::ToggleSelectedFloor()
{
 if (!bActive || Target==EWarehouseConstructionTarget::Building) return false;
 if (!CanApplyImmediateAction()) return false;RefreshDraft();auto& Floor=SelectedFloor(Draft);Floor.bEnabled=!Floor.bEnabled;
 if (Floor.bEnabled) Draft.RoofHeightCm=FMath::Max(Draft.RoofHeightCm,float((Target==EWarehouseConstructionTarget::Stairs ? StairFloor : int32(Target))*400+400));
 return ApplyDraft();
}
bool UWarehouseConstructionEditor::AdjustRoofHeight(float DeltaCm)
{
 if (!bActive || !FMath::IsFinite(DeltaCm)) return false;
 if (!CanApplyImmediateAction()) return false;RefreshDraft();Draft.RoofHeightCm=FMath::GridSnap(Draft.RoofHeightCm+DeltaCm,100.f);return ApplyDraft();
}
bool UWarehouseConstructionEditor::RotateStairs(float DeltaDegrees)
{
 if (!bActive || !FMath::IsFinite(DeltaDegrees)) return false;
 if (!CanApplyImmediateAction()) return false;RefreshDraft();SelectedFloor(Draft).StairYawDegrees=FRotator::NormalizeAxis(SelectedFloor(Draft).StairYawDegrees+DeltaDegrees);return ApplyDraft();
}
void UWarehouseConstructionEditor::RefreshTilePreview(bool ResetOriginal)
{
 PreviewCells.Reset();
 if (IsFloorTarget() && SelectedFloor(Draft).bEnabled)
  for (FIntPoint Cell:AWarehouseBuildingLayout::GetMezzanineTiles(SelectedFloor(Draft))) PreviewCells.Add(Cell);
 if (ResetOriginal) OriginalCells=PreviewCells;
 RebuildTilePreview();
}
void UWarehouseConstructionEditor::RebuildTilePreview()
{
 // Cache boundary segments only when a brush changes cells, never generate a
 // building plan or run collision queries from Slate's per-frame paint path.
 PreviewEdges.Reset();AddedTiles.Reset();RemovedTiles.Reset();
 TMap<int32,TArray<int32>> Horizontal,Vertical;
 for (FIntPoint Cell:PreviewCells)
 {
  if (!PreviewCells.Contains(Cell+FIntPoint(0,-1))) Horizontal.FindOrAdd(Cell.Y).Add(Cell.X);
  if (!PreviewCells.Contains(Cell+FIntPoint(0,1))) Horizontal.FindOrAdd(Cell.Y+1).Add(Cell.X);
  if (!PreviewCells.Contains(Cell+FIntPoint(-1,0))) Vertical.FindOrAdd(Cell.X).Add(Cell.Y);
  if (!PreviewCells.Contains(Cell+FIntPoint(1,0))) Vertical.FindOrAdd(Cell.X+1).Add(Cell.Y);
  if (!OriginalCells.Contains(Cell)) AddedTiles.Add(Cell);
 }
 for (FIntPoint Cell:OriginalCells) if (!PreviewCells.Contains(Cell)) RemovedTiles.Add(Cell);
 auto Merge=[&](TMap<int32,TArray<int32>>& Lines,bool AlongX)
 {
  for (auto& Row:Lines)
  {
   Row.Value.Sort();
   for (int32 Index=0;Index<Row.Value.Num();)
   {
    const int32 First=Row.Value[Index];int32 Last=First;++Index;
    while (Index<Row.Value.Num() && Row.Value[Index]==Last+1) Last=Row.Value[Index++];
    PreviewEdges.Emplace(AlongX ? FVector2D(First*100.,Row.Key*100.) : FVector2D(Row.Key*100.,First*100.),
                         AlongX ? FVector2D((Last+1)*100.,Row.Key*100.) : FVector2D(Row.Key*100.,(Last+1)*100.));
   }
  }
 };
 Merge(Horizontal,true);Merge(Vertical,false);
}
bool UWarehouseConstructionEditor::PaintTile(FIntPoint Cell)
{
 const double MinX=Cell.X*100.,MinY=Cell.Y*100.;
 if (MinX<-Draft.WidthCm*.5 || MinY<-Draft.DepthCm*.5 || MinX+100>Draft.WidthCm*.5 || MinY+100>Draft.DepthCm*.5) return false;
 if (BrushMode==EWarehouseConstructionBrush::AddTiles)
 {
  // Match native floor clearance. Removal remains possible at the shell edge
  // so a draft or imported layout can always erase an invalid boundary cell.
  if (MinX<-Draft.WidthCm*.5+20 || MinY<-Draft.DepthCm*.5+20 || MinX+100>Draft.WidthCm*.5-20 || MinY+100>Draft.DepthCm*.5-20) return false;
  if (PreviewCells.Contains(Cell)) return false;PreviewCells.Add(Cell);return true;
 }
 return PreviewCells.Remove(Cell)>0;
}
void UWarehouseConstructionEditor::PaintTo(FVector WorldPoint)
{
 const FVector Local=WorldPoint-GetOrigin();
 // Clamp pointer interpolation to the fixed supported grid before converting
 // to integers; a pointer on the horizon cannot create an unbounded loop.
 const FIntPoint End(FMath::FloorToInt(FMath::Clamp(Local.X,-4800.,4799.)/100.),FMath::FloorToInt(FMath::Clamp(Local.Y,-4000.,3999.)/100.));
 int32 X=LastPaintedCell.X,Y=LastPaintedCell.Y;
 const int32 DX=FMath::Abs(End.X-X),DY=-FMath::Abs(End.Y-Y),SX=X<End.X ? 1 : -1,SY=Y<End.Y ? 1 : -1;
 int32 Error=DX+DY;bool Changed=false;
 for (;;)
 {
  Changed|=PaintTile(FIntPoint(X,Y));if (X==End.X && Y==End.Y) break;
  const int32 Twice=2*Error;
  if (Twice>=DY) { Error+=DY;X+=SX; }
  // A diagonal pointer sweep paints a connected stair-step, not tiles that
  // only touch at a corner and leave an unsupported diagonal gap.
  if (Twice>=DY && Twice<=DX && DX>0 && DY<0) Changed|=PaintTile(FIntPoint(X,Y));
  if (Twice<=DX) { Error+=DX;Y+=SY; }
 }
 LastPaintedCell=End;
 if (!Changed) return;
 auto& Floor=SelectedFloor(Draft);
 if (BrushMode==EWarehouseConstructionBrush::AddTiles)
 {
  Floor.bEnabled=true;Draft.RoofHeightCm=FMath::Max(Draft.RoofHeightCm,float((int32(Target)+1)*400));
 }
 AWarehouseBuildingLayout::SetMezzanineTiles(Floor,PreviewCells.Array());
 RebuildTilePreview();bChanged=true;
 bHasPendingTiles=AddedTiles.Num()>0 || RemovedTiles.Num()>0 || Floor.bEnabled!=SelectedFloor(Building->Settings).bEnabled;
 Status=FString::Printf(TEXT("미확정 · 추가 %d칸 / 제거 %d칸 · 여러 번 그린 뒤 편집 적용 · 실제 바닥·난간·장식은 그대로"),AddedTiles.Num(),RemovedTiles.Num());
}
FVector UWarehouseConstructionEditor::GetOrigin() const { return IsValid(Building) ? Building->GetActorLocation() : FVector(400,0,0); }
float UWarehouseConstructionEditor::GetPlaneHeight() const
{ return GetOrigin().Z+(Target==EWarehouseConstructionTarget::Building ? 0.f : (Target==EWarehouseConstructionTarget::Stairs ? StairFloor : int32(Target))*400.f)+3.f; }
FBox UWarehouseConstructionEditor::GetBuildingBounds() const
{
 const FVector Origin=GetOrigin(),Half(Draft.WidthCm*.5,Draft.DepthCm*.5,0);
 return FBox(FVector(Origin.X-Half.X,Origin.Y-Half.Y,GetPlaneHeight()),FVector(Origin.X+Half.X,Origin.Y+Half.Y,GetPlaneHeight()));
}
FBox UWarehouseConstructionEditor::GetTargetBounds() const
{
 if (Target==EWarehouseConstructionTarget::Building) return GetBuildingBounds();
 const auto& Floor=SelectedFloor(Draft);FVector2D Centre=Floor.OffsetCm,Size=Floor.SizeCm;
 if (Target==EWarehouseConstructionTarget::Stairs)
 {
  const FQuat Rotation(FVector::UpVector,FMath::DegreesToRadians(Floor.StairYawDegrees));FBox Bounds(ForceInit);
  for (double X:{-476.,476.}) for (double Y:{-185.,65.})
   Bounds+=GetOrigin()+FVector(Floor.StairOffsetCm.X,Floor.StairOffsetCm.Y,GetPlaneHeight()-GetOrigin().Z)+Rotation.RotateVector(FVector(X,Y,0));
  return Bounds;
 }
 const FVector Origin=GetOrigin();return FBox(FVector(Origin.X+Centre.X-Size.X*.5,Origin.Y+Centre.Y-Size.Y*.5,GetPlaneHeight()),
  FVector(Origin.X+Centre.X+Size.X*.5,Origin.Y+Centre.Y+Size.Y*.5,GetPlaneHeight()));
}
FBox UWarehouseConstructionEditor::GetAtriumBounds() const
{
 if (Target==EWarehouseConstructionTarget::Building || Target==EWarehouseConstructionTarget::Stairs) return FBox(ForceInit);
 if (SelectedFloor(Draft).bUseTileMask) return FBox(ForceInit);
 const auto& Floor=SelectedFloor(Draft);const FVector Origin=GetOrigin()+FVector(Floor.OffsetCm.X,Floor.OffsetCm.Y,0);
 return FBox(FVector(Origin.X-Floor.AtriumSizeCm.X*.5,Origin.Y-Floor.AtriumSizeCm.Y*.5,GetPlaneHeight()),
  FVector(Origin.X+Floor.AtriumSizeCm.X*.5,Origin.Y+Floor.AtriumSizeCm.Y*.5,GetPlaneHeight()));
}
TArray<FVector> UWarehouseConstructionEditor::GetResizeHandles() const
{
 TArray<FVector> Points;if (Target==EWarehouseConstructionTarget::Stairs || IsTileBrush() || (IsFloorTarget() && SelectedFloor(Draft).bUseTileMask)) return Points;
 const FBox Box=GetTargetBounds();const FVector Centre=Box.GetCenter();
 for (int32 X=-1;X<=1;++X) for (int32 Y=-1;Y<=1;++Y) if (X || Y) Points.Add(Centre+FVector(X*Box.GetExtent().X,Y*Box.GetExtent().Y,0));
 return Points;
}
bool UWarehouseConstructionEditor::ProjectPointer(FVector2D Pixels,FVector& World) const
{
 FVector Origin,Direction;if (!OwnerController.IsValid() || !OwnerController->DeprojectScreenPositionToWorld(Pixels.X,Pixels.Y,Origin,Direction) || FMath::Abs(Direction.Z)<.0001) return false;
 const double T=(GetPlaneHeight()-Origin.Z)/Direction.Z;if (T<0) return false;World=Origin+Direction*T;return !World.ContainsNaN();
}
bool UWarehouseConstructionEditor::PointerDown(FVector2D Pixels)
{
 if (!bActive || !OwnerController.IsValid()) return false;
 FVector World;if (!ProjectPointer(Pixels,World)) return false;
 if (IsTileBrush()) return BeginGestureAt(World,FVector2D::ZeroVector);
 FVector2D Chosen=FVector2D::ZeroVector;float Best=18.f*18.f;
 const FBox Box=GetTargetBounds();const FVector Centre=Box.GetCenter();
 for (FVector Point:GetResizeHandles())
 {
  FVector2D Screen;if (!OwnerController->ProjectWorldLocationToScreen(Point,Screen)) continue;
  const float Distance=FVector2D::DistSquared(Screen,Pixels);
  if (Distance<Best) { Best=Distance;Chosen=FVector2D(FMath::Sign(Point.X-Centre.X),FMath::Sign(Point.Y-Centre.Y)); }
 }
 if (Chosen.IsNearlyZero() && (World.X<Box.Min.X || World.X>Box.Max.X || World.Y<Box.Min.Y || World.Y>Box.Max.Y)) return false;
 if (Target==EWarehouseConstructionTarget::Building && Chosen.IsNearlyZero()) { Status=TEXT("건물의 네모 외곽 손잡이를 잡아 크기를 바꾸세요.");return false; }
 return BeginGestureAt(World,Chosen);
}
void UWarehouseConstructionEditor::PointerMove(FVector2D Pixels) { FVector World;if (ProjectPointer(Pixels,World)) MoveGestureAt(World); }
bool UWarehouseConstructionEditor::BeginGestureAt(FVector WorldPoint,FVector2D ResizeHandle)
{
 if (!bActive || bDragging || WorldPoint.ContainsNaN() || ResizeHandle.ContainsNaN() || !IsValid(Building)) return false;
 if (IsTileBrush())
 {
  const FBox Bounds=GetBuildingBounds();
  if (WorldPoint.X<Bounds.Min.X || WorldPoint.X>=Bounds.Max.X || WorldPoint.Y<Bounds.Min.Y || WorldPoint.Y>=Bounds.Max.Y) return false;
  if (!bHasPendingTiles) RefreshDraft();
  if (!SelectedFloor(Draft).bEnabled && BrushMode==EWarehouseConstructionBrush::RemoveTiles)
  { Status=TEXT("이 층에는 제거할 바닥이 없습니다. 타일 추가로 새 바닥을 그릴 수 있습니다.");return false; }
  Start=Draft;bPendingBeforeStroke=bHasPendingTiles;GrabPoint=WorldPoint;bDragging=true;bChanged=false;
  const FVector Local=WorldPoint-GetOrigin();LastPaintedCell=FIntPoint(FMath::FloorToInt(Local.X/100.),FMath::FloorToInt(Local.Y/100.));
  PaintTo(WorldPoint);return true;
 }
 if (bHasPendingTiles) { Status=TEXT("타일 편집을 먼저 적용하거나 취소하세요.");return false; }
 if (Target==EWarehouseConstructionTarget::Building && ResizeHandle.IsNearlyZero()) return false;
 if (Target!=EWarehouseConstructionTarget::Building && !SelectedFloor(Draft).bEnabled)
 { Status=TEXT("이 층은 꺼져 있습니다. 먼저 층 켜기 버튼으로 바닥을 추가하세요.");return false; }
 if (IsFloorTarget() && SelectedFloor(Draft).bUseTileMask && !ResizeHandle.IsNearlyZero())
 { Status=TEXT("자유형상 바닥의 모양은 타일 추가·제거로 편집하세요. 안쪽을 드래그하면 모양을 유지하며 이동합니다.");return false; }
 RefreshDraft();Start=Draft;GrabPoint=WorldPoint;Handle=FVector2D(FMath::Sign(ResizeHandle.X),FMath::Sign(ResizeHandle.Y));bDragging=true;bChanged=false;return true;
}
void UWarehouseConstructionEditor::MoveGestureAt(FVector WorldPoint)
{
 if (!bDragging || WorldPoint.ContainsNaN()) return;
 if (IsTileBrush()) { PaintTo(WorldPoint);return; }
 Draft=Start;const FVector Local=WorldPoint-GetOrigin(),Delta=WorldPoint-GrabPoint;
 if (Target==EWarehouseConstructionTarget::Building)
 {
  if (Handle.X) Draft.WidthCm=FMath::Clamp(FMath::GridSnap(float(FMath::Abs(Local.X)*2),100.f),1600.f,9600.f);
  if (Handle.Y) Draft.DepthCm=FMath::Clamp(FMath::GridSnap(float(FMath::Abs(Local.Y)*2),100.f),1600.f,8000.f);
 }
 else
 {
  auto& Floor=SelectedFloor(Draft);const auto& Old=SelectedFloor(Start);
  if (Target==EWarehouseConstructionTarget::Stairs) Floor.StairOffsetCm=Old.StairOffsetCm+FVector2D(FMath::GridSnap(Delta.X,100.),FMath::GridSnap(Delta.Y,100.));
  else if (Handle.IsNearlyZero())
  {
   const FVector2D Shift(FMath::GridSnap(Delta.X,100.),FMath::GridSnap(Delta.Y,100.));
   if (Old.bUseTileMask)
   {
    TArray<FIntPoint> Moved=Old.Tiles;const FIntPoint TileShift(FMath::RoundToInt(Shift.X/100.),FMath::RoundToInt(Shift.Y/100.));
    for (FIntPoint& Cell:Moved) Cell+=TileShift;
    AWarehouseBuildingLayout::SetMezzanineTiles(Floor,Moved);RefreshTilePreview(false);
   }
   else Floor.OffsetCm=Old.OffsetCm+Shift;
  }
  else
  {
   if (Handle.X) Floor.SizeCm.X=FMath::Clamp(FMath::GridSnap(FMath::Abs(Local.X-Old.OffsetCm.X)*2,100.),400.,9600.);
   if (Handle.Y) Floor.SizeCm.Y=FMath::Clamp(FMath::GridSnap(FMath::Abs(Local.Y-Old.OffsetCm.Y)*2,100.),400.,8000.);
  }
 }
 if (Target==EWarehouseConstructionTarget::Building) bChanged=Draft.WidthCm!=Start.WidthCm || Draft.DepthCm!=Start.DepthCm;
 else
 {
  const auto& Floor=SelectedFloor(Draft);const auto& Previous=SelectedFloor(Start);
  bChanged=Floor.OffsetCm!=Previous.OffsetCm || Floor.SizeCm!=Previous.SizeCm || Floor.StairOffsetCm!=Previous.StairOffsetCm;
 }
 Status=TEXT("미리보기 · 실제 바닥과 충돌은 그대로 · 놓으면 안전 검사 후 적용");
}
bool UWarehouseConstructionEditor::EndGesture(bool Commit)
{
 if (!bDragging) return false;bDragging=false;
 if (IsTileBrush())
 {
  if (!Commit)
  {
   Draft=Start;bHasPendingTiles=bPendingBeforeStroke;RefreshTilePreview(false);
   Status=bHasPendingTiles ? TEXT("현재 획만 취소 · 앞선 타일 편집은 미확정 상태로 유지") : TEXT("현재 획 취소 · 기존 바닥 유지");
   bChanged=false;return false;
  }
  const bool Changed=bChanged;bChanged=false;
  Status=bHasPendingTiles ? TEXT("미확정 타일 편집 · 계속 그리거나 편집 적용을 누르세요. 난간·장식·지지대는 적용할 때 한 번 재배치됩니다.") : TEXT("타일 변경 없음 · 기존 구조 유지");
  return Changed;
 }
 if (!Commit || !bChanged) { RefreshDraft();Status=TEXT("건물 편집 취소 · 기존 구조 유지");return false; }
 bChanged=false;return ApplyDraft();
}
FString UWarehouseConstructionEditor::GetReadout() const
{
 const auto& Floor=SelectedFloor(Draft);
 if (IsTileBrush())
  return FString::Printf(TEXT("%d층 · 1m 타일 %d칸 · 추가 %d / 제거 %d · %s\n클릭·드래그로 타일 %s · 편집 적용 전에는 구조·충돌·난간·장식을 변경하지 않음"),
   int32(Target)+1,PreviewCells.Num(),AddedTiles.Num(),RemovedTiles.Num(),bHasPendingTiles ? TEXT("미확정") : TEXT("변경 없음"),
   BrushMode==EWarehouseConstructionBrush::AddTiles ? TEXT("추가") : TEXT("제거"));
 return FString::Printf(TEXT("건물 %.0f × %.0fm · 높이 %.0fm · 한계 96 × 80m / 20m / 4층\n%s"),Draft.WidthCm/100,Draft.DepthCm/100,Draft.RoofHeightCm/100,
  Target==EWarehouseConstructionTarget::Building ? TEXT("건물 외곽 손잡이로 크기 조절") : *FString::Printf(TEXT("%d층 %s · 바닥 %.0f × %.0fm · 계단 방향 %.0f°"),Target==EWarehouseConstructionTarget::Stairs ? StairFloor+1 : int32(Target)+1,
   Floor.bEnabled ? TEXT("켜짐") : TEXT("꺼짐"),Floor.SizeCm.X/100,Floor.SizeCm.Y/100,Floor.StairYawDegrees));
}
