#include "AgvAutoAttach.h"
#include "AgvShadowSensors.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "WarehouseForklift.h"

DEFINE_LOG_CATEGORY_STATIC(LogAgvAuto, Log, All);

bool UAgvAutoAttachSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UAgvAutoAttachSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bool bWanted = false;
	int32 Localizing = 0, Guarded = 0, Total = 0;
	for (TActorIterator<AWarehouseForklift> It(&InWorld); It; ++It)
	{
		bWanted |= It->bUseAgvLocalization || It->bUseAgvSafety;
		Localizing += It->bUseAgvLocalization;
		Guarded += It->bUseAgvSafety;
		++Total;
	}
	UE_LOG(LogAgvAuto, Log, TEXT("AGV_AUTO %d forklift(s): AGV localization %d, AGV safety fields %d"), Total, Localizing, Guarded);
	for (TActorIterator<AAgvShadowManager> It(&InWorld); It; ++It)
	{
		return; // a test or the console already placed one
	}
	if (bWanted)
	{
		InWorld.SpawnActor<AAgvShadowManager>();
	}
}
