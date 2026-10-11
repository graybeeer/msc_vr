#include "WarehouseAreaLabel.h"
#include "WarehouseWorker.h"
#include "msc_vrPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"

AWarehouseAreaLabel::AWarehouseAreaLabel()
{
 PrimaryActorTick.bCanEverTick=true;
 PrimaryActorTick.TickInterval=.1f;
 AreaLabel=CreateDefaultSubobject<UWidgetComponent>(TEXT("AreaLabel"));
 SetRootComponent(AreaLabel);
 AreaLabel->SetWidgetSpace(EWidgetSpace::Screen);
 AreaLabel->SetDrawSize(FVector2D(280,42));
 AreaLabel->SetDrawAtDesiredSize(true);
 AreaLabel->SetPivot(FVector2D(.5f,1.f));
 AreaLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 AreaLabel->SetGenerateOverlapEvents(false);
 AreaLabel->SetWidgetClass(UWarehouseRoleWidget::StaticClass());
 AreaLabel->SetWindowFocusable(false);
 Tags.Add(TEXT("WarehouseAreaLabel"));
}

void AWarehouseAreaLabel::BeginPlay()
{
 Super::BeginPlay();
 AreaLabel->InitWidget();
 Tick(0.f);
}

void AWarehouseAreaLabel::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 const auto* PC=GetWorld()->GetFirstPlayerController();
 if (!PC || !PC->PlayerCameraManager) { AreaLabel->SetVisibility(false); return; }
 const auto* WarehousePC=Cast<Amsc_vrPlayerController>(PC);
 const bool Observer=WarehousePC && WarehousePC->IsObserverView();
 const bool MatchingFloor=!Observer || WarehousePC->ObserverFloor<0 || WarehousePC->ObserverFloor==FloorIndex;
 const FVector Eye=PC->PlayerCameraManager->GetCameraLocation();
 const float Distance=Observer ? 20000.f : ViewDistanceCm;
 bool Visible=MatchingFloor && !(WarehousePC && WarehousePC->IsWarehouseMenuOpen()) && !PC->HiddenActors.Contains(this) &&
  FVector::DistSquared(Eye,GetActorLocation())<=FMath::Square(Distance);
 if (Visible)
 {
  FCollisionQueryParams Params(SCENE_QUERY_STAT(AreaLabel),true,this);
  Params.AddIgnoredActor(PC->GetPawn());
  Params.AddIgnoredActor(PC->GetViewTarget());
  for (AActor* Hidden : PC->HiddenActors) if (Hidden) Params.AddIgnoredActor(Hidden);
  FHitResult Hit;
  Visible=!GetWorld()->LineTraceSingleByChannel(Hit,Eye,GetActorLocation(),ECC_Visibility,Params);
 }
 AreaLabel->SetVisibility(Visible);
 if (auto* Widget=Cast<UWarehouseRoleWidget>(AreaLabel->GetUserWidgetObject()))
 {
  Widget->Label=AreaName;
  Widget->FontSize=Observer ? WarehouseUIStyle::CompactSize : WarehouseUIStyle::LabelSize;
  Widget->BackgroundColor=WarehouseUIStyle::Panel;
  Widget->TextColor=WarehouseUIStyle::Accent;
 }
}

bool AWarehouseAreaLabel::ReplacesWorkerLabel(FVector Head) const
{
 // Overview uses one nearby area title instead of repeating the workers' roles.
 // Use world distance so visibility stays stable as camera zoom and DPI change.
 return AreaLabel->IsVisible() && FloorIndex==FMath::FloorToInt(FMath::Max(0.0,Head.Z)/400.0) &&
  FVector::DistSquared(Head,GetActorLocation())<=FMath::Square(500.f);
}
