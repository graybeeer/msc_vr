#include "WarehouseExperimentUI.h"
#include "WarehouseExperimentSubsystem.h"
#include "WarehouseExperimentMetrics.h"
#include "WarehouseExperimentStorage.h"
#include "WarehouseUIStyle.h"
#include "WarehouseComputer.h"
#include "WarehousePallet.h"
#include "msc_vrPlayerController.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#if WITH_EDITOR && !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#endif

namespace WarehouseExperimentUI
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
 return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Surface).Padding(14)[Content];
}

FString Duration(double Seconds)
{
 return Seconds>=60 ? FString::Printf(TEXT("%.1f분"),Seconds/60.) : FString::Printf(TEXT("%.1f초"),Seconds);
}
FString StateLabel(const FString& State)
{
 if (State==TEXT("completed")) return TEXT("완료");
 if (State==TEXT("stopped") || State==TEXT("cancelled")) return TEXT("사용자 중지");
 if (State==TEXT("timeout") || State==TEXT("time_limit")) return TEXT("시간 한도");
 if (State==TEXT("failed") || State==TEXT("error")) return TEXT("실행 실패");
 if (State==TEXT("running")) return TEXT("진행 중");
 if (State==TEXT("invalid_conditions")) return TEXT("조건 불일치 · 비교 제외");
 if (State==TEXT("interrupted")) return TEXT("실행 끊김 · 비교 제외");
 return State;
}
TSharedRef<SWidget> SummaryCards(const FWarehouseExperimentSummary& Summary)
{
 auto Row=SNew(SWrapBox).UseAllottedSize(true);
 auto Card=[&](const FString& Name,const FString& Value)
 {
  Row->AddSlot().Padding(0,0,14,8)[SNew(SBox).MinDesiredWidth(125)
   [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Label(Name,13,Muted)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Label(Value,20,WarehouseUIStyle::Accent,true)]]];
 };
 Card(TEXT("완료 팔레트 / 시간"),FString::Printf(TEXT("%.1f"),Summary.ThroughputPerHour));
 Card(TEXT("평균 운반 시간"),Summary.Completed>0 ? Duration(Summary.MeanTransportSeconds) : TEXT("완료 후 계산"));
 Card(TEXT("95% 운반 시간"),Summary.Completed>0 ? Duration(Summary.P95TransportSeconds) : TEXT("완료 후 계산"));
 Card(TEXT("완료 / 전체"),FString::Printf(TEXT("%d / %d"),Summary.Completed,Summary.Planned));
 Card(TEXT("실패 / 취소 / 미완료"),FString::Printf(TEXT("%d / %d / %d"),Summary.Failed,Summary.Cancelled,Summary.Unfinished));
 Card(TEXT("실험 경과"),Duration(Summary.ElapsedSeconds));
 return Row;
}
TSharedRef<SWidget> WaitCauses(const FWarehouseExperimentSummary& Summary,bool VehicleCharging=false)
{
 auto Rows=SNew(SVerticalBox);
 const TArray<FString> Keys=VehicleCharging ? TArray<FString>{TEXT("charger_wait"),TEXT("charging")} : TArray<FString>{TEXT("queue"),TEXT("traffic_wait"),TEXT("obstacle_wait"),TEXT("sensor_wait"),TEXT("elevator_wait"),TEXT("paused"),TEXT("fault"),TEXT("recovery")};
 for (const FString& Key:Keys)
 {
  const double Seconds=VehicleCharging ? Summary.VehiclePhaseSeconds.FindRef(Key) : Summary.JobPhaseSeconds.FindRef(Key);
  if (Seconds<=.01) continue;
  Rows->AddSlot().AutoHeight().Padding(0,3)[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().FillWidth(1)[Label(FWarehouseExperimentMetrics::PhaseLabel(Key).ToString(),14)]
   +SHorizontalBox::Slot().AutoWidth()[Label(Duration(Seconds),14,WarehouseUIStyle::Warning)]];
 }
 if (Rows->NumSlots()==0) Rows->AddSlot().AutoHeight()[Label(TEXT("기록된 대기가 없습니다."),14,Muted)];
 return Rows;
}

using FChoicePtr=TSharedPtr<FWarehouseExperimentChoice>;
class SWarehouseExperiments : public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SWarehouseExperiments) {}
 SLATE_ARGUMENT(TWeakObjectPtr<Amsc_vrPlayerController>,Controller)
 SLATE_ARGUMENT(bool,Modal)
 SLATE_END_ARGS()

 void Construct(const FArguments& Args)
 {
  Controller=Args._Controller;bModal=Args._Modal;
  if (Controller.IsValid() && Controller->GetGameInstance()) Store=Controller->GetGameInstance()->GetSubsystem<UWarehouseExperimentSubsystem>();
  Refresh(true);
  if (Store.IsValid() && (Store->bBatchRunning || !Store->GetResults().IsEmpty())) Page=1;
  auto Tabs=SNew(SHorizontalBox);
  for (int32 I=0;I<2;++I)
   Tabs->AddSlot().FillWidth(1).Padding(0,0,I==0 ? 6 : 0,0)
    [Action(I==0 ? TEXT("configuration") : TEXT("results"),I==0 ? TEXT("배치 · 공통 조건") : TEXT("운영 성과 · 비교"),
     [this,I]() { Page=I; },[]() { return true; })];

  ChildSlot[SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Background).Padding(bModal ? 24 : 4)
   [SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1)[SNew(SVerticalBox)
      +SVerticalBox::Slot().AutoHeight()[Label(TEXT("배치 비교 실험"),22,WarehouseUIStyle::Text,true)]
      +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Live([this]() { return StatusLine(); },14,WarehouseUIStyle::Accent)]]
     +SHorizontalBox::Slot().AutoWidth()[SNew(SBox).Visibility(bModal ? EVisibility::Visible : EVisibility::Collapsed)
      [Action(TEXT("back"),TEXT("화면 닫기 · Esc"),[this]() { Close(); })]]]
    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[Tabs]
    +SVerticalBox::Slot().FillHeight(1)[SNew(SWidgetSwitcher).WidgetIndex_Lambda([this]() { return Page; })
     +SWidgetSwitcher::Slot()[BuildConfiguration()]
     +SWidgetSwitcher::Slot()[BuildResults()]]
    +SVerticalBox::Slot().AutoHeight().Padding(0,10,0,0)[Live([this]() { return Notice.IsEmpty() && Store.IsValid() ? Store->LastMessage : Notice; },14,WarehouseUIStyle::Warning)]]];
  RefreshViews();
 }
 virtual bool SupportsKeyboardFocus() const override { return true; }
 virtual FReply OnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override
 {
  if (bModal && (Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::F1)) { Close();return FReply::Handled(); }
  return SCompoundWidget::OnPreviewKeyDown(Geometry,Event);
 }
 virtual void Tick(const FGeometry& Geometry,double Now,float DeltaSeconds) override
 {
  SCompoundWidget::Tick(Geometry,Now,DeltaSeconds);
  if (Now>=NextRefresh) { NextRefresh=Now+1.;Refresh();RefreshViews(); }
 }

private:
 TWeakObjectPtr<Amsc_vrPlayerController> Controller;
 TWeakObjectPtr<UWarehouseExperimentSubsystem> Store;
 bool bModal=false,bConfirmLoad=false,bConfirmReplace=false,bConfirmRemove=false;
 int32 Page=0;
 double NextRefresh=0,ReleaseSeconds=0;
 FString Notice,LoadLayoutId,LayoutRowsKey,OrderRowsKey;
 int32 DrawnResultCount=-1;
 TArray<WarehouseExperimentStorage::FLayoutInfo> Layouts;
 TArray<FChoicePtr> Sources,Destinations,Workloads;
 FChoicePtr Source,Destination,Workload;
 TSharedPtr<SComboBox<FChoicePtr>> SourceCombo,DestinationCombo,WorkloadCombo;
 TSharedPtr<SEditableTextBox> LayoutName,WorkloadName;
 TSharedPtr<SVerticalBox> LayoutRows,OrderRows,LiveRows,ResultRows;
 TMap<FString,TSharedPtr<SButton>> Buttons;
 TMap<FString,TSharedPtr<SCheckBox>> LayoutChecks;

 bool Busy() const { return !Store.IsValid() || Store->bLoading || Store->bBatchRunning; }
 const WarehouseExperimentStorage::FLayoutInfo* SelectedLayout() const
 { return Layouts.FindByPredicate([this](const auto& Layout) { return Layout.Id==LoadLayoutId; }); }
 void ResetLayoutConfirmation() { bConfirmLoad=false;bConfirmReplace=false;bConfirmRemove=false; }
 void SelectLayout(const FString& Id)
 {
  LoadLayoutId=Id;ResetLayoutConfirmation();Notice.Reset();
  if (const auto* Selected=SelectedLayout();Selected && LayoutName) LayoutName->SetText(FText::FromString(Selected->Name));
 }
 void Close()
 {
  if (Store.IsValid() && (Store->bBatchRunning || Store->bLoading))
  { Notice=TEXT("실험 중에는 '실험 중지' 버튼을 먼저 눌러야 일반 조작으로 돌아갈 수 있습니다.");Page=1;return; }
  if (Controller.IsValid()) Controller->CloseWarehouseExperiments();
 }
 void Report(bool Success)
 {
  Notice=(Success ? TEXT("완료 · ") : TEXT("확인 필요 · "))+(Store.IsValid() ? Store->LastMessage : TEXT("실험 관리 연결이 없습니다."));
  Refresh(true);RefreshViews();
 }
 FString StatusLine() const
 {
  if (!Store.IsValid()) return TEXT("실험 관리 연결을 기다립니다.");
  if (Store->bLoading) return TEXT("저장한 초기 조건으로 창고를 다시 여는 중입니다.");
  if (Store->bBatchRunning) return FString::Printf(TEXT("반복 실험 %d / %d · %s · 물리·센서·주행 방식 유지"),Store->CurrentRun,Store->TotalRuns,*Duration(Store->GetLiveSummary().ElapsedSeconds));
  return TEXT("같은 물류와 주문 · 저장한 배치만 바꾸어 비교합니다.");
 }
 TSharedRef<SWidget> Action(const FString& Key,const FString& Title,TFunction<void()> Callback,TFunction<bool()> Enabled=[]() { return true; },bool PrimaryAction=false)
 {
  TSharedPtr<SButton> Button;
  auto Widget=SAssignNew(Button,SButton).ContentPadding(FMargin(12,8)).IsFocusable(false)
   .ButtonColorAndOpacity(PrimaryAction ? Primary : FLinearColor(.09f,.13f,.17f))
   .IsEnabled_Lambda([Enabled]() { return Enabled(); })
   .OnClicked_Lambda([Callback]() { Callback();return FReply::Handled(); })[Label(Title,15,WarehouseUIStyle::Text,true)];
  Buttons.Add(Key,Button);return Widget;
 }
 TSharedRef<SWidget> Combo(TArray<FChoicePtr>& Values,FChoicePtr& Selected,TSharedPtr<SComboBox<FChoicePtr>>& Widget,const FString& Empty)
 {
  auto* Selection=&Selected;
  return SAssignNew(Widget,SComboBox<FChoicePtr>).OptionsSource(&Values).InitiallySelectedItem(Selected)
   .IsEnabled_Lambda([this]() { return !Busy(); })
   .OnGenerateWidget_Lambda([](FChoicePtr Item) { return Label(Item ? Item->Label : FString(),14); })
   .OnSelectionChanged_Lambda([Selection](FChoicePtr Item,ESelectInfo::Type) { *Selection=Item; })
   [Live([Selection,Empty]() { return *Selection ? (*Selection)->Label : Empty; },14)];
 }
 TSharedRef<SWidget> Number(const FString& Title,TFunction<double()> Get,TFunction<void(double)> Set,double Min,double Max)
 {
  return SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)[Label(Title,14,Muted)]
   +SVerticalBox::Slot().AutoHeight()[SNew(SNumericEntryBox<double>).MinDesiredValueWidth(95).AllowSpin(false)
    .Font(WarehouseUIStyle::Font(16)).MinValue(Min).MaxValue(Max).IsEnabled_Lambda([this]() { return !Busy(); })
    .Value_Lambda([Get]() -> TOptional<double> { return Get(); })
    .OnValueCommitted_Lambda([Set,Min,Max](double Value,ETextCommit::Type) { if (FMath::IsFinite(Value)) Set(FMath::Clamp(Value,Min,Max)); })];
 }
 TSharedRef<SWidget> BuildConfiguration()
 {
  auto LayoutPanel=SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[Label(TEXT("1. 비교할 배치 저장 · 선택"),18,WarehouseUIStyle::Text,true)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,5)[Live([this]() { return FString::Printf(TEXT("저장된 배치 %d / %d"),Layouts.Num(),UWarehouseExperimentSubsystem::MaxSavedLayouts); },16,WarehouseUIStyle::Accent)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,10)[Label(TEXT("작업 전 초기 상태를 저장합니다. 불러오기와 반복 실행은 현재 실행을 초기화합니다. 원본 맵 파일은 바꾸지 않습니다."),14,Muted)]
   +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[SAssignNew(LayoutName,SEditableTextBox).Font(WarehouseUIStyle::Font(16)).HintText(FText::FromString(TEXT("배치 이름 · 예: 넓은 중앙 통로"))).IsEnabled_Lambda([this]() { return !Busy(); })
     .OnTextChanged_Lambda([this](const FText&) { ResetLayoutConfirmation(); })]
    +SHorizontalBox::Slot().AutoWidth()[Action(TEXT("saveLayout"),TEXT("새 배치로 저장"),[this]() { ResetLayoutConfirmation();const bool Success=Store->SaveLayout(LayoutName->GetText().ToString());if (Success) LoadLayoutId=Store->LastSavedLayoutId;Report(Success); },[this]() { return !Busy() && Layouts.Num()<UWarehouseExperimentSubsystem::MaxSavedLayouts && LayoutName.IsValid() && !LayoutName->GetText().IsEmpty(); },true)]]
   +SVerticalBox::Slot().AutoHeight().Padding(0,6)[Live([this]() { return Layouts.Num()>=UWarehouseExperimentSubsystem::MaxSavedLayouts ? TEXT("저장 공간이 찼습니다. 선택한 배치를 덮어쓰거나 삭제하면 계속 저장할 수 있습니다.") : TEXT("현재 배치를 새 저장으로 남기거나, 아래에서 기존 배치를 선택해 관리하세요."); },13,Muted)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,10)[SAssignNew(LayoutRows,SVerticalBox)]
   +SVerticalBox::Slot().AutoHeight()[Live([this]()
    {
     const auto* Selected=SelectedLayout();
     const FString Name=Selected ? Selected->Name : TEXT("선택 없음");
     return bConfirmLoad ? FString::Printf(TEXT("'%s' 배치를 불러옵니다. 현재 진행은 없어집니다. 아래 버튼을 다시 눌러 확인하세요."),*Name) :
      FString::Printf(TEXT("관리 대상: %s\n체크박스는 반복 실험 대상입니다. 불러오기·덮어쓰기·삭제 대상은 오른쪽 '대상 선택'으로 고릅니다."),*Name);
    },13,WarehouseUIStyle::Warning)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[Action(TEXT("loadLayout"),TEXT("선택 배치 불러오기 · 두 번 눌러 확인"),[this]()
    { if (!bConfirmLoad) { ResetLayoutConfirmation();bConfirmLoad=true;Notice=TEXT("현재 진행을 초기화해도 되면 불러오기 버튼을 다시 누르세요."); } else { ResetLayoutConfirmation();Report(Store->LoadLayout(LoadLayoutId)); } },[this]() { return !Busy() && SelectedLayout(); })]
   +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)[Action(TEXT("replaceLayout"),TEXT("선택 배치 덮어쓰기"),[this]()
    {
     const auto* Selected=SelectedLayout();if (!Selected) return;
     if (!bConfirmReplace) { ResetLayoutConfirmation();bConfirmReplace=true;Notice=TEXT("선택한 저장 배치를 현재 배치로 교체합니다. 덮어쓰기 버튼을 다시 눌러 확인하세요.");return; }
     const FString Name=LayoutName->GetText().ToString().TrimStartAndEnd();
     const FString ReplacementName=Name.IsEmpty() ? Selected->Name : Name;
     ResetLayoutConfirmation();const bool Success=Store->ReplaceLayout(LoadLayoutId,ReplacementName);
     if (Success) LoadLayoutId=Store->LastSavedLayoutId;Report(Success);
    },[this]() { return !Busy() && SelectedLayout(); })]
   +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)[Action(TEXT("removeLayout"),TEXT("선택 저장 배치 삭제"),[this]()
    {
     if (!bConfirmRemove) { ResetLayoutConfirmation();bConfirmRemove=true;Notice=TEXT("선택한 저장 배치만 삭제합니다. 현재 창고와 기존 실험 결과는 유지됩니다. 삭제 버튼을 다시 눌러 확인하세요.");return; }
     ResetLayoutConfirmation();Report(Store->RemoveLayout(LoadLayoutId));
    },[this]() { return !Busy() && SelectedLayout(); })];
  Buttons[TEXT("loadLayout")]->SetContent(Live([this]()
  {
   const auto* Selected=SelectedLayout();
   if (!Selected) return FString(TEXT("불러올 배치를 선택하세요"));
   return FString(TEXT("'"))+Selected->Name+(bConfirmLoad ? TEXT("' 불러오기 확인 · 현재 실행 초기화") : TEXT("' 배치 불러오기"));
  },15));
  Buttons[TEXT("replaceLayout")]->SetContent(Live([this]()
  {
   const auto* Selected=SelectedLayout();if (!Selected) return FString(TEXT("덮어쓸 배치를 선택하세요"));
   return FString(TEXT("'"))+Selected->Name+(bConfirmReplace ? TEXT("' 덮어쓰기 확인 · 현재 배치로 교체") : TEXT("' 배치 덮어쓰기"));
  },15));
  Buttons[TEXT("removeLayout")]->SetContent(Live([this]()
  {
   const auto* Selected=SelectedLayout();if (!Selected) return FString(TEXT("삭제할 배치를 선택하세요"));
   return FString(TEXT("'"))+Selected->Name+(bConfirmRemove ? TEXT("' 저장 배치 삭제 확인") : TEXT("' 저장 배치 삭제"));
  },15,Danger));

  auto OrderPanel=SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight()[Label(TEXT("2. 모든 배치에 사용할 공통 주문"),18,WarehouseUIStyle::Text,true)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,6,0,10)[Label(TEXT("팔레트 전체를 운반합니다. 배치를 바꾸어도 같은 팔레트·목적지·주문 시각을 사용합니다. 차량은 기존 관제 규칙으로 자동 배정합니다."),14,Muted)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Combo(Sources,Source,SourceCombo,TEXT("운반할 팔레트가 없습니다"))]
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Combo(Destinations,Destination,DestinationCombo,TEXT("목적지가 없습니다"))]
   +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,8,0)[Number(TEXT("주문 시각 · 실험 시작 후 초"),[this]() { return ReleaseSeconds; },[this](double Value) { ReleaseSeconds=Value; },0,86400)]
    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)[Action(TEXT("addOrder"),TEXT("주문 추가"),[this]() { Report(Store->AddOrder(Source->Id,Destination->Id,ReleaseSeconds)); },[this]() { return !Busy() && Source.IsValid() && Destination.IsValid(); },true)]]
   +SVerticalBox::Slot().AutoHeight().Padding(0,10)[SAssignNew(OrderRows,SVerticalBox)]
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Action(TEXT("clearOrders"),TEXT("주문 목록 비우기"),[this]() { Store->ClearOrders();Report(true); },[this]() { return !Busy() && !Store->Orders.IsEmpty(); })]
   +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[SAssignNew(WorkloadName,SEditableTextBox).Font(WarehouseUIStyle::Font(15)).HintText(FText::FromString(TEXT("공통 주문 묶음 이름"))).IsEnabled_Lambda([this]() { return !Busy(); })]
    +SHorizontalBox::Slot().AutoWidth()[Action(TEXT("saveWorkload"),TEXT("주문 저장"),[this]() { Report(Store->SaveWorkload(WorkloadName->GetText().ToString())); },[this]() { return !Busy() && !Store->Orders.IsEmpty() && WorkloadName.IsValid() && !WorkloadName->GetText().IsEmpty(); })]]
   +SVerticalBox::Slot().AutoHeight().Padding(0,6)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,6,0)[Combo(Workloads,Workload,WorkloadCombo,TEXT("저장한 주문 묶음이 없습니다"))]
    +SHorizontalBox::Slot().AutoWidth()[Action(TEXT("loadWorkload"),TEXT("주문 불러오기"),[this]() { Report(Store->LoadWorkload(Workload->Id)); },[this]() { return !Busy() && Workload.IsValid(); })]];

  auto Settings=SNew(SWrapBox).UseAllottedSize(true);
  Settings->AddSlot().Padding(0,0,18,8)[Number(TEXT("배치마다 반복 횟수"),[this]() { return Store.IsValid() ? Store->DraftRepeats : 3.; },[this](double V) { Store->DraftRepeats=FMath::RoundToInt(V); },1,20)];
  Settings->AddSlot().Padding(0,0,18,8)[Number(TEXT("한 회 시간 한도 · 초"),[this]() { return Store.IsValid() ? Store->DraftDurationSeconds : 300.; },[this](double V) { Store->DraftDurationSeconds=V; },10,86400)];
  Settings->AddSlot().Padding(0,0,18,8)[Number(TEXT("반복 조건 번호 · 시드"),[this]() { return Store.IsValid() ? double(Store->DraftSeed) : 42.; },[this](double V) { Store->DraftSeed=FMath::RoundToInt(V); },0,2147483000.)];
  auto Speeds=SNew(SHorizontalBox);
  for (int32 Speed:{1,2,4})
   Speeds->AddSlot().AutoWidth().Padding(0,0,4,0)[SNew(SButton).IsFocusable(false).ContentPadding(FMargin(10,8))
    .IsEnabled_Lambda([this]() { return !Busy(); })
    .ButtonColorAndOpacity_Lambda([this,Speed]() { return Store.IsValid() && Store->DraftSpeed==Speed ? Primary : Surface; })
    .OnClicked_Lambda([this,Speed]() { Store->DraftSpeed=Speed;return FReply::Handled(); })[Label(FString::Printf(TEXT("%d배"),Speed),15)]];
  Settings->AddSlot().Padding(0,0,0,8)[SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)[Label(TEXT("실행 배속"),14,Muted)]
   +SVerticalBox::Slot().AutoHeight()[Speeds]];

  return SNew(SScrollBox)
   +SScrollBox::Slot()[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1).Padding(0,0,10,0)[Panel(LayoutPanel)]
    +SHorizontalBox::Slot().FillWidth(1)[Panel(OrderPanel)]]
   +SScrollBox::Slot().Padding(0,10,0,0)[Panel(SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[Label(TEXT("3. 동일 조건으로 반복 실행"),18,WarehouseUIStyle::Text,true)]
    +SVerticalBox::Slot().AutoHeight()[Settings]
    +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,10)[Live([this]() { return Store.IsValid() ? FString::Printf(TEXT("선택 배치 %d개 × %d회 = 총 %d회 · 매 회 창고를 다시 불러와 시작합니다."),Store->SelectedLayoutIds.Num(),Store->DraftRepeats,Store->SelectedLayoutIds.Num()*Store->DraftRepeats) : FString(); },14,Muted)]
    +SVerticalBox::Slot().AutoHeight()[Action(TEXT("start"),TEXT("초기화하고 반복 실험 시작"),[this]() { const bool Ok=Store->StartBatch(Store->SelectedLayoutIds,Store->DraftRepeats,Store->DraftDurationSeconds,Store->DraftSeed,Store->DraftSpeed);if (Ok) Page=1;Report(Ok); },[this]() { return !Busy() && !Store->SelectedLayoutIds.IsEmpty() && !Store->Orders.IsEmpty(); },true)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)[Label(TEXT("배치 간 물류 구성이나 주문 대상이 다르면 실행을 막고 이유를 표시합니다. 같은 시드여도 물리·센서 결과에는 작은 차이가 생길 수 있어 여러 번 비교합니다."),13,Muted)])];
 }

 TSharedRef<SWidget> BuildResults()
 {
  return SNew(SVerticalBox)
   +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1)[Label(TEXT("모든 시간은 시뮬레이션 기준 · 평균과 95% 시간은 완료한 주문만 계산 · 실패·미완료도 함께 비교하세요."),14,Muted)]
    +SHorizontalBox::Slot().AutoWidth().Padding(8,0)[Action(TEXT("stop"),TEXT("실험 중지 · 현재 결과 기록"),[this]() { Store->StopBatch();Report(true); },[this]() { return Store.IsValid() && Store->bBatchRunning; })]
    +SHorizontalBox::Slot().AutoWidth()[Action(TEXT("export"),TEXT("비교 결과 CSV 내보내기"),[this]() { Report(Store->ExportCSV()); },[this]() { return Store.IsValid() && !Store->GetResults().IsEmpty(); })]]
   +SVerticalBox::Slot().FillHeight(1)[SNew(SScrollBox)
    +SScrollBox::Slot()[SAssignNew(LiveRows,SVerticalBox)]
    +SScrollBox::Slot().Padding(0,10,0,0)[SAssignNew(ResultRows,SVerticalBox)]];
 }

 void RefreshChoices(TArray<FChoicePtr>& Values,FChoicePtr& Selected,TSharedPtr<SComboBox<FChoicePtr>>& Widget,const TArray<FWarehouseExperimentChoice>& Incoming)
 {
  bool Same=Values.Num()==Incoming.Num();
  for (int32 I=0;Same && I<Incoming.Num();++I) Same=Values[I]->Id==Incoming[I].Id && Values[I]->Label==Incoming[I].Label;
  if (Same) return;
  const FString Previous=Selected ? Selected->Id : FString();Values.Reset();Selected.Reset();
  for (const auto& Choice:Incoming) { auto Item=MakeShared<FWarehouseExperimentChoice>(Choice);Values.Add(Item);if (Choice.Id==Previous) Selected=Item; }
  if (!Selected && !Values.IsEmpty()) Selected=Values[0];
  if (Widget) { Widget->RefreshOptions();Widget->SetSelectedItem(Selected); }
 }
 void Refresh(bool ReadSavedFiles=false)
 {
  if (!Store.IsValid()) return;
  // Snapshot files may be large. Read their index only when opening the screen
  // or after a user action, never once per simulated second or frame.
  if (ReadSavedFiles) Layouts=Store->GetLayouts();
  Store->SelectedLayoutIds.RemoveAll([this](const FString& Id) { return !Layouts.ContainsByPredicate([&](const auto& Layout) { return Layout.Id==Id; }); });
  if (!SelectedLayout()) { LoadLayoutId=Layouts.IsEmpty() ? FString() : Layouts[0].Id;ResetLayoutConfirmation(); }
  RefreshChoices(Sources,Source,SourceCombo,Store->GetOrderSources());
  RefreshChoices(Destinations,Destination,DestinationCombo,Store->GetDestinations());
  if (ReadSavedFiles) RefreshChoices(Workloads,Workload,WorkloadCombo,Store->GetWorkloads());
 }
 FString ChoiceLabel(const TArray<FChoicePtr>& Values,const FString& Id) const
 {
  if (const auto* Choice=Values.FindByPredicate([&](const auto& Item) { return Item->Id==Id; })) return (*Choice)->Label;
  return TEXT("현재 배치에서 찾을 수 없는 대상");
 }
 void RefreshViews()
 {
  if (!Store.IsValid() || !LayoutRows || !OrderRows || !ResultRows) return;
  FString NewLayoutKey;
  for (const auto& Layout:Layouts) NewLayoutKey+=Layout.Id+Layout.Hash+Layout.Name;
  if (NewLayoutKey!=LayoutRowsKey || LayoutRows->NumSlots()==0)
  {
   LayoutRowsKey=NewLayoutKey;LayoutRows->ClearChildren();LayoutChecks.Reset();
   for (auto It=Buttons.CreateIterator();It;++It) if (It.Key().StartsWith(TEXT("pickLayout:"))) It.RemoveCurrent();
   if (Layouts.IsEmpty()) LayoutRows->AddSlot().AutoHeight()[Label(TEXT("아직 저장한 배치가 없습니다. 초기 배치를 먼저 저장하세요."),14,Muted)];
   for (const auto& Layout:Layouts)
   {
    TSharedPtr<SCheckBox> Check;
    LayoutRows->AddSlot().AutoHeight().Padding(0,5)[SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1)[SAssignNew(Check,SCheckBox).IsEnabled_Lambda([this]() { return !Busy(); })
      .IsChecked_Lambda([this,Id=Layout.Id]() { return Store.IsValid() && Store->SelectedLayoutIds.Contains(Id) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
      .OnCheckStateChanged_Lambda([this,Id=Layout.Id](ECheckBoxState Check) { if (Check==ECheckBoxState::Checked) Store->SelectedLayoutIds.AddUnique(Id);else Store->SelectedLayoutIds.Remove(Id); })
      [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight()[Label(Layout.Name,16)]
       +SVerticalBox::Slot().AutoHeight()[Label(FString::Printf(TEXT("상자 %d개 · 보류 %d개"),Layout.CargoCount,Layout.HeldCargoCount),12,Muted)]]]
     +SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0)[Action(TEXT("pickLayout:")+Layout.Id,TEXT("대상 선택"),[this,Id=Layout.Id]() { SelectLayout(Id); },[this]() { return !Busy(); })]];
    Buttons[TEXT("pickLayout:")+Layout.Id]->SetContent(Live([this,Id=Layout.Id]() { return LoadLayoutId==Id ? TEXT("선택됨") : TEXT("대상 선택"); },14));
    LayoutChecks.Add(Layout.Id,Check);
   }
  }
  FString NewOrderKey;
  for (const auto& Order:Store->Orders) NewOrderKey+=Order.OrderId+Order.PalletId+Order.DestinationId+FString::SanitizeFloat(Order.ReleaseSeconds);
  for (const auto& Item:Sources) NewOrderKey+=Item->Id+Item->Label;
  for (const auto& Item:Destinations) NewOrderKey+=Item->Id+Item->Label;
  if (NewOrderKey!=OrderRowsKey || OrderRows->NumSlots()==0)
  {
   OrderRowsKey=NewOrderKey;OrderRows->ClearChildren();
   for (auto It=Buttons.CreateIterator();It;++It) if (It.Key().StartsWith(TEXT("removeOrder"))) It.RemoveCurrent();
   if (Store->Orders.IsEmpty()) OrderRows->AddSlot().AutoHeight()[Label(TEXT("주문을 추가하면 모든 선택 배치에 똑같이 적용합니다."),14,Muted)];
   for (int32 I=0;I<Store->Orders.Num();++I)
   {
    const auto& Order=Store->Orders[I];
    OrderRows->AddSlot().AutoHeight().Padding(0,4)[SNew(SHorizontalBox)
     +SHorizontalBox::Slot().FillWidth(1)[SNew(SVerticalBox)
      +SVerticalBox::Slot().AutoHeight()[Label(FString::Printf(TEXT("%d · %.0f초  %s"),I+1,Order.ReleaseSeconds,*ChoiceLabel(Sources,Order.PalletId)),14)]
      +SVerticalBox::Slot().AutoHeight()[Label(TEXT("→ ")+ChoiceLabel(Destinations,Order.DestinationId),13,Muted)]]
     +SHorizontalBox::Slot().AutoWidth()[Action(FString::Printf(TEXT("removeOrder%d"),I),TEXT("제외"),[this,I]() { Report(Store->RemoveOrder(I)); },[this]() { return !Busy(); })]];
   }
  }
  LiveRows->ClearChildren();
  if (Store->bBatchRunning)
   LiveRows->AddSlot().AutoHeight()[Panel(SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[Label(TEXT("현재 실행"),18,WarehouseUIStyle::Text,true)]
    +SVerticalBox::Slot().AutoHeight()[SummaryCards(Store->GetLiveSummary())]
    +SVerticalBox::Slot().AutoHeight().Padding(0,6)[WaitCauses(Store->GetLiveSummary())]
    +SVerticalBox::Slot().AutoHeight().Padding(0,6)[Label(TEXT("차량별 충전·충전 대기 합계 · 위 주문 대기와 합산하지 않음"),13,Muted)]
    +SVerticalBox::Slot().AutoHeight()[WaitCauses(Store->GetLiveSummary(),true)])];

  const auto& Results=Store->GetResults();
  if (DrawnResultCount==Results.Num()) return;
  DrawnResultCount=Results.Num();ResultRows->ClearChildren();
  if (Results.IsEmpty()) { ResultRows->AddSlot().AutoHeight()[Panel(Label(TEXT("아직 기록된 결과가 없습니다. 배치와 공통 주문을 선택하여 실험을 시작하세요."),16,Muted))];return; }
  // Only like-for-like conditions share a comparison group. Never average unrelated workloads.
  TMap<FString,TArray<const FWarehouseExperimentResult*>> Groups;
  TArray<FString> GroupOrder;
  for (const auto& Result:Results)
  {
   if (Result.Status!=TEXT("completed")) continue;
   const auto& Info=Result.Info;
   const FString Key=Info.ScenarioFingerprint+TEXT("|")+Info.LayoutFingerprint+TEXT("|")+Info.CargoFingerprint+TEXT("|")+Info.MapPackage+TEXT("|")+Info.EngineVersion+TEXT("|")+Info.RuntimeRevision+
    FString::Printf(TEXT("|%.9f|%.9f|%.3f|%d|%d"),Info.StepSeconds,Info.PhysicsSubstepSeconds,Info.DurationSeconds,Info.Seed,Info.RequestedSpeed);
   if (!Groups.Contains(Key)) GroupOrder.Add(Key);Groups.FindOrAdd(Key).Add(&Result);
  }
  ResultRows->AddSlot().AutoHeight().Padding(0,0,0,8)[Label(TEXT("배치별 반복 평균 · 정해진 시간까지 정상 실행한 기록만"),20,WarehouseUIStyle::Text,true)];
  if (Groups.IsEmpty()) ResultRows->AddSlot().AutoHeight().Padding(0,0,0,8)[Label(TEXT("중지되거나 조건이 바뀐 실험은 평균에서 제외합니다. 개별 기록은 아래에 남습니다."),14,WarehouseUIStyle::Warning)];
  for (const FString& Key:GroupOrder)
  {
   const auto& Runs=Groups[Key];const auto& First=*Runs[0];
   double Throughput=0,Elapsed=0,Mean=0,P95=0;int32 CompletedRuns=0,Completed=0,Failed=0,Cancelled=0,Unfinished=0,NotReleased=0;
   TMap<FString,double> Waits,VehicleTimes;
   for (const auto* Run:Runs)
   {
    const auto& S=Run->Summary;Throughput+=S.ThroughputPerHour;Elapsed+=S.ElapsedSeconds;Completed+=S.Completed;Failed+=S.Failed;Cancelled+=S.Cancelled;Unfinished+=S.Unfinished;NotReleased+=S.NotReleased;
    if (S.Completed>0) { Mean+=S.MeanTransportSeconds;P95+=S.P95TransportSeconds;++CompletedRuns; }
    for (const auto& Phase:S.JobPhaseSeconds) Waits.FindOrAdd(Phase.Key)+=Phase.Value;
    for (const auto& Phase:S.VehiclePhaseSeconds) VehicleTimes.FindOrAdd(Phase.Key)+=Phase.Value;
   }
   FWarehouseExperimentSummary Aggregate;Aggregate.JobPhaseSeconds=Waits;Aggregate.VehiclePhaseSeconds=VehicleTimes;
   for (auto& Phase:Aggregate.JobPhaseSeconds) Phase.Value/=Runs.Num();
   for (auto& Phase:Aggregate.VehiclePhaseSeconds) Phase.Value/=Runs.Num();
   auto Group=SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[Label(FString::Printf(TEXT("%s · %d회"),*First.LayoutName,Runs.Num()),18,WarehouseUIStyle::Accent,true)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,4,0,8)[Label(FString::Printf(TEXT("조건 %s · 시드 %d · %d배 · 한도 %s · 조건·물류·실행 버전이 다르면 따로 표시"),*First.Info.ScenarioFingerprint.Left(8),First.Info.Seed,First.Info.RequestedSpeed,*Duration(First.Info.DurationSeconds)),12,Muted)]
    +SVerticalBox::Slot().AutoHeight()[Label(FString::Printf(TEXT("처리량 평균 %.1f 팔레트/시간 · 실행 평균 %s"),Throughput/Runs.Num(),*Duration(Elapsed/Runs.Num())),16)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Label(CompletedRuns>0 ? FString::Printf(TEXT("완료 주문 평균시간의 회별 평균 %s · 회별 95%% 시간의 평균 %s"),*Duration(Mean/CompletedRuns),*Duration(P95/CompletedRuns)) : TEXT("완료한 주문이 없어 운반 시간을 계산하지 않았습니다."),14,Muted)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,4)[Label(FString::Printf(TEXT("전체 반복 합계  완료 %d · 실패 %d · 취소 %d · 미완료 %d · 주문 시각 이전 %d"),Completed,Failed,Cancelled,Unfinished,NotReleased),14,WarehouseUIStyle::Warning)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,2)[Label(TEXT("주문별 대기·복구 합계의 회별 평균"),14,WarehouseUIStyle::Text,true)]
    +SVerticalBox::Slot().AutoHeight()[WaitCauses(Aggregate)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,8,0,2)[Label(TEXT("차량별 충전·충전 대기 합계의 회별 평균 · 주문 대기와 별도"),13,Muted)]
    +SVerticalBox::Slot().AutoHeight()[WaitCauses(Aggregate,true)];
   ResultRows->AddSlot().AutoHeight().Padding(0,0,0,10)[Panel(Group)];
  }
  ResultRows->AddSlot().AutoHeight().Padding(0,4,0,8)[Label(TEXT("개별 실행 기록"),20,WarehouseUIStyle::Text,true)];
  for (int32 I=Results.Num()-1;I>=0;--I)
  {
   const auto& Result=Results[I];
   ResultRows->AddSlot().AutoHeight().Padding(0,0,0,8)[Panel(SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)[Label(FString::Printf(TEXT("%s · %d회차 · %s · 실제 처리 %.2f배"),*Result.LayoutName,Result.Info.RepeatIndex,*StateLabel(Result.Status),Result.AchievedSpeed),16,WarehouseUIStyle::Text,true)]
    +SVerticalBox::Slot().AutoHeight()[SummaryCards(Result.Summary)]
    +SVerticalBox::Slot().AutoHeight()[Label(FString::Printf(TEXT("주문 시각 이전 %d건 · 고장 기록 %d회"),Result.Summary.NotReleased,Result.Summary.FaultEvents),13,Muted)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,6)[WaitCauses(Result.Summary)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,6)[Label(TEXT("차량별 충전·충전 대기 합계 · 주문 대기와 별도"),13,Muted)]
    +SVerticalBox::Slot().AutoHeight()[WaitCauses(Result.Summary,true)])];
  }
 }

#if WITH_EDITOR && !UE_BUILD_SHIPPING
public:
 // Drives the same Slate callbacks as the user; compiled out of packaged games.
 void Test(const TArray<FString>& Args)
 {
  Refresh(true);RefreshViews();
  bool Accepted=Args.IsEmpty() || Args[0]==TEXT("snapshot");
  if (Args.Num()>=2 && Args[0]==TEXT("click"))
  {
   if (auto Button=Buttons.FindRef(Args[1])) { Button->UpdateAllAttributes();if (Button->IsEnabled()) { Button->SimulateClick();Accepted=true; } }
  }
  else if (Args.Num()>=3 && Args[0]==TEXT("text"))
  {
   if (Args[1]==TEXT("layout")) { LayoutName->SetText(FText::FromString(Args[2]));Accepted=true; }
   if (Args[1]==TEXT("workload")) { WorkloadName->SetText(FText::FromString(Args[2]));Accepted=true; }
  }
  else if (Args.Num()>=3 && Args[0]==TEXT("choose"))
  {
   auto Choose=[&](const TArray<FChoicePtr>& Values,const TSharedPtr<SComboBox<FChoicePtr>>& Widget)
   { if (const auto* Item=Values.FindByPredicate([&](const auto& Value) { return Value->Id==Args[2]; })) { Widget->SetSelectedItem(*Item);Accepted=true; } };
   if (Args[1]==TEXT("source")) Choose(Sources,SourceCombo);
   if (Args[1]==TEXT("destination")) Choose(Destinations,DestinationCombo);
   if (Args[1]==TEXT("workload")) Choose(Workloads,WorkloadCombo);
  }
  else if (Args.Num()>=2 && Args[0]==TEXT("toggleLayout"))
  {
   if (const auto* Check=LayoutChecks.Find(Args[1])) { (*Check)->UpdateAllAttributes();if ((*Check)->IsEnabled()) { (*Check)->ToggleCheckedState();Accepted=true; } }
  }
  auto Quote=[](FString Value) { return TEXT("\"")+Value.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\"")).Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"))+TEXT("\""); };
  TArray<FString> Enabled,LayoutIds,SourceIds,DestinationIds,WorkloadIds,SelectedIds;
  for (const auto& Layout:Layouts) LayoutIds.Add(Quote(Layout.Id));
  for (const auto& Item:Sources) SourceIds.Add(Quote(Item->Id));
  for (const auto& Item:Destinations) DestinationIds.Add(Quote(Item->Id));
  for (const auto& Item:Workloads) WorkloadIds.Add(Quote(Item->Id));
  if (Store.IsValid()) for (const FString& Id:Store->SelectedLayoutIds) SelectedIds.Add(Quote(Id));
  for (const auto& Entry:Buttons) { Entry.Value->UpdateAllAttributes();Enabled.Add(Quote(Entry.Key)+TEXT(":")+(Entry.Value->IsEnabled() ? TEXT("true") : TEXT("false"))); }
  const FVector2D Size=GetCachedGeometry().GetLocalSize();
  const FString Json=FString::Printf(TEXT("{\"accepted\":%s,\"page\":%d,\"layout_count\":%d,\"order_count\":%d,\"result_count\":%d,\"width\":%.1f,\"height\":%.1f,\"notice\":%s,\"busy\":%s,\"actions_enabled\":{%s},\"layout_ids\":[%s],\"source_ids\":[%s],\"destination_ids\":[%s],\"workload_ids\":[%s],\"selected_layout_ids\":[%s]}"),Accepted ? TEXT("true") : TEXT("false"),Page,Layouts.Num(),Store.IsValid() ? Store->Orders.Num():0,Store.IsValid() ? Store->GetResults().Num():0,Size.X,Size.Y,*Quote(Notice),Busy() ? TEXT("true") : TEXT("false"),*FString::Join(Enabled,TEXT(",")),*FString::Join(LayoutIds,TEXT(",")),*FString::Join(SourceIds,TEXT(",")),*FString::Join(DestinationIds,TEXT(",")),*FString::Join(WorkloadIds,TEXT(",")),*FString::Join(SelectedIds,TEXT(",")));
  const FString Directory=FPaths::ProjectSavedDir()/TEXT("Testing/WarehouseExperiments");IFileManager::Get().MakeDirectory(*Directory,true);
  FFileHelper::SaveStringToFile(Json,*(Directory/TEXT("UIState.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 }
#endif
};
#if WITH_EDITOR && !UE_BUILD_SHIPPING
TWeakPtr<SWarehouseExperiments> TestPanel;
FAutoConsoleCommand ExperimentUITestCommand(TEXT("Warehouse.Experiment.UI"),TEXT("Editor-only experiment UI regression: snapshot, click, text, choose."),
 FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) { if (auto Panel=TestPanel.Pin()) Panel->Test(Args); }));
#endif
}

TSharedRef<SWidget> MakeWarehouseExperimentPanel(Amsc_vrPlayerController* Controller,bool Modal)
{
 auto Widget=SNew(WarehouseExperimentUI::SWarehouseExperiments).Controller(Controller).Modal(Modal);
#if WITH_EDITOR && !UE_BUILD_SHIPPING
 WarehouseExperimentUI::TestPanel=Widget;
#endif
 return Widget;
}
