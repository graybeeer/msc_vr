#pragma once
#include "CoreMinimal.h"

class AWarehouseForklift;
class UWorld;
class FJsonObject;
struct FWarehouseJobReport;

/** Stable machine keys, also used by persisted JSON/CSV and the comparison screen. */
enum class EWarehouseExperimentPhase : uint8
{
 Queue, Preparing, EmptyTravel, Pickup, LoadedTravel, Unload, ElevatorTravel,
 TrafficWait, ObstacleWait, SensorWait, ElevatorWait, ChargerWait, Charging,
 Paused, Fault, Recovery, Idle, Count
};

struct FWarehouseExperimentRunInfo
{
 FString RunId;
 FString ScenarioId;
 FString ScenarioFingerprint;
 FString LayoutId;
 FString LayoutFingerprint;
 FString CargoFingerprint;
 FString MapPackage;
 FString EngineVersion;
 FString RuntimeRevision;
 double StepSeconds=1./30.;
 double PhysicsSubstepSeconds=1./120.;
 double DurationSeconds=0;
 int32 RepeatIndex=1;
 int32 Seed=0;
 int32 RequestedSpeed=1;
};

struct FWarehouseExperimentJobMetrics
{
 FString JobId;
 FString OrderId;
 FString VehicleId;
 FString Result=TEXT("pending");
 FString Detail;
 double DueSeconds=0;       // All persisted times are seconds relative to this run.
 double SubmittedSeconds=-1;
 double StartedSeconds=-1;
 double FinishedSeconds=-1;
 double ElapsedSeconds=0;
 int32 FaultEvents=0;
 TMap<FString,double> PhaseSeconds;
 bool IsTerminal() const { return FinishedSeconds>=0; }
};

struct FWarehouseExperimentVehicleMetrics
{
 FString VehicleId;
 FString Name;
 TMap<FString,double> PhaseSeconds;
 double DistanceMetres=0;
 double EmptyDistanceMetres=0;
 double LoadedDistanceMetres=0;
 double EnergyWh=0;
 int32 RecoveryAttempts=0;
 int32 InsertionRetries=0;
};

struct FWarehouseExperimentSummary
{
 double ElapsedSeconds=0;
 double ThroughputPerHour=0;
 double MeanTransportSeconds=0;
 double MedianTransportSeconds=0;
 double P95TransportSeconds=0;
 int32 Planned=0;
 int32 Released=0;
 int32 Submitted=0;
 int32 Completed=0;
 int32 Failed=0;
 int32 Cancelled=0;
 int32 Unfinished=0;
 int32 NotReleased=0;
 int32 FaultEvents=0;
 TMap<FString,double> JobPhaseSeconds;
 TMap<FString,double> VehiclePhaseSeconds;
};

/** Read-only observer. No physics, load, route, sensor or authority queries are issued here.
 * One active collector per UWorld; the experiment subsystem owns its lifetime.
 * Register all scenario jobs before releasing them. Unrelated jobs are never counted.
 */
class MSC_VR_API FWarehouseExperimentMetrics
{
public:
 FWarehouseExperimentMetrics()=default;
 ~FWarehouseExperimentMetrics();
 FWarehouseExperimentMetrics(const FWarehouseExperimentMetrics&)=delete;
 FWarehouseExperimentMetrics& operator=(const FWarehouseExperimentMetrics&)=delete;

 bool Start(UWorld* World,const FWarehouseExperimentRunInfo& Info);
 // DueSeconds is relative to Start; a future order accrues no time before its release.
 bool RegisterJob(const FString& JobId,const FString& OrderId,double DueSeconds=0);
 void Tick();
 void Finish(const FString& Status=TEXT("completed"));
 void RecordFailure(const FString& JobId,const FString& Detail);
 void RecordCancelled(const FString& JobId,const FString& Detail);
 bool IsRunning() const { return bRunning; }
 const FWarehouseExperimentRunInfo& GetInfo() const { return Info; }
 const TArray<FWarehouseExperimentJobMetrics>& GetJobs() const { return Jobs; }
 const TArray<FWarehouseExperimentVehicleMetrics>& GetVehicles() const { return Vehicles; }
 const FString& GetStatus() const { return Status; }
 FWarehouseExperimentSummary GetSummary() const;
 TSharedRef<FJsonObject> ToJson() const;
 bool WriteFiles(const FString& Directory,FString& Error) const;
 static bool ReadSummary(const FString& Directory,FWarehouseExperimentRunInfo& Info,
  FWarehouseExperimentSummary& Summary,FString& Status,FString& Error);
 static FString PhaseKey(EWarehouseExperimentPhase Phase);
 static FText PhaseLabel(const FString& Key);
 static double Percentile(TArray<double> Values,double Probability);

 // Called only from existing lifecycle points; these methods never command a vehicle.
 static void NotifySubmitted(AWarehouseForklift* Vehicle,const FString& JobId);
 static void NotifyReport(AWarehouseForklift* Vehicle,const FWarehouseJobReport& Report);
 static void NotifyVehicleTick(AWarehouseForklift* Vehicle);
 static void NotifyCancelled(UWorld* World,const FString& JobId);

private:
 friend class FWarehouseExperimentAccountingTest;
 struct FJobObservation
 {
  TWeakObjectPtr<AWarehouseForklift> Vehicle;
  EWarehouseExperimentPhase Phase=EWarehouseExperimentPhase::Queue;
  double LastSeconds=0;
 };
 struct FVehicleObservation
 {
  TWeakObjectPtr<AWarehouseForklift> Vehicle;
  EWarehouseExperimentPhase Phase=EWarehouseExperimentPhase::Idle;
  double LastSeconds=0;
  FVector LastPosition=FVector::ZeroVector;
  float LastEnergy=0;
  int32 LastRecovery=0,LastRetries=0;
  bool bLoaded=false;
 };
 TWeakObjectPtr<UWorld> World;
 FWarehouseExperimentRunInfo Info;
 FString StartedUtc;
 FString FinishedUtc;
 FString Status=TEXT("idle");
 double StartSeconds=0;
 double LastObservedSeconds=0;
 double EndSeconds=-1;
 bool bRunning=false;
 TArray<FWarehouseExperimentJobMetrics> Jobs;
 TArray<FJobObservation> JobObservations;
 TMap<FString,int32> JobLookup;
 TMap<TWeakObjectPtr<AWarehouseForklift>,TArray<int32>> VehicleJobIndices;
 TArray<FWarehouseExperimentVehicleMetrics> Vehicles;
 TArray<FVehicleObservation> VehicleObservations;
 TMap<TWeakObjectPtr<AWarehouseForklift>,int32> VehicleLookup;
 static TMap<TWeakObjectPtr<UWorld>,FWarehouseExperimentMetrics*> Collectors;
 static FWarehouseExperimentMetrics* Find(UWorld* World);
 static EWarehouseExperimentPhase Classify(const AWarehouseForklift* Vehicle);
 double Now() const;
 void AccountJob(int32 Index,double At);
 void CaptureVehicle(AWarehouseForklift* Vehicle,double At);
 void RecordReport(const FWarehouseJobReport& Report,double At);
 void CompleteJob(int32 Index,double At,const FString& Result,const FString& Detail);
 void Unregister();
};
