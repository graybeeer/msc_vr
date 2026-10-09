#include "WarehouseWorker.h"
#include "WarehousePhysics.h"
#include "WarehouseDamageSystem.h"
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
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

bool WarehouseHumanPhysics::DriveCapsule(ACharacter* Person,FVector DesiredVelocity,float MotorForceN,bool SupportLegs,float DesiredHalfHeight,float* GroundDistance,float ExtraSupportedMassKg)
{
 auto* Body=Person->GetCapsuleComponent();
 FHitResult Floor;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(HumanFeet),false,Person);
 const FVector Center=Body->GetComponentLocation();
 const float Half=Body->GetScaledCapsuleHalfHeight();
 const bool Ground=Person->GetWorld()->LineTraceSingleByChannel(Floor,Center,Center-FVector(0,0,Half+8.f),ECC_Visibility,Params) && Floor.ImpactNormal.Z>.65f;
 if (GroundDistance) *GroundDistance=Ground ? Center.Z-Floor.ImpactPoint.Z : -1.f;
 if (!Ground || !SupportLegs) return false; // No air thrusters or world-anchored balance.
 const FVector Velocity=Body->GetPhysicsLinearVelocity();
 const float Distance=Center.Z-Floor.ImpactPoint.Z;
 const float LegGoal=DesiredHalfHeight>0.f ? DesiredHalfHeight : Half;
 const float NormalForce=FMath::Clamp((Body->GetMass()+FMath::Max(0.f,ExtraSupportedMassKg))*980.f+(LegGoal+1.f-Distance)*8000.f-Velocity.Z*1600.f,0.f,180000.f);
 const FVector LegForce(0,0,NormalForce);
 Body->AddForce(LegForce);
 // A feet force also creates a tipping moment; respect the finite balance torque.
 const float BalanceForce=20000.f/FMath::Max(.3f,Half/100.f);
 const float Traction=FMath::Min3(MotorForceN*100.f,.7f*NormalForce,BalanceForce);
 const FVector Motor=((DesiredVelocity-FVector(Velocity.X,Velocity.Y,0))*Body->GetMass()*8.f).GetClampedToMaxSize(Traction);
 Body->AddForceAtLocation(Motor,Floor.ImpactPoint);
 const FVector Omega=Body->GetPhysicsAngularVelocityInRadians();
 FVector Torque=FVector::CrossProduct(Body->GetUpVector(),FVector::UpVector)*600.f-Omega*120.f;
 Torque-=FVector::CrossProduct(Floor.ImpactPoint-Center,Motor)/10000.f;
 if (Person->GetController()) Torque.Z=FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(Body->GetComponentRotation().Yaw,Person->GetControlRotation().Yaw))*60.f-Omega.Z*20.f;
 else Torque.Z=-Omega.Z*20.f;
 Torque=Torque.GetClampedToMaxSize(200.f)*10000.f;
 Body->AddTorqueInRadians(Torque);
 if (auto* Support=Floor.GetComponent(); Support && Support->IsSimulatingPhysics(Floor.BoneName))
 {
  Support->AddForceAtLocation(-LegForce-Motor,Floor.ImpactPoint,Floor.BoneName);
  Support->AddTorqueInRadians(-Torque,Floor.BoneName);
 }
 return true;
}

void WarehouseHumanPhysics::StartRagdoll(ACharacter* Person,float MassKg)
{
 auto* Capsule=Person->GetCapsuleComponent(); auto* Mesh=Person->GetMesh();
 if (!Mesh->GetPhysicsAsset())
 {
  auto* Asset=LoadObject<UPhysicsAsset>(nullptr,TEXT("/Game/Characters/Mannequins/Rigs/PA_Mannequin"));
  bool Compatible=Asset!=nullptr;
  if (Asset) for (USkeletalBodySetup* Setup : Asset->SkeletalBodySetups)
   if (Setup && Mesh->GetBoneIndex(Setup->BoneName)==INDEX_NONE) Compatible=false;
  if (Compatible) Mesh->SetPhysicsAsset(Asset);
 }
 if (!Mesh->GetPhysicsAsset()) return;
 const FVector Velocity=Capsule->GetPhysicsLinearVelocity();
 const float OldMass=Capsule->GetMass();
 const FVector ExpectedMomentum=Velocity*OldMass;
 const FVector Angular=Capsule->GetPhysicsAngularVelocityInRadians();
 Capsule->SetSimulatePhysics(false); Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 Mesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
 Person->SetRootComponent(Mesh);
 Capsule->AttachToComponent(Mesh,FAttachmentTransformRules::KeepWorldTransform);
 Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
 WarehousePhysics::ConfigureContact(Mesh,EWarehouseSurface::Rubber);
 Mesh->SetCollisionObjectType(ECC_Pawn); Mesh->SetCollisionResponseToAllChannels(ECR_Block);
 Mesh->SetAllBodiesSimulatePhysics(true); Mesh->SetPhysicsBlendWeight(1.f); Mesh->SetEnablePhysicsBlending(true);
 TArray<TPair<FBodyInstance*,float>> InitialBodies;
 float Existing=0.f;
 for (auto* Body : Mesh->Bodies) if (Body && Body->IsValidBodyInstance())
 {
  const float Mass=Body->GetBodyMass();
  InitialBodies.Emplace(Body,Mass); Existing+=Mass;
 }
 for (const auto& Initial : InitialBodies)
 {
  auto* Body=Initial.Key;
  if (Existing>0.f)
  {
   Body->SetMassOverride(MassKg*Initial.Value/Existing);
   Body->UpdateMassProperties();
  }
  Body->SetResponseToAllChannels(ECR_Block); Body->SetObjectType(ECC_Pawn); Body->SetUseCCD(true);
  Body->SetOverrideIterationCounts(true); Body->SetPositionSolverIterationCount(16); Body->SetVelocitySolverIterationCount(8);
 }
 double TotalMass=0;
 FVector WeightedCenter=FVector::ZeroVector;
 for (auto* Body : Mesh->Bodies) if (Body && Body->IsValidBodyInstance())
 {
  const float Mass=Body->GetBodyMass();
  TotalMass+=Mass; WeightedCenter+=Body->GetCOMPosition()*Mass;
 }
 const double Denominator=FMath::Max(TotalMass,1.e-6);
 const FVector Center=WeightedCenter/Denominator;
 const FVector Translation=ExpectedMomentum/Denominator;
 FVector ActualMomentum=FVector::ZeroVector;
 for (auto* Body : Mesh->Bodies) if (Body && Body->IsValidBodyInstance())
 {
  // Spin around the new aggregate COM so rotation adds no net linear momentum.
  Body->SetLinearVelocity(Translation+FVector::CrossProduct(Angular,Body->GetCOMPosition()-Center),false);
  Body->SetAngularVelocityInRadians(Angular,false);
  ActualMomentum+=Body->GetUnrealWorldVelocity()*Body->GetBodyMass();
 }
 UE_LOG(LogTemp,Log,TEXT("WAREHOUSE_RAGDOLL_LINEAR_MOMENTUM Actor=%s ExpectedP=(%.9f,%.9f,%.9f) ActualP=(%.9f,%.9f,%.9f) Error=%.9f OldMass=%.9f NewMass=%.9f"),
  *Person->GetName(),ExpectedMomentum.X,ExpectedMomentum.Y,ExpectedMomentum.Z,
  ActualMomentum.X,ActualMomentum.Y,ActualMomentum.Z,(ActualMomentum-ExpectedMomentum).Size(),double(OldMass),TotalMass);
 for (auto* Constraint : Mesh->Constraints) if (Constraint) { Constraint->DisableProjection(); Constraint->DisableMassConditioning(); }
 Mesh->WakeAllRigidBodies();
 if (auto* Damage=AWarehouseDamageSystem::Find(Person)) Damage->RegisterPhysicalContacts(Person);
}

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
 PrimaryActorTick.TickInterval=0.f;
 AutoPossessAI=EAutoPossessAI::Disabled;
 AutoPossessPlayer=EAutoReceiveInput::Disabled;
 AIControllerClass=nullptr;
 GetCapsuleComponent()->InitCapsuleSize(34,96);
 GetCapsuleComponent()->SetCollisionProfileName(TEXT("Pawn"));
 GetCharacterMovement()->DisableMovement();
 GetCharacterMovement()->SetComponentTickEnabled(false);
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
 Tags.Add(TEXT("WarehousePhysicalHuman"));
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
 Tags.AddUnique(TEXT("WarehousePhysicalHuman"));
 GetCapsuleComponent()->SetCapsuleSize(30.f,GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
 GetCharacterMovement()->DisableMovement();
 GetCharacterMovement()->SetComponentTickEnabled(false);
 WarehousePhysics::ConfigureContact(GetCapsuleComponent(),EWarehouseSurface::Rubber,HumanMassKg);
 GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
 GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
 GetCapsuleComponent()->SetEnableGravity(true);
 GetCapsuleComponent()->SetSimulatePhysics(true);
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
 if (!bRagdoll)
 {
  WarehouseHumanPhysics::DriveCapsule(this,FVector::ZeroVector,350.f);
  if (GetCapsuleComponent()->GetUpVector().Z<.5f || GetCapsuleComponent()->GetPhysicsLinearVelocity().Size()>450.f)
  {
   WarehouseHumanPhysics::StartRagdoll(this,HumanMassKg);
   bRagdoll=GetMesh()->IsSimulatingPhysics();
   if (bRagdoll) RoleLabel->AttachToComponent(GetMesh(),FAttachmentTransformRules::SnapToTargetNotIncludingScale,TEXT("head"));
  }
 }
 else RoleLabel->SetRelativeLocation(FVector(0,0,24));
 LabelElapsed+=DeltaSeconds;
 if (LabelElapsed<.1f) return;
 LabelElapsed=0.f;
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
