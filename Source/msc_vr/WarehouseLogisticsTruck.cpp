#include "WarehouseLogisticsTruck.h"
#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehouseDamageSystem.h"
#include "WarehouseForklift.h"
#include "WarehousePhysics.h"
#include "msc_vrCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "TwoBoneIK.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"

namespace
{
 const FName TransitTag(TEXT("LogisticsInTransit"));
 FBox PhysicalLocalBounds(AActor* Actor,const FTransform& Reference)
 {
  FBox Result(ForceInit);
  TArray<UPrimitiveComponent*> Parts;Actor->GetComponents(Parts);
  for (auto* Part:Parts) if (WarehousePhysics::IsPhysicalContact(Part))
  {
   if (const auto* Mesh=Cast<UStaticMeshComponent>(Part);Mesh && Mesh->GetStaticMesh())
   {
    // World AABBs grow when a body rotates. Reboxing that AABB in the pallet
    // frame grows it a second time and rejects a correctly sized real load.
    const FBox Box=Mesh->GetStaticMesh()->GetBoundingBox();
    const FTransform Pose=Mesh->GetComponentTransform();
    for (int32 I=0;I<8;++I)
     Result+=Reference.InverseTransformPosition(Pose.TransformPosition(FVector(I&1 ? Box.Max.X:Box.Min.X,I&2 ? Box.Max.Y:Box.Min.Y,I&4 ? Box.Max.Z:Box.Min.Z)));
   }
   else
   {
    const FBox Box=Part->Bounds.GetBox();
    for (int32 I=0;I<8;++I)
     Result+=Reference.InverseTransformPosition(FVector(I&1 ? Box.Max.X:Box.Min.X,I&2 ? Box.Max.Y:Box.Min.Y,I&4 ? Box.Max.Z:Box.Min.Z));
   }
  }
  return Result;
 }
 bool FinitePose(const FTransform& Pose)
 { return !Pose.ContainsNaN() && Pose.GetScale3D().Equals(FVector::OneVector,.001f); }
 bool PoseMatches(const FTransform& A,const FTransform& B)
 { return FVector::Dist(A.GetLocation(),B.GetLocation())<=1.f && A.GetRotation().AngularDistance(B.GetRotation())<=FMath::DegreesToRadians(.5f) && A.GetScale3D().Equals(B.GetScale3D(),.001f); }
}

AWarehouseLogisticsTruck::AWarehouseLogisticsTruck()
{
 PrimaryActorTick.bCanEverTick=true;
 RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("TruckFrame"));
 DriverMesh=CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("TruckDriverVisual"));
 DriverMesh->SetupAttachment(RootComponent);DriverMesh->SetMobility(EComponentMobility::Movable);
 DriverMesh->SetCollisionProfileName(TEXT("NoCollision"));DriverMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
 DriverMesh->SetGenerateOverlapEvents(false);DriverMesh->SetCanEverAffectNavigation(false);
 DriverMesh->SetComponentTickEnabled(false);DriverMesh->ComponentTags.Add(TEXT("WarehouseCinematicVisual"));
}
void AWarehouseLogisticsTruck::OnConstruction(const FTransform& Transform)
{
 Super::OnConstruction(Transform);UpdateDriverPresentation();
}
void AWarehouseLogisticsTruck::UpdateDriverPresentation()
{
 bDriverPoseReady=false;
 DriverMesh->SetVisibility(false);DriverMesh->SetHiddenInGame(true);
 if (!bShowDriver || DriverSeatPose.ContainsNaN() || DriverSteeringWheelCentreCm.ContainsNaN()) return;
 UClass* Appearance=DriverCharacterClass.Get();
 if (!Appearance && GetWorld())
 {
  const AGameModeBase* Mode=GetWorld()->GetAuthGameMode();
  if (!Mode && GetWorld()->GetWorldSettings()->DefaultGameMode) Mode=GetWorld()->GetWorldSettings()->DefaultGameMode->GetDefaultObject<AGameModeBase>();
  if (Mode) Appearance=Mode->DefaultPawnClass.Get();
 }
 if (!Appearance || !Appearance->IsChildOf(ACharacter::StaticClass()))
  Appearance=LoadClass<ACharacter>(nullptr,TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
 const auto* Character=Appearance ? Appearance->GetDefaultObject<ACharacter>() : nullptr;
 const auto* Source=Character ? Character->GetMesh() : nullptr;
 auto* Mesh=Source ? Source->GetSkeletalMeshAsset() : nullptr;
 if (!Mesh) return;
 DriverMesh->SetSkinnedAssetAndUpdate(Mesh);
 DriverMesh->EmptyOverrideMaterials();
 for (int32 I=0;I<Source->GetNumMaterials();++I) DriverMesh->SetMaterial(I,Source->GetMaterial(I));
 const FTransform Alignment(Source->GetRelativeRotation(),FVector::ZeroVector,Source->GetRelativeScale3D());
 DriverMesh->SetRelativeTransform(Alignment*DriverSeatPose);
 const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
 TArray<FTransform> Local=Ref.GetRefBonePose(),Component;
 const int32 Pelvis=Ref.FindBoneIndex(TEXT("pelvis"));
 if (Pelvis==INDEX_NONE) return;
 Local[Pelvis].SetTranslation(FVector::ZeroVector);
 auto Rebuild=[&]()
 {
  Component=Local;
  for (int32 I=1;I<Component.Num();++I) Component[I]=Local[I]*Component[Ref.GetParentIndex(I)];
 };
 Rebuild();
 auto Limb=[&](const TCHAR* UpperName,const TCHAR* LowerName,const TCHAR* EndName,FVector Goal,FVector Pole)
 {
  const int32 Upper=Ref.FindBoneIndex(UpperName),Lower=Ref.FindBoneIndex(LowerName),End=Ref.FindBoneIndex(EndName);
  if (Upper==INDEX_NONE || Lower==INDEX_NONE || End==INDEX_NONE || Ref.GetParentIndex(Lower)!=Upper || Ref.GetParentIndex(End)!=Lower) return false;
  FTransform A=Component[Upper],B=Component[Lower],C=Component[End];
  AnimationCore::SolveTwoBoneIK(A,B,C,Alignment.InverseTransformPosition(Pole),Alignment.InverseTransformPosition(Goal),false,1.,1.);
  Local[Upper]=A.GetRelativeTransform(Component[Ref.GetParentIndex(Upper)]);
  Local[Lower]=B.GetRelativeTransform(A);Local[End]=C.GetRelativeTransform(B);Rebuild();return true;
 };
 // Fixed seat/steering pose, preserving the mannequin's actual limb lengths.
 // It has no capsule, AI, physical mass or world collision of its own.
 for (const TCHAR* Suffix:{TEXT("_l"),TEXT("_r")})
 {
  const FName Thigh(*(FString(TEXT("thigh"))+Suffix));
  const int32 Index=Ref.FindBoneIndex(Thigh);
  if (Index==INDEX_NONE) return;
  const float Side=Alignment.TransformPosition(Component[Index].GetLocation()).Y<0 ? -1.f : 1.f;
  const FString S(Suffix);
  if (!Limb(*(TEXT("thigh")+S),*(TEXT("calf")+S),*(TEXT("foot")+S),FVector(45,Side*14,-40),FVector(75,Side*18,0)) ||
      !Limb(*(TEXT("upperarm")+S),*(TEXT("lowerarm")+S),*(TEXT("hand")+S),DriverSteeringWheelCentreCm+FVector(0,Side*17,0),FVector(12,Side*55,25))) return;
 }
 DriverMesh->BoneSpaceTransforms=MoveTemp(Local);
 DriverMesh->MarkRefreshTransformDirty();DriverMesh->RefreshBoneTransforms();DriverMesh->UpdateBounds();DriverMesh->MarkRenderTransformDirty();
 bDriverPoseReady=true;DriverMesh->SetVisibility(true);DriverMesh->SetHiddenInGame(State==EWarehouseTruckState::Departed);
}

void AWarehouseLogisticsTruck::BeginPlay()
{
 Super::BeginPlay();LastPose=GetActorTransform();UpdateDriverPresentation();
 for (AWarehousePallet* Pallet:InitialPallets) InitialManifest.Add(Pallet);
 ManifestPallets.Reset();
 for (AActor* Part:TruckParts) if (IsValid(Part)) DockParts.Add({Part,Part->GetActorTransform().GetRelativeTransform(GetActorTransform())});
 if (!PartsValid()) { bConfigurationUnavailable=true;State=EWarehouseTruckState::Unavailable;Status=TEXT("트럭 구성 부품 또는 도크가 없습니다."); }
 else Status=TEXT("도크 대기");
}

bool AWarehouseLogisticsTruck::PartsValid() const
{
 if (TruckId.IsEmpty() || DockId.IsEmpty() || !IsValid(DockAnchor) || TruckParts.IsEmpty()) return false;
 for (AActor* Part:TruckParts)
  if (!IsValid(Part) || Part==this || Part==DockAnchor || Part->ActorHasTag(TEXT("LogisticsFixedDock"))) return false;
 return true;
}

bool AWarehouseLogisticsTruck::IsReady() const
{
 for (const auto& Part:DockParts)
  if (!Part.Actor.IsValid() || !PoseMatches(Part.Actor->GetActorTransform(),Part.RelativePose*GetActorTransform())) return false;
 return State==EWarehouseTruckState::AtDock && PartsValid() && MeasuredSpeedCm<1.f &&
  FVector::Dist(GetActorLocation(),DockAnchor->GetActorLocation())<=10.f &&
  GetActorQuat().AngularDistance(DockAnchor->GetActorQuat())<=FMath::DegreesToRadians(3.f);
}

bool AWarehouseLogisticsTruck::Fail(const FString& Message)
{ Status=Message;return false; }

FBox AWarehouseLogisticsTruck::UnitBounds(const FUnit& Unit,const FTransform& PalletPose,const FTransform& Reference)
{
 FBox Result(ForceInit);
 const FTransform Current=Unit.Pallet.IsValid() ? Unit.Pallet->GetActorTransform():FTransform::Identity;
 for (const auto& Member:Unit.Members) if (auto* Actor=Member.Actor.Get())
 {
  const FBox Box=PhysicalLocalBounds(Actor,Current);
  if (!Box.IsValid) continue;
  for (int32 I=0;I<8;++I)
   Result+=Reference.InverseTransformPosition(PalletPose.TransformPosition(FVector(I&1 ? Box.Max.X:Box.Min.X,I&2 ? Box.Max.Y:Box.Min.Y,I&4 ? Box.Max.Z:Box.Min.Z)));
 }
 return Result;
}

bool AWarehouseLogisticsTruck::FitsSlot(const FUnit& Unit,const FTransform& PalletPose,int32 Slot) const
{
 if (!LoadSlots.IsValidIndex(Slot) || !FinitePose(LoadSlots[Slot]) || LoadSlotSizeCm.ContainsNaN() || LoadSlotSizeCm.GetMin()<=0) return false;
 const FTransform Frame=LoadSlots[Slot]*GetActorTransform();
 const FBox Box=UnitBounds(Unit,PalletPose,Frame);
 if (!Box.IsValid || Box.Min.X < -LoadSlotSizeCm.X*.5-.5 || Box.Max.X>LoadSlotSizeCm.X*.5+.5 ||
  Box.Min.Y < -LoadSlotSizeCm.Y*.5-.5 || Box.Max.Y>LoadSlotSizeCm.Y*.5+.5 ||
  Box.Min.Z<-.5 || Box.Max.Z>LoadSlotSizeCm.Z+.5)
 {
  UE_LOG(LogTemp,Warning,TEXT("LOGISTICS_SLOT_BOUNDS_REJECT truck=%s pallet=%s slot=%d bounds=%s size=%s"),
   *TruckId,*GetNameSafe(Unit.Pallet.Get()),Slot,*Box.ToString(),*LoadSlotSizeCm.ToString());
  return false;
 }
 // Reject overlapping occupied slots using the complete existing load footprint.
 const FBox Candidate=UnitBounds(Unit,PalletPose,GetActorTransform());
 for (const FUnit& Other:Units)
 {
  if (Other.Pallet==Unit.Pallet) continue;
  if (Other.Slot==Slot || !Other.Pallet.IsValid())
  {
   UE_LOG(LogTemp,Warning,TEXT("LOGISTICS_SLOT_OCCUPIED truck=%s pallet=%s slot=%d other=%s otherSlot=%d"),
    *TruckId,*GetNameSafe(Unit.Pallet.Get()),Slot,*GetNameSafe(Other.Pallet.Get()),Other.Slot);
   return false;
  }
  const FBox Existing=UnitBounds(Other,Other.Pallet->GetActorTransform(),GetActorTransform());
  if (Existing.IsValid && Candidate.ExpandBy(-.1f).Intersect(Existing.ExpandBy(-.1f)))
  {
   UE_LOG(LogTemp,Warning,TEXT("LOGISTICS_SLOT_OVERLAP truck=%s pallet=%s slot=%d candidate=%s other=%s otherSlot=%d bounds=%s"),
    *TruckId,*GetNameSafe(Unit.Pallet.Get()),Slot,*Candidate.ToString(),*GetNameSafe(Other.Pallet.Get()),Other.Slot,*Existing.ToString());
   return false;
  }
 }
 return true;
}

void AWarehouseLogisticsTruck::MoveUnit(FUnit& Unit,const FTransform& PalletPose)
{
 for (const auto& Member:Unit.Members) if (auto* Actor=Member.Actor.Get())
  Actor->SetActorTransform(Member.RelativePose*PalletPose,false,nullptr,ETeleportType::TeleportPhysics);
}

void AWarehouseLogisticsTruck::RestoreUnit(FUnit& Unit)
{
 for (const FBodyState& Saved:Unit.Bodies) if (auto* Body=Saved.Body.Get())
 {
  Body->SetCollisionEnabled(Saved.Collision);Body->SetEnableGravity(Saved.Gravity);
  Body->SetSimulatePhysics(Saved.Simulated);
  if (Saved.Simulated)
  {
   // This is the explicit cinematic/warehouse boundary, not a warehouse physics correction.
   Body->SetPhysicsLinearVelocity(FVector::ZeroVector);Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
   Body->WakeAllRigidBodies();
  }
 }
 for (const auto& Member:Unit.Members) if (auto* Actor=Member.Actor.Get())
 {
  Actor->Tags.Remove(TransitTag);
  // An unloaded unit is independent warehouse inventory. Keeping its authored
  // truck group would drag the whole dock assembly on a later cargo layout edit.
  if (Actor->IsA<AWarehousePallet>() || Actor->IsA<AWarehouseCargo>())
  {
   Actor->Tags.RemoveAll([](const FName& Tag) { return Tag.ToString().StartsWith(TEXT("LayoutGroup:TruckDock")); });
   Actor->Tags.Remove(TEXT("TruckCargo"));
  }
 }
 for (const auto& Saved:Unit.Joints) if (auto* Joint=Saved.Joint.Get())
 {
  Joint->InitComponentConstraint();
  Joint->SetConstraintReferenceFrame(EConstraintFrame::Frame1,Saved.Frame1);
  Joint->SetConstraintReferenceFrame(EConstraintFrame::Frame2,Saved.Frame2);
 }
}

bool AWarehouseLogisticsTruck::TransferUnit(AWarehousePallet* Pallet,int32 SlotIndex,bool IntoTruck,FTransform WorldDestination)
{ return Transfer(Pallet,SlotIndex,IntoTruck,WorldDestination); }

bool AWarehouseLogisticsTruck::Transfer(AWarehousePallet* Pallet,int32 SlotIndex,bool IntoTruck,FTransform WorldDestination,const TArray<AActor*>* InitialCargo)
{
 if (!HasAuthority() || !IsReady() || !IsValid(Pallet)) return Fail(TEXT("정차한 트럭과 유효 팔레트가 필요합니다."));
 if (IntoTruck)
 {
  if (Pallet->ActorHasTag(TransitTag) || ManifestPallets.Contains(Pallet)) return Fail(TEXT("이미 운송 중인 팔레트입니다."));
  auto* Strength=AWarehouseDamageSystem::Find(this);
  if (!Strength || Strength->HasFailed(Pallet)) return Fail(TEXT("팔레트의 하중·손상 정보를 확인할 수 없습니다."));
  TArray<AActor*> Members={Pallet};
  const TArray<AActor*> Supported=InitialCargo ? *InitialCargo:Strength->GetSupportedActors(Pallet);
  for (AActor* Actor:Supported)
  {
   if (!Cast<AWarehouseCargo>(Actor)) return Fail(TEXT("팔레트 위에 상자 이외의 물체가 있습니다."));
   Members.AddUnique(Actor);
  }
  for (auto* Box:Pallet->GetWrappedCargo()) Members.AddUnique(Box);
  for (TActorIterator<Amsc_vrCharacter> It(GetWorld());It;++It)
   if (Members.Contains(It->GetHeldCargo())) return Fail(TEXT("작업자가 들고 있는 화물은 상차할 수 없습니다."));
  for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
   if (It->GetSupportedPallet()==Pallet) return Fail(TEXT("지게차 포크 인출 후 상차할 수 있습니다."));
  FUnit Unit;Unit.Pallet=Pallet;Unit.Slot=SlotIndex;
  const FTransform Initial=Pallet->GetActorTransform();
  for (AActor* Actor:Members)
  {
   if (!IsValid(Actor) || Actor->ActorHasTag(TransitTag) || Strength->HasFailed(Actor) || Actor->GetAttachParentActor())
    return Fail(TEXT("화물 상태 또는 다른 운송 예약을 확인하세요."));
   Unit.Members.Add({Actor,Actor->GetActorTransform().GetRelativeTransform(Initial)});
   if (auto* Box=Cast<AWarehouseCargo>(Actor)) Unit.MassKg+=Box->GrossMassKg;
   else Unit.MassKg+=Pallet->PalletMassKg+FMath::Max(0.f,Pallet->PayloadMassKg);
   TArray<UPrimitiveComponent*> Parts;Actor->GetComponents(Parts);
   for (auto* Body:Parts) if (WarehousePhysics::IsPhysicalContact(Body))
    Unit.Bodies.Add({Body,Body->IsSimulatingPhysics(),Body->IsGravityEnabled(),Body->GetCollisionEnabled()});
   TArray<UPhysicsConstraintComponent*> Joints;Actor->GetComponents(Joints);
   for (auto* Joint:Joints)
    Unit.Joints.Add({Joint,Joint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame1),Joint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame2)});
  }
  if (!FMath::IsFinite(RatedPayloadKg) || RatedPayloadKg<=0 || !FMath::IsFinite(Unit.MassKg) || Unit.MassKg<=0 || PayloadMassKg()+Unit.MassKg>RatedPayloadKg)
   return Fail(TEXT("트럭 정격 적재 중량을 초과합니다."));
  if (!LoadSlots.IsValidIndex(SlotIndex)) return Fail(TEXT("유효 적재 자리가 없습니다."));
  const FTransform Slot=LoadSlots[SlotIndex]*GetActorTransform();
  const FBox PalletLocal=PhysicalLocalBounds(Pallet,Initial);
  if (!PalletLocal.IsValid) return Fail(TEXT("팔레트의 실제 충돌 크기를 확인할 수 없습니다."));
  const FVector Bottom(PalletLocal.GetCenter().X,PalletLocal.GetCenter().Y,PalletLocal.Min.Z);
  const FTransform Goal(Slot.GetRotation(),Slot.GetLocation()-Slot.GetRotation().RotateVector(Bottom*Initial.GetScale3D()),Initial.GetScale3D());
  if (!FitsSlot(Unit,Goal,SlotIndex)) return Fail(TEXT("팔레트·화물이 적재 자리보다 크거나 다른 화물과 겹칩니다."));
  for (const auto& Member:Unit.Members) if (auto* Actor=Member.Actor.Get()) Actor->Tags.AddUnique(TransitTag);
  for (const auto& Body:Unit.Bodies) if (auto* Component=Body.Body.Get()) Component->SetSimulatePhysics(false);
  MoveUnit(Unit,Goal);Unit.LocalPalletPose=Goal.GetRelativeTransform(GetActorTransform());
  Units.Add(MoveTemp(Unit));ManifestPallets.Add(Pallet);
 }
 else
 {
  const int32 Index=Units.IndexOfByPredicate([&](const FUnit& Unit){return Unit.Pallet.Get()==Pallet;});
  if (Index==INDEX_NONE || !IsUnitOnTruck(Pallet) || !FinitePose(WorldDestination))
   return Fail(TEXT("이번 트럭의 화물 또는 하역 위치가 유효하지 않습니다."));
  auto& Unit=Units[Index];
  const FBox Local=PhysicalLocalBounds(Pallet,Pallet->GetActorTransform());
  if (!Local.IsValid) return Fail(TEXT("팔레트의 실제 충돌 크기를 확인할 수 없습니다."));
  const FVector Bottom(Local.GetCenter().X,Local.GetCenter().Y,Local.Min.Z);
  const FTransform Goal(WorldDestination.GetRotation(),WorldDestination.GetLocation()-WorldDestination.GetRotation().RotateVector(Bottom*Pallet->GetActorScale3D()),Pallet->GetActorScale3D());
  MoveUnit(Unit,Goal);RestoreUnit(Unit);Units.RemoveAt(Index);ManifestPallets.Remove(Pallet);
 }
 State=EWarehouseTruckState::Loading;TransferRemaining=FMath::Max(.1f,TransferIntervalSeconds);
 Status=IntoTruck ? TEXT("NPC 상차 중"):TEXT("NPC 하역 중");
 UE_LOG(LogTemp,Log,TEXT("LOGISTICS_UNIT_TRANSFER truck=%s pallet=%s into=%d"),*TruckId,*Pallet->GetName(),IntoTruck);
 return true;
}

bool AWarehouseLogisticsTruck::IsUnitOnTruck(AWarehousePallet* Pallet) const
{
 if (!IsValid(Pallet) || State==EWarehouseTruckState::Departed || !ManifestPallets.Contains(Pallet)) return false;
 const FUnit* Unit=Units.FindByPredicate([&](const FUnit& Entry){return Entry.Pallet.Get()==Pallet;});
 if (!Unit || !LoadSlots.IsValidIndex(Unit->Slot) || !Pallet->ActorHasTag(TransitTag)) return false;
 const FTransform Expected=Unit->LocalPalletPose*GetActorTransform();
 if (!PoseMatches(Pallet->GetActorTransform(),Expected)) return false;
 for (const auto& Member:Unit->Members)
 {
  auto* Actor=Member.Actor.Get();
  if (!Actor || !Actor->ActorHasTag(TransitTag) || !PoseMatches(Actor->GetActorTransform(),Member.RelativePose*Expected)) return false;
 }
 return FitsSlot(*Unit,Pallet->GetActorTransform(),Unit->Slot);
}

float AWarehouseLogisticsTruck::PayloadMassKg() const
{
 float Total=0;
 for (const auto& Unit:Units) if (Unit.Pallet.IsValid()) Total+=Unit.MassKg;
 return Total;
}

TArray<AActor*> AWarehouseLogisticsTruck::GetUnitCargo(AWarehousePallet* Pallet) const
{
 TArray<AActor*> Result;
 const FUnit* Unit=Units.FindByPredicate([&](const FUnit& Entry){return Entry.Pallet.Get()==Pallet;});
 if (Unit) for (const auto& Member:Unit->Members) if (auto* Cargo=Cast<AWarehouseCargo>(Member.Actor.Get())) Result.Add(Cargo);
 return Result;
}

bool AWarehouseLogisticsTruck::Depart()
{
 if (!IsManifestReady()) return Fail(TEXT("초기 입고 화물 명세 등록이 끝나지 않았습니다."));
 if (!HasAuthority() || !IsReady() || DepartureRoute.IsEmpty()) return Fail(TEXT("출차 경로와 정차 상태를 확인하세요."));
 for (const FVector& Point:DepartureRoute) if (Point.ContainsNaN()) return Fail(TEXT("유효 출차 경로가 없습니다."));
 if (ManifestPallets.Num()!=Units.Num()) return Fail(TEXT("트럭 화물 명세가 일치하지 않습니다."));
 for (AWarehousePallet* Pallet:ManifestPallets) if (!IsUnitOnTruck(Pallet)) return Fail(TEXT("미확인 또는 이탈한 화물이 있습니다."));
 DepartureParts.Reset();
 for (AActor* Part:TruckParts)
 {
  DepartureParts.Add({Part,Part->GetActorTransform().GetRelativeTransform(GetActorTransform())});
  TArray<USceneComponent*> Components;Part->GetComponents(Components);
  for (auto* Component:Components) Component->SetMobility(EComponentMobility::Movable);
  TArray<UPrimitiveComponent*> Bodies;Part->GetComponents(Bodies);
  for (auto* Body:Bodies) if (Body->IsSimulatingPhysics()) Body->SetSimulatePhysics(false);
 }
 RouteIndex=0;State=EWarehouseTruckState::Departing;Status=TEXT("출차 중");return true;
}

bool AWarehouseLogisticsTruck::RetryInitialManifest()
{
 if (!HasAuthority() || !bInitialManifestError || !PartsValid()) return false;
 bInitialManifestError=false;bConfigurationUnavailable=false;
 State=EWarehouseTruckState::AtDock;Status=TEXT("초기 입고 명세 등록 재시도");return true;
}

void AWarehouseLogisticsTruck::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if (DriverMesh) DriverMesh->SetHiddenInGame(!bShowDriver || !bDriverPoseReady || State==EWarehouseTruckState::Departed);
 MeasuredSpeedCm=DeltaSeconds>0 ? FVector::Dist(LastPose.GetLocation(),GetActorLocation())/DeltaSeconds:0;
 LastPose=GetActorTransform();
 if (!HasAuthority() || State==EWarehouseTruckState::Departed) return;
 if (!PartsValid()) { bConfigurationUnavailable=true;State=EWarehouseTruckState::Unavailable;Status=TEXT("트럭 부품 또는 도크가 삭제되었습니다.");return; }
 if (DockParts.IsEmpty()) for (AActor* Part:TruckParts) DockParts.Add({Part,Part->GetActorTransform().GetRelativeTransform(GetActorTransform())});
 if (bConfigurationUnavailable)
 { bConfigurationUnavailable=false;if (!bInitialManifestError) State=EWarehouseTruckState::AtDock; }
 if (!bInitialManifestError && !InitialManifest.IsEmpty() && IsReady())
 {
  // An arriving truck already owns its secured stock. Snapshot every authored
  // stack before changing any body, then adopt the whole manifest in this tick.
  // Spacing these registrations across physics steps can spill still-unregistered
  // cartons and silently capture an incomplete inbound manifest.
  auto* Strength=AWarehouseDamageSystem::Find(this);
  if (!Strength) { bInitialManifestError=true;State=EWarehouseTruckState::Unavailable;Status=TEXT("초기 입고 하중 관리가 없습니다.");return; }
  TArray<TArray<AActor*>> InitialContents;InitialContents.Reserve(InitialManifest.Num());
  for (int32 I=0;I<InitialManifest.Num();++I)
  {
   auto* Pallet=InitialManifest[I].Get();
   if (!Pallet) { bInitialManifestError=true;State=EWarehouseTruckState::Unavailable;Status=TEXT("초기 입고 팔레트가 없습니다.");return; }
   InitialContents.Add(Strength->GetSupportedActors(Pallet,I==0));
  }
  for (const auto& Contents:InitialContents)
  {
   AWarehousePallet* Pallet=InitialManifest[0].Get();
   const int32 Slot=ManifestPallets.Num();
   if (!Transfer(Pallet,Slot,true,FTransform::Identity,&Contents))
   { bInitialManifestError=true;State=EWarehouseTruckState::Unavailable;Status=TEXT("초기 입고 명세 등록 실패: ")+Status;return; }
   InitialManifest.RemoveAt(0);TransferRemaining=0;State=EWarehouseTruckState::AtDock;
  }
  Status=TEXT("도크 대기");
 }
 if (State==EWarehouseTruckState::Loading)
 {
  TransferRemaining-=DeltaSeconds;
  if (TransferRemaining<=0) { State=EWarehouseTruckState::AtDock;Status=TEXT("도크 대기"); }
  return;
 }
 if (State!=EWarehouseTruckState::Departing) return;
 for (const FUnit& Unit:Units) if (!IsUnitOnTruck(Unit.Pallet.Get()))
 { State=EWarehouseTruckState::Unavailable;Status=TEXT("출차 중 화물 이탈 또는 삭제가 발생했습니다.");return; }
 float Remaining=FMath::Max(0.f,DeltaSeconds)*FMath::Max(1.f,DepartureSpeedCm);
 while (Remaining>0 && DepartureRoute.IsValidIndex(RouteIndex))
 {
  const FVector Target=DepartureRoute[RouteIndex],Delta=Target-GetActorLocation();const float Distance=Delta.Size();
  if (Distance<=Remaining) { SetActorLocation(Target,false,nullptr,ETeleportType::TeleportPhysics);Remaining-=Distance;++RouteIndex; }
  else { SetActorLocation(GetActorLocation()+Delta/Distance*Remaining,false,nullptr,ETeleportType::TeleportPhysics);Remaining=0; }
 }
 for (const auto& Part:DepartureParts) if (auto* Actor=Part.Actor.Get()) Actor->SetActorTransform(Part.RelativePose*GetActorTransform(),false,nullptr,ETeleportType::TeleportPhysics);
 for (auto& Unit:Units) MoveUnit(Unit,Unit.LocalPalletPose*GetActorTransform());
 if (RouteIndex<DepartureRoute.Num()) return;
 State=EWarehouseTruckState::Departed;Status=TEXT("출차 완료 · 운송 기록 보존");
 DriverMesh->SetHiddenInGame(true);
 for (const auto& Part:DepartureParts) if (auto* Actor=Part.Actor.Get()) { Actor->SetActorHiddenInGame(true);Actor->SetActorEnableCollision(false); }
 for (const auto& Unit:Units) for (const auto& Member:Unit.Members) if (auto* Actor=Member.Actor.Get())
 { Actor->SetActorHiddenInGame(true);Actor->SetActorEnableCollision(false); }
 UE_LOG(LogTemp,Log,TEXT("LOGISTICS_TRUCK_DEPARTED truck=%s units=%d kg=%.1f"),*TruckId,ManifestPallets.Num(),PayloadMassKg());
}

void AWarehouseLogisticsTruck::EndPlay(const EEndPlayReason::Type Reason)
{
 // Removing a live truck must not leave warehouse cargo permanently kinematic.
 if (Reason==EEndPlayReason::Destroyed && State!=EWarehouseTruckState::Departed)
  for (auto& Unit:Units) RestoreUnit(Unit);
 Super::EndPlay(Reason);
}
