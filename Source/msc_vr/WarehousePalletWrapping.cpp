#include "WarehousePallet.h"
#include "WarehouseCargo.h"
#include "WarehouseDamageSystem.h"
#include "WarehousePhysics.h"
#include "msc_vrCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SplineMeshComponent.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"

AWarehousePallet* AWarehouseCargo::GetWrappedPallet() const
{
 auto* Pallet=WrappedPallet.Get();
 return Pallet && Pallet->ReceiptState==EWarehouseReceiptState::Confirmed ? Pallet : nullptr;
}

FText AWarehousePallet::GetReceiptDescription() const
{
 return FText::FromString(ReceiptState==EWarehouseReceiptState::Confirmed ?
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

bool AWarehousePallet::ConfirmInbound(const TArray<AActor*>& Loads)
{
 if (ReceiptState!=EWarehouseReceiptState::Pending || !Body->IsSimulatingPhysics()) return false;
 auto* Strength=AWarehouseDamageSystem::Find(this);
 if (!Strength || Strength->HasFailed(this)) return false;
 TArray<AWarehouseCargo*> Cargo;
 for (auto* Load:Loads) if (Cast<AWarehousePallet>(Load)) return false;
 for (auto* Load:Loads) if (auto* Box=Cast<AWarehouseCargo>(Load))
 {
  if (Box->GetWrappedPallet() || !Box->GetCargoBody()->IsSimulatingPhysics() || Strength->HasFailed(Box)) return false;
  Cargo.AddUnique(Box);
 }
 if (Cargo.IsEmpty()) return false;
 // Confirm only the current physical stack, never arbitrary nearby parcels.
 const TArray<AActor*> Supported=Strength->GetSupportedActors(this);
 for (auto* Box:Cargo) if (!Supported.Contains(Box)) return false;
 for (auto* Actor:Supported) if (auto* Box=Cast<AWarehouseCargo>(Actor);Box && !Cargo.Contains(Box)) return false;
 for (TActorIterator<Amsc_vrCharacter> It(GetWorld());It;++It)
  if (It->GetHeldCargo()==this || Supported.Contains(It->GetHeldCargo())) return false;
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
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube"));
 if (!Material || !Cube) return;
 const FTransform Frame=GetActorTransform();
 TArray<FBox> Boxes;
 TArray<float> Levels{12.f}; // Leave the actual fork openings below the film clear.
 for (AWarehouseCargo* Box:WrappedCargo) if (IsValid(Box))
 {
  const auto* Part=Box->GetCargoBody();
  FBox Bounds=Part->CalcBounds(Part->GetComponentTransform().GetRelativeTransform(Frame)).GetBox();
  Boxes.Add(Bounds);Levels.Add(FMath::Max(12.1f,float(Bounds.Min.Z)));Levels.Add(float(Bounds.Max.Z));
 }
 Levels.Sort();
 for (int32 I=Levels.Num()-1;I>0;--I) if (Levels[I]-Levels[I-1]<.2f) Levels.RemoveAt(I);
 auto Ring=[&](float Height)
 {
  FBox Bounds(ForceInit);
  if (Height<=12.1f) Bounds=FBox(FVector(-55,-55,Height),FVector(55,55,Height));
  else for (const auto& Box:Boxes) if (Box.Max.Z>=Height-.2f) Bounds+=Box;
  return Bounds.ExpandBy(.25f);
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
   auto Centre=[&](const FBox& Box,float Z)
   { FVector P=Box.GetCenter();P.Z=Z;if (X) P.X=Sign<0 ? Box.Min.X : Box.Max.X;else P.Y=Sign<0 ? Box.Min.Y : Box.Max.Y;return P; };
   FVector Start=Centre(Bottom,Levels[I-1]),End=Centre(Top,Levels[I]);
   if (ReceiptState==EWarehouseReceiptState::WrapTorn) End+=(X ? FVector(Sign*9,0,-3) : FVector(0,Sign*9,-3));
   auto* Film=NewObject<USplineMeshComponent>(this);
   AddInstanceComponent(Film);
   Film->SetMobility(EComponentMobility::Movable);
   Film->SetupAttachment(Body);Film->SetRelativeTransform(FTransform::Identity);
   Film->SetStaticMesh(Cube);Film->SetMaterial(0,Material);Film->SetForwardAxis(ESplineMeshAxis::Z,false);
   Film->SetSplineUpDir(X ? FVector(-1,0,0) : FVector(0,1,0),false);
   const FVector Tangent=End-Start;
   Film->SetStartAndEnd(Start,Tangent,End,Tangent,false);
   Film->SetStartScale(FVector2D((X ? Bottom.GetSize().Y : Bottom.GetSize().X)/100.f,.001f),false);
   Film->SetEndScale(FVector2D((X ? Top.GetSize().Y : Top.GetSize().X)/100.f,.001f),false);
   Film->SetCollisionEnabled(ECollisionEnabled::NoCollision);Film->SetGenerateOverlapEvents(false);
   Film->SetCanEverAffectNavigation(false);Film->SetCastShadow(false);
   Film->ComponentTags.Add(TEXT("PalletWrapVisual"));Film->RegisterComponent();Film->UpdateMesh();WrapFilm.Add(Film);
  }
 }
}

void AWarehousePallet::OnWrapJointBroken(int32 Index) { bWrapBreakPending=true; }

void AWarehousePallet::TearWrap()
{
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
 for (UPhysicsConstraintComponent* Joint:WrapJoints) if (Joint) Joint->DestroyComponent();
 for (AWarehouseCargo* Box:WrappedCargo) if (IsValid(Box)) Box->WrappedPallet.Reset();
 Super::EndPlay(Reason);
}
