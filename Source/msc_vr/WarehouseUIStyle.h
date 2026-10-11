#pragma once

#include "CoreMinimal.h"
#include "Styling/CoreStyle.h"

/** Shared appearance for the warehouse HUD, menu and world-positioned labels. */
namespace WarehouseUIStyle
{
 inline constexpr int32 TitleSize=24, BodySize=20, LabelSize=18, CompactSize=16, ReadoutSize=28;
 inline const FLinearColor Panel(.02f,.035f,.05f,.94f);
 inline const FLinearColor Text=FLinearColor::White;
 inline const FLinearColor Accent(.3f,.8f,.85f);
 inline const FLinearColor Warning(1.f,.8f,.25f);
 inline const FLinearColor DangerPanel(.35f,.015f,.01f,.96f);
 inline const FMargin PanelPadding(16), LabelPadding(12,6);
 inline FSlateFontInfo Font(int32 Size=BodySize,bool Bold=false)
 {
  return FCoreStyle::GetDefaultFontStyle(Bold ? "Bold" : "Regular",Size);
 }
}
