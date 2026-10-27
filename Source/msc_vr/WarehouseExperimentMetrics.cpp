#include "WarehouseExperimentMetrics.h"
#include "WarehouseForklift.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/EngineVersion.h"
#include "Serialization/JsonSerializer.h"

TMap<TWeakObjectPtr<UWorld>,FWarehouseExperimentMetrics*> FWarehouseExperimentMetrics::Collectors;

namespace WarehouseExperimentMetricsPrivate
{
 TMap<FString,double> EmptyPhases()
 {
  TMap<FString,double> Result;
  for (uint8 I=0;I<uint8(EWarehouseExperimentPhase::Count);++I)
   Result.Add(FWarehouseExperimentMetrics::PhaseKey(EWarehouseExperimentPhase(I)),0.);
  return Result;
 }
 void AddPhases(TMap<FString,double>& To,const TMap<FString,double>& From)
 {
  for (const auto& Pair : From) To.FindOrAdd(Pair.Key)+=Pair.Value;
 }
 TSharedRef<FJsonObject> PhasesJson(const TMap<FString,double>& Phases)
 {
  auto Object=MakeShared<FJsonObject>();
  for (uint8 I=0;I<uint8(EWarehouseExperimentPhase::Count);++I)
  {
   const FString Key=FWarehouseExperimentMetrics::PhaseKey(EWarehouseExperimentPhase(I));
   Object->SetNumberField(Key,Phases.FindRef(Key));
  }
  return Object;
 }
 FString Csv(const FString& Value)
 {
  return TEXT("\"")+Value.Replace(TEXT("\""),TEXT("\"\""))+TEXT("\"");
 }
 FString Number(double Value) { return FString::Printf(TEXT("%.6f"),Value); }
 FString PhaseColumns()
 {
  FString Out;
  for (uint8 I=0;I<uint8(EWarehouseExperimentPhase::Count);++I)
   Out+=TEXT(",")+FWarehouseExperimentMetrics::PhaseKey(EWarehouseExperimentPhase(I))+TEXT("_seconds");
  return Out;
 }
 FString PhaseValues(const TMap<FString,double>& Phases)
 {
  FString Out;
  for (uint8 I=0;I<uint8(EWarehouseExperimentPhase::Count);++I)
   Out+=TEXT(",")+Number(Phases.FindRef(FWarehouseExperimentMetrics::PhaseKey(EWarehouseExperimentPhase(I))));
  return Out;
 }
}

FWarehouseExperimentMetrics::~FWarehouseExperimentMetrics() { Unregister(); }

void FWarehouseExperimentMetrics::Unregister()
{
 if (Collectors.FindRef(World)==this) Collectors.Remove(World);
}

FWarehouseExperimentMetrics* FWarehouseExperimentMetrics::Find(UWorld* InWorld)
{
 return InWorld ? Collectors.FindRef(InWorld) : nullptr;
}

bool FWarehouseExperimentMetrics::Start(UWorld* InWorld,const FWarehouseExperimentRunInfo& InInfo)
{
 if (bRunning || !IsValid(InWorld) || !InWorld->IsGameWorld() || InInfo.RunId.IsEmpty() ||
     (Find(InWorld) && Find(InWorld)!=this)) return false;
 Unregister();World=InWorld;Info=InInfo;
 StartedUtc=FDateTime::UtcNow().ToIso8601();FinishedUtc.Reset();Status=TEXT("running");
 StartSeconds=InWorld->GetTimeSeconds();LastObservedSeconds=0;EndSeconds=-1;bRunning=true;
 Jobs.Reset();JobObservations.Reset();JobLookup.Reset();VehicleJobIndices.Reset();
 Vehicles.Reset();VehicleObservations.Reset();VehicleLookup.Reset();
 Collectors.Add(World,this);
 TArray<AWarehouseForklift*> Fleet;
 for (TActorIterator<AWarehouseForklift> It(InWorld);It;++It) Fleet.Add(*It);
 Fleet.Sort([](const AWarehouseForklift& A,const AWarehouseForklift& B) { return A.GetName()<B.GetName(); });
 for (auto* Vehicle : Fleet) CaptureVehicle(Vehicle,0.);
 return true;
}

double FWarehouseExperimentMetrics::Now() const
{
 if (EndSeconds>=0) return EndSeconds;
 // GameInstance teardown can outlive its world. Retain the last measured time
 // for interrupted runs, and never rewind if cleanup resets a world clock.
 return World.IsValid() ? FMath::Max(LastObservedSeconds,double(World->GetTimeSeconds())-StartSeconds) : LastObservedSeconds;
}

bool FWarehouseExperimentMetrics::RegisterJob(const FString& JobId,const FString& OrderId,double DueSeconds)
{
 if (!bRunning || JobId.IsEmpty() || JobLookup.Contains(JobId) || !FMath::IsFinite(DueSeconds) || DueSeconds<0) return false;
 FWarehouseExperimentJobMetrics Job;
 Job.JobId=JobId;Job.OrderId=OrderId;Job.DueSeconds=DueSeconds;
 Job.PhaseSeconds=WarehouseExperimentMetricsPrivate::EmptyPhases();
 const int32 Index=Jobs.Add(MoveTemp(Job));JobLookup.Add(JobId,Index);
 FJobObservation Observation;Observation.LastSeconds=DueSeconds;JobObservations.Add(Observation);
 return true;
}

FString FWarehouseExperimentMetrics::PhaseKey(EWarehouseExperimentPhase Phase)
{
 switch (Phase)
 {
  case EWarehouseExperimentPhase::Queue:return TEXT("queue");
  case EWarehouseExperimentPhase::Preparing:return TEXT("preparing");
  case EWarehouseExperimentPhase::EmptyTravel:return TEXT("empty_travel");
  case EWarehouseExperimentPhase::Pickup:return TEXT("pickup");
  case EWarehouseExperimentPhase::LoadedTravel:return TEXT("loaded_travel");
  case EWarehouseExperimentPhase::Unload:return TEXT("unload");
  case EWarehouseExperimentPhase::ElevatorTravel:return TEXT("elevator_travel");
  case EWarehouseExperimentPhase::TrafficWait:return TEXT("traffic_wait");
  case EWarehouseExperimentPhase::ObstacleWait:return TEXT("obstacle_wait");
  case EWarehouseExperimentPhase::SensorWait:return TEXT("sensor_wait");
  case EWarehouseExperimentPhase::ElevatorWait:return TEXT("elevator_wait");
  case EWarehouseExperimentPhase::ChargerWait:return TEXT("charger_wait");
  case EWarehouseExperimentPhase::Charging:return TEXT("charging");
  case EWarehouseExperimentPhase::Paused:return TEXT("paused");
  case EWarehouseExperimentPhase::Fault:return TEXT("fault");
  case EWarehouseExperimentPhase::Recovery:return TEXT("recovery");
  default:return TEXT("idle");
 }
}

FText FWarehouseExperimentMetrics::PhaseLabel(const FString& Key)
{
 static const TMap<FString,FString> Labels={
  {TEXT("queue"),TEXT("배정·작업 순서 대기")},{TEXT("preparing"),TEXT("점검·경로 준비")},
  {TEXT("empty_travel"),TEXT("빈 차 이동")},{TEXT("pickup"),TEXT("팔레트 집기")},
  {TEXT("loaded_travel"),TEXT("적재 이동")},{TEXT("unload"),TEXT("내려놓기")},
  {TEXT("elevator_travel"),TEXT("승강기 탑승·이동")},{TEXT("traffic_wait"),TEXT("교통 양보·대기")},
  {TEXT("obstacle_wait"),TEXT("장애물 대기")},{TEXT("sensor_wait"),TEXT("센서·지도 준비 대기")},
  {TEXT("elevator_wait"),TEXT("승강기 호출·도착 대기")},{TEXT("charger_wait"),TEXT("충전 자리 대기")},
  {TEXT("charging"),TEXT("충전·충전 이동")},{TEXT("paused"),TEXT("정지·수동 조작")},
  {TEXT("fault"),TEXT("고장·복구 필요")},{TEXT("recovery"),TEXT("자동 탈출·재시도")},
  {TEXT("idle"),TEXT("작업 없음·대기 자리 이동")}};
 const FString* Found=Labels.Find(Key);return FText::FromString(Found ? *Found : Key);
}

EWarehouseExperimentPhase FWarehouseExperimentMetrics::Classify(const AWarehouseForklift* V)
{
 using P=EWarehouseExperimentPhase;using S=EWarehouseAIState;
 if (!IsValid(V)) return P::Fault;
 if (V->IsRemoteControlled()) return P::Paused;
 if (V->AIState==S::Fault || V->bMechanicalFailure || V->bEmergencyBlocked) return P::Fault;
 if (V->AIState==S::Paused || V->AIState==S::Off || !V->bPowered) return P::Paused;
 if (V->bExperimentSensorWaiting) return P::SensorWait;
 if (V->bTrafficWaiting || V->bTrafficYielding ||
     (V->DestinationTrafficSeconds>0 && (V->AIState==S::AlignUnload || V->AIState==S::LowerLoad))) return P::TrafficWait;
 switch (V->AIState)
 {
  case S::SelfCheck:case S::ValidateJob:case S::PlanPickup:case S::PlanDelivery:return P::Preparing;
  case S::NavigatePickup:return P::EmptyTravel;
  case S::SlowApproach:case S::DetectPallet:case S::EstimatePose:case S::AlignVehicle:
  case S::CorrectFork:case S::InsertFork:case S::VerifyInsertion:case S::LiftLoad:case S::VerifyLoad:return P::Pickup;
  case S::DepartPickup:case S::TravelHeight:case S::NavigateDelivery:return P::LoadedTravel;
  case S::AlignUnload:case S::LowerLoad:case S::WithdrawFork:case S::VerifyUnload:case S::ReportComplete:return P::Unload;
  case S::ElevatorApproach:return V->bSupportingPallet ? P::LoadedTravel : P::EmptyTravel;
  case S::ElevatorCall:case S::ElevatorWait:return P::ElevatorWait;
  case S::ElevatorBoard:case S::ElevatorRide:case S::ElevatorExit:return P::ElevatorTravel;
  case S::WaitingObstacle:return P::ObstacleWait;
  case S::RecoverLower:case S::RecoverWithdraw:case S::RecoverReplan:return P::Recovery;
  case S::WaitingCharger:return P::ChargerWait;
  case S::PlanCharge:case S::NavigateCharge:case S::DockCharge:case S::Charging:case S::LeaveCharger:return P::Charging;
  default:return P::Idle;
 }
}

void FWarehouseExperimentMetrics::AccountJob(int32 Index,double At)
{
 auto& Job=Jobs[Index];auto& Observation=JobObservations[Index];
 if (Job.IsTerminal()) return;
 // Future releases never accrue negative time. Repeated same-frame events cost zero seconds.
 const double Until=FMath::Max(Observation.LastSeconds,FMath::Max(Job.DueSeconds,At));
 const double Dt=FMath::Max(0.,Until-Observation.LastSeconds);
 Job.PhaseSeconds.FindOrAdd(PhaseKey(Observation.Phase))+=Dt;
 Observation.LastSeconds=Until;Job.ElapsedSeconds=FMath::Max(0.,Until-Job.DueSeconds);
}

void FWarehouseExperimentMetrics::CaptureVehicle(AWarehouseForklift* V,double At)
{
 if (!bRunning || !IsValid(V)) return;
 LastObservedSeconds=FMath::Max(LastObservedSeconds,At);At=LastObservedSeconds;
 int32* Existing=VehicleLookup.Find(V);
 if (!Existing)
 {
  FWarehouseExperimentVehicleMetrics Row;Row.VehicleId=V->GetName();Row.Name=V->VehicleName;
  Row.PhaseSeconds=WarehouseExperimentMetricsPrivate::EmptyPhases();
  const int32 Index=Vehicles.Add(MoveTemp(Row));VehicleLookup.Add(V,Index);
  FVehicleObservation Observation;Observation.Vehicle=V;Observation.LastSeconds=At;
  Observation.LastPosition=V->GetActorLocation();Observation.LastEnergy=V->EnergyConsumedWh;
  Observation.LastRecovery=V->RecoveryCount;Observation.LastRetries=V->ForkInsertionRetries;
  Observation.bLoaded=V->bSupportingPallet;Observation.Phase=Classify(V);
  VehicleObservations.Add(Observation);Existing=VehicleLookup.Find(V);
 }
 auto& Row=Vehicles[*Existing];auto& Observation=VehicleObservations[*Existing];
 const double Dt=FMath::Max(0.,At-Observation.LastSeconds);
 Row.PhaseSeconds.FindOrAdd(PhaseKey(Observation.Phase))+=Dt;
 const double Distance=FVector::Dist2D(Observation.LastPosition,V->GetActorLocation())*.01;
 Row.DistanceMetres+=Distance;
 (Observation.bLoaded ? Row.LoadedDistanceMetres : Row.EmptyDistanceMetres)+=Distance;
 Row.EnergyWh+=FMath::Max(0.f,V->EnergyConsumedWh-Observation.LastEnergy);
 Row.RecoveryAttempts+=FMath::Max(0,V->RecoveryCount-Observation.LastRecovery);
 Row.InsertionRetries+=FMath::Max(0,V->ForkInsertionRetries-Observation.LastRetries);
 Observation.LastSeconds=At;Observation.LastPosition=V->GetActorLocation();Observation.LastEnergy=V->EnergyConsumedWh;
 Observation.LastRecovery=V->RecoveryCount;Observation.LastRetries=V->ForkInsertionRetries;
 Observation.bLoaded=V->bSupportingPallet;Observation.Phase=Classify(V);
 const auto* Assigned=VehicleJobIndices.Find(V);
 if (Assigned) for (int32 I : *Assigned) if (!Jobs[I].IsTerminal())
 {
  AccountJob(I,At);
  // Work waiting behind another order remains queue time, not that other order's travel time.
  JobObservations[I].Phase=V->ActiveJob.JobId==Jobs[I].JobId ? Observation.Phase : EWarehouseExperimentPhase::Queue;
 }
}

void FWarehouseExperimentMetrics::Tick()
{
 if (!bRunning) return;
 const double At=Now();
 LastObservedSeconds=At;
 // No actor scan: known vehicle state and counters only. Native tick hooks capture phase changes.
 for (auto& Observation : VehicleObservations)
  if (auto* V=Observation.Vehicle.Get()) CaptureVehicle(V,At);
 for (int32 I=0;I<Jobs.Num();++I) AccountJob(I,At);
}

void FWarehouseExperimentMetrics::CompleteJob(int32 Index,double At,const FString& Result,const FString& Detail)
{
 if (Jobs[Index].IsTerminal()) return;
 AccountJob(Index,At);auto& Job=Jobs[Index];
 Job.FinishedSeconds=FMath::Max(0.,At);Job.Result=Result;Job.Detail=Detail;
}

void FWarehouseExperimentMetrics::NotifySubmitted(AWarehouseForklift* V,const FString& JobId)
{
 auto* Collector=V ? Find(V->GetWorld()) : nullptr;
 if (!Collector || !Collector->bRunning) return;
 const auto* Index=Collector->JobLookup.Find(JobId);if (!Index) return;
 auto& Job=Collector->Jobs[*Index];if (Job.IsTerminal() || Job.SubmittedSeconds>=0) return;
 const double At=Collector->Now();Collector->AccountJob(*Index,At);
 Job.SubmittedSeconds=At;Job.Result=TEXT("queued");Job.VehicleId=V->GetName();
 Collector->JobObservations[*Index].Vehicle=V;Collector->VehicleJobIndices.FindOrAdd(V).Add(*Index);
 Collector->CaptureVehicle(V,At);
}

void FWarehouseExperimentMetrics::NotifyReport(AWarehouseForklift* V,const FWarehouseJobReport& Report)
{
 auto* Collector=V ? Find(V->GetWorld()) : nullptr;
 if (!Collector || !Collector->bRunning) return;
 // Report.SimulationSeconds is float for the legacy UI. Observe the same event
 // at the world's native precision to avoid repeated rounded timestamp intervals.
 const double At=Collector->Now();
 Collector->CaptureVehicle(V,At);
 Collector->RecordReport(Report,At);
}
void FWarehouseExperimentMetrics::RecordReport(const FWarehouseJobReport& Report,double At)
{
 const auto* Index=JobLookup.Find(Report.JobId);if (!Index) return;
 auto& Job=Jobs[*Index];if (Job.IsTerminal()) return;
 if (Report.Result==TEXT("COMPLETED")) CompleteJob(*Index,At,TEXT("completed"),Report.Detail);
 else if (Report.Result==TEXT("CANCELLED")) CompleteJob(*Index,At,TEXT("cancelled"),Report.Detail);
 else if (Report.Result==TEXT("ACCEPTED"))
 { if (Job.StartedSeconds<0) Job.StartedSeconds=At;Job.Result=TEXT("active"); }
 else if (Report.Result==TEXT("FAULT"))
 { ++Job.FaultEvents;Job.Detail=Report.Detail; } // Recoverable faults are not terminal failures.
}

void FWarehouseExperimentMetrics::NotifyVehicleTick(AWarehouseForklift* V)
{
 if (auto* Collector=V ? Find(V->GetWorld()) : nullptr;Collector && Collector->bRunning)
  Collector->CaptureVehicle(V,Collector->Now());
}

void FWarehouseExperimentMetrics::NotifyCancelled(UWorld* InWorld,const FString& JobId)
{
 if (auto* Collector=Find(InWorld)) Collector->RecordCancelled(JobId,TEXT("관제에서 대기 작업 취소"));
}

void FWarehouseExperimentMetrics::RecordFailure(const FString& JobId,const FString& Detail)
{
 if (!bRunning) return;
 if (const auto* Index=JobLookup.Find(JobId)) CompleteJob(*Index,Now(),TEXT("failed"),Detail);
}

void FWarehouseExperimentMetrics::RecordCancelled(const FString& JobId,const FString& Detail)
{
 if (!bRunning) return;
 if (const auto* Index=JobLookup.Find(JobId)) CompleteJob(*Index,Now(),TEXT("cancelled"),Detail);
}

void FWarehouseExperimentMetrics::Finish(const FString& InStatus)
{
 if (!bRunning) return;
 Tick();EndSeconds=Now();Status=InStatus;FinishedUtc=FDateTime::UtcNow().ToIso8601();
 for (auto& Job : Jobs) if (!Job.IsTerminal())
  Job.Result=Job.DueSeconds>EndSeconds ? TEXT("not_released") : TEXT("unfinished");
 bRunning=false;Unregister();
}

double FWarehouseExperimentMetrics::Percentile(TArray<double> Values,double Probability)
{
 if (Values.IsEmpty()) return 0.;
 Values.Sort();const double Position=FMath::Clamp(Probability,0.,1.)*(Values.Num()-1);
 const int32 Lo=FMath::FloorToInt(Position),Hi=FMath::CeilToInt(Position);
 return FMath::Lerp(Values[Lo],Values[Hi],Position-Lo);
}

FWarehouseExperimentSummary FWarehouseExperimentMetrics::GetSummary() const
{
 FWarehouseExperimentSummary Result;Result.ElapsedSeconds=Now();Result.Planned=Jobs.Num();
 Result.JobPhaseSeconds=WarehouseExperimentMetricsPrivate::EmptyPhases();
 Result.VehiclePhaseSeconds=WarehouseExperimentMetricsPrivate::EmptyPhases();
 TArray<double> CompletedTimes;
 for (const auto& Job : Jobs)
 {
  if (Job.DueSeconds<=Result.ElapsedSeconds) ++Result.Released;
  if (Job.SubmittedSeconds>=0) ++Result.Submitted;
  if (Job.Result==TEXT("completed")) { ++Result.Completed;CompletedTimes.Add(Job.ElapsedSeconds); }
  else if (Job.Result==TEXT("failed")) ++Result.Failed;
  else if (Job.Result==TEXT("cancelled")) ++Result.Cancelled;
  else if (Job.DueSeconds<=Result.ElapsedSeconds) ++Result.Unfinished;
  else ++Result.NotReleased;
  Result.FaultEvents+=Job.FaultEvents;
  WarehouseExperimentMetricsPrivate::AddPhases(Result.JobPhaseSeconds,Job.PhaseSeconds);
 }
 for (const auto& Vehicle : Vehicles) WarehouseExperimentMetricsPrivate::AddPhases(Result.VehiclePhaseSeconds,Vehicle.PhaseSeconds);
 for (double Seconds : CompletedTimes) Result.MeanTransportSeconds+=Seconds;
 if (!CompletedTimes.IsEmpty()) Result.MeanTransportSeconds/=CompletedTimes.Num();
 Result.MedianTransportSeconds=Percentile(CompletedTimes,.5);Result.P95TransportSeconds=Percentile(CompletedTimes,.95);
 if (Result.ElapsedSeconds>0) Result.ThroughputPerHour=3600.*Result.Completed/Result.ElapsedSeconds;
 return Result;
}

TSharedRef<FJsonObject> FWarehouseExperimentMetrics::ToJson() const
{
 using namespace WarehouseExperimentMetricsPrivate;
 auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("schema_version"),1);
 Root->SetStringField(TEXT("run_id"),Info.RunId);Root->SetStringField(TEXT("scenario_id"),Info.ScenarioId);
 Root->SetStringField(TEXT("scenario_fingerprint"),Info.ScenarioFingerprint);Root->SetStringField(TEXT("layout_id"),Info.LayoutId);
 Root->SetStringField(TEXT("layout_fingerprint"),Info.LayoutFingerprint);Root->SetStringField(TEXT("map"),Info.MapPackage);
 Root->SetStringField(TEXT("cargo_fingerprint"),Info.CargoFingerprint);Root->SetStringField(TEXT("runtime_revision"),Info.RuntimeRevision);
 Root->SetNumberField(TEXT("step_seconds"),Info.StepSeconds);Root->SetNumberField(TEXT("physics_substep_seconds"),Info.PhysicsSubstepSeconds);
 Root->SetNumberField(TEXT("duration_seconds"),Info.DurationSeconds);
 Root->SetNumberField(TEXT("repeat_index"),Info.RepeatIndex);Root->SetNumberField(TEXT("seed"),Info.Seed);
 Root->SetNumberField(TEXT("requested_speed"),Info.RequestedSpeed);Root->SetStringField(TEXT("engine_version"),Info.EngineVersion.IsEmpty() ? FEngineVersion::Current().ToString() : Info.EngineVersion);
 Root->SetStringField(TEXT("status"),Status);Root->SetStringField(TEXT("started_utc"),StartedUtc);Root->SetStringField(TEXT("finished_utc"),FinishedUtc);
 Root->SetStringField(TEXT("time_basis"),TEXT("world_simulation_seconds; job elapsed starts at scenario release; completed-only duration statistics; linear interpolated percentiles"));
 Root->SetStringField(TEXT("phase_basis"),TEXT("exclusive previous observed state intervals; no extra sensors/physics queries; job-seconds and vehicle-seconds are distinct and may exceed run duration"));
 const auto Summary=GetSummary();auto S=MakeShared<FJsonObject>();
 S->SetNumberField(TEXT("elapsed_seconds"),Summary.ElapsedSeconds);S->SetNumberField(TEXT("throughput_per_hour"),Summary.ThroughputPerHour);
 S->SetNumberField(TEXT("mean_transport_seconds"),Summary.MeanTransportSeconds);S->SetNumberField(TEXT("median_transport_seconds"),Summary.MedianTransportSeconds);
 S->SetNumberField(TEXT("p95_transport_seconds"),Summary.P95TransportSeconds);S->SetNumberField(TEXT("planned"),Summary.Planned);
 S->SetNumberField(TEXT("released"),Summary.Released);S->SetNumberField(TEXT("submitted"),Summary.Submitted);S->SetNumberField(TEXT("completed"),Summary.Completed);
 S->SetNumberField(TEXT("failed"),Summary.Failed);S->SetNumberField(TEXT("cancelled"),Summary.Cancelled);S->SetNumberField(TEXT("unfinished"),Summary.Unfinished);
 S->SetNumberField(TEXT("not_released"),Summary.NotReleased);S->SetNumberField(TEXT("fault_events"),Summary.FaultEvents);
 S->SetObjectField(TEXT("job_phase_seconds"),PhasesJson(Summary.JobPhaseSeconds));S->SetObjectField(TEXT("vehicle_phase_seconds"),PhasesJson(Summary.VehiclePhaseSeconds));
 Root->SetObjectField(TEXT("summary"),S);
 TArray<TSharedPtr<FJsonValue>> JsonJobs;
 for (const auto& Job : Jobs)
 {
  auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("job_id"),Job.JobId);J->SetStringField(TEXT("order_id"),Job.OrderId);
  J->SetStringField(TEXT("vehicle_id"),Job.VehicleId);J->SetStringField(TEXT("result"),Job.Result);J->SetStringField(TEXT("detail"),Job.Detail);
  J->SetNumberField(TEXT("due_seconds"),Job.DueSeconds);J->SetNumberField(TEXT("submitted_seconds"),Job.SubmittedSeconds);
  J->SetNumberField(TEXT("started_seconds"),Job.StartedSeconds);J->SetNumberField(TEXT("finished_seconds"),Job.FinishedSeconds);
  J->SetNumberField(TEXT("elapsed_seconds"),Job.ElapsedSeconds);J->SetNumberField(TEXT("fault_events"),Job.FaultEvents);
  J->SetObjectField(TEXT("phase_seconds"),PhasesJson(Job.PhaseSeconds));JsonJobs.Add(MakeShared<FJsonValueObject>(J));
 }
 Root->SetArrayField(TEXT("jobs"),JsonJobs);
 TArray<TSharedPtr<FJsonValue>> JsonVehicles;
 for (const auto& Vehicle : Vehicles)
 {
  auto V=MakeShared<FJsonObject>();V->SetStringField(TEXT("vehicle_id"),Vehicle.VehicleId);V->SetStringField(TEXT("name"),Vehicle.Name);
  V->SetNumberField(TEXT("distance_metres"),Vehicle.DistanceMetres);V->SetNumberField(TEXT("empty_distance_metres"),Vehicle.EmptyDistanceMetres);
  V->SetNumberField(TEXT("loaded_distance_metres"),Vehicle.LoadedDistanceMetres);V->SetNumberField(TEXT("energy_wh"),Vehicle.EnergyWh);
  V->SetNumberField(TEXT("recovery_attempts"),Vehicle.RecoveryAttempts);V->SetNumberField(TEXT("insertion_retries"),Vehicle.InsertionRetries);
  V->SetObjectField(TEXT("phase_seconds"),PhasesJson(Vehicle.PhaseSeconds));JsonVehicles.Add(MakeShared<FJsonValueObject>(V));
 }
 Root->SetArrayField(TEXT("vehicles"),JsonVehicles);return Root;
}

bool FWarehouseExperimentMetrics::WriteFiles(const FString& Directory,FString& Error) const
{
 using namespace WarehouseExperimentMetricsPrivate;
 Error.Reset();if (bRunning) { Error=TEXT("실험을 종료한 뒤 결과를 저장하세요.");return false; }
 if (!IFileManager::Get().MakeDirectory(*Directory,true)) { Error=TEXT("실험 결과 폴더를 만들 수 없습니다.");return false; }
 FString Json;auto Writer=TJsonWriterFactory<>::Create(&Json);
 if (!FJsonSerializer::Serialize(ToJson(),Writer)) { Error=TEXT("실험 결과 변환에 실패했습니다.");return false; }
 FString JobsCsv=TEXT("job_id,order_id,vehicle_id,result,due_seconds,submitted_seconds,started_seconds,finished_seconds,elapsed_seconds,fault_events,detail")+PhaseColumns()+TEXT("\n");
 for (const auto& Job : Jobs)
  JobsCsv+=Csv(Job.JobId)+TEXT(",")+Csv(Job.OrderId)+TEXT(",")+Csv(Job.VehicleId)+TEXT(",")+Csv(Job.Result)+TEXT(",")+
   Number(Job.DueSeconds)+TEXT(",")+Number(Job.SubmittedSeconds)+TEXT(",")+Number(Job.StartedSeconds)+TEXT(",")+
   Number(Job.FinishedSeconds)+TEXT(",")+Number(Job.ElapsedSeconds)+TEXT(",")+FString::FromInt(Job.FaultEvents)+TEXT(",")+Csv(Job.Detail)+PhaseValues(Job.PhaseSeconds)+TEXT("\n");
 FString VehiclesCsv=TEXT("vehicle_id,name,distance_metres,empty_distance_metres,loaded_distance_metres,energy_wh,recovery_attempts,insertion_retries")+PhaseColumns()+TEXT("\n");
 for (const auto& V : Vehicles)
  VehiclesCsv+=Csv(V.VehicleId)+TEXT(",")+Csv(V.Name)+TEXT(",")+Number(V.DistanceMetres)+TEXT(",")+Number(V.EmptyDistanceMetres)+TEXT(",")+
   Number(V.LoadedDistanceMetres)+TEXT(",")+Number(V.EnergyWh)+TEXT(",")+FString::FromInt(V.RecoveryAttempts)+TEXT(",")+
   FString::FromInt(V.InsertionRetries)+PhaseValues(V.PhaseSeconds)+TEXT("\n");
 const auto S=GetSummary();
 const FString SummaryCsv=TEXT("run_id,layout_id,layout_fingerprint,scenario_fingerprint,cargo_fingerprint,engine_version,runtime_revision,step_seconds,physics_substep_seconds,duration_seconds,repeat_index,seed,requested_speed,status,elapsed_seconds,planned,released,submitted,completed,failed,cancelled,unfinished,not_released,throughput_per_hour,mean_transport_seconds,median_transport_seconds,p95_transport_seconds,fault_events")+PhaseColumns()+TEXT("\n")+
  Csv(Info.RunId)+TEXT(",")+Csv(Info.LayoutId)+TEXT(",")+Csv(Info.LayoutFingerprint)+TEXT(",")+Csv(Info.ScenarioFingerprint)+TEXT(",")+
  Csv(Info.CargoFingerprint)+TEXT(",")+Csv(Info.EngineVersion.IsEmpty() ? FEngineVersion::Current().ToString() : Info.EngineVersion)+TEXT(",")+
  Csv(Info.RuntimeRevision)+TEXT(",")+Number(Info.StepSeconds)+TEXT(",")+Number(Info.PhysicsSubstepSeconds)+TEXT(",")+Number(Info.DurationSeconds)+TEXT(",")+FString::FromInt(Info.RepeatIndex)+TEXT(",")+
  FString::FromInt(Info.Seed)+TEXT(",")+FString::FromInt(Info.RequestedSpeed)+TEXT(",")+Csv(Status)+TEXT(",")+Number(S.ElapsedSeconds)+TEXT(",")+
  FString::FromInt(S.Planned)+TEXT(",")+FString::FromInt(S.Released)+TEXT(",")+FString::FromInt(S.Submitted)+TEXT(",")+FString::FromInt(S.Completed)+TEXT(",")+
  FString::FromInt(S.Failed)+TEXT(",")+FString::FromInt(S.Cancelled)+TEXT(",")+FString::FromInt(S.Unfinished)+TEXT(",")+FString::FromInt(S.NotReleased)+TEXT(",")+
  Number(S.ThroughputPerHour)+TEXT(",")+Number(S.MeanTransportSeconds)+TEXT(",")+Number(S.MedianTransportSeconds)+TEXT(",")+
  Number(S.P95TransportSeconds)+TEXT(",")+FString::FromInt(S.FaultEvents)+PhaseValues(S.JobPhaseSeconds)+TEXT("\n");
 // Result.json is the completion marker consumed by history; publish it last.
 const TArray<TPair<FString,FString>> Files={{TEXT("Jobs.csv"),JobsCsv},{TEXT("Vehicles.csv"),VehiclesCsv},{TEXT("Summary.csv"),SummaryCsv},{TEXT("Result.json"),Json}};
 // Stage all files before publishing; no existing result is overwritten.
 for (const auto& Pair : Files)
 {
  const FString Path=FPaths::Combine(Directory,Pair.Key);
  if (IFileManager::Get().FileExists(*Path)) { Error=TEXT("동일한 실험 결과가 이미 있습니다. 새 실행 ID가 필요합니다.");return false; }
 }
 for (const auto& Pair : Files)
  if (!FFileHelper::SaveStringToFile(Pair.Value,*FPaths::Combine(Directory,Pair.Key+TEXT(".tmp")),FFileHelper::EEncodingOptions::ForceUTF8))
  { Error=TEXT("실험 결과 파일을 저장할 수 없습니다.");return false; }
 for (const auto& Pair : Files)
  if (!IFileManager::Get().Move(*FPaths::Combine(Directory,Pair.Key),*FPaths::Combine(Directory,Pair.Key+TEXT(".tmp")),false,true))
  { Error=TEXT("실험 결과 파일을 확정할 수 없습니다.");return false; }
 return true;
}

bool FWarehouseExperimentMetrics::ReadSummary(const FString& Directory,FWarehouseExperimentRunInfo& OutInfo,
 FWarehouseExperimentSummary& OutSummary,FString& OutStatus,FString& Error)
{
 Error.Reset();const FString Path=FPaths::Combine(Directory,TEXT("Result.json"));
 const int64 Size=IFileManager::Get().FileSize(*Path);
 if (Size<=0 || Size>32*1024*1024) { Error=TEXT("실험 결과 파일이 없거나 크기가 올바르지 않습니다.");return false; }
 FString Text;TSharedPtr<FJsonObject> Root;
 if (!FFileHelper::LoadFileToString(Text,*Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root) || !Root)
 { Error=TEXT("실험 결과 JSON을 읽을 수 없습니다.");return false; }
 auto Invalid=[&]() { Error=TEXT("실험 결과의 버전·항목·숫자가 올바르지 않습니다.");return false; };
 auto Number=[](const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,double& Value)
 { return Object && Object->TryGetNumberField(Key,Value) && FMath::IsFinite(Value) && Value>=0; };
 auto Count=[&](const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,int32& Value)
 {
  double N=0;if (!Number(Object,Key,N) || N>1000000 || N!=FMath::FloorToDouble(N)) return false;
  Value=int32(N);return true;
 };
 auto String=[](const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,FString& Value)
 { return Object && Object->TryGetStringField(Key,Value) && Value.Len()<=4096; };
 auto Phases=[&](const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,TMap<FString,double>& Values)
 {
  const TSharedPtr<FJsonObject>* P=nullptr;if (!Object->TryGetObjectField(Key,P) || !P || !*P) return false;
  for (uint8 I=0;I<uint8(EWarehouseExperimentPhase::Count);++I)
  {
   const FString Name=PhaseKey(EWarehouseExperimentPhase(I));double Seconds=0;
   if (!Number(*P,*Name,Seconds)) return false;Values.Add(Name,Seconds);
  }
  return true;
 };
 double Version=0;FWarehouseExperimentRunInfo Info;FWarehouseExperimentSummary S;FString Status;
 if (!Number(Root,TEXT("schema_version"),Version) || Version!=1 ||
     !String(Root,TEXT("run_id"),Info.RunId) || Info.RunId.IsEmpty() ||
     !String(Root,TEXT("scenario_id"),Info.ScenarioId) || !String(Root,TEXT("scenario_fingerprint"),Info.ScenarioFingerprint) ||
     !String(Root,TEXT("layout_id"),Info.LayoutId) || !String(Root,TEXT("layout_fingerprint"),Info.LayoutFingerprint) ||
     !String(Root,TEXT("cargo_fingerprint"),Info.CargoFingerprint) || !String(Root,TEXT("map"),Info.MapPackage) ||
     !String(Root,TEXT("engine_version"),Info.EngineVersion) || !String(Root,TEXT("runtime_revision"),Info.RuntimeRevision) ||
     !Number(Root,TEXT("step_seconds"),Info.StepSeconds) || Info.StepSeconds<=0 || Info.StepSeconds>1 ||
     !Number(Root,TEXT("physics_substep_seconds"),Info.PhysicsSubstepSeconds) || Info.PhysicsSubstepSeconds<=0 || Info.PhysicsSubstepSeconds>Info.StepSeconds ||
     !Number(Root,TEXT("duration_seconds"),Info.DurationSeconds) || !Count(Root,TEXT("repeat_index"),Info.RepeatIndex) || Info.RepeatIndex<1 ||
     !Count(Root,TEXT("requested_speed"),Info.RequestedSpeed) || (Info.RequestedSpeed!=1 && Info.RequestedSpeed!=2 && Info.RequestedSpeed!=4) ||
     !String(Root,TEXT("status"),Status) || Status.IsEmpty() || Status==TEXT("running")) return Invalid();
 double Seed=0;
 if (!Root->TryGetNumberField(TEXT("seed"),Seed) || !FMath::IsFinite(Seed) || Seed<MIN_int32 || Seed>MAX_int32 || Seed!=FMath::FloorToDouble(Seed)) return Invalid();
 Info.Seed=int32(Seed);
 const TSharedPtr<FJsonObject>* Summary=nullptr;if (!Root->TryGetObjectField(TEXT("summary"),Summary) || !Summary || !*Summary) return Invalid();
 if (!Number(*Summary,TEXT("elapsed_seconds"),S.ElapsedSeconds) || !Number(*Summary,TEXT("throughput_per_hour"),S.ThroughputPerHour) ||
     !Number(*Summary,TEXT("mean_transport_seconds"),S.MeanTransportSeconds) || !Number(*Summary,TEXT("median_transport_seconds"),S.MedianTransportSeconds) ||
     !Number(*Summary,TEXT("p95_transport_seconds"),S.P95TransportSeconds) || !Count(*Summary,TEXT("planned"),S.Planned) ||
     !Count(*Summary,TEXT("released"),S.Released) || !Count(*Summary,TEXT("submitted"),S.Submitted) || !Count(*Summary,TEXT("completed"),S.Completed) ||
     !Count(*Summary,TEXT("failed"),S.Failed) || !Count(*Summary,TEXT("cancelled"),S.Cancelled) || !Count(*Summary,TEXT("unfinished"),S.Unfinished) ||
     !Count(*Summary,TEXT("not_released"),S.NotReleased) || !Count(*Summary,TEXT("fault_events"),S.FaultEvents) ||
     !Phases(*Summary,TEXT("job_phase_seconds"),S.JobPhaseSeconds) || !Phases(*Summary,TEXT("vehicle_phase_seconds"),S.VehiclePhaseSeconds)) return Invalid();
 if (S.Planned!=S.Completed+S.Failed+S.Cancelled+S.Unfinished+S.NotReleased || S.Released>S.Planned || S.Submitted>S.Planned ||
     S.P95TransportSeconds<S.MedianTransportSeconds ||
     (S.Completed==0 && (S.MeanTransportSeconds!=0 || S.MedianTransportSeconds!=0 || S.P95TransportSeconds!=0)) ||
     !FMath::IsNearlyEqual(S.ThroughputPerHour,S.ElapsedSeconds>0 ? S.Completed*3600./S.ElapsedSeconds : 0.,1.e-6)) return Invalid();
 const TArray<TSharedPtr<FJsonValue>>* JsonJobs=nullptr;
 if (!Root->TryGetArrayField(TEXT("jobs"),JsonJobs) || JsonJobs->Num()!=S.Planned) return Invalid();
 // Results are committed only by the final Result.json rename. CSV availability
 // is not required for reading: a user may retain just the self-contained JSON.
 OutInfo=MoveTemp(Info);OutSummary=MoveTemp(S);OutStatus=MoveTemp(Status);return true;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarehouseExperimentAccountingTest,"Warehouse.Experiments.MetricsAccounting",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWarehouseExperimentAccountingTest::RunTest(const FString& Parameters)
{
 FWarehouseExperimentMetrics M;
 // Pure accounting fixture: explicit simulated timestamps, no actor/physics mutation.
 M.bRunning=true;M.Info.RunId=TEXT("accounting-fixture");M.Status=TEXT("running");
 TestTrue(TEXT("Register completed order"),M.RegisterJob(TEXT("A"),TEXT("order-a"),0));
 TestFalse(TEXT("Reject duplicate ID"),M.RegisterJob(TEXT("A"),TEXT("duplicate"),0));
 TestFalse(TEXT("Reject negative release"),M.RegisterJob(TEXT("negative"),TEXT("negative"),-1));
 M.AccountJob(0,2);M.JobObservations[0].Phase=EWarehouseExperimentPhase::EmptyTravel;
 M.AccountJob(0,5);M.JobObservations[0].Phase=EWarehouseExperimentPhase::Pickup;
 M.AccountJob(0,8);M.JobObservations[0].Phase=EWarehouseExperimentPhase::LoadedTravel;
 M.AccountJob(0,11);M.JobObservations[0].Phase=EWarehouseExperimentPhase::Unload;
 M.CompleteJob(0,13,TEXT("completed"),TEXT("actual unload report"));
 M.AccountJob(0,100); // A duplicate/late terminal callback must never extend the duration.
 TestEqual(TEXT("Completed interval freezes"),M.Jobs[0].ElapsedSeconds,13.);
 TestEqual(TEXT("Queue phase"),M.Jobs[0].PhaseSeconds.FindRef(TEXT("queue")),2.);
 double PhaseSum=0;for (const auto& Pair : M.Jobs[0].PhaseSeconds) PhaseSum+=Pair.Value;
 TestEqual(TEXT("Exclusive phases sum to elapsed"),PhaseSum,M.Jobs[0].ElapsedSeconds);

 M.RegisterJob(TEXT("B"),TEXT("cancelled-order"),2);M.AccountJob(1,5);
 FWarehouseJobReport Cancel;Cancel.JobId=TEXT("B");Cancel.Result=TEXT("CANCELLED");Cancel.Detail=TEXT("building edit");
 M.RecordReport(Cancel,7);
 Cancel.Result=TEXT("COMPLETED");M.RecordReport(Cancel,100);
 TestEqual(TEXT("Cancelled report is terminal despite late completion"),M.Jobs[1].Result,FString(TEXT("cancelled")));
 TestEqual(TEXT("Cancelled report freezes exact end time"),M.Jobs[1].FinishedSeconds,7.);
 M.RegisterJob(TEXT("C"),TEXT("recoverable-fault"),0);
 M.JobObservations[2].Phase=EWarehouseExperimentPhase::Fault;M.Jobs[2].FaultEvents=1;
 M.AccountJob(2,13);
 M.RegisterJob(TEXT("D"),TEXT("future-order"),20);M.AccountJob(3,13);
 M.RegisterJob(TEXT("E"),TEXT("rejected-order"),3);M.CompleteJob(4,9,TEXT("failed"),TEXT("no destination"));
 M.EndSeconds=15;M.Finish(TEXT("time_limit"));
 const auto S=M.GetSummary();
 TestEqual(TEXT("All scenario orders included"),S.Planned,5);
 TestEqual(TEXT("Released orders"),S.Released,4);
 TestEqual(TEXT("Completed count"),S.Completed,1);TestEqual(TEXT("Cancelled count"),S.Cancelled,1);
 TestEqual(TEXT("Failed count"),S.Failed,1);TestEqual(TEXT("Fault is unfinished, not terminal failed"),S.Unfinished,1);
 TestEqual(TEXT("Future order remains separately visible"),S.NotReleased,1);
 TestEqual(TEXT("Future release accrues no time"),M.Jobs[3].ElapsedSeconds,0.);
 TestEqual(TEXT("Cancelled elapsed freezes"),M.Jobs[1].ElapsedSeconds,5.);
 TestEqual(TEXT("Completed-only mean"),S.MeanTransportSeconds,13.);
 TestEqual(TEXT("Run simulation time throughput"),S.ThroughputPerHour,240.);
 TestEqual(TEXT("Fault events preserved"),S.FaultEvents,1);
 M.Tick();M.RecordFailure(TEXT("C"),TEXT("late"));M.Finish(TEXT("late"));
 TestEqual(TEXT("Finished collector immutable"),M.Jobs[2].ElapsedSeconds,15.);
 TestEqual(TEXT("Finished result immutable"),M.GetStatus(),FString(TEXT("time_limit")));
 TestEqual(TEXT("Zero completions percentile"),FWarehouseExperimentMetrics::Percentile({},.95),0.);
 TestEqual(TEXT("Median interpolation"),FWarehouseExperimentMetrics::Percentile({1,2,3,4},.5),2.5);
 TestTrue(TEXT("P95 interpolation"),FMath::IsNearlyEqual(FWarehouseExperimentMetrics::Percentile({1,2,3,4},.95),3.85,1.e-9));
 const auto Json=M.ToJson();
 TestEqual(TEXT("Schema version"),Json->GetNumberField(TEXT("schema_version")),1.);
 TestEqual(TEXT("All outcomes exported"),Json->GetArrayField(TEXT("jobs")).Num(),5);
 TestTrue(TEXT("No fake completion for unresolved fault"),M.Jobs[2].Result==TEXT("unfinished"));
 const FString Directory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Testing/WarehouseExperiments/MetricsAccounting"),FGuid::NewGuid().ToString(EGuidFormats::Digits));
 FString Error;TestTrue(TEXT("Write durable JSON and CSV"),M.WriteFiles(Directory,Error));
 TestFalse(TEXT("Never overwrite an existing run"),M.WriteFiles(Directory,Error));
 FWarehouseExperimentRunInfo LoadedInfo;FWarehouseExperimentSummary Loaded;FString LoadedStatus;
 TestTrue(TEXT("Read durable summary"),FWarehouseExperimentMetrics::ReadSummary(Directory,LoadedInfo,Loaded,LoadedStatus,Error));
 TestEqual(TEXT("Reloaded completion count"),Loaded.Completed,S.Completed);
 TestEqual(TEXT("Reloaded cancelled count"),Loaded.Cancelled,S.Cancelled);
 TestEqual(TEXT("Reloaded unfinished count"),Loaded.Unfinished,S.Unfinished);
 TestEqual(TEXT("Reloaded exclusive job seconds"),Loaded.JobPhaseSeconds.FindRef(TEXT("fault")),15.);
 const FString ResultPath=FPaths::Combine(Directory,TEXT("Result.json"));
 FString ValidText;FFileHelper::LoadFileToString(ValidText,*ResultPath);
 auto Broken=M.ToJson();Broken->GetObjectField(TEXT("summary"))->SetNumberField(TEXT("completed"),-1);
 FString BrokenText;FJsonSerializer::Serialize(Broken,TJsonWriterFactory<>::Create(&BrokenText));
 FFileHelper::SaveStringToFile(BrokenText,*ResultPath,FFileHelper::EEncodingOptions::ForceUTF8);
 TestFalse(TEXT("Reject corrupt result counts"),FWarehouseExperimentMetrics::ReadSummary(Directory,LoadedInfo,Loaded,LoadedStatus,Error));
 TestEqual(TEXT("Invalid read preserves previous valid summary"),Loaded.Completed,1);
 FFileHelper::SaveStringToFile(ValidText,*ResultPath,FFileHelper::EEncodingOptions::ForceUTF8);
 FWarehouseExperimentMetrics LostWorld;LostWorld.bRunning=true;LostWorld.LastObservedSeconds=12.;
 LostWorld.RegisterJob(TEXT("interrupted"),TEXT("interrupted"));LostWorld.Tick();LostWorld.Finish(TEXT("interrupted"));
 TestEqual(TEXT("World cleanup retains measured run time"),LostWorld.GetSummary().ElapsedSeconds,12.);
 TestEqual(TEXT("World cleanup retains unfinished job duration"),LostWorld.Jobs[0].ElapsedSeconds,12.);
 TestEqual(TEXT("World cleanup still counts unresolved order"),LostWorld.GetSummary().Unfinished,1);
 return true;
}
#endif
