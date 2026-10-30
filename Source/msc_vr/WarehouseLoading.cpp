#include "WarehouseLoading.h"
#include "WarehouseUIStyle.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "MoviePlayer.h"
#include "Misc/App.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"
#include <atomic>

struct FWarehouseLoadingPaintState
{
 std::atomic<int32> Paints{0};
};

namespace
{
 // MoviePlayer paints this Slate-only tree on its loading thread. No UObject,
 // world query, UI binding, or mutable gameplay text is read by that thread.
 class SWarehouseLoadingCurtain : public SCompoundWidget
 {
 public:
  using FPaintStatePtr=TSharedPtr<FWarehouseLoadingPaintState,ESPMode::ThreadSafe>;
  SLATE_BEGIN_ARGS(SWarehouseLoadingCurtain) {}
   SLATE_ARGUMENT(FText,Message)
   SLATE_ARGUMENT(FPaintStatePtr,PaintState)
  SLATE_END_ARGS()
  void Construct(const FArguments& Args)
  {
   State=Args._PaintState;
   ChildSlot
   [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
    .BorderBackgroundColor(FLinearColor(.012f,.025f,.038f,1.f)).Padding(40)
    .HAlign(HAlign_Center).VAlign(VAlign_Center)
    [SNew(SBox).WidthOverride(620)
     [SNew(SVerticalBox)
      +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,12)
       [SNew(STextBlock).Text(FText::FromString(TEXT("물류창고 준비 중")))
        .Font(WarehouseUIStyle::Font(30,true)).ColorAndOpacity(WarehouseUIStyle::Text)]
      +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,24)
       [SNew(STextBlock).Text(Args._Message).Font(WarehouseUIStyle::Font(20))
        .ColorAndOpacity(WarehouseUIStyle::Text).AutoWrapText(true)]
      +SVerticalBox::Slot().AutoHeight().Padding(0,0,0,18)
       [SNew(SBox).HeightOverride(7)
        [SNew(SProgressBar).Percent(TOptional<float>()).FillColorAndOpacity(WarehouseUIStyle::Accent)]]
      +SVerticalBox::Slot().AutoHeight()
       [SNew(STextBlock).Text(FText::FromString(TEXT("처리가 끝나면 자동으로 돌아갑니다.")))
        .Font(WarehouseUIStyle::Font(16)).ColorAndOpacity(FLinearColor(.65f,.73f,.78f,1.f))]
     ]
    ]
   ];
  }
  virtual bool SupportsKeyboardFocus() const override { return true; }
  virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent&) override { return FReply::Handled(); }
  virtual FReply OnKeyUp(const FGeometry&,const FKeyEvent&) override { return FReply::Handled(); }
  virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&) override { return FReply::Handled(); }
  virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&) override { return FReply::Handled(); }
  virtual FReply OnMouseWheel(const FGeometry&,const FPointerEvent&) override { return FReply::Handled(); }
  virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
   FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override
  {
   State->Paints.fetch_add(1,std::memory_order_relaxed);
   return SCompoundWidget::OnPaint(Args,Geometry,CullingRect,Elements,Layer,Style,Enabled);
  }
 private:
  TSharedPtr<FWarehouseLoadingPaintState,ESPMode::ThreadSafe> State;
 };
}

void UWarehouseLoadingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);
 PreMapHandle=FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this,&ThisClass::MapLoading);
 PostMapHandle=FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this,&ThisClass::MapLoaded);
 WorldInitHandle=FWorldDelegates::OnWorldInitializedActors.AddUObject(this,&ThisClass::WorldInitialized);
 WorldTickHandle=FWorldDelegates::OnWorldTickStart.AddUObject(this,&ThisClass::WorldTick);
}
void UWarehouseLoadingSubsystem::Deinitialize()
{
 FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreMapHandle);
 FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostMapHandle);
 FWorldDelegates::OnWorldInitializedActors.Remove(WorldInitHandle);
 FWorldDelegates::OnWorldTickStart.Remove(WorldTickHandle);
 if (IsLoading()) { ActiveTokens.Reset();Hide(); }
 Super::Deinitialize();
}
UWarehouseLoadingSubsystem* UWarehouseLoadingSubsystem::Find(const UObject* Context)
{
 UWorld* World=GEngine && Context ? GEngine->GetWorldFromContextObject(Context,EGetWorldErrorMode::ReturnNull) : nullptr;
 if (!World || !World->IsGameWorld() || !World->GetGameInstance()) return nullptr;
 return World->GetGameInstance()->GetSubsystem<UWarehouseLoadingSubsystem>();
}
int32 UWarehouseLoadingSubsystem::BeginTask(FText Message)
{
 if (!IsInGameThread()) return 0;
 const int32 Token=++NextToken;const bool First=ActiveTokens.IsEmpty();ActiveTokens.Add(Token);
 if (First)
 {
  ActiveMessage=Message.IsEmpty() ? FText::FromString(TEXT("창고 정보를 불러오고 있습니다.")) : Message;
  ++StartedScreens;Show();
 }
 return Token;
}
void UWarehouseLoadingSubsystem::EndTask(int32 Token)
{
 if (!IsInGameThread() || Token<=0 || ActiveTokens.RemoveSingle(Token)==0) return;
 if (ActiveTokens.IsEmpty()) Hide();
}
int32 UWarehouseLoadingSubsystem::GetCurrentPaintCount() const
{ return PaintState ? PaintState->Paints.load(std::memory_order_relaxed) : 0; }

void UWarehouseLoadingSubsystem::Show()
{
 LastPresentation=TEXT("Headless");LastPaintCount=0;
 if (!FApp::CanEverRender() || GUsingNullRHI || !FSlateApplication::IsInitialized()) return;
 auto* Instance=GetGameInstance();auto* GameViewport=Instance ? Instance->GetGameViewportClient() : nullptr;
 if (!GameViewport) { LastPresentation=TEXT("AwaitingViewport");return; }
 Viewport=GameViewport;PreviousFocus=FSlateApplication::Get().GetKeyboardFocusedWidget();
 PaintState=MakeShared<FWarehouseLoadingPaintState,ESPMode::ThreadSafe>();
 Widget=SNew(SWarehouseLoadingCurtain).Message(ActiveMessage).PaintState(PaintState);
 for (ULocalPlayer* Player:Instance->GetLocalPlayers())
  if (auto* Controller=Player ? Player->GetPlayerController(GetWorld()) : nullptr)
  { Controller->SetIgnoreMoveInput(true);Controller->SetIgnoreLookInput(true);InputOwners.Add(Controller); }

 auto* Movies=IsMoviePlayerEnabled() ? GetMoviePlayer() : nullptr;
 // Slate input/post-tick callbacks still hold SlateTickCriticalSection. A movie
 // loading thread needs that same lock to tick time, and joining it here would
 // deadlock. Those synchronous UI operations use the viewport curtain below.
 if (Movies && !FSlateApplication::Get().IsTicking() && Movies->IsInitialized() && !Movies->IsMovieCurrentlyPlaying())
 {
  FLoadingScreenAttributes Screen;Screen.WidgetLoadingScreen=Widget;
  Screen.bAutoCompleteWhenLoadingCompletes=false;Screen.bWaitForManualStop=true;
  Screen.bMoviesAreSkippable=false;Screen.bAllowEngineTick=false;
  Movies->SetupLoadingScreen(Screen);bOwnsMovie=Movies->PlayMovie();
  if (bOwnsMovie) { LastPresentation=TEXT("MoviePlayer");return; }
 }
 // UE disables MoviePlayer in PIE; Slate callbacks must also use this fallback.
 // Paint only the game's viewport overlay synchronously before expensive work.
 // Never tick world/input recursively or cover tools with an editor-wide dialog.
 LastPresentation=TEXT("Viewport");GameViewport->AddViewportWidgetContent(Widget.ToSharedRef(),100000);
 FSlateApplication::Get().SetKeyboardFocus(Widget,EFocusCause::SetDirectly);
 if (const auto Window=GameViewport->GetWindow();Window && !bRedrawing)
 {
  TGuardValue<bool> Guard(bRedrawing,true);
  FSlateApplication::Get().ForceRedrawWindow(Window.ToSharedRef());
  FlushRenderingCommands();
 }
 UE_LOG(LogTemp,Log,TEXT("WAREHOUSE_LOADING_SHOW presentation=%s paints=%d reason=%s"),*LastPresentation,GetCurrentPaintCount(),*ActiveMessage.ToString());
}
void UWarehouseLoadingSubsystem::Hide()
{
 TSharedPtr<SWidget> RestoreFocus=PreviousFocus.Pin();
 if (FSlateApplication::IsInitialized())
 {
  const auto Current=FSlateApplication::Get().GetKeyboardFocusedWidget();
  // BeginPlay may already have opened the new scene's menu. Preserve that
  // newer focus instead of reviving a widget from the unloaded scene.
  if (Current && Current!=Widget) RestoreFocus=Current;
 }
 if (bOwnsMovie)
 {
  if (auto* Movies=GetMoviePlayer()) { Movies->StopMovie();Movies->WaitForMovieToFinish(false); }
  bOwnsMovie=false;
 }
 if (Viewport.IsValid() && Widget) Viewport->RemoveViewportWidgetContent(Widget.ToSharedRef());
 if (FSlateApplication::IsInitialized() && RestoreFocus && FSlateApplication::Get().FindWidgetWindow(RestoreFocus.ToSharedRef()))
  FSlateApplication::Get().SetKeyboardFocus(RestoreFocus,EFocusCause::SetDirectly);
 for (const auto& Controller:InputOwners) if (Controller.IsValid())
 { Controller->SetIgnoreMoveInput(false);Controller->SetIgnoreLookInput(false); }
 InputOwners.Reset();PreviousFocus.Reset();Viewport.Reset();
 LastPaintCount=GetCurrentPaintCount();PaintState.Reset();Widget.Reset();ActiveMessage=FText::GetEmpty();++CompletedScreens;
 UE_LOG(LogTemp,Log,TEXT("WAREHOUSE_LOADING_HIDE presentation=%s paints=%d"),*LastPresentation,LastPaintCount);
}
void UWarehouseLoadingSubsystem::MapLoading(const FWorldContext& Context,const FString& MapName)
{
 if (Context.OwningGameInstance!=GetGameInstance() || MapToken) return;
 MapToken=BeginTask(FText::FromString(TEXT("장면과 물류 정보를 불러오고 있습니다.")));
 // A transition requested during BeginPlay must not leave a token waiting for
 // the old world's first tick, which will never occur after that world unloads.
 if (SceneToken) { const int32 Token=SceneToken;SceneToken=0;SceneWorld.Reset();EndTask(Token); }
}
void UWarehouseLoadingSubsystem::MapLoaded(UWorld* World)
{
 if (!MapToken || (World && World->GetGameInstance()!=GetGameInstance())) return;
 const int32 Token=MapToken;MapToken=0;EndTask(Token);
}
void UWarehouseLoadingSubsystem::WorldInitialized(const UWorld::FActorsInitializedParams& Params)
{
 UWorld* World=Params.World;
 if (!World || !World->IsGameWorld() || World->GetGameInstance()!=GetGameInstance() || MapToken || SceneToken) return;
 SceneWorld=World;SceneToken=BeginTask(FText::FromString(TEXT("창고 설비와 물류를 준비하고 있습니다.")));
}
void UWarehouseLoadingSubsystem::WorldTick(UWorld* World,ELevelTick Tick,float DeltaSeconds)
{
 if (SceneToken && SceneWorld==World && World->HasBegunPlay())
 { const int32 Token=SceneToken;SceneToken=0;SceneWorld.Reset();EndTask(Token); }
}
WarehouseLoading::FScopedTask::FScopedTask(const UObject* Context,const TCHAR* Message)
{
 Owner=UWarehouseLoadingSubsystem::Find(Context);
 if (Owner.IsValid()) Token=Owner->BeginTask(FText::FromString(Message));
}
WarehouseLoading::FScopedTask::~FScopedTask()
{ if (Owner.IsValid()) Owner->EndTask(Token); }
