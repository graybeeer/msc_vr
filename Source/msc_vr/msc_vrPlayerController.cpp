// Copyright Epic Games, Inc. All Rights Reserved.


#include "msc_vrPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "msc_vrCameraManager.h"
#include "msc_vrCharacter.h"
#include "Blueprint/UserWidget.h"
#include "msc_vr.h"
#include "Widgets/Input/SVirtualJoystick.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehouseForklift.h"
#include "WarehousePalletLift.h"
#include "WarehouseComputer.h"
#include "WarehouseUIStyle.h"
#include "WarehouseLayoutEditor.h"
#include "WarehouseLayoutInventory.h"
#include "WarehouseSimulationClock.h"
#include "WarehouseTransportVisualizer.h"
#include "WarehouseObserverWallFade.h"
#include "Misc/App.h"
#include "HAL/PlatformTime.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Kismet/GameplayStatics.h"

namespace
{
// Slate keeps the actual down/move/up positions even if a short mouse gesture
// falls between paused world ticks. The toolbar remains above this canvas.
class SWarehouseLayoutCanvas:public SLeafWidget
{
public:
 SLATE_BEGIN_ARGS(SWarehouseLayoutCanvas) {}
  SLATE_ARGUMENT(AWarehouseLayoutEditor*,Layout)
  SLATE_ARGUMENT(Amsc_vrPlayerController*,Controller)
  SLATE_EVENT(FSimpleDelegate,OnEnded)
 SLATE_END_ARGS()
 void Construct(const FArguments& Args) { Layout=Args._Layout;Controller=Args._Controller;OnEnded=Args._OnEnded; }
 virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
 FVector2D ViewportPoint(const FGeometry& Geometry,const FPointerEvent& Event) const
 {
  int32 Width=0,Height=0;if (Controller.IsValid()) Controller->GetViewportSize(Width,Height);
  const FVector2D Size=Geometry.GetLocalSize(),Local=Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
  return Local*FVector2D(Width/FMath::Max(1.,Size.X),Height/FMath::Max(1.,Size.Y));
 }
 virtual FReply OnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override
 {
  if (Event.GetEffectingButton()==EKeys::RightMouseButton && Layout.IsValid() && Controller.IsValid())
  {
   if (Layout->bDragging || Layout->bMarqueeSelecting) Layout->PointerUp(false);
   return Controller->OpenLayoutRotationMenu(Event.GetScreenSpacePosition()) ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
  }
  if (Event.GetEffectingButton()!=EKeys::LeftMouseButton || !Layout.IsValid() || !Controller.IsValid()) return FReply::Unhandled();
  Layout->PointerDown(Controller.Get(),ViewportPoint(Geometry,Event),Event.IsControlDown());
  return FReply::Handled().CaptureMouse(SharedThis(this));
 }
 virtual FReply OnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event) override
 {
  if (!HasMouseCapture() || !Layout.IsValid() || !Controller.IsValid()) return FReply::Unhandled();
  Layout->PointerMove(Controller.Get(),ViewportPoint(Geometry,Event));return FReply::Handled();
 }
 virtual FReply OnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event) override
 {
  if (Event.GetEffectingButton()!=EKeys::LeftMouseButton || !HasMouseCapture()) return FReply::Unhandled();
  if (Layout.IsValid() && Controller.IsValid()) { Layout->PointerMove(Controller.Get(),ViewportPoint(Geometry,Event));Layout->PointerUp(); }
  OnEnded.ExecuteIfBound();return FReply::Handled().ReleaseMouseCapture();
 }
 virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override
 { if (Layout.IsValid()) Layout->PointerUp(false); }
 virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle&,bool) const override
 {
  if (!Layout.IsValid() || !Controller.IsValid()) return Layer;
  int32 Width=0,Height=0;Controller->GetViewportSize(Width,Height);if (Width<=0 || Height<=0) return Layer;
  const FVector2D Factor=Geometry.GetLocalSize()/FVector2D(Width,Height);
  auto Outline=[&](FVector2D A,FVector2D B,FLinearColor Color)
  {
   A*=Factor;B*=Factor;
   TArray<FVector2D> Points{A,FVector2D(B.X,A.Y),B,FVector2D(A.X,B.Y),A};
   FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,2);
  };
  for (AActor* Selected:Layout->GetSelection())
  {
   const FBox Bounds=Layout->GetSelectionBounds(Selected);if (!Bounds.IsValid) continue;
   FBox2D Screen(ForceInit);
   for (int32 Corner=0;Corner<8;++Corner)
   {
    FVector2D Point;const FVector World(Corner&1 ? Bounds.Max.X:Bounds.Min.X,Corner&2 ? Bounds.Max.Y:Bounds.Min.Y,Corner&4 ? Bounds.Max.Z:Bounds.Min.Z);
    if (Controller->ProjectWorldLocationToScreen(World,Point)) Screen+=Point;
   }
   if (Screen.bIsValid) Outline(Screen.Min,Screen.Max,WarehouseUIStyle::Accent);
  }
  if (Layout->bMarqueeSelecting) Outline(Layout->MarqueeStart,Layout->MarqueeEnd,FLinearColor(.3f,.8f,1.f,1.f));
  return Layer+1;
 }
private:
 TWeakObjectPtr<AWarehouseLayoutEditor> Layout;
 TWeakObjectPtr<Amsc_vrPlayerController> Controller;
 FSimpleDelegate OnEnded;
};
}

Amsc_vrPlayerController::Amsc_vrPlayerController()
{
	// set the player camera manager class
	PlayerCameraManagerClass = Amsc_vrCameraManager::StaticClass();
	PrimaryActorTick.bTickEvenWhenPaused=true;
	bShouldPerformFullTickWhenPaused=true;
	TransportVisualizer=CreateDefaultSubobject<UWarehouseTransportVisualizer>(TEXT("TransportVisualizer"));
	ObserverWallFade=CreateDefaultSubobject<UWarehouseObserverWallFade>(TEXT("ObserverWallFade"));
}

void Amsc_vrPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalPlayerController()) { LayoutEditor=AWarehouseLayoutEditor::Find(this);SimulationClock=AWarehouseSimulationClock::Find(this); }
	if (IsLocalPlayerController() && GetWorld()->GetGameViewport())
	{
		auto RemoteActive=[this]()
		{
			const auto* OperatorCharacter=Cast<Amsc_vrCharacter>(GetPawn());
			return OperatorCharacter && OperatorCharacter->GetRemoteForklift()!=nullptr;
		};
		CargoReadoutWidget=SAssignNew(CargoReadoutBox,SBox).HAlign(HAlign_Center).VAlign(VAlign_Bottom)
			.Padding_Lambda([RemoteActive]() { return FMargin(0,0,0,RemoteActive() ? 24 : 48); })
			.Visibility(EVisibility::HitTestInvisible)
			[SNew(SBorder).Padding_Lambda([RemoteActive]() { return RemoteActive() ? FMargin(0) : WarehouseUIStyle::PanelPadding; })
				.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor_Lambda([RemoteActive]() { return RemoteActive() ? FLinearColor::Transparent : WarehouseUIStyle::Panel; })
				.Visibility_Lambda([this]() { return GetCargoReadout().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				[SNew(STextBlock).Font_Lambda([RemoteActive]() { return WarehouseUIStyle::Font(RemoteActive() ? WarehouseUIStyle::CompactSize : WarehouseUIStyle::ReadoutSize); })
					.ColorAndOpacity(WarehouseUIStyle::Text).WrapTextAt_Lambda([RemoteActive]() { return RemoteActive() ? 1000.f : 680.f; })
					.Justification_Lambda([RemoteActive]() { return RemoteActive() ? ETextJustify::Center : ETextJustify::Left; })
					.ShadowOffset(FVector2D(1,1)).ShadowColorAndOpacity(FLinearColor::Black)
					.Text_Lambda([this]() { return GetCargoReadout(); })]];
		GetWorld()->GetGameViewport()->AddViewportWidgetContent(CargoReadoutWidget.ToSharedRef(),5);
		EmergencyAlertWidget=SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(24,24))
			.Visibility(EVisibility::HitTestInvisible)
			[SNew(SBox).WidthOverride(820)
				[SNew(SBorder).Padding(WarehouseUIStyle::PanelPadding).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(WarehouseUIStyle::DangerPanel)
				.Visibility_Lambda([this]() { return EmergencyAlertText.IsEmpty() && ResourceAlertText.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
					[SNew(SVerticalBox)
						+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::TitleSize,true))
						.ColorAndOpacity(WarehouseUIStyle::Warning).Text_Lambda([this]() { return FText::FromString(EmergencyAlertText.IsEmpty() ? TEXT("운영 · 필요한 설비 확인") : TEXT("비상 · 지게차 작업 중단")); })]
						+SVerticalBox::Slot().AutoHeight().Padding(0,8)[SNew(STextBlock).Font(WarehouseUIStyle::Font())
						.ColorAndOpacity(WarehouseUIStyle::Text).WrapTextAt(780).Text_Lambda([this]() { return FText::FromString(EmergencyAlertText.IsEmpty() ? ResourceAlertText : EmergencyAlertText+TEXT("\n")+ResourceAlertText); })]
						+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(WarehouseUIStyle::Font())
						.ColorAndOpacity(WarehouseUIStyle::Text).WrapTextAt(780).Text_Lambda([this]() { return FText::FromString(EmergencyAlertText.IsEmpty() ? TEXT("중앙 컴퓨터에서 작업을 확인하거나 배치 모드에서 필요한 설비를 추가하세요.") : TEXT("주변 장애물을 제거하거나 G로 수동 탈출하세요. 해결 후 E로 재점검·재개합니다.")); })]]]];
		GetWorld()->GetGameViewport()->AddViewportWidgetContent(EmergencyAlertWidget.ToSharedRef(),200);
		auto Tools=SNew(SVerticalBox);
		LayoutCanvas=SNew(SWarehouseLayoutCanvas).Layout(LayoutEditor).Controller(this)
			.OnEnded_Lambda([this]() { SetObserverRoofVisibility(false);SetObserverRoofVisibility(true); })
			.Visibility_Lambda([this]() { return bObserverView && LayoutEditor && LayoutEditor->bEditing && !IsWarehouseMenuOpen() ? EVisibility::Visible:EVisibility::Collapsed; });
		GetWorld()->GetGameViewport()->AddViewportWidgetContent(LayoutCanvas.ToSharedRef(),10);
		auto Actions=SNew(SHorizontalBox);
		const TCHAR* ActionNames[]={TEXT("재배치"),TEXT("삭제"),TEXT("추가")};
		for (int32 I=0;I<3;++I)
			Actions->AddSlot().FillWidth(1).Padding(3)[SNew(SButton).IsFocusable(false).ContentPadding(12)
				.OnClicked_Lambda([this,I]() { SetLayoutAction(I);return FReply::Handled(); })
				[SNew(STextBlock).Font(WarehouseUIStyle::Font()).ColorAndOpacity_Lambda([this,I]() { return LayoutEditor && int32(LayoutEditor->Action)==I ? WarehouseUIStyle::Accent : WarehouseUIStyle::Text; }).Text(FText::FromString(ActionNames[I]))]];
		Tools->AddSlot().AutoHeight()[Actions];
		auto Row=SNew(SWrapBox).UseAllottedSize(true);
		Row->AddSlot().Padding(3)[SNew(SButton).IsFocusable(false).ContentPadding(8)
			.Visibility_Lambda([this]() { return LayoutEditor && LayoutEditor->Action!=EWarehouseLayoutAction::Add ? EVisibility::Visible : EVisibility::Collapsed; })
			.ToolTipText(FText::FromString(TEXT("현재 층의 모든 편집 가능한 종류를 함께 선택합니다.")))
			.OnClicked_Lambda([this]() { SetLayoutCategory(int32(EWarehouseLayoutCategory::All));return FReply::Handled(); })
			[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::LabelSize))
				.ColorAndOpacity_Lambda([this]() { return LayoutEditor && LayoutEditor->Category==EWarehouseLayoutCategory::All ? WarehouseUIStyle::Accent : WarehouseUIStyle::Text; })
				.Text(FText::FromString(TEXT("모두")))]];
		const TCHAR* Labels[]={TEXT("랙"),TEXT("지게차"),TEXT("승강기"),TEXT("입출고 구역"),TEXT("기타 물건"),TEXT("충전기"),TEXT("일반 대기 자리"),TEXT("상자"),TEXT("팔레트")};
		for (int32 I=0;I<UE_ARRAY_COUNT(Labels);++I)
			Row->AddSlot().Padding(3)[SNew(SButton).IsFocusable(false).ContentPadding(8)
				.OnClicked_Lambda([this,I]() { SetLayoutCategory(I);return FReply::Handled(); })
				[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::LabelSize)).ColorAndOpacity_Lambda([this,I]() { return LayoutEditor && int32(LayoutEditor->Category)==I ? WarehouseUIStyle::Accent : WarehouseUIStyle::Text; }).Text(FText::FromString(Labels[I]))]];
		Tools->AddSlot().AutoHeight()[Row];
		auto SelectionRow=SNew(SHorizontalBox);
		SelectionRow->AddSlot().AutoWidth().Padding(3)[SNew(STextBlock).Font(WarehouseUIStyle::Font()).ColorAndOpacity(WarehouseUIStyle::Accent)
			.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("선택 %d개 그룹"),LayoutEditor ? LayoutEditor->SelectionCount:0)); })];
		SelectionRow->AddSlot().AutoWidth().Padding(8,0)[SNew(SButton).IsFocusable(false).ContentPadding(8)
			.OnClicked_Lambda([this]() { if (LayoutEditor) LayoutEditor->ClearSelection();return FReply::Handled(); })
			[SNew(STextBlock).Font(WarehouseUIStyle::Font()).Text(FText::FromString(TEXT("선택 해제")))]];
		SelectionRow->AddSlot().AutoWidth().Padding(3)[SNew(SButton).IsFocusable(false).ContentPadding(8)
			.OnClicked_Lambda([this]() { if (LayoutEditor) LayoutEditor->bEdgeSnapEnabled=!LayoutEditor->bEdgeSnapEnabled;return FReply::Handled(); })
			[SNew(STextBlock).Font(WarehouseUIStyle::Font()).Text_Lambda([this]() { return FText::FromString(LayoutEditor && LayoutEditor->bEdgeSnapEnabled ? TEXT("스냅 켜짐"):TEXT("스냅 꺼짐")); })]];
		SelectionRow->AddSlot().AutoWidth().Padding(12,0)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::LabelSize))
			.ColorAndOpacity(WarehouseUIStyle::Text).Text(FText::FromString(TEXT("선택 후 우클릭 · 좌우 90도 회전 / 삭제")))];
		Tools->AddSlot().AutoHeight()[SelectionRow];
		Tools->AddSlot().AutoHeight()[SNew(SButton).IsFocusable(false).ContentPadding(8)
			.Visibility_Lambda([this]() { return LayoutEditor && LayoutEditor->Action==EWarehouseLayoutAction::Delete ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnClicked_Lambda([this]() { DeleteLayoutSelection();return FReply::Handled(); })
			[SNew(STextBlock).Font(WarehouseUIStyle::Font()).Text(FText::FromString(TEXT("선택한 요소 모두 삭제 · 상자는 보류 목록으로")))]];
		Tools->AddSlot().AutoHeight()[SNew(SButton).IsFocusable(false).ContentPadding(8)
			.Visibility_Lambda([this]() { return LayoutEditor && LayoutEditor->Action==EWarehouseLayoutAction::Add ? EVisibility::Visible : EVisibility::Collapsed; })
			.OnClicked_Lambda([this]() { if (LayoutEditor) LayoutEditor->CycleAddChoice();return FReply::Handled(); })
			[SNew(STextBlock).Font(WarehouseUIStyle::Font()).Text_Lambda([this]() { return FText::FromString(LayoutEditor ? TEXT("추가 대상: ")+LayoutEditor->GetAddChoiceLabel()+TEXT(" · 클릭하면 다음 종류") : FString()); })]];
		Tools->AddSlot().AutoHeight().Padding(3)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::CompactSize)).ColorAndOpacity(WarehouseUIStyle::Text)
			.Text_Lambda([this]() { auto* Store=AWarehouseLayoutInventory::Find(this);return FText::FromString(FString::Printf(TEXT("보류된 물류 %d개 · 상자 데이터 생성·영구 삭제는 중앙 컴퓨터에서"),Store ? Store->HeldCargo.Num() : 0)); })];
		Tools->AddSlot().AutoHeight().Padding(3,8)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::LabelSize)).ColorAndOpacity(WarehouseUIStyle::Text).WrapTextAt(740)
			.Text_Lambda([this]() { return FText::FromString(LayoutEditor ? LayoutEditor->Status : FString()); })];
		Tools->AddSlot().AutoHeight()[SNew(SButton).IsFocusable(false).ContentPadding(8)
			.OnClicked_Lambda([this]() { ToggleLayoutEditing();return FReply::Handled(); })
			[SNew(STextBlock).Font(WarehouseUIStyle::Font()).ColorAndOpacity(WarehouseUIStyle::Text).Text(FText::FromString(TEXT("재배치 종료 · 시뮬레이션 재개")))]
			];
		LayoutToolbar=SNew(SBox).HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(FMargin(20,0,20,20))
			.Visibility_Lambda([this]() { return bObserverView && LayoutEditor && LayoutEditor->bEditing && !IsWarehouseMenuOpen() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
			[SNew(SBorder).Padding(WarehouseUIStyle::PanelPadding).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(WarehouseUIStyle::Panel)[Tools]];
		GetWorld()->GetGameViewport()->AddViewportWidgetContent(LayoutToolbar.ToSharedRef(),20);
	}

	
	// only spawn touch controls on local player controllers
	if (IsLocalPlayerController() && ShouldUseTouchControls())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(Logmsc_vr, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void Amsc_vrPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputComponent->BindKey(EKeys::F1,IE_Pressed,this,&Amsc_vrPlayerController::ToggleWarehouseMenu).bExecuteWhenPaused=true;
	InputComponent->BindKey(EKeys::Escape,IE_Pressed,this,&Amsc_vrPlayerController::ToggleWarehouseMenu).bExecuteWhenPaused=true;

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Context
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
	
}

bool Amsc_vrPlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}

void Amsc_vrPlayerController::UpdateInputMode()
{
	const bool Locked=IsWarehouseMenuOpen() || bObserverView;
	if (Locked!=bWarehouseInputLocked)
	{
		SetIgnoreMoveInput(Locked); SetIgnoreLookInput(Locked);
		bWarehouseInputLocked=Locked;
		if (auto* PlayerCharacter=Cast<ACharacter>(GetPawn())) PlayerCharacter->GetCharacterMovement()->StopMovementImmediately();
	}
	bShowMouseCursor=IsWarehouseMenuOpen() || (LayoutEditor && LayoutEditor->bEditing);
	if (ComputerWidget.IsValid())
	{
		FInputModeUIOnly Mode;Mode.SetWidgetToFocus(ComputerWidget);SetInputMode(Mode);
	}
	else if (MenuWidget.IsValid() || (LayoutEditor && LayoutEditor->bEditing))
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
		if (GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		if (GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->SetMouseCaptureMode(EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
		if (FSlateApplication::IsInitialized()) FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
}

void Amsc_vrPlayerController::CloseWarehouseMenu()
{
	if (MenuWidget.IsValid() && GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(MenuWidget.ToSharedRef());
	MenuWidget.Reset();
	SaveConfig();
	UpdateInputMode();
}

void Amsc_vrPlayerController::ToggleWarehouseMenu()
{
	if (LayoutEditor && LayoutEditor->bDragging) LayoutEditor->EndDrag(false);
	if (ComputerWidget.IsValid()) { CloseWarehouseComputer(); return; }
	if (!IsLocalController() || !GetWorld()->GetGameViewport()) return;
	if (MenuWidget.IsValid()) { CloseWarehouseMenu(); return; }
	if (auto* OperatorCharacter=Cast<Amsc_vrCharacter>(GetPawn())) OperatorCharacter->EndRemoteControl();
	static const FTextBlockStyle ButtonText=FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		.SetFont(WarehouseUIStyle::Font()).SetColorAndOpacity(WarehouseUIStyle::Text);
	auto Panel=SNew(SVerticalBox);
	auto Text=[&](const FString& Label)
	{
		Panel->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Font(WarehouseUIStyle::Font())
			.ColorAndOpacity(WarehouseUIStyle::Text).Text(FText::FromString(Label)).AutoWrapText(true)];
	};
	auto Button=[&](const FString& Label, TFunction<void()> Action)
	{
		Panel->AddSlot().AutoHeight().Padding(8,4)[SNew(SButton).IsFocusable(false).ContentPadding(10).TextStyle(&ButtonText)
			.Text(FText::FromString(Label)).OnClicked_Lambda([Action]() { Action(); return FReply::Handled(); })];
	};
	Panel->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Font(WarehouseUIStyle::Font(WarehouseUIStyle::TitleSize,true))
		.ColorAndOpacity(WarehouseUIStyle::Accent).Text(FText::FromString(TEXT("물류창고 · 설정 / 관찰")))];
	Text(TEXT("F1: 메뉴  |  Shift: 달리기  |  Ctrl: 앉기  |  E: 집기 / 적재\nG: 바라보는 지게차의 후방 3인칭 원격 조작 / 종료\n원격: W/S 전후진 · A/D 조향·자리 회전 · 좌클릭 포크 올림 · 우클릭 내림 · Space 제동\n마우스 이동: 지게차 주변 보기 · 종료하면 플레이어 1인칭 복귀\n메뉴와 관찰 중에도 창고 시뮬레이션은 계속됩니다."));
	Button(bObserverView ? TEXT("1인칭 캐릭터로 돌아가기") : TEXT("전지적 관찰 시점으로 전환"),[this]() { ToggleObserverView(); CloseWarehouseMenu(); });
	Button(TEXT("창고 전체 보기"),[this]() { if (!bObserverView) ToggleObserverView(); SetObserverFloor(-1); CloseWarehouseMenu(); });
    for (int32 Floor=0; Floor<3; ++Floor)
        Button(FString::Printf(TEXT("%d층 관찰 (위층 구조 숨기기)"),Floor+1),[this,Floor]() { SetObserverFloor(Floor); CloseWarehouseMenu(); });
	Button(LayoutEditor && LayoutEditor->bEditing ? TEXT("재배치 종료 · 시뮬레이션 재개") : TEXT("현재 층 배치 편집 · 시뮬레이션 일시정지"),[this]() { ToggleLayoutEditing();CloseWarehouseMenu(); });
	Panel->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Font(WarehouseUIStyle::Font()).ColorAndOpacity(WarehouseUIStyle::Accent)
		.Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("시뮬레이션 배속: %d배 · 실제 처리 %.2f배 · 같은 물리 정확도"),SimulationClock ? SimulationClock->RequestedSpeed : 1,SimulationClock ? SimulationClock->AchievedSpeed : 1.f)); }).AutoWrapText(true)];
	for (int32 Speed:{1,2,4}) Button(FString::Printf(TEXT("시뮬레이션 %d배"),Speed),[this,Speed]() { SetSimulationSpeed(Speed); });
	Text(TEXT("관찰: WASD 평면 이동 · 휠 확대/축소 · 우클릭 드래그 회전\nSpace/Ctrl 축소/확대 · Shift 가속\n지붕은 관찰 중 투명해집니다. F1 메뉴에서 1인칭으로 복귀하세요."));
	Text(TEXT("마우스 감도 (0.2 ~ 3.0)"));
	Panel->AddSlot().AutoHeight().Padding(12)[SNew(SSlider).IsFocusable(false).Value((MouseSensitivity-.2f)/2.8f)
		.OnValueChanged_Lambda([this](float V) { MouseSensitivity=.2f+V*2.8f; })];
	Text(TEXT("시야각 FOV (70 ~ 110)"));
	Panel->AddSlot().AutoHeight().Padding(12)[SNew(SSlider).IsFocusable(false).Value((ViewFOV-70.f)/40.f)
		.OnValueChanged_Lambda([this](float V) { ViewFOV=70.f+V*40.f; })];
	Panel->AddSlot().AutoHeight().Padding(8,4)[SNew(SButton).IsFocusable(false).ContentPadding(10).TextStyle(&ButtonText)
		.Text_Lambda([]() { const int32 Q=GEngine->GetGameUserSettings()->GetOverallScalabilityLevel(); return FText::FromString(FString::Printf(TEXT("그래픽 품질: %s (클릭하여 변경)"),Q<0 ? TEXT("사용자 지정") : *FString::FromInt(Q))); })
		.OnClicked_Lambda([]() { auto* S=GEngine->GetGameUserSettings(); S->SetOverallScalabilityLevel((S->GetOverallScalabilityLevel()+1)%4); S->ApplySettings(false); return FReply::Handled(); })];
	Panel->AddSlot().AutoHeight().Padding(8,4)[SNew(SButton).IsFocusable(false).ContentPadding(10).TextStyle(&ButtonText)
		.Text_Lambda([]() { return FText::FromString(GEngine->GetGameUserSettings()->IsVSyncEnabled() ? TEXT("수직 동기화: 켜짐") : TEXT("수직 동기화: 꺼짐")); })
		.OnClicked_Lambda([]() { auto* S=GEngine->GetGameUserSettings(); S->SetVSyncEnabled(!S->IsVSyncEnabled()); S->ApplySettings(false); return FReply::Handled(); })];
	Button(TEXT("설정 저장 · 메뉴 닫기"),[this]() { CloseWarehouseMenu(); });
	MenuWidget=SNew(SBorder).HAlign(HAlign_Center).VAlign(VAlign_Center)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(0,0,0,.72f))
		[SNew(SBox).WidthOverride(680).MaxDesiredHeight(760)
			[SNew(SBorder).Padding(WarehouseUIStyle::PanelPadding).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(WarehouseUIStyle::Panel)
				[SNew(SScrollBox)+SScrollBox::Slot()[Panel]]]];
	GetWorld()->GetGameViewport()->AddViewportWidgetContent(MenuWidget.ToSharedRef(),100);
	UpdateInputMode();
}

void Amsc_vrPlayerController::ToggleObserverView()
{
	if (ComputerWidget.IsValid()) CloseWarehouseComputer();
	if (!GetPawn()) return;
	if (auto* OperatorCharacter=Cast<Amsc_vrCharacter>(GetPawn())) OperatorCharacter->EndRemoteControl();
	if (!bObserverView)
	{
		if (!IsValid(ObserverCamera)) ObserverCamera=GetWorld()->SpawnActor<ACameraActor>();
		if (!ObserverCamera) return;
		ObserverCamera->GetCameraComponent()->SetFieldOfView(ViewFOV);
		ObserverCamera->GetCameraComponent()->bConstrainAspectRatio=false;
        // An overview needs a neutral, sharp camera, independent of cinematic lens effects.
        auto& PP=ObserverCamera->GetCameraComponent()->PostProcessSettings;
        PP.bOverride_VignetteIntensity=true; PP.VignetteIntensity=0.f;
        PP.bOverride_FilmGrainIntensity=true; PP.FilmGrainIntensity=0.f;
        PP.bOverride_SceneFringeIntensity=true; PP.SceneFringeIntensity=0.f;
        PP.bOverride_MotionBlurAmount=true; PP.MotionBlurAmount=0.f;
        PP.bOverride_DepthOfFieldFstop=true; PP.DepthOfFieldFstop=32.f;
        PP.bOverride_BloomIntensity=true; PP.BloomIntensity=0.f;
        PP.bOverride_AutoExposureBias=true; PP.AutoExposureBias=-2.f;
        ObserverCamera->GetCameraComponent()->PostProcessBlendWeight=1.f;
		bObserverView=true;
		SetObserverRoofVisibility(true);
		FrameWarehouse();
		SetViewTarget(ObserverCamera);
	}
	else
	{
		if (LayoutEditor && LayoutEditor->bEditing) LayoutEditor->SetEditing(false,ObserverFloor);
		bObserverView=false;
		if (ObserverWallFade) ObserverWallFade->Restore();
		SetObserverRoofVisibility(false);
		SetViewTarget(GetPawn());
	}
	if (auto* PlayerCharacter=Cast<Amsc_vrCharacter>(GetPawn())) PlayerCharacter->SetObserverPresentation(bObserverView);
	UpdateInputMode();
}

void Amsc_vrPlayerController::FrameWarehouse()
{
	if (!IsValid(ObserverCamera)) return;
	FBox Bounds(ForceInit);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (It->ActorHasTag(TEXT("ObserverArea"))) Bounds+=It->GetComponentsBoundingBox(true);
	if (!Bounds.IsValid)
		for (TActorIterator<AWarehousePallet> It(GetWorld()); It; ++It) Bounds+=It->GetActorLocation();
	if (!Bounds.IsValid) Bounds=FBox(GetPawn()->GetActorLocation()-FVector(2000,2000,0),GetPawn()->GetActorLocation()+FVector(2000,2000,700));
	// Include building height so the raised racks and wall tops also fit the view.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		if (It->ActorHasTag(TEXT("ObserverRoof")))
			Bounds.Max.Z=FMath::Max(Bounds.Max.Z,It->GetComponentsBoundingBox(true).Max.Z);
	ObserverFocus=Bounds.GetCenter();
	ObserverRotation=FRotator(-70,90,0);
	int32 Width=0,Height=0; GetViewportSize(Width,Height);
	const float Aspect=Height>0 ? float(Width)/Height : 16.f/9.f;
	const float TanHorizontal=FMath::Tan(FMath::DegreesToRadians(ViewFOV*.5f));
	const float TanVertical=TanHorizontal/Aspect;
	const FRotationMatrix Axes(ObserverRotation);
	ObserverDistance=700.f;
	// Fit each corner in camera space, rather than fitting an oversized bounding sphere.
	for (int32 Corner=0; Corner<8; ++Corner)
	{
		const FVector Point((Corner&1) ? Bounds.Max.X : Bounds.Min.X,
			(Corner&2) ? Bounds.Max.Y : Bounds.Min.Y,(Corner&4) ? Bounds.Max.Z : Bounds.Min.Z);
		const FVector Offset=Point-ObserverFocus;
		const float Depth=FVector::DotProduct(Offset,Axes.GetUnitAxis(EAxis::X));
		const float Horizontal=FMath::Abs(FVector::DotProduct(Offset,Axes.GetUnitAxis(EAxis::Y)))/TanHorizontal;
		const float Vertical=FMath::Abs(FVector::DotProduct(Offset,Axes.GetUnitAxis(EAxis::Z)))/TanVertical;
		ObserverDistance=FMath::Max(ObserverDistance,FMath::Max(Horizontal,Vertical)-Depth);
	}
	ObserverDistance=FMath::Clamp(ObserverDistance*1.1f,700.f,18000.f);
	UpdateObserverCamera();
}

void Amsc_vrPlayerController::SetObserverFloor(int32 Floor)
{
 if (Floor < -1 || Floor > 2) return;
 if (!bObserverView) ToggleObserverView();
 SetObserverRoofVisibility(false);
 ObserverFloor=Floor;
 if (LayoutEditor && LayoutEditor->bEditing) LayoutEditor->SetEditing(Floor>=0,Floor);
 SetObserverRoofVisibility(true);
 FrameWarehouse();
 if (Floor>=0) { ObserverFocus.Z=Floor*400.f+100.f; UpdateObserverCamera(); }
}

bool Amsc_vrPlayerController::SetSimulationSpeed(int32 Multiplier)
{ return SimulationClock && SimulationClock->SetSpeed(Multiplier); }
void Amsc_vrPlayerController::ToggleLayoutEditing()
{
 if (!LayoutEditor) return;
 if (LayoutEditor->bEditing) LayoutEditor->SetEditing(false,ObserverFloor);
 else { if (!bObserverView || ObserverFloor<0) SetObserverFloor(0);LayoutEditor->SetEditing(true,ObserverFloor); }
 UpdateInputMode();
}
void Amsc_vrPlayerController::SetLayoutCategory(int32 CategoryIndex)
{ if (LayoutEditor && CategoryIndex>=0 && CategoryIndex<=int32(EWarehouseLayoutCategory::All)) LayoutEditor->SetCategory(EWarehouseLayoutCategory(CategoryIndex)); }
void Amsc_vrPlayerController::SetLayoutAction(int32 ActionIndex)
{ if (LayoutEditor && ActionIndex>=0 && ActionIndex<=int32(EWarehouseLayoutAction::Add)) LayoutEditor->SetAction(EWarehouseLayoutAction(ActionIndex)); }

bool Amsc_vrPlayerController::OpenLayoutRotationMenu(FVector2D ScreenPosition)
{
 if (!bObserverView || IsWarehouseMenuOpen() || !LayoutEditor || !LayoutEditor->CanRotateSelection() ||
     ScreenPosition.ContainsNaN() || !LayoutCanvas.IsValid() || !FSlateApplication::IsInitialized()) return false;
 auto Menu=SNew(SVerticalBox);
 const TWeakObjectPtr<Amsc_vrPlayerController> MenuOwner(this);
 for (int32 Side=0;Side<2;++Side)
  Menu->AddSlot().AutoHeight().Padding(2)[SNew(SButton).IsFocusable(true).ContentPadding(FMargin(16,10))
   .IsEnabled_Lambda([MenuOwner]() { return MenuOwner.IsValid() && MenuOwner->LayoutEditor && MenuOwner->LayoutEditor->CanRotateSelection(); })
   .OnClicked_Lambda([MenuOwner,Side]() { if (MenuOwner.IsValid()) MenuOwner->RotateLayoutSelection(Side==0 ? -90.f : 90.f);return FReply::Handled(); })
   [SNew(STextBlock).Font(WarehouseUIStyle::Font()).ColorAndOpacity(WarehouseUIStyle::Text)
    .Text(FText::FromString(Side==0 ? TEXT("좌측으로 90도 회전") : TEXT("우측으로 90도 회전")))]];
 Menu->AddSlot().AutoHeight().Padding(2,6,2,2)[SNew(SButton).IsFocusable(true).ContentPadding(FMargin(16,10))
  .IsEnabled_Lambda([MenuOwner]() { return MenuOwner.IsValid() && MenuOwner->LayoutEditor && MenuOwner->LayoutEditor->CanRotateSelection(); })
  .ToolTipText(FText::FromString(TEXT("선택한 요소를 삭제합니다. 상자는 보류된 물류에 보관됩니다.")))
  .OnClicked_Lambda([MenuOwner]() { if (MenuOwner.IsValid()) MenuOwner->DeleteLayoutSelection();return FReply::Handled(); })
  [SNew(STextBlock).Font(WarehouseUIStyle::Font()).ColorAndOpacity(WarehouseUIStyle::Warning)
   .Text(FText::FromString(TEXT("선택한 요소 삭제")))]];
 const auto Content=SNew(SBorder).Padding(4).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
  .BorderBackgroundColor(WarehouseUIStyle::Panel)[Menu];
 FSlateApplication::Get().DismissAllMenus();
 return FSlateApplication::Get().PushMenu(LayoutCanvas.ToSharedRef(),FWidgetPath(),Content,ScreenPosition,
  FPopupTransitionEffect::ContextMenu,true,FVector2D::ZeroVector,EPopupMethod::UseCurrentWindow).IsValid();
}

bool Amsc_vrPlayerController::RotateLayoutSelection(float DeltaYawDegrees)
{
 if (FSlateApplication::IsInitialized()) FSlateApplication::Get().DismissAllMenus();
 if (!bObserverView || IsWarehouseMenuOpen() || !LayoutEditor) return false;
 const bool Accepted=LayoutEditor->RotateSelection(DeltaYawDegrees);
 SetObserverRoofVisibility(false);SetObserverRoofVisibility(true);
 return Accepted;
}

bool Amsc_vrPlayerController::DeleteLayoutSelection()
{
 if (FSlateApplication::IsInitialized()) FSlateApplication::Get().DismissAllMenus();
 if (!bObserverView || IsWarehouseMenuOpen() || !LayoutEditor || !LayoutEditor->CanRotateSelection()) return false;
 const bool Accepted=LayoutEditor->DeleteSelection();
 if (Accepted) { SetObserverRoofVisibility(false);SetObserverRoofVisibility(true); }
 return Accepted;
}

void Amsc_vrPlayerController::SetObserverRoofVisibility(bool Hide)
{
	if (Hide)
	{
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        {
            const bool Dynamic=It->IsA<AWarehouseForklift>() || It->IsA<AWarehousePallet>() || It->IsA<AWarehouseCargo>();
            bool Above=ObserverFloor>=0 && (Dynamic ? It->GetActorLocation().Z>=(ObserverFloor+1)*400.f-1.f :
                ((ObserverFloor<1 && It->ActorHasTag(TEXT("WarehouseFloor1"))) ||
                 (ObserverFloor<2 && It->ActorHasTag(TEXT("WarehouseFloor2")))));
            if (Above && !Dynamic)
            {
                FVector Center,Extent;It->GetActorBounds(false,Center,Extent);
                // Upper decks are hidden, but columns rooted in the observed
                // floor remain visible as the real obstacles they still are.
                if (Extent.Z>50.f && Center.Z-Extent.Z<=ObserverFloor*400.f+10.f) Above=false;
            }
            if ((Above || It->ActorHasTag(TEXT("ObserverRoof"))) && !HiddenActors.Contains(*It))
            {
                HiddenActors.Add(*It);
                ObserverHiddenRoofs.Add(*It);
            }
        }
	}
	else
	{
		for (const auto& Roof : ObserverHiddenRoofs) if (Roof.IsValid()) HiddenActors.Remove(Roof.Get());
		ObserverHiddenRoofs.Reset();
	}
}

void Amsc_vrPlayerController::UpdateObserverCamera()
{
	if (IsValid(ObserverCamera)) ObserverCamera->SetActorLocationAndRotation(
		ObserverFocus-ObserverRotation.Vector()*ObserverDistance,ObserverRotation);
}

void Amsc_vrPlayerController::PlayerTick(float Dt)
{
	Super::PlayerTick(Dt);
	if (ObserverWallFade) ObserverWallFade->Update(this,bObserverView);
	const double ObserverNow=FPlatformTime::Seconds();
	const float CameraDt=ObserverLastWall>0 ? float(FMath::Clamp(ObserverNow-ObserverLastWall,0.,.05)) : 1.f/60.f;
	ObserverLastWall=ObserverNow;
	if (IsLocalPlayerController() && ObserverNow>=NextTransportOutlineUpdate)
	{
		NextTransportOutlineUpdate=ObserverNow+.2;
		TransportVisualizer->Update();
	}
	if (ComputerWidget.IsValid() && (!IsValid(ActiveComputer) || !ActiveComputer->CanUse())) CloseWarehouseComputer();
	UpdateEmergencyAlerts(Dt);
	MouseSensitivity=FMath::Clamp(MouseSensitivity,.2f,3.f);
	ViewFOV=FMath::Clamp(ViewFOV,70.f,110.f);
	if (GetPawn()) if (auto* Camera=GetPawn()->FindComponentByClass<UCameraComponent>()) Camera->SetFieldOfView(ViewFOV);
	if (!bObserverView || !IsValid(ObserverCamera)) return;
    if (ObserverFloor>=0)
    {
        ObserverVisibilityElapsed+=Dt;
        if (ObserverVisibilityElapsed>=.25f)
        {
            ObserverVisibilityElapsed=0;
            SetObserverRoofVisibility(false); SetObserverRoofVisibility(true);
        }
    }
	ObserverCamera->GetCameraComponent()->SetFieldOfView(ViewFOV);
	if (MenuWidget.IsValid() || !GetWorld()->GetGameViewport() || !GetWorld()->GetGameViewport()->Viewport || !GetWorld()->GetGameViewport()->Viewport->HasFocus()) return;
	// Layout gestures are handled by the Slate canvas, including while paused.
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().AnyMenusVisible()) return;
	float X=0,Y=0; GetInputMouseDelta(X,Y);
	if (IsInputKeyDown(EKeys::RightMouseButton) && !(LayoutEditor && LayoutEditor->bEditing && LayoutEditor->CanRotateSelection()))
	{
		ObserverRotation.Yaw+=X*MouseSensitivity*.2f;
		ObserverRotation.Pitch=FMath::Clamp(ObserverRotation.Pitch-Y*MouseSensitivity*.2f,-85.f,-45.f);
	}
	const float Forward=float(IsInputKeyDown(EKeys::W))-float(IsInputKeyDown(EKeys::S));
	const float Right=float(IsInputKeyDown(EKeys::D))-float(IsInputKeyDown(EKeys::A));
	const float Zoom=float(IsInputKeyDown(EKeys::SpaceBar))-float(IsInputKeyDown(EKeys::LeftControl));
	const float Speed=FMath::Clamp(ObserverDistance*.35f,450.f,3500.f)*(IsInputKeyDown(EKeys::LeftShift) ? 3.f : 1.f);
	const FRotator Flat(0,ObserverRotation.Yaw,0);
	const FVector Direction=Flat.Vector()*Forward+FRotationMatrix(Flat).GetUnitAxis(EAxis::Y)*Right;
	ObserverFocus+=Direction.GetClampedToMaxSize(1.f)*CameraDt*Speed;
	ObserverDistance=FMath::Clamp((ObserverDistance+Zoom*CameraDt*Speed)*FMath::Pow(.85f,GetInputAnalogKeyState(EKeys::MouseWheelAxis)),700.f,18000.f);
	UpdateObserverCamera();
}

FText Amsc_vrPlayerController::GetCargoReadout() const
{
 if (IsWarehouseMenuOpen() || !PlayerCameraManager || !GetPawn()) return FText::GetEmpty();
 if (auto* OperatorCharacter=Cast<Amsc_vrCharacter>(GetPawn()))
  if (OperatorCharacter->GetRemoteForklift())
   return FText::FromString(TEXT("W/S 전후진 · A/D 조향·제자리 회전 · 좌클릭 포크 올림 · 우클릭 내림\n마우스 둘러보기 · Space 제동 · G 원격 조작 종료"));
 if (auto* OperatorCharacter=Cast<Amsc_vrCharacter>(GetPawn()))
 {
  if (auto* Cargo=Cast<AWarehouseCargo>(OperatorCharacter->GetHeldCargo()))
   return OperatorCharacter->IsPlacingCargo() ? FText::FromString(Cargo->GetCargoDescription().ToString()+TEXT("\n내려놓는 중 · E : 취소")) : Cargo->GetCargoDescription();
  if (auto* Pallet=Cast<AWarehousePallet>(OperatorCharacter->GetHeldCargo()))
   return FText::FromString(FString::Printf(TEXT("팔레트 · %.0f kg\n%s"),Pallet->PalletMassKg,
     OperatorCharacter->IsPlacingCargo() ? TEXT("팔레트 위에 내려놓는 중 · E : 취소") : TEXT("E : 가까운 팔레트 위에 쌓기 / 안전한 곳에 내려놓기")));
  if (!bObserverView)
   if (Cast<AWarehouseComputer>(OperatorCharacter->GetInteractionTarget()))
    return FText::FromString(TEXT("E : 관제 컴퓨터 접속\n실시간 재고 · 운반 작업 · 지게차 관리"));
  if (!bObserverView)
   if (auto* Lift=Cast<AWarehousePalletLift>(OperatorCharacter->GetInteractionTarget()))
    return Lift->GetInteractionText(FMath::Clamp(FMath::FloorToInt((OperatorCharacter->GetActorLocation().Z-Lift->GetActorLocation().Z)/400.f),0,2));
  if (!bObserverView)
   if (auto* Vehicle=Cast<AWarehouseForklift>(OperatorCharacter->GetInteractionTarget()))
    return FText::FromString(FString::Printf(TEXT("E : %s  |  G : 스마트폰 원격 조작\n%s\n속도 %.2f m/s · 배터리 %.0f%% · 적재 %.0f / %.0f kg"),
     Vehicle->bPowered ? TEXT("자율 운행 정지") : TEXT("자율 운행 시작 / 재개"),
     *Vehicle->Status,FMath::Abs(Vehicle->CurrentSpeedCm)*.01f,Vehicle->BatteryPercent,Vehicle->GetLoadMassKg(),Vehicle->RatedLoadKg));
 }
 const FVector Eye=PlayerCameraManager->GetCameraLocation();
 FHitResult Hit;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(CargoReadout),true,GetPawn());
 for (AActor* Roof : HiddenActors) if (Roof) Params.AddIgnoredActor(Roof);
 if (GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+PlayerCameraManager->GetCameraRotation().Vector()*(bObserverView ? 18000.f : 400.f),ECC_Visibility,Params))
 {
  if (auto* Cargo=Cast<AWarehouseCargo>(Hit.GetActor())) return Cargo->GetCargoDescription();
  if (!bObserverView && Hit.Distance<=250.f)
   if (auto* Pallet=Cast<AWarehousePallet>(Hit.GetActor()))
    return FText::FromString(FString::Printf(TEXT("팔레트 · %.0f kg\n%s\nE : 빈 팔레트 들기"),Pallet->PalletMassKg,*Pallet->GetReceiptDescription().ToString()));
 }
 return FText::GetEmpty();
}

void Amsc_vrPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (ObserverWallFade) ObserverWallFade->Restore();
	if (FSlateApplication::IsInitialized()) FSlateApplication::Get().DismissAllMenus();
	if (LayoutEditor && LayoutEditor->bEditing) LayoutEditor->SetEditing(false,ObserverFloor);
	if (LayoutToolbar.IsValid() && GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(LayoutToolbar.ToSharedRef());
	LayoutToolbar.Reset();
	if (LayoutCanvas.IsValid() && GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(LayoutCanvas.ToSharedRef());
	LayoutCanvas.Reset();
	CloseWarehouseComputer();
	if (EmergencyAudio) { EmergencyAudio->Stop(); EmergencyAudio->DestroyComponent(); }
	if (EmergencyAlertWidget.IsValid() && GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(EmergencyAlertWidget.ToSharedRef());
	EmergencyAlertWidget.Reset(); EmergencyAudio=nullptr; EmergencyTone=nullptr;
	if (auto* OperatorCharacter=Cast<Amsc_vrCharacter>(GetPawn())) OperatorCharacter->EndRemoteControl();
	SetObserverRoofVisibility(false);
	if (CargoReadoutWidget.IsValid() && GetWorld()->GetGameViewport()) GetWorld()->GetGameViewport()->RemoveViewportWidgetContent(CargoReadoutWidget.ToSharedRef());
	CargoReadoutWidget.Reset();
	CargoReadoutBox.Reset();
	CloseWarehouseMenu();
	if (IsValid(ObserverCamera)) ObserverCamera->Destroy();
	Super::EndPlay(Reason);
}

void Amsc_vrPlayerController::UpdateEmergencyAlerts(float Dt)
{
 if (!IsLocalPlayerController()) return;
 EmergencyAlarmElapsed+=Dt; EmergencyToneElapsed+=Dt; EmergencyPollElapsed+=Dt;
 if (EmergencyAudio && EmergencyToneElapsed>.75f) EmergencyAudio->Stop();
 if (EmergencyPollElapsed>=.25f)
 {
  EmergencyPollElapsed=0;
  TArray<FString> Notices;
  for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) if (It->bEmergencyBlocked)
  {
   const float Distance=GetPawn() ? FVector::Dist(GetPawn()->GetActorLocation(),It->GetActorLocation())*.01f : 0.f;
   const FString Job=It->ActiveJob.JobId.IsEmpty() ? FString() :
    FString::Printf(TEXT(" · %s 작업 #%s"),It->ActiveJob.SourceSystem==TEXT("FMS-COMPUTER") ? TEXT("출고") : TEXT("운반"),*It->ActiveJob.JobId.Right(6));
   Notices.Add(FString::Printf(TEXT("%s%s · 거리 %.0fm\n%s"),*It->VehicleName,*Job,Distance,*It->EmergencyReason));
  }
  if (EmergencyAlertText.IsEmpty() && !Notices.IsEmpty()) EmergencyAlarmElapsed=6.f;
  EmergencyAlertText=FString::Join(Notices,TEXT("\n\n"));
  TArray<FString> Resources;
  for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
   if (!It->LastErrorMessage.IsEmpty()) Resources.Add(It->VehicleName+TEXT(": ")+It->LastErrorMessage);
  ResourceAlertText=FString::Join(Resources,TEXT("\n"));
  if (Notices.IsEmpty())
  {
   EmergencyAlarmElapsed=0;
   if (EmergencyAudio) EmergencyAudio->Stop();
  }
 }
 if (!EmergencyAlertText.IsEmpty() && EmergencyAlarmElapsed>=6.f) PlayEmergencyAlarm();
}

void Amsc_vrPlayerController::PlayEmergencyAlarm()
{
 EmergencyAlarmElapsed=0; EmergencyToneElapsed=0; ++EmergencyAlarmCount;
 if (!EmergencyTone)
 {
  EmergencyTone=NewObject<USoundWaveProcedural>(this);
  EmergencyTone->SetSampleRate(22050); EmergencyTone->NumChannels=1; EmergencyTone->Duration=.7f;
  EmergencyAudio=UGameplayStatics::CreateSound2D(this,EmergencyTone,.45f,1.f,0.f,nullptr,false,false);
  if (EmergencyAudio) EmergencyAudio->bIsUISound=true;
 }
 // A short two-tone PCM alarm needs no editor-only or third-party sound asset.
 if (EmergencyAudio)
 {
  EmergencyAudio->Stop(); EmergencyTone->ResetAudio();
  TArray<int16> Samples; Samples.SetNumZeroed(15435);
  for (int32 I=0;I<Samples.Num();++I)
  {
   const float T=float(I)/22050.f;
   const float Local=T<.3f ? T : T-.4f;
   if (Local<0 || Local>=.3f) continue;
   const float Envelope=FMath::Clamp(FMath::Min(Local,.3f-Local)*100.f,0.f,1.f);
   Samples[I]=int16(11000.f*Envelope*FMath::Sin(2.f*PI*(T<.3f ? 880.f : 1180.f)*T));
  }
  EmergencyTone->QueueAudio(reinterpret_cast<const uint8*>(Samples.GetData()),Samples.Num()*sizeof(int16));
  EmergencyAudio->Play();
 }
 UE_LOG(Logmsc_vr,Log,TEXT("AGV_EMERGENCY_ALARM_REQUEST %d: %s"),EmergencyAlarmCount,*EmergencyAlertText);
}

bool Amsc_vrPlayerController::IsEmergencyAlarmPlaying() const
{
 return EmergencyAudio && EmergencyAudio->IsPlaying();
}
