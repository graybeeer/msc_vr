#include "WarehouseOperationsSettings.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UWarehouseOperationsSettings::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);
 LoadConfig();
}

UWarehouseOperationsSettings* UWarehouseOperationsSettings::Find(const UObject* Context)
{
 UWorld* World=GEngine ? GEngine->GetWorldFromContextObject(Context,EGetWorldErrorMode::ReturnNull) : nullptr;
 return World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UWarehouseOperationsSettings>() : nullptr;
}

void UWarehouseOperationsSettings::SetManualOperations(bool bManual,bool bPersist)
{
 bManualOperations=bManual;
 if (bPersist) SaveConfig();
}
