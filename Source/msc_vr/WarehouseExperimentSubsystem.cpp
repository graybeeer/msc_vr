#include "WarehouseExperimentSubsystem.h"
#include "WarehouseComputer.h"
#include "WarehouseCargo.h"
#include "WarehousePallet.h"
#include "WarehouseForklift.h"
#include "WarehouseInboundZone.h"
#include "WarehouseShippingSystem.h"
#include "WarehouseLogisticsTruck.h"
#include "WarehouseLayoutEditor.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseSimulationClock.h"
#include "msc_vrPlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/EngineVersion.h"
#include "Modules/ModuleManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonSerializer.h"

namespace
{
 FString JsonText(const TSharedRef<FJsonObject>& Object)
 {
  FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(Object,Writer);return Text;
 }
 FString ExperimentScenarioDigest(const FString& Text)
 {
  FTCHARToUTF8 Utf8(*Text);uint8 Hash[20];FSHA1::HashBuffer(Utf8.Get(),Utf8.Length(),Hash);return BytesToHex(Hash,20);
 }
 FString NewId() { return FGuid::NewGuid().ToString(EGuidFormats::Digits); }
 bool ValidId(const FString& Id) { FGuid Guid;return Id.Len()==32 && FGuid::ParseExact(Id,EGuidFormats::Digits,Guid); }
 FString RootDirectory() { return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("WarehouseExperiments")); }
 FString ScenarioDirectory() { return FPaths::Combine(RootDirectory(),TEXT("Workloads")); }
 bool SaveJson(const FString& Path,const TSharedRef<FJsonObject>& Object)
 {
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
  const FString Temporary=Path+TEXT(".tmp");
  return FFileHelper::SaveStringToFile(JsonText(Object),*Temporary,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
   IFileManager::Get().Move(*Path,*Temporary,true,true);
 }
 bool ReadJson(const FString& Path,TSharedPtr<FJsonObject>& Object)
 {
  if (IFileManager::Get().FileSize(*Path)>32*1024*1024) return false;
  FString Text;return FFileHelper::LoadFileToString(Text,*Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Object);
 }
 FString Csv(FString Value) { return TEXT("\"")+Value.Replace(TEXT("\""),TEXT("\"\""))+TEXT("\""); }
 FString JobId(int32 Index) { return FString::Printf(TEXT("EXPERIMENT-%04d"),Index+1); }
 // The same 120cm work-order reservation used by the ordinary console. This
 // reads published destination poses, not hidden cargo or additional sensors.
 bool DestinationReserved(const FTransform& Destination,const FWarehouseWorkOrder& Existing)
 {
  if (Existing.JobId.IsEmpty() || FMath::Abs(Existing.Destination.GetLocation().Z-Destination.GetLocation().Z)>=20) return false;
  const FVector A=Destination.GetUnitAxis(EAxis::X),B=Destination.GetUnitAxis(EAxis::Y);
  const FVector C=Existing.Destination.GetUnitAxis(EAxis::X),D=Existing.Destination.GetUnitAxis(EAxis::Y);
  const FVector Delta=Existing.Destination.GetLocation()-Destination.GetLocation();
  for (const FVector Axis : {A,B,C,D})
  {
   const double Extent=AWarehouseForklift::FloorSlotHalfExtentCm*(FMath::Abs(FVector::DotProduct(A,Axis))+
    FMath::Abs(FVector::DotProduct(B,Axis))+FMath::Abs(FVector::DotProduct(C,Axis))+FMath::Abs(FVector::DotProduct(D,Axis)));
   if (FMath::Abs(FVector::DotProduct(Delta,Axis))>=Extent) return false;
  }
  return true;
 }
}

void UWarehouseExperimentSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);
 InitWorldHandle=FWorldDelegates::OnWorldInitializedActors.AddUObject(this,&UWarehouseExperimentSubsystem::WorldInitialized);
 ReadResults();
 LastMessage=TEXT("정지한 초기 배치를 저장한 뒤 같은 주문으로 비교하세요. 불러오기·실험 시작은 현재 실행을 새로 시작합니다.");
}
void UWarehouseExperimentSubsystem::Deinitialize()
{
 if (Metrics && Metrics->IsRunning()) EndRun(TEXT("interrupted"));
 FWorldDelegates::OnWorldInitializedActors.Remove(InitWorldHandle);Metrics.Reset();Super::Deinitialize();
}
bool UWarehouseExperimentSubsystem::IsTickable() const { return !IsTemplate() && GetWorld() && GetWorld()->IsGameWorld(); }
TStatId UWarehouseExperimentSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(WarehouseExperiments,STATGROUP_Tickables); }
TArray<WarehouseExperimentStorage::FLayoutInfo> UWarehouseExperimentSubsystem::GetLayouts() const { return WarehouseExperimentStorage::List(); }
FString UWarehouseExperimentSubsystem::GetOutputDirectory() const { return FPaths::ConvertRelativePathToFull(RootDirectory()); }
FWarehouseExperimentSummary UWarehouseExperimentSubsystem::GetLiveSummary() const { return Metrics ? Metrics->GetSummary() : FWarehouseExperimentSummary(); }

TArray<FWarehouseExperimentChoice> UWarehouseExperimentSubsystem::GetOrderSources() const
{
 TArray<FWarehouseExperimentChoice> Out;if (!GetWorld()) return Out;
 TMap<AWarehousePallet*,FString> Descriptions;
 TMap<AWarehousePallet*,int32> Counts;
 for (TActorIterator<AWarehouseComputer> It(GetWorld());It;++It)
  for (const auto& Item:It->Inventory) if (IsValid(Item.Pallet))
  {
   FString& Description=Descriptions.FindOrAdd(Item.Pallet);
   if (Description.IsEmpty()) Description=Item.Location+TEXT(" · ")+Item.Product.Left(30)+TEXT(" ")+Item.CargoId;
   Counts.FindOrAdd(Item.Pallet)++;
  }
 for (TActorIterator<AWarehousePallet> It(GetWorld());It;++It)
 {
  if (It->ActorHasTag(TEXT("LogisticsInTransit"))) continue;
  const FString Id=WarehouseExperimentStorage::StableActorId(*It);
  const FString Description=Descriptions.FindRef(*It);
  Out.Add({Id,Description.IsEmpty() ? It->GetActorNameOrLabel()+TEXT(" · 빈 팔레트") : Description+FString::Printf(TEXT(" 외 %d개"),FMath::Max(0,Counts.FindRef(*It)-1))});
 }
 Out.Sort([](const auto& A,const auto& B) { return A.Id<B.Id; });return Out;
}
TArray<UWarehouseExperimentSubsystem::FDestination> UWarehouseExperimentSubsystem::Destinations() const
{
 TArray<FDestination> Out;if (!GetWorld()) return Out;TSet<FString> Seen;
 for (TActorIterator<AWarehouseInboundZone> It(GetWorld());It;++It) for (const auto& Slot:It->RackSlots)
 {
  if (!IsValid(Slot.Rack) || Slot.Name.IsEmpty()) continue;
  const FString Id=TEXT("rack:")+WarehouseExperimentStorage::StableActorId(Slot.Rack)+TEXT(":")+Slot.Name;
  if (!Seen.Contains(Id)) { Seen.Add(Id);Out.Add({Id,TEXT("랙 · ")+Slot.Name,Slot.Pose,Slot.Rack,true}); }
 }
 for (TActorIterator<AWarehouseShippingSystem> It(GetWorld());It;++It) for (const auto& Zone:It->Zones)
 {
  if (!IsValid(Zone.Anchor)) continue;
  for (int32 I=0;I<Zone.Slots.Num();++I)
  {
   const FString Id=TEXT("zone:")+Zone.ZoneId+FString::Printf(TEXT(":%d"),I);
   if (!Seen.Contains(Id)) { Seen.Add(Id);Out.Add({Id,FString::Printf(TEXT("%s · %s 자리 %d"),Zone.bInbound ? TEXT("입고") : TEXT("출고"),*Zone.ZoneId,I+1),Zone.Slots[I]*Zone.Anchor->GetActorTransform(),Zone.Anchor,false}); }
  }
 }
 // Older maps without a shipping system retain their authored console destinations.
 if (!Out.ContainsByPredicate([](const auto& D){return !D.bRack;}))
  for (TActorIterator<AWarehouseComputer> It(GetWorld());It;++It) for (int32 I=0;I<It->OutboundDestinations.Num();++I)
  {
   const FString Id=TEXT("console:")+WarehouseExperimentStorage::StableActorId(*It)+FString::Printf(TEXT(":%d"),I);
   Out.Add({Id,FString::Printf(TEXT("출고 자리 %d"),I+1),It->OutboundDestinations[I],*It,false});
  }
 Out.Sort([](const auto& A,const auto& B){return A.Id<B.Id;});return Out;
}
TArray<FWarehouseExperimentChoice> UWarehouseExperimentSubsystem::GetDestinations() const
{
 TArray<FWarehouseExperimentChoice> Out;for(const auto& D:Destinations()) Out.Add({D.Id,D.Label});return Out;
}
bool UWarehouseExperimentSubsystem::SaveLayout(const FString& Name)
{
 if (bBatchRunning || bLoading) { LastMessage=TEXT("진행 중에는 초기 배치를 저장할 수 없습니다.");return false; }
 if (GetLayouts().Num()>=MaxSavedLayouts) { LastMessage=FString::Printf(TEXT("저장 배치 %d개가 모두 찼습니다. 기존 배치를 덮어쓰거나 삭제하세요."),MaxSavedLayouts);return false; }
 TSharedPtr<FJsonObject> Snapshot;WarehouseExperimentStorage::FLayoutInfo Info;
 if (!WarehouseExperimentStorage::Capture(GetWorld(),Name,Snapshot,LastMessage) || !WarehouseExperimentStorage::Write(Snapshot,Info,LastMessage)) return false;
 LastSavedLayoutId=Info.Id;LastMessage=TEXT("초기 배치 저장 완료: ")+Info.Name;return true;
}
bool UWarehouseExperimentSubsystem::ReplaceLayout(const FString& Id,const FString& Name)
{
 if (bBatchRunning || bLoading) { LastMessage=TEXT("진행 중에는 저장 배치를 덮어쓸 수 없습니다.");return false; }
 const auto Layouts=GetLayouts();const auto* Existing=Layouts.FindByPredicate([&](const auto& Layout) { return Layout.Id==Id; });
 if (!Existing) { LastMessage=TEXT("덮어쓸 저장 배치를 선택하세요.");return false; }
 TSharedPtr<FJsonObject> Snapshot;WarehouseExperimentStorage::FLayoutInfo Info;
 const FString NewName=Name.TrimStartAndEnd().IsEmpty() ? Existing->Name : Name;
 if (!WarehouseExperimentStorage::Capture(GetWorld(),NewName,Snapshot,LastMessage) ||
     !WarehouseExperimentStorage::Write(Snapshot,Info,LastMessage,Id)) return false;
 for (FString& Selected:SelectedLayoutIds) if (Selected==Id) Selected=Info.Id;
 LastSavedLayoutId=Info.Id;LastMessage=TEXT("저장 배치 덮어쓰기 완료: ")+Info.Name;return true;
}
bool UWarehouseExperimentSubsystem::RemoveLayout(const FString& Id)
{
 if (bBatchRunning || bLoading) { LastMessage=TEXT("진행 중에는 저장 배치를 삭제할 수 없습니다.");return false; }
 if (!WarehouseExperimentStorage::Archive(Id,LastMessage)) return false;
 SelectedLayoutIds.Remove(Id);if (LastSavedLayoutId==Id) LastSavedLayoutId.Reset();
 LastMessage=TEXT("저장 배치를 목록에서 삭제했습니다. 현재 맵과 이전 실험 결과는 유지됩니다.");return true;
}
bool UWarehouseExperimentSubsystem::QueueLoad(const FString& Id)
{
 TSharedPtr<FJsonObject> Snapshot;
 if (!WarehouseExperimentStorage::Read(Id,Snapshot,LastMessage) || !WarehouseExperimentStorage::Validate(GetWorld(),Snapshot,LastMessage)) return false;
 ActiveLayout=WarehouseExperimentStorage::Describe(Snapshot);BaseMap=ActiveLayout.MapPackage;
 PendingSnapshot=Snapshot;bLoading=true;bPrepared=false;PrepareFrames=0;AppliedWorld.Reset();bOpenPanel=true;
 UGameplayStatics::OpenLevel(GetWorld(),FName(*BaseMap));return true;
}
bool UWarehouseExperimentSubsystem::LoadLayout(const FString& Id)
{
 if (bBatchRunning || bLoading) { LastMessage=TEXT("진행 중인 실험을 먼저 중지하세요.");return false; }
 LastMessage=TEXT("저장 배치를 새 실행에 복원 중…");return QueueLoad(Id);
}
bool UWarehouseExperimentSubsystem::AddOrder(const FString& PalletId,const FString& DestinationId,double ReleaseSeconds,const FString& VehicleId)
{
 if (bBatchRunning || bLoading || Orders.Num()>=500 || !FMath::IsFinite(ReleaseSeconds) || ReleaseSeconds<0 || ReleaseSeconds>86400 ||
     !GetOrderSources().ContainsByPredicate([&](const auto& P){return P.Id==PalletId;}) ||
     !Destinations().ContainsByPredicate([&](const auto& D){return D.Id==DestinationId;}))
 { LastMessage=TEXT("유효한 팔레트·목적지·투입 시각(0~86400초)을 선택하세요. 최대 500개입니다.");return false; }
 if (!VehicleId.IsEmpty() && !Cast<AWarehouseForklift>(WarehouseExperimentStorage::ResolveActorId(GetWorld(),VehicleId))) return false;
 FWarehouseExperimentOrder Order;Order.OrderId=NewId();Order.PalletId=PalletId;Order.DestinationId=DestinationId;Order.ReleaseSeconds=ReleaseSeconds;Order.VehicleId=VehicleId;Orders.Add(Order);
 LastMessage=TEXT("공통 주문에 추가했습니다. 동일 팔레트의 후속 작업은 앞 작업이 끝난 뒤 배정됩니다.");return true;
}
bool UWarehouseExperimentSubsystem::RemoveOrder(int32 Index)
{ if (bBatchRunning || bLoading || !Orders.IsValidIndex(Index)) return false;Orders.RemoveAt(Index);return true; }
void UWarehouseExperimentSubsystem::ClearOrders() { if (!bBatchRunning && !bLoading) Orders.Reset(); }
TSharedRef<FJsonObject> UWarehouseExperimentSubsystem::WorkloadJson(const FString& Name) const
{
 auto Object=MakeShared<FJsonObject>();Object->SetNumberField(TEXT("version"),1);Object->SetStringField(TEXT("name"),Name);
 TArray<TSharedPtr<FJsonValue>> Values;
 for (const auto& Order:Orders)
 {
  auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("order"),Order.OrderId);O->SetStringField(TEXT("pallet"),Order.PalletId);
  O->SetStringField(TEXT("destination"),Order.DestinationId);O->SetStringField(TEXT("vehicle"),Order.VehicleId);O->SetNumberField(TEXT("release_seconds"),Order.ReleaseSeconds);
  Values.Add(MakeShared<FJsonValueObject>(O));
 }
 Object->SetArrayField(TEXT("orders"),Values);return Object;
}
bool UWarehouseExperimentSubsystem::SaveWorkload(const FString& Name)
{
 if (Orders.IsEmpty() || bBatchRunning || bLoading || Name.TrimStartAndEnd().IsEmpty()) { LastMessage=TEXT("이름과 주문을 입력하세요.");return false; }
 const FString Id=NewId();auto Object=WorkloadJson(Name.Left(80));Object->SetStringField(TEXT("id"),Id);
 if (!SaveJson(FPaths::Combine(ScenarioDirectory(),Id+TEXT(".json")),Object)) { LastMessage=TEXT("주문 파일 저장 실패");return false; }
 WorkloadId=Id;LastMessage=TEXT("공통 주문 저장 완료");return true;
}
TArray<FWarehouseExperimentChoice> UWarehouseExperimentSubsystem::GetWorkloads() const
{
 TArray<FString> Files;IFileManager::Get().FindFiles(Files,*(ScenarioDirectory()/TEXT("*.json")),true,false);
 Files.Sort();TArray<FWarehouseExperimentChoice> Out;
 for (const auto& File:Files)
 {
  TSharedPtr<FJsonObject> Object;FString Name;const FString Id=FPaths::GetBaseFilename(File);
  if (ValidId(Id) && ReadJson(ScenarioDirectory()/File,Object) && Object->TryGetStringField(TEXT("name"),Name)) Out.Add({Id,Name});
 }
 return Out;
}
bool UWarehouseExperimentSubsystem::LoadWorkload(const FString& Id)
{
 if (bBatchRunning || bLoading || !ValidId(Id)) return false;
 TSharedPtr<FJsonObject> Object;const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
 if (!ReadJson(ScenarioDirectory()/(Id+TEXT(".json")),Object) || !Object->TryGetArrayField(TEXT("orders"),Values) || Values->IsEmpty() || Values->Num()>500)
 { LastMessage=TEXT("주문 파일이 없거나 형식이 올바르지 않습니다.");return false; }
 TArray<FWarehouseExperimentOrder> Loaded;TSet<FString> Ids;
 for (const auto& Value:*Values)
 {
  const TSharedPtr<FJsonObject>* O=nullptr;FWarehouseExperimentOrder Order;
  if (!Value->TryGetObject(O) || !(*O)->TryGetStringField(TEXT("order"),Order.OrderId) || !(*O)->TryGetStringField(TEXT("pallet"),Order.PalletId) ||
      !(*O)->TryGetStringField(TEXT("destination"),Order.DestinationId) || !(*O)->TryGetStringField(TEXT("vehicle"),Order.VehicleId) ||
      !(*O)->TryGetNumberField(TEXT("release_seconds"),Order.ReleaseSeconds) || !FMath::IsFinite(Order.ReleaseSeconds) || Order.ReleaseSeconds<0 || Order.ReleaseSeconds>86400 ||
      Order.OrderId.IsEmpty() || Order.PalletId.IsEmpty() || Order.DestinationId.IsEmpty() || Ids.Contains(Order.OrderId))
  { LastMessage=TEXT("주문 파일의 항목이 올바르지 않습니다. 기존 주문은 유지됩니다.");return false; }
  Loaded.Add(Order);Ids.Add(Order.OrderId);
 }
 Orders=MoveTemp(Loaded);WorkloadId=Id;LastMessage=TEXT("공통 주문 불러오기 완료");return true;
}
bool UWarehouseExperimentSubsystem::CheckWorkload(const TArray<FString>& LayoutIds,FString& Error)
{
 if (LayoutIds.IsEmpty() || Orders.IsEmpty()) { Error=TEXT("저장 배치와 공통 주문을 선택하세요.");return false; }
 FString CargoHash;TSet<FString> Unique;
 for (const FString& Id:LayoutIds)
 {
  TSharedPtr<FJsonObject> Snapshot;if (Unique.Contains(Id) || !WarehouseExperimentStorage::Read(Id,Snapshot,Error) || !WarehouseExperimentStorage::Validate(GetWorld(),Snapshot,Error)) return false;
  Unique.Add(Id);const auto Info=WarehouseExperimentStorage::Describe(Snapshot);
  if (CargoHash.IsEmpty()) CargoHash=Info.CargoFingerprint;
  else if (CargoHash!=Info.CargoFingerprint) { Error=TEXT("선택 배치의 물류 ID·품목·질량·크기 조건이 다릅니다. 같은 초기 물류로 저장하세요.");return false; }
 }
 return true;
}
bool UWarehouseExperimentSubsystem::StartBatch(const TArray<FString>& LayoutIds,int32 Repeats,double DurationSeconds,int32 InSeed,int32 InSpeed)
{
 if (bBatchRunning || bLoading) return false;
 if (Repeats<1 || Repeats>30 || LayoutIds.Num()*Repeats>100 || !FMath::IsFinite(DurationSeconds) || DurationSeconds<10 || DurationSeconds>86400 || (InSpeed!=1 && InSpeed!=2 && InSpeed!=4))
 { LastMessage=TEXT("반복 1~30회, 총 100회 이하, 실행 10~86400초, 배속 1/2/4를 사용하세요.");return false; }
 if (!CheckWorkload(LayoutIds,LastMessage)) return false;
 Seed=InSeed;Speed=InSpeed;Duration=DurationSeconds;RunOrders=Orders;
 // Keep the user's per-pallet sequence before randomizing independent ties.
 RunOrders.StableSort([](const auto& A,const auto& B){return A.ReleaseSeconds<B.ReleaseSeconds;});
 TMap<FString,FString> PreviousByOrder,LastByPallet;
 for (const auto& Order:RunOrders)
 {
  if (const auto* Previous=LastByPallet.Find(Order.PalletId)) PreviousByOrder.Add(Order.OrderId,*Previous);
  LastByPallet.Add(Order.PalletId,Order.OrderId);
 }
 // Stable order submission; the seed resolves otherwise equal release-time ordering.
 FRandomStream Random(Seed);for (int32 I=RunOrders.Num()-1;I>0;--I) RunOrders.Swap(I,Random.RandRange(0,I));
 RunOrders.StableSort([](const auto& A,const auto& B){return A.ReleaseSeconds<B.ReleaseSeconds;});
 TMap<FString,int32> OrderIndices;for(int32 I=0;I<RunOrders.Num();++I) OrderIndices.Add(RunOrders[I].OrderId,I);
 OrderPredecessors.Init(INDEX_NONE,RunOrders.Num());
 for(int32 I=0;I<RunOrders.Num();++I) if(const auto* Previous=PreviousByOrder.Find(RunOrders[I].OrderId))
  OrderPredecessors[I]=OrderIndices.FindChecked(*Previous);
 BatchId=NewId();WorkloadHash=ExperimentScenarioDigest(JsonText(WorkloadJson(TEXT("common"))));
 if (WorkloadId.IsEmpty()) WorkloadId=WorkloadHash.Left(32);
 auto Manifest=WorkloadJson(TEXT("Batch conditions"));Manifest->SetStringField(TEXT("batch_id"),BatchId);Manifest->SetStringField(TEXT("workload_hash"),WorkloadHash);
 Manifest->SetNumberField(TEXT("seed"),Seed);Manifest->SetNumberField(TEXT("speed"),Speed);Manifest->SetNumberField(TEXT("duration_seconds"),Duration);
 Manifest->SetNumberField(TEXT("repeats"),Repeats);TArray<TSharedPtr<FJsonValue>> LayoutValues;
 for (const auto& Id:LayoutIds) LayoutValues.Add(MakeShared<FJsonValueString>(Id));Manifest->SetArrayField(TEXT("layouts"),LayoutValues);
 if (!SaveJson(RootDirectory()/TEXT("Batches")/BatchId/TEXT("Conditions.json"),Manifest)) { LastMessage=TEXT("실험 조건 기록 실패");return false; }
 Runs.Reset();for (int32 R=1;R<=Repeats;++R) for (const auto& Id:LayoutIds) Runs.Add({Id,R});
 CurrentRun=0;TotalRuns=Runs.Num();bBatchRunning=true;bStartNext=true;LastMessage=TEXT("같은 물류·주문으로 반복 실험을 시작합니다.");return true;
}
void UWarehouseExperimentSubsystem::DisableUnscheduledWork(UWorld* World)
{
 // Controlled workload owns releases. Native physics, sensors, traffic and equipment keep ticking.
 for (TActorIterator<AWarehouseInboundZone> It(World);It;++It) It->bExternalDispatchOnly=true;
 auto Pause=[&](AActor* A) { if (!PreviousTicks.Contains(A)) PreviousTicks.Add(A,A->IsActorTickEnabled());A->SetActorTickEnabled(false); };
 for (TActorIterator<AWarehouseShippingSystem> It(World);It;++It) Pause(*It);
 for (TActorIterator<AWarehouseLogisticsTruck> It(World);It;++It) Pause(*It);
}
void UWarehouseExperimentSubsystem::RestoreUnscheduledWork()
{
 for (const auto& Pair:PreviousTicks) if (Pair.Key.IsValid()) Pair.Key->SetActorTickEnabled(Pair.Value);PreviousTicks.Reset();
 if(GetWorld()) for (TActorIterator<AWarehouseInboundZone> It(GetWorld());It;++It) It->bExternalDispatchOnly=false;
}
void UWarehouseExperimentSubsystem::WorldInitialized(const UWorld::FActorsInitializedParams& Params)
{
 UWorld* World=Params.World;if (!World || World->GetGameInstance()!=GetGameInstance() || !World->IsGameWorld()) return;
 if (!PendingSnapshot.IsValid()) return;
 AppliedWorld=World;
 PreviousTicks.Reset();
 RestoreAutoStart.Reset();
 if (!WarehouseExperimentStorage::ApplyBeforeBeginPlay(World,PendingSnapshot,RestoreError))
 {
  DisableUnscheduledWork(World);for(TActorIterator<AWarehouseForklift> It(World);It;++It) { It->bAutoStart=false;It->SetActorTickEnabled(false); }
  bNeedRollback=true;return;
 }
 // Loading an initial layout must not schedule new work before the user can edit it.
 DisableUnscheduledWork(World);
 for(TActorIterator<AWarehouseForklift> It(World);It;++It)
 {
  RestoreAutoStart.Add(*It,It->bAutoStart);It->bAutoStart=false;
 }
 bPrepared=true;PrepareFrames=0;PrepareTime=0;
}
void UWarehouseExperimentSubsystem::BeginNextRun()
{
 bStartNext=false;
 if (!Runs.IsValidIndex(CurrentRun)) { bBatchRunning=false;LastMessage=TEXT("반복 실험 완료. 실행별 성과와 미완료·대기 원인을 비교하세요.");bOpenPanel=true;return; }
 if (!QueueLoad(Runs[CurrentRun].LayoutId)) { bBatchRunning=false;RestoreUnscheduledWork();LastMessage=TEXT("실험 배치 불러오기 실패: ")+LastMessage;return; }
 ++CurrentRun;LastMessage=FString::Printf(TEXT("실험 %d/%d · 초기 배치 복원 중"),CurrentRun,TotalRuns);
}
void UWarehouseExperimentSubsystem::BeginRun()
{
 Metrics=MakeUnique<FWarehouseExperimentMetrics>();FWarehouseExperimentRunInfo Info;
 Info.RunId=NewId();Info.ScenarioId=WorkloadId;Info.ScenarioFingerprint=WorkloadHash;Info.LayoutId=ActiveLayout.Id;Info.LayoutFingerprint=ActiveLayout.Hash;
 Info.MapPackage=ActiveLayout.MapPackage;Info.RepeatIndex=Runs[CurrentRun-1].Repeat;Info.Seed=Seed;Info.RequestedSpeed=Speed;
 Info.DurationSeconds=Duration;Info.CargoFingerprint=ActiveLayout.CargoFingerprint;Info.EngineVersion=FEngineVersion::Current().ToString();
 Info.RuntimeRevision=LexToString(FMD5Hash::HashFile(*FModuleManager::Get().GetModuleFilename(TEXT("msc_vr"))));
 if (!Metrics->Start(GetWorld(),Info)) { LastMessage=TEXT("지표 기록 시작 실패");bBatchRunning=false;RestoreUnscheduledWork();return; }
 Released.Init(false,RunOrders.Num());for(int32 I=0;I<RunOrders.Num();++I) Metrics->RegisterJob(JobId(I),RunOrders[I].OrderId,RunOrders[I].ReleaseSeconds);
 RunWallStart=FPlatformTime::Seconds();NextDispatch=0;
 auto Available=Destinations();bool bInvalid=false;RunPallets.Reset();
 TMap<FString,AWarehousePallet*> Pallets;
 for(TActorIterator<AWarehousePallet> It(GetWorld());It;++It) Pallets.Add(WarehouseExperimentStorage::StableActorId(*It),*It);
 for (int32 I=0;I<RunOrders.Num();++I)
 {
  RunPallets.Add(Pallets.FindRef(RunOrders[I].PalletId));
  if (!RunPallets.Last().IsValid() ||
      !Available.ContainsByPredicate([&](const auto& D){return D.Id==RunOrders[I].DestinationId;}) ||
      (!RunOrders[I].VehicleId.IsEmpty() && !Cast<AWarehouseForklift>(WarehouseExperimentStorage::ResolveActorId(GetWorld(),RunOrders[I].VehicleId))))
  { Metrics->RecordFailure(JobId(I),TEXT("저장 배치에 주문의 팔레트·목적지·지정 차량이 없습니다."));Released[I]=true;bInvalid=true; }
 }
 if (bInvalid) { EndRun(TEXT("invalid_conditions"));return; }
 for(TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) if (!It->bPowered) It->TogglePower();
 LastMessage=FString::Printf(TEXT("실험 %d/%d · %s · %.0f초 관찰"),CurrentRun,TotalRuns,*ActiveLayout.Name,Duration);
}
void UWarehouseExperimentSubsystem::DispatchOrders()
{
 const double Now=Metrics->GetSummary().ElapsedSeconds;if (Now<NextDispatch) return;NextDispatch=Now+.25;
 const auto Available=Destinations();TArray<AWarehouseForklift*> Fleet;
 for(TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) Fleet.Add(*It);
 Fleet.Sort([](const auto& A,const auto& B){return WarehouseExperimentStorage::StableActorId(&A)<WarehouseExperimentStorage::StableActorId(&B);});
 for (int32 I=0;I<RunOrders.Num();++I)
 {
  const auto& Order=RunOrders[I];if (Released[I] || Order.ReleaseSeconds>Now) continue;
  // A later instruction for the same pallet cannot overtake its earlier instruction.
  const int32 Earlier=OrderPredecessors[I];
  if (Earlier!=INDEX_NONE && !Metrics->GetJobs()[Earlier].IsTerminal()) continue;
  auto* Pallet=RunPallets[I].Get();
  const auto* Destination=Available.FindByPredicate([&](const auto& D){return D.Id==Order.DestinationId;});
  if (!Pallet || !Destination || !Destination->Resource.IsValid())
  { Metrics->RecordFailure(JobId(I),TEXT("필수 팔레트 또는 목적지가 사라졌습니다."));Released[I]=true;continue; }
  bool Claimed=false;for (const auto* V:Fleet)
   if (V->ActiveJob.Pallet==Pallet || V->PendingJobs.ContainsByPredicate([&](const auto& J){return J.Pallet==Pallet;})) { Claimed=true;break; }
  if (Claimed) continue;
  bool DestinationBusy=false;
  for (const auto* V:Fleet)
   if (DestinationReserved(Destination->Pose,V->ActiveJob) ||
       V->PendingJobs.ContainsByPredicate([&](const auto& J){return DestinationReserved(Destination->Pose,J);}))
   { DestinationBusy=true;break; }
  if (DestinationBusy) continue;
  AWarehouseForklift* Vehicle=nullptr;double Best=TNumericLimits<double>::Max();
  for (auto* V:Fleet)
  {
   if (!Order.VehicleId.IsEmpty() && WarehouseExperimentStorage::StableActorId(V)!=Order.VehicleId) continue;
   if (!V->CanAcceptNewWork() || !V->ActiveJob.JobId.IsEmpty() || !V->PendingJobs.IsEmpty()) continue;
   const double Cost=FVector::DistSquared2D(V->GetActorLocation(),Pallet->GetActorLocation());
   if (Cost<Best) { Vehicle=V;Best=Cost; }
  }
  if (!Vehicle) continue;
  FWarehouseWorkOrder Job;Job.JobId=JobId(I);Job.SourceSystem=TEXT("FMS-EXPERIMENT");Job.Pallet=Pallet;
  Job.Destination=Destination->Pose;Job.DestinationResource=Destination->Resource.Get();Job.bRequiresDestinationResource=true;Job.bRackDestination=Destination->bRack;
  // Existing validation and state machine keep ownership of load, collision, lift and route decisions.
  if (Vehicle->SubmitJob(Job)) Released[I]=true;
  else
  {
   const FString ResourceError=Vehicle->ValidateJobResources(Job);
   // NPC inspection remains part of the measured queue time, including its finite wrapping delay.
   if (!ResourceError.IsEmpty() && !ResourceError.Contains(TEXT("INBOUND_PENDING")))
   { Metrics->RecordFailure(JobId(I),ResourceError);Released[I]=true; }
  }
 }
}
bool UWarehouseExperimentSubsystem::EndRun(const FString& Status)
{
 if (!Metrics || !Metrics->IsRunning()) return false;
 Metrics->Finish(Status);FWarehouseExperimentResult Result;Result.Info=Metrics->GetInfo();Result.Summary=Metrics->GetSummary();
 Result.LayoutName=ActiveLayout.Name;Result.Status=Status;Result.AchievedSpeed=Result.Summary.ElapsedSeconds/FMath::Max(.001,FPlatformTime::Seconds()-RunWallStart);
 FString Error;const FString Dir=RootDirectory()/TEXT("Runs")/Result.Info.RunId;
 auto Metadata=MakeShared<FJsonObject>();Metadata->SetStringField(TEXT("layout_name"),Result.LayoutName);Metadata->SetStringField(TEXT("batch_id"),BatchId);
 Metadata->SetStringField(TEXT("cargo_fingerprint"),ActiveLayout.CargoFingerprint);Metadata->SetNumberField(TEXT("achieved_speed"),Result.AchievedSpeed);Metadata->SetNumberField(TEXT("duration_seconds"),Duration);
 Metadata->SetStringField(TEXT("interpretation"),TEXT("Fixed-step physical simulation; seed fixes order ties, not bit-identical Chaos outcomes. Compare identical workload/cargo/runtime conditions. Interrupted/invalid runs are not full-duration trials."));
 const bool ConditionsSaved=SaveJson(Dir/TEXT("Conditions.json"),Metadata);
 if (!ConditionsSaved) Error=TEXT("실행 조건 파일을 저장할 수 없습니다.");
 const bool Saved=ConditionsSaved && Metrics->WriteFiles(Dir,Error);
 Results.Add(Result);const bool IndexSaved=WriteIndex();
 if (!Saved) { LastMessage=TEXT("결과 파일 기록 실패(화면에는 이번 세션 기록만 남음): ")+Error;bBatchRunning=false; }
 else if (!IndexSaved) LastMessage=TEXT("회별 결과는 저장했지만 비교 CSV를 내보내지 못했습니다. 결과 화면에서 다시 내보내세요.");
 if (GetWorld()) for(TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) if (It->bPowered) It->TogglePower();
 if (!bBatchRunning || CurrentRun>=TotalRuns) RestoreUnscheduledWork();
 bStartNext=bBatchRunning;
 return Saved && IndexSaved;
}
void UWarehouseExperimentSubsystem::StopBatch()
{
 bBatchRunning=false;bStartNext=false;
 const bool HadActiveRun=Metrics && Metrics->IsRunning();
 const bool Saved=HadActiveRun && EndRun(TEXT("stopped"));
 if (!bLoading) RestoreUnscheduledWork();
 if (Saved) LastMessage=TEXT("실험 중지. 완료·미완료 작업과 중지 시점까지의 지표를 저장했습니다.");
 else if (!HadActiveRun) LastMessage=TEXT("반복 실험을 중지했습니다. 이미 완료한 실행 기록은 유지됩니다.");
}
void UWarehouseExperimentSubsystem::Tick(float)
{
 auto* World=GetWorld();if (!World || !World->HasBegunPlay()) return;
 if (bNeedRollback)
 {
  bNeedRollback=false;bLoading=false;bBatchRunning=false;bStartNext=false;bPrepared=false;PendingSnapshot.Reset();AppliedWorld.Reset();
  RestoreAutoStart.Reset();
  LastMessage=TEXT("배치 복원 실패로 원본 맵을 다시 엽니다: ")+RestoreError;UGameplayStatics::OpenLevel(World,FName(*BaseMap));return;
 }
 if (bStartNext) { BeginNextRun();return; }
 if (bOpenPanel)
 {
  if (auto* PC=Cast<Amsc_vrPlayerController>(World->GetFirstPlayerController())) { PC->OpenWarehouseExperiments();bOpenPanel=false; }
 }
 if (bLoading && bPrepared && AppliedWorld.Get()==World)
 {
  if (++PrepareFrames==1)
  {
   if (!WarehouseExperimentStorage::FinalizeAfterBeginPlay(World,PendingSnapshot,RestoreError)) { bNeedRollback=true;return; }
   // BeginPlay has passed, so restoring the setting cannot schedule its startup timer.
   for (const auto& Pair:RestoreAutoStart) if (Pair.Key.IsValid()) Pair.Key->bAutoStart=Pair.Value;
   RestoreAutoStart.Reset();
   if (!bBatchRunning)
   {
    WarehouseExperimentStorage::ResetInitialVelocities(World);
    bLoading=false;bPrepared=false;PendingSnapshot.Reset();
    if (auto* PC=Cast<Amsc_vrPlayerController>(World->GetFirstPlayerController())) PC->SetLayoutEditing(true);
    RestoreUnscheduledWork();bOpenPanel=true;
    LastMessage=TEXT("초기 배치 복원 완료 · 정지한 배치 모드: ")+ActiveLayout.Name;return;
   }
   if (bBatchRunning) DisableUnscheduledWork(World);
   PrepareTime=World->GetTimeSeconds();
   for(TActorIterator<AWarehouseSimulationClock> It(World);It;++It) It->SetSpeed(bBatchRunning ? Speed : 1);
  }
  if (World->GetTimeSeconds()-PrepareTime<2.) return;
  bLoading=false;bPrepared=false;PendingSnapshot.Reset();
  if (bBatchRunning) BeginRun();else { RestoreUnscheduledWork();LastMessage=TEXT("초기 배치 복원 완료: ")+ActiveLayout.Name; }
 }
 if (bBatchRunning && Metrics && Metrics->IsRunning())
 {
  Metrics->Tick();if (Metrics->GetSummary().ElapsedSeconds>=Duration) EndRun(TEXT("completed"));else DispatchOrders();
 }
}
bool UWarehouseExperimentSubsystem::WriteIndex()
{
 // Per-run records are durable; the comparison CSV is a convenience export, never the sole record.
 return ExportCSV();
}
void UWarehouseExperimentSubsystem::ReadResults()
{
 const FString Directory=RootDirectory()/TEXT("Runs");TArray<FString> Folders;
 IFileManager::Get().FindFiles(Folders,*(Directory/TEXT("*")),false,true);Folders.Sort();
 for (const auto& Id:Folders)
 {
  if (!ValidId(Id)) continue;FWarehouseExperimentResult Result;FString Error;TSharedPtr<FJsonObject> Metadata;
  if (!FWarehouseExperimentMetrics::ReadSummary(Directory/Id,Result.Info,Result.Summary,Result.Status,Error) ||
      !ReadJson(Directory/Id/TEXT("Conditions.json"),Metadata) || !Metadata->TryGetStringField(TEXT("layout_name"),Result.LayoutName) ||
      !Metadata->TryGetNumberField(TEXT("achieved_speed"),Result.AchievedSpeed) || !FMath::IsFinite(Result.AchievedSpeed) || Result.AchievedSpeed<0) continue;
  Results.Add(Result);
 }
}
bool UWarehouseExperimentSubsystem::ExportCSV()
{
 if (Results.IsEmpty()) { LastMessage=TEXT("아직 결과가 없습니다.");return false; }
 FString Text=TEXT("run_id,layout_id,layout_name,workload_hash,repeat,status,seed,requested_speed,achieved_speed,simulation_seconds,planned,released,completed,failed,cancelled,unfinished,not_released,throughput_per_hour,mean_seconds,median_seconds,p95_seconds,cargo_hash,layout_hash,map,engine_version,runtime_revision,step_seconds,physics_substep_seconds,duration_limit_seconds");
 for(uint8 P=0;P<uint8(EWarehouseExperimentPhase::Count);++P) Text+=TEXT(",")+FWarehouseExperimentMetrics::PhaseKey(EWarehouseExperimentPhase(P))+TEXT("_job_seconds");
 for(uint8 P=0;P<uint8(EWarehouseExperimentPhase::Count);++P) Text+=TEXT(",")+FWarehouseExperimentMetrics::PhaseKey(EWarehouseExperimentPhase(P))+TEXT("_vehicle_seconds");Text+=TEXT("\n");
 for (const auto& R:Results)
 {
  const auto& S=R.Summary;
  Text+=Csv(R.Info.RunId)+TEXT(",")+Csv(R.Info.LayoutId)+TEXT(",")+Csv(R.LayoutName)+TEXT(",")+Csv(R.Info.ScenarioFingerprint)+FString::Printf(TEXT(",%d,"),R.Info.RepeatIndex)+Csv(R.Status);
  Text+=FString::Printf(TEXT(",%d,%d,%.3f,%.3f,%d,%d,%d,%d,%d,%d,%d,%.6f,%.3f,%.3f,%.3f"),R.Info.Seed,R.Info.RequestedSpeed,R.AchievedSpeed,S.ElapsedSeconds,S.Planned,S.Released,S.Completed,S.Failed,S.Cancelled,S.Unfinished,S.NotReleased,S.ThroughputPerHour,S.MeanTransportSeconds,S.MedianTransportSeconds,S.P95TransportSeconds);
  Text+=TEXT(",")+Csv(R.Info.CargoFingerprint)+TEXT(",")+Csv(R.Info.LayoutFingerprint)+TEXT(",")+Csv(R.Info.MapPackage)+TEXT(",")+Csv(R.Info.EngineVersion)+TEXT(",")+Csv(R.Info.RuntimeRevision)+FString::Printf(TEXT(",%.9f,%.9f,%.3f"),R.Info.StepSeconds,R.Info.PhysicsSubstepSeconds,R.Info.DurationSeconds);
  for(uint8 P=0;P<uint8(EWarehouseExperimentPhase::Count);++P) Text+=FString::Printf(TEXT(",%.6f"),S.JobPhaseSeconds.FindRef(FWarehouseExperimentMetrics::PhaseKey(EWarehouseExperimentPhase(P))));
  for(uint8 P=0;P<uint8(EWarehouseExperimentPhase::Count);++P) Text+=FString::Printf(TEXT(",%.6f"),S.VehiclePhaseSeconds.FindRef(FWarehouseExperimentMetrics::PhaseKey(EWarehouseExperimentPhase(P))));Text+=TEXT("\n");
 }
 const FString Path=RootDirectory()/TEXT("Comparison.csv");IFileManager::Get().MakeDirectory(*RootDirectory(),true);
 if (!FFileHelper::SaveStringToFile(Text,*Path,FFileHelper::EEncodingOptions::ForceUTF8)) { LastMessage=TEXT("비교 CSV 저장 실패");return false; }
 LastMessage=TEXT("비교 CSV 저장: ")+FPaths::ConvertRelativePathToFull(Path);return true;
}
FString UWarehouseExperimentSubsystem::GetStatusJson() const
{
 auto Object=MakeShared<FJsonObject>();Object->SetBoolField(TEXT("running"),bBatchRunning);Object->SetBoolField(TEXT("loading"),bLoading);
 Object->SetNumberField(TEXT("current_run"),CurrentRun);Object->SetNumberField(TEXT("total_runs"),TotalRuns);Object->SetStringField(TEXT("message"),LastMessage);
 TArray<TSharedPtr<FJsonValue>> Layouts;for(const auto& L:GetLayouts()) { auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),L.Id);O->SetStringField(TEXT("name"),L.Name);O->SetStringField(TEXT("cargo_hash"),L.CargoFingerprint);Layouts.Add(MakeShared<FJsonValueObject>(O)); }Object->SetArrayField(TEXT("layouts"),Layouts);
 auto Choices=[](const TArray<FWarehouseExperimentChoice>& Items){TArray<TSharedPtr<FJsonValue>> Values;for(const auto& C:Items){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("id"),C.Id);O->SetStringField(TEXT("label"),C.Label);Values.Add(MakeShared<FJsonValueObject>(O));}return Values;};
 Object->SetArrayField(TEXT("sources"),Choices(GetOrderSources()));Object->SetArrayField(TEXT("destinations"),Choices(GetDestinations()));
 if (Metrics) Object->SetObjectField(TEXT("metrics"),Metrics->ToJson());
 Object->SetNumberField(TEXT("result_count"),Results.Num());return JsonText(Object);
}
