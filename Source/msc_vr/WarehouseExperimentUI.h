#pragma once

#include "CoreMinimal.h"

class Amsc_vrPlayerController;
class SWidget;

// One view is shared by the observation toolbar and the warehouse computer.
TSharedRef<SWidget> MakeWarehouseExperimentPanel(Amsc_vrPlayerController* Controller, bool Modal = false);
