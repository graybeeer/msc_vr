// Copyright Epic Games, Inc. All Rights Reserved.

#include "msc_vrCharacter.h"
#include "WarehouseDamageSystem.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "WarehouseForklift.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "msc_vr.h"
#include "WarehouseCargo.h"
#include "WarehousePallet.h"
#include "msc_vrPlayerController.h"
#include "Engine/Engine.h"
#include "WarehouseChargingStation.h"
#include "WarehouseCarryAnimInstance.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
 UStaticMeshComponent* CarryBody(AActor* Actor)
 {
  if (auto* Cargo=Cast<AWarehouseCargo>(Actor)) return Cargo->GetCargoBody();
  if (auto* Pallet=Cast<AWarehousePallet>(Actor)) return Pallet->GetPalletBody();
  return nullptr;
 }
}

Amsc_vrCharacter::Amsc_vrCharacter()
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CarryMaterial(TEXT("/Game/Warehouse/Materials/M_CarryTransparent"));
	CarryTransparentMaterial=CarryMaterial.Object;
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
 GetCharacterMovement()->InitialPushForceFactor=50.f;
 GetCharacterMovement()->PushForceFactor=250.f;
 GetCharacterMovement()->bPushForceScaledToMass=false;
 GetCharacterMovement()->bScalePushForceToVelocity=false;
	
	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	CarryMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("TwoHandCarryMesh"));
	CarryMesh->SetupAttachment(FirstPersonMesh);
	CarryMesh->SetOnlyOwnerSee(true);
	CarryMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	CarryMesh->SetCollisionProfileName(TEXT("NoCollision"));
	CarryMesh->SetVisibility(false);
	CarryMesh->SetComponentTickEnabled(false);

	// Create the Camera Component	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("head"));
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// configure the character comps
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;

	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
	GetCharacterMovement()->SetCrouchedHalfHeight(58.f);
	GetCharacterMovement()->MaxWalkSpeedCrouched = 150.f;
}

void Amsc_vrCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{	
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &Amsc_vrCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &Amsc_vrCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &Amsc_vrCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &Amsc_vrCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &Amsc_vrCharacter::LookInput);

		if (!CarryAction)
		{
			CarryAction = NewObject<UInputAction>(this, TEXT("CarryCargo"));
			CarryMappingContext = NewObject<UInputMappingContext>(this, TEXT("CarryCargoKeys"));
			CarryMappingContext->MapKey(CarryAction, EKeys::E);
		}
		EnhancedInputComponent->BindAction(CarryAction, ETriggerEvent::Started, this, &Amsc_vrCharacter::ToggleCarry);
		if (const APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
			{
				Subsystem->AddMappingContext(CarryMappingContext, 1);
			}
		}
	}
	else
	{
		UE_LOG(Logmsc_vr, Error, TEXT("'%s' Failed to find an Enhanced Input Component! This template is built to use the Enhanced Input system. If you intend to use the legacy system, then you will need to update this C++ file."), *GetNameSafe(this));
	}
}

void Amsc_vrCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DropCargo();
	if (const APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (CarryMappingContext)
			{
				Subsystem->RemoveMappingContext(CarryMappingContext);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void Amsc_vrCharacter::ToggleCarry()
{
	if (Controller && Controller->IsMoveInputIgnored()) return;
	if (PlacementTime >= 0.f) return;
	if (IsValid(HeldCargo))
	{
		const FVector Eye=FirstPersonCameraComponent->GetComponentLocation();
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PlaceAim),true,this);
		Params.AddIgnoredActor(HeldCargo);
		if (Cast<AWarehouseCargo>(HeldCargo) && GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+FirstPersonCameraComponent->GetForwardVector()*250.f,ECC_Visibility,Params))
		{
			AWarehousePallet* TargetPallet=Cast<AWarehousePallet>(Hit.GetActor());
			for (TActorIterator<AWarehousePallet> It(GetWorld()); It; ++It)
			{
				if (Cast<AWarehousePallet>(Hit.GetActor())) break;
				const FVector Local=It->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
				if (FMath::Abs(Local.X)>56 || FMath::Abs(Local.Y)>56 || Local.Z<0 || Local.Z>180) continue;
				if (!TargetPallet || It->GetActorLocation().Z>TargetPallet->GetActorLocation().Z) TargetPallet=*It;
			}
			if (TargetPallet)
			{
				if (!TryPlaceOnPallet(TargetPallet,Hit.ImpactPoint) && GEngine)
					GEngine->AddOnScreenDebugMessage(41,2.f,FColor::Yellow,TEXT("No stable space on this pallet. Aim at a clear, level surface."));
				return;
			}
		}
		DropCargo();
		return;
	}

	const FVector Eye = FirstPersonCameraComponent->GetComponentLocation();
	const FVector Forward = FirstPersonCameraComponent->GetForwardVector();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(Interact), true, this);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Eye, Eye + Forward * 250.f, ECC_Visibility, Params)) return;
	if (AWarehouseForklift* Forklift = Cast<AWarehouseForklift>(Hit.GetActor()))
	{
		Forklift->TogglePower();
		return;
	}
	if (auto* Station=Cast<AWarehouseChargingStation>(Hit.GetActor()))
	{
		Station->RequestManualCharge();
		return;
	}
	if (auto* Pallet=Cast<AWarehousePallet>(Hit.GetActor())) { TryPickupPallet(Pallet); return; }
	AWarehouseCargo* Best = Cast<AWarehouseCargo>(Hit.GetActor());
	if (!Best)
	{
		return;
	}

	TryPickupCargo(Best);
}

bool Amsc_vrCharacter::InitializeCarryMesh()
{
	if (CarryMesh->GetSkeletalMeshAsset()) return true;
	if (!FirstPersonMesh->GetSkeletalMeshAsset()) return false;
	for (FName Bone : {FName(TEXT("hand_l")),FName(TEXT("hand_r"))})
		if (FirstPersonMesh->GetBoneIndex(Bone)==INDEX_NONE) return false;
	CarryMesh->SetSkeletalMeshAsset(FirstPersonMesh->GetSkeletalMeshAsset());
	for (int I=0; I<FirstPersonMesh->GetNumMaterials(); ++I) CarryMesh->SetMaterial(I,FirstPersonMesh->GetMaterial(I));
	CarryMesh->SetDisablePostProcessBlueprint(true);
	CarryMesh->SetAnimInstanceClass(UWarehouseCarryAnimInstance::StaticClass());
	CarryMesh->AddTickPrerequisiteComponent(FirstPersonMesh);
	CarryMesh->AddTickPrerequisiteActor(this);
	FirstPersonMesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	CarryMesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	NormalFirstPersonFOV=FirstPersonCameraComponent->FirstPersonFieldOfView;
	return true;
}

bool Amsc_vrCharacter::TryPickupCargo(AWarehouseCargo* Cargo) { return TryPickupObject(Cargo); }
bool Amsc_vrCharacter::TryPickupPallet(AWarehousePallet* Pallet)
{
 if (!IsValid(Pallet) || Pallet->PayloadMassKg>0) return false;
 if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->GetSupportedMass(Pallet)>.01f) return false;
 return TryPickupObject(Pallet);
}
bool Amsc_vrCharacter::TryPickupObject(AActor* Cargo)
{
 if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->HasFailed(Cargo)) return false;
	if (IsValid(HeldCargo) || !IsValid(Cargo) || Cargo->GetAttachParentActor() || !CarryBody(Cargo)->GetStaticMesh() || !InitializeCarryMesh()) return false;
	FVector Center, Extent;
	Cargo->GetActorBounds(false,Center,Extent);
	if (FVector::Dist(Center,FirstPersonCameraComponent->GetComponentLocation())>300.f) return false;
	UStaticMeshComponent* Body=CarryBody(Cargo);
	const bool WasSimulating=Body->IsSimulatingPhysics();
	Body->SetSimulatePhysics(false);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (auto* Box=Cast<AWarehouseCargo>(Cargo)) Box->WakeStackAbove();
 else for (TActorIterator<AWarehouseCargo> It(GetWorld());It;++It) It->GetCargoBody()->WakeAllRigidBodies();
	if (!Cargo->AttachToComponent(GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform))
	{
		Body->SetCollisionProfileName(TEXT("PhysicsActor"));
		Body->SetSimulatePhysics(WasSimulating);
		return false;
	}
	HeldCargo=Cargo;
	HeldDisallowedNanite=Body->bDisallowNanite;
	Body->bDisallowNanite=true; // Translucency uses the conventional mesh renderer while carried.
	HeldOriginalMaterials.Reset();
	for (int32 Index=0; Index<Body->GetNumMaterials(); ++Index)
	{
		UMaterialInterface* Original=Body->GetMaterial(Index);
		HeldOriginalMaterials.Add(Original);
		if (Original && CarryTransparentMaterial)
		{
			auto* Transparent=UMaterialInstanceDynamic::Create(CarryTransparentMaterial,this);
			UTexture* Albedo=nullptr;
			if (Original->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Albedo")),Albedo) && Albedo)
				Transparent->SetTextureParameterValue(TEXT("Albedo"),Albedo);
			float SamplingScale=1.f;
			Original->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SamplingScale")),SamplingScale);
			Transparent->SetScalarParameterValue(TEXT("SamplingScale"),SamplingScale);
			Transparent->SetScalarParameterValue(TEXT("CarryOpacity"),FMath::Clamp(CarryOpacity,.05f,1.f));
			Body->SetMaterial(Index,Transparent);
		}
	}
	PickupTransform=Cargo->GetActorTransform();
	LastCarryPose=PickupTransform;
	PickupTime=0.f;
	CarryPhase=0.f;
	NormalWalkSpeed=GetCharacterMovement()->MaxWalkSpeed;
	GetCharacterMovement()->MaxWalkSpeed=FMath::Min(NormalWalkSpeed,260.f);
	Body->FirstPersonPrimitiveType=EFirstPersonPrimitiveType::FirstPerson;
	Body->MarkRenderStateDirty();
	CarryMesh->SetComponentTickEnabled(true);
	CarryMesh->SetVisibility(true);
	FirstPersonMesh->SetVisibility(false,false);
	FirstPersonCameraComponent->FirstPersonFieldOfView=90.f;
	UpdateCarryPose(0.f);
	return true;
}

void Amsc_vrCharacter::DropCargo()
{
	PlacementTime=-1.f;
	PlacementPallet=nullptr;
	if (IsValid(HeldCargo))
	{
		AActor* Cargo=HeldCargo;
        auto* CarryComponent=CarryBody(Cargo);
        const auto Bounds=CarryComponent->GetStaticMesh()->GetBounds();
        const FVector Scale=Cargo->GetActorScale3D();
        const FVector Extent=Bounds.BoxExtent*Scale.GetAbs();
        const FVector Center=Cargo->GetActorTransform().TransformPosition(Bounds.Origin);
        const FVector Forward=FRotator(0,FirstPersonCameraComponent->GetComponentRotation().Yaw,0).Vector();
        FCollisionQueryParams ReleaseParams(SCENE_QUERY_STAT(SafeCargoRelease),false);
        ReleaseParams.AddIgnoredActor(Cargo);
        FTransform SafePose=Cargo->GetActorTransform(); bool Found=false;
        for (float Z : {0.f,20.f,40.f})
        {
         for (float D : {0.f,20.f,40.f,60.f,80.f,100.f,120.f,140.f,160.f})
         {
          const FVector Candidate=Center+Forward*D+FVector(0,0,Z);
          if (GetWorld()->OverlapBlockingTestByProfile(Candidate,SafePose.GetRotation(),TEXT("PhysicsActor"),FCollisionShape::MakeBox(Extent+FVector(.5f)),ReleaseParams)) continue;
          FCollisionQueryParams PathParams=ReleaseParams; PathParams.AddIgnoredActor(this);
          FHitResult PathHit;
          if (GetWorld()->SweepSingleByProfile(PathHit,Center,Candidate,SafePose.GetRotation(),TEXT("PhysicsActor"),FCollisionShape::MakeBox((Extent-FVector(.2f)).ComponentMax(FVector(.1f))),PathParams)) continue;
          SafePose.SetLocation(Candidate-SafePose.GetRotation().RotateVector(Bounds.Origin*Scale)); Found=true; break;
         }
         if (Found) break;
        }
        if (!Found)
        {
         if (GEngine) GEngine->AddOnScreenDebugMessage(41,2.f,FColor::Yellow,TEXT("No clear drop space. Move away from the wall or stack."));
         return;
        }
        HeldCargo=nullptr;
		Cargo->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        Cargo->SetActorTransform(SafePose,false,nullptr,ETeleportType::TeleportPhysics);
		auto* Body=CarryBody(Cargo);
		Body->bDisallowNanite=HeldDisallowedNanite;
		for (int32 Index=0; Index<HeldOriginalMaterials.Num(); ++Index)
			Body->SetMaterial(Index,HeldOriginalMaterials[Index]);
		Body->FirstPersonPrimitiveType=EFirstPersonPrimitiveType::None;
		Body->MarkRenderStateDirty();
		Body->SetCollisionProfileName(TEXT("PhysicsActor"));
        Body->SetEnableGravity(true); Body->SetUseCCD(true);
		Body->SetSimulatePhysics(true);
        Body->SetWorldTransform(SafePose,false,nullptr,ETeleportType::TeleportPhysics);
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		Body->WakeAllRigidBodies();
	}
	HeldOriginalMaterials.Reset();
	if (NormalWalkSpeed>0.f)
	{
		GetCharacterMovement()->MaxWalkSpeed=NormalWalkSpeed;
		NormalWalkSpeed=0.f;
	}
}

void Amsc_vrCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateLocomotion();
	UpdateCarryPose(DeltaSeconds);
}

void Amsc_vrCharacter::UpdateLocomotion()
{
	auto* PC=Cast<APlayerController>(Controller);
	if (!PC) return;
	const bool Active=!PC->IsMoveInputIgnored() && PlacementTime<0;
	SetLocomotionInput(Active && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift)),
		Active ? (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl)) : bIsCrouched);
}

void Amsc_vrCharacter::SetObserverPresentation(bool Observing)
{
	FirstPersonMesh->SetHiddenInGame(Observing,true);
	GetMesh()->SetOwnerNoSee(!Observing);
	if (IsValid(HeldCargo))
	{
		auto* Body=CarryBody(HeldCargo);
		Body->FirstPersonPrimitiveType=Observing ? EFirstPersonPrimitiveType::None : EFirstPersonPrimitiveType::FirstPerson;
		Body->MarkRenderStateDirty();
	}
}

void Amsc_vrCharacter::SetLocomotionInput(bool Sprint, bool Crouching)
{
	auto* Movement=GetCharacterMovement();
	if (BaseWalkSpeed<=0) BaseWalkSpeed=NormalWalkSpeed>0 ? NormalWalkSpeed : Movement->MaxWalkSpeed;
	if (Crouching) Crouch();
	else UnCrouch();
	Movement->MaxWalkSpeed=IsValid(HeldCargo) ? 260.f : (Sprint && !Crouching && !bIsCrouched ? BaseWalkSpeed*1.65f : BaseWalkSpeed);
}

void Amsc_vrCharacter::OnStartCrouch(float HeightAdjust, float ScaledHeightAdjust)
{
	Super::OnStartCrouch(HeightAdjust,ScaledHeightAdjust);
	// The camera is on the head mesh, so undo Character's mesh-height compensation.
	FirstPersonMesh->AddLocalOffset(FVector(0,0,-HeightAdjust));
}

void Amsc_vrCharacter::OnEndCrouch(float HeightAdjust, float ScaledHeightAdjust)
{
	Super::OnEndCrouch(HeightAdjust,ScaledHeightAdjust);
	FirstPersonMesh->AddLocalOffset(FVector(0,0,HeightAdjust));
}

bool Amsc_vrCharacter::FindPlacement(AWarehousePallet* Pallet, const FVector& Aim, FTransform& Target) const
{
	if (!IsValid(Pallet) || !IsValid(HeldCargo) || Pallet->GetAttachParentActor() ||
		FVector::Dist(Aim,FirstPersonCameraComponent->GetComponentLocation())>260.f) return false;
	if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->HasFailed(Pallet)) return false;
	if (FMath::Abs(Pallet->GetActorRotation().Pitch)>2 || FMath::Abs(Pallet->GetActorRotation().Roll)>2) return false;
	const auto Bounds=CarryBody(HeldCargo)->GetStaticMesh()->GetBounds();
	const FVector Scale=HeldCargo->GetActorScale3D();
	const FVector Extent=Bounds.BoxExtent*Scale.GetAbs();
	const FVector PalletScale=Pallet->GetActorScale3D().GetAbs();
	const FVector Half=FVector(55,55,0)*PalletScale;
	if (Extent.X>Half.X-2 || Extent.Y>Half.Y-2) return false;
	const FQuat Rotation=FRotator(0,Pallet->GetActorRotation().Yaw,0).Quaternion();
	FVector Local=Rotation.UnrotateVector(Aim-Pallet->GetActorLocation());
	Local.X=FMath::Clamp(Local.X,-Half.X+Extent.X+2,Half.X-Extent.X-2);
	Local.Y=FMath::Clamp(Local.Y,-Half.Y+Extent.Y+2,Half.Y-Extent.Y-2);
	FVector Center=Pallet->GetActorLocation()+Rotation.RotateVector(FVector(Local.X,Local.Y,0));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PalletPlacement),true,this);
	Params.AddIgnoredActor(HeldCargo);
	float High=-FLT_MAX;
	TArray<FVector> Supports;
	// A carton bridges the gaps between pallet deck boards. Require support in
	// all four footprint quadrants at the highest contact plane, not solid wood at every ray.
	for (float X : {-.9f,-.5f,-.15f,0.f,.15f,.5f,.9f}) for (float Y : {-.9f,-.5f,-.15f,0.f,.15f,.5f,.9f})
	{
		FVector Start=Center+Rotation.RotateVector(FVector(X*Extent.X,Y*Extent.Y,0));
		Start.Z=FMath::Min(Aim.Z+Extent.Z*2+20,FirstPersonCameraComponent->GetComponentLocation().Z+40);
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit,Start,FVector(Start.X,Start.Y,Pallet->GetActorLocation().Z),ECC_Visibility,Params) ||
			Hit.ImpactNormal.Z<.95f || (Hit.GetActor()!=Pallet && !Cast<AWarehouseCargo>(Hit.GetActor()))) continue;
		if (auto* Cargo=Cast<AWarehouseCargo>(Hit.GetActor()); Cargo && (Cargo->GetAttachParentActor() || Cargo->GetVelocity().Size()>5)) return false;
		High=FMath::Max(High,static_cast<float>(Hit.ImpactPoint.Z));
		Supports.Add(FVector(X,Y,Hit.ImpactPoint.Z));
	}
	bool Quadrants[4]={false,false,false,false};
	for (const FVector& Point : Supports)
		if (High-Point.Z<=2.5f && FMath::Abs(Point.X)>=.1f && FMath::Abs(Point.Y)>=.1f)
			Quadrants[(Point.X>0 ? 1 : 0)+(Point.Y>0 ? 2 : 0)]=true;
	for (bool Supported : Quadrants) if (!Supported) { UE_LOG(Logmsc_vr,Verbose,TEXT("Placement: insufficient support, samples=%d top=%.2f"),Supports.Num(),High); return false; }
	Center.Z=High+Extent.Z+1.f;
	if (Center.Z+Extent.Z>FirstPersonCameraComponent->GetComponentLocation().Z+35.f ||
		FVector::Dist(Center,GetActorLocation())>230.f) return false;
	if (GetWorld()->OverlapBlockingTestByChannel(Center,Rotation,ECC_Visibility,FCollisionShape::MakeBox(Extent-FVector(.3f)),Params)) { UE_LOG(Logmsc_vr,Verbose,TEXT("Placement: occupied destination %s extent %s"),*Center.ToString(),*Extent.ToString()); return false; }
	Target=FTransform(Rotation,Center-Rotation.RotateVector(Bounds.Origin*Scale),Scale);
	return true;
}

bool Amsc_vrCharacter::TryPlaceOnPallet(AWarehousePallet* Pallet, FVector Aim)
{
	if (PlacementTime>=0 || !Cast<AWarehouseCargo>(HeldCargo) || !FindPlacement(Pallet,Aim,PlacementTarget)) return false;
	PlacementPallet=Pallet;
	PlacementPalletPose=Pallet->GetActorTransform();
	PlacementStart=HeldCargo->GetActorTransform();
	PlacementTime=0.f;
	GetCharacterMovement()->StopMovementImmediately();
	return true;
}

void Amsc_vrCharacter::UpdateCarryPose(float DeltaSeconds)
{
	const bool Carrying=IsValid(HeldCargo);
	CarryBlend=FMath::FInterpConstantTo(CarryBlend,Carrying ? 1.f : 0.f,DeltaSeconds,5.f);
	if (!Carrying)
	{
		if (NormalWalkSpeed>0.f) DropCargo(); // Also recover if a carried actor is destroyed.
		if (CarryBlend<=0.f && CarryMesh->IsVisible())
		{
			CarryMesh->SetVisibility(false);
			CarryMesh->SetComponentTickEnabled(false);
			FirstPersonMesh->SetVisibility(true,false);
			FirstPersonCameraComponent->FirstPersonFieldOfView=NormalFirstPersonFOV;
		}
		return;
	}
	// Anchor the load at the waist, independent of head motion and camera pitch.
	const FVector CarryOrigin=GetActorLocation();
	const FQuat Facing=FRotator(0,FirstPersonCameraComponent->GetComponentRotation().Yaw,0).Quaternion();
	const auto Bounds=CarryBody(HeldCargo)->GetStaticMesh()->GetBounds();
	const FVector Scale=HeldCargo->GetActorScale3D();
	const FVector Extent=Bounds.BoxExtent*Scale.GetAbs();
	CarryPhase+=GetVelocity().Size2D()*DeltaSeconds*.025f;
	const float Sway=FMath::Clamp(GetVelocity().Size2D()/260.f,0.f,1.f);
	const FVector Center=CarryOrigin+Facing.RotateVector(FVector(8.f+Extent.X,FMath::Sin(CarryPhase)*.6f*Sway,10.f+Extent.Z+FMath::Sin(CarryPhase*2.f)*.6f*Sway));
	const FVector Pivot=Center-Facing.RotateVector(Bounds.Origin*Scale);
	PickupTime=FMath::Min(PickupTime+DeltaSeconds,.35f);
	const float T=FMath::SmoothStep(0.f,1.f,PickupTime/.35f);
	if (PlacementTime>=0.f)
	{
		if (!IsValid(PlacementPallet) || PlacementPallet->GetAttachParentActor() || !PlacementPallet->GetActorTransform().Equals(PlacementPalletPose,.1f))
		{
			PlacementTime=-1.f;
			PickupTransform=HeldCargo->GetActorTransform(); PickupTime=0.f;
			return;
		}
		PlacementTime+=DeltaSeconds;
		const float Alpha=FMath::SmoothStep(0.f,1.f,PlacementTime/.45f);
		FTransform Pose;
		Pose.Blend(PlacementStart,PlacementTarget,Alpha);
		Pose.AddToTranslation(FVector(0,0,FMath::Sin(Alpha*PI)*12.f));
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PlaceMotion),false,this); Params.AddIgnoredActor(HeldCargo);
		const FVector NextCenter=Pose.TransformPosition(Bounds.Origin);
		FHitResult Obstacle;
		const bool Blocked=GetWorld()->SweepSingleByChannel(Obstacle,HeldCargo->GetActorTransform().TransformPosition(Bounds.Origin),NextCenter,Pose.GetRotation(),ECC_Visibility,FCollisionShape::MakeBox(Extent-FVector(.5f)),Params);
		if (Blocked)
		{
			PlacementTime=-1.f; PickupTransform=HeldCargo->GetActorTransform(); PickupTime=0.f;
			if (GEngine) GEngine->AddOnScreenDebugMessage(41,2.f,FColor::Yellow,TEXT("Placement blocked. Move closer to a clear space."));
			return;
		}
		HeldCargo->SetActorTransform(Pose);
		if (PlacementTime>=.45f) { DropCargo(); return; }
	}
	else
 {
  const FQuat Rotation=FQuat::Slerp(PickupTransform.GetRotation(),Facing,T);
  FVector NextPivot=FMath::Lerp(PickupTransform.GetLocation(),Pivot,T);
  const FVector PreviousCenter=LastCarryPose.TransformPosition(Bounds.Origin);
  const FVector NextCenter=NextPivot+Rotation.RotateVector(Bounds.Origin*Scale);
  FCollisionQueryParams Params(SCENE_QUERY_STAT(CarryMotion),false,this); Params.AddIgnoredActor(HeldCargo);
  FHitResult Hit;
  if (GetWorld()->SweepSingleByProfile(Hit,PreviousCenter,NextCenter,Rotation,TEXT("PhysicsActor"),FCollisionShape::MakeBox((Extent-FVector(.2f)).ComponentMax(FVector(.1f))),Params))
   NextPivot=FMath::Lerp(PreviousCenter,NextCenter,FMath::Max(0.f,Hit.Time-.01f))-Rotation.RotateVector(Bounds.Origin*Scale);
  HeldCargo->SetActorLocationAndRotation(NextPivot,Rotation,false,nullptr,ETeleportType::TeleportPhysics);
 }
 LastCarryPose=HeldCargo->GetActorTransform();
	CarryFacing=HeldCargo->GetActorQuat();
	const FVector ActualCenter=HeldCargo->GetActorTransform().TransformPosition(Bounds.Origin);
	for (int I=0; I<2; ++I)
	{
		const float Side=I==0 ? -1.f : 1.f;
		CarryHands[I]=ActualCenter+CarryFacing.RotateVector(FVector(-Extent.X+2.f,Side*FMath::Clamp(Extent.Y-4.f,5.f,22.f),-Extent.Z-2.f));
		CarryElbows[I]=CarryOrigin+Facing.RotateVector(FVector(0,Side*28.f,18.f));
	}
#if WITH_EDITOR
	// Allow the same carry pose to be previewed and checked without starting PIE.
	if (!GetWorld()->IsGameWorld())
	{
		for (USkeletalMeshComponent* PreviewMesh : {GetMesh(),FirstPersonMesh,CarryMesh.Get()})
		{
			PreviewMesh->TickAnimation(DeltaSeconds,false);
			PreviewMesh->RefreshBoneTransforms();
		}
	}
#endif
}


void Amsc_vrCharacter::MoveInput(const FInputActionValue& Value)
{
	// get the Vector2D move axis
	FVector2D MovementVector = Value.Get<FVector2D>();

	// pass the axis values to the move input
	DoMove(MovementVector.X, MovementVector.Y);

}

void Amsc_vrCharacter::LookInput(const FInputActionValue& Value)
{
	// get the Vector2D look axis
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	// pass the axis values to the aim input
	DoAim(LookAxisVector.X, LookAxisVector.Y);

}

void Amsc_vrCharacter::DoAim(float Yaw, float Pitch)
{
	if (auto* PC=Cast<Amsc_vrPlayerController>(Controller)) { Yaw*=PC->MouseSensitivity; Pitch*=PC->MouseSensitivity; }
	if (GetController())
	{
		// pass the rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void Amsc_vrCharacter::DoMove(float Right, float Forward)
{
	if (PlacementTime>=0.f) return;
	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void Amsc_vrCharacter::DoJumpStart()
{
	if (PlacementTime>=0.f || (Controller && Controller->IsMoveInputIgnored())) return;
	// pass Jump to the character
	Jump();
}

void Amsc_vrCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}
