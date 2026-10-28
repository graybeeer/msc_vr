#pragma once

#include "CoreMinimal.h"

class AWarehouseComputer;
class Amsc_vrPlayerController;
class SWidget;

// Simplified receiving, dock service and optional continuous flow in the computer dashboard.
TSharedRef<SWidget> MakeWarehouseOperationsPanel(AWarehouseComputer* Computer,Amsc_vrPlayerController* Controller);
