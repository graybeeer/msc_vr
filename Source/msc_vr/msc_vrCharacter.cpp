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
#include "WarehouseWorker.h"
#include "WarehousePhysics.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/GameViewportClient.h"
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
	Tags.Add(TEXT("WarehousePhysicalHuman"));
	bUseControllerRotationYaw=false;
	CarryGrip=CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("TwoHandPhysicalGrip"));
	CarryGrip->SetupAttachment(GetRootComponent());
	CarryGrip->SetLinearXLimit(LCM_Free,0); CarryGrip->SetLinearYLimit(LCM_Free,0); CarryGrip->SetLinearZLimit(LCM_Free,0);
	CarryGrip->SetAngularSwing1Limit(ACM_Free,0); CarryGrip->SetAngularSwing2Limit(ACM_Free,0); CarryGrip->SetAngularTwistLimit(ACM_Free,0);
	CarryGrip->SetLinearPositionDrive(true,true,true); CarryGrip->SetLinearVelocityDrive(true,true,true);
	CarryGrip->SetLinearDriveAccelerationMode(false); CarryGrip->SetLinearDriveParams(12000.f,1200.f,45000.f);
	CarryGrip->SetAngularDriveMode(EAngularDriveMode::SLERP); CarryGrip->SetOrientationDriveSLERP(true); CarryGrip->SetAngularVelocityDriveSLERP(true);
	CarryGrip->SetAngularDriveAccelerationMode(false); CarryGrip->SetAngularDriveParams(30000.f,6000.f,400000.f);
	CarryGrip->SetDisableCollision(false);
	CarryGrip->ConstraintInstance.ProfileInstance.bEnableProjection=false;
	CarryGrip->ConstraintInstance.ProfileInstance.bEnableMassConditioning=false;
	CarryGrip->ConstraintInstance.DisableMassConditioning();
	CarryGrip->ConstraintInstance.DisableParentDominates();
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
	RemoteWorldMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RemoteOperatorWorldMesh"));
	RemoteWorldMesh->SetupAttachment(GetMesh());
	RemoteWorldMesh->SetOwnerNoSee(true);
	RemoteWorldMesh->SetCollisionProfileName(TEXT("NoCollision"));
	RemoteWorldMesh->SetVisibility(false);
	RemoteWorldMesh->SetComponentTickEnabled(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PhoneCube(TEXT("/Engine/BasicShapes/Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PhoneBlack(TEXT("/Game/Warehouse/Materials/MI_Rubber"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PhoneBlue(TEXT("/Game/Warehouse/AGV/Materials/M_Screen_blue"));
	for (int32 View=0; View<2; ++View)
	{
		const bool OwnerView=View==0;
		auto* Body=CreateDefaultSubobject<UStaticMeshComponent>(OwnerView ? TEXT("RemotePhone") : TEXT("RemoteWorldPhone"));
		Body->SetupAttachment(GetRootComponent());
		Body->SetStaticMesh(PhoneCube.Object);
		Body->SetMaterial(0,PhoneBlack.Object);
		Body->SetRelativeScale3D(FVector(.008f,.075f,.15f)); // 0.8 x 7.5 x 15 cm.
		Body->SetCollisionProfileName(TEXT("NoCollision"));
		Body->SetOnlyOwnerSee(OwnerView);
		Body->SetOwnerNoSee(!OwnerView);
		Body->FirstPersonPrimitiveType=OwnerView ? EFirstPersonPrimitiveType::FirstPerson : EFirstPersonPrimitiveType::None;
		Body->SetCastShadow(false);
		auto* Screen=CreateDefaultSubobject<UStaticMeshComponent>(OwnerView ? TEXT("RemotePhoneScreen") : TEXT("RemoteWorldPhoneScreen"));
		Screen->SetupAttachment(Body);
		Screen->SetStaticMesh(PhoneCube.Object);
		Screen->SetMaterial(0,PhoneBlue.Object);
		Screen->SetRelativeLocation(FVector(-51.f,0,0));
		Screen->SetRelativeScale3D(FVector(.1f,.9f,.9f));
		Screen->SetCollisionProfileName(TEXT("NoCollision"));
		Screen->SetOnlyOwnerSee(OwnerView); Screen->SetOwnerNoSee(!OwnerView);
		Screen->FirstPersonPrimitiveType=Body->FirstPersonPrimitiveType;
		Screen->SetCastShadow(false);
		Body->SetVisibility(false,true);
		Body->SetAutoActivate(false);
		if (OwnerView) RemotePhone=Body; else RemoteWorldPhone=Body;
	}

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

	GetCapsuleComponent()->SetCapsuleSize(30.0f, 96.0f);

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
		if (!RemoteAction)
		{
			RemoteAction=NewObject<UInputAction>(this,TEXT("RemoteForklift"));
			CarryMappingContext->MapKey(RemoteAction,EKeys::G);
		}
		EnhancedInputComponent->BindAction(RemoteAction,ETriggerEvent::Started,this,&Amsc_vrCharacter::ToggleRemoteControl);
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
	EndRemoteControl();
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
	if (IsValid(RemoteForklift)) return;
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
	// The leaning carry pose has its own head; the camera follows the source mesh.
	// Hide only this owner's render copy, preserving the world body and camera socket.
	CarryMesh->HideBoneByName(TEXT("neck_01"),PBO_None);
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

void Amsc_vrCharacter::BeginPlay()
{
	Super::BeginPlay();
	bUseControllerRotationYaw=false; bUseControllerRotationPitch=false; bUseControllerRotationRoll=false;
	Tags.AddUnique(TEXT("WarehousePhysicalHuman"));
	GetCapsuleComponent()->SetCapsuleSize(30.f,96.f);
	auto* Movement=GetCharacterMovement();
	Movement->bEnablePhysicsInteraction=false;
	Movement->SetComponentTickEnabled(false);
	WarehousePhysics::ConfigureContact(GetCapsuleComponent(),EWarehouseSurface::Rubber,HumanMassKg);
	GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
	GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
	GetCapsuleComponent()->SetEnableGravity(true);
	GetCapsuleComponent()->SetSimulatePhysics(true);
	// The first-person copy and carry IK must receive a live locomotion pose,
	// including when the third-person source is hidden from its owner.
	GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	GetMesh()->AddTickPrerequisiteActor(this);
	FirstPersonMesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	FirstPersonMesh->AddTickPrerequisiteComponent(GetMesh());
	CarryGrip->BreakConstraint();
}

FVector Amsc_vrCharacter::GetVelocity() const
{
	return bPhysicalRagdoll ? GetMesh()->GetPhysicsLinearVelocity(TEXT("pelvis")) : GetCapsuleComponent()->GetPhysicsLinearVelocity();
}

void Amsc_vrCharacter::UpdatePhysicalMovement(float DeltaSeconds)
{
	if (bPhysicalRagdoll) return;
	JumpSupportDelay=FMath::Max(0.f,JumpSupportDelay-DeltaSeconds);
	FVector Input=ConsumeMovementInputVector().GetClampedToMaxSize(1.f);
	if (IsValid(RemoteForklift) || bObserverPresentation || (Controller && Controller->IsMoveInputIgnored())) Input=FVector::ZeroVector;
	const float Speed=bIsCrouched ? 60.f : (IsValid(HeldCargo) ? 120.f : (bPhysicalSprint ? 350.f : 200.f));
	float GroundDistance=-1.f;
	const float DesiredHalf=bPhysicalCrouchRequested ? 58.f : 96.f;
	const float HeldMass=IsValid(HeldCargo) ? CarryBody(HeldCargo)->GetMass() : 0.f;
	bPhysicalGround=WarehouseHumanPhysics::DriveCapsule(this,Input*Speed,500.f,JumpSupportDelay<=0.f,DesiredHalf,&GroundDistance,HeldMass);
	auto* Movement=GetCharacterMovement();
	Movement->Velocity=GetVelocity();
	// Chaos owns displacement; retain the template's animation telemetry only.
	Movement->UpdateProxyAcceleration();
	Movement->MovementMode=bPhysicalGround ? MOVE_Walking : MOVE_Falling;
	Movement->MaxWalkSpeed=Speed;
	float ClearHalf=DesiredHalf;
	if (DesiredHalf>GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() && GroundDistance>=0.f)
		ClearHalf=FMath::Min(DesiredHalf,FMath::Max(GetCapsuleComponent()->GetUnscaledCapsuleRadius(),GroundDistance-1.f));
	const float Half=FMath::FInterpConstantTo(GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),ClearHalf,DeltaSeconds,150.f);
	GetCapsuleComponent()->SetCapsuleHalfHeight(Half,true);
	SetIsCrouched(bPhysicalCrouchRequested || Half<95.f);
	if (GetCapsuleComponent()->GetUpVector().Z<.5f || GetVelocity().Size()>650.f)
	{
		DropCargo(); EndRemoteControl();
		WarehouseHumanPhysics::StartRagdoll(this,HumanMassKg);
		bPhysicalRagdoll=GetMesh()->IsSimulatingPhysics();
	}
}

bool Amsc_vrCharacter::InitializeRemotePresentation()
{
	if (!InitializeCarryMesh() || !GetMesh()->GetSkeletalMeshAsset()) return false;
	if (!RemoteWorldMesh->GetSkeletalMeshAsset())
	{
		RemoteWorldMesh->SetSkeletalMeshAsset(GetMesh()->GetSkeletalMeshAsset());
		for (int32 I=0; I<GetMesh()->GetNumMaterials(); ++I) RemoteWorldMesh->SetMaterial(I,GetMesh()->GetMaterial(I));
		RemoteWorldMesh->SetDisablePostProcessBlueprint(true);
		RemoteWorldMesh->SetAnimInstanceClass(UWarehouseCarryAnimInstance::StaticClass());
		RemoteWorldMesh->AddTickPrerequisiteComponent(GetMesh());
		RemoteWorldMesh->AddTickPrerequisiteActor(this);
		RemoteWorldMesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	}
	return true;
}

void Amsc_vrCharacter::SetRemoteCharacterFade(bool Enabled)
{
	for (int32 View=0; View<2; ++View)
	{
		auto* Phone=View==0 ? RemotePhone.Get() : RemoteWorldPhone.Get();
		TArray<UMeshComponent*> Meshes;
		Meshes.Add(View==0 ? CarryMesh.Get() : RemoteWorldMesh.Get());
		Meshes.Add(Phone);
		TArray<USceneComponent*> PhoneChildren;
		Phone->GetChildrenComponents(true,PhoneChildren);
		for (auto* Child : PhoneChildren) if (auto* RenderMesh=Cast<UMeshComponent>(Child)) Meshes.Add(RenderMesh);
		auto& Originals=View==0 ? RemoteCarryMaterials : RemoteWorldMaterials;
		if (Enabled && Originals.IsEmpty())
		{
			for (auto* RenderMesh : Meshes)
			for (int32 Slot=0; Slot<RenderMesh->GetNumMaterials(); ++Slot)
			{
				auto* Original=RenderMesh->GetMaterial(Slot);
				Originals.Add(Original);
				if (!Original) continue;
				const FString Path=TEXT("/Game/Warehouse/Materials/RemoteCharacter/MI_RemoteFade_")+Original->GetName();
				if (auto* Fade=LoadObject<UMaterialInterface>(nullptr,*Path))
				{
					auto* Dynamic=UMaterialInstanceDynamic::Create(Fade,this);
					Dynamic->SetScalarParameterValue(TEXT("RemoteCharacterOpacity"),.3f);
					RenderMesh->SetMaterial(Slot,Dynamic);
				}
			}
		}
		else if (!Enabled)
		{
			int32 Index=0;
			for (auto* RenderMesh : Meshes)
			for (int32 Slot=0; Slot<RenderMesh->GetNumMaterials(); ++Slot)
				if (Originals.IsValidIndex(Index)) RenderMesh->SetMaterial(Slot,Originals[Index++]);
			Originals.Reset();
		}
	}
}

void Amsc_vrCharacter::ToggleRemoteControl()
{
	if (IsValid(RemoteForklift)) { EndRemoteControl(); return; }
	FHitResult Hit;
	const FVector Eye=FirstPersonCameraComponent->GetComponentLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RemoteInteract),true,this);
	if (GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+FirstPersonCameraComponent->GetForwardVector()*300.f,ECC_Visibility,Params))
		BeginRemoteControl(Cast<AWarehouseForklift>(Hit.GetActor()));
}

bool Amsc_vrCharacter::BeginRemoteControl(AWarehouseForklift* Forklift)
{
	if (IsValid(RemoteForklift) || !IsValid(Forklift)) return false;
	if (IsValid(HeldCargo))
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(42,2.f,FColor::Yellow,TEXT("짐을 먼저 내려놓은 뒤 G키로 원격 조작하세요."));
		return false;
	}
	if (bPhysicalRagdoll || PlacementTime>=0 || bObserverPresentation || (Controller && Controller->IsMoveInputIgnored())) return false;
	if (auto* PC=Cast<Amsc_vrPlayerController>(Controller); PC && (PC->IsObserverView() || PC->IsWarehouseMenuOpen())) return false;
	const FVector Eye=FirstPersonCameraComponent->GetComponentLocation();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RemoteSight),true,this);
	if (!GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+FirstPersonCameraComponent->GetForwardVector()*300.f,ECC_Visibility,Params) || Hit.GetActor()!=Forklift) return false;
	if (!InitializeRemotePresentation() || !Forklift->BeginRemoteControl(this)) return false;
	RemoteForklift=Forklift;
	SetRemoteCharacterFade(true);
	StopJumping();
	CarryMesh->SetComponentTickEnabled(true); CarryMesh->SetVisibility(true);
	FirstPersonMesh->SetVisibility(false,false);
	RemoteWorldMesh->SetComponentTickEnabled(true); RemoteWorldMesh->SetVisibility(true);
	GetMesh()->SetVisibility(false,false);
	RemotePhone->SetActive(true); RemoteWorldPhone->SetActive(true);
	RemotePhone->SetVisibility(true,true); RemoteWorldPhone->SetVisibility(true,true);
	FirstPersonCameraComponent->FirstPersonFieldOfView=90.f;
	auto* Viewport=GetWorld()->GetGameViewport();
	bRemoteHadViewportFocus=Viewport && Viewport->Viewport && Viewport->Viewport->HasFocus();
	UpdateCarryPose(0.f);
	return true;
}

void Amsc_vrCharacter::EndRemoteControl()
{
	if (IsValid(RemoteForklift))
	{
		RemoteForklift->SetRemoteInput(0,0,0);
		RemoteForklift->EndRemoteControl(this);
	}
	RemoteForklift=nullptr;
	SetRemoteCharacterFade(false);
	bRemoteHadViewportFocus=false;
	RemotePhone->SetVisibility(false,true); RemoteWorldPhone->SetVisibility(false,true);
	RemotePhone->SetActive(false); RemoteWorldPhone->SetActive(false);
	RemoteWorldMesh->SetVisibility(false); RemoteWorldMesh->SetComponentTickEnabled(false);
	GetMesh()->SetVisibility(true,false);
	if (!IsValid(HeldCargo))
	{
		CarryBlend=0.f;
		CarryMesh->SetVisibility(false);
		CarryMesh->SetComponentTickEnabled(false);
		FirstPersonMesh->SetVisibility(true,false);
		FirstPersonCameraComponent->FirstPersonFieldOfView=NormalFirstPersonFOV;
	}
}

void Amsc_vrCharacter::UpdateRemoteControl()
{
	if (!IsValid(RemoteForklift))
	{
		if (RemotePhone->IsVisible()) EndRemoteControl();
		return;
	}
	auto* PC=Cast<Amsc_vrPlayerController>(Controller);
	if (!PC || PC->IsWarehouseMenuOpen() || PC->IsObserverView() || RemoteForklift->GetRemoteOperator()!=this)
	{
		EndRemoteControl(); return;
	}
	auto* Viewport=GetWorld()->GetGameViewport();
	if (Viewport && Viewport->Viewport && !Viewport->Viewport->HasFocus())
	{
		RemoteForklift->SetRemoteInput(0,0,0);
		if (bRemoteHadViewportFocus) EndRemoteControl();
		return;
	}
	bRemoteHadViewportFocus=true;
	const bool Brake=PC->IsInputKeyDown(EKeys::SpaceBar);
	RemoteForklift->SetRemoteInput(
		Brake ? 0.f : float(PC->IsInputKeyDown(EKeys::W))-float(PC->IsInputKeyDown(EKeys::S)),
		Brake ? 0.f : float(PC->IsInputKeyDown(EKeys::D))-float(PC->IsInputKeyDown(EKeys::A)),
		Brake ? 0.f : float(PC->IsInputKeyDown(EKeys::R))-float(PC->IsInputKeyDown(EKeys::F)));
}

FQuat Amsc_vrCharacter::GetHandFacing(int Index) const
{
	if (!IsValid(RemoteForklift)) return CarryFacing;
	return FRotationMatrix::MakeFromXZ(CarryFacing.GetAxisZ(),Index==0 ? CarryFacing.GetAxisY() : CarryFacing.GetAxisX()).ToQuat();
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
	if (bPhysicalRagdoll || IsValid(RemoteForklift)) return false;
 if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->HasFailed(Cargo)) return false;
	if (IsValid(HeldCargo) || !IsValid(Cargo) || Cargo->GetAttachParentActor() || !CarryBody(Cargo)->GetStaticMesh() || !InitializeCarryMesh()) return false;
	FVector Center, Extent;
	Cargo->GetActorBounds(false,Center,Extent);
	if (FVector::Dist(Center,FirstPersonCameraComponent->GetComponentLocation())>300.f) return false;
	UStaticMeshComponent* Body=CarryBody(Cargo);
	if (Body->GetMass()>30.f)
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(41,2.f,FColor::Yellow,TEXT("30kg을 넘는 화물은 지게차로 운반하세요."));
		return false;
	}
	Body->SetSimulatePhysics(true);
	Body->SetEnableGravity(true);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (auto* Box=Cast<AWarehouseCargo>(Cargo)) Box->WakeStackAbove();
 else for (TActorIterator<AWarehouseCargo> It(GetWorld());It;++It) It->GetCargoBody()->WakeAllRigidBodies();
	GripBodyOffset=Body->GetComponentQuat().UnrotateVector(Center-Body->GetComponentLocation());
	CarryGrip->SetWorldLocationAndRotation(Center,Body->GetComponentQuat());
	CarryGrip->SetConstrainedComponents(GetCapsuleComponent(),NAME_None,Body,NAME_None);
	CarryGrip->SetConstraintReferenceFrame(EConstraintFrame::Frame2,FTransform(FQuat::Identity,GripBodyOffset));
	GripSlipTime=0.f;
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
	Body->FirstPersonPrimitiveType=EFirstPersonPrimitiveType::None;
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
 PlacementTime=-1.f; PlacementPallet=nullptr;
 CarryGrip->BreakConstraint();
 if (IsValid(HeldCargo))
 {
  auto* Body=CarryBody(HeldCargo);
  Body->bDisallowNanite=HeldDisallowedNanite;
  for (int32 I=0;I<HeldOriginalMaterials.Num();++I) Body->SetMaterial(I,HeldOriginalMaterials[I]);
  Body->FirstPersonPrimitiveType=EFirstPersonPrimitiveType::None;
  Body->MarkRenderStateDirty();
  Body->WakeAllRigidBodies();
 }
 HeldCargo=nullptr; HeldOriginalMaterials.Reset();
 if (NormalWalkSpeed>0.f) { GetCharacterMovement()->MaxWalkSpeed=NormalWalkSpeed; NormalWalkSpeed=0.f; }
}

void Amsc_vrCharacter::UpdateGripTarget(const FTransform& Pose)
{
 const FQuat Parent=GetCapsuleComponent()->GetComponentQuat();
 const FVector Center=Pose.TransformPosition(CarryBody(HeldCargo)->GetStaticMesh()->GetBounds().Origin);
 CarryGrip->SetConstraintReferenceFrame(EConstraintFrame::Frame1,FTransform(Parent.Inverse()*Pose.GetRotation(),Parent.UnrotateVector(Center-GetCapsuleComponent()->GetComponentLocation())));
 CarryGrip->SetLinearPositionTarget(FVector::ZeroVector);
 CarryGrip->SetAngularOrientationTarget(FRotator::ZeroRotator);
}

void Amsc_vrCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateRemoteControl();
	UpdateLocomotion();
	UpdatePhysicalMovement(DeltaSeconds);
	UpdateCarryPose(DeltaSeconds);
}

void Amsc_vrCharacter::UpdateLocomotion()
{
	auto* PC=Cast<APlayerController>(Controller);
	if (!PC) return;
	const bool Active=!IsValid(RemoteForklift) && !PC->IsMoveInputIgnored() && PlacementTime<0;
	SetLocomotionInput(Active && (PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift)),
		Active ? (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl)) : bIsCrouched);
}

void Amsc_vrCharacter::SetObserverPresentation(bool Observing)
{
	if (Observing) EndRemoteControl();
	bObserverPresentation=Observing;
	FirstPersonMesh->SetHiddenInGame(Observing,true);
	GetMesh()->SetOwnerNoSee(!Observing);
	if (IsValid(HeldCargo))
	{
		auto* Body=CarryBody(HeldCargo);
		Body->FirstPersonPrimitiveType=EFirstPersonPrimitiveType::None;
		Body->MarkRenderStateDirty();
	}
}

void Amsc_vrCharacter::SetLocomotionInput(bool Sprint, bool Crouching)
{
	if (bPhysicalRagdoll || IsValid(RemoteForklift)) return;
	bPhysicalSprint=Sprint;
	if (!Crouching && (bIsCrouched || bPhysicalCrouchRequested || GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()<95.f))
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PhysicalStandClearance),false,this);
		FVector StandingCenter=GetActorLocation();
		FHitResult Floor;
		if (GetWorld()->LineTraceSingleByChannel(Floor,StandingCenter,StandingCenter-FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+8.f),ECC_Visibility,Params)) StandingCenter.Z=Floor.ImpactPoint.Z+97.f;
		const FCollisionResponseParams Responses(GetCapsuleComponent()->GetCollisionResponseToChannels());
		if (GetWorld()->OverlapBlockingTestByChannel(StandingCenter,GetActorQuat(),ECC_Pawn,FCollisionShape::MakeCapsule(30,96),Params,Responses)) { bPhysicalCrouchRequested=true; return; }
	}
	bPhysicalCrouchRequested=Crouching;
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
	return true;
}

void Amsc_vrCharacter::UpdateCarryPose(float DeltaSeconds)
{
	const bool Carrying=IsValid(HeldCargo);
	const bool UsingPhone=IsValid(RemoteForklift);
	CarryBlend=FMath::FInterpConstantTo(CarryBlend,Carrying || UsingPhone ? 1.f : 0.f,DeltaSeconds,5.f);
	if (UsingPhone)
	{
		const FQuat Facing=FRotator(0,FirstPersonCameraComponent->GetComponentRotation().Yaw,0).Quaternion();
		// Keep the phone below eye height while the operator watches the vehicle.
		const FVector Center=GetActorLocation()+Facing.RotateVector(FVector(30,10,45));
		const FQuat PhoneFacing=Facing*FRotator(-20,0,0).Quaternion();
		RemotePhone->SetWorldLocationAndRotation(Center,PhoneFacing);
		RemoteWorldPhone->SetWorldLocationAndRotation(Center,PhoneFacing);
		CarryFacing=PhoneFacing;
		CarryHands[0]=Center+PhoneFacing.RotateVector(FVector(-1,-6,-5));
		CarryHands[1]=Center+PhoneFacing.RotateVector(FVector(-3,7.5,-5));
		CarryElbows[0]=GetActorLocation()+Facing.RotateVector(FVector(5,-23,15));
		CarryElbows[1]=GetActorLocation()+Facing.RotateVector(FVector(7,26,20));
		return;
	}
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
 // The grip is a finite force between two dynamic bodies; only its target moves.
 auto* Body=CarryBody(HeldCargo);
 const auto Bounds=Body->GetStaticMesh()->GetBounds();
 const FVector NativeExtent=Bounds.BoxExtent*HeldCargo->GetActorScale3D().GetAbs();
 const FVector Origin=GetActorLocation();
 const FQuat Facing=FRotator(0,FirstPersonCameraComponent->GetComponentRotation().Yaw,0).Quaternion();
 const bool Pallet=Cast<AWarehousePallet>(HeldCargo)!=nullptr;
 const FQuat DesiredRotation=Facing*(Pallet ? FRotator(90,0,0).Quaternion() : FQuat::Identity);
 const float Depth=Pallet ? NativeExtent.Z : NativeExtent.X;
 const float Height=Pallet ? 15.f : 10.f+NativeExtent.Z;
 const FVector GoalCenter=Origin+Facing.RotateVector(FVector(GetCapsuleComponent()->GetScaledCapsuleRadius()+8.f+Depth,0,Height));
 FTransform Goal(DesiredRotation,GoalCenter-DesiredRotation.RotateVector(Bounds.Origin*HeldCargo->GetActorScale3D()),HeldCargo->GetActorScale3D());
 PickupTime+=DeltaSeconds;
 if (PlacementTime>=0.f)
 {
  if (!IsValid(PlacementPallet) || !PlacementPallet->GetActorTransform().Equals(PlacementPalletPose,2.f))
  { PlacementTime=-1.f; PlacementPallet=nullptr; }
  else
  {
   PlacementTime+=DeltaSeconds;
   Goal.Blend(PlacementStart,PlacementTarget,FMath::SmoothStep(0.f,1.f,FMath::Min(PlacementTime/.45f,1.f)));
   const FVector Actual=HeldCargo->GetActorTransform().TransformPosition(Bounds.Origin);
   const FVector Target=PlacementTarget.TransformPosition(Bounds.Origin);
   FHitResult Support; FCollisionQueryParams Query(SCENE_QUERY_STAT(HandPlacementSupport),true,this); Query.AddIgnoredActor(HeldCargo);
   const bool Supported=GetWorld()->LineTraceSingleByChannel(Support,Actual,Actual-FVector(0,0,NativeExtent.Z+5.f),ECC_Visibility,Query) && Support.ImpactNormal.Z>.95f && (Support.GetActor()==PlacementPallet || Cast<AWarehouseCargo>(Support.GetActor()));
   if (PlacementTime>.65f && FVector::Dist(Actual,Target)<3.f && Body->GetPhysicsLinearVelocity().Size()<12.f && Body->GetPhysicsAngularVelocityInDegrees().Size()<20.f && Supported)
   { DropCargo(); return; }
   if (PlacementTime>5.f) { PlacementTime=-1.f; PlacementPallet=nullptr; }
  }
 }
 else
 {
  const float Alpha=FMath::SmoothStep(0.f,1.f,FMath::Min(PickupTime/.6f,1.f));
  FTransform Interpolated; Interpolated.Blend(PickupTransform,Goal,Alpha); Goal=Interpolated;
 }
 UpdateGripTarget(Goal);
 const FVector ActualCenter=HeldCargo->GetActorTransform().TransformPosition(Bounds.Origin);
 const float Error=FVector::Dist(ActualCenter,Goal.TransformPosition(Bounds.Origin));
 GripSlipTime=PickupTime>1.f && Error>40.f ? GripSlipTime+DeltaSeconds : 0.f;
 if (GripSlipTime>.35f)
 {
  if (GEngine) GEngine->AddOnScreenDebugMessage(41,2.f,FColor::Yellow,TEXT("충돌 또는 하중 때문에 손의 지지가 풀렸습니다."));
  DropCargo(); return;
 }
 CarryFacing=HeldCargo->GetActorQuat();
 for (int I=0;I<2;++I)
 {
  const float Side=I==0 ? -1.f : 1.f;
  const FVector Grip=Pallet ? FVector(15.f,Side*23.f,NativeExtent.Z) : FVector(-NativeExtent.X+2.f,Side*FMath::Clamp(NativeExtent.Y-4.f,5.f,22.f),FMath::Clamp(20.f-NativeExtent.Z,-NativeExtent.Z+2.f,NativeExtent.Z-2.f));
  CarryHands[I]=ActualCenter+CarryFacing.RotateVector(Grip);
  CarryElbows[I]=Origin+Facing.RotateVector(FVector(5,Side*28.f,18));
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
	if (bPhysicalRagdoll || IsValid(RemoteForklift) || PlacementTime>=0.f) return;
	if (GetController())
	{
		// pass the move inputs
		const FRotator Flat(0,GetControlRotation().Yaw,0);
		AddMovementInput(FRotationMatrix(Flat).GetUnitAxis(EAxis::Y), Right);
		AddMovementInput(Flat.Vector(), Forward);
	}
}

void Amsc_vrCharacter::DoJumpStart()
{
	if (bPhysicalRagdoll || !bPhysicalGround || bIsCrouched || IsValid(RemoteForklift) || PlacementTime>=0.f || (Controller && Controller->IsMoveInputIgnored())) return;
	GetCapsuleComponent()->AddImpulse(FVector(0,0,GetCapsuleComponent()->GetMass()*360.f));
	JumpSupportDelay=.35f;
}

FQuat Amsc_vrCharacter::GetCarryTorsoLean() const
{
	if (!IsValid(HeldCargo)) return FQuat::Identity;
	const FQuat Facing=FRotator(0,FirstPersonCameraComponent->GetComponentRotation().Yaw,0).Quaternion();
	return FQuat(Facing.GetAxisY(),FMath::DegreesToRadians(40.f)*CarryBlend);
}

void Amsc_vrCharacter::DoJumpEnd()
{
	// pass StopJumping to the character
	StopJumping();
}
