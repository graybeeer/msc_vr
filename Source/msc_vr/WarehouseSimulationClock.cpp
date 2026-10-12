#include "WarehouseSimulationClock.h"
#include "WarehousePerformance.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Kismet/GameplayStatics.h"

AWarehouseSimulationClock::AWarehouseSimulationClock()
{
 PrimaryActorTick.bCanEverTick=true;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Clock")));
}
AWarehouseSimulationClock* AWarehouseSimulationClock::Find(const AActor* Context)
{
 if (!Context || !Context->GetWorld()) return nullptr;
 for (TActorIterator<AWarehouseSimulationClock> It(Context->GetWorld());It;++It) return *It;
 return Context->GetWorld()->SpawnActor<AWarehouseSimulationClock>();
}
void AWarehouseSimulationClock::BeginPlay()
{
 Super::BeginPlay();
 if (auto* Settings=GetWorld()->GetWorldSettings())
 {
  FixedWorldSettings=Settings;
  OldMinimumWorldStep=Settings->MinUndilatedFrameTime;OldMaximumWorldStep=Settings->MaxUndilatedFrameTime;
  // PIE can already have captured the editor's previous frame delta before
  // BeginPlay updates FApp. Native world clamping fixes that very first tick,
  // before world time, actor ticks and Chaos all consume the same timestep.
  Settings->MinUndilatedFrameTime=float(StepSeconds);Settings->MaxUndilatedFrameTime=float(StepSeconds);
 }
 OldFixed=FApp::UseFixedTimeStep();OldFixedDelta=FApp::GetFixedDeltaTime();
 if (auto* VSync=IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"))) OldVSync=VSync->GetInt();
 if (auto* Override=IConsoleManager::Get().FindConsoleVariable(TEXT("t.OverrideFPS"))) { OldOverrideFPS=Override->GetFloat();if (OldOverrideFPS!=0) Override->SetWithCurrentPriority(0.f); }
 FApp::SetFixedDeltaTime(StepSeconds);FApp::SetUseFixedTimeStep(true);
 FrameHandle=FCoreDelegates::OnBeginFrame.AddUObject(this,&AWarehouseSimulationClock::PaceFrame);
 SampleWall=FPlatformTime::Seconds();SampleSimulation=GetWorld()->GetTimeSeconds();
 SetSpeed(1);
}
bool AWarehouseSimulationClock::SetSpeed(int32 Multiplier)
{
 if (Multiplier!=1 && Multiplier!=2 && Multiplier!=4) return false;
 RequestedSpeed=Multiplier;NextFrame=FPlatformTime::Seconds();
 if (auto* VSync=IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
  if (VSync->GetInt()!=(Multiplier>1 ? 0 : OldVSync)) VSync->SetWithCurrentPriority(Multiplier>1 ? 0 : OldVSync);
 SampleWall=FPlatformTime::Seconds();SampleSimulation=GetWorld()->GetTimeSeconds();
 return true;
}
void AWarehouseSimulationClock::PaceFrame()
{
 // t.MaxFPS is bypassed by UE's fixed-timestep branch. Pace separately,
 // and never skip world/Chaos ticks to catch up when rendering is slower.
 double Now=FPlatformTime::Seconds();
 if (Now<NextFrame) { FPlatformProcess::SleepNoStats(float(NextFrame-Now));Now=FPlatformTime::Seconds(); }
 const int32 Speed=UGameplayStatics::IsGamePaused(this) ? 1 : RequestedSpeed;
 NextFrame=FMath::Max(NextFrame,Now)+StepSeconds/Speed;
}
void AWarehouseSimulationClock::Tick(float Dt)
{
 CSV_CUSTOM_STAT(Warehouse,SimulationSeconds,float(GetWorld()->GetTimeSeconds()),ECsvCustomStatOp::Set);
 CSV_CUSTOM_STAT(Warehouse,RequestedSpeed,RequestedSpeed,ECsvCustomStatOp::Set);
 Super::Tick(Dt);
 const double Now=FPlatformTime::Seconds();
 if (Now-SampleWall>=1.)
 {
  AchievedSpeed=float((GetWorld()->GetTimeSeconds()-SampleSimulation)/(Now-SampleWall));
  SampleWall=Now;SampleSimulation=GetWorld()->GetTimeSeconds();
 }
}
void AWarehouseSimulationClock::EndPlay(const EEndPlayReason::Type Reason)
{
 FCoreDelegates::OnBeginFrame.Remove(FrameHandle);
 if (auto* Settings=FixedWorldSettings.Get())
 { Settings->MinUndilatedFrameTime=OldMinimumWorldStep;Settings->MaxUndilatedFrameTime=OldMaximumWorldStep; }
 FixedWorldSettings.Reset();
 FApp::SetUseFixedTimeStep(OldFixed);FApp::SetFixedDeltaTime(OldFixedDelta);
 if (auto* VSync=IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));VSync && VSync->GetInt()!=OldVSync) VSync->SetWithCurrentPriority(OldVSync);
 if (auto* Override=IConsoleManager::Get().FindConsoleVariable(TEXT("t.OverrideFPS"));Override && Override->GetFloat()!=OldOverrideFPS) Override->SetWithCurrentPriority(OldOverrideFPS);
 Super::EndPlay(Reason);
}
