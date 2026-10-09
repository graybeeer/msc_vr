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
	UFUNCTION(BlueprintCallable, Category="Settings") void ToggleObserverView();
	UFUNCTION(BlueprintCallable, Category="Settings") void FrameWarehouse();
	UFUNCTION(BlueprintCallable, Category="Settings") void SetObserverFloor(int32 Floor);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Settings") int32 ObserverFloor=-1;
	UFUNCTION(BlueprintPure, Category="Settings") bool IsObserverView() const { return bObserverView; }
	UFUNCTION(BlueprintPure, Category="Settings") bool IsWarehouseMenuOpen() const { return MenuWidget.IsValid(); }
	virtual void PlayerTick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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
	TSharedPtr<SBox> CargoReadoutBox;
	void SetObserverRoofVisibility(bool Hide);
	TArray<TWeakObjectPtr<AActor>> ObserverHiddenRoofs;
	FVector ObserverFocus = FVector::ZeroVector;
	FRotator ObserverRotation = FRotator(-70,90,0);
	float ObserverDistance = 6000.f;
	float ObserverVisibilityElapsed = 0;
	TSharedPtr<SWidget> MenuWidget;
	UPROPERTY(Transient) TObjectPtr<ACameraActor> ObserverCamera;
	bool bObserverView = false;
	bool bWarehouseInputLocked = false;
};
