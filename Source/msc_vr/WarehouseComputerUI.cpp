#include "msc_vrPlayerController.h"
#include "msc_vrCharacter.h"
#include "WarehouseComputer.h"
#include "WarehouseComputerDashboard.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"

namespace
{
// Legacy stays callable for comparison and rollback; choose it before opening the console.
TAutoConsoleVariable<int32> CVarWarehouseComputerLegacyUI(
 TEXT("Warehouse.Computer.LegacyUI"),0,
 TEXT("0: warehouse dashboard (default); 1: previous warehouse command screen. Reopen the computer after changing."));
}
bool Amsc_vrPlayerController::OpenWarehouseComputer(AWarehouseComputer* Computer)
{
 auto* OperatorCharacter=Cast<Amsc_vrCharacter>(GetPawn());
 if (!IsLocalController() || !GetWorld()->GetGameViewport() || !OperatorCharacter || !IsValid(Computer) || !Computer->CanUse() ||
     IsWarehouseMenuOpen() || bObserverView || IsMoveInputIgnored() || OperatorCharacter->GetHeldCargo() || OperatorCharacter->GetRemoteForklift() ||
     OperatorCharacter->GetInteractionTarget()!=Computer) return false;
 Computer->RefreshState();ActiveComputer=Computer;
 ComputerWidget=CVarWarehouseComputerLegacyUI.GetValueOnGameThread()!=0 ? MakeWarehouseLegacyComputerScreen(Computer,this) : MakeWarehouseComputerDashboard(Computer,this);
 GetWorld()->GetGameViewport()->AddViewportWidgetContent(ComputerWidget.ToSharedRef(),250);
 UpdateInputMode();FSlateApplication::Get().SetKeyboardFocus(ComputerWidget);return true;
}
void Amsc_vrPlayerController::CloseWarehouseComputer()
{
 if (!ComputerWidget.IsValid()) return;
 if (GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(ComputerWidget.ToSharedRef());
 ComputerWidget.Reset();ActiveComputer=nullptr;UpdateInputMode();
}
