#pragma once
#include "CoreMinimal.h"
enum class EWarehouseLayoutCategory:uint8;
class SWidget;

// Icons are offline renders of the actual project models; no runtime scene capture.
MSC_VR_API TSharedRef<SWidget> MakeWarehouseLayoutIcon(TFunction<EWarehouseLayoutCategory()> Category,
 TFunction<FString()> Choice=[](){return FString();},float Size=32.f);
