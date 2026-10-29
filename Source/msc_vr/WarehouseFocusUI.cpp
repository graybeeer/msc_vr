#include "WarehouseFocusUI.h"
#include "WarehouseFocusDirector.h"
#include "WarehouseComputer.h"
#include "WarehouseForklift.h"
#include "WarehousePallet.h"
#include "WarehouseShippingSystem.h"
#include "WarehousePlayMode.h"
#include "WarehouseUIStyle.h"
#include "msc_vrPlayerController.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/SListView.h"
#if WITH_EDITOR && !UE_BUILD_SHIPPING
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#endif

namespace WarehouseFocusUI
{
const FLinearColor Surface(.025f,.045f,.064f),Muted(.65f,.72f,.78f);
FString VehicleState(const AWarehouseForklift* Vehicle)
{
 if(!IsValid(Vehicle)) return TEXT("차량 확인 필요");
 if(Vehicle->IsRemoteControlled()) return TEXT("플레이어 원격 조작 중");
 if(!Vehicle->bEStopReleased) return TEXT("비상 정지 · 해제 필요");
 if(Vehicle->AIState==EWarehouseAIState::Fault) return TEXT("고장 · 점검 필요");
 if(Vehicle->bEmergencyBlocked) return TEXT("이동 불가 · 주변 확인 필요");
 if(!Vehicle->bPowered) return TEXT("정지 · 운행 시작 대기");
 switch(Vehicle->AIState)
 {
  case EWarehouseAIState::Off:case EWarehouseAIState::Paused:return TEXT("일시정지");
  case EWarehouseAIState::SelfCheck:return TEXT("출발 전 자체 점검");
  case EWarehouseAIState::Ready:return TEXT("다음 작업 대기");
  case EWarehouseAIState::ValidateJob:return TEXT("운반 작업 확인");
  case EWarehouseAIState::PlanPickup:case EWarehouseAIState::PlanDelivery:return TEXT("이동 경로 준비");
  case EWarehouseAIState::NavigatePickup:return TEXT("팔레트로 이동 중");
  case EWarehouseAIState::SlowApproach:return TEXT("팔레트에 천천히 접근");
  case EWarehouseAIState::DetectPallet:case EWarehouseAIState::EstimatePose:return TEXT("팔레트 위치 확인");
  case EWarehouseAIState::AlignVehicle:case EWarehouseAIState::CorrectFork:return TEXT("팔레트와 포크 정렬");
  case EWarehouseAIState::InsertFork:case EWarehouseAIState::VerifyInsertion:return TEXT("팔레트에 포크 삽입");
  case EWarehouseAIState::LiftLoad:case EWarehouseAIState::VerifyLoad:return TEXT("화물 들어 올리기");
  case EWarehouseAIState::DepartPickup:case EWarehouseAIState::TravelHeight:return TEXT("화물을 들고 출발 준비");
  case EWarehouseAIState::NavigateDelivery:return TEXT("목적지로 화물 운반 중");
  case EWarehouseAIState::AlignUnload:return TEXT("내려놓을 위치 정렬");
  case EWarehouseAIState::LowerLoad:return TEXT("화물 내려놓기");
  case EWarehouseAIState::WithdrawFork:return TEXT("팔레트에서 포크 빼기");
  case EWarehouseAIState::VerifyUnload:return TEXT("화물 인계 확인");
  case EWarehouseAIState::ReportComplete:return TEXT("운반 완료 보고");
  case EWarehouseAIState::PlanCharge:case EWarehouseAIState::NavigateCharge:return TEXT("충전소로 이동 중");
  case EWarehouseAIState::DockCharge:return TEXT("충전기 연결 중");
  case EWarehouseAIState::Charging:return TEXT("배터리 충전 중");
  case EWarehouseAIState::LeaveCharger:return TEXT("충전소에서 출발");
  case EWarehouseAIState::WaitingObstacle:return TEXT("통로가 비기를 기다리는 중");
  case EWarehouseAIState::ElevatorApproach:return TEXT("승강기로 이동 중");
  case EWarehouseAIState::ElevatorCall:case EWarehouseAIState::ElevatorWait:return TEXT("승강기 도착 대기");
  case EWarehouseAIState::ElevatorBoard:return TEXT("승강기 탑승 중");
  case EWarehouseAIState::ElevatorRide:return TEXT("승강기로 층간 이동");
  case EWarehouseAIState::ElevatorExit:return TEXT("승강기에서 내리는 중");
  case EWarehouseAIState::RecoverLower:case EWarehouseAIState::RecoverWithdraw:return TEXT("다시 시도하도록 포크 조정");
  case EWarehouseAIState::RecoverReplan:return TEXT("막힌 경로를 다시 찾는 중");
  case EWarehouseAIState::PlanParking:case EWarehouseAIState::NavigateParking:return TEXT("대기 자리로 이동 중");
  case EWarehouseAIState::AlignParking:return TEXT("대기 자리에 정렬");
  case EWarehouseAIState::WaitingCharger:return TEXT("빈 충전 자리 대기");
  default:return TEXT("작업 진행 중");
 }
}
FString TaskState(const FWarehouseComputerTask& Task)
{
 if(Task.bCompleted) return TEXT("운반 완료");
 if(Task.bCancelled) return TEXT("취소됨");
 if(Task.bDeferred) return TEXT("운반 보류 · 다른 작업 처리");
 if(!IsValid(Task.Vehicle.Get())) return TEXT("담당 차량 확인 필요");
 if(Task.Vehicle->ActiveJob.JobId==Task.JobId) return VehicleState(Task.Vehicle.Get());
 if(Task.Vehicle->PendingJobs.ContainsByPredicate([&Task](const FWarehouseWorkOrder& Job){return Job.JobId==Task.JobId;})) return TEXT("운반 순서 대기");
 return TEXT("작업 상태 확인 필요");
}
FString LocationLabel(const FString& Value)
{
 if(Value.IsEmpty() || Value==TEXT("대상 없음")) return TEXT("위치 확인 필요");
 if(Value.StartsWith(TEXT("("))) return TEXT("지정 하역 위치");
 if(Value.StartsWith(TEXT("WH_")) || Value.StartsWith(TEXT("PalletRack_"))) return TEXT("팔레트 적재 위치");
 const int32 Coordinates=Value.Find(TEXT(" ("));
 return Coordinates!=INDEX_NONE && Value.Contains(TEXT("cm)")) ? Value.Left(Coordinates)+TEXT(" 임시 적재 위치") : Value;
}
FString FriendlyMessage(FString Value)
{
 // Diagnostic identifiers stay in the original status/log; the ordinary screen shows their explanation.
 int32 Begin=Value.Find(TEXT("["));
 while(Begin!=INDEX_NONE)
 {
  const int32 End=Value.Find(TEXT("]"),ESearchCase::CaseSensitive,ESearchDir::FromStart,Begin+1);
  if(End==INDEX_NONE) break;
  bool Code=End>Begin+1;
  for(int32 I=Begin+1;I<End;++I) Code &= (Value[I]>=TEXT('A') && Value[I]<=TEXT('Z')) || (Value[I]>=TEXT('0') && Value[I]<=TEXT('9')) || Value[I]==TEXT('_');
  if(Code) Value.RemoveAt(Begin,End-Begin+1);
  Begin=Value.Find(TEXT("["),ESearchCase::CaseSensitive,ESearchDir::FromStart,Code ? Begin : End+1);
 }
 Value.ReplaceInline(TEXT("  "),TEXT(" "));
 return Value.TrimStartAndEnd();
}
TSharedRef<STextBlock> Text(const FString& Value,int32 Size=16,FLinearColor Color=WarehouseUIStyle::Text)
{
 return SNew(STextBlock).Text(FText::FromString(Value)).Font(WarehouseUIStyle::Font(Size)).ColorAndOpacity(Color).AutoWrapText(true);
}
TSharedRef<SWidget> Action(const FString& Title,TFunction<void()> Callback,TFunction<bool()> Enabled=[](){return true;})
{
 return SNew(SButton).IsFocusable(false).ContentPadding(FMargin(16,10)).IsEnabled_Lambda([Enabled](){return Enabled();})
  .OnClicked_Lambda([Callback](){Callback();return FReply::Handled();})[Text(Title)];
}
struct FReadyPallet
{
 TWeakObjectPtr<AWarehousePallet> Pallet;
 FString Product,Location;
 float MassKg=0;
 int32 Cartons=0;
 bool bReserved=false;
};
struct FVisibleTask
{
 FWarehouseComputerTask Task;
 bool bCanCancel=false;
};

class SWarehouseFocusScreen:public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SWarehouseFocusScreen) {}
 SLATE_ARGUMENT(TWeakObjectPtr<AWarehouseComputer>,Computer)
 SLATE_ARGUMENT(TWeakObjectPtr<Amsc_vrPlayerController>,Controller)
 SLATE_END_ARGS()

 void Construct(const FArguments& Args)
 {
  Computer=Args._Computer;Controller=Args._Controller;
  Stage=AWarehousePlayStage::Find(Computer.Get());
  const bool Campaign=IsCampaign();
  auto Body=SNew(SVerticalBox);
  Body->AddSlot().AutoHeight()[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Text(TEXT("지게차 명령"),28,WarehouseUIStyle::Accent)]
   +SHorizontalBox::Slot().AutoWidth().Padding(5,0)[Action(TEXT("플레이어로 돌아가기"),[this](){ReturnToPlayer();})]
   +SHorizontalBox::Slot().AutoWidth().Padding(5,0)[Action(Campaign ? TEXT("설비 배치") : TEXT("창고 지형 · 물체 재배치"),[this](){OpenLayout();})]
   +SHorizontalBox::Slot().AutoWidth().Padding(5,0)[Action(TEXT("닫기 · Esc"),[this](){Close();})]];
  Body->AddSlot().AutoHeight().Padding(0,8,0,5)[Text(Campaign ? TEXT("1. 설비 배치  →  2. 운행 시작  →  3. 입고 목표 달성") : TEXT("1. 입고 / 출고 선택  →  2. 운행 시작  →  3. 운반할 팔레트 선택"),17,WarehouseUIStyle::Accent)];
  Body->AddSlot().AutoHeight().Padding(0,0,0,14)[Text(Campaign ? TEXT("입고 검수와 랩핑은 자동입니다. 직접 운전하려면 화면을 닫고 지게차 가까이에서 바라본 뒤 G를 누르세요.") : TEXT("검수 · 랩핑 · 트럭 상하차는 자동입니다. 직접 운전하려면 화면을 닫고 지게차 가까이에서 바라본 뒤 G를 누르세요."),14,Muted)];
  Body->AddSlot().AutoHeight()[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[Action(TEXT("전체 지게차 운행 시작"),[this](){SetFleet(true);})]
   +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[Action(TEXT("전체 지게차 일시정지"),[this](){SetFleet(false);})]
   +SHorizontalBox::Slot().AutoWidth()[Action(TEXT("전체 작업 중지"),[this](){StopFleet();})]
   +SHorizontalBox::Slot().FillWidth(1).Padding(20,0).VAlign(VAlign_Center)[SNew(STextBlock).Font(WarehouseUIStyle::Font(16)).ColorAndOpacity(WarehouseUIStyle::Accent).AutoWrapText(true)
    .Text_Lambda([this](){return FText::FromString(Counts);})]];
  Body->AddSlot().AutoHeight().Padding(0,12)[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().FillWidth(1.5f).Padding(0,0,12,0)[SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
     +SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)[DirectionTab(true)]
     +SHorizontalBox::Slot().AutoWidth()[DirectionTab(false)]]
    +SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock).Font(WarehouseUIStyle::Font(15)).ColorAndOpacity(Muted).AutoWrapText(true)
     .Text_Lambda([this](){return FText::FromString(IsCampaign() ? TEXT("이 스테이지는 입고 운반이 목표입니다.") : bInbound ? TEXT("입고: 검수·랩핑된 팔레트를 창고 랙으로 운반합니다.") : TEXT("출고: 창고에 보관한 팔레트를 출고 구역으로 운반합니다."));})]
    +SVerticalBox::Slot().AutoHeight()[Text(TEXT("일시정지: 작업을 잠깐 멈춤\n중지: 맡은 작업을 취소하고 차량을 정지"),13,Muted)]]
   +SHorizontalBox::Slot().FillWidth(1)[SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[Text(TEXT("차량 관리 · 휠로 목록 이동"),17,WarehouseUIStyle::Accent)]
    +SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)[SNew(SBox).HeightOverride(150)
     [SAssignNew(FleetScroll,SScrollBox).ConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible)
      +SScrollBox::Slot()[SAssignNew(FleetBox,SVerticalBox)]]]]];
  auto WorkPanel=SNew(SVerticalBox);
  if(Campaign)
  {
   WorkPanel->AddSlot().AutoHeight()[Text(TEXT("스테이지 목표"),21,WarehouseUIStyle::Accent)];
   WorkPanel->AddSlot().AutoHeight().Padding(0,12)[Text(AWarehousePlayStage::StageTitle(Stage->StageIndex),19)];
   WorkPanel->AddSlot().AutoHeight().Padding(0,8)[SNew(STextBlock).Font(WarehouseUIStyle::Font(26,true)).ColorAndOpacity(WarehouseUIStyle::Accent).AutoWrapText(true)
    .Text_Lambda([this](){return FText::FromString(Stage.IsValid() ? FString::Printf(TEXT("랙 보관 완료  %d / %d 팔레트"),Stage->Delivered,Stage->DeliveryGoal) : FString());})];
   WorkPanel->AddSlot().AutoHeight().Padding(0,8)[SNew(STextBlock).Font(WarehouseUIStyle::Font(18)).AutoWrapText(true)
    .Text_Lambda([this](){return FText::FromString(Stage.IsValid() ? FString::Printf(TEXT("사용 가능한 예산  %s원"),*FText::AsNumber(Stage->BudgetWon).ToString()) : FString());})];
   WorkPanel->AddSlot().AutoHeight().Padding(0,8)[Text(TEXT("들어온 팔레트를 지게차가 랙에 보관하면 목표가 올라갑니다."),15,Muted)];
  }
  else
  {
   WorkPanel->AddSlot().AutoHeight()[SNew(STextBlock).Font(WarehouseUIStyle::Font(21)).ColorAndOpacity(WarehouseUIStyle::Accent)
    .Text_Lambda([this](){return FText::FromString(bInbound ? TEXT("입고할 팔레트") : TEXT("출고할 팔레트"));})];
   WorkPanel->AddSlot().AutoHeight().Padding(0,6,0,10)[Text(TEXT("검수·랩핑이 끝난 팔레트 전체를 운반합니다. 작업을 맡을 차량은 자동으로 배정합니다."),14,Muted)];
  }
  WorkPanel->AddSlot().FillHeight(1)[SAssignNew(PalletList,SListView<TSharedPtr<FReadyPallet>>).ListItemsSource(&Pallets).SelectionMode(ESelectionMode::None).OnGenerateRow(this,&SWarehouseFocusScreen::PalletRow)];
  WorkPanel->AddSlot().AutoHeight()[SNew(STextBlock).Font(WarehouseUIStyle::Font(15)).ColorAndOpacity(Muted).AutoWrapText(true)
   .Text_Lambda([this](){return FText::FromString(Pallets.IsEmpty() ? (bInbound ? TEXT("입고 대기 구역에 검수·랩핑을 마친 팔레트가 준비되면 표시됩니다.") : TEXT("출고할 수 있는 보관 팔레트가 없습니다.")) : FString());})];
  Body->AddSlot().FillHeight(1)[SNew(SHorizontalBox)
   +SHorizontalBox::Slot().FillWidth(1.5f).Padding(0,0,12,0)[SNew(SBorder).Padding(16).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Surface)
    [WorkPanel]]
   +SHorizontalBox::Slot().FillWidth(1)[SNew(SBorder).Padding(16).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(Surface)
    [SNew(SVerticalBox)
     +SVerticalBox::Slot().AutoHeight()[Text(TEXT("창고 자동 운영"),21,WarehouseUIStyle::Accent)]
     +SVerticalBox::Slot().AutoHeight().Padding(0,10)[SNew(STextBlock).Font(WarehouseUIStyle::Font(16)).AutoWrapText(true).Text_Lambda([this](){return FText::FromString(Summary);})]
     +SVerticalBox::Slot().AutoHeight().Padding(0,12,0,8)[Text(TEXT("최근 운반 작업"),21,WarehouseUIStyle::Accent)]
     +SVerticalBox::Slot().FillHeight(1)[SAssignNew(TaskList,SListView<TSharedPtr<FVisibleTask>>).ListItemsSource(&TaskItems).SelectionMode(ESelectionMode::None).OnGenerateRow(this,&SWarehouseFocusScreen::TaskRow)]
     +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(WarehouseUIStyle::Font(15)).AutoWrapText(true)
      .Text_Lambda([this](){return FText::FromString(TaskItems.IsEmpty() ? TEXT("아직 운반 작업이 없습니다.") : FString());})]]]];
  Body->AddSlot().AutoHeight().Padding(0,12,0,0)[SNew(STextBlock).Font(WarehouseUIStyle::Font(17)).ColorAndOpacity(WarehouseUIStyle::Warning).AutoWrapText(true)
   .Text_Lambda([this](){return FText::FromString(Notice);})];
  Body->AddSlot().AutoHeight().Padding(0,5)[SNew(STextBlock).Font(WarehouseUIStyle::Font(15)).ColorAndOpacity(FLinearColor(1,.4f,.35f)).AutoWrapText(true)
   .Text_Lambda([this](){return FText::FromString(Controller.IsValid() ? FriendlyMessage(Controller->EmergencyAlertText+TEXT("\n")+Controller->ResourceAlertText) : FString());})];
  ChildSlot[SNew(SBorder).Padding(24).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.012f,.022f,.035f,1))[Body]];
  Notice=Campaign ? TEXT("설비 배치를 마쳤다면 ‘전체 지게차 운행 시작’을 누르세요. 정해진 입고 물량을 자동으로 운반합니다.") : TEXT("처음에는 ‘전체 지게차 운행 시작’을 누르세요. 입고 운반과 나머지 창고 업무가 자동으로 시작됩니다.");
  Refresh();
 }

 virtual bool SupportsKeyboardFocus() const override {return true;}
 virtual FReply OnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override
 {
  if(Event.GetKey()==EKeys::Escape || Event.GetKey()==EKeys::F1) {Close();return FReply::Handled();}
  return SCompoundWidget::OnPreviewKeyDown(Geometry,Event);
 }
 virtual void Tick(const FGeometry& Geometry,double Now,float DeltaSeconds) override
 {
  SCompoundWidget::Tick(Geometry,Now,DeltaSeconds);
  if(!Computer.IsValid()) {Close();return;}
  if(LastRefresh!=Computer->RefreshCount) Refresh();
 }
#if WITH_EDITOR && !UE_BUILD_SHIPPING
 void Test(const TArray<FString>& Args)
 {
  const FString Command=Args.IsEmpty() ? TEXT("snapshot") : Args[0];
  const int32 Index=Args.Num()>1 ? FCString::Atoi(*Args[1]) : 0;
  if(Command==TEXT("spawn_test_fleet") && Computer.IsValid())
  {
   for(int32 I=0;I<FMath::Clamp(Index,0,8);++I)
   {
    const FTransform Pose(FVector(-50000,I*300,1000));
    auto* Vehicle=Computer->GetWorld()->SpawnActorDeferred<AWarehouseForklift>(AWarehouseForklift::StaticClass(),Pose);
    if(!Vehicle) continue;
    Vehicle->bAutoStart=false;Vehicle->bIdleParkingEnabled=false;
    Vehicle->VehicleName=FString::Printf(TEXT("UI 확인 차량 %02d"),I+3);
    Vehicle->Tags.Add(TEXT("FocusUITestVehicle"));Vehicle->FinishSpawning(Pose);
    Vehicle->SetActorTickEnabled(false);Vehicle->SetActorEnableCollision(false);Vehicle->SetActorHiddenInGame(true);
    TArray<UPrimitiveComponent*> Parts;Vehicle->GetComponents(Parts);
    for(auto* Part:Parts) Part->SetSimulatePhysics(false);
   }
   Computer->RefreshState();
  }
  if(Command==TEXT("clear_test_fleet") && Computer.IsValid())
  {
   TArray<AActor*> Remove;for(TActorIterator<AWarehouseForklift> It(Computer->GetWorld());It;++It)
    if(It->ActorHasTag(TEXT("FocusUITestVehicle"))) Remove.Add(*It);
   for(auto* Actor:Remove) Actor->Destroy();
   Computer->RefreshState();
  }
  if(Command==TEXT("start") || Command==TEXT("stop")) SetFleet(Command==TEXT("start"));
  if(Command==TEXT("stop_work")) StopFleet();
  if(Command==TEXT("tab")) SelectInbound(Args.Num()<2 || Args[1]!=TEXT("outbound"));
  if(Command==TEXT("inbound") && Pallets.IsValidIndex(Index)) RequestTransport(Pallets[Index]);
  if(Command==TEXT("outbound") && Pallets.IsValidIndex(Index)) RequestOutbound(Pallets[Index]);
  if(Command==TEXT("vehicle") && DisplayedFleet.IsValidIndex(Index)) SetVehicle(DisplayedFleet[Index],Args.Num()>2 && Args[2]==TEXT("start"));
  if(Command==TEXT("vehicle_stop_work") && DisplayedFleet.IsValidIndex(Index)) StopVehicle(DisplayedFleet[Index]);
  if(Command==TEXT("fleet_end") && FleetScroll.IsValid()) FleetScroll->ScrollToEnd();
  if(Command==TEXT("fleet_top") && FleetScroll.IsValid()) FleetScroll->ScrollToStart();
  if(Command==TEXT("cancel") && TaskItems.IsValidIndex(Index)) CancelTask(TaskItems[Index]);
  if(Command==TEXT("player")) ReturnToPlayer();
  if(Command==TEXT("layout")) OpenLayout();
  Refresh();
  const FString Directory=FPaths::ProjectSavedDir()/TEXT("Testing/FocusUI");IFileManager::Get().MakeDirectory(*Directory,true);
  const FString Json=FString::Printf(TEXT("{\"focus_screen\":true,\"fleet\":%d,\"pallets\":%d,\"tasks\":%d,\"manual_operations\":%s,\"stage\":%d,\"outbound_visible\":%s,\"inbound_selected\":%s,\"fleet_scroll_offset\":%.2f}"),DisplayedFleet.Num(),Pallets.Num(),TaskItems.Num(),Director.IsValid() && Director->IsManualOperations() ? TEXT("true") : TEXT("false"),IsCampaign() ? Stage->StageIndex : 0,IsCampaign() ? TEXT("false") : TEXT("true"),bInbound ? TEXT("true") : TEXT("false"),FleetScroll.IsValid() ? FleetScroll->GetScrollOffset() : 0.f);
  FFileHelper::SaveStringToFile(Json,*(Directory/TEXT("State.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 }
#endif

private:
 TWeakObjectPtr<AWarehouseComputer> Computer;
 TWeakObjectPtr<Amsc_vrPlayerController> Controller;
 TWeakObjectPtr<AWarehouseFocusDirector> Director;
 TWeakObjectPtr<AWarehousePlayStage> Stage;
 TSharedPtr<SVerticalBox> FleetBox;
 TSharedPtr<SScrollBox> FleetScroll;
 TArray<TWeakObjectPtr<AWarehouseForklift>> DisplayedFleet;
 TArray<TSharedPtr<FReadyPallet>> Pallets;
 TSharedPtr<SListView<TSharedPtr<FReadyPallet>>> PalletList;
 TArray<TSharedPtr<FVisibleTask>> TaskItems;
 TSharedPtr<SListView<TSharedPtr<FVisibleTask>>> TaskList;
 int32 LastRefresh=-1;
 bool bInbound=true;
 FString Counts,Summary,Notice;

 bool IsCampaign() const {return Stage.IsValid() && Stage->StageIndex>0;}
 TSharedRef<SWidget> DirectionTab(bool Inbound)
 {
  return SNew(SButton).IsFocusable(false).ContentPadding(FMargin(24,10))
   .IsEnabled_Lambda([this,Inbound](){return Inbound || !IsCampaign();})
   .ButtonColorAndOpacity_Lambda([this,Inbound](){return bInbound==Inbound ? FLinearColor(.1f,.65f,.7f) : FLinearColor(.3f,.35f,.4f);})
   .OnClicked_Lambda([this,Inbound](){SelectInbound(Inbound);return FReply::Handled();})
   [Text(Inbound ? TEXT("입고") : TEXT("출고"),18)];
 }
 void Close() {if(Controller.IsValid()) Controller->CloseWarehouseComputer();}
 void ReturnToPlayer() {Close();if(Controller.IsValid() && Controller->IsObserverView()) Controller->ToggleObserverView();}
 void OpenLayout() {Close();if(Controller.IsValid()) Controller->SetLayoutEditing(true);}
 void SetFleet(bool Running)
 {
  if(Director.IsValid()) {Director->SetFleetRunning(Running);Notice=FriendlyMessage(Director->LastMessage);}
  else Notice=TEXT("창고 자동 운영 시스템을 찾을 수 없습니다.");
 }
 void SetVehicle(TWeakObjectPtr<AWarehouseForklift> Vehicle,bool Running)
 {
  if(Computer.IsValid() && Vehicle.IsValid()) {Computer->SetVehicleRunning(Vehicle.Get(),Running);Notice=FriendlyMessage(Computer->LastMessage);}
 }
 void StopFleet()
 {
  if(Director.IsValid()) {Director->StopFleet();Notice=FriendlyMessage(Director->LastMessage);}
 }
 void StopVehicle(TWeakObjectPtr<AWarehouseForklift> Vehicle)
 {
  if(Director.IsValid() && Vehicle.IsValid()) {Director->StopVehicle(Vehicle.Get());Notice=FriendlyMessage(Director->LastMessage);}
 }
 void SelectInbound(bool Inbound)
 {
  if(!Inbound && IsCampaign()) {Notice=TEXT("이 스테이지에서는 입고 운반만 지시할 수 있습니다.");return;}
  bInbound=Inbound;Refresh();
 }
 void RequestTransport(const TSharedPtr<FReadyPallet>& Item)
 {
  if(!bInbound) {RequestOutbound(Item);return;}
  if(Director.IsValid() && Item->Pallet.IsValid())
  {
   const bool Accepted=Director->RequestInbound(Item->Pallet.Get());Notice=FriendlyMessage(Director->LastMessage);
   if(Accepted) Item->bReserved=true;
  }
 }
 void RequestOutbound(const TSharedPtr<FReadyPallet>& Item)
 {
  if(IsCampaign()) {Notice=TEXT("이 스테이지는 정해진 입고 물량을 랙에 보관하는 것이 목표입니다.");return;}
  if(Director.IsValid() && Item->Pallet.IsValid())
  {
   const bool Accepted=Director->RequestOutbound(Item->Pallet.Get());Notice=FriendlyMessage(Director->LastMessage);
   if(Accepted) Item->bReserved=true;
  }
 }
 void CancelTask(const TSharedPtr<FVisibleTask>& Item)
 {
  if(Computer.IsValid() && Item->bCanCancel) {Computer->CancelQueuedJob(Item->Task.JobId);Notice=FriendlyMessage(Computer->LastMessage);}
 }
 TSharedRef<ITableRow> TaskRow(TSharedPtr<FVisibleTask> Item,const TSharedRef<STableViewBase>& Owner)
 {
  return SNew(STableRow<TSharedPtr<FVisibleTask>>,Owner).Padding(FMargin(0,5))
   [SNew(SVerticalBox)
    +SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(WarehouseUIStyle::Font(15)).AutoWrapText(true)
     .Text_Lambda([Item](){const auto& Task=Item->Task;const FString Time=Task.bSubmissionTimeKnown ? FString::Printf(TEXT("%.0f초"),Task.ElapsedSimulationSeconds) : TEXT("시간 미기록");const FString State=Task.bDeferred ? FriendlyMessage(Task.State) : TaskState(Task);return FText::FromString(FString::Printf(TEXT("%s → %s\n%s · %s · %s"),*LocationLabel(Task.Source),*LocationLabel(Task.Destination),IsValid(Task.Vehicle.Get()) ? *Task.Vehicle->VehicleName : TEXT("배정 대기"),*State,*Time));})]
    +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0,4)[SNew(SBox).Visibility_Lambda([Item](){return Item->bCanCancel ? EVisibility::Visible : EVisibility::Collapsed;})
     [Action(TEXT("대기 작업 취소"),[this,Item](){CancelTask(Item);},[Item](){return Item->bCanCancel;})]]];
 }
 TSharedRef<ITableRow> PalletRow(TSharedPtr<FReadyPallet> Item,const TSharedRef<STableViewBase>& Owner)
 {
  return SNew(STableRow<TSharedPtr<FReadyPallet>>,Owner).Padding(FMargin(0,4))
   [SNew(SHorizontalBox)
    +SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,5,10,5)[SNew(STextBlock).Font(WarehouseUIStyle::Font(16)).AutoWrapText(true)
     .Text_Lambda([Item](){return FText::FromString(FString::Printf(TEXT("%s\n%s · %d상자 · %.0f kg%s"),*Item->Product,*LocationLabel(Item->Location),Item->Cartons,Item->MassKg,Item->bReserved ? TEXT(" · 운반 예약됨") : TEXT("")));})]
    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(100)[SNew(SButton).IsFocusable(false).ContentPadding(FMargin(8,10))
     .IsEnabled_Lambda([Item](){return Item->Pallet.IsValid() && !Item->bReserved;})
     .OnClicked_Lambda([this,Item](){RequestTransport(Item);return FReply::Handled();})
     [SNew(STextBlock).Font(WarehouseUIStyle::Font(16)).Text_Lambda([this](){return FText::FromString(bInbound ? TEXT("입고 운반") : TEXT("출고 운반"));})]]]];
 }
 void Refresh()
 {
  if(!Computer.IsValid()) return;
  LastRefresh=Computer->RefreshCount;Director=AWarehouseFocusDirector::Find(Computer.Get());
  Summary=Director.IsValid() ? FriendlyMessage(Director->GetSummary()) : TEXT("자동 운영 시스템 준비 중");
  TMap<TWeakObjectPtr<AWarehousePallet>,TSharedPtr<FReadyPallet>> Previous;
  for(const auto& Item:Pallets) Previous.Add(Item->Pallet,Item);
  Pallets.Reset();TMap<AWarehousePallet*,TSharedPtr<FReadyPallet>> Current;
  for(const auto& Cargo:Computer->Inventory)
  {
   auto* Pallet=Cargo.Pallet.Get();
   if(!IsValid(Pallet) || Pallet->ReceiptState!=EWarehouseReceiptState::Confirmed || Pallet->ReceivingState!=EWarehouseReceivingState::Accepted || Pallet->bWrapping) continue;
   const bool AtInbound=Director.IsValid() && Director->IsInboundCandidate(Pallet);
   if(bInbound!=AtInbound) continue;
   auto* Existing=Current.Find(Pallet);
   if(!Existing)
   {
    auto Item=Previous.FindRef(TWeakObjectPtr<AWarehousePallet>(Pallet));if(!Item.IsValid()) Item=MakeShared<FReadyPallet>();
    Item->Pallet=Pallet;Item->Product=Cargo.Product;Item->Location=Cargo.Location;Item->Cartons=0;Item->MassKg=0;
    Item->bReserved=Director.IsValid() && (bInbound ? Director->IsInboundPending(Pallet) : Director->IsOutboundPending(Pallet));
    Pallets.Add(Item);Current.Add(Pallet,Item);Existing=Current.Find(Pallet);
   }
   (*Existing)->Cartons++;(*Existing)->MassKg+=Cargo.MassKg;(*Existing)->bReserved|=Cargo.bReserved;
  }
  if(PalletList.IsValid()) PalletList->RequestListRefresh();
  TArray<TWeakObjectPtr<AWarehouseForklift>> Fleet;
  int32 Running=0,Completed=0,Working=0;
  for(AWarehouseForklift* Vehicle:Computer->Fleet) if(IsValid(Vehicle)) {Fleet.Add(Vehicle);if(Vehicle->bPowered) ++Running;}
  Fleet.Sort([](const auto& A,const auto& B){return A.IsValid() && B.IsValid() ? A->VehicleName<B->VehicleName : A.IsValid();});
  if(Fleet!=DisplayedFleet)
  {
   DisplayedFleet=Fleet;FleetBox->ClearChildren();
   for(const auto Vehicle:Fleet)
   {
    FleetBox->AddSlot().AutoHeight().Padding(0,3)[SNew(SVerticalBox)
     +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)[SNew(STextBlock).Font(WarehouseUIStyle::Font(15)).AutoWrapText(true)
      .Text_Lambda([Vehicle](){return FText::FromString(Vehicle.IsValid() ? FString::Printf(TEXT("%s · 배터리 %.0f%% · %s"),*Vehicle->VehicleName,Vehicle->BatteryPercent,*VehicleState(Vehicle.Get())) : FString());})]
     +SVerticalBox::Slot().AutoHeight()[SNew(SHorizontalBox)
      +SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)[Action(TEXT("재개"),[this,Vehicle](){SetVehicle(Vehicle,true);},[Vehicle](){return Vehicle.IsValid() && !Vehicle->bPowered && !Vehicle->IsRemoteControlled();})]
      +SHorizontalBox::Slot().AutoWidth().Padding(0,0,4,0)[Action(TEXT("일시정지"),[this,Vehicle](){SetVehicle(Vehicle,false);},[Vehicle](){return Vehicle.IsValid() && Vehicle->bPowered && !Vehicle->IsRemoteControlled();})]
      +SHorizontalBox::Slot().AutoWidth()[Action(TEXT("중지"),[this,Vehicle](){StopVehicle(Vehicle);},[Vehicle](){return Vehicle.IsValid() && !Vehicle->IsRemoteControlled();})]]];
   }
   if(Fleet.IsEmpty()) FleetBox->AddSlot().AutoHeight()[Text(TEXT("지게차가 없습니다. 재배치에서 지게차와 충전 / 대기 자리를 추가하세요."),16,WarehouseUIStyle::Warning)];
  }
  TMap<FString,TSharedPtr<FVisibleTask>> PreviousTasks;
  for(const auto& Item:TaskItems) PreviousTasks.Add(Item->Task.JobId,Item);
  TaskItems.Reset();
  for(const auto& Task:Computer->Tasks) {if(Task.bCompleted) ++Completed;else if(!Task.bCancelled && !Task.bDeferred) ++Working;}
  const auto* Shipping=AWarehouseShippingSystem::Find(Computer.Get());
  for(int32 I=Computer->Tasks.Num()-1;I>=FMath::Max(0,Computer->Tasks.Num()-8);--I)
  {
   const auto& Task=Computer->Tasks[I];
   auto Item=PreviousTasks.FindRef(Task.JobId);if(!Item.IsValid()) Item=MakeShared<FVisibleTask>();
   // Shipment-owned queues are retried by the shipping system; cancelling only that AGV job would be misleading.
   const bool ShipmentOwnsTask=Shipping && IsValid(Task.Pallet.Get()) && !Shipping->GetPalletShipment(Task.Pallet.Get()).IsEmpty();
   Item->Task=Task;Item->bCanCancel=!ShipmentOwnsTask && IsValid(Task.Vehicle.Get()) && !Task.bCompleted && !Task.bCancelled && Task.Vehicle->PendingJobs.ContainsByPredicate([&Task](const FWarehouseWorkOrder& Job){return Job.JobId==Task.JobId && Job.SourceSystem==TEXT("FMS-COMPUTER");});
   TaskItems.Add(Item);
  }
  TaskList->RequestListRefresh();
  Counts=FString::Printf(TEXT("운행 %d / %d대   ·   진행 %d건   ·   완료 %d건"),Running,Fleet.Num(),Working,Completed);
 }
};
#if WITH_EDITOR && !UE_BUILD_SHIPPING
TWeakPtr<SWarehouseFocusScreen> TestScreen;
FAutoConsoleCommand FocusTestCommand(TEXT("Warehouse.Focus.Test"),TEXT("Editor-only focus screen: snapshot/start/stop/stop_work/tab inbound|outbound/inbound|outbound <index>/vehicle <index> start|stop/vehicle_stop_work <index>/fleet_end/fleet_top/cancel <index>/player/layout."),
 FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args){if(auto Screen=TestScreen.Pin()) Screen->Test(Args);}));
#endif
}

TSharedRef<SWidget> MakeWarehouseFocusScreen(AWarehouseComputer* Computer,Amsc_vrPlayerController* Controller)
{
 auto Screen=SNew(WarehouseFocusUI::SWarehouseFocusScreen).Computer(Computer).Controller(Controller);
#if WITH_EDITOR && !UE_BUILD_SHIPPING
 WarehouseFocusUI::TestScreen=Screen;
#endif
 return Screen;
}
