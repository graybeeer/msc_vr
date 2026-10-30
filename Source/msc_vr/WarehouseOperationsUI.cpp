#include "WarehouseOperationsUI.h"
#include "WarehouseComputer.h"
#include "WarehouseExperimentSubsystem.h"
#include "WarehouseInboundZone.h"
#include "WarehouseLogisticsTruck.h"
#include "WarehousePallet.h"
#include "WarehousePlayMode.h"
#include "WarehouseShippingSystem.h"
#include "WarehouseUIStyle.h"
#include "msc_vrPlayerController.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

namespace WarehouseOperationsUI
{
const FLinearColor Surface(.025f,.045f,.064f),Muted(.57f,.66f,.73f),Primary(.055f,.30f,.36f);

TSharedRef<STextBlock> Label(const FString& Value,int32 Size=16,FLinearColor Color=WarehouseUIStyle::Text,bool Bold=false)
{
 return SNew(STextBlock).Text(FText::FromString(Value)).Font(WarehouseUIStyle::Font(Size,Bold))
  .ColorAndOpacity(Color).AutoWrapText(true);
}
struct FQuarantinedPallet
{
 TWeakObjectPtr<AWarehousePallet> Pallet;
 TWeakObjectPtr<AWarehouseInboundZone> Zone;
 FText Label;
};
using FQuarantineChoice=TSharedPtr<FQuarantinedPallet>;

class SWarehouseOperations : public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SWarehouseOperations) {}
 SLATE_ARGUMENT(TWeakObjectPtr<AWarehouseComputer>,Computer)
 SLATE_ARGUMENT(TWeakObjectPtr<Amsc_vrPlayerController>,Controller)
 SLATE_END_ARGS()

 void Construct(const FArguments& Args)
 {
  Computer=Args._Computer;Controller=Args._Controller;
  Refresh();
  ChildSlot[SNew(SScrollBox)
   +SScrollBox::Slot().Padding(0,0,0,12)[Section(TEXT("입고 검수"),SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[CachedText(InspectionSummary,20,WarehouseUIStyle::Accent)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,8)[CachedText(InspectionDetails)]
    +SVerticalBox::Slot().AutoHeight()[Label(TEXT("검수 합격 → 랩핑 → 지게차 운반 · 수량·품목 불일치와 파손은 격리합니다."),14,Muted)])]
   +SScrollBox::Slot().Padding(0,0,0,12)[Section(TEXT("격리 물류 확인"),SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[SAssignNew(QuarantineCombo,SComboBox<FQuarantineChoice>)
     .OptionsSource(&QuarantineChoices)
     .OnGenerateWidget_Lambda([](FQuarantineChoice Item) { return Label(Item.IsValid() ? Item->Label.ToString() : FString()); })
     .OnSelectionChanged_Lambda([this](FQuarantineChoice Item,ESelectInfo::Type) { Select(Item); })
     [CachedText(SelectedLabel)]]
    +SVerticalBox::Slot().AutoHeight().Padding(0,8)[CachedText(SelectedDetails)]
    +SVerticalBox::Slot().AutoHeight()[SNew(SWrapBox).UseAllottedSize(true)
     +SWrapBox::Slot().Padding(0,0,12,8)[Label(TEXT("예정 상자 수량"))]
     +SWrapBox::Slot().Padding(0,0,12,8)[SNew(SNumericEntryBox<int32>).MinValue(0).MaxValue(10000).AllowSpin(false)
      .MinDesiredValueWidth(100).Font(WarehouseUIStyle::Font(16))
      .Value_Lambda([this]() -> TOptional<int32> { return ExpectedCount; })
      .IsEnabled_Lambda([this]() { return CanHandleSelected(); })
      .OnValueChanged_Lambda([this](int32 Value) { ExpectedCount=FMath::Clamp(Value,0,10000); })]
     +SWrapBox::Slot().Padding(0,0,8,8)[Action(TEXT("예정 수량 수정"),[this]() { CorrectCount(); },[this]() { return CanHandleSelected(); })]
     +SWrapBox::Slot().Padding(0,0,8,8)[Action(TEXT("재검수 접수"),[this]() { Reinspect(); },[this]() { return CanHandleSelected(); },true)]]
    +SVerticalBox::Slot().AutoHeight()[Label(TEXT("0은 예정 수량 미지정입니다. 품목별 명세는 보존되며, 수량 수정 후 재검수를 접수해야 합니다."),14,Muted)])]
   +SScrollBox::Slot().Padding(0,0,0,12)[Section(TEXT("출고 준비 · 도크"),SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[CachedText(PreparationSummary)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,8)[CachedText(DockSummary)]
    +SVerticalBox::Slot().AutoHeight()[Label(TEXT("검품·라벨 처리와 도크 준비가 끝나면 한 묶음씩 상하차합니다. 출차 정리를 마친 트럭이 출발합니다."),14,Muted)])]
   +SScrollBox::Slot().Padding(0,0,0,12)[Section(TEXT("반복 입고"),SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[CachedText(OperationsSummary,18,WarehouseUIStyle::Accent)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,8)[CachedText(OperationsSettings)]
    +SVerticalBox::Slot().AutoHeight()[SNew(SWrapBox).UseAllottedSize(true)
     +SWrapBox::Slot().Padding(0,0,12,8)[Setting(TEXT("방문 간격 (초)"),VisitInterval,5,3600)]
     +SWrapBox::Slot().Padding(0,0,12,8)[Setting(TEXT("생성 한도 (묶음)"),UnitLimit,1,1000)]
     +SWrapBox::Slot().Padding(0,0,12,8)[Setting(TEXT("물량 조건 번호"),RecipeSeed,0,MAX_int32)]
     +SWrapBox::Slot().Padding(0,0,12,8)[Action(TEXT("설정 적용"),[this]() { ApplySettings(); },[this]() { return CanConfigure(); })]]
    +SVerticalBox::Slot().AutoHeight()[SNew(SWrapBox).UseAllottedSize(true)
     +SWrapBox::Slot().Padding(0,0,8,8)[Action(TEXT("반복 입고 시작"),[this]() { SetContinuous(true); },[this]() { return !bReadOnly && Shipping.IsValid() && !bContinuous; },true)]
     +SWrapBox::Slot().Padding(0,0,8,8)[Action(TEXT("새 물량 유입 중지"),[this]() { SetContinuous(false); },[this]() { return !bReadOnly && Shipping.IsValid() && bContinuous; })]]
    +SVerticalBox::Slot().AutoHeight()[Label(TEXT("선택한 시뮬레이션에서만 켭니다. 출고 주문은 배송편 탭에서 직접 지정합니다. 중지해도 이미 접수된 물류·작업은 계속되며, 설정은 중지 상태에서 적용합니다. 공정 시간은 시뮬레이션 시간을 사용합니다."),14,Muted)])]
   +SScrollBox::Slot().Padding(0,0,0,8)[CachedText(AccessReason,14,WarehouseUIStyle::Warning)]
   +SScrollBox::Slot()[CachedText(Notice,16,WarehouseUIStyle::Warning)]];
  if (Selected.IsValid()) QuarantineCombo->SetSelectedItem(Selected);
 }

 virtual void Tick(const FGeometry& Geometry,double Now,float DeltaSeconds) override
 {
  SCompoundWidget::Tick(Geometry,Now,DeltaSeconds);
  if (Now>=NextRefresh) { NextRefresh=Now+1.;Refresh(); }
 }

private:
 TWeakObjectPtr<AWarehouseComputer> Computer;
 TWeakObjectPtr<Amsc_vrPlayerController> Controller;
 TWeakObjectPtr<AWarehouseShippingSystem> Shipping;
 TArray<FQuarantineChoice> QuarantineChoices;
 FQuarantineChoice Selected;
 TSharedPtr<SComboBox<FQuarantineChoice>> QuarantineCombo;
 FText InspectionSummary,InspectionDetails,SelectedLabel,SelectedDetails,PreparationSummary,DockSummary;
 FText OperationsSummary,OperationsSettings,AccessReason,Notice;
 double NextRefresh=0;
 int32 ExpectedCount=0;
 int32 VisitInterval=45,UnitLimit=24,RecipeSeed=20261029;
 bool bReadOnly=true,bContinuous=false,bSettingsLoaded=false;

 TSharedRef<SWidget> CachedText(const FText& Value,int32 Size=16,FLinearColor Color=WarehouseUIStyle::Text)
 {
  // Attributes read only cached text; actor iteration and state aggregation happen once per second.
  const FText* Cached=&Value;
  return SNew(STextBlock).Text_Lambda([Cached]() { return *Cached; }).Font(WarehouseUIStyle::Font(Size))
   .ColorAndOpacity(Color).AutoWrapText(true);
 }
 TSharedRef<SWidget> Section(const FString& Title,const TSharedRef<SWidget>& Content)
 {
  return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Surface).Padding(18)
   [SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)[Label(Title,20,WarehouseUIStyle::Text,true)]
    +SVerticalBox::Slot().AutoHeight()[Content]];
 }
 TSharedRef<SWidget> Action(const FString& Title,TFunction<void()> Callback,TFunction<bool()> Enabled,bool Main=false)
 {
  return SNew(SButton).ContentPadding(FMargin(14,10)).IsFocusable(false)
   .ButtonColorAndOpacity(Main ? Primary : FLinearColor(.09f,.13f,.17f)).IsEnabled_Lambda([Enabled]() { return Enabled(); })
   .OnClicked_Lambda([Callback]() { Callback();return FReply::Handled(); })[Label(Title,16,WarehouseUIStyle::Text,true)];
 }
 TSharedRef<SWidget> Setting(const FString& Title,int32& Value,int32 Minimum,int32 Maximum)
 {
  int32* Storage=&Value;
  return SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)[Label(Title,14,Muted)]
   +SVerticalBox::Slot().AutoHeight()[SNew(SNumericEntryBox<int32>).MinValue(Minimum).MaxValue(Maximum).AllowSpin(false)
    .MinDesiredValueWidth(150).Font(WarehouseUIStyle::Font(16))
    .Value_Lambda([Storage]() -> TOptional<int32> { return *Storage; })
    .IsEnabled_Lambda([this]() { return CanConfigure(); })
    .OnValueChanged_Lambda([Storage,Minimum,Maximum](int32 NewValue) { *Storage=FMath::Clamp(NewValue,Minimum,Maximum); })];
 }
 bool CanConfigure() const { return !bReadOnly && Shipping.IsValid() && !bContinuous; }
 bool CanHandleSelected() const
 {
  return !bReadOnly && Selected.IsValid() && Selected->Zone.IsValid() && Selected->Pallet.IsValid()
   && Selected->Pallet->ReceivingState==EWarehouseReceivingState::Quarantined;
 }
 void UpdateAccess()
 {
  bReadOnly=true;
  if (!Computer.IsValid() || !Computer->CanUse()) { AccessReason=FText::FromString(TEXT("관제 컴퓨터 연결을 확인해 주세요."));return; }
  if (AWarehousePlayStage::IsPlay(Computer.Get())) { AccessReason=FText::FromString(TEXT("플레이 모드에서는 공정 현황만 확인할 수 있습니다. 반복 입고와 명세 수정은 관리자 모드에서 사용하세요."));return; }
  const auto* Instance=Controller.IsValid() ? Controller->GetGameInstance() : nullptr;
  const auto* Experiments=Instance ? Instance->GetSubsystem<UWarehouseExperimentSubsystem>() : nullptr;
  if (Experiments && (Experiments->bLoading || Experiments->bBatchRunning))
  { AccessReason=FText::FromString(TEXT("반복 실험 진행 중에는 물량 조건과 검수 명세를 변경할 수 없습니다."));return; }
  bReadOnly=false;AccessReason=FText::GetEmpty();
 }
 void Select(FQuarantineChoice Item)
 {
  Selected=Item;ExpectedCount=Selected.IsValid() && Selected->Pallet.IsValid() ? Selected->Pallet->ExpectedCartonCount : 0;
  RefreshSelected();
 }
 void RefreshSelected()
 {
  if (!Selected.IsValid() || !Selected->Pallet.IsValid())
  {
   SelectedLabel=FText::FromString(TEXT("격리된 팔레트가 없습니다."));
   SelectedDetails=FText::FromString(TEXT("검수에서 확인이 필요한 팔레트가 생기면 이 목록에 표시됩니다."));return;
  }
  SelectedLabel=Selected->Label;
  const auto* Pallet=Selected->Pallet.Get();
  TArray<FString> Manifest;
  for (const auto& Pair:Pallet->ExpectedCargoByKind) Manifest.Add(FString::Printf(TEXT("%s %d개"),*Pair.Key,Pair.Value));
  Manifest.Sort();
  SelectedDetails=FText::FromString(Pallet->ReceivingStatus+TEXT("\n예정 상자: ")
   +(Pallet->ExpectedCartonCount>0 ? FString::Printf(TEXT("%d개"),Pallet->ExpectedCartonCount) : FString(TEXT("미지정")))
   +TEXT(" · 품목 명세: ")+(Manifest.IsEmpty() ? FString(TEXT("미지정")) : FString::Join(Manifest,TEXT(", ")))
   +(Selected->Zone.IsValid() ? FString() : FString(TEXT("\n입고 구역 안으로 옮겨야 재검수할 수 있습니다."))));
 }
 void Refresh()
 {
  UpdateAccess();
  auto* World=Computer.IsValid() ? Computer->GetWorld() : nullptr;
  if (!World) return;
  if (!Shipping.IsValid()) for (TActorIterator<AWarehouseShippingSystem> It(World);It;++It) { Shipping=*It;break; }
  TArray<AWarehouseInboundZone*> Zones;
  TArray<FString> ZoneLines;
  int32 Waiting=0,Inspecting=0,Quarantined=0;
  for (TActorIterator<AWarehouseInboundZone> It(World);It;++It)
  {
   Zones.Add(*It);Waiting+=It->WaitingInspectionCount;Inspecting+=It->ActiveInspectionCount;Quarantined+=It->QuarantinedCount;
   FString ZoneLabel=FString::Printf(TEXT("입고 구역 %d"),Zones.Num());
   if (Shipping.IsValid()) for (const auto& Zone:Shipping->Zones) if (Zone.bInbound && Zone.Anchor==*It) { ZoneLabel=Zone.ZoneId;break; }
   ZoneLines.Add(FString::Printf(TEXT("%s · 검수 %d자리 · 1회 %.1f초\n%s"),*ZoneLabel,It->InspectionWorkers,It->InspectionSeconds,*It->Status));
  }
  ZoneLines.Sort();
  InspectionSummary=FText::FromString(FString::Printf(TEXT("검수 대기 %d · 진행 %d · 격리 %d"),Waiting,Inspecting,Quarantined));
  InspectionDetails=FText::FromString(ZoneLines.IsEmpty() ? TEXT("입고 구역이 없습니다. 배치 모드에서 입고 구역을 추가하세요.") : FString::Join(ZoneLines,TEXT("\n\n")));

  const auto SelectedPallet=Selected.IsValid() ? Selected->Pallet : TWeakObjectPtr<AWarehousePallet>();
  const auto PreviousChoices=QuarantineChoices;
  QuarantineChoices.Reset();
  for (TActorIterator<AWarehousePallet> It(World);It;++It)
  {
   if (It->ReceivingState!=EWarehouseReceivingState::Quarantined) continue;
   const auto* Old=PreviousChoices.FindByPredicate([&](const FQuarantineChoice& Choice) { return Choice->Pallet.Get()==*It; });
   auto Choice=Old ? *Old : MakeShared<FQuarantinedPallet>();Choice->Pallet=*It;Choice->Zone.Reset();
   for (auto* Zone:Zones) if (Zone->ContainsPallet(*It)) { Choice->Zone=Zone;break; }
   const auto* Item=Computer->Inventory.FindByPredicate([&](const auto& Row) { return Row.Pallet==*It; });
   Choice->Label=FText::FromString(Item ? Item->Product+TEXT(" · ")+Item->Location+TEXT(" · ")+Item->CargoId : FString::Printf(TEXT("격리 팔레트 %u"),It->GetUniqueID()));
   QuarantineChoices.Add(Choice);
  }
  QuarantineChoices.Sort([](const FQuarantineChoice& A,const FQuarantineChoice& B) { return A->Label.ToString()<B->Label.ToString(); });
  const auto* Existing=QuarantineChoices.FindByPredicate([&](const FQuarantineChoice& Choice) { return Choice->Pallet==SelectedPallet; });
  const auto NewSelected=Existing ? *Existing : QuarantineChoices.IsEmpty() ? FQuarantineChoice() : QuarantineChoices[0];
  if (NewSelected!=Selected) Select(NewSelected);else RefreshSelected();
  if (QuarantineCombo.IsValid())
  {
   const bool Changed=PreviousChoices!=QuarantineChoices;
   if (Changed)
   {
    QuarantineCombo->RefreshOptions();
    if (QuarantineCombo->GetSelectedItem()!=Selected) QuarantineCombo->SetSelectedItem(Selected);
   }
  }

  TArray<FString> Docks;
  for (TActorIterator<AWarehouseLogisticsTruck> It(World);It;++It)
  {
   Docks.Add(FString::Printf(TEXT("%s · %s\n%s\n%s"),*It->TruckId,*It->DockId,*It->DockServiceStatus,*It->Status));
  }
  Docks.Sort();DockSummary=FText::FromString(Docks.IsEmpty() ? TEXT("등록된 트럭이 없습니다.") : FString::Join(Docks,TEXT("\n\n")));
  bContinuous=Shipping.IsValid() && Shipping->bContinuousOperations;
  if (Shipping.IsValid())
  {
   if (!bSettingsLoaded)
   {
    VisitInterval=FMath::RoundToInt(Shipping->VisitIntervalSeconds);UnitLimit=Shipping->MaxGeneratedUnits;RecipeSeed=Shipping->ScenarioSeed;
    bSettingsLoaded=true;
   }
   PreparationSummary=FText::FromString(FString::Printf(TEXT("출고 검품·라벨 %d자리 · 팔레트당 %.1f초"),Shipping->OutboundPreparationStations,Shipping->OutboundPreparationSeconds));
   OperationsSummary=FText::FromString(Shipping->GetOperationsSummary());
   OperationsSettings=FText::FromString(FString::Printf(TEXT("추가 방문 간격 %.0f초 · 생성 한도 %d묶음 · 생성됨 %d묶음 · 물량 조건 번호 %d"),Shipping->VisitIntervalSeconds,Shipping->MaxGeneratedUnits,Shipping->GeneratedUnits,Shipping->ScenarioSeed));
  }
  else
  {
   PreparationSummary=FText::FromString(TEXT("배송 관리 시스템이 없습니다."));
   OperationsSummary=FText::FromString(TEXT("반복 입고를 시작할 수 없습니다. 배송 관리 시스템을 확인하세요."));
   OperationsSettings=FText::GetEmpty();
  }
 }
 void CorrectCount()
 {
  UpdateAccess();
  if (!CanHandleSelected()) return;
  const bool Success=Selected->Zone->SetExpectedManifest(Selected->Pallet.Get(),ExpectedCount,Selected->Pallet->ExpectedCargoByKind);
  Notice=FText::FromString(Success ? TEXT("예정 수량을 수정했습니다. 물류를 확인한 뒤 재검수를 접수하세요.") : TEXT("수정할 수 없습니다. 팔레트가 입고 구역에 있으며 검수·운반·랩핑 중이 아닌지 확인하세요."));
  Refresh();
 }
 void Reinspect()
 {
  UpdateAccess();
  if (!CanHandleSelected()) return;
  const bool Success=Selected->Zone->RequestReinspection(Selected->Pallet.Get());
  Notice=FText::FromString(Success ? TEXT("재검수를 접수했습니다. 검수 합격 후 랩핑과 운반이 진행됩니다.") : TEXT("재검수를 접수할 수 없습니다. 운반 중이거나 적재가 불안정한지 확인하세요."));
  Refresh();
 }
 void ApplySettings()
 {
  UpdateAccess();
  if (bReadOnly || !Shipping.IsValid() || Shipping->bContinuousOperations) return;
  Shipping->VisitIntervalSeconds=FMath::Clamp(VisitInterval,5,3600);
  Shipping->MaxGeneratedUnits=FMath::Clamp(UnitLimit,1,1000);
  Shipping->ScenarioSeed=FMath::Max(0,RecipeSeed);
  Notice=FText::FromString(TEXT("반복 입고 설정을 적용했습니다. 이미 생성된 물류는 변경되지 않습니다."));
  Refresh();
 }
 void SetContinuous(bool Enabled)
 {
  UpdateAccess();
  if (bReadOnly || !Shipping.IsValid()) return;
  const bool Success=Shipping->SetContinuousOperations(Enabled);
  Notice=FText::FromString(Success ? (Enabled ? TEXT("반복 입고를 시작했습니다.") : TEXT("새 물량 유입을 중지했습니다. 이미 접수된 작업은 계속됩니다.")) : Shipping->OperationsStatus);
  Refresh();
 }
};
}

TSharedRef<SWidget> MakeWarehouseOperationsPanel(AWarehouseComputer* Computer,Amsc_vrPlayerController* Controller)
{
 return SNew(WarehouseOperationsUI::SWarehouseOperations).Computer(Computer).Controller(Controller);
}
