#include "msc_vrPlayerController.h"
#include "msc_vrCharacter.h"
#include "WarehouseComputer.h"
#include "WarehouseCargo.h"
#include "WarehousePallet.h"
#include "WarehouseForklift.h"
#include "WarehouseUIStyle.h"
#include "WarehouseLayoutInventory.h"
#include "WarehouseShippingSystem.h"
#include "WarehouseLogisticsTruck.h"
#include "Engine/GameViewportClient.h"
#include "Camera/PlayerCameraManager.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Views/SListView.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
struct FVehicleChoice
{
 TWeakObjectPtr<AWarehouseForklift> Vehicle;
 FString Title;
};
struct FTruckChoice
{
 TWeakObjectPtr<AWarehouseLogisticsTruck> Truck;
};
class SWarehouseComputerScreen : public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SWarehouseComputerScreen) {}
 SLATE_ARGUMENT(TWeakObjectPtr<AWarehouseComputer>,Computer)
 SLATE_ARGUMENT(TWeakObjectPtr<Amsc_vrPlayerController>,Controller)
 SLATE_END_ARGS()
 void Construct(const FArguments& Args)
 {
  Computer=Args._Computer;Controller=Args._Controller;
  Shipping=AWarehouseShippingSystem::Find(Computer.Get());
  auto Automatic=MakeShared<FVehicleChoice>();Automatic->Title=TEXT("자동 배정 · 가까운 대기 차량");VehicleChoices.Add(Automatic);SelectedVehicle=Automatic;
  for (AWarehouseForklift* V : Computer->Fleet)
  { auto Choice=MakeShared<FVehicleChoice>();Choice->Vehicle=V;Choice->Title=V->VehicleName;VehicleChoices.Add(Choice); }
  for (int32 I=0;I<Computer->OutboundDestinations.Num();++I) DestChoices.Add(MakeShared<FString>(FString::Printf(TEXT("출고 대기 %02d"),I+1)));
  if (!DestChoices.IsEmpty()) SelectedDest=DestChoices[0];
  RefreshInventory();
  RefreshHeld();
  static const FSearchBoxStyle SearchStyle=[]()
  {
   FSearchBoxStyle Style=FCoreStyle::Get().GetWidgetStyle<FSearchBoxStyle>("SearchBox");
   Style.TextBoxStyle.SetFont(WarehouseUIStyle::Font(WarehouseUIStyle::LabelSize));
   Style.SetActiveFont(WarehouseUIStyle::Font(WarehouseUIStyle::LabelSize));return Style;
  }();
  auto Right=SNew(SVerticalBox);
  auto Text=[&](const FString& Value,int32 Size=WarehouseUIStyle::BodySize)
  { Right->AddSlot().AutoHeight().Padding(0,6)[SNew(STextBlock).Font(WarehouseUIStyle::Font(Size)).Text(FText::FromString(Value)).AutoWrapText(true).ColorAndOpacity(WarehouseUIStyle::Text)]; };
  auto Button=[&](const FString& Label,TFunction<void()> Action)
  {
   Right->AddSlot().AutoHeight().Padding(0,5)[SNew(SButton).ContentPadding(12).IsFocusable(false)
    [SNew(STextBlock).Font(WarehouseUIStyle::Font()).ColorAndOpacity(WarehouseUIStyle::Text).Text(FText::FromString(Label))]
    .OnClicked_Lambda([Action]() { Action();return FReply::Handled(); })];
  };
  Text(TEXT("배송편 · 트럭 상하차"),WarehouseUIStyle::TitleSize);
  Text(TEXT("왼쪽에서 Ctrl / Shift로 여러 상품을 선택하세요. 같은 팔레트는 한 번만 포함합니다."),WarehouseUIStyle::CompactSize);
  Right->AddSlot().AutoHeight().Padding(0,4)[SNew(STextBlock).Font(WarehouseUIStyle::Font()).ColorAndOpacity(WarehouseUIStyle::Accent)
   .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("배송편 명세: 선택 팔레트 %d개"),SelectedPallets().Num())); }).AutoWrapText(true)];
  Right->AddSlot().AutoHeight().Padding(0,4)[SAssignNew(ShipmentNameInput,SEditableTextBox).Font(WarehouseUIStyle::Font()).HintText(FText::FromString(TEXT("배송편 이름 (비우면 자동 생성)")))];
  auto ZoneCombo=[this](bool Inbound)->TSharedRef<SWidget>
  {
   auto& Choices=Inbound ? InboundZoneChoices : OutboundZoneChoices;
   auto& Selected=Inbound ? SelectedInboundZone : SelectedOutboundZone;
   auto& Combo=Inbound ? InboundZoneCombo : OutboundZoneCombo;
   return SAssignNew(Combo,SComboBox<TSharedPtr<FString>>).OptionsSource(&Choices).InitiallySelectedItem(Selected)
    .OnGenerateWidget_Lambda([](TSharedPtr<FString> Choice) { return SNew(STextBlock).Font(WarehouseUIStyle::Font()).Text(FText::FromString(*Choice)); })
    .OnSelectionChanged_Lambda([this,Inbound](TSharedPtr<FString> Choice,ESelectInfo::Type) { (Inbound ? SelectedInboundZone : SelectedOutboundZone)=Choice; })
    [SNew(STextBlock).Font(WarehouseUIStyle::Font()).Text_Lambda([this,Inbound]() { const auto& Value=Inbound ? SelectedInboundZone : SelectedOutboundZone;return FText::FromString(Value ? *Value : TEXT("사용 가능한 구역 없음")); })];
  };
  auto TruckCombo=[this](bool Inbound)->TSharedRef<SWidget>
  {
   auto& Selected=Inbound ? SelectedInboundTruck : SelectedOutboundTruck;
   auto& Combo=Inbound ? InboundTruckCombo : OutboundTruckCombo;
   return SAssignNew(Combo,SComboBox<TSharedPtr<FTruckChoice>>).OptionsSource(&TruckChoices).InitiallySelectedItem(Selected)
    .OnGenerateWidget_Lambda([](TSharedPtr<FTruckChoice> Choice) { return SNew(STextBlock).Font(WarehouseUIStyle::Font()).AutoWrapText(true).ColorAndOpacity(WarehouseUIStyle::Text).Text_Lambda([Choice]() { return FText::FromString(TruckText(Choice)); }); })
    .OnSelectionChanged_Lambda([this,Inbound](TSharedPtr<FTruckChoice> Choice,ESelectInfo::Type) { (Inbound ? SelectedInboundTruck : SelectedOutboundTruck)=Choice; })
    [SNew(STextBlock).Font(WarehouseUIStyle::Font()).AutoWrapText(true).ColorAndOpacity(WarehouseUIStyle::Text).Text_Lambda([this,Inbound]() { return FText::FromString(TruckText(Inbound ? SelectedInboundTruck : SelectedOutboundTruck)); })];
  };
  Text(TEXT("출고 집결 구역 / 배정 트럭"));
  Right->AddSlot().AutoHeight().Padding(0,3)[ZoneCombo(false)];
  Right->AddSlot().AutoHeight().Padding(0,3)[TruckCombo(false)];
  Button(TEXT("선택 팔레트로 출고 배송편 생성"),[this]()
  {
   if (Shipping.IsValid()) Shipping->CreateOutbound(ShipmentNameInput->GetText().ToString().TrimStartAndEnd(),SelectedPallets(),
    SelectedOutboundZone ? *SelectedOutboundZone : FString(),SelectedOutboundTruck ? SelectedOutboundTruck->Truck.Get() : nullptr);
   if (Computer.IsValid()) Computer->RefreshState();RefreshShippingChoices();
  });
  Text(TEXT("명세 전량을 내려놓고 포크를 뺀 뒤, 배정 트럭이 준비되면 자동 상차합니다."),WarehouseUIStyle::CompactSize);
  Text(TEXT("입고 인계 구역 / 하역할 트럭"));
  Right->AddSlot().AutoHeight().Padding(0,3)[ZoneCombo(true)];
  Right->AddSlot().AutoHeight().Padding(0,3)[TruckCombo(true)];
  Button(TEXT("트럭 명세로 입고 하역 시작"),[this]()
  {
   if (Shipping.IsValid()) Shipping->StartInbound(SelectedInboundTruck ? SelectedInboundTruck->Truck.Get() : nullptr,
    SelectedInboundZone ? *SelectedInboundZone : FString());
   if (Computer.IsValid()) Computer->RefreshState();RefreshShippingChoices();
  });
  Text(TEXT("상하차는 일정 간격마다 팔레트 묶음을 즉시 옮깁니다. 창고 안 운반은 기존 지게차가 수행합니다."),WarehouseUIStyle::CompactSize);
  Right->AddSlot().AutoHeight().Padding(0,6)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::CompactSize)).ColorAndOpacity(WarehouseUIStyle::Warning)
   .Text_Lambda([this]() { return FText::FromString(Shipping.IsValid() ? Shipping->LastMessage : TEXT("물류 시스템 연결 없음")); }).AutoWrapText(true)];
  Text(TEXT("배송편 명세 · 준비 상태 · 경과 시간"));
  Right->AddSlot().AutoHeight()[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::CompactSize)).ColorAndOpacity(WarehouseUIStyle::Text)
   .Text_Lambda([this]() { return FText::FromString(ShipmentsText()); }).AutoWrapText(true)];
  Right->AddSlot().AutoHeight().Padding(0,4)[SAssignNew(ShipmentCombo,SComboBox<TSharedPtr<FString>>).OptionsSource(&ShipmentChoices).InitiallySelectedItem(SelectedShipment)
   .OnGenerateWidget_Lambda([](TSharedPtr<FString> Choice) { return SNew(STextBlock).Font(WarehouseUIStyle::Font()).Text(FText::FromString(*Choice)); })
   .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Choice,ESelectInfo::Type) { SelectedShipment=Choice; })
   [SNew(STextBlock).Font(WarehouseUIStyle::Font()).Text_Lambda([this]() { return FText::FromString(SelectedShipment ? *SelectedShipment : TEXT("취소할 배송편 없음")); })]];
  Button(TEXT("선택 배송편 취소"),[this]()
  { if (Shipping.IsValid()) Shipping->CancelShipment(SelectedShipment ? *SelectedShipment : FString());RefreshShippingChoices(); });
  Text(TEXT("운반 작업 지시"),WarehouseUIStyle::TitleSize);
  Right->AddSlot().AutoHeight().Padding(0,8)[SNew(STextBlock).Font(WarehouseUIStyle::Font()).ColorAndOpacity(WarehouseUIStyle::Accent)
   .Text_Lambda([this]() { return FText::FromString(SelectionText()); }).AutoWrapText(true)];
  Text(TEXT("담당 지게차"));
  Right->AddSlot().AutoHeight()[SNew(SComboBox<TSharedPtr<FVehicleChoice>>).OptionsSource(&VehicleChoices).InitiallySelectedItem(SelectedVehicle)
   .OnGenerateWidget_Lambda([](TSharedPtr<FVehicleChoice> Choice) { return SNew(STextBlock).Font(WarehouseUIStyle::Font()).Text(FText::FromString(Choice->Title)); })
   .OnSelectionChanged_Lambda([this](TSharedPtr<FVehicleChoice> Choice,ESelectInfo::Type) { SelectedVehicle=Choice; })
   [SNew(STextBlock).Font(WarehouseUIStyle::Font()).Text_Lambda([this]() { return FText::FromString(SelectedVehicle->Title); })]];
  Text(TEXT("도착 위치"));
  Right->AddSlot().AutoHeight()[SNew(SComboBox<TSharedPtr<FString>>).OptionsSource(&DestChoices).InitiallySelectedItem(SelectedDest)
   .OnGenerateWidget_Lambda([](TSharedPtr<FString> Choice) { return SNew(STextBlock).Font(WarehouseUIStyle::Font()).Text(FText::FromString(*Choice)); })
   .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Choice,ESelectInfo::Type) { SelectedDest=Choice; })
   [SNew(STextBlock).Font(WarehouseUIStyle::Font()).Text_Lambda([this]() { return FText::FromString(SelectedDest ? *SelectedDest : TEXT("출고 위치 없음")); })]];
  Button(TEXT("출고 운반 지시 · 팔레트 전체"),[this]()
  {
   if (auto* C=Computer.Get()) C->IssueOutbound(SelectedCargo.Get(),SelectedVehicle->Vehicle.Get(),DestChoices.IndexOfByKey(SelectedDest));
   RefreshInventory();
  });
  Text(TEXT("단건 지시는 마지막으로 선택한 상품의 팔레트만 운반합니다. 여러 팔레트를 함께 출고하려면 위에서 배송편을 생성하세요."),WarehouseUIStyle::CompactSize);
  Right->AddSlot().AutoHeight().Padding(0,6)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::CompactSize)).ColorAndOpacity(WarehouseUIStyle::Warning)
   .Text_Lambda([this]() { return FText::FromString(Computer.IsValid() ? Computer->LastMessage : TEXT("연결 종료")); }).AutoWrapText(true)];
  Text(TEXT("지게차 운행 관리"));
  Button(TEXT("선택 차량 일시정지"),[this]() { if (Computer.IsValid()) Computer->SetVehicleRunning(SelectedVehicle->Vehicle.Get(),false); });
  Button(TEXT("선택 차량 점검 · 운행 재개"),[this]() { if (Computer.IsValid()) Computer->SetVehicleRunning(SelectedVehicle->Vehicle.Get(),true); });
  Button(TEXT("선택 차량의 마지막 대기 지시 취소"),[this]()
  {
   auto* V=SelectedVehicle->Vehicle.Get();
   if (Computer.IsValid()) Computer->CancelQueuedJob(V && !V->PendingJobs.IsEmpty() ? V->PendingJobs.Last().JobId : FString());
  });
  Text(TEXT("차량 상태 · 실시간"));
  Right->AddSlot().AutoHeight()[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::CompactSize)).ColorAndOpacity(WarehouseUIStyle::Text)
   .Text_Lambda([this]() { return FText::FromString(FleetText()); }).AutoWrapText(true)];
  Text(TEXT("작업 이력 · 소요 시간"));
  Text(TEXT("시뮬레이션 시간 기준 · 지시 접수부터 완료까지 대기·정지 포함\n진행 작업 전체와 최근 완료·취소 200건을 기록합니다."),WarehouseUIStyle::CompactSize);
  Right->AddSlot().AutoHeight()[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::CompactSize)).ColorAndOpacity(WarehouseUIStyle::Text)
   .Text_Lambda([this]() { return FText::FromString(TasksText()); }).AutoWrapText(true)];
  Text(TEXT("보류된 물류 · 데이터 관리"),WarehouseUIStyle::TitleSize);
  Text(TEXT("상자 번호와 품목, 포장 포함 무게·크기를 입력하세요. 생성한 데이터는 배치 모드에서 순서대로 놓습니다."),WarehouseUIStyle::CompactSize);
  Right->AddSlot().AutoHeight().Padding(0,4)[SAssignNew(NewIdInput,SEditableTextBox).Font(WarehouseUIStyle::Font()).HintText(FText::FromString(TEXT("고유 화물 번호")))];
  Right->AddSlot().AutoHeight().Padding(0,4)[SAssignNew(NewProductInput,SEditableTextBox).Font(WarehouseUIStyle::Font()).HintText(FText::FromString(TEXT("물류 종류 / 상품명")))];
  auto Number=[&](const TCHAR* Label,float* Value,float Min,float Max)
  {
   Right->AddSlot().AutoHeight().Padding(0,4)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(WarehouseUIStyle::Font()).Text(FText::FromString(Label))]
    +SHorizontalBox::Slot().FillWidth(1)[SNew(SNumericEntryBox<float>).Font(WarehouseUIStyle::Font()).MinValue(Min).MaxValue(Max)
     .Value_Lambda([Value]() { return TOptional<float>(*Value); }).OnValueChanged_Lambda([Value](float V) { *Value=V; })]];
  };
  Number(TEXT("총 무게 (kg)"),&NewMass,.1f,10000.f);
  Number(TEXT("가로 (cm)"),&NewWidth,5.f,300.f);Number(TEXT("깊이 (cm)"),&NewDepth,5.f,300.f);Number(TEXT("높이 (cm)"),&NewHeight,5.f,300.f);
  Button(TEXT("입력 데이터로 보류 물류 추가"),[this]()
  {
   if (Computer.IsValid()) Computer->CreateHeldCargo(FName(*NewIdInput->GetText().ToString().TrimStartAndEnd()),
    FText::FromString(NewProductInput->GetText().ToString().TrimStartAndEnd()),NewMass,FVector(NewWidth,NewDepth,NewHeight));
   RefreshHeld();
  });
  Button(TEXT("선택한 보류 물류 데이터 삭제"),[this]()
  { if (Computer.IsValid()) Computer->DeleteHeldCargo(SelectedHeldId);RefreshHeld(); });
  ChildSlot[SNew(SBorder).Padding(24).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.012f,.022f,.035f,1.f))
   [SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,16)
     [SNew(SHorizontalBox)
      +SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::TitleSize,true)).ColorAndOpacity(WarehouseUIStyle::Accent).Text(FText::FromString(TEXT("창고 관리 · 관제 컴퓨터")))]
      +SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ContentPadding(FMargin(14,8)).IsFocusable(false)
       [SNew(STextBlock).Font(WarehouseUIStyle::Font()).ColorAndOpacity(WarehouseUIStyle::Text).Text(FText::FromString(TEXT("플레이어로 돌아가기")))]
       .OnClicked_Lambda([this]() { if (Controller.IsValid()) Controller->CloseWarehouseComputer();return FReply::Handled(); })]]
    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,14)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::CompactSize)).ColorAndOpacity(WarehouseUIStyle::Text)
     .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("상자 %d개  ·  표시 %d개  ·  지게차 %d대  |  1초마다 갱신  |  화면 사용 중에도 시뮬레이션 진행"),Computer.IsValid() ? Computer->Inventory.Num() : 0,Items.Num(),Computer.IsValid() ? Computer->Fleet.Num() : 0)); })]
    +SVerticalBox::Slot().FillHeight(1)[SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(.66).Padding(0,0,20,0)[SNew(SVerticalBox)
      +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[SNew(SSearchBox).Style(&SearchStyle).HintText(FText::FromString(TEXT("상품 종류 · 화물 번호 · 위치 검색"))).OnTextChanged_Lambda([this](const FText& Value) { Filter=Value.ToString();RefreshInventory(); })]
      +SVerticalBox::Slot().AutoHeight().Padding(8,6)[SNew(SHorizontalBox)
       +SHorizontalBox::Slot().FillWidth(.54)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::LabelSize,true)).ColorAndOpacity(WarehouseUIStyle::Accent).Text(FText::FromString(TEXT("화물 번호 / 종류")))]
       +SHorizontalBox::Slot().FillWidth(.46)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::LabelSize,true)).ColorAndOpacity(WarehouseUIStyle::Accent).Text(FText::FromString(TEXT("현재 위치 / 무게")))]]
      +SVerticalBox::Slot().FillHeight(1)[SAssignNew(List,SListView<TSharedPtr<FWarehouseInventoryItem>>).ListItemsSource(&Items).SelectionMode(ESelectionMode::Multi)
       .OnSelectionChanged_Lambda([this](TSharedPtr<FWarehouseInventoryItem> Item,ESelectInfo::Type)
       {
        const auto Selected=List->GetSelectedItems();
        SelectedCargo=Selected.IsEmpty() ? nullptr : (Item && Selected.Contains(Item) ? Item->Cargo.Get() : Selected.Last()->Cargo.Get());
       })
       .OnGenerateRow_Lambda([](TSharedPtr<FWarehouseInventoryItem> Item,const TSharedRef<STableViewBase>& Owner)
       {
        return SNew(STableRow<TSharedPtr<FWarehouseInventoryItem>>,Owner).Padding(FMargin(8,8))
         [SNew(SHorizontalBox)
          +SHorizontalBox::Slot().FillWidth(.54)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::LabelSize)).ColorAndOpacity(WarehouseUIStyle::Text)
           .Text_Lambda([Item]() { return FText::FromString(Item->CargoId+TEXT("\n")+Item->Product); }).AutoWrapText(true)]
          +SHorizontalBox::Slot().FillWidth(.46)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::CompactSize)).ColorAndOpacity(WarehouseUIStyle::Text)
           .Text_Lambda([Item]() { return FText::FromString(FString::Printf(TEXT("%s\n%.1f kg · %s\n%s"),*Item->Location,Item->MassKg,Item->bReserved ? TEXT("예약 / 작업 중") : (Item->Pallet ? TEXT("팔레트 적재") : TEXT("개별 상자")),*Item->ReceiptStatus)); }).AutoWrapText(true)]];
       })]
      +SVerticalBox::Slot().AutoHeight().Padding(8,12)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::LabelSize,true)).ColorAndOpacity(WarehouseUIStyle::Accent)
       .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("보류된 물류 %d개 · 맨 위 항목부터 배치"),HeldItems.Num())); })]
      +SVerticalBox::Slot().FillHeight(.6)[SAssignNew(HeldList,SListView<TSharedPtr<FWarehouseHeldCargo>>).ListItemsSource(&HeldItems).SelectionMode(ESelectionMode::Single)
       .OnSelectionChanged_Lambda([this](TSharedPtr<FWarehouseHeldCargo> Item,ESelectInfo::Type) { SelectedHeldId=Item ? Item->CargoId : NAME_None; })
       .OnGenerateRow_Lambda([](TSharedPtr<FWarehouseHeldCargo> Item,const TSharedRef<STableViewBase>& Owner)
       {
        return SNew(STableRow<TSharedPtr<FWarehouseHeldCargo>>,Owner).Padding(FMargin(8,8))
         [SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::LabelSize)).ColorAndOpacity(WarehouseUIStyle::Text).AutoWrapText(true)
          .Text(FText::FromString(FString::Printf(TEXT("%s · %s\n%.1f kg · %.0f × %.0f × %.0f cm"),*Item->CargoId.ToString(),*Item->Product.ToString(),Item->MassKg,Item->SizeCm.X,Item->SizeCm.Y,Item->SizeCm.Z)))];
       })]]
     +SHorizontalBox::Slot().FillWidth(.34)[SNew(SScrollBox)+SScrollBox::Slot()[Right]]]
    +SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::CompactSize)).ColorAndOpacity(WarehouseUIStyle::Warning)
     .Text_Lambda([this]() { return Controller.IsValid() ? FText::FromString(Controller->EmergencyAlertText) : FText::GetEmpty(); }).AutoWrapText(true)]]];
 }
 virtual bool SupportsKeyboardFocus() const override { return true; }
 virtual FReply OnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override
 {
  if ((Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::F1) && Controller.IsValid())
  { Controller->CloseWarehouseComputer();return FReply::Handled(); }
  return SCompoundWidget::OnPreviewKeyDown(Geometry,Event);
 }
 virtual void Tick(const FGeometry& Geometry,double Now,float Dt) override
 {
  SCompoundWidget::Tick(Geometry,Now,Dt);
  if (Computer.IsValid() && LastRefresh!=Computer->RefreshCount) RefreshInventory();
  if (Computer.IsValid()) if (auto* Store=AWarehouseLayoutInventory::Find(Computer.Get());Store && LastHeldRevision!=Store->Revision) RefreshHeld();
 }
private:
 TWeakObjectPtr<AWarehouseComputer> Computer;
 TWeakObjectPtr<AWarehouseShippingSystem> Shipping;
 TWeakObjectPtr<Amsc_vrPlayerController> Controller;
 TWeakObjectPtr<AWarehouseCargo> SelectedCargo;
 TArray<TSharedPtr<FWarehouseInventoryItem>> Items;
 TMap<TWeakObjectPtr<AWarehouseCargo>,TSharedPtr<FWarehouseInventoryItem>> Cache;
 TSharedPtr<SListView<TSharedPtr<FWarehouseInventoryItem>>> List;
 TArray<TSharedPtr<FVehicleChoice>> VehicleChoices;
 TSharedPtr<FVehicleChoice> SelectedVehicle;
 TArray<TSharedPtr<FString>> DestChoices;
 TSharedPtr<FString> SelectedDest;
 FString Filter;int32 LastRefresh=-1;
 TArray<TSharedPtr<FWarehouseHeldCargo>> HeldItems;
 TSharedPtr<SListView<TSharedPtr<FWarehouseHeldCargo>>> HeldList;
 FName SelectedHeldId;
 int32 LastHeldRevision=-1;
 TSharedPtr<SEditableTextBox> NewIdInput,NewProductInput;
 float NewMass=12,NewWidth=40,NewDepth=30,NewHeight=25;
 TSharedPtr<SEditableTextBox> ShipmentNameInput;
 TArray<TSharedPtr<FString>> OutboundZoneChoices,InboundZoneChoices,ShipmentChoices;
 TSharedPtr<FString> SelectedOutboundZone,SelectedInboundZone,SelectedShipment;
 TSharedPtr<SComboBox<TSharedPtr<FString>>> OutboundZoneCombo,InboundZoneCombo,ShipmentCombo;
 TArray<TSharedPtr<FTruckChoice>> TruckChoices;
 TSharedPtr<FTruckChoice> SelectedOutboundTruck,SelectedInboundTruck;
 TSharedPtr<SComboBox<TSharedPtr<FTruckChoice>>> OutboundTruckCombo,InboundTruckCombo;
 TArray<AWarehousePallet*> SelectedPallets() const
 {
  TArray<AWarehousePallet*> Pallets;
  if (!Computer.IsValid() || !List.IsValid()) return Pallets;
  // Resolve against the current inventory so a stale selection cannot ship an old support.
  for (const auto& Selected:List->GetSelectedItems())
   if (Selected) if (const auto* Current=Computer->Inventory.FindByPredicate([&](const auto& Row) { return Row.Cargo==Selected->Cargo; });Current && IsValid(Current->Pallet))
    Pallets.AddUnique(Current->Pallet);
  return Pallets;
 }
 static FString TruckText(const TSharedPtr<FTruckChoice>& Choice)
 {
  auto* Truck=Choice ? Choice->Truck.Get() : nullptr;
  if (!Truck) return TEXT("사용 가능한 트럭 없음");
  return FString::Printf(TEXT("%s · 도크 %s · 명세 %d개\n%s"),*Truck->TruckId,*Truck->DockId,Truck->ManifestPallets.Num(),*Truck->Status);
 }
 void RefreshShippingChoices()
 {
  if (!Shipping.IsValid()) return;
  auto Strings=[](TArray<TSharedPtr<FString>>& Choices,TSharedPtr<FString>& Selected,const TArray<FString>& Values,const TSharedPtr<SComboBox<TSharedPtr<FString>>>& Combo)
  {
   bool Changed=Choices.Num()!=Values.Num();
   for (int32 I=0;!Changed && I<Values.Num();++I) Changed=*Choices[I]!=Values[I];
   if (!Changed) return;
   const FString Old=Selected ? *Selected : FString();Choices.Reset();Selected.Reset();
   for (const FString& Value:Values) { auto Item=MakeShared<FString>(Value);Choices.Add(Item);if (Value==Old) Selected=Item; }
   if (!Selected && !Choices.IsEmpty()) Selected=Choices[0];
   if (Combo.IsValid()) { Combo->RefreshOptions();Combo->SetSelectedItem(Selected); }
  };
  TArray<FString> Inbound,Outbound,Active;
  for (const auto& Zone:Shipping->Zones) if (IsValid(Zone.Anchor)) (Zone.bInbound ? Inbound : Outbound).AddUnique(Zone.ZoneId);
  for (const auto& Shipment:Shipping->Shipments)
   if (Shipment.State!=EWarehouseShipmentState::Completed && Shipment.State!=EWarehouseShipmentState::Cancelled) Active.Add(Shipment.ShipmentId);
  Strings(InboundZoneChoices,SelectedInboundZone,Inbound,InboundZoneCombo);
  Strings(OutboundZoneChoices,SelectedOutboundZone,Outbound,OutboundZoneCombo);
  Strings(ShipmentChoices,SelectedShipment,Active,ShipmentCombo);
  TArray<TSharedPtr<FTruckChoice>> Updated;
  for (AWarehouseLogisticsTruck* Truck:Shipping->Trucks) if (IsValid(Truck))
  {
   const auto* Found=TruckChoices.FindByPredicate([Truck](const auto& Choice) { return Choice->Truck==Truck; });
   auto Choice=Found ? *Found : MakeShared<FTruckChoice>();Choice->Truck=Truck;Updated.Add(Choice);
  }
  const bool Changed=Updated!=TruckChoices;TruckChoices=MoveTemp(Updated);
  auto Retain=[this](TSharedPtr<FTruckChoice>& Selected,bool Inbound)
  {
   if (Selected && TruckChoices.Contains(Selected)) return;
   Selected.Reset();
   for (const auto& Choice:TruckChoices) if (auto* Truck=Choice->Truck.Get();Truck && (Inbound ? !Truck->ManifestPallets.IsEmpty() : Truck->ManifestPallets.IsEmpty())) { Selected=Choice;break; }
   if (!Selected && !TruckChoices.IsEmpty()) Selected=TruckChoices[0];
  };
  Retain(SelectedOutboundTruck,false);Retain(SelectedInboundTruck,true);
  if (Changed)
  {
   if (OutboundTruckCombo.IsValid()) { OutboundTruckCombo->RefreshOptions();OutboundTruckCombo->SetSelectedItem(SelectedOutboundTruck); }
   if (InboundTruckCombo.IsValid()) { InboundTruckCombo->RefreshOptions();InboundTruckCombo->SetSelectedItem(SelectedInboundTruck); }
  }
 }
 FString ShipmentsText() const
 {
  if (!Shipping.IsValid() || Shipping->Shipments.IsEmpty()) return TEXT("등록된 배송편이 없습니다.");
  FString Value;
  for (int32 I=Shipping->Shipments.Num()-1;I>=0;--I)
  {
   const auto& Shipment=Shipping->Shipments[I];int32 Arrived=0,Loaded=0;
   for (const auto& Line:Shipment.Lines) { Arrived+=Line.bArrived ? 1 : 0;Loaded+=Line.bLoaded ? 1 : 0; }
   const bool Inbound=Shipment.Direction==EWarehouseShipmentDirection::Inbound;
   const FString TruckName=IsValid(Shipment.Truck) ? Shipment.Truck->TruckId : TEXT("트럭 없음");
   Value+=FString::Printf(TEXT("%s · %s · %s\n%s → %s · %.1f초\n%s\n%s %d/%d · %s %d/%d · 트럭 %s\n"),
    *Shipment.ShipmentId,*Shipment.Name,Inbound ? TEXT("입고") : TEXT("출고"),
    *(Inbound ? TruckName : Shipment.ZoneId),*(Inbound ? Shipment.ZoneId : TruckName),Shipment.ElapsedSeconds,*Shipment.Status,
    Inbound ? TEXT("인계") : TEXT("집결"),Arrived,Shipment.Lines.Num(),Inbound ? TEXT("트럭 잔량") : TEXT("상차"),Loaded,Shipment.Lines.Num(),
    Shipment.bTruckReady ? TEXT("준비 완료") : TEXT("대기"));
   if (!Inbound) Value+=(Shipment.bCargoReady ? TEXT("화물 준비 완료\n") : TEXT("화물 준비 대기\n"));
   if (!Shipment.WaitReason.IsEmpty()) Value+=TEXT("대기 / 오류: ")+Shipment.WaitReason+TEXT("\n");
   for (const auto& Line:Shipment.Lines) Value+=FString::Printf(TEXT("  %s · %s\n"),*Line.PalletLabel,*Line.Status);
   Value+=TEXT("\n");
  }
  return Value;
 }
 void RefreshHeld()
 {
  if (!Computer.IsValid()) return;auto* Store=AWarehouseLayoutInventory::Find(Computer.Get());if (!Store) return;
  LastHeldRevision=Store->Revision;HeldItems.Reset();
  for (const auto& Row:Store->HeldCargo) HeldItems.Add(MakeShared<FWarehouseHeldCargo>(Row));
  if (HeldList.IsValid()) HeldList->RequestListRefresh();
 }
 void RefreshInventory()
 {
  if (!Computer.IsValid()) return;
  LastRefresh=Computer->RefreshCount;Items.Reset();TSet<TWeakObjectPtr<AWarehouseCargo>> Present;
  for (const auto& Row : Computer->Inventory)
  {
   Present.Add(Row.Cargo.Get());auto& Item=Cache.FindOrAdd(Row.Cargo.Get());if (!Item) Item=MakeShared<FWarehouseInventoryItem>();*Item=Row;
   if (Filter.IsEmpty() || (Row.CargoId+Row.Product+Row.Location).Contains(Filter)) Items.Add(Item);
  }
  for (auto It=Cache.CreateIterator();It;++It) if (!Present.Contains(It.Key())) It.RemoveCurrent();
  if (List.IsValid()) List->RequestListRefresh();
  RefreshShippingChoices();
 }
 FString SelectionText() const
 {
  auto* C=Computer.Get();if (!C || !SelectedCargo.IsValid()) return TEXT("왼쪽에서 운반할 상품을 선택하세요.");
  const auto* Row=C->Inventory.FindByPredicate([this](const auto& Item) { return Item.Cargo==SelectedCargo.Get(); });
  if (!Row) return TEXT("선택한 상품이 사라졌습니다.");
  if (!Row->Pallet) return Row->Product+TEXT("\n팔레트에 적재한 뒤 운반 지시가 가능합니다.");
  float Total=Row->Pallet->PalletMassKg;int32 Count=0;
  for (const auto& Item : C->Inventory) if (Item.Pallet==Row->Pallet) { Total+=Item.MassKg;++Count; }
  return FString::Printf(TEXT("%s\n%s\n팔레트 전체 %d상자 · %.1f kg%s"),*Row->Product,*Row->Location,Count,Total,Row->bReserved ? TEXT(" · 예약됨") : TEXT(""));
 }
 FString FleetText() const
 {
  FString Value;if (!Computer.IsValid()) return Value;
  for (AWarehouseForklift* V : Computer->Fleet)
   Value+=FString::Printf(TEXT("%s · 배터리 %.0f%% · 대기 %d건\n%s\n\n"),*V->VehicleName,V->BatteryPercent,V->PendingJobs.Num(),*V->Status);
  return Value;
 }
 FString TasksText() const
 {
  if (!Computer.IsValid() || Computer->Tasks.IsEmpty()) return TEXT("아직 등록된 작업이 없습니다.");
  FString Value;const auto& Tasks=Computer->Tasks;
  for (int32 I=Tasks.Num()-1;I>=0;--I)
  {
   const auto& Task=Tasks[I];Value+=FString::Printf(TEXT("%s → %s\n%s · %s\n"),*Task.Source,*Task.Destination,
    IsValid(Task.Vehicle) ? *Task.Vehicle->VehicleName : TEXT("차량 없음"),*Task.State);
   if (Task.bSubmissionTimeKnown)
    Value+=FString::Printf(TEXT("접수 %.1f초 · %s %.1f초\n\n"),Task.SubmittedSimulationSeconds,
     Task.bCompleted ? TEXT("완료 소요") : Task.bCancelled ? TEXT("취소까지") : TEXT("현재 경과"),Computer->GetTaskElapsedSeconds(Task.JobId));
   else Value+=TEXT("접수 시각 미기록 · 소요 시간 계산 제외\n\n");
  }
  return Value;
 }
};
}

bool Amsc_vrPlayerController::OpenWarehouseComputer(AWarehouseComputer* Computer)
{
 auto* OperatorCharacter=Cast<Amsc_vrCharacter>(GetPawn());
 if (!IsLocalController() || !GetWorld()->GetGameViewport() || !OperatorCharacter || !IsValid(Computer) || !Computer->CanUse() ||
     IsWarehouseMenuOpen() || bObserverView || IsMoveInputIgnored() || OperatorCharacter->GetHeldCargo() || OperatorCharacter->GetRemoteForklift() ||
     OperatorCharacter->GetInteractionTarget()!=Computer) return false;
 Computer->RefreshState();ActiveComputer=Computer;
 ComputerWidget=SNew(SWarehouseComputerScreen).Computer(Computer).Controller(this);
 GetWorld()->GetGameViewport()->AddViewportWidgetContent(ComputerWidget.ToSharedRef(),250);
 UpdateInputMode();FSlateApplication::Get().SetKeyboardFocus(ComputerWidget);return true;
}
void Amsc_vrPlayerController::CloseWarehouseComputer()
{
 if (!ComputerWidget.IsValid()) return;
 if (GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(ComputerWidget.ToSharedRef());
 ComputerWidget.Reset();ActiveComputer=nullptr;UpdateInputMode();
}
