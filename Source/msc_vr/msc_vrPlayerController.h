// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "msc_vrPlayerController.generated.h"

class UInputMappingContext;
class UUserWidget;
class SWidget;
class SBox;
class ACameraActor;
class UAudioComponent;
class USoundWaveProcedural;
class AWarehouseComputer;
class AWarehouseLayoutEditor;
class AWarehouseSimulationClock;
class UWarehouseTransportVisualizer;
class UWarehouseObserverWallFade;
class UWarehouseConstructionEditor;

/**
 *  Simple first person Player Controller
 *  Manages the input mapping context.
 *  Overrides the Player Camera Manager class.
 */
UCLASS(abstract, config="Game")
class MSC_VR_API Amsc_vrPlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:

	/** Constructor */
	Amsc_vrPlayerController();
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Settings") float MouseSensitivity = 1.f;
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category="Settings") float ViewFOV = 90.f;
	UFUNCTION(BlueprintCallable, Category="Settings") void ToggleWarehouseMenu();
	UFUNCTION(BlueprintCallable, Category="Play mode") bool OpenPlayMode(int32 StageIndex);
	UFUNCTION(BlueprintPure, Category="Play mode") bool IsMainMenu() const;
	UFUNCTION(BlueprintCallable, Category="Settings") void ToggleObserverView();
	UFUNCTION(BlueprintCallable, Category="Settings") void FrameWarehouse();
	UFUNCTION(BlueprintCallable, Category="Settings") void SetObserverFloor(int32 Floor);
	UFUNCTION(BlueprintCallable, Category="Simulation") bool SetSimulationSpeed(int32 Multiplier);
	UFUNCTION(BlueprintCallable, Category="Layout") void ToggleLayoutEditing();
	UFUNCTION(BlueprintCallable, Category="Layout") void SetLayoutEditing(bool Enabled);
	UFUNCTION(BlueprintPure, Category="Layout") bool IsLayoutEditing() const;
	UFUNCTION(BlueprintCallable, Category="Building editor") void SetConstructionEditing(bool Enabled);
	UFUNCTION(BlueprintPure, Category="Building editor") UWarehouseConstructionEditor* GetConstructionEditor() const { return ConstructionEditor; }
	UFUNCTION(BlueprintCallable, Category="Layout") void SetLayoutCategory(int32 CategoryIndex);
	UFUNCTION(BlueprintCallable, Category="Layout") void SetLayoutAction(int32 ActionIndex);
	UFUNCTION(BlueprintCallable, Category="Layout") bool OpenLayoutRotationMenu(FVector2D ScreenPosition);
	UFUNCTION(BlueprintCallable, Category="Layout") bool RotateLayoutSelection(float DeltaYawDegrees);
	UFUNCTION(BlueprintCallable, Category="Layout") bool DeleteLayoutSelection();
	UFUNCTION(BlueprintCallable, Category="Simulation|Experiments") void OpenWarehouseExperiments();
	UFUNCTION(BlueprintCallable, Category="Simulation|Experiments") void CloseWarehouseExperiments();
	UFUNCTION(BlueprintCallable, Category="Layout|Conveyor") bool ConfigureLayoutConveyor(int32 Field,float Value);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Settings") int32 ObserverFloor=-1;
	UFUNCTION(BlueprintPure, Category="Settings") bool IsObserverView() const { return bObserverView; }
	UFUNCTION(BlueprintPure, Category="Settings") bool IsWarehouseMenuOpen() const { return IsMainMenu() || MenuWidget.IsValid() || ComputerWidget.IsValid() || ExperimentWidget.IsValid(); }
	UFUNCTION(BlueprintCallable, Category="Console") bool OpenWarehouseComputer(AWarehouseComputer* Computer);
	UFUNCTION(BlueprintCallable, Category="Console") void CloseWarehouseComputer();
	UFUNCTION(BlueprintPure, Category="Console") bool IsWarehouseComputerOpen() const { return ComputerWidget.IsValid(); }
	virtual void PlayerTick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Alerts") FString EmergencyAlertText;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Alerts") FString ResourceAlertText;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Alerts") int32 EmergencyAlarmCount=0;
	UFUNCTION(BlueprintPure, Category="Alerts") bool IsEmergencyAlarmPlaying() const;

protected:

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Input Mapping Contexts */
	UPROPERTY(EditAnywhere, Category="Input|Input Mappings")
	TArray<UInputMappingContext*> MobileExcludedMappingContexts;

	/** Mobile controls widget to spawn */
	UPROPERTY(EditAnywhere, Category="Input|Touch Controls")
	TSubclassOf<UUserWidget> MobileControlsWidgetClass;

	/** Pointer to the mobile controls widget */
	UPROPERTY()
	TObjectPtr<UUserWidget> MobileControlsWidget;

	/** If true, the player will use UMG touch controls even if not playing on mobile platforms */
	UPROPERTY(EditAnywhere, Config, Category = "Input|Touch Controls")
	bool bForceTouchControls = false;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;

	/** Returns true if the player should use UMG touch controls */
	bool ShouldUseTouchControls() const;
	void UpdateInputMode();
	void CloseWarehouseMenu();
	void UpdateObserverCamera();
	FText GetCargoReadout() const;
	TSharedPtr<SWidget> CargoReadoutWidget;
	TSharedPtr<SWidget> EmergencyAlertWidget;
	UPROPERTY(Transient) TObjectPtr<USoundWaveProcedural> EmergencyTone;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> EmergencyAudio;
	void UpdateEmergencyAlerts(float Dt);
	void PlayEmergencyAlarm();
	UPROPERTY(VisibleAnywhere, Category="Warehouse|Display") TObjectPtr<UWarehouseTransportVisualizer> TransportVisualizer;
	UPROPERTY(VisibleAnywhere, Category="Warehouse|Display") TObjectPtr<UWarehouseObserverWallFade> ObserverWallFade;
	double NextTransportOutlineUpdate=0;
	float EmergencyPollElapsed=0, EmergencyAlarmElapsed=0, EmergencyToneElapsed=0;
	TSharedPtr<SBox> CargoReadoutBox;
	void SetObserverRoofVisibility(bool Hide);
	TArray<TWeakObjectPtr<AActor>> ObserverHiddenRoofs;
	FVector ObserverFocus = FVector::ZeroVector;
	FRotator ObserverRotation = FRotator(-70,90,0);
	float ObserverDistance = 6000.f;
	float ObserverVisibilityElapsed = 0;
	double ObserverLastWall=0;
	TSharedPtr<SWidget> MenuWidget;
	TSharedPtr<SWidget> ComputerWidget;
	TSharedPtr<SWidget> ExperimentWidget;
	TSharedPtr<SWidget> PlayWidget;
	UPROPERTY(Transient) TObjectPtr<AWarehouseComputer> ActiveComputer;
	UPROPERTY(Transient) TObjectPtr<ACameraActor> ObserverCamera;
	UPROPERTY(Transient) TObjectPtr<AWarehouseLayoutEditor> LayoutEditor;
	UPROPERTY(Transient) TObjectPtr<UWarehouseConstructionEditor> ConstructionEditor;
	UPROPERTY(Transient) TObjectPtr<AWarehouseSimulationClock> SimulationClock;
	TSharedPtr<SWidget> LayoutToolbar;
	TSharedPtr<SWidget> LayoutCanvas;
	bool bConveyorLiftInfeedDraft=true;
	int32 ConveyorLiftDestinationDraft=2;
	int32 LastLayoutFloor=0;
	bool bObserverView = false;
	bool bWarehouseInputLocked = false;
	bool bWarehouseEndingPlay = false;
};
