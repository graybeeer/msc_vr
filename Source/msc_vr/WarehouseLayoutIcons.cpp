#include "WarehouseLayoutIcons.h"
#include "WarehouseLayoutEditor.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/SlateBrush.h"

namespace
{
 FString IconKey(EWarehouseLayoutCategory Category,const FString& Name)
 {
  switch(Category)
  {
  case EWarehouseLayoutCategory::Rack:return TEXT("Rack");
  case EWarehouseLayoutCategory::Forklift:return TEXT("Forklift");
  case EWarehouseLayoutCategory::Cargo:return TEXT("Cargo");
  case EWarehouseLayoutCategory::Pallet:return TEXT("Pallet");
  case EWarehouseLayoutCategory::Charger:return TEXT("Charger");
  case EWarehouseLayoutCategory::WaitingBay:return TEXT("WaitingBay");
  case EWarehouseLayoutCategory::Zone:return Name.Contains(TEXT("출고")) ? TEXT("ZoneOutbound") : TEXT("ZoneInbound");
  case EWarehouseLayoutCategory::Elevator:return Name.Contains(TEXT("화물용")) ? TEXT("PalletLift") : TEXT("Elevator");
  case EWarehouseLayoutCategory::Conveyor:
   return Name.Contains(TEXT("90°")) ? TEXT("ConveyorTurn") : Name.Contains(TEXT("상자")) ? TEXT("ConveyorBelt") : TEXT("ConveyorRoller");
  case EWarehouseLayoutCategory::Other:return Name.Contains(TEXT("Worker")) || Name.Contains(TEXT("작업자")) ? TEXT("Worker") : TEXT("Computer");
  default:return TEXT("All");
  }
 }
 struct FIcon
 {
  TStrongObjectPtr<UTexture2D> Texture;
  FSlateBrush Brush;
  explicit FIcon(const FString& Key)
   : Texture(LoadObject<UTexture2D>(nullptr,*(TEXT("/Game/Warehouse/UI/Icons/T_Icon_")+Key+TEXT(".T_Icon_")+Key)))
  {
   Brush.SetResourceObject(Texture.Get());Brush.ImageSize=FVector2D(512,512);Brush.DrawAs=Texture.IsValid() ? ESlateBrushDrawType::Image : ESlateBrushDrawType::NoDrawType;
  }
 };
 const FSlateBrush* Icon(EWarehouseLayoutCategory Category,const FString& Name)
 {
  static TMap<FString,TUniquePtr<FIcon>> Cache;
  const FString Key=IconKey(Category,Name);
  auto& Entry=Cache.FindOrAdd(Key);
  if(!Entry) Entry=MakeUnique<FIcon>(Key);
  return &Entry->Brush;
 }
}
TSharedRef<SWidget> MakeWarehouseLayoutIcon(TFunction<EWarehouseLayoutCategory()> Category,TFunction<FString()> Choice,float Size)
{
 return SNew(SBox).WidthOverride(Size).HeightOverride(Size)
  [SNew(SImage).Image_Lambda([Category,Choice](){return Icon(Category(),Choice());})];
}
