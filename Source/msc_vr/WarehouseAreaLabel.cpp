#include "WarehouseAreaLabel.h"
#include "WarehouseWorker.h"
#include "msc_vrPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/WidgetComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"

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
 const FVector Eye=PC->PlayerCameraManager->GetCameraLocation();
 FBox2D Rect;
 bool Visible=CandidateVisible(PC,Observer,Eye) && ScreenRect(PC,Observer,Rect);
 if (Visible)
 {
  const double Distance=FVector::DistSquared(Eye,GetActorLocation());
  for (TActorIterator<AWarehouseAreaLabel> It(GetWorld());It;++It) if (*It!=this)
  {
   const double OtherDistance=FVector::DistSquared(Eye,It->GetActorLocation());
   if (OtherDistance>Distance || (FMath::IsNearlyEqual(OtherDistance,Distance,1.) && It->GetName()>GetName())) continue;
   FBox2D OtherRect;
   if (It->ScreenRect(PC,Observer,OtherRect) && Rect.Intersect(OtherRect) && It->CandidateVisible(PC,Observer,Eye))
   { Visible=false;break; }
  }
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

bool AWarehouseAreaLabel::CandidateVisible(const APlayerController* PC,bool Observer,FVector Eye) const
{
 const auto* WarehousePC=Cast<Amsc_vrPlayerController>(PC);
 const auto* View=PC->GetViewTarget();
 const int32 Floor=View ? FMath::FloorToInt((FMath::Max(0.,View->GetActorLocation().Z)+1.)/400.) : 0;
 const bool Matching=Observer ? WarehousePC->ObserverFloor<0 || WarehousePC->ObserverFloor==FloorIndex : Floor==FloorIndex;
 if (!Matching || (WarehousePC && (WarehousePC->IsWarehouseMenuOpen() || WarehousePC->IsWarehouseComputerOpen())) ||
     PC->HiddenActors.Contains(this) || FVector::DistSquared(Eye,GetActorLocation())>FMath::Square(Observer ? 20000.f : ViewDistanceCm)) return false;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(AreaLabel),true,this);
 Params.AddIgnoredActor(PC->GetPawn());Params.AddIgnoredActor(View);
 for (AActor* Hidden : PC->HiddenActors) if (Hidden) Params.AddIgnoredActor(Hidden);
 FHitResult Hit;
 return !GetWorld()->LineTraceSingleByChannel(Hit,Eye,GetActorLocation(),ECC_Visibility,Params);
}

bool AWarehouseAreaLabel::ScreenRect(const APlayerController* PC,bool Observer,FBox2D& Rect) const
{
 FVector2D Point;
 if (!PC->ProjectWorldLocationToScreen(GetActorLocation(),Point,true)) return false;
 const auto Font=WarehouseUIStyle::Font(Observer ? WarehouseUIStyle::CompactSize : WarehouseUIStyle::LabelSize,true);
 const FVector2D Size=(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(AreaName.ToString(),Font)+FVector2D(28,16))*UWidgetLayoutLibrary::GetViewportScale(this);
 Rect=FBox2D(Point-FVector2D(Size.X*.5f,Size.Y),Point+FVector2D(Size.X*.5f,0));
 return true;
}

bool AWarehouseAreaLabel::ReplacesWorkerLabel(FVector Head,const FText& WorkerTitle) const
{
 if (!AreaLabel->IsVisible() || FloorIndex!=FMath::FloorToInt(FMath::Max(0.0,Head.Z)/400.0)) return false;
 const auto* PC=GetWorld()->GetFirstPlayerController();
 const auto* WarehousePC=Cast<Amsc_vrPlayerController>(PC);
 // Overview replaces nearby roles with one area title; other views suppress
 // only roles whose actual screen rectangle overlaps that title.
 if (WarehousePC && WarehousePC->IsObserverView())
  return FVector::DistSquared(Head,GetActorLocation())<=FMath::Square(500.f);
 FVector2D Point;FBox2D AreaRect;
 if (!PC || !PC->ProjectWorldLocationToScreen(Head,Point,true) || !ScreenRect(PC,false,AreaRect)) return false;
 const auto Font=WarehouseUIStyle::Font(WarehouseUIStyle::LabelSize,true);
 const FVector2D Size=(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(WorkerTitle.ToString(),Font)+FVector2D(28,16))*UWidgetLayoutLibrary::GetViewportScale(this);
 return AreaRect.Intersect(FBox2D(Point-FVector2D(Size.X*.5f,Size.Y),Point+FVector2D(Size.X*.5f,0)));
}
