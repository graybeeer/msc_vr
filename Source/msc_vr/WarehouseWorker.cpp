#include "WarehouseWorker.h"
#include "WarehouseAreaLabel.h"
#include "EngineUtils.h"
#include "WarehousePhysics.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseLogisticsTruck.h"
#include "WarehouseBuildingLayout.h"
#include "WarehouseLayoutEditor.h"
#include "WarehousePlayMode.h"
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

bool WarehouseHumanPhysics::DriveCapsule(ACharacter* Person,FVector DesiredVelocity,float MotorForceN,bool SupportLegs,float DesiredHalfHeight,float* GroundDistance,float ExtraSupportedMassKg,FVector LoadOffsetCm)
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
 auto* Support=Floor.GetComponent();
 const FVector SupportVelocity=Support ? Support->GetPhysicsLinearVelocityAtPoint(Floor.ImpactPoint,Floor.BoneName) : FVector::ZeroVector;
 const FVector RelativeVelocity=Velocity-SupportVelocity;
 const float Distance=Center.Z-Floor.ImpactPoint.Z;
 const float LegGoal=DesiredHalfHeight>0.f ? DesiredHalfHeight : Half;
 const float NormalForce=FMath::Clamp((Body->GetMass()+FMath::Max(0.f,ExtraSupportedMassKg))*980.f+(LegGoal+1.f-Distance)*8000.f-RelativeVelocity.Z*1600.f,0.f,180000.f);
 const FVector LegForce(0,0,NormalForce);
 Body->AddForce(LegForce);
 // A feet force also creates a tipping moment; respect the finite balance torque.
 const float BalanceTorqueNm=600.f;
 const float BalanceForce=BalanceTorqueNm*100.f/FMath::Max(.3f,Half/100.f);
 const float Traction=FMath::Min3(MotorForceN*100.f,.8f*NormalForce,BalanceForce);
 // Active steps brake against the floor, not air damping or a velocity reset.
 // Limit feedback at low frame rates so one step cannot reverse its own error.
 const float Response=FMath::Min(32.f,1.f/FMath::Max(Person->GetWorld()->GetDeltaSeconds(),SMALL_NUMBER));
 const FVector PlanarVelocity=FVector::VectorPlaneProject(RelativeVelocity,Floor.ImpactNormal);
 const FVector Motor=(FVector::VectorPlaneProject(DesiredVelocity,Floor.ImpactNormal)-PlanarVelocity)*Body->GetMass()*Response;
 const FVector FeetForce=Motor.GetClampedToMaxSize(Traction);
 Body->AddForceAtLocation(FeetForce,Floor.ImpactPoint);
 const FVector Omega=Body->GetPhysicsAngularVelocityInRadians();
 FVector BalanceDamping(120.f,120.f,20.f);
 if (ExtraSupportedMassKg>0.f)
 {
  // A load held away from the torso adds rotational inertia. Match damping to
  // that actual lever arm so the carrier does not rock while placing heavy cargo.
  const FVector Inertia=Body->GetInertiaTensor()/10000.f+
      FVector(FMath::Square(LoadOffsetCm.Y)+FMath::Square(LoadOffsetCm.Z),
          FMath::Square(LoadOffsetCm.X)+FMath::Square(LoadOffsetCm.Z),
          FMath::Square(LoadOffsetCm.X)+FMath::Square(LoadOffsetCm.Y))*(ExtraSupportedMassKg/10000.f);
  BalanceDamping.X=FMath::Max(120.f,2.f*FMath::Sqrt(600.f*Inertia.X));
  BalanceDamping.Y=FMath::Max(120.f,2.f*FMath::Sqrt(600.f*Inertia.Y));
  BalanceDamping.Z=FMath::Max(20.f,2.f*FMath::Sqrt(60.f*Inertia.Z));
 }
 FVector Torque=FVector::CrossProduct(Body->GetUpVector(),FVector::UpVector)*600.f-Omega*BalanceDamping;
 Torque-=FVector::CrossProduct(Floor.ImpactPoint-Center,FeetForce)/10000.f;
 // Anticipate the held load's gravity moment, just as a carrier shifts their stance.
 // Keep the combined balance torque within the existing finite limit.
 Torque+=FVector::CrossProduct(LoadOffsetCm,FVector(0,0,FMath::Max(0.f,ExtraSupportedMassKg)*980.f))/10000.f;
 if (Person->GetController()) Torque.Z=FMath::DegreesToRadians(FMath::FindDeltaAngleDegrees(Body->GetComponentRotation().Yaw,Person->GetControlRotation().Yaw))*60.f-Omega.Z*BalanceDamping.Z;
 else Torque.Z=-Omega.Z*BalanceDamping.Z;
 Torque=Torque.GetClampedToMaxSize(BalanceTorqueNm)*10000.f;
 Body->AddTorqueInRadians(Torque);
 if (Support && Support->IsSimulatingPhysics(Floor.BoneName))
 {
  Support->AddForceAtLocation(-LegForce-FeetForce,Floor.ImpactPoint,Floor.BoneName);
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
 return SNew(SBorder).Padding(WarehouseUIStyle::LabelPadding).HAlign(HAlign_Center).VAlign(VAlign_Center)
  .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
  .BorderBackgroundColor_Lambda([this]() { return BackgroundColor; })
  [SNew(STextBlock).Font_Lambda([this]() { return WarehouseUIStyle::Font(FontSize,true); })
   .ColorAndOpacity_Lambda([this]() { return FSlateColor(TextColor); }).Text_Lambda([this]() { return Label; })];
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
 RoleLabel->SetDrawAtDesiredSize(true);
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
 RoleLabel->SetDrawAtDesiredSize(true);
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

int32 AWarehouseWorker::RelocateIndoorWorkers(const UObject* Context)
{
 UWorld* World=Context ? Context->GetWorld() : nullptr;if (!World) return 0;
 FBox Building(ForceInit);
 if (const auto* Stage=AWarehousePlayStage::Find(Context);Stage && Stage->StageIndex>0)
  Building=FBox(FVector(-Stage->HalfSizeCm.X,-Stage->HalfSizeCm.Y,0),FVector(Stage->HalfSizeCm.X,Stage->HalfSizeCm.Y,Stage->CeilingCm));
 else for (TActorIterator<AWarehouseBuildingLayout> It(World);It;++It) {Building=It->GetFootprint();break;}
 if (!Building.IsValid) for (TActorIterator<AActor> It(World);It;++It)
  if (It->ActorHasTag(TEXT("ObserverFadeWall")) && It->GetActorEnableCollision()) Building+=It->GetComponentsBoundingBox(true);
 if (!Building.IsValid) return 0;
 TArray<AWarehouseWorker*> Workers;TArray<FVector> Occupied;
 for (TActorIterator<AWarehouseWorker> It(World);It;++It) Workers.Add(*It);
 Workers.Sort([](const AWarehouseWorker& A,const AWarehouseWorker& B){return A.GetName()<B.GetName();});
 // Role presenters no longer belong to the physical staging-area groups.
 // Clear references even when an earlier saved pass already placed them outside.
 for (auto* Worker:Workers)
 {
  Worker->Tags.RemoveAll([](FName Tag){const FString Name=Tag.ToString();return Name.StartsWith(TEXT("LayoutGroup:")) || Name==TEXT("LayoutType:Zone");});
  Worker->Tags.AddUnique(TEXT("LayoutType:Other"));
  for (TActorIterator<AWarehouseLayoutEditor> It(World);It;++It) for (auto& Zone:It->Zones) Zone.Members.Remove(Worker);
 }
 const auto Inside=[&](const FVector& Point){return Point.X>Building.Min.X-45 && Point.X<Building.Max.X+45 && Point.Y>Building.Min.Y-45 && Point.Y<Building.Max.Y+45;};
 for (auto* Worker:Workers) if (!Inside(Worker->GetActorLocation())) Occupied.Add(Worker->GetActorLocation());
 int32 Moved=0;
 for (auto* Worker:Workers)
 {
  if (!Inside(Worker->GetActorLocation()) || Worker->IsRagdoll()) continue;
  auto* Capsule=Worker->GetCapsuleComponent();const float Half=Capsule->GetScaledCapsuleHalfHeight(),Radius=Capsule->GetScaledCapsuleRadius();
  FCollisionQueryParams Query(SCENE_QUERY_STAT(ExteriorWorker),true,Worker);
  FVector Destination=FVector::ZeroVector;bool Found=false;
  // Use perimeter ground and clear standing space, starting near the corners
  // so the central truck doors and approach lanes remain available.
  for (int32 Side=0;Side<4 && !Found;++Side) for (int32 Row=0;Row<3 && !Found;++Row) for (int32 Slot=0;Slot<24 && !Found;++Slot)
  {
   const double Gap=180.+Row*140.;FVector Point;
   if (Side<2)
    Point=FVector(Side==0 ? Building.Min.X-Gap : Building.Max.X+Gap,Building.Min.Y+140.+Slot*140.,0);
   else Point=FVector(Building.Min.X+140.+Slot*140.,Side==2 ? Building.Min.Y-Gap : Building.Max.Y+Gap,0);
   if ((Side<2 && Point.Y>Building.Max.Y-140.) || (Side>=2 && Point.X>Building.Max.X-140.)) continue;
   if (Occupied.ContainsByPredicate([&](const FVector& Other){return FVector::DistSquared2D(Point,Other)<FMath::Square(110.);})) continue;
   TArray<FHitResult> Floors;
   World->LineTraceMultiByObjectType(Floors,Point+FVector(0,0,150),Point-FVector(0,0,300),FCollisionObjectQueryParams::AllObjects,Query);
   for (const auto& Floor:Floors)
   {
    auto* Part=Floor.GetComponent();
    if (Floor.ImpactNormal.Z<.95 || !WarehousePhysics::IsPhysicalContact(Part) || Part->IsSimulatingPhysics() || Floor.ImpactPoint.Z>Building.Min.Z+80.) continue;
    Point.Z=Floor.ImpactPoint.Z+Half+.8;
    if (World->OverlapBlockingTestByChannel(Point,FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius+4,Half-1),Query)) continue;
    Destination=Point;Found=true;break;
   }
  }
  if (!Found) {UE_LOG(LogTemp,Warning,TEXT("WORKER_EXTERIOR_NO_CLEAR_GROUND %s"),*Worker->GetName());continue;}
  Worker->SetActorLocationAndRotation(Destination,FRotator(0,90,0),false,nullptr,ETeleportType::TeleportPhysics);
  if (Capsule->IsSimulatingPhysics()) {Capsule->SetPhysicsLinearVelocity(FVector::ZeroVector);Capsule->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);}
  Worker->Tags.RemoveAll([](FName Tag){return Tag.ToString().StartsWith(TEXT("WarehouseFloor"));});
  Worker->Tags.AddUnique(TEXT("WarehouseFloor0"));Worker->Tags.AddUnique(TEXT("LayoutType:Other"));
  Occupied.Add(Destination);++Moved;
  UE_LOG(LogTemp,Log,TEXT("WORKER_RELOCATED_OUTSIDE %s %s"),*Worker->GetName(),*Destination.ToString());
 }
 return Moved;
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
 const bool MatchingFloor=!WarehousePC || !WarehousePC->IsObserverView() || WarehousePC->ObserverFloor<0 ||
  FMath::FloorToInt(FMath::Max(0.f,GetActorLocation().Z-GetCapsuleComponent()->GetScaledCapsuleHalfHeight())/400.f)==WarehousePC->ObserverFloor;
 bool Visible=MatchingFloor && !(WarehousePC && WarehousePC->IsWarehouseMenuOpen()) && !PC->HiddenActors.Contains(this) &&
  FVector::DistSquared(Eye,Head)<=FMath::Square(NameDistance) &&
  !GetWorld()->LineTraceSingleByChannel(Hit,Eye,Head,ECC_Visibility,Params);
 if (auto* Widget=Cast<UWarehouseRoleWidget>(RoleLabel->GetUserWidgetObject()))
 {
  Widget->Label=RoleName;
  if (RoleName.ToString().Contains(TEXT("상하차")))
   for (TActorIterator<AWarehouseLogisticsTruck> Truck(GetWorld());Truck;++Truck)
    if (Truck->State==EWarehouseTruckState::Loading && FVector::DistSquared(GetActorLocation(),Truck->GetActorLocation())<FMath::Square(2000.f))
    { Widget->Label=FText::FromString(RoleName.ToString()+TEXT("\n")+Truck->TruckId+TEXT(" · ")+Truck->Status);break; }
  Widget->FontSize=WarehousePC && WarehousePC->IsObserverView() ? WarehouseUIStyle::CompactSize : WarehouseUIStyle::LabelSize;
  if (Visible)
   for (TActorIterator<AWarehouseAreaLabel> Area(GetWorld());Area;++Area)
    if (Area->ReplacesWorkerLabel(Head,RoleName)) { Visible=false; break; }
 }
 RoleLabel->SetVisibility(Visible);
}
