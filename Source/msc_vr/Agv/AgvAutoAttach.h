#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AgvAutoAttach.generated.h"

/**
 * Puts the AGV module's sensor kits on the main warehouse forklifts when play starts, so the forklifts drive on the
 * module's localization and safety fields without a console command whenever their bUseAgvLocalization /
 * bUseAgvSafety switches are on (on by default on the AGV branch). agv.localize 0 / agv.safety 0 still turn them off.
 */
UCLASS()
class MSC_VR_API UAgvAutoAttachSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
};
