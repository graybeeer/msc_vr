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

int32 UWarehouseOperationsSettings::GetTruckVisitLevel(bool bInbound) const
{
 return FMath::Clamp(bInbound ? InboundTruckVisitLevel : OutboundTruckVisitLevel,1,5);
}

void UWarehouseOperationsSettings::SetTruckVisitLevel(bool bInbound,int32 Level,bool bPersist)
{
 (bInbound ? InboundTruckVisitLevel : OutboundTruckVisitLevel)=FMath::Clamp(Level,1,5);
 if (bPersist) SaveConfig();
}
