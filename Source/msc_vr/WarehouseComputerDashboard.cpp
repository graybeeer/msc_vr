#include "WarehouseComputerDashboard.h"
#include "WarehouseExperimentUI.h"
#include "WarehouseOperationsUI.h"
#include "WarehousePlayMode.h"
#include "WarehouseComputer.h"
#include "WarehouseCargo.h"
#include "WarehousePallet.h"
#include "WarehouseForklift.h"
#include "WarehouseLayoutInventory.h"
#include "WarehouseShippingSystem.h"
#include "WarehouseLogisticsTruck.h"
#include "WarehouseUIStyle.h"
#include "msc_vrPlayerController.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Views/SListView.h"
#if WITH_EDITOR && !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#endif

namespace WarehouseDashboard
{
const FLinearColor Background(.012f,.022f,.035f), Surface(.025f,.045f,.064f), Muted(.57f,.66f,.73f);
const FLinearColor Danger(1.f,.45f,.40f), Primary(.055f,.30f,.36f);

TSharedRef<STextBlock> Label(const FString& Value,int32 Size=16,FLinearColor Color=WarehouseUIStyle::Text,bool Bold=false)
{
 return SNew(STextBlock).Text(FText::FromString(Value)).Font(WarehouseUIStyle::Font(Size,Bold)).ColorAndOpacity(Color).AutoWrapText(true);
}
TSharedRef<STextBlock> Live(TFunction<FString()> Value,int32 Size=16,FLinearColor Color=WarehouseUIStyle::Text)
{
 return SNew(STextBlock).Text_Lambda([Value]() { return FText::FromString(Value()); })
  .Font(WarehouseUIStyle::Font(Size)).ColorAndOpacity(Color).AutoWrapText(true);
}
TSharedRef<SWidget> Panel(const TSharedRef<SWidget>& Content)
{
 return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Surface).Padding(18)[Content];
}
TSharedRef<SWidget> Action(const FString& Title,TFunction<void()> Callback,TFunction<bool()> Enabled=[]() { return true; },bool Main=false)
{
 return SNew(SButton).ContentPadding(FMargin(14,10)).IsFocusable(false)
  .ButtonColorAndOpacity(Main ? Primary : FLinearColor(.09f,.13f,.17f))
  .IsEnabled_Lambda([Enabled]() { return Enabled(); })
  .OnClicked_Lambda([Callback]() { Callback();return FReply::Handled(); })[Label(Title,16,WarehouseUIStyle::Text,true)];
}
bool Finished(const FWarehouseShipment& Shipment)
{
 return Shipment.State==EWarehouseShipmentState::Completed || Shipment.State==EWarehouseShipmentState::Cancelled;
}
struct FDashboardVehicle
{
 TWeakObjectPtr<AWarehouseForklift> Vehicle;
 FString Name() const { return Vehicle.IsValid() ? Vehicle->VehicleName : TEXT("자동 배정 · 가까운 대기 차량"); }
};
struct FDashboardTruck { TWeakObjectPtr<AWarehouseLogisticsTruck> Truck; };

class SWarehouseDashboard : public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SWarehouseDashboard) {}
 SLATE_ARGUMENT(TWeakObjectPtr<AWarehouseComputer>,Computer)
 SLATE_ARGUMENT(TWeakObjectPtr<Amsc_vrPlayerController>,Controller)
 SLATE_END_ARGS()

 void Construct(const FArguments& Args)
 {
  Computer=Args._Computer;Controller=Args._Controller;
  AutomaticVehicle=MakeShared<FDashboardVehicle>();SelectedVehicle=AutomaticVehicle;
  Refresh();RefreshHeld();
  auto Tabs=SNew(SHorizontalBox);
  const TCHAR* Titles[]={TEXT("운반 지시"),TEXT("배송편"),TEXT("작업 · 차량"),TEXT("보류 물류"),TEXT("실험 · 성과"),TEXT("창고 공정")};
  for (int32 Index=0;Index<UE_ARRAY_COUNT(Titles);++Index)
  {
   TSharedPtr<SButton> Tab;
   Tabs->AddSlot().FillWidth(1).Padding(Index ? 4 : 0,0,0,0)
   [SAssignNew(Tab,SButton).ContentPadding(FMargin(14,12)).IsFocusable(false)
    .Visibility(AWarehousePlayStage::IsPlay(Computer.Get()) && (Index==3 || Index==4) ? EVisibility::Collapsed : EVisibility::Visible)
    .ButtonColorAndOpacity_Lambda([this,Index]() { return Page==Index ? Primary : Surface; })
    .OnClicked_Lambda([this,Index]() { Page=Index;bConfirmDeleteHeld=false;return FReply::Handled(); })
    [SNew(STextBlock).Text(FText::FromString(Titles[Index])).Font(WarehouseUIStyle::Font(18,true))
     .ColorAndOpacity_Lambda([this,Index]() { return Page==Index ? WarehouseUIStyle::Accent : WarehouseUIStyle::Text; })]];
   TabButtons.Add(Tab);
  }

  auto Summary=SNew(SHorizontalBox);
  auto Metric=[&](const FString& Title,TFunction<FString()> Value)
  {
   Summary->AddSlot().FillWidth(1).Padding(0,0,8,0)[Panel(SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[Label(Title,14,Muted)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)[Live(Value,22,WarehouseUIStyle::Accent)])];
  };
  Metric(TEXT("창고 물류"),[this]() { return FString::Printf(TEXT("%d 상자 · %d 팔레트"),InventoryCount,PalletCount); });
  Metric(TEXT("운용 차량"),[this]() { return FString::Printf(TEXT("%d 대 · 대기 %d 대"),FleetItems.Num(),IdleVehicleCount); });
  Metric(TEXT("운반 작업"),[this]() { return FString::Printf(TEXT("진행 %d · 완료 %d"),ActiveTaskCount,CompletedTaskCount); });
  Metric(TEXT("보류 물류"),[this]() { return FString::Printf(TEXT("%d 개"),HeldItems.Num()); });

  ChildSlot[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Background).Padding(24)
   [SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)[SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1)[SNew(SVerticalBox)
      +SVerticalBox::Slot().AutoHeight()[Label(TEXT("창고 관제"),26,WarehouseUIStyle::Text,true)]
      +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Label(TEXT("실시간 현황 · 화면을 보는 동안에도 시뮬레이션은 진행됩니다"),14,Muted)]]
     +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[CommandAction(TEXT("close"),TEXT("플레이어로 돌아가기  ·  Esc"),[this]() { Close(); })]]
    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)[Summary]
    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)[Tabs]
    +SVerticalBox::Slot().FillHeight(1)[SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(.56f).Padding(0,0,12,0)
      [SNew(SBox).Visibility_Lambda([this]() { return Page<2 ? EVisibility::Visible : EVisibility::Collapsed; })[BuildInventory()]]
     +SHorizontalBox::Slot().FillWidth(.44f)
      [SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]() { return Page; })
       +SWidgetSwitcher::Slot()[BuildTransport()]
       +SWidgetSwitcher::Slot()[BuildShipping()]
       +SWidgetSwitcher::Slot()[BuildOperations()]
       +SWidgetSwitcher::Slot()[BuildHeld()]
       +SWidgetSwitcher::Slot()[MakeWarehouseExperimentPanel(Controller.Get())]
       +SWidgetSwitcher::Slot()[MakeWarehouseOperationsPanel(Computer.Get(),Controller.Get())]]]
    +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,0)[SNew(SBorder).Padding(FMargin(12,8))
     .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Surface)
     [SNew(SHorizontalBox)
      +SHorizontalBox::Slot().FillWidth(1)[Live([this]() { return Notice.IsEmpty() ? TEXT("작업을 선택하면 다음 단계가 표시됩니다.") : Notice; },15)]
      +SHorizontalBox::Slot().AutoWidth().Padding(16,0,0,0)[Label(TEXT("1초마다 자동 갱신"),13,Muted)]]]
    +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)[Live([this]() { return Controller.IsValid() ? Controller->EmergencyAlertText : FString(); },14,Danger)]]];
 }

 virtual bool SupportsKeyboardFocus() const override { return true; }
 virtual FReply OnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override
 {
  if (Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::F1) { Close();return FReply::Handled(); }
  return SCompoundWidget::OnPreviewKeyDown(Geometry,Event);
 }
 virtual void Tick(const FGeometry& Geometry,double Now,float DeltaSeconds) override
 {
  SCompoundWidget::Tick(Geometry,Now,DeltaSeconds);
  if (!Computer.IsValid()) { Close();return; }
  if (LastRefresh!=Computer->RefreshCount) Refresh();
  if (auto* Store=AWarehouseLayoutInventory::Find(Computer.Get());Store && HeldRevision!=Store->Revision) RefreshHeld();
 }

private:
 TWeakObjectPtr<AWarehouseComputer> Computer;
 TWeakObjectPtr<Amsc_vrPlayerController> Controller;
 TWeakObjectPtr<AWarehouseShippingSystem> Shipping;
 int32 Page=0,InventoryFilter=0,LastRefresh=-1,HeldRevision=-1;
 int32 InventoryCount=0,PalletCount=0,IdleVehicleCount=0,ActiveTaskCount=0,CompletedTaskCount=0;
 FString Search,Notice;
 TArray<TSharedPtr<FWarehouseInventoryItem>> InventoryItems;
 TMap<TWeakObjectPtr<AWarehouseCargo>,TSharedPtr<FWarehouseInventoryItem>> InventoryCache;
 TSharedPtr<SListView<TSharedPtr<FWarehouseInventoryItem>>> InventoryList;
 TSharedPtr<SSearchBox> SearchBox;
 TArray<TSharedPtr<SButton>> TabButtons,FilterButtons;
 TMap<FString,TSharedPtr<SButton>> CommandButtons;
 TArray<TSharedPtr<FDashboardVehicle>> VehicleChoices,FleetItems;
 TSharedPtr<FDashboardVehicle> AutomaticVehicle,SelectedVehicle;
 TSharedPtr<SComboBox<TSharedPtr<FDashboardVehicle>>> VehicleCombo;
 TArray<TSharedPtr<FString>> DestChoices,InboundZones,OutboundZones;
 TSharedPtr<FString> SelectedDest,SelectedInboundZone,SelectedOutboundZone;
 TSharedPtr<SComboBox<TSharedPtr<FString>>> DestCombo,InboundZoneCombo,OutboundZoneCombo;
 TArray<TSharedPtr<FDashboardTruck>> TruckChoices;
 TSharedPtr<FDashboardTruck> SelectedInboundTruck,SelectedOutboundTruck;
 TSharedPtr<SComboBox<TSharedPtr<FDashboardTruck>>> InboundTruckCombo,OutboundTruckCombo;
 TSharedPtr<SEditableTextBox> ShipmentNameInput;
 TArray<TSharedPtr<FWarehouseShipment>> ShipmentItems;
 TMap<FString,TSharedPtr<FWarehouseShipment>> ShipmentCache;
 TSharedPtr<SListView<TSharedPtr<FWarehouseShipment>>> ShipmentList;
 FString SelectedShipmentId,SelectedTaskId;
 TArray<TSharedPtr<FWarehouseComputerTask>> TaskItems;
 TMap<FString,TSharedPtr<FWarehouseComputerTask>> TaskCache;
 TSharedPtr<SListView<TSharedPtr<FWarehouseComputerTask>>> TaskList;
 TSharedPtr<SListView<TSharedPtr<FDashboardVehicle>>> FleetList;
 TArray<TSharedPtr<FWarehouseHeldCargo>> HeldItems;
 TSharedPtr<SListView<TSharedPtr<FWarehouseHeldCargo>>> HeldList;
 FName SelectedHeldId;
 bool bConfirmDeleteHeld=false;
 TSharedPtr<SEditableTextBox> NewIdInput,NewProductInput;
 float NewMass=12,NewWidth=40,NewDepth=30,NewHeight=25;

 void Close() { if (Controller.IsValid()) Controller->CloseWarehouseComputer(); }
 TSharedRef<SWidget> CommandAction(const FString& Key,const FString& Title,TFunction<void()> Callback,TFunction<bool()> Enabled=[]() { return true; },bool Main=false)
 {
  auto Widget=Action(Title,MoveTemp(Callback),MoveTemp(Enabled),Main);
  CommandButtons.Add(Key,StaticCastSharedRef<SButton>(Widget));return Widget;
 }
 void Report(bool SuccessValue,bool ShippingMessage=false)
 {
  const FString Message=ShippingMessage && Shipping.IsValid() ? Shipping->LastMessage : Computer.IsValid() ? Computer->LastMessage : TEXT("연결이 종료되었습니다.");
  Notice=(SuccessValue ? TEXT("완료 · ") : TEXT("확인 필요 · "))+Message;
  if (Computer.IsValid()) Computer->RefreshState();
  Refresh();RefreshHeld();
 }
 bool CanUse() const { return Computer.IsValid() && Computer->CanUse(); }

 TSharedRef<SWidget> BuildInventory()
 {
  static const FSearchBoxStyle SearchStyle=[]()
  {
   auto Style=FCoreStyle::Get().GetWidgetStyle<FSearchBoxStyle>("SearchBox");
   Style.TextBoxStyle.SetFont(WarehouseUIStyle::Font(16));Style.SetActiveFont(WarehouseUIStyle::Font(16));return Style;
  }();
  auto Filters=SNew(SHorizontalBox);
  const TCHAR* Titles[]={TEXT("전체"),TEXT("팔레트 · 미예약"),TEXT("작업 중"),TEXT("개별 상자")};
  for (int32 Index=0;Index<4;++Index)
  {
   TSharedPtr<SButton> FilterButton;
   Filters->AddSlot().FillWidth(1).Padding(0,0,4,0)[SAssignNew(FilterButton,SButton).ContentPadding(FMargin(7,7)).IsFocusable(false)
    .ButtonColorAndOpacity_Lambda([this,Index]() { return InventoryFilter==Index ? Primary : Surface; })
    .OnClicked_Lambda([this,Index]() { InventoryFilter=Index;FilterInventory();return FReply::Handled(); })[Label(Titles[Index],14)]];
   FilterButtons.Add(FilterButton);
  }
  return Panel(SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[Label(TEXT("1. 운반할 물류 선택"),20,WarehouseUIStyle::Text,true)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,12)[Live([this]() { return Page==1 ? TEXT("체크한 상품의 팔레트를 배송편에 포함합니다. 같은 팔레트는 한 번만 셉니다.") : TEXT("상품 하나를 선택하면 그 상품이 실린 팔레트 전체를 운반합니다."); },14,Muted)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[SAssignNew(SearchBox,SSearchBox).Style(&SearchStyle)
    .HintText(FText::FromString(TEXT("상품명, 화물 번호, 랙 위치 검색")))
    .OnTextChanged_Lambda([this](const FText& Text) { Search=Text.ToString().TrimStartAndEnd();FilterInventory(); })]
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Filters]
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[Live([this]() { return FString::Printf(TEXT("표시 %d / %d 상자 · 선택 팔레트 %d개"),InventoryItems.Num(),InventoryCount,SelectedPallets().Num()); },14,WarehouseUIStyle::Accent)]
   +SVerticalBox::Slot().AutoHeight()[SNew(SBox).Visibility_Lambda([this]() { return InventoryItems.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
    [Label(TEXT("조건에 맞는 물류가 없습니다. 검색어나 필터를 바꿔 주세요."),16,Muted)]]
   +SVerticalBox::Slot().FillHeight(1)[SAssignNew(InventoryList,SListView<TSharedPtr<FWarehouseInventoryItem>>)
    .ListItemsSource(&InventoryItems).SelectionMode(ESelectionMode::Multi)
    .OnGenerateRow_Lambda([this](TSharedPtr<FWarehouseInventoryItem> Item,const TSharedRef<STableViewBase>& Owner)
    {
     return SNew(STableRow<TSharedPtr<FWarehouseInventoryItem>>,Owner).Padding(FMargin(4,10))
      [SNew(SHorizontalBox)
       +SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0).VAlign(VAlign_Center)
        [SNew(SCheckBox).IsChecked_Lambda([this,Item]() { return InventoryList->IsItemSelected(Item) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
         .OnCheckStateChanged_Lambda([this,Item](ECheckBoxState State) { InventoryList->SetItemSelection(Item,State==ECheckBoxState::Checked); })]
       +SHorizontalBox::Slot().FillWidth(.55f)[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[Live([Item]() { return Item->Product; },17)]
        +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[Live([Item]() { return Item->CargoId; },13,Muted)]
        +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[Live([Item]() { return Item->ReceiptStatus; },13,Muted)]]
       +SHorizontalBox::Slot().FillWidth(.45f).Padding(8,0,0,0)[SNew(SVerticalBox)
        +SVerticalBox::Slot().AutoHeight()[Live([Item]() { return Item->Location; },15,WarehouseUIStyle::Accent)]
        +SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)[Live([Item]() { return FString::Printf(TEXT("%.1f kg · %s"),Item->MassKg,Item->bReserved ? TEXT("예약 / 운반 중") : Item->Pallet ? TEXT("팔레트 적재") : TEXT("개별 상자")); },14,Muted)]]];
    })]
   +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[Action(TEXT("선택 해제"),[this]() { InventoryList->ClearSelection(); },[this]() { return InventoryList && InventoryList->GetNumItemsSelected()>0; })]);
 }

 TSharedRef<SWidget> BuildTransport()
 {
  return Panel(SNew(SScrollBox)+SScrollBox::Slot()[SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[Label(TEXT("2. 운반 내용 확인"),20,WarehouseUIStyle::Text,true)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,20)[Live([this]() { return SelectionText(); },17)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Label(TEXT("도착 위치"),16,Muted)]
   +SVerticalBox::Slot().AutoHeight()[StringCombo(DestChoices,SelectedDest,DestCombo,TEXT("출고 자리가 없습니다"))]
   +SVerticalBox::Slot().AutoHeight().Padding(0,18,0,8)[Label(TEXT("담당 지게차"),16,Muted)]
   +SVerticalBox::Slot().AutoHeight()[SAssignNew(VehicleCombo,SComboBox<TSharedPtr<FDashboardVehicle>>).OptionsSource(&VehicleChoices).InitiallySelectedItem(SelectedVehicle)
    .OnGenerateWidget_Lambda([](TSharedPtr<FDashboardVehicle> Item) { return Label(Item->Name()); })
    .OnSelectionChanged_Lambda([this](TSharedPtr<FDashboardVehicle> Item,ESelectInfo::Type) { SelectedVehicle=Item; })
    [Live([this]() { return SelectedVehicle ? SelectedVehicle->Name() : TEXT("차량을 선택하세요"); })]]
   +SVerticalBox::Slot().AutoHeight().Padding(0,10,0,16)[Label(TEXT("자동 배정은 대기 차량을 선택합니다. 작업 중인 차량을 직접 선택하면 대기열에 추가합니다."),14,Muted)]
   +SVerticalBox::Slot().AutoHeight()[CommandAction(TEXT("dispatch"),TEXT("3. 팔레트 전체 운반 지시"),[this]()
    {
     const auto* Item=SelectedItem();if (!Item || !TransportBlockReason().IsEmpty()) return;
     Report(Computer->IssueOutbound(Item->Cargo,SelectedVehicle ? SelectedVehicle->Vehicle.Get() : nullptr,DestChoices.IndexOfByKey(SelectedDest)));
    },[this]() { return TransportBlockReason().IsEmpty(); },true)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)[Live([this]() { const FString Reason=TransportBlockReason();return Reason.IsEmpty() ? TEXT("지시할 준비가 되었습니다. 접수 시 하중·경로·도착 자리 조건을 다시 확인합니다.") : Reason; },14,WarehouseUIStyle::Warning)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,24,0,0)[Label(TEXT("여러 팔레트를 트럭으로 보내려면 ‘배송편’ 탭을 사용하세요. 입고 확정은 기존 NPC 검수 절차로 진행됩니다."),14,Muted)]]);
 }

 TSharedRef<SWidget> StringCombo(TArray<TSharedPtr<FString>>& Items,TSharedPtr<FString>& Selected,TSharedPtr<SComboBox<TSharedPtr<FString>>>& Combo,const FString& Empty)
 {
  auto* Selection=&Selected;
  return SAssignNew(Combo,SComboBox<TSharedPtr<FString>>).OptionsSource(&Items).InitiallySelectedItem(Selected)
   .OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return Label(*Item); })
   .OnSelectionChanged_Lambda([Selection](TSharedPtr<FString> Item,ESelectInfo::Type) { *Selection=Item; })
   [Live([Selection,Empty]() { return Selection->IsValid() ? **Selection : Empty; })];
 }
 TSharedRef<SWidget> TruckCombo(bool Inbound)
 {
  auto& Combo=Inbound ? InboundTruckCombo : OutboundTruckCombo;
  return SAssignNew(Combo,SComboBox<TSharedPtr<FDashboardTruck>>).OptionsSource(&TruckChoices)
   .InitiallySelectedItem(Inbound ? SelectedInboundTruck : SelectedOutboundTruck)
   .OnGenerateWidget_Lambda([](TSharedPtr<FDashboardTruck> Item) { return Label(TruckText(Item)); })
   .OnSelectionChanged_Lambda([this,Inbound](TSharedPtr<FDashboardTruck> Item,ESelectInfo::Type) { (Inbound ? SelectedInboundTruck : SelectedOutboundTruck)=Item; })
   [Live([this,Inbound]() { return TruckText(Inbound ? SelectedInboundTruck : SelectedOutboundTruck); })];
 }
 static FString TruckText(const TSharedPtr<FDashboardTruck>& Choice)
 {
  const auto* Truck=Choice ? Choice->Truck.Get() : nullptr;
  return Truck ? FString::Printf(TEXT("%s · 도크 %s · 적재 %d개"),*Truck->TruckId,*Truck->DockId,Truck->ManifestPallets.Num()) : TEXT("트럭이 없습니다");
 }

 TSharedRef<SWidget> BuildShipping()
 {
  return Panel(SNew(SScrollBox)+SScrollBox::Slot()[SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[Label(TEXT("출고 배송편 만들기"),20,WarehouseUIStyle::Text,true)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,8)[Live([this]() { return FString::Printf(TEXT("선택 팔레트 %d개를 한 배송편으로 운반합니다."),SelectedPallets().Num()); },16,WarehouseUIStyle::Accent)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,4)[SAssignNew(ShipmentNameInput,SEditableTextBox).Font(WarehouseUIStyle::Font(16)).HintText(FText::FromString(TEXT("배송편 이름 · 비우면 자동 생성")))]
   +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,6)[Label(TEXT("출고 집결 구역"),14,Muted)]
   +SVerticalBox::Slot().AutoHeight()[StringCombo(OutboundZones,SelectedOutboundZone,OutboundZoneCombo,TEXT("출고 구역이 없습니다"))]
   +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,6)[Label(TEXT("배정 트럭"),14,Muted)]
   +SVerticalBox::Slot().AutoHeight()[TruckCombo(false)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,4)[CommandAction(TEXT("outbound"),TEXT("선택 팔레트로 출고 배송편 생성"),[this]()
    { if (Shipping.IsValid()) Report(Shipping->CreateOutbound(ShipmentNameInput->GetText().ToString(),SelectedPallets(),SelectedOutboundZone ? *SelectedOutboundZone : FString(),SelectedOutboundTruck ? SelectedOutboundTruck->Truck.Get() : nullptr),true); },
    [this]() { return ShippingBlockReason(false).IsEmpty(); },true)]
   +SVerticalBox::Slot().AutoHeight()[Live([this]() { const auto Reason=ShippingBlockReason(false);return Reason.IsEmpty() ? TEXT("화물 집결 + 트럭 준비가 완료되면 자동 상차합니다.") : Reason; },14,WarehouseUIStyle::Warning)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,24,0,8)[Label(TEXT("트럭에서 입고 하역"),20,WarehouseUIStyle::Text,true)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)[Label(TEXT("입고 인계 구역"),14,Muted)]
   +SVerticalBox::Slot().AutoHeight()[StringCombo(InboundZones,SelectedInboundZone,InboundZoneCombo,TEXT("입고 구역이 없습니다"))]
   +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,6)[Label(TEXT("하역할 트럭"),14,Muted)]
   +SVerticalBox::Slot().AutoHeight()[TruckCombo(true)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,4)[CommandAction(TEXT("inbound"),TEXT("트럭 명세로 입고 하역 시작"),[this]()
    { if (Shipping.IsValid()) Report(Shipping->StartInbound(SelectedInboundTruck ? SelectedInboundTruck->Truck.Get() : nullptr,SelectedInboundZone ? *SelectedInboundZone : FString()),true); },
    [this]() { return ShippingBlockReason(true).IsEmpty(); })]
   +SVerticalBox::Slot().AutoHeight()[Live([this]() { const auto Reason=ShippingBlockReason(true);return Reason.IsEmpty() ? TEXT("트럭에 실린 팔레트를 지정 구역으로 하나씩 옮깁니다.") : Reason; },14,Muted)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,24,0,8)[Label(TEXT("배송편 진행 현황"),20,WarehouseUIStyle::Text,true)]
   +SVerticalBox::Slot().AutoHeight()[Live([this]() { return ShipmentItems.IsEmpty() ? TEXT("아직 배송편이 없습니다.") : TEXT("배송편을 선택하면 아래에서 취소할 수 있습니다."); },14,Muted)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(SBox).HeightOverride(300)[SAssignNew(ShipmentList,SListView<TSharedPtr<FWarehouseShipment>>)
    .ListItemsSource(&ShipmentItems).SelectionMode(ESelectionMode::Single)
    .OnSelectionChanged_Lambda([this](TSharedPtr<FWarehouseShipment> Item,ESelectInfo::Type) { SelectedShipmentId=Item ? Item->ShipmentId : FString(); })
    .OnGenerateRow_Lambda([](TSharedPtr<FWarehouseShipment> Item,const TSharedRef<STableViewBase>& Owner)
    { return SNew(STableRow<TSharedPtr<FWarehouseShipment>>,Owner).Padding(8)[Live([Item]() { return ShipmentText(*Item); },15)]; })]]
   +SVerticalBox::Slot().AutoHeight()[CommandAction(TEXT("cancelShipment"),TEXT("선택 배송편 취소"),[this]() { if (Shipping.IsValid()) Report(Shipping->CancelShipment(SelectedShipmentId),true); },[this]()
    { const auto* Item=SelectedShipment();return CanUse() && Item && !Finished(*Item) && Item->State!=EWarehouseShipmentState::Departing; })]
   +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[Label(TEXT("상하차가 시작되었거나 해당 팔레트를 지게차가 운반 중이면 취소할 수 없습니다."),14,Muted)]]);
 }

 TSharedRef<SWidget> BuildOperations()
 {
  return SNew(SHorizontalBox)
   +SHorizontalBox::Slot().FillWidth(.38f).Padding(0,0,12,0)[Panel(SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[Label(TEXT("차량 상태 · 운행 관리"),20,WarehouseUIStyle::Text,true)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,12)[Label(TEXT("일시정지와 재개는 해당 차량에만 적용됩니다."),14,Muted)]
    +SVerticalBox::Slot().AutoHeight()[Live([this]() { return FleetItems.IsEmpty() ? TEXT("창고에 지게차가 없습니다. 배치 모드에서 추가하세요.") : FString(); },16,WarehouseUIStyle::Warning)]
    +SVerticalBox::Slot().FillHeight(1)[SAssignNew(FleetList,SListView<TSharedPtr<FDashboardVehicle>>).ListItemsSource(&FleetItems).SelectionMode(ESelectionMode::None)
     .OnGenerateRow_Lambda([this](TSharedPtr<FDashboardVehicle> Item,const TSharedRef<STableViewBase>& Owner)
     {
      return SNew(STableRow<TSharedPtr<FDashboardVehicle>>,Owner).Padding(FMargin(4,12))[SNew(SVerticalBox)
       +SVerticalBox::Slot().AutoHeight()[Live([Item]() { return Item->Name(); },19,WarehouseUIStyle::Accent)]
       +SVerticalBox::Slot().AutoHeight().Padding(0,6)[Live([Item]() { auto* V=Item->Vehicle.Get();return V ? FString::Printf(TEXT("배터리 %.0f%% · 대기 지시 %d건\n%s"),V->BatteryPercent,V->PendingJobs.Num(),*V->Status) : TEXT("삭제된 차량"); },15)]
       +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SHorizontalBox)
        +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,4,0)[Action(TEXT("일시정지"),[this,Item]() { if (CanUse()) Report(Computer->SetVehicleRunning(Item->Vehicle.Get(),false)); },[this,Item]() { auto* V=Item->Vehicle.Get();return CanUse() && V && V->bPowered && !V->IsRemoteControlled(); })]
        +SHorizontalBox::Slot().FillWidth(1)[Action(TEXT("점검 · 재개"),[this,Item]() { if (CanUse()) Report(Computer->SetVehicleRunning(Item->Vehicle.Get(),true)); },[this,Item]() { auto* V=Item->Vehicle.Get();return CanUse() && V && !V->bPowered && !V->IsRemoteControlled(); })]]];
     })])]
   +SHorizontalBox::Slot().FillWidth(.62f)[Panel(SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[Label(TEXT("운반 작업 · 소요 시간"),20,WarehouseUIStyle::Text,true)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,12)[Label(TEXT("지시 접수부터 완료까지의 시뮬레이션 시간입니다. 대기·정지 시간을 포함합니다."),14,Muted)]
    +SVerticalBox::Slot().AutoHeight()[Live([this]() { return TaskItems.IsEmpty() ? TEXT("등록된 운반 작업이 없습니다.") : TEXT("진행 작업 전체와 최근 완료·취소 200건을 표시합니다."); },14,Muted)]
    +SVerticalBox::Slot().FillHeight(1).Padding(0,8)[SAssignNew(TaskList,SListView<TSharedPtr<FWarehouseComputerTask>>).ListItemsSource(&TaskItems).SelectionMode(ESelectionMode::Single)
     .OnSelectionChanged_Lambda([this](TSharedPtr<FWarehouseComputerTask> Item,ESelectInfo::Type) { SelectedTaskId=Item ? Item->JobId : FString(); })
     .OnGenerateRow_Lambda([this](TSharedPtr<FWarehouseComputerTask> Item,const TSharedRef<STableViewBase>& Owner)
     {
      return SNew(STableRow<TSharedPtr<FWarehouseComputerTask>>,Owner).Padding(FMargin(8,12))[SNew(SVerticalBox)
       +SVerticalBox::Slot().AutoHeight()[Live([Item]() { return Item->Source+TEXT(" → ")+Item->Destination; },17)]
       +SVerticalBox::Slot().AutoHeight().Padding(0,5)[Live([Item]() { return (IsValid(Item->Vehicle) ? Item->Vehicle->VehicleName : TEXT("차량 없음"))+TEXT(" · ")+Item->State; },15,WarehouseUIStyle::Accent)]
       +SVerticalBox::Slot().AutoHeight()[Live([this,Item]() { return Item->bSubmissionTimeKnown && Computer.IsValid() ? FString::Printf(TEXT("%s %.1f초 · 접수 %.1f초"),Item->bCompleted ? TEXT("완료 소요") : Item->bCancelled ? TEXT("취소까지") : TEXT("현재 경과"),Computer->GetTaskElapsedSeconds(Item->JobId),Item->SubmittedSimulationSeconds) : TEXT("접수 시각 미기록"); },14,Muted)]];
     })]
    +SVerticalBox::Slot().AutoHeight()[CommandAction(TEXT("cancelTask"),TEXT("선택한 대기 지시 취소"),[this]() { if (CanUse()) Report(Computer->CancelQueuedJob(SelectedTaskId)); },[this]() { return CanCancelTask(); })]
    +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[Label(TEXT("관제에서 내린 대기 지시만 취소할 수 있습니다. 이미 운반 중이면 차량을 일시정지하세요."),14,Muted)])];
 }

 TSharedRef<SWidget> BuildHeld()
 {
  auto Form=SNew(SVerticalBox);
  Form->AddSlot().AutoHeight()[Label(TEXT("새 물류 데이터"),20,WarehouseUIStyle::Text,true)];
  Form->AddSlot().AutoHeight().Padding(0,6,0,16)[Label(TEXT("여기서 만든 물류는 보류 목록에 저장됩니다. 배치 모드의 ‘추가 → 상자’에서 순서대로 놓으세요."),14,Muted)];
  Form->AddSlot().AutoHeight().Padding(0,5)[Label(TEXT("고유 화물 번호"),14,Muted)];
  Form->AddSlot().AutoHeight()[SAssignNew(NewIdInput,SEditableTextBox).Font(WarehouseUIStyle::Font(16)).HintText(FText::FromString(TEXT("예: CARGO-001")))];
  Form->AddSlot().AutoHeight().Padding(0,12,0,5)[Label(TEXT("상품명 / 물류 종류"),14,Muted)];
  Form->AddSlot().AutoHeight()[SAssignNew(NewProductInput,SEditableTextBox).Font(WarehouseUIStyle::Font(16)).HintText(FText::FromString(TEXT("예: 생활용품")))];
  auto Number=[&](const TCHAR* Title,float* Value,float Minimum,float Maximum)
  {
   Form->AddSlot().AutoHeight().Padding(0,12,0,0)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Label(Title,16)]
    +SHorizontalBox::Slot().FillWidth(1)[SNew(SNumericEntryBox<float>).Font(WarehouseUIStyle::Font(16)).MinValue(Minimum).MaxValue(Maximum)
     .Value_Lambda([Value]() { return TOptional<float>(*Value); }).OnValueChanged_Lambda([Value](float NewValue) { *Value=NewValue; })]];
  };
  Number(TEXT("포장 포함 무게 (kg)"),&NewMass,.1f,10000.f);
  Number(TEXT("가로 (cm)"),&NewWidth,5.f,300.f);Number(TEXT("깊이 (cm)"),&NewDepth,5.f,300.f);Number(TEXT("높이 (cm)"),&NewHeight,5.f,300.f);
  Form->AddSlot().AutoHeight().Padding(0,18,0,0)[CommandAction(TEXT("heldCreate"),TEXT("보류 물류 목록에 추가"),[this]()
  {
   if (!CanUse()) return;
   const bool Result=Computer->CreateHeldCargo(FName(*NewIdInput->GetText().ToString().TrimStartAndEnd()),NewProductInput->GetText(),NewMass,FVector(NewWidth,NewDepth,NewHeight));
   Report(Result);if (Result) NewIdInput->SetText(FText::GetEmpty());
  },[this]() { return CanUse() && !NewIdInput->GetText().ToString().TrimStartAndEnd().IsEmpty() && !NewProductInput->GetText().ToString().TrimStartAndEnd().IsEmpty(); },true)];
  return SNew(SHorizontalBox)
   +SHorizontalBox::Slot().FillWidth(.58f).Padding(0,0,12,0)[Panel(SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[Label(TEXT("보류된 물류"),20,WarehouseUIStyle::Text,true)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,12)[Label(TEXT("맨 위 항목부터 맵에 추가됩니다. 품목·무게·크기 데이터는 그대로 보관됩니다."),14,Muted)]
    +SVerticalBox::Slot().AutoHeight()[Live([this]() { return HeldItems.IsEmpty() ? TEXT("보류된 물류가 없습니다.") : FString::Printf(TEXT("대기 %d개"),HeldItems.Num()); },16,WarehouseUIStyle::Accent)]
    +SVerticalBox::Slot().FillHeight(1).Padding(0,8)[SAssignNew(HeldList,SListView<TSharedPtr<FWarehouseHeldCargo>>).ListItemsSource(&HeldItems).SelectionMode(ESelectionMode::Single)
     .OnSelectionChanged_Lambda([this](TSharedPtr<FWarehouseHeldCargo> Item,ESelectInfo::Type) { SelectedHeldId=Item ? Item->CargoId : NAME_None;bConfirmDeleteHeld=false; })
     .OnGenerateRow_Lambda([this](TSharedPtr<FWarehouseHeldCargo> Item,const TSharedRef<STableViewBase>& Owner)
     {
      return SNew(STableRow<TSharedPtr<FWarehouseHeldCargo>>,Owner).Padding(FMargin(8,12))[SNew(SVerticalBox)
       +SVerticalBox::Slot().AutoHeight()[Label(Item->Product.ToString(),18)]
       +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Label(Item->CargoId.ToString(),14,Muted)]
       +SVerticalBox::Slot().AutoHeight()[Label(FString::Printf(TEXT("%.1f kg · %.0f × %.0f × %.0f cm"),Item->MassKg,Item->SizeCm.X,Item->SizeCm.Y,Item->SizeCm.Z),15,WarehouseUIStyle::Accent)]];
     })]
    +SVerticalBox::Slot().AutoHeight()[CommandAction(TEXT("heldDelete"),TEXT("선택 데이터 영구 삭제"),[this]() { bConfirmDeleteHeld=true; },[this]() { return CanUse() && HeldItems.ContainsByPredicate([this](const auto& Item) { return Item->CargoId==SelectedHeldId; }); })]
    +SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)[SNew(SBox).Visibility_Lambda([this]() { return bConfirmDeleteHeld ? EVisibility::Visible : EVisibility::Collapsed; })
     [SNew(SVerticalBox)
      +SVerticalBox::Slot().AutoHeight()[Live([this]() { return SelectedHeldId.ToString()+TEXT(" 데이터를 영구 삭제합니다. 복원할 수 없습니다."); },14,WarehouseUIStyle::Warning)]
      +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SHorizontalBox)
       +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[CommandAction(TEXT("heldConfirm"),TEXT("삭제 확인"),[this]() { if (CanUse()) Report(Computer->DeleteHeldCargo(SelectedHeldId));bConfirmDeleteHeld=false; },[this]() { return CanUse() && bConfirmDeleteHeld && HeldItems.ContainsByPredicate([this](const auto& Item) { return Item->CargoId==SelectedHeldId; }); })]
       +SHorizontalBox::Slot().FillWidth(1)[Action(TEXT("취소"),[this]() { bConfirmDeleteHeld=false; })]]]])]
   +SHorizontalBox::Slot().FillWidth(.42f)[Panel(SNew(SScrollBox)+SScrollBox::Slot()[Form])];
 }

 const FWarehouseInventoryItem* SelectedItem() const
 {
  if (!Computer.IsValid() || !InventoryList) return nullptr;
  for (const auto& Item:InventoryList->GetSelectedItems())
   if (Item) if (const auto* Current=Computer->Inventory.FindByPredicate([&](const auto& Row) { return Row.Cargo==Item->Cargo; })) return Current;
  return nullptr;
 }
 TArray<AWarehousePallet*> SelectedPallets() const
 {
  TArray<AWarehousePallet*> Result;
  if (Computer.IsValid() && InventoryList) for (const auto& Item:InventoryList->GetSelectedItems())
   if (Item) if (const auto* Current=Computer->Inventory.FindByPredicate([&](const auto& Row) { return Row.Cargo==Item->Cargo; });Current && IsValid(Current->Pallet)) Result.AddUnique(Current->Pallet);
  return Result;
 }
 FString SelectionText() const
 {
  const auto* Item=SelectedItem();if (!Item) return TEXT("왼쪽 목록에서 상품을 선택하세요.");
  const int32 Count=SelectedPallets().Num();
  if (Count>1) return FString::Printf(TEXT("팔레트 %d개 선택됨\n단건 운반은 한 팔레트만 선택하세요. 여러 팔레트는 ‘배송편’에서 지시합니다."),Count);
  if (!IsValid(Item->Pallet)) return Item->Product+TEXT("\n팔레트에 실리지 않은 개별 상자입니다.");
  int32 Boxes=0;float Mass=Item->Pallet->PalletMassKg;
  for (const auto& Other:Computer->Inventory) if (Other.Pallet==Item->Pallet) { ++Boxes;Mass+=Other.MassKg; }
  return FString::Printf(TEXT("%s\n%s\n\n팔레트 전체 · 상자 %d개\n총 무게 %.1f kg (팔레트 포함)\n%s"),*Item->Product,*Item->Location,Boxes,Mass,*Item->ReceiptStatus);
 }
 FString TransportBlockReason() const
 {
  if (!CanUse()) return TEXT("컴퓨터를 사용할 수 없습니다.");
  const auto* Item=SelectedItem();if (!Item) return TEXT("먼저 운반할 상품을 선택하세요.");
  // SListView does not promise selected-item order. Validate every current row
  // before choosing the representative parcel for a whole-pallet dispatch.
  for (const auto& Selected:InventoryList->GetSelectedItems())
  {
   const auto* Current=Selected ? Computer->Inventory.FindByPredicate([&](const auto& Row) { return Row.Cargo==Selected->Cargo; }) : nullptr;
   if (!Current || !IsValid(Current->Cargo)) return TEXT("선택한 물류가 변경되었습니다. 목록에서 다시 선택하세요.");
   if (!IsValid(Current->Pallet)) return TEXT("선택 목록에 개별 상자가 있습니다. 팔레트에 올리거나 선택을 해제하세요.");
  }
  if (SelectedPallets().Num()!=1) return TEXT("한 팔레트만 선택하거나 ‘배송편’ 탭을 사용하세요.");
  if (Item->bReserved) return TEXT("이미 예약되었거나 운반 중인 팔레트입니다.");
  if (FleetItems.IsEmpty()) return TEXT("창고에 지게차가 없습니다.");
  if (!SelectedDest || !DestChoices.Contains(SelectedDest)) return TEXT("사용 가능한 출고 자리가 없습니다.");
  if (SelectedVehicle && SelectedVehicle!=AutomaticVehicle && (!SelectedVehicle->Vehicle.IsValid() || !SelectedVehicle->Vehicle->CanAcceptNewWork()))
   return TEXT("선택 차량의 전원·고장·배터리·원격 조작 상태를 확인하세요.");
  if ((!SelectedVehicle || SelectedVehicle==AutomaticVehicle) && !FleetItems.ContainsByPredicate([](const auto& Choice)
      { const auto* Vehicle=Choice->Vehicle.Get();return Vehicle && Vehicle->CanAcceptNewWork() && Vehicle->ActiveJob.JobId.IsEmpty() && Vehicle->PendingJobs.IsEmpty(); }))
   return TEXT("자동 배정할 대기 차량이 없습니다. 차량 상태를 확인하거나 작업 중인 차량을 직접 선택해 대기열에 넣으세요.");
  return FString();
 }
 FString ShippingBlockReason(bool Inbound) const
 {
  if (!CanUse() || !Shipping.IsValid()) return TEXT("배송 시스템을 사용할 수 없습니다.");
  const auto& Zone=Inbound ? SelectedInboundZone : SelectedOutboundZone;
  if (!Zone) return Inbound ? TEXT("입고 구역이 없습니다.") : TEXT("출고 구역이 없습니다.");
  const auto& Choice=Inbound ? SelectedInboundTruck : SelectedOutboundTruck;
  auto* Truck=Choice ? Choice->Truck.Get() : nullptr;
  if (!Truck) return TEXT("트럭을 선택하세요.");
  if (Truck->State==EWarehouseTruckState::Departing || Truck->State==EWarehouseTruckState::Departed || Truck->State==EWarehouseTruckState::Unavailable) return TEXT("현재 사용할 수 없는 트럭입니다.");
  if (!Truck->IsManifestReady()) return TEXT("트럭의 초기 화물 확인을 기다리고 있습니다.");
  for (const auto& Shipment:Shipping->Shipments) if (!Finished(Shipment) && Shipment.Truck==Truck) return TEXT("이 트럭은 다른 배송편에 배정되어 있습니다.");
  if (Inbound) return Truck->ManifestPallets.IsEmpty() ? TEXT("선택한 트럭에 하역할 팔레트가 없습니다.") : FString();
  if (!Truck->ManifestPallets.IsEmpty()) return TEXT("출고에는 비어 있는 트럭을 선택하세요.");
  const auto Pallets=SelectedPallets();if (Pallets.IsEmpty()) return TEXT("왼쪽에서 출고할 팔레트의 상품을 체크하세요.");
  if (InventoryList) for (const auto& Item:InventoryList->GetSelectedItems())
   if (!IsValid(Item->Pallet)) return TEXT("선택한 개별 상자를 먼저 팔레트에 올리거나 선택을 해제하세요.");
  for (auto* Pallet:Pallets) if (!Shipping->GetPalletShipment(Pallet).IsEmpty()) return TEXT("선택 목록에 다른 배송편에 예약된 팔레트가 있습니다.");
  if (Pallets.Num()>Truck->LoadSlots.Num()) return TEXT("선택 팔레트 수가 트럭 적재 자리를 초과합니다.");
  return FString();
 }
 const FWarehouseShipment* SelectedShipment() const
 {
  return Shipping.IsValid() ? Shipping->Shipments.FindByPredicate([this](const auto& Item) { return Item.ShipmentId==SelectedShipmentId; }) : nullptr;
 }
 bool CanCancelTask() const
 {
  if (!CanUse() || SelectedTaskId.IsEmpty()) return false;
  for (const auto& Item:FleetItems) if (auto* Vehicle=Item->Vehicle.Get())
   if (Vehicle->PendingJobs.ContainsByPredicate([this](const auto& Job) { return Job.JobId==SelectedTaskId && Job.SourceSystem==TEXT("FMS-COMPUTER"); })) return true;
  return false;
 }
 static FString ShipmentText(const FWarehouseShipment& Item)
 {
  int32 Arrived=0,Loaded=0;for (const auto& Line:Item.Lines) { Arrived+=Line.bArrived;Loaded+=Line.bLoaded; }
  const bool Inbound=Item.Direction==EWarehouseShipmentDirection::Inbound;
  FString Text=FString::Printf(TEXT("%s · %s\n%s · %s\n%s %d/%d · %s %d/%d · %.1f초\n%s"),
   Inbound ? TEXT("입고") : TEXT("출고"),*Item.Name,*Item.ZoneId,IsValid(Item.Truck) ? *Item.Truck->TruckId : TEXT("트럭 없음"),
   Inbound ? TEXT("인계") : TEXT("집결"),Arrived,Item.Lines.Num(),Inbound ? TEXT("트럭 잔량") : TEXT("상차"),Loaded,Item.Lines.Num(),Item.ElapsedSeconds,*Item.Status);
  if (!Inbound) Text+=FString::Printf(TEXT("\n화물 준비 %s · 트럭 준비 %s"),Item.bCargoReady ? TEXT("완료") : TEXT("대기"),Item.bTruckReady ? TEXT("완료") : TEXT("대기"));
  else Text+=FString::Printf(TEXT("\n트럭 준비 %s"),Item.bTruckReady ? TEXT("완료") : TEXT("대기"));
  if (!Item.WaitReason.IsEmpty()) Text+=TEXT("\n확인: ")+Item.WaitReason;
  for (const auto& Line:Item.Lines) Text+=TEXT("\n  ")+Line.PalletLabel+TEXT(" · ")+Line.Status;
  return Text;
 }

 void FilterInventory()
 {
  InventoryItems.Reset();if (!Computer.IsValid()) return;
  for (const auto& Row:Computer->Inventory)
  {
   const bool Matches=Search.IsEmpty() || (Row.CargoId+Row.Product+Row.Location+Row.ReceiptStatus).Contains(Search);
   const bool Included=InventoryFilter==0 || (InventoryFilter==1 && IsValid(Row.Pallet) && !Row.bReserved) || (InventoryFilter==2 && Row.bReserved) || (InventoryFilter==3 && !IsValid(Row.Pallet));
   if (Matches && Included) if (const auto* Item=InventoryCache.Find(Row.Cargo.Get())) InventoryItems.Add(*Item);
  }
  if (InventoryList)
  {
   // Hidden or removed rows must not silently remain in an outbound manifest.
   for (const auto& Selected:InventoryList->GetSelectedItems()) if (!InventoryItems.Contains(Selected)) InventoryList->SetItemSelection(Selected,false);
   InventoryList->RequestListRefresh();
  }
 }
 static void RefreshStrings(TArray<TSharedPtr<FString>>& Items,TSharedPtr<FString>& Selected,const TArray<FString>& Values,const TSharedPtr<SComboBox<TSharedPtr<FString>>>& Combo)
 {
  bool Changed=Items.Num()!=Values.Num();
  for (int32 I=0;!Changed && I<Values.Num();++I) Changed=*Items[I]!=Values[I];
  if (!Changed) return;
  const FString Previous=Selected ? *Selected : FString();Items.Reset();Selected.Reset();
  for (const auto& Value:Values) { auto Item=MakeShared<FString>(Value);Items.Add(Item);if (Value==Previous) Selected=Item; }
  if (!Selected && !Items.IsEmpty()) Selected=Items[0];
  if (Combo) { Combo->RefreshOptions();Combo->SetSelectedItem(Selected); }
 }
 void Refresh()
 {
  if (!Computer.IsValid()) return;
  LastRefresh=Computer->RefreshCount;InventoryCount=Computer->Inventory.Num();
  TSet<TWeakObjectPtr<AWarehouseCargo>> Existing;TSet<AWarehousePallet*> Pallets;
  for (const auto& Row:Computer->Inventory)
  {
   Existing.Add(Row.Cargo.Get());auto& Cached=InventoryCache.FindOrAdd(Row.Cargo.Get());if (!Cached) Cached=MakeShared<FWarehouseInventoryItem>();*Cached=Row;
   if (IsValid(Row.Pallet)) Pallets.Add(Row.Pallet);
  }
  for (auto It=InventoryCache.CreateIterator();It;++It) if (!Existing.Contains(It.Key())) It.RemoveCurrent();
  PalletCount=Pallets.Num();FilterInventory();
  TArray<TSharedPtr<FDashboardVehicle>> NewFleet;IdleVehicleCount=0;
  for (AWarehouseForklift* Vehicle:Computer->Fleet) if (IsValid(Vehicle))
  {
   const auto* ExistingItem=FleetItems.FindByPredicate([Vehicle](const auto& Item) { return Item->Vehicle==Vehicle; });
   auto Item=ExistingItem ? *ExistingItem : MakeShared<FDashboardVehicle>();Item->Vehicle=Vehicle;NewFleet.Add(Item);
   if (Vehicle->CanAcceptNewWork() && Vehicle->ActiveJob.JobId.IsEmpty() && Vehicle->PendingJobs.IsEmpty()) ++IdleVehicleCount;
  }
  const bool Changed=NewFleet!=FleetItems;FleetItems=MoveTemp(NewFleet);
  VehicleChoices=FleetItems;VehicleChoices.Insert(AutomaticVehicle,0);
  if (!SelectedVehicle || !VehicleChoices.Contains(SelectedVehicle)) SelectedVehicle=AutomaticVehicle;
  if (Changed)
  {
   if (VehicleCombo) { VehicleCombo->RefreshOptions();VehicleCombo->SetSelectedItem(SelectedVehicle); }
   if (FleetList) FleetList->RequestListRefresh();
  }
  TArray<FString> Destinations;
  for (int32 I=0;I<Computer->OutboundDestinations.Num();++I) Destinations.Add(FString::Printf(TEXT("출고 대기 자리 %02d"),I+1));
  RefreshStrings(DestChoices,SelectedDest,Destinations,DestCombo);
  ActiveTaskCount=0;CompletedTaskCount=0;TaskItems.Reset();TSet<FString> CurrentTasks;
  for (int32 I=Computer->Tasks.Num()-1;I>=0;--I)
  {
   const auto& Task=Computer->Tasks[I];auto& Item=TaskCache.FindOrAdd(Task.JobId);if (!Item) Item=MakeShared<FWarehouseComputerTask>();*Item=Task;TaskItems.Add(Item);CurrentTasks.Add(Task.JobId);
   if (Task.bCompleted) ++CompletedTaskCount;else if (!Task.bCancelled) ++ActiveTaskCount;
  }
  for (auto It=TaskCache.CreateIterator();It;++It) if (!CurrentTasks.Contains(It.Key())) It.RemoveCurrent();
  if (!CurrentTasks.Contains(SelectedTaskId)) SelectedTaskId.Reset();
  if (TaskList) TaskList->RequestListRefresh();
  RefreshShipping();
 }
 void RefreshShipping()
 {
  Shipping=AWarehouseShippingSystem::Find(Computer.Get());
  TArray<FString> Inbound,Outbound;TArray<TSharedPtr<FDashboardTruck>> Trucks;
  ShipmentItems.Reset();TSet<FString> CurrentShipments;
  if (Shipping.IsValid())
  {
   for (const auto& Zone:Shipping->Zones) if (IsValid(Zone.Anchor) && !Zone.Slots.IsEmpty()) (Zone.bInbound ? Inbound : Outbound).AddUnique(Zone.ZoneId);
   for (AWarehouseLogisticsTruck* Truck:Shipping->Trucks) if (IsValid(Truck))
   {
    const auto* Existing=TruckChoices.FindByPredicate([Truck](const auto& Choice) { return Choice->Truck==Truck; });
    auto Choice=Existing ? *Existing : MakeShared<FDashboardTruck>();Choice->Truck=Truck;Trucks.Add(Choice);
   }
   for (int32 I=Shipping->Shipments.Num()-1;I>=0;--I)
   {
    const auto& Shipment=Shipping->Shipments[I];auto& Item=ShipmentCache.FindOrAdd(Shipment.ShipmentId);if (!Item) Item=MakeShared<FWarehouseShipment>();*Item=Shipment;ShipmentItems.Add(Item);CurrentShipments.Add(Shipment.ShipmentId);
   }
  }
  for (auto It=ShipmentCache.CreateIterator();It;++It) if (!CurrentShipments.Contains(It.Key())) It.RemoveCurrent();
  if (!CurrentShipments.Contains(SelectedShipmentId)) SelectedShipmentId.Reset();
  if (ShipmentList) ShipmentList->RequestListRefresh();
  RefreshStrings(InboundZones,SelectedInboundZone,Inbound,InboundZoneCombo);
  RefreshStrings(OutboundZones,SelectedOutboundZone,Outbound,OutboundZoneCombo);
  const bool Changed=Trucks!=TruckChoices;TruckChoices=MoveTemp(Trucks);
  auto Retain=[this](TSharedPtr<FDashboardTruck>& Selected,bool InboundValue)
  {
   if (Selected && TruckChoices.Contains(Selected)) return;
   Selected.Reset();for (const auto& Item:TruckChoices) if (auto* Truck=Item->Truck.Get();Truck && (InboundValue ? !Truck->ManifestPallets.IsEmpty() : Truck->ManifestPallets.IsEmpty())) { Selected=Item;break; }
   if (!Selected && !TruckChoices.IsEmpty()) Selected=TruckChoices[0];
  };
  Retain(SelectedInboundTruck,true);Retain(SelectedOutboundTruck,false);
  if (Changed)
  {
   if (InboundTruckCombo) { InboundTruckCombo->RefreshOptions();InboundTruckCombo->SetSelectedItem(SelectedInboundTruck); }
   if (OutboundTruckCombo) { OutboundTruckCombo->RefreshOptions();OutboundTruckCombo->SetSelectedItem(SelectedOutboundTruck); }
  }
 }
 void RefreshHeld()
 {
  auto* Store=Computer.IsValid() ? AWarehouseLayoutInventory::Find(Computer.Get()) : nullptr;
  if (!Store) return;
  HeldRevision=Store->Revision;HeldItems.Reset();TSharedPtr<FWarehouseHeldCargo> Selected;
  for (const auto& Cargo:Store->HeldCargo) { auto Item=MakeShared<FWarehouseHeldCargo>(Cargo);HeldItems.Add(Item);if (Item->CargoId==SelectedHeldId) Selected=Item; }
  if (!Selected) { SelectedHeldId=NAME_None;bConfirmDeleteHeld=false; }
  if (HeldList) { HeldList->RequestListRefresh();HeldList->SetSelection(Selected); }
 }

#if WITH_EDITOR && !UE_BUILD_SHIPPING
public:
 // Editor-only regression bridge. It drives the same Slate widgets as user input;
 // it never substitutes backend calls for a command button or changes simulation state itself.
 void Test(const TArray<FString>& Args)
 {
  bool Accepted=Args.IsEmpty() || Args[0]==TEXT("snapshot");
  if (Args.Num()>=2 && (Args[0]==TEXT("tab") || Args[0]==TEXT("filter")))
  {
   const int32 Index=FCString::Atoi(*Args[1]);const auto& Buttons=Args[0]==TEXT("tab") ? TabButtons : FilterButtons;
   if (Buttons.IsValidIndex(Index)) { Buttons[Index]->SimulateClick();Accepted=true; }
  }
  else if (Args.Num()>=2 && (Args[0]==TEXT("select") || Args[0]==TEXT("addSelection")))
  {
   if (const auto* Item=InventoryItems.FindByPredicate([&](const auto& Row) { return Row->CargoId==Args[1]; }))
   {
    if (Args[0]==TEXT("select")) InventoryList->ClearSelection();
    InventoryList->SetItemSelection(*Item,true);InventoryList->RequestScrollIntoView(*Item);Accepted=true;
   }
  }
  else if (!Args.IsEmpty() && Args[0]==TEXT("search"))
  { SearchBox->SetText(FText::FromString(Args.Num()>1 ? Args[1] : FString()));Accepted=true; }
  else if (Args.Num()>=2 && Args[0]==TEXT("click"))
  {
   if (const auto* Button=CommandButtons.Find(Args[1]))
   {
    // Inactive switcher pages have not run a Slate attribute prepass yet.
    // Evaluate the widget's real binding before simulating input, including
    // when a form/selection changed earlier in this same editor tick.
    (*Button)->UpdateAllAttributes();
    if ((*Button)->IsEnabled()) { (*Button)->SimulateClick();Accepted=true; }
   }
  }
  else if (Args.Num()>=2 && Args[0]==TEXT("selectHeld"))
  {
   if (const auto* Item=HeldItems.FindByPredicate([&](const auto& Row) { return Row->CargoId.ToString()==Args[1]; }))
   { HeldList->SetSelection(*Item);Accepted=true; }
  }
  else if (Args.Num()==7 && Args[0]==TEXT("form"))
  {
   NewIdInput->SetText(FText::FromString(Args[1]));NewProductInput->SetText(FText::FromString(Args[2]));
   NewMass=FCString::Atof(*Args[3]);NewWidth=FCString::Atof(*Args[4]);NewDepth=FCString::Atof(*Args[5]);NewHeight=FCString::Atof(*Args[6]);
   Accepted=true;
  }
  auto Quote=[](FString Value)
  { return TEXT("\"")+Value.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\"")).Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"))+TEXT("\""); };
  TArray<FString> SelectedIds,Buttons;
  for (const auto& Item:InventoryList->GetSelectedItems()) SelectedIds.Add(Quote(Item->CargoId));
  for (const auto& Entry:CommandButtons)
  {
   Entry.Value->UpdateAllAttributes();
   Buttons.Add(Quote(Entry.Key)+TEXT(":")+(Entry.Value->IsEnabled() ? TEXT("true") : TEXT("false")));
  }
  const FVector2D Size=GetCachedGeometry().GetLocalSize();
  FString Json=FString::Printf(TEXT("{\"accepted\":%s,\"page\":%d,\"refresh\":%d,\"inventoryShown\":%d,\"inventoryTotal\":%d,\"selectedPalletCount\":%d,\"vehicleCount\":%d,\"destinationCount\":%d,\"heldCount\":%d,\"width\":%.1f,\"height\":%.1f,\"selectedCargoIds\":[%s],\"actionsEnabled\":{%s},\"transportBlockReason\":%s,\"notice\":%s}"),
   Accepted ? TEXT("true") : TEXT("false"),Page,LastRefresh,InventoryItems.Num(),InventoryCount,SelectedPallets().Num(),FleetItems.Num(),DestChoices.Num(),HeldItems.Num(),Size.X,Size.Y,
   *FString::Join(SelectedIds,TEXT(",")),*FString::Join(Buttons,TEXT(",")),*Quote(TransportBlockReason()),*Quote(Notice));
  const FString Directory=FPaths::ProjectSavedDir()/TEXT("Testing/ComputerDashboard");IFileManager::Get().MakeDirectory(*Directory,true);
  FFileHelper::SaveStringToFile(Json,*(Directory/TEXT("State.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 }
#endif
};
#if WITH_EDITOR && !UE_BUILD_SHIPPING
TWeakPtr<SWarehouseDashboard> TestDashboard;
FAutoConsoleCommand DashboardTestCommand(TEXT("Warehouse.Computer.Test"),TEXT("Editor-only Slate dashboard regression: snapshot/tab/filter/select/search/click."),
 FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) { if (auto Screen=TestDashboard.Pin()) Screen->Test(Args); }));
#endif
}

TSharedRef<SWidget> MakeWarehouseComputerDashboard(AWarehouseComputer* Computer,Amsc_vrPlayerController* Controller)
{
 auto Dashboard=SNew(WarehouseDashboard::SWarehouseDashboard).Computer(Computer).Controller(Controller);
#if WITH_EDITOR && !UE_BUILD_SHIPPING
 WarehouseDashboard::TestDashboard=Dashboard;
#endif
 return Dashboard;
}
