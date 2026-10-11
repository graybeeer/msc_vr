#include "WarehouseComputer.h"
#include "WarehouseCargo.h"
#include "WarehousePallet.h"
#include "WarehouseForklift.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseInboundZone.h"
#include "WarehouseWorker.h"
#include "msc_vrCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

AWarehouseComputer::AWarehouseComputer()
{
 PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=1.f;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Workstation")));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Steel(TEXT("/Game/Warehouse/AGV/Materials/M_Graphite_powder-coated_steel"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Chrome(TEXT("/Game/Warehouse/AGV/Materials/M_Machined_steel"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Screen(TEXT("/Game/Warehouse/AGV/Materials/M_Sensor_status_green"));
 auto Part=[&](const TCHAR* Name,FVector Position,FVector Size,UMaterialInterface* Material)
 {
  auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(Name);Mesh->SetupAttachment(RootComponent);
  Mesh->SetStaticMesh(Cube.Object);Mesh->SetMaterial(0,Material);Mesh->SetRelativeLocation(Position);
  Mesh->SetRelativeScale3D(Size/100.f);Mesh->SetCollisionProfileName(TEXT("BlockAll"));
 };
 Part(TEXT("Desktop"),FVector(0,0,74),FVector(70,120,4),Chrome.Object);
 for (int32 I=0;I<4;++I)
  Part(*FString::Printf(TEXT("DeskLeg%d"),I),FVector(I<2 ? -25 : 25,I%2 ? -48 : 48,36),FVector(4,4,72),Steel.Object);
 Part(TEXT("MonitorBase"),FVector(12,0,77),FVector(25,28,2),Steel.Object);
 Part(TEXT("MonitorStand"),FVector(15,0,89),FVector(5,7,24),Steel.Object);
 Part(TEXT("MonitorFrame"),FVector(12,0,111),FVector(6,57,35),Steel.Object);
 Part(TEXT("MonitorScreen"),FVector(8.7,0,111),FVector(.6,52,30),Screen.Object);
 Part(TEXT("Keyboard"),FVector(-16,0,77),FVector(15,43,2),Steel.Object);
 Part(TEXT("Mouse"),FVector(-16,-34,77.5),FVector(11,6,3),Steel.Object);
 Part(TEXT("ComputerTower"),FVector(10,41,25),FVector(40,19,48),Steel.Object);
 auto* Label=CreateDefaultSubobject<UWidgetComponent>(TEXT("ComputerLabel"));Label->SetupAttachment(RootComponent);
 Label->SetRelativeLocation(FVector(0,0,145));Label->SetWidgetSpace(EWidgetSpace::Screen);
 Label->SetDrawAtDesiredSize(true);Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 Label->SetWidgetClass(UWarehouseRoleWidget::StaticClass());Label->SetWindowFocusable(false);
 Tags.Add(TEXT("WarehouseFloor0"));
}
void AWarehouseComputer::BeginPlay()
{
 Super::BeginPlay();
 if (auto* Label=FindComponentByClass<UWidgetComponent>())
 {
  Label->InitWidget();if (auto* Widget=Cast<UWarehouseRoleWidget>(Label->GetUserWidgetObject()))
  { Widget->Label=FText::FromString(TEXT("관제 컴퓨터 · E 접속"));Widget->TextColor=WarehouseUIStyle::Accent; }
 }
 RefreshState();
}
bool AWarehouseComputer::CanUse() const
{
 const auto* Strength=AWarehouseDamageSystem::Find(this);
 return !Strength || !Strength->HasFailed(const_cast<AWarehouseComputer*>(this));
}
void AWarehouseComputer::Tick(float Dt)
{
 Super::Tick(Dt);RefreshState();
 if (auto* Label=FindComponentByClass<UWidgetComponent>())
 {
  auto* PC=GetWorld()->GetFirstPlayerController();
  Label->SetVisibility(PC && !PC->IsMoveInputIgnored() && CanUse() && PC->GetPawn() &&
   FVector::DistSquared(PC->GetPawn()->GetActorLocation(),GetActorLocation())<FMath::Square(700.f));
 }
}
bool AWarehouseComputer::Claimed(AWarehousePallet* Pallet) const
{
 for (AWarehouseForklift* Vehicle : Fleet) if (IsValid(Vehicle))
 {
  if (Vehicle->ActiveJob.Pallet==Pallet) return true;
  for (const auto& Job : Vehicle->PendingJobs) if (Job.Pallet==Pallet) return true;
 }
 return false;
}
FString AWarehouseComputer::Locate(AActor* Object) const
{
 const FVector P=Object->GetActorLocation();const int32 Floor=FMath::FloorToInt(FMath::Max(0.,P.Z)/400.);
 for (AWarehouseForklift* Vehicle : Fleet) if (IsValid(Vehicle) && Vehicle->GetLoadMassKg()>0 && Vehicle->ActiveJob.Pallet==Object)
  return Vehicle->VehicleName+TEXT(" · 운반 중");
 for (TActorIterator<AWarehouseInboundZone> It(GetWorld());It;++It)
 {
  const FVector Local=It->GetActorTransform().InverseTransformPosition(P);
  if (FMath::Abs(Local.X)<It->HalfSizeCm.X && FMath::Abs(Local.Y)<It->HalfSizeCm.Y && FMath::Abs(Local.Z)<20)
   return TEXT("입고 대기 구역");
 }
 const FVector Local=P-OutboundCentre;
 if (FMath::Abs(Local.X)<OutboundHalfSize.X && FMath::Abs(Local.Y)<OutboundHalfSize.Y && FMath::Abs(Local.Z)<20)
  return TEXT("출고 대기 구역");
 if (Object->ActorHasTag(TEXT("TruckCargo"))) return TEXT("트럭 적재함");
 return FString::Printf(TEXT("%d층 (%0.f, %0.f cm)"),Floor+1,P.X,P.Y);
}
void AWarehouseComputer::RefreshState()
{
 ++RefreshCount;Fleet.Reset();Inventory.Reset();
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) Fleet.Add(*It);
 auto* Strength=AWarehouseDamageSystem::Find(this);
 TMap<AWarehouseCargo*,AWarehousePallet*> Owners;TMap<AWarehousePallet*,FString> Racks;
 if (Strength)
 {
  bool Refresh=true;
  for (TActorIterator<AWarehousePallet> It(GetWorld());It;++It)
  {
   for (auto* Load : Strength->GetSupportedActors(*It,Refresh)) if (auto* Cargo=Cast<AWarehouseCargo>(Load))
   {
    auto* Previous=Owners.FindRef(Cargo);
    if (!Previous || Previous->GetActorLocation().Z<It->GetActorLocation().Z) Owners.Add(Cargo,*It);
   }
   Refresh=false;
  }
  for (const auto& Object : Strength->Objects) if (Object.Failure==EWarehouseFailure::Rack && !Object.Members.IsEmpty())
   for (auto* Supported : Strength->GetSupportedActors(Object.Members[0],false)) if (auto* Pallet=Cast<AWarehousePallet>(Supported))
   {
    FString Name=Object.Name;TArray<FString> Parts;Name.ParseIntoArray(Parts,TEXT("_"));
    const TCHAR* Code=Name.StartsWith(TEXT("PalletRack_Left_")) ? TEXT("A") :
     Name.StartsWith(TEXT("PalletRack_Right_")) ? TEXT("B") : Name.StartsWith(TEXT("WH_InboundRack_")) ? TEXT("C") : nullptr;
    if (Code && !Parts.IsEmpty())
    {
     const int32 Floor=FMath::FloorToInt((Pallet->GetActorLocation().Z+1)/400.);
     Name=FString::Printf(TEXT("%d층 랙 %s-%02d · %d단"),Floor+1,Code,FCString::Atoi(*Parts.Last())+1,Pallet->GetActorLocation().Z-Floor*400>80 ? 2 : 1);
    }
    Racks.Add(Pallet,Name);
   }
 }
 for (TActorIterator<AWarehouseCargo> It(GetWorld());It;++It)
 {
  FWarehouseInventoryItem Item;Item.Cargo=*It;Item.Pallet=Owners.FindRef(*It);
  Item.CargoId=It->CargoId.IsNone() ? It->GetName() : It->CargoId.ToString();Item.Product=It->CargoKind.ToString();Item.MassKg=It->GrossMassKg;
  Item.Location=Locate(Item.Pallet ? static_cast<AActor*>(Item.Pallet.Get()) : *It);
  if (Item.Pallet && Racks.Contains(Item.Pallet) && !Item.Location.Contains(TEXT("운반 중"))) Item.Location=Racks[Item.Pallet];
  Item.bReserved=Item.Pallet && Claimed(Item.Pallet);Inventory.Add(Item);
 }
 Inventory.Sort([](const auto& A,const auto& B) { return A.CargoId<B.CargoId; });
 auto Remember=[&](AWarehouseForklift* Vehicle,const FWarehouseWorkOrder& Job)
 {
  if (Job.JobId.IsEmpty()) return;
  auto* Task=Tasks.FindByPredicate([&](const auto& T) { return T.JobId==Job.JobId; });
  if (!Task)
  {
   FWarehouseComputerTask T;T.JobId=Job.JobId;T.Pallet=Job.Pallet;T.Vehicle=Vehicle;
   T.Source=IsValid(Job.Pallet) ? Locate(Job.Pallet) : TEXT("대상 없음");
   T.Destination=FString::Printf(TEXT("(%0.f, %0.f, %0.f cm)"),Job.Destination.GetLocation().X,Job.Destination.GetLocation().Y,Job.Destination.GetLocation().Z);
   Tasks.Add(T);
  }
 };
 for (AWarehouseForklift* Vehicle : Fleet)
 {
  Remember(Vehicle,Vehicle->ActiveJob);for (const auto& Job : Vehicle->PendingJobs) Remember(Vehicle,Job);
 }
 for (auto& Task : Tasks)
 {
  auto* V=Task.Vehicle.Get();if (!IsValid(V)) { Task.State=TEXT("차량 연결 끊김");continue; }
  if (Task.State==TEXT("취소됨")) continue;
  Task.bCompleted=V->CompletedJobIds.Contains(Task.JobId);
  if (Task.bCompleted) Task.State=TEXT("완료 · 재고 위치 갱신됨");
  else if (V->ActiveJob.JobId==Task.JobId) Task.State=V->Status;
  else Task.State=TEXT("작업 대기");
 }
 // ponytail: keep 200 session-local records; persistent WMS storage belongs to a later integration.
 if (Tasks.Num()>200) Tasks.RemoveAt(0,Tasks.Num()-200);
}
bool AWarehouseComputer::IssueOutbound(AWarehouseCargo* Cargo,AWarehouseForklift* Vehicle,int32 DestinationIndex)
{
 auto Fail=[&](const TCHAR* Message) { LastMessage=Message;return false; };
 if (!CanUse()) return Fail(TEXT("컴퓨터가 손상되어 사용할 수 없습니다."));
 if (!IsValid(Cargo) || !OutboundDestinations.IsValidIndex(DestinationIndex)) return Fail(TEXT("상품과 출고 위치를 선택하세요."));
 RefreshState();
 const auto* Item=Inventory.FindByPredicate([&](const auto& Row) { return Row.Cargo==Cargo; });
 auto* Pallet=Item ? Item->Pallet.Get() : nullptr;
 auto* Strength=AWarehouseDamageSystem::Find(this);
 if (!Pallet || !Strength) return Fail(TEXT("이 상품을 팔레트 위에 올린 뒤 지시하세요."));
 if (Claimed(Pallet)) return Fail(TEXT("이 팔레트는 이미 다른 작업에 예약되어 있습니다."));
 const auto Loads=Strength->GetSupportedActors(Pallet);
 for (TActorIterator<Amsc_vrCharacter> It(GetWorld());It;++It)
  if (It->GetHeldCargo()==Pallet || Loads.Contains(It->GetHeldCargo())) return Fail(TEXT("사람이 들고 있는 물체는 운반할 수 없습니다."));
 if (Strength->HasFailed(Pallet) || Pallet->GetPalletBody()->GetPhysicsLinearVelocity().Size()>10 ||
     FMath::Abs(Pallet->GetActorRotation().Pitch)>2 || FMath::Abs(Pallet->GetActorRotation().Roll)>2)
  return Fail(TEXT("팔레트가 움직이거나 기울었거나 손상되었습니다."));
 FBox Bounds=AWarehouseDamageSystem::GetPhysicalBounds(Pallet);
 for (auto* Load : Loads) if (auto* Parcel=Cast<AWarehouseCargo>(Load))
 {
  if (Strength->HasFailed(Parcel) || Parcel->GetCargoBody()->GetPhysicsLinearVelocity().Size()>10)
   return Fail(TEXT("적재물이 안정되지 않았거나 손상되었습니다."));
  Bounds+=AWarehouseDamageSystem::GetPhysicalBounds(Parcel);
 }
 const float Mass=Pallet->PalletMassKg+Strength->GetSupportedMass(Pallet);
 const auto& Destination=OutboundDestinations[DestinationIndex];
 const FVector OutboundLocal=Destination.GetLocation()-OutboundCentre;
 if (FMath::Abs(OutboundLocal.X)>OutboundHalfSize.X-55 || FMath::Abs(OutboundLocal.Y)>OutboundHalfSize.Y-55 || FMath::Abs(OutboundLocal.Z)>5)
  return Fail(TEXT("출고 위치가 대기 구역 경계를 벗어났습니다."));
 if (Destination.ContainsNaN() || !Destination.GetScale3D().Equals(FVector::OneVector) ||
     FMath::Abs(Destination.Rotator().Pitch)>.1 || FMath::Abs(Destination.Rotator().Roll)>.1)
  return Fail(TEXT("출고 위치 설정이 올바르지 않습니다."));
 auto Eligible=[&](AWarehouseForklift* V,bool AllowQueue)
 {
  return IsValid(V) && Fleet.Contains(V) && V->bAutonomousMode && V->bPowered && !V->IsRemoteControlled() &&
   V->CheckSystems().IsEmpty() && !V->bChargePending && V->BatteryPercent>V->ChargeBelowPercent &&
   Mass<=V->RatedLoadKg && FMath::Max(0.,Pallet->GetActorLocation().Z-400.*FMath::FloorToDouble((Pallet->GetActorLocation().Z+1.)/400.))+20<=V->MaxForkHeightCm &&
   (AllowQueue || (V->AIState==EWarehouseAIState::Ready && V->ActiveJob.JobId.IsEmpty() && V->PendingJobs.IsEmpty()));
 };
 if (Vehicle && !Eligible(Vehicle,true)) return Fail(TEXT("선택한 차량의 전원·고장·원격 조작·배터리·하중·리프트 범위를 확인하세요."));
 if (!Vehicle)
 {
  float Best=MAX_flt;
  for (AWarehouseForklift* Candidate : Fleet) if (Eligible(Candidate,false))
  {
   const float Cost=FVector::Dist2D(Candidate->GetActorLocation(),Pallet->GetActorLocation()-Pallet->GetActorForwardVector()*220);
   if (Cost<Best) { Best=Cost;Vehicle=Candidate; }
  }
 }
 if (!Vehicle) return Fail(TEXT("사용 가능한 차량이 없습니다. 작업 중인 차량을 직접 선택하면 대기열에 추가할 수 있습니다."));
 for (AWarehouseForklift* V : Fleet)
 {
  auto Reserved=[&](const FWarehouseWorkOrder& Job) { return !Job.JobId.IsEmpty() && FVector::Dist2D(Job.Destination.GetLocation(),Destination.GetLocation())<110 && FMath::Abs(Job.Destination.GetLocation().Z-Destination.GetLocation().Z)<20; };
  if (Reserved(V->ActiveJob)) return Fail(TEXT("출고 위치가 다른 작업에 예약되어 있습니다."));
  for (const auto& Job : V->PendingJobs) if (Reserved(Job)) return Fail(TEXT("출고 위치가 다른 작업에 예약되어 있습니다."));
 }
 FCollisionQueryParams Params(SCENE_QUERY_STAT(ConsoleDestination),false,this);Params.AddIgnoredActor(Pallet);
 for (auto* Load : Loads) Params.AddIgnoredActor(Load);
 const FVector P=Destination.GetLocation();const float Height=Bounds.IsValid ? Bounds.GetSize().Z : 120;
 if (GetWorld()->OverlapBlockingTestByChannel(P+FVector(0,0,Height*.5+2),Destination.GetRotation(),ECC_Visibility,
     FCollisionShape::MakeBox(FVector(54,54,FMath::Max(1.f,Height*.5f-1))),Params)) return Fail(TEXT("출고 위치에 물체가 있습니다. 먼저 비워 주세요."));
 for (float X : {-40.f,40.f}) for (float Y : {-40.f,40.f})
 {
  FHitResult Hit;const FVector Probe=Destination.TransformPosition(FVector(X,Y,0));
  if (!GetWorld()->LineTraceSingleByChannel(Hit,Probe+FVector(0,0,2),Probe-FVector(0,0,5),ECC_Visibility,Params) || Hit.ImpactNormal.Z<.98f)
   return Fail(TEXT("출고 위치의 수평 받침이 없습니다."));
 }
 FWarehouseWorkOrder Job;Job.JobId=FString::Printf(TEXT("CONSOLE-%s-%06d"),*GetName(),NextJob++);
 Job.SourceSystem=TEXT("FMS-COMPUTER");Job.Pallet=Pallet;Job.Destination=Destination;
 if (!Vehicle->SubmitJob(Job)) return Fail(TEXT("작업이 거부되었습니다. 중복 지시 또는 대기열 한도를 확인하세요."));
 RefreshState();
 if (auto* Task=Tasks.FindByPredicate([&](const auto& T) { return T.JobId==Job.JobId; })) Task->Destination=FString::Printf(TEXT("출고 대기 %02d"),DestinationIndex+1);
 LastMessage=Vehicle->VehicleName+TEXT("에 팔레트 전체 운반을 지시했습니다.");return true;
}
bool AWarehouseComputer::SetVehicleRunning(AWarehouseForklift* Vehicle,bool Running)
{
 if (!CanUse() || !IsValid(Vehicle) || !Fleet.Contains(Vehicle) || Vehicle->IsRemoteControlled())
 { LastMessage=TEXT("차량을 선택하고 원격 조작을 해제하세요.");return false; }
 if (Running && !Vehicle->CheckSystems().IsEmpty()) { LastMessage=TEXT("고장 원인을 해결한 뒤 재개하세요.");return false; }
 if (Vehicle->bPowered!=Running) Vehicle->TogglePower();
 RefreshState();LastMessage=Running ? TEXT("자체 점검 후 운행을 재개합니다.") : TEXT("차량에 제동·일시정지를 요청했습니다.");return true;
}
bool AWarehouseComputer::CancelQueuedJob(const FString& JobId)
{
 if (!CanUse()) return false;
 for (AWarehouseForklift* Vehicle : Fleet) if (IsValid(Vehicle))
 {
  const int32 Removed=Vehicle->PendingJobs.RemoveAll([&](const auto& Job) { return Job.JobId==JobId && Job.SourceSystem==TEXT("FMS-COMPUTER"); });
  if (!Removed) continue;
  if (auto* Task=Tasks.FindByPredicate([&](const auto& T) { return T.JobId==JobId; })) Task->State=TEXT("취소됨");
  LastMessage=TEXT("대기 작업을 취소했습니다.");RefreshState();return true;
 }
 LastMessage=TEXT("관제 컴퓨터의 대기 작업만 취소할 수 있습니다. 진행 중이면 차량을 일시정지하세요.");return false;
}
