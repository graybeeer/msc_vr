// Copyright Epic Games, Inc. All Rights Reserved.

#include "msc_vrGameMode.h"
#include "WarehousePallet.h"
#include "WarehouseExperimentSubsystem.h"
#include "Engine/GameInstance.h"

Amsc_vrGameMode::Amsc_vrGameMode()
{
	// stub
}

void Amsc_vrGameMode::StartPlay()
{
	Super::StartPlay();
	// All bodies have completed BeginPlay, but no simulation tick has run yet.
	// Saved layouts own their receipt state, including pending and torn units.
	if (auto* GI=GetGameInstance())
		if (auto* Experiments=GI->GetSubsystem<UWarehouseExperimentSubsystem>(); Experiments && Experiments->bLoading) return;
	AWarehousePallet::InitializeRackWrapping(GetWorld());
}
