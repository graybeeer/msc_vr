#include "WarehouseConstructionEditor.h"
#include "GameFramework/PlayerController.h"

void UWarehouseConstructionEditor::Initialize(APlayerController* Controller) { OwnerController=Controller; }
void UWarehouseConstructionEditor::RefreshDraft()
{
 if (!IsValid(Building) && OwnerController.IsValid()) Building=AWarehouseBuildingLayout::Find(OwnerController.Get());
 if (Building) Draft=Building->GetSettings();
}
void UWarehouseConstructionEditor::SetActive(bool Active)
{
 EndGesture(false);bActive=Active;
 if (Active) { RefreshDraft();Status=TEXT("1m 격자 · 외곽 손잡이로 크기 조절 · 복층/계단 안쪽을 잡아 이동 · 놓으면 적용"); }
}
void UWarehouseConstructionEditor::SetTarget(EWarehouseConstructionTarget Value)
{
 if (int32(Value)>int32(EWarehouseConstructionTarget::Stairs)) return;
 EndGesture(false);Target=Value;RefreshDraft();
 if (Value>=EWarehouseConstructionTarget::Floor2 && Value<=EWarehouseConstructionTarget::Floor4) StairFloor=int32(Value);
 Status=Value==EWarehouseConstructionTarget::Building ? TEXT("건물 외곽의 네모 손잡이를 드래그하세요. 최대 96 × 80m, 높이 20m입니다.") :
  Value==EWarehouseConstructionTarget::Stairs ? TEXT("계단 범위를 잡아 이동하세요. 선택층의 계단만 이동하며 회전 버튼으로 방향을 바꿉니다.") :
  TEXT("복층 바닥 안쪽은 이동, 외곽 네모 손잡이는 크기 조절입니다. 내부 화물·시설의 지지가 사라지는 변경은 차단됩니다.");
}
void UWarehouseConstructionEditor::SetStairFloor(int32 Floor)
{ EndGesture(false);StairFloor=FMath::Clamp(Floor,1,3);RefreshDraft(); }
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
 const bool Accepted=Building->ApplySettings(Draft);Status=Building->Status;Draft=Building->GetSettings();return Accepted;
}
bool UWarehouseConstructionEditor::ToggleSelectedFloor()
{
 if (!bActive || Target==EWarehouseConstructionTarget::Building) return false;
 EndGesture(false);RefreshDraft();auto& Floor=SelectedFloor(Draft);Floor.bEnabled=!Floor.bEnabled;
 if (Floor.bEnabled) Draft.RoofHeightCm=FMath::Max(Draft.RoofHeightCm,float((Target==EWarehouseConstructionTarget::Stairs ? StairFloor : int32(Target))*400+400));
 return ApplyDraft();
}
bool UWarehouseConstructionEditor::AdjustRoofHeight(float DeltaCm)
{
 if (!bActive || !FMath::IsFinite(DeltaCm)) return false;
 EndGesture(false);RefreshDraft();Draft.RoofHeightCm=FMath::GridSnap(Draft.RoofHeightCm+DeltaCm,100.f);return ApplyDraft();
}
bool UWarehouseConstructionEditor::RotateStairs(float DeltaDegrees)
{
 if (!bActive || !FMath::IsFinite(DeltaDegrees)) return false;
 EndGesture(false);RefreshDraft();SelectedFloor(Draft).StairYawDegrees=FRotator::NormalizeAxis(SelectedFloor(Draft).StairYawDegrees+DeltaDegrees);return ApplyDraft();
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
 const auto& Floor=SelectedFloor(Draft);const FVector Origin=GetOrigin()+FVector(Floor.OffsetCm.X,Floor.OffsetCm.Y,0);
 return FBox(FVector(Origin.X-Floor.AtriumSizeCm.X*.5,Origin.Y-Floor.AtriumSizeCm.Y*.5,GetPlaneHeight()),
  FVector(Origin.X+Floor.AtriumSizeCm.X*.5,Origin.Y+Floor.AtriumSizeCm.Y*.5,GetPlaneHeight()));
}
TArray<FVector> UWarehouseConstructionEditor::GetResizeHandles() const
{
 TArray<FVector> Points;if (Target==EWarehouseConstructionTarget::Stairs) return Points;
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
 if (Target==EWarehouseConstructionTarget::Building && ResizeHandle.IsNearlyZero()) return false;
 if (Target!=EWarehouseConstructionTarget::Building && !SelectedFloor(Draft).bEnabled)
 { Status=TEXT("이 층은 꺼져 있습니다. 먼저 층 켜기 버튼으로 바닥을 추가하세요.");return false; }
 RefreshDraft();Start=Draft;GrabPoint=WorldPoint;Handle=FVector2D(FMath::Sign(ResizeHandle.X),FMath::Sign(ResizeHandle.Y));bDragging=true;bChanged=false;return true;
}
void UWarehouseConstructionEditor::MoveGestureAt(FVector WorldPoint)
{
 if (!bDragging || WorldPoint.ContainsNaN()) return;
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
  else if (Handle.IsNearlyZero()) Floor.OffsetCm=Old.OffsetCm+FVector2D(FMath::GridSnap(Delta.X,100.),FMath::GridSnap(Delta.Y,100.));
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
 if (!Commit || !bChanged) { RefreshDraft();Status=TEXT("건물 편집 취소 · 기존 구조 유지");return false; }
 bChanged=false;return ApplyDraft();
}
FString UWarehouseConstructionEditor::GetReadout() const
{
 const auto& Floor=SelectedFloor(Draft);
 return FString::Printf(TEXT("건물 %.0f × %.0fm · 높이 %.0fm · 한계 96 × 80m / 20m / 4층\n%s"),Draft.WidthCm/100,Draft.DepthCm/100,Draft.RoofHeightCm/100,
  Target==EWarehouseConstructionTarget::Building ? TEXT("건물 외곽 손잡이로 크기 조절") : *FString::Printf(TEXT("%d층 %s · 바닥 %.0f × %.0fm · 계단 방향 %.0f°"),Target==EWarehouseConstructionTarget::Stairs ? StairFloor+1 : int32(Target)+1,
   Floor.bEnabled ? TEXT("켜짐") : TEXT("꺼짐"),Floor.SizeCm.X/100,Floor.SizeCm.Y/100,Floor.StairYawDegrees));
}
