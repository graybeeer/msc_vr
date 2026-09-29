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
#include "WarehouseChargingStation.h"
#include "WarehouseCarryAnimInstance.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

Amsc_vrCharacter::Amsc_vrCharacter()
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CarryMaterial(TEXT("/Game/Warehouse/Materials/M_CarryTransparent"));
	CarryTransparentMaterial=CarryMaterial.Object;
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
	
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
	if (IsValid(HeldCargo))
	{
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

bool Amsc_vrCharacter::TryPickupCargo(AWarehouseCargo* Cargo)
{
 if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->HasFailed(Cargo)) return false;
	if (IsValid(HeldCargo) || !IsValid(Cargo) || !Cargo->GetCargoBody()->GetStaticMesh() || !InitializeCarryMesh()) return false;
	FVector Center, Extent;
	Cargo->GetActorBounds(false,Center,Extent);
	if (FVector::Dist(Center,FirstPersonCameraComponent->GetComponentLocation())>300.f) return false;
	UStaticMeshComponent* Body=Cargo->GetCargoBody();
	const bool WasSimulating=Body->IsSimulatingPhysics();
	Body->SetSimulatePhysics(false);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
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
	if (IsValid(HeldCargo))
	{
		AWarehouseCargo* Cargo=HeldCargo;
		HeldCargo=nullptr;
		Cargo->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		auto* Body=Cargo->GetCargoBody();
		Body->bDisallowNanite=HeldDisallowedNanite;
		for (int32 Index=0; Index<HeldOriginalMaterials.Num(); ++Index)
			Body->SetMaterial(Index,HeldOriginalMaterials[Index]);
		Body->FirstPersonPrimitiveType=EFirstPersonPrimitiveType::None;
		Body->MarkRenderStateDirty();
		Body->SetCollisionProfileName(TEXT("PhysicsActor"));
		Body->SetSimulatePhysics(true);
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
	UpdateCarryPose(DeltaSeconds);
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
	const auto Bounds=HeldCargo->GetCargoBody()->GetStaticMesh()->GetBounds();
	const FVector Scale=HeldCargo->GetActorScale3D();
	const FVector Extent=Bounds.BoxExtent*Scale.GetAbs();
	CarryPhase+=GetVelocity().Size2D()*DeltaSeconds*.025f;
	const float Sway=FMath::Clamp(GetVelocity().Size2D()/260.f,0.f,1.f);
	const FVector Center=CarryOrigin+Facing.RotateVector(FVector(8.f+Extent.X,FMath::Sin(CarryPhase)*.6f*Sway,10.f+Extent.Z+FMath::Sin(CarryPhase*2.f)*.6f*Sway));
	const FVector Pivot=Center-Facing.RotateVector(Bounds.Origin*Scale);
	PickupTime=FMath::Min(PickupTime+DeltaSeconds,.35f);
	const float T=FMath::SmoothStep(0.f,1.f,PickupTime/.35f);
	HeldCargo->SetActorLocationAndRotation(FMath::Lerp(PickupTransform.GetLocation(),Pivot,T),FQuat::Slerp(PickupTransform.GetRotation(),Facing,T));
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
	if (GetController())
	{
		// pass the rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void Amsc_vrCharacter::DoMove(float Right, float Forward)
{
	if (GetController())
	{
		// pass the move inputs
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void Amsc_vrCharacter::DoJumpStart()
{
	// pass Jump to the character
	Jump();
}

void Amsc_vrCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}
