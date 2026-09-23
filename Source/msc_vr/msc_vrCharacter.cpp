// Copyright Epic Games, Inc. All Rights Reserved.

#include "msc_vrCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "msc_vr.h"

Amsc_vrCharacter::Amsc_vrCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);
	
	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));

	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

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
		AStaticMeshActor* Cargo = HeldCargo;
		HeldCargo = nullptr;
		Cargo->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		UStaticMeshComponent* CargoMesh = Cargo->GetStaticMeshComponent();
		CargoMesh->SetCollisionProfileName(TEXT("PhysicsActor"));
		CargoMesh->SetSimulatePhysics(true);
		if (!CargoMesh->IsSimulatingPhysics())
		{
			// Imported meshes without a simple shape still land on the floor.
			FVector Origin, Extent;
			Cargo->GetActorBounds(false, Origin, Extent);
			FHitResult Floor;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(CargoDrop), false, this);
			Query.AddIgnoredActor(Cargo);
			if (GetWorld()->LineTraceSingleByChannel(Floor, Origin, Origin - FVector(0, 0, 300), ECC_Visibility, Query))
			{
				Cargo->SetActorLocation(Cargo->GetActorLocation() + FVector(0, 0, Floor.ImpactPoint.Z + Extent.Z - Origin.Z + 2));
			}
			CargoMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
		}
		return;
	}

	const FVector Eye = FirstPersonCameraComponent->GetComponentLocation();
	const FVector Forward = FirstPersonCameraComponent->GetForwardVector();
	AStaticMeshActor* Best = nullptr;
	float BestOffset = TNumericLimits<float>::Max();
	for (TActorIterator<AStaticMeshActor> It(GetWorld()); It; ++It)
	{
		AStaticMeshActor* Cargo = *It;
		if (!Cargo->ActorHasTag(TEXT("Carryable")))
		{
			continue;
		}
		FVector Origin, Extent;
		Cargo->GetActorBounds(false, Origin, Extent);
		const FVector ToCargo = Origin - Eye;
		const float AlongView = FVector::DotProduct(ToCargo, Forward);
		if (AlongView < 20.f || AlongView > 250.f)
		{
			continue;
		}
		const float Offset = (ToCargo - Forward * AlongView).SizeSquared();
		if (Offset < 65.f * 65.f && Offset < BestOffset)
		{
			Best = Cargo;
			BestOffset = Offset;
		}
	}
	if (!Best)
	{
		return;
	}

	UStaticMeshComponent* CargoMesh = Best->GetStaticMeshComponent();
	CargoMesh->SetMobility(EComponentMobility::Movable);
	CargoMesh->SetSimulatePhysics(false);
	CargoMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (Best->AttachToComponent(FirstPersonCameraComponent, FAttachmentTransformRules::KeepWorldTransform))
	{
		Best->SetActorRelativeLocation(FVector(170, 55, -45));
		Best->SetActorRelativeRotation(FRotator::ZeroRotator);
		HeldCargo = Best;
	}
	else
	{
		CargoMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	}
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
