// Copyright Epic Games, Inc. All Rights Reserved.

#include "msc_vrGameMode.h"
#include "WarehousePallet.h"
#include "WarehouseExperimentSubsystem.h"
#include "WarehouseFocusDirector.h"
#include "WarehousePlayMode.h"
#include "WarehouseOperationsSettings.h"
#include "WarehouseForklift.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

Amsc_vrGameMode::Amsc_vrGameMode()
{
	// stub
}

void Amsc_vrGameMode::StartPlay()
{
	const auto* GI=GetGameInstance();
	const auto* Experiments=GI ? GI->GetSubsystem<UWarehouseExperimentSubsystem>() : nullptr;
	const auto* Stage=AWarehousePlayStage::Find(this);
	const bool bWarehouseGameplay=(Stage && Stage->StageIndex>0) ||
		(!Stage && UGameplayStatics::GetCurrentLevelName(this,true)==TEXT("Lvl_FirstPerson"));
	const auto* Settings=UWarehouseOperationsSettings::Find(this);
	// Suppress authored auto-start before vehicle BeginPlay schedules its timer.
	// This changes only the new session; saved maps and controlled experiments retain their data.
	if (bWarehouseGameplay && Settings && !Settings->IsManualOperations() &&
		(!Experiments || (!Experiments->bLoading && !Experiments->bBatchRunning)))
		for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) It->bAutoStart=false;
	Super::StartPlay();
	// All bodies have completed BeginPlay, but no simulation tick has run yet.
	// Saved layouts own their receipt state, including pending and torn units.
	if (!Experiments || !Experiments->bLoading) AWarehousePallet::InitializeRackWrapping(GetWorld());
	// All gameplay modes use the simplified controls. The director delegates
	// campaign arrivals to the stage's finite order generator. The main menu and
	// test fixture worlds do not create office work. Restored layouts wait safely.
	if (bWarehouseGameplay && !AWarehouseFocusDirector::Find(this))
		GetWorld()->SpawnActor<AWarehouseFocusDirector>();
}
