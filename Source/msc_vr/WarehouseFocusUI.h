#pragma once
#include "CoreMinimal.h"

class SWidget;
class AWarehouseComputer;
class Amsc_vrPlayerController;

// The default warehouse console exposes forklift commands; office operations stay automatic.
TSharedRef<SWidget> MakeWarehouseFocusScreen(AWarehouseComputer* Computer,Amsc_vrPlayerController* Controller);
