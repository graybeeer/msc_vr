#include "WarehouseWorker.h"
#include "msc_vrPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"

TSharedRef<SWidget> UWarehouseRoleWidget::RebuildWidget()
{
 return SNew(SBorder).Padding(FMargin(12,5)).HAlign(HAlign_Center).VAlign(VAlign_Center)
  .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
  .BorderBackgroundColor(FLinearColor(.02f,.035f,.05f,.9f))
  [SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold",22))
   .ColorAndOpacity(FLinearColor::White).Text_Lambda([this]() { return Label; })];
}

AWarehouseWorker::AWarehouseWorker()
{
 PrimaryActorTick.bCanEverTick=true;
 PrimaryActorTick.TickInterval=.1f;
 AutoPossessAI=EAutoPossessAI::Disabled;
 AutoPossessPlayer=EAutoReceiveInput::Disabled;
 AIControllerClass=nullptr;
 GetCapsuleComponent()->InitCapsuleSize(34,96);
 GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
 GetCharacterMovement()->DisableMovement();
 GetMesh()->SetCollisionProfileName(TEXT("NoCollision"));
 RoleName=FText::FromString(TEXT("작업자"));
 RoleLabel=CreateDefaultSubobject<UWidgetComponent>(TEXT("RoleLabel"));
 RoleLabel->SetupAttachment(GetRootComponent());
 RoleLabel->SetRelativeLocation(FVector(0,0,120));
 RoleLabel->SetWidgetSpace(EWidgetSpace::Screen);
 RoleLabel->SetDrawSize(FVector2D(260,42));
 RoleLabel->SetPivot(FVector2D(.5f,1.f));
 RoleLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 RoleLabel->SetWidgetClass(UWarehouseRoleWidget::StaticClass());
 RoleLabel->SetWindowFocusable(false);
 Tags.Add(TEXT("WarehouseWorker"));
}

bool AWarehouseWorker::CopyPlayerAppearance(TSubclassOf<ACharacter> PlayerClass)
{
 const ACharacter* Player=PlayerClass ? PlayerClass->GetDefaultObject<ACharacter>() : nullptr;
 if (!Player || !Player->GetMesh()->GetSkeletalMeshAsset()) return false;
 auto* Source=Player->GetMesh();
 GetMesh()->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());
 GetMesh()->SetRelativeTransform(Source->GetRelativeTransform());
 GetMesh()->SetAnimInstanceClass(Source->GetAnimClass());
 for (int32 I=0; I<Source->GetNumMaterials(); ++I) GetMesh()->SetMaterial(I,Source->GetMaterial(I));
 GetMesh()->SetOwnerNoSee(false);
 GetMesh()->SetOnlyOwnerSee(false);
 GetMesh()->FirstPersonPrimitiveType=EFirstPersonPrimitiveType::None;
 GetMesh()->SetVisibility(true);
 GetMesh()->SetHiddenInGame(false);
 GetCapsuleComponent()->SetCapsuleSize(Player->GetCapsuleComponent()->GetUnscaledCapsuleRadius(),Player->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
 RoleLabel->SetRelativeLocation(FVector(0,0,GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()+24));
 return true;
}

void AWarehouseWorker::BeginPlay()
{
 Super::BeginPlay();
 GetCharacterMovement()->DisableMovement();
 RoleLabel->InitWidget();
 if (auto* Widget=Cast<UWarehouseRoleWidget>(RoleLabel->GetUserWidgetObject())) Widget->Label=RoleName;
}

bool AWarehouseWorker::HasStandingClearance() const
{
 FCollisionQueryParams Params(SCENE_QUERY_STAT(WorkerClearance),false,this);
 return !GetWorld()->OverlapBlockingTestByProfile(GetActorLocation(),GetActorQuat(),TEXT("Pawn"),
  FCollisionShape::MakeCapsule(GetCapsuleComponent()->GetScaledCapsuleRadius()+5,
   GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-2),Params);
}

void AWarehouseWorker::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 auto* PC=GetWorld()->GetFirstPlayerController();
 if (!PC || !PC->PlayerCameraManager) return;
 const FVector Eye=PC->PlayerCameraManager->GetCameraLocation();
 const FVector Head=RoleLabel->GetComponentLocation();
 FCollisionQueryParams Params(SCENE_QUERY_STAT(WorkerLabel),true,this);
 if (PC->GetPawn()) Params.AddIgnoredActor(PC->GetPawn());
 for (AActor* Hidden : PC->HiddenActors) if (Hidden) Params.AddIgnoredActor(Hidden);
 const auto* WarehousePC=Cast<Amsc_vrPlayerController>(PC);
 const float NameDistance=WarehousePC && WarehousePC->IsObserverView() ? 20000.f : 2500.f;
 FHitResult Hit;
 const bool Visible=FVector::DistSquared(Eye,Head)<=FMath::Square(NameDistance) &&
  !GetWorld()->LineTraceSingleByChannel(Hit,Eye,Head,ECC_Visibility,Params);
 RoleLabel->SetVisibility(Visible);
 if (auto* Widget=Cast<UWarehouseRoleWidget>(RoleLabel->GetUserWidgetObject())) Widget->Label=RoleName;
}
