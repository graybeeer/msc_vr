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
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundWaveProcedural.h"
#include "Kismet/GameplayStatics.h"

Amsc_vrPlayerController::Amsc_vrPlayerController()
{
	// set the player camera manager class
	PlayerCameraManagerClass = Amsc_vrCameraManager::StaticClass();
}

void Amsc_vrPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalPlayerController() && GetWorld()->GetGameViewport())
	{
		auto RemoteActive=[this]()
		{
			const auto* OperatorCharacter=Cast<Amsc_vrCharacter>(GetPawn());
			return OperatorCharacter && OperatorCharacter->GetRemoteForklift()!=nullptr;
		};
		CargoReadoutWidget=SAssignNew(CargoReadoutBox,SBox).HAlign(HAlign_Center).VAlign(VAlign_Bottom)
			.Padding_Lambda([RemoteActive]() { return RemoteActive() ? FMargin(24,24,0,0) : FMargin(0,0,0,48); })
			.Visibility(EVisibility::HitTestInvisible)
			[SNew(SBorder).Padding(12).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(0.02f,0.03f,0.04f,.9f))
				.Visibility_Lambda([this]() { return GetCargoReadout().IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",28)).ColorAndOpacity(FLinearColor::White)
					.Text_Lambda([this]() { return GetCargoReadout(); })]];
		GetWorld()->GetGameViewport()->AddViewportWidgetContent(CargoReadoutWidget.ToSharedRef(),5);
		EmergencyAlertWidget=SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(24,24))
			.Visibility(EVisibility::HitTestInvisible)
			[SNew(SBox).WidthOverride(820)
				[SNew(SBorder).Padding(18).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FLinearColor(.35f,.015f,.01f,.96f))
					.Visibility_Lambda([this]() { return EmergencyAlertText.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
					[SNew(SVerticalBox)
						+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",30))
							.ColorAndOpacity(FLinearColor(1,.8f,.25f)).Text(FText::FromString(TEXT("비상 · 지게차 끼임 / 이동 불가")))]
						+SVerticalBox::Slot().AutoHeight().Padding(0,10)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",23))
							.ColorAndOpacity(FLinearColor::White).WrapTextAt(780).Text_Lambda([this]() { return FText::FromString(EmergencyAlertText); })]
						+SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",20))
							.ColorAndOpacity(FLinearColor::White).WrapTextAt(780).Text(FText::FromString(TEXT("주변 장애물을 제거하거나 G로 수동 탈출하세요. 해결 후 E로 재점검·재개합니다.")))]]]];
		GetWorld()->GetGameViewport()->AddViewportWidgetContent(EmergencyAlertWidget.ToSharedRef(),200);
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
	InputComponent->BindKey(EKeys::F1,IE_Pressed,this,&Amsc_vrPlayerController::ToggleWarehouseMenu);
	InputComponent->BindKey(EKeys::Escape,IE_Pressed,this,&Amsc_vrPlayerController::ToggleWarehouseMenu);

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
	const bool Locked=MenuWidget.IsValid() || bObserverView;
	if (Locked!=bWarehouseInputLocked)
	{
		SetIgnoreMoveInput(Locked); SetIgnoreLookInput(Locked);
		bWarehouseInputLocked=Locked;
		if (auto* PlayerCharacter=Cast<ACharacter>(GetPawn())) PlayerCharacter->GetCharacterMovement()->StopMovementImmediately();
	}
	bShowMouseCursor=MenuWidget.IsValid();
	if (MenuWidget.IsValid())
	{
		FInputModeGameAndUI Mode;
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
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
	if (!IsLocalController() || !GetWorld()->GetGameViewport()) return;
	if (MenuWidget.IsValid()) { CloseWarehouseMenu(); return; }
	if (auto* OperatorCharacter=Cast<Amsc_vrCharacter>(GetPawn())) OperatorCharacter->EndRemoteControl();
	static const FTextBlockStyle ButtonText=FTextBlockStyle(FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
		.SetFont(FCoreStyle::GetDefaultFontStyle("Regular",20)).SetColorAndOpacity(FLinearColor::White);
	auto Panel=SNew(SVerticalBox);
	auto Text=[&](const FString& Label)
	{
		Panel->AddSlot().AutoHeight().Padding(8)[SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",20))
			.ColorAndOpacity(FLinearColor::White).Text(FText::FromString(Label)).AutoWrapText(true)];
	};
	auto Button=[&](const FString& Label, TFunction<void()> Action)
	{
		Panel->AddSlot().AutoHeight().Padding(8,4)[SNew(SButton).IsFocusable(false).ContentPadding(10).TextStyle(&ButtonText)
			.Text(FText::FromString(Label)).OnClicked_Lambda([Action]() { Action(); return FReply::Handled(); })];
	};
	Text(TEXT("물류창고 · 설정 / 관찰"));
	Text(TEXT("F1: 메뉴  |  Shift: 달리기  |  Ctrl: 앉기  |  E: 집기 / 적재\nG: 바라보는 지게차를 스마트폰으로 원격 조작 / 종료\n원격: W/S 전후진 · A/D 조향·자리 회전 · R/F 포크 승강 · Space 제동\n메뉴와 관찰 중에도 창고 시뮬레이션은 계속됩니다."));
	Button(bObserverView ? TEXT("1인칭 캐릭터로 돌아가기") : TEXT("전지적 관찰 시점으로 전환"),[this]() { ToggleObserverView(); CloseWarehouseMenu(); });
	Button(TEXT("창고 전체 보기"),[this]() { if (!bObserverView) ToggleObserverView(); SetObserverFloor(-1); CloseWarehouseMenu(); });
    for (int32 Floor=0; Floor<3; ++Floor)
        Button(FString::Printf(TEXT("%d층 관찰 (위층 구조 숨기기)"),Floor+1),[this,Floor]() { SetObserverFloor(Floor); CloseWarehouseMenu(); });
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
			[SNew(SBorder).Padding(18).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FLinearColor(.025f,.035f,.05f,1))
				[SNew(SScrollBox)+SScrollBox::Slot()[Panel]]]];
	GetWorld()->GetGameViewport()->AddViewportWidgetContent(MenuWidget.ToSharedRef(),100);
	UpdateInputMode();
}

void Amsc_vrPlayerController::ToggleObserverView()
{
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
		bObserverView=false;
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
 SetObserverRoofVisibility(true);
 FrameWarehouse();
 if (Floor>=0) { ObserverFocus.Z=Floor*400.f+100.f; UpdateObserverCamera(); }
}

void Amsc_vrPlayerController::SetObserverRoofVisibility(bool Hide)
{
	if (Hide)
	{
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        {
            const bool Dynamic=It->IsA<AWarehouseForklift>() || It->IsA<AWarehousePallet>() || It->IsA<AWarehouseCargo>();
            const bool Above=ObserverFloor>=0 && (Dynamic ? It->GetActorLocation().Z>=(ObserverFloor+1)*400.f-1.f :
                ((ObserverFloor<1 && It->ActorHasTag(TEXT("WarehouseFloor1"))) ||
                 (ObserverFloor<2 && It->ActorHasTag(TEXT("WarehouseFloor2")))));
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
	UpdateEmergencyAlerts(Dt);
	if (CargoReadoutBox.IsValid())
	{
		const auto* OperatorCharacter=Cast<Amsc_vrCharacter>(GetPawn());
		const bool Remote=OperatorCharacter && OperatorCharacter->GetRemoteForklift()!=nullptr;
		CargoReadoutBox->SetHAlign(Remote ? HAlign_Left : HAlign_Center);
		CargoReadoutBox->SetVAlign(Remote ? VAlign_Top : VAlign_Bottom);
	}
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
	float X=0,Y=0; GetInputMouseDelta(X,Y);
	if (IsInputKeyDown(EKeys::RightMouseButton))
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
	ObserverFocus+=Direction.GetClampedToMaxSize(1.f)*Dt*Speed;
	ObserverDistance=FMath::Clamp((ObserverDistance+Zoom*Dt*Speed)*FMath::Pow(.85f,GetInputAnalogKeyState(EKeys::MouseWheelAxis)),700.f,18000.f);
	UpdateObserverCamera();
}

FText Amsc_vrPlayerController::GetCargoReadout() const
{
 if (MenuWidget.IsValid() || !PlayerCameraManager || !GetPawn()) return FText::GetEmpty();
 if (auto* OperatorCharacter=Cast<Amsc_vrCharacter>(GetPawn()))
  if (auto* Vehicle=OperatorCharacter->GetRemoteForklift())
   return FText::FromString(FString::Printf(TEXT("스마트폰 원격 조작 · %s\nW/S 전후진 · A/D 조향·자리 회전 · R/F 포크 승강 · Space 제동 · G 종료\n%s\n속도 %.2f m/s · 배터리 %.0f%% · 적재 %.0f / %.0f kg"),
    *Vehicle->VehicleName,*Vehicle->Status,FMath::Abs(Vehicle->CurrentSpeedCm)*.01f,Vehicle->BatteryPercent,Vehicle->GetLoadMassKg(),Vehicle->RatedLoadKg));
 if (auto* OperatorCharacter=Cast<Amsc_vrCharacter>(GetPawn()))
 {
  if (auto* Cargo=Cast<AWarehouseCargo>(OperatorCharacter->GetHeldCargo())) return Cargo->GetCargoDescription();
  if (auto* Pallet=Cast<AWarehousePallet>(OperatorCharacter->GetHeldCargo()))
   return FText::FromString(FString::Printf(TEXT("팔레트 · %.0f kg\nE : 안전한 곳에 내려놓기"),Pallet->PalletMassKg));
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
    return FText::FromString(FString::Printf(TEXT("팔레트 · %.0f kg\nE : 빈 팔레트 들기"),Pallet->PalletMassKg));
  if (!bObserverView && Hit.Distance<=300.f)
   if (auto* Vehicle=Cast<AWarehouseForklift>(Hit.GetActor()))
    return FText::FromString(FString::Printf(TEXT("E : %s  |  G : 스마트폰 원격 조작\n%s\n속도 %.2f m/s · 배터리 %.0f%% · 적재 %.0f / %.0f kg"),
     Vehicle->bPowered ? TEXT("자율 운행 정지") : TEXT("자율 운행 시작 / 재개"),
     *Vehicle->Status,FMath::Abs(Vehicle->CurrentSpeedCm)*.01f,Vehicle->BatteryPercent,Vehicle->GetLoadMassKg(),Vehicle->RatedLoadKg));
 }
 return FText::GetEmpty();
}

void Amsc_vrPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
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
   const FString Job=It->ActiveJob.JobId.IsEmpty() ? FString() : TEXT(" · 작업 ")+It->ActiveJob.JobId;
   Notices.Add(FString::Printf(TEXT("%s%s · 거리 %.0fm\n%s"),*It->VehicleName,*Job,Distance,*It->EmergencyReason));
  }
  if (EmergencyAlertText.IsEmpty() && !Notices.IsEmpty()) EmergencyAlarmElapsed=6.f;
  EmergencyAlertText=FString::Join(Notices,TEXT("\n\n"));
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
