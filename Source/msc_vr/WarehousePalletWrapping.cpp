#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehouseDamageSystem.h"
#include "WarehousePhysics.h"
#include "WarehouseInboundZone.h"
#include "msc_vrCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AWarehousePallet* AWarehouseCargo::GetWrappedPallet() const
{
 auto* Pallet=WrappedPallet.Get();
 return Pallet && Pallet->ReceiptState==EWarehouseReceiptState::Confirmed ? Pallet : nullptr;
}

FText AWarehousePallet::GetReceiptDescription() const
{
 return FText::FromString(bWrapping ? TEXT("입고 검수 완료 · 랩 포장 중") : ReceiptState==EWarehouseReceiptState::Confirmed ?
  TEXT("입고 확정 · 랩 포장") : ReceiptState==EWarehouseReceiptState::WrapTorn ?
  TEXT("랩 파손 · 재검수 필요") : TEXT("입고 미확정"));
}

TArray<AWarehouseCargo*> AWarehousePallet::GetWrappedCargo() const
{
 TArray<AWarehouseCargo*> Result;
 if (ReceiptState==EWarehouseReceiptState::Confirmed)
  for (AWarehouseCargo* Cargo:WrappedCargo) if (IsValid(Cargo)) Result.Add(Cargo);
 return Result;
}

bool AWarehousePallet::GetWrappableCargo(const TArray<AActor*>& Loads,TArray<AWarehouseCargo*>& Cargo,bool Refresh)
{
 if (ReceiptState!=EWarehouseReceiptState::Pending || !Body->IsSimulatingPhysics()) return false;
 auto* Strength=AWarehouseDamageSystem::Find(this);
 if (!Strength || Strength->HasFailed(this)) return false;
 for (auto* Load:Loads) if (!IsValid(Load)) return false;
 for (auto* Load:Loads) if (Cast<AWarehousePallet>(Load)) return false;
 for (auto* Load:Loads) if (auto* Box=Cast<AWarehouseCargo>(Load))
 {
  if ((Box->WrappedPallet.IsValid() && Box->WrappedPallet.Get()!=this) || Box->GetWrappedPallet() ||
      !Box->GetCargoBody()->IsSimulatingPhysics() || Strength->HasFailed(Box)) return false;
  Cargo.AddUnique(Box);
 }
 if (Cargo.IsEmpty()) return false;
 // Confirm only the current physical stack, never arbitrary nearby parcels.
 // Start/commit retain an exact fresh query. The 10 Hz progress poll shares
 // that physics interval's graph with inventory, dispatch and damage checks.
 if (!Refresh) Strength->EnsureSupportSnapshot();
 const TArray<AActor*> Supported=Strength->GetSupportedActors(this,Refresh);
 for (auto* Box:Cargo) if (!Supported.Contains(Box)) return false;
 for (auto* Actor:Supported) if (auto* Box=Cast<AWarehouseCargo>(Actor);Box && !Cargo.Contains(Box)) return false;
 for (TActorIterator<Amsc_vrCharacter> It(GetWorld());It;++It)
  if (It->GetHeldCargo()==this || Supported.Contains(It->GetHeldCargo())) return false;
 return true;
}

float AWarehousePallet::GetWrappingRemainingSeconds() const
{ return bWrapping ? WrappingStartedSeconds<0 ? 1.f : float(FMath::Max(0.,1.-(GetWorld()->GetTimeSeconds()-WrappingStartedSeconds))) : 0.f; }

bool AWarehousePallet::BeginInboundWrapping(const TArray<AActor*>& Loads,AWarehouseInboundZone* Zone)
{
 TArray<AWarehouseCargo*> Cargo;
 if (bWrapping || !IsValid(Zone) || !Zone->ContainsPallet(this) || !GetWrappableCargo(Loads,Cargo)) return false;
 auto* White=WrappingHighlightMaterial.LoadSynchronous();
 if (!White) { UE_LOG(LogTemp,Warning,TEXT("PALLET_WRAPPING_MATERIAL_MISSING %s"),*GetName());return false; }
 // Reserve shared support until completion; the public getter still exposes confirmed units only.
 WrappingParts.Add(Body);for (auto* Box:Cargo)
 { Box->WrappedPallet=this;WrappingParts.Add(Box->GetCargoBody());WrappingCargo.Add(Box); }
 for (UStaticMeshComponent* Part:WrappingParts)
 {
  WrappingPreviousOverlays.Add(Part->GetOverlayMaterial());WrappingPoses.Add(Part->GetComponentTransform());
  // Native translucent overlays are drawn by the conventional mesh renderer.
  // Switch only these components for the brief wash; keep the asset/collision
  // and any independent carry-mode bDisallowNanite setting unchanged.
  WrappingPreviousForceDisableNanite.Add(Part->IsForceDisableNanite());
  Part->SetForceDisableNanite(true);Part->SetOverlayMaterial(White);
 }
 WrappingZone=Zone;WrappingStartedSeconds=-1;bWrapping=true;SetActorTickEnabled(true);
 UE_LOG(LogTemp,Log,TEXT("PALLET_WRAPPING_STARTED %s cartons=%d duration=1 simulation second"),*GetName(),Cargo.Num());
 return true;
}

void AWarehousePallet::CancelInboundWrapping()
{
 for (AWarehouseCargo* Box:WrappingCargo) if (IsValid(Box) && Box->WrappedPallet.Get()==this) Box->WrappedPallet.Reset();
 for (int32 I=0;I<WrappingParts.Num();++I) if (IsValid(WrappingParts[I]))
 {
  WrappingParts[I]->SetOverlayMaterial(WrappingPreviousOverlays.IsValidIndex(I) ? WrappingPreviousOverlays[I].Get() : nullptr);
  if (WrappingPreviousForceDisableNanite.IsValidIndex(I)) WrappingParts[I]->SetForceDisableNanite(WrappingPreviousForceDisableNanite[I]);
 }
 WrappingParts.Reset();WrappingPreviousOverlays.Reset();WrappingPreviousForceDisableNanite.Reset();WrappingPoses.Reset();WrappingCargo.Reset();WrappingZone.Reset();WrappingStartedSeconds=-1;bWrapping=false;
 if (ReceiptState!=EWarehouseReceiptState::Confirmed) SetActorTickEnabled(false);
}

void AWarehousePallet::InitializeRackWrapping(UWorld* World)
{
 if (!World || !World->IsGameWorld()) return;
 int32 Count=0;
 for (TActorIterator<AWarehouseDamageSystem> It(World);It;++It)
 {
  auto* Strength=*It;
  // One exact graph for the whole initial scene, not one full scan per pallet.
  Strength->GetSupportedActors(nullptr,true);
  TSet<AWarehousePallet*> RackPallets;
  for (const auto& Spec:Strength->Objects)
  {
   if (Spec.Failure!=EWarehouseFailure::Rack || Spec.bFailed) continue;
   for (AActor* Member:Spec.Members) if (IsValid(Member))
   {
    for (auto* Load:Strength->GetSupportedActors(Member,false))
     if (auto* Pallet=Cast<AWarehousePallet>(Load)) RackPallets.Add(Pallet);
    break; // All members of this rack share one support profile.
   }
  }
  for (auto* Pallet:RackPallets)
   if (Pallet->ReceiptState==EWarehouseReceiptState::Pending &&
       Pallet->ConfirmInbound(Strength->GetSupportedActors(Pallet,false),false)) ++Count;
 }
 UE_LOG(LogTemp,Log,TEXT("INITIAL_RACK_WRAPPING pallets=%d"),Count);
}

bool AWarehousePallet::ConfirmInbound(const TArray<AActor*>& Loads,bool Refresh)
{
 TArray<AWarehouseCargo*> Cargo;
 if (bWrapping || !GetWrappableCargo(Loads,Cargo,Refresh)) return false;
 WrappedCargo=Cargo;
 for (auto* Box:Cargo)
 {
  auto* Joint=NewObject<UPhysicsConstraintComponent>(this);
  AddInstanceComponent(Joint);
  Joint->RegisterComponent();
  Joint->SetWorldTransform(FTransform(GetActorQuat(),Box->GetCargoBody()->Bounds.Origin));
  Joint->SetProjectionEnabled(false);
  Joint->ConstraintInstance.DisableMassConditioning();
  Joint->ConstraintInstance.DisableParentDominates();
  Joint->ConstraintInstance.SetShockPropagationParams(false,0);
  Joint->SetLinearXLimit(LCM_Limited,1.5f);Joint->SetLinearYLimit(LCM_Limited,1.5f);Joint->SetLinearZLimit(LCM_Limited,1.5f);
  Joint->SetAngularSwing1Limit(ACM_Limited,3);Joint->SetAngularSwing2Limit(ACM_Limited,3);Joint->SetAngularTwistLimit(ACM_Limited,3);
  auto& Profile=Joint->ConstraintInstance.ProfileInstance;
  Profile.bEnableProjection=false;
  // Small free play represents film compliance; native finite break limits
  // release the independent bodies under a separating load.
  Profile.LinearLimit.bSoftConstraint=false;
  Profile.ConeLimit.bSoftConstraint=false;Profile.TwistLimit.bSoftConstraint=false;
  Joint->SetDisableCollision(false);
  Joint->SetLinearBreakable(true,FMath::Max(1.f,WrapBreakForceN)*100.f); // N -> kg cm / s².
  Joint->SetAngularBreakable(true,FMath::Max(1.f,WrapBreakTorqueNm)*10000.f); // Nm -> kg cm² / s².
  Joint->OnConstraintBroken.AddDynamic(this,&AWarehousePallet::OnWrapJointBroken);
  Joint->SetConstrainedComponents(Body,NAME_None,Box->GetCargoBody(),NAME_None);
  Box->WrappedPallet=this;WrapJoints.Add(Joint);
 }
 ReceiptState=EWarehouseReceiptState::Confirmed;
 BuildWrapFilm();SetActorTickEnabled(true);
 UE_LOG(LogTemp,Log,TEXT("INBOUND_CONFIRMED %s cartons=%d"),*GetName(),WrappedCargo.Num());
 return true;
}

void AWarehousePallet::BuildWrapFilm()
{
 for (USplineMeshComponent* Part:WrapFilm) if (Part) Part->DestroyComponent();
 WrapFilm.Reset();
 auto* Material=WrapMaterial.LoadSynchronous();
 auto* Sheet=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane"));
 if (!Material || !Sheet) return;
 const FTransform Frame=GetActorTransform();
 TArray<FBox> Boxes;
 const FBox PalletBounds=Body->GetStaticMesh()->GetBoundingBox();
 const float BaseHeight=FMath::Max(float(PalletBounds.Min.Z),float(PalletBounds.Max.Z)-3.f);
 TArray<float> Levels{BaseHeight}; // Bind the top boards, leaving the fork openings clear.
 FBox BaseRing=PalletBounds;
 constexpr float Clearance=.4f;
 for (AWarehouseCargo* Box:WrappedCargo) if (IsValid(Box))
 {
  const auto* Part=Box->GetCargoBody();
  FBox Bounds=Part->CalcBounds(Part->GetComponentTransform().GetRelativeTransform(Frame)).GetBox();
  Boxes.Add(Bounds);BaseRing+=Bounds;
  Levels.Add(FMath::Max(BaseHeight+.1f,float(Bounds.Max.Z)+Clearance));
 }
 if (Boxes.IsEmpty()) return;
 Levels.Sort();
 for (int32 I=Levels.Num()-1;I>0;--I) if (Levels[I]-Levels[I-1]<.2f) Levels.RemoveAt(I);
 auto Ring=[&](float Height)
 {
  FBox Bounds(ForceInit);
  if (Height<=BaseHeight+.01f) Bounds=BaseRing;
  else for (const auto& Box:Boxes) if (Box.Max.Z+Clearance>=Height-.01f) Bounds+=Box;
  return Bounds.ExpandBy(Clearance);
 };
 auto NewSheet=[&](FName Tag)
 {
  auto* Film=NewObject<USplineMeshComponent>(this);
  AddInstanceComponent(Film);Film->SetMobility(EComponentMobility::Movable);
  Film->SetupAttachment(Body);Film->SetRelativeTransform(FTransform::Identity);
  Film->SetStaticMesh(Sheet);Film->SetMaterial(0,Material);Film->SetForwardAxis(ESplineMeshAxis::X,false);
  Film->bSmoothInterpRollScale=false;
  Film->SetCollisionEnabled(ECollisionEnabled::NoCollision);Film->SetGenerateOverlapEvents(false);
  Film->SetCanEverAffectNavigation(false);Film->SetCastShadow(false);
  Film->ComponentTags.Add(TEXT("PalletWrapVisual"));Film->ComponentTags.Add(Tag);
  WrapFilm.Add(Film);return Film;
 };
 for (int32 I=1;I<Levels.Num();++I)
 {
  const FBox Bottom=Ring(Levels[I-1]),Top=Ring(Levels[I]);
  if (!Bottom.IsValid || !Top.IsValid) continue;
  for (int32 Side=0;Side<4;++Side)
  {
   // The torn seam is open, with the remaining film visibly hanging away.
   if (ReceiptState==EWarehouseReceiptState::WrapTorn && Side==0) continue;
   const bool X=Side<2;const float Sign=Side%2 ? 1.f : -1.f;
   // Keep the spline vertical. Interpolated offsets place its four corners
   // exactly on both rings, even when an upper carton is off-centre. A spline
   // following the sloping face centre would rotate the width axis and gap seams.
   auto Offset=[&](const FBox& Box)
   { return X ? FVector2D(Box.GetCenter().Y,-(Sign<0 ? Box.Min.X : Box.Max.X)) :
                FVector2D(Box.GetCenter().X,Sign<0 ? Box.Min.Y : Box.Max.Y); };
   FVector Start(0,0,Levels[I-1]),End(0,0,Levels[I]);
   FVector2D EndOffset=Offset(Top);
   if (ReceiptState==EWarehouseReceiptState::WrapTorn)
   { EndOffset.Y+=(X ? -Sign : Sign)*9;End.Z-=FMath::Min(3.f,(Levels[I]-Levels[I-1])*.3f); }
   auto* Film=NewSheet(TEXT("PalletWrapSide"));
   Film->SetSplineUpDir(X ? FVector(-1,0,0) : FVector(0,1,0),false);
   const FVector Tangent=End-Start;
   Film->SetStartAndEnd(Start,Tangent,End,Tangent,false);
   Film->SetStartOffset(Offset(Bottom),false);Film->SetEndOffset(EndOffset,false);
   Film->SetStartScale(FVector2D((X ? Bottom.GetSize().Y : Bottom.GetSize().X)/100.f,1),false);
   Film->SetEndScale(FVector2D((X ? Top.GetSize().Y : Top.GetSize().X)/100.f,1),false);
   Film->RegisterComponent();Film->UpdateMesh();
  }
 }
 if (ReceiptState!=EWarehouseReceiptState::WrapTorn && Levels.Num()>1)
 {
  const FBox Top=Ring(Levels.Last());
  auto* Cap=NewSheet(TEXT("PalletWrapTop"));
  const FVector Start(Top.Min.X,Top.GetCenter().Y,Levels.Last()),End(Top.Max.X,Top.GetCenter().Y,Levels.Last());
  Cap->SetSplineUpDir(FVector::UpVector,false);Cap->SetStartAndEnd(Start,End-Start,End,End-Start,false);
  Cap->SetStartScale(FVector2D(Top.GetSize().Y/100.f,1),false);Cap->SetEndScale(FVector2D(Top.GetSize().Y/100.f,1),false);
  Cap->RegisterComponent();Cap->UpdateMesh();
 }
}

void AWarehousePallet::OnWrapJointBroken(int32 Index) { bWrapBreakPending=true; }

void AWarehousePallet::TearWrap()
{
 if (bWrapping) CancelInboundWrapping();
 if (ReceiptState!=EWarehouseReceiptState::Confirmed) return;
 ReceiptState=EWarehouseReceiptState::WrapTorn;
 for (UPhysicsConstraintComponent* Joint:WrapJoints) if (Joint) Joint->DestroyComponent();
 WrapJoints.Reset();
 for (AWarehouseCargo* Box:WrappedCargo) if (IsValid(Box)) { Box->WrappedPallet.Reset();Box->GetCargoBody()->WakeAllRigidBodies(); }
 BuildWrapFilm();WrappedCargo.Reset();bWrapBreakPending=false;SetActorTickEnabled(false);
 UE_LOG(LogTemp,Warning,TEXT("PALLET_WRAP_TORN %s - inspection required"),*GetName());
}

void AWarehousePallet::Tick(float Dt)
{
 Super::Tick(Dt);
 if (bWrapping)
 {
  TArray<AActor*> Loads;for (AWarehouseCargo* Box:WrappingCargo) Loads.Add(Box);
  TArray<AWarehouseCargo*> Validated;
  bool Stable=WrappingZone.IsValid() && WrappingZone->ContainsPallet(this) && GetWrappableCargo(Loads,Validated,false);
  for (int32 I=0;I<WrappingParts.Num();++I)
  {
   const auto* Part=WrappingParts[I].Get();
   if (!IsValid(Part) || !Part->IsSimulatingPhysics() || Part->GetPhysicsLinearVelocity().Size()>=10 ||
       Part->GetPhysicsAngularVelocityInDegrees().Size()>=5 || !WrappingPoses.IsValidIndex(I) ||
       FVector::Dist(Part->GetComponentLocation(),WrappingPoses[I].GetLocation())>2 ||
       Part->GetComponentQuat().AngularDistance(WrappingPoses[I].GetRotation())>FMath::DegreesToRadians(2.f)) Stable=false;
  }
  if (!Stable) { CancelInboundWrapping();return; }
  if (WrappingStartedSeconds<0)
  {
   // SetOverlayMaterial already requests the native PSOs. Do not spend the
   // one-second visual phase while a first-use render proxy is still deferred.
   for (const auto& Part:WrappingParts) if (Part->IsPSOPrecaching()) return;
   WrappingStartedSeconds=GetWorld()->GetTimeSeconds();return;
  }
  if (GetWrappingRemainingSeconds()<=0)
  {
   const auto Zone=WrappingZone;CancelInboundWrapping();
   if (ConfirmInbound(Loads) && Zone.IsValid()) Zone->RecordInboundConfirmed(this);
  }
  return;
 }
 if (ActorHasTag(TEXT("LogisticsInTransit"))) return;
 bool Failed=bWrapBreakPending;
 auto* Strength=AWarehouseDamageSystem::Find(this);
 Failed |= Strength && Strength->HasFailed(this);
 for (int32 I=0;I<WrappedCargo.Num();++I)
 {
  auto* Box=WrappedCargo[I].Get();
  auto* Joint=WrapJoints.IsValidIndex(I) ? WrapJoints[I].Get() : nullptr;
  if (!IsValid(Box) || !IsValid(Joint) || (Strength && Strength->HasFailed(Box)) || Joint->IsBroken())
  { Failed=true;continue; }
  // Chaos can release a native joint before its cached IsBroken/event reaches
  // the game thread. The film cannot remain intact once its connectors have
  // physically separated beyond the 1.5 cm / 3 degree envelope plus tolerance.
  const FTransform A=Joint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame1)*Body->BodyInstance.GetUnrealWorldTransform();
  const FTransform B=Joint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame2)*Box->GetCargoBody()->BodyInstance.GetUnrealWorldTransform();
  Failed |= FVector::DistSquared(A.GetLocation(),B.GetLocation())>FMath::Square(6.f) ||
   A.GetRotation().AngularDistance(B.GetRotation())>FMath::DegreesToRadians(10.f);
 }
 if (Failed) TearWrap();
}

void AWarehousePallet::EndPlay(const EEndPlayReason::Type Reason)
{
 CancelInboundWrapping();
 for (UPhysicsConstraintComponent* Joint:WrapJoints) if (Joint) Joint->DestroyComponent();
 for (AWarehouseCargo* Box:WrappedCargo) if (IsValid(Box)) Box->WrappedPallet.Reset();
 Super::EndPlay(Reason);
}
