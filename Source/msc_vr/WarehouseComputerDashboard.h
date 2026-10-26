#pragma once
#include "CoreMinimal.h"

class SWidget;
class AWarehouseComputer;
class Amsc_vrPlayerController;

TSharedRef<SWidget> MakeWarehouseComputerDashboard(AWarehouseComputer* Computer, Amsc_vrPlayerController* Controller);
TSharedRef<SWidget> MakeWarehouseLegacyComputerScreen(AWarehouseComputer* Computer, Amsc_vrPlayerController* Controller);