#include "WarehousePlayMode.h"
#include "WarehouseCargo.h"
#include "WarehousePallet.h"
#include "WarehouseForklift.h"
#include "WarehouseInboundZone.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseChargingStation.h"
#include "WarehouseWaitingBay.h"
#include "WarehouseConveyor.h"
#include "WarehouseUIStyle.h"
#include "msc_vrPlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

FString UWarehousePlayProgress::SaveSlot()
{
#if WITH_EDITOR
 if (FParse::Param(FCommandLine::Get(),TEXT("WarehousePlayTest"))) return TEXT("WarehousePlayProgress_Test");
#endif
 return TEXT("WarehousePlayProgress_v1");
}
void UWarehousePlayProgress::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);
 if (auto* Saved=Cast<UWarehousePlaySave>(UGameplayStatics::LoadGameFromSlot(SaveSlot(),0)))
 { UnlockedStage=FMath::Clamp(Saved->UnlockedStage,1,3);CompletedStages=Saved->CompletedStages;CompletedStages.RemoveAll([](int32 I) {return I<1 || I>3;}); }
}
bool UWarehousePlayProgress::RecordCompletion(int32 Stage)
{
 if (Stage<1 || Stage>3 || Stage>UnlockedStage) return false;
 CompletedStages.AddUnique(Stage);UnlockedStage=FMath::Max(UnlockedStage,FMath::Min(3,Stage+1));
 auto* Saved=Cast<UWarehousePlaySave>(UGameplayStatics::CreateSaveGameObject(UWarehousePlaySave::StaticClass()));
 Saved->UnlockedStage=UnlockedStage;Saved->CompletedStages=CompletedStages;
 return UGameplayStatics::SaveGameToSlot(Saved,SaveSlot(),0);
}
AWarehousePlayStage::AWarehousePlayStage()
{
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Stage")));
 PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=.5f;
 Tags.Add(TEXT("LayoutFixed"));
}
AWarehousePlayStage* AWarehousePlayStage::Find(const UObject* Context)
{
 if (!Context || !Context->GetWorld()) return nullptr;
 for (TActorIterator<AWarehousePlayStage> It(Context->GetWorld());It;++It) return *It;
 return nullptr;
}
bool AWarehousePlayStage::IsPlay(const UObject* Context) {const auto* S=Find(Context);return S && S->StageIndex>0;}
bool AWarehousePlayStage::IsMenu(const UObject* Context) {const auto* S=Find(Context);return S && S->StageIndex==0;}
FString AWarehousePlayStage::StageMap(int32 Stage)
{return Stage==0 ? TEXT("/Game/Warehouse/Play/Maps/MainMenu") : FString::Printf(TEXT("/Game/Warehouse/Play/Maps/Stage%02d"),Stage);}
FString AWarehousePlayStage::StageTitle(int32 Stage)
{return Stage==1 ? TEXT("01 · 소형 입고 창고") : Stage==2 ? TEXT("02 · 확장 물류 창고") : TEXT("03 · 복합 화물 창고");}
FString AWarehousePlayStage::UnlockDescription(int32 Stage)
{return Stage==1 ? TEXT("기본 지게차·랙 / 의류·수건") : Stage==2 ? TEXT("컨베이어 3종 해금 / 서적·식품·생수") : TEXT("공유 충전·대기 자리 해금 / 식기·전자제품·중량 부품");}
int32 AWarehousePlayStage::EquipmentCost(EWarehouseLayoutCategory C)
{
 switch(C) {
 case EWarehouseLayoutCategory::Forklift:return 50000000;
 case EWarehouseLayoutCategory::Rack:return 1000000;
 case EWarehouseLayoutCategory::Conveyor:return 10000000;
 case EWarehouseLayoutCategory::Charger:return 3000000;
 case EWarehouseLayoutCategory::WaitingBay:return 500000;
 default:return 0;
 }
}
int32 AWarehousePlayStage::RequiredStage(EWarehouseLayoutCategory C)
{
 switch(C) {
 case EWarehouseLayoutCategory::Forklift:case EWarehouseLayoutCategory::Rack:return 1;
 case EWarehouseLayoutCategory::Conveyor:return 2;
 case EWarehouseLayoutCategory::Charger:case EWarehouseLayoutCategory::WaitingBay:return 3;
 default:return 99;
 }
}
void AWarehousePlayStage::BeginPlay()
{
 Super::BeginPlay();BudgetWon=FMath::Max(0,StartingBudgetWon);
 if (StageIndex==0) { SetActorTickEnabled(false);return; }
 if (Inbound) Inbound->bExternalDispatchOnly=true;
 Status=TEXT("배치 준비 · 추가 설비는 배치 확정 시 결제됩니다. 운영 시작을 누르세요.");
}
bool AWarehousePlayStage::CanPurchase(EWarehouseLayoutCategory C) const
{ return StageIndex>=RequiredStage(C) && EquipmentCost(C)>0 && BudgetWon>=EquipmentCost(C) && !bCompleted; }
FString AWarehousePlayStage::PurchaseDescription(EWarehouseLayoutCategory C) const
{
 if (RequiredStage(C)>3) return TEXT("플레이 모드에서 추가할 수 없는 요소");
 if (StageIndex<RequiredStage(C)) return FString::Printf(TEXT("스테이지 %d에서 해금"),RequiredStage(C));
 return FString::Printf(TEXT("구매 %s원%s · 취소 무료 / 철거 환불 없음"),*FText::AsNumber(EquipmentCost(C)).ToString(),CanPurchase(C) ? TEXT("") : TEXT(" · 예산 부족 또는 완료됨"));
}
bool AWarehousePlayStage::ChargePurchase(EWarehouseLayoutCategory C)
{
 if (!CanPurchase(C)) {Status=PurchaseDescription(C);return false;}
 BudgetWon-=EquipmentCost(C);SpentWon+=EquipmentCost(C);return true;
}
bool AWarehousePlayStage::CanEditActor(const AActor* A) const
{
 // Fixed-map knowledge is not an edit lock: purchased conveyors/racks also belong to the map.
 if (!IsValid(A) || A->ActorHasTag(TEXT("PlayFixed"))) return false;
 return A->ActorHasTag(TEXT("LayoutType:Rack")) || A->IsA<AWarehouseForklift>() || A->IsA<AWarehouseConveyor>() || A->IsA<AWarehouseChargingStation>() || A->IsA<AWarehouseWaitingBay>();
}
bool AWarehousePlayStage::Contains(const FBox& B) const
{return B.IsValid && B.Min.X>=-HalfSizeCm.X+30 && B.Max.X<=HalfSizeCm.X-30 && B.Min.Y>=-HalfSizeCm.Y+30 && B.Max.Y<=HalfSizeCm.Y-30 && B.Min.Z>=-5 && B.Max.Z<CeilingCm-30;}
bool AWarehousePlayStage::StartOperation()
{
 if (StageIndex<1 || bOperating || bCompleted || !IsValid(Inbound) || !CartonMesh || ArrivalPoints.Num()<DeliveryGoal)
 {Status=TEXT("운영 시작 불가 · 입고 구역/상자 에셋/입고 위치 또는 진행 상태를 확인하세요.");return false;}
 if (Inbound->RackSlots.IsEmpty()) {Status=TEXT("운반할 보관 랙이 없습니다. 랙을 배치하세요.");return false;}
 int32 Vehicles=0;for(TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) ++Vehicles;
 if (!Vehicles) {Status=TEXT("지게차가 없습니다. 지게차를 구매하거나 스테이지를 다시 시작하세요.");return false;}
 bOperating=true;Inbound->bExternalDispatchOnly=false;NextArrivalSeconds=GetWorld()->GetTimeSeconds()+2;
 if (auto* PC=Cast<Amsc_vrPlayerController>(GetWorld()->GetFirstPlayerController())) PC->SetLayoutEditing(false);
 for(TActorIterator<AWarehouseForklift> It(GetWorld());It;++It) if (!It->bPowered) It->TogglePower();
 Status=TEXT("입고 운영 중 · 안정된 팔레트를 NPC가 확정하면 지게차가 랙으로 운반합니다.");return true;
}
bool AWarehousePlayStage::SpawnArrival()
{
 const FVector Point=ArrivalPoints[Arrived];
 // Arrivals wait rather than teleporting through existing physical stock/vehicles.
 FCollisionQueryParams Query(SCENE_QUERY_STAT(PlayArrival),false,this);
 if (GetWorld()->OverlapBlockingTestByChannel(Point+FVector(0,0,80),FQuat::Identity,ECC_Visibility,FCollisionShape::MakeBox(FVector(56,56,75)),Query))
 {Status=TEXT("입고 자리가 막혀 다음 화물이 대기 중입니다.");return false;}
 auto* Strength=AWarehouseDamageSystem::Find(this);if (!Strength) return false;
 auto* Pallet=GetWorld()->SpawnActor<AWarehousePallet>(Point+FVector(0,0,.1),FRotator::ZeroRotator);
 if (!Pallet) return false;
 const int32 Profiles1[]={0,13},Profiles2[]={1,2,3},Profiles3[]={5,6,7,8};
 const int32 Profile=StageIndex==1 ? Profiles1[Arrived%2] : StageIndex==2 ? Profiles2[Arrived%3] : Profiles3[Arrived%4];
 const auto Recipe=AWarehouseCargo::GenerateCargoRecipe(20261025+StageIndex,Arrived,Profile,FVector(75,65,65),StageIndex==3 ? 1.8f : 1.2f);
 const FVector Scale=Recipe.SizeCm/CartonMesh->GetBoundingBox().GetSize();
 const FBox PalletBox=AWarehouseDamageSystem::GetPhysicalBounds(Pallet);
 const FVector Position(Point.X,Point.Y,PalletBox.Max.Z+.2);
 const FVector Origin=Position-CartonMesh->GetBoundingBox().GetCenter()*Scale+FVector(0,0,Recipe.SizeCm.Z*.5);
 const FTransform Pose(FQuat::Identity,Origin,Scale);
 auto* Cargo=GetWorld()->SpawnActorDeferred<AWarehouseCargo>(AWarehouseCargo::StaticClass(),Pose,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if (!Cargo) {Pallet->Destroy();return false;}
 Cargo->SetCargoMesh(CartonMesh);Cargo->ApplyCargoRecipe(Recipe);
 Cargo->CargoId=FName(*FString::Printf(TEXT("ST%d-IN-%03d"),StageIndex,Arrived+1));
 UGameplayStatics::FinishSpawningActor(Cargo,Pose);
 FWarehouseStrength P;P.Name=Pallet->GetName();P.Members={Pallet};P.MassKg=25;P.RatedLoadKg=1500;P.PhysicsMeshes={Pallet->GetPalletBody()->GetStaticMesh()};Strength->RegisterLayoutProfile(P);
 FWarehouseStrength C;C.Name=Cargo->CargoId.ToString();C.Members={Cargo};C.MassKg=Recipe.GrossMassKg;C.RatedLoadKg=Recipe.bFragile ? 60.f : 150.f;C.Failure=EWarehouseFailure::Crush;C.PhysicsMeshes={CartonMesh};Strength->RegisterLayoutProfile(C);
 ++Arrived;Status=FString::Printf(TEXT("입고 %d/%d · %s · %.1fkg"),Arrived,DeliveryGoal,*Recipe.ProductKind.ToString(),Recipe.GrossMassKg);return true;
}
void AWarehousePlayStage::Tick(float Dt)
{
 Super::Tick(Dt);if (!bOperating || bCompleted || !IsValid(Inbound)) return;
 Delivered=Inbound->CompletedCount;
 if (Arrived<DeliveryGoal && GetWorld()->GetTimeSeconds()>=NextArrivalSeconds)
 { if (SpawnArrival()) NextArrivalSeconds=GetWorld()->GetTimeSeconds()+12;else NextArrivalSeconds=GetWorld()->GetTimeSeconds()+2; }
 if (Delivered>=DeliveryGoal && Arrived>=DeliveryGoal)
 {
  bCompleted=true;bOperating=false;
  const bool Saved=GetGameInstance()->GetSubsystem<UWarehousePlayProgress>()->RecordCompletion(StageIndex);
  Status=StageIndex<3 ? TEXT("목표 달성! 다음 스테이지와 새 설비가 해금됐습니다.") : TEXT("프로토타입 3개 스테이지 완료!");
  if (!Saved) Status+=TEXT(" 진행 저장 실패 · 이번 실행에서는 해금 유지");
 }
}

namespace
{
 TSharedRef<SWidget> PlayText(TFunction<FString()> Value,int32 Size=15,FLinearColor Color=WarehouseUIStyle::Text)
 {return SNew(STextBlock).Font(WarehouseUIStyle::Font(Size)).ColorAndOpacity(Color).Text_Lambda([Value](){return FText::FromString(Value());}).AutoWrapText(true);}
 TSharedRef<SWidget> PlayButton(FString Label,TFunction<void()> Action,TFunction<bool()> Enabled=[](){return true;})
 {return SNew(SButton).IsFocusable(false).ContentPadding(FMargin(10,7)).IsEnabled_Lambda([Enabled](){return Enabled();}).OnClicked_Lambda([Action](){Action();return FReply::Handled();})[PlayText([Label](){return Label;})];}
}
TSharedRef<SWidget> MakeWarehousePlayPanel(Amsc_vrPlayerController* PC,AWarehousePlayStage* Stage)
{
 const TWeakObjectPtr<Amsc_vrPlayerController> Owner=PC;const TWeakObjectPtr<AWarehousePlayStage> S=Stage;
 auto* Progress=PC->GetGameInstance()->GetSubsystem<UWarehousePlayProgress>();const TWeakObjectPtr<UWarehousePlayProgress> P=Progress;
 if (Stage->StageIndex==0)
 {
  auto Body=SNew(SVerticalBox);
  Body->AddSlot().AutoHeight().Padding(0,0,0,14)[PlayText([](){return TEXT("WAREHOUSE / 물류창고 운영");},28,WarehouseUIStyle::Accent)];
  Body->AddSlot().AutoHeight().Padding(0,6)[PlayText([](){return TEXT("관리자 모드 · 기존 창고에서 배치·물류·실험 변수를 자유롭게 조절합니다.");})];
  Body->AddSlot().AutoHeight().Padding(0,6,0,20)[PlayButton(TEXT("관리자 모드 시작"),[Owner](){if(Owner.IsValid()) Owner->OpenPlayMode(-1);})];
  Body->AddSlot().AutoHeight().Padding(0,6)[PlayText([](){return TEXT("플레이 모드 · 고정된 창고에서 예산 안에 설비를 배치하고 입고 목표를 달성하세요.");},18)];
  for(int32 I=1;I<=3;++I)
  {
   Body->AddSlot().AutoHeight().Padding(0,7,0,2)[PlayButton(AWarehousePlayStage::StageTitle(I),[Owner,I](){if(Owner.IsValid()) Owner->OpenPlayMode(I);},[P,I](){return P.IsValid() && P->UnlockedStage>=I;})];
   Body->AddSlot().AutoHeight().Padding(4,0,0,5)[PlayText([P,I](){return AWarehousePlayStage::UnlockDescription(I)+(P.IsValid() && P->UnlockedStage<I ? TEXT(" · 앞 스테이지 완료 필요") : TEXT(""));},13,WarehouseUIStyle::Accent)];
  }
  Body->AddSlot().AutoHeight().Padding(0,14)[PlayText([](){return TEXT("가벼운 3단계 프로토타입 · 해금은 자동 저장됩니다. 각 스테이지는 초기 예산과 배치로 새로 시작합니다.");},13)];
  return SNew(SBorder).HAlign(HAlign_Center).VAlign(VAlign_Center).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(.025f,.04f,.055f,1))
   [SNew(SBox).WidthOverride(720).MaxDesiredHeight(720)[SNew(SScrollBox)+SScrollBox::Slot()[Body]]];
 }
 auto Body=SNew(SVerticalBox);
 Body->AddSlot().AutoHeight()[PlayText([S](){return S.IsValid() ? FString::Printf(TEXT("%s  |  예산 %s원  |  운반 %d/%d"),*AWarehousePlayStage::StageTitle(S->StageIndex),*FText::AsNumber(S->BudgetWon).ToString(),S->Delivered,S->DeliveryGoal) : FString();},17,WarehouseUIStyle::Accent)];
 Body->AddSlot().AutoHeight().Padding(0,4)[PlayText([S](){return S.IsValid() ? S->Status : FString();},13)];
 auto Buttons=SNew(SHorizontalBox);
 Buttons->AddSlot().AutoWidth().Padding(2)[PlayButton(TEXT("운영 시작"),[S](){if(S.IsValid()) S->StartOperation();},[S](){return S.IsValid() && !S->bOperating && !S->bCompleted;})];
 Buttons->AddSlot().AutoWidth().Padding(2)[PlayButton(TEXT("설비 구매 · 재배치"),[Owner](){if(Owner.IsValid()) Owner->SetLayoutEditing(true);})];
 Buttons->AddSlot().AutoWidth().Padding(2)[PlayButton(TEXT("다음 스테이지"),[Owner,S](){if(Owner.IsValid() && S.IsValid()) Owner->OpenPlayMode(S->StageIndex+1);},[S](){return S.IsValid() && S->bCompleted && S->StageIndex<3;})];
 Buttons->AddSlot().AutoWidth().Padding(2)[PlayButton(TEXT("다시 시작"),[Owner,S](){if(Owner.IsValid() && S.IsValid()) Owner->OpenPlayMode(S->StageIndex);})];
 Buttons->AddSlot().AutoWidth().Padding(2)[PlayButton(TEXT("메인 메뉴"),[Owner](){if(Owner.IsValid()) Owner->OpenPlayMode(0);})];
 Body->AddSlot().AutoHeight()[Buttons];
 return SNew(SVerticalBox)+SVerticalBox::Slot().AutoHeight().Padding(12)[SNew(SBorder).Padding(10).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(WarehouseUIStyle::Panel)[Body]];
}
