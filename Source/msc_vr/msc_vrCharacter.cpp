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
#include "msc_vrCameraManager.h"
#include "Engine/Engine.h"
#include "WarehouseChargingStation.h"
#include "WarehousePalletLift.h"
#include "WarehouseElevator.h"
#include "WarehouseComputer.h"
#include "WarehouseCarryAnimInstance.h"
#include "WarehousePlayerMovement.h"
#include "WarehousePhysics.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/OverlapResult.h"
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

Amsc_vrCharacter::Amsc_vrCharacter(const FObjectInitializer& ObjectInitializer)
 : Super(ObjectInitializer.SetDefaultSubobjectClass<UWarehousePlayerMovement>(ACharacter::CharacterMovementComponentName))
{
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> CarryMaterial(TEXT("/Game/Warehouse/Materials/M_CarryTransparent"));
	CarryTransparentMaterial=CarryMaterial.Object;
	bUseControllerRotationYaw=true;
	CarryGrip=CreateDefaultSubobject<UPhysicsConstraintComponent>(TEXT("TwoHandPhysicalGrip"));
	CarryGrip->SetupAttachment(GetRootComponent());
	// Retain the saved Blueprint subobject name; carrying no longer drives a joint.
	CarryGrip->SetComponentTickEnabled(false);
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
		// SetupAttachment has not registered the children yet; hide the screen explicitly.
		Screen->SetVisibility(false);
		Screen->SetHiddenInGame(true);
		Body->SetVisibility(false,true);
		Body->SetHiddenInGame(true);
		Body->SetAutoActivate(false);
		if (OwnerView) RemotePhone=Body; else RemoteWorldPhone=Body;
	}

	// Create the Camera Component	
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	BaseEyeHeight=74.f; CrouchedEyeHeight=40.f;
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(0,0,BaseEyeHeight),FRotator::ZeroRotator);
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
	if (PlacementTime >= 0.f) { CancelCarryAction(TEXT("놓기를 취소했습니다.")); return; }
	AActor* Target=GetInteractionTarget();
	if (auto* Lift=Cast<AWarehouseElevator>(Target))
	{
		if (Lift->IsHumanInside(this) && Lift->CanUseHumanPanel(this))
		{
			const int32 Floor=Lift->HumanButtonAtSight(this);
			if (Floor!=INDEX_NONE) Lift->RequestHumanDestination(this,Floor);
		}
		else if (const int32 Floor=Lift->HumanLandingFloor(this);Floor!=INDEX_NONE) Lift->RequestHumanCall(this,Floor);
		return;
	}
	if (IsValid(HeldCargo))
	{
		const FVector Eye=FirstPersonCameraComponent->GetComponentLocation();
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PlaceAim),true,this);
		Params.AddIgnoredActors(GetHeldCargoGroup());
		if (GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+FirstPersonCameraComponent->GetForwardVector()*250.f,ECC_Visibility,Params))
		{
			AWarehousePallet* TargetPallet=Cast<AWarehousePallet>(Hit.GetActor());
			for (TActorIterator<AWarehousePallet> It(GetWorld()); It; ++It)
			{
				if (Cast<AWarehousePallet>(Hit.GetActor())) break;
				if (*It==HeldCargo) continue;
				const FVector Local=It->GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
				if (FMath::Abs(Local.X)>56 || FMath::Abs(Local.Y)>56 || Local.Z<0 || Local.Z>180) continue;
				if (!TargetPallet || It->GetActorLocation().Z>TargetPallet->GetActorLocation().Z) TargetPallet=*It;
			}
			if (TargetPallet)
			{
				if (!TryPlaceOnPallet(TargetPallet,Hit.ImpactPoint) && GEngine)
					GEngine->AddOnScreenDebugMessage(41,2.f,FColor::Yellow,TEXT("비어 있는 수평 받침면을 가까이에서 바라보세요. 팔레트 위 공간이 필요합니다."));
				return;
			}
		}
		DropCargo();
		return;
	}

	if (auto* Computer=Cast<AWarehouseComputer>(Target))
	{
		if (auto* PC=Cast<Amsc_vrPlayerController>(Controller)) PC->OpenWarehouseComputer(Computer);
		return;
	}
	if (AWarehouseForklift* Forklift = Cast<AWarehouseForklift>(Target))
	{
		Forklift->TogglePower();
		return;
	}
	if (auto* Station=Cast<AWarehouseChargingStation>(Target))
	{
		Station->RequestManualCharge();
		return;
	}
	if (auto* Lift=Cast<AWarehousePalletLift>(Target))
	{
		const int32 Floor=FMath::Clamp(FMath::FloorToInt((GetActorLocation().Z-Lift->GetActorLocation().Z)/400.f),0,Lift->FloorCount-1);
		Lift->TryInteract(Floor);
		return;
	}
	if (auto* Pallet=Cast<AWarehousePallet>(Target)) { TryPickupPallet(Pallet); return; }
	AWarehouseCargo* Best = Cast<AWarehouseCargo>(Target);
	if (!Best)
	{
		return;
	}

	TryPickupCargo(Best);
}

AActor* Amsc_vrCharacter::GetInteractionTarget(float RangeCm) const
{
  const FVector Eye=FirstPersonCameraComponent->GetComponentLocation();
 const FVector End=Eye+FirstPersonCameraComponent->GetForwardVector()*FMath::Clamp(RangeCm,0.f,400.f);
 FCollisionQueryParams Params(SCENE_QUERY_STAT(WarehouseInteraction),false,this);
 FHitResult Hit;
 const bool Blocking=GetWorld()->LineTraceSingleByChannel(Hit,Eye,End,ECC_Visibility,Params);
 if (Blocking) if (auto* Lift=Cast<AWarehouseElevator>(Hit.GetActor()))
  return Lift->HumanLandingFloor(this)!=INDEX_NONE || Lift->CanUseHumanPanel(this) ? Lift : nullptr;
 if (Blocking && (Cast<AWarehouseForklift>(Hit.GetActor()) || Cast<AWarehouseCargo>(Hit.GetActor()) ||
     Cast<AWarehousePallet>(Hit.GetActor()) || Cast<AWarehouseChargingStation>(Hit.GetActor()) || Cast<AWarehouseComputer>(Hit.GetActor()) || Cast<AWarehousePalletLift>(Hit.GetActor()))) return Hit.GetActor();
 // A forklift's visible outline includes gaps between the mast and sensor
 // poles. Select that outline without adding any fake physical collider.
 AWarehouseForklift* Best=nullptr;float Nearest=1.f;
 for (TActorIterator<AWarehouseForklift> It(GetWorld());It;++It)
 {
  FVector Center,Extent,Point,Normal;float Enter,Reverse;
  It->GetActorBounds(true,Center,Extent);
  const FBox Box(Center-Extent,Center+Extent);
  if (!FMath::LineExtentBoxIntersection(Box,Eye,End,FVector::ZeroVector,Point,Normal,Enter) ||
      !FMath::LineExtentBoxIntersection(Box,End,Eye,FVector::ZeroVector,Point,Normal,Reverse)) continue;
  // A wall or rack on the sight line before the rear of the outline blocks selection.
  if (Blocking && Hit.Time<1.f-Reverse-.001f) continue;
  if (Enter<Nearest) { Best=*It;Nearest=Enter; }
 }
 return Best ? Best : Hit.GetActor();
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
 SetRemotePhoneVisible(false);
 Tags.Remove(TEXT("WarehousePhysicalHuman"));
 bUseControllerRotationYaw=true; bUseControllerRotationPitch=false; bUseControllerRotationRoll=false;
 auto* Capsule=GetCapsuleComponent();
 Capsule->SetSimulatePhysics(false);
 Capsule->SetCapsuleSize(30.f,96.f);
 Capsule->SetCollisionProfileName(TEXT("Pawn"));
 Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
 Capsule->SetCollisionResponseToAllChannels(ECR_Block);
 Capsule->SetMassOverrideInKg(NAME_None,HumanMassKg,true);
 GetMesh()->SetAllBodiesSimulatePhysics(false);
 GetMesh()->SetPhysicsBlendWeight(0.f);
 GetMesh()->SetCollisionProfileName(TEXT("NoCollision"));
 auto* Movement=GetCharacterMovement();
 Movement->SetComponentTickEnabled(true);
 Movement->SetMovementMode(MOVE_Walking);
 Movement->MaxWalkSpeed=350.f;
 Movement->MaxWalkSpeedCrouched=150.f;
 Movement->SetCrouchedHalfHeight(58.f);
 Movement->GetNavAgentPropertiesRef().bCanCrouch=true;
 Movement->GravityScale=1.f;
 Movement->JumpZVelocity=420.f;
 Movement->AirControl=1.f;
 Movement->AirControlBoostMultiplier=0.f;
 Movement->MinAnalogWalkSpeed=0.f;
 Movement->bOrientRotationToMovement=false;
 Movement->bUseControllerDesiredRotation=false;
 Movement->bEnablePhysicsInteraction=true;
 Movement->InitialPushForceFactor=50.f;
 Movement->PushForceFactor=250.f;
 Movement->bPushForceScaledToMass=false;
 Movement->bScalePushForceToVelocity=false;
 Movement->Mass=HumanMassKg;
 // A stable capsule-mounted view is independent of the animated head and hands.
 BaseEyeHeight=74.f; CrouchedEyeHeight=40.f;
 FirstPersonCameraComponent->AttachToComponent(Capsule,FAttachmentTransformRules::KeepRelativeTransform);
 FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(0,0,BaseEyeHeight),FRotator::ZeroRotator);
 FirstPersonCameraComponent->bUsePawnControlRotation=true;
 GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 GetMesh()->AddTickPrerequisiteComponent(Movement);
 FirstPersonMesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 FirstPersonMesh->AddTickPrerequisiteComponent(GetMesh());
 CarryGrip->BreakConstraint();
}

void Amsc_vrCharacter::UpdatePlayerMovement()
{
 if (IsValid(RemoteForklift) || bObserverPresentation || PlacementTime>=0.f || (Controller && Controller->IsMoveInputIgnored()))
 {
  ConsumeMovementInputVector();
  // No new horizontal input; CharacterMovement still integrates falling and
  // moving floors while a menu, placement or remote control owns the input.
 }
 if (Controller)
  SetActorRotation(FRotator(0,GetControlRotation().Yaw,0));
 FirstPersonCameraComponent->SetRelativeLocation(FVector(0,0,BaseEyeHeight));
}

bool Amsc_vrCharacter::InitializeCarryPresentation()
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

void Amsc_vrCharacter::SetRemotePhoneVisible(bool Visible)
{
	for (auto* Phone : {RemotePhone.Get(),RemoteWorldPhone.Get()})
	{
		Phone->SetVisibility(Visible,true);
		Phone->SetHiddenInGame(!Visible,true);
		Phone->SetActive(Visible);
	}
}

void Amsc_vrCharacter::ToggleRemoteControl()
{
	if (IsValid(RemoteForklift)) { EndRemoteControl(); return; }
	BeginRemoteControl(Cast<AWarehouseForklift>(GetInteractionTarget()));
}

bool Amsc_vrCharacter::BeginRemoteControl(AWarehouseForklift* Forklift)
{
	if (IsValid(RemoteForklift) || !IsValid(Forklift)) return false;
	if (IsValid(HeldCargo))
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(42,2.f,FColor::Yellow,TEXT("짐을 먼저 내려놓은 뒤 G키로 원격 조작하세요."));
		return false;
	}
	if (PlacementTime>=0 || bObserverPresentation || (Controller && Controller->IsMoveInputIgnored())) return false;
	if (auto* PC=Cast<Amsc_vrPlayerController>(Controller); PC && (PC->IsObserverView() || PC->IsWarehouseMenuOpen())) return false;
	if (GetInteractionTarget()!=Forklift) return false;
	if (!InitializeCarryPresentation() || !Forklift->BeginRemoteControl(this)) return false;
	RemoteForklift=Forklift;
	SetRemoteCharacterFade(true);
	StopJumping();
	CarryMesh->SetComponentTickEnabled(true); CarryMesh->SetVisibility(true);
	FirstPersonMesh->SetVisibility(false,false);
	RemoteWorldMesh->SetComponentTickEnabled(true); RemoteWorldMesh->SetVisibility(true);
	GetMesh()->SetVisibility(false,false);
	SetRemotePhoneVisible(true);
	FirstPersonCameraComponent->FirstPersonFieldOfView=90.f;
	auto* Viewport=GetWorld()->GetGameViewport();
	bRemoteHadViewportFocus=Viewport && Viewport->Viewport && Viewport->Viewport->HasFocus();
	UpdateCarryPose(0.f);
	if (auto* PC=Cast<APlayerController>(Controller))
	{
		if (auto* Camera=Cast<Amsc_vrCameraManager>(PC->PlayerCameraManager)) Camera->ResetRemoteCamera();
		PC->SetViewTargetWithBlend(Forklift,.2f,VTBlend_Cubic,0.f,true);
	}
	return true;
}

void Amsc_vrCharacter::EndRemoteControl()
{
	const bool HadRemote=IsValid(RemoteForklift) || RemotePhone->IsActive();
	if (IsValid(RemoteForklift))
	{
		RemoteForklift->SetRemoteInput(0,0,0);
		RemoteForklift->EndRemoteControl(this);
	}
	RemoteForklift=nullptr;
	SetRemoteCharacterFade(false);
	bRemoteHadViewportFocus=false;
	SetRemotePhoneVisible(false);
	if (!IsValid(HeldCargo))
	{
		RemoteWorldMesh->SetVisibility(false); RemoteWorldMesh->SetComponentTickEnabled(false);
		GetMesh()->SetVisibility(true,false);
		CarryBlend=0.f;
		CarryMesh->SetVisibility(false);
		CarryMesh->SetComponentTickEnabled(false);
		FirstPersonMesh->SetVisibility(true,false);
		FirstPersonCameraComponent->FirstPersonFieldOfView=NormalFirstPersonFOV;
	}
	if (HadRemote)
		if (auto* PC=Cast<APlayerController>(Controller)) PC->SetViewTargetWithBlend(this,.15f,VTBlend_Cubic,0.f,true);
}

void Amsc_vrCharacter::UpdateRemoteControl()
{
	if (!IsValid(RemoteForklift))
	{
		if (RemotePhone->IsActive()) EndRemoteControl();
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
		Brake ? 0.f : float(PC->IsInputKeyDown(EKeys::LeftMouseButton))-float(PC->IsInputKeyDown(EKeys::RightMouseButton)));
}

FQuat Amsc_vrCharacter::GetHandFacing(int Index) const
{
	if (!IsValid(RemoteForklift)) return CarryFacing;
	return FRotationMatrix::MakeFromXZ(CarryFacing.GetAxisZ(),Index==0 ? CarryFacing.GetAxisY() : CarryFacing.GetAxisX()).ToQuat();
}

TArray<AActor*> Amsc_vrCharacter::GetHeldCargoGroup() const
{
 TArray<AActor*> Result;
 for (const auto& Saved:HeldBodies) if (auto* Actor=Saved.Actor.Get()) Result.Add(Actor);
 return Result;
}

bool Amsc_vrCharacter::TryPickupCargo(AWarehouseCargo* Cargo)
{
 if (IsValid(Cargo)) if (auto* Pallet=Cargo->GetWrappedPallet()) return TryPickupPallet(Pallet);
 return TryPickupObject(Cargo);
}

bool Amsc_vrCharacter::TryPickupPallet(AWarehousePallet* Pallet) { return TryPickupObject(Pallet); }

bool Amsc_vrCharacter::TryPickupObject(AActor* Cargo)
{
 if (IsValid(RemoteForklift) || IsValid(HeldCargo) || !IsValid(Cargo) || !CarryBody(Cargo) ||
     Cargo->GetAttachParentActor() || !CarryBody(Cargo)->GetStaticMesh() || !InitializeCarryPresentation()) return false;
 FVector Center,Extent;Cargo->GetActorBounds(false,Center,Extent);
 if (FVector::Dist(Center,FirstPersonCameraComponent->GetComponentLocation())>300.f) return false;
 auto* Strength=AWarehouseDamageSystem::Find(this);
 TArray<AActor*> Members{Cargo};
 if (auto* Pallet=Cast<AWarehousePallet>(Cargo))
 {
  if (Strength) for (auto* Actor:Strength->GetSupportedActors(Pallet)) Members.AddUnique(Actor);
  for (auto* Box:Pallet->GetWrappedCargo()) Members.AddUnique(Box);
 }
 // A held unit is a player tool. Preserve the complete unit and its real mass;
 // only its temporary motion source changes, never the warehouse's physics rules.
 for (auto* Actor:Members)
 {
  if (!IsValid(Actor) || !CarryBody(Actor) || !CarryBody(Actor)->GetStaticMesh() || Actor->GetAttachParentActor() ||
      Actor->ActorHasTag(TEXT("LogisticsInTransit")) || Actor->ActorHasTag(TEXT("PlayerHeldCargo")) ||
      (Strength && Strength->HasFailed(Actor))) return false;
  if (auto* Box=Cast<AWarehouseCargo>(Actor);Box && Box->GetWrappedPallet() && !Members.Contains(Box->GetWrappedPallet())) return false;
 }
 HeldBodies.Reset();HeldJoints.Reset();HeldOriginalMaterials.Reset();
 const FTransform Initial=Cargo->GetActorTransform();
 for (auto* Actor:Members)
 {
  if (auto* Pallet=Cast<AWarehousePallet>(Actor);Pallet && Pallet->bWrapping) Pallet->CancelInboundWrapping();
  auto* Body=CarryBody(Actor);
  FHeldBodyState Saved;
  Saved.Actor=Actor;Saved.Body=Body;Saved.RelativePose=Actor->GetActorTransform().GetRelativeTransform(Initial);
  Saved.bSimulated=Body->IsSimulatingPhysics();Saved.bGravity=Body->IsGravityEnabled();Saved.Collision=Body->GetCollisionEnabled();
  Saved.MassKg=Body->GetMass();Saved.bMassOverride=Body->BodyInstance.bOverrideMass;
  Saved.bTickEnabled=Actor->IsActorTickEnabled();Saved.bNanite=Body->bDisallowNanite;
  Saved.bIgnoredByPlayer=GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Actor);
  Saved.bHadHeldTag=Actor->ActorHasTag(TEXT("PlayerHeldCargo"));
  Saved.MaterialStart=HeldOriginalMaterials.Num();Saved.MaterialCount=Body->GetNumMaterials();
  for (int32 Index=0;Index<Saved.MaterialCount;++Index) HeldOriginalMaterials.Add(Body->GetMaterial(Index));
  HeldBodies.Add(Saved);
  TArray<UPhysicsConstraintComponent*> Joints;Actor->GetComponents(Joints);
  for (auto* Joint:Joints) if (!Joint->IsBroken())
   HeldJoints.Add({Joint,Joint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame1),Joint->ConstraintInstance.GetRefFrame(EConstraintFrame::Frame2)});
 }
 // Terminate, not BreakConstraint: player manipulation must not emit a torn-wrap event.
 for (const auto& Saved:HeldJoints) if (auto* Joint=Saved.Joint.Get()) Joint->TermComponentConstraint();
 CarryGrip->TermComponentConstraint();
 HeldCargo=Cargo;
 HeldMovementMass=GetCharacterMovement()->Mass;
 for (const auto& Saved:HeldBodies) GetCharacterMovement()->Mass+=Saved.MassKg;
 for (const auto& Saved:HeldBodies)
 {
  auto* Actor=Saved.Actor.Get();auto* Body=Saved.Body.Get();
  Actor->Tags.AddUnique(TEXT("PlayerHeldCargo"));Actor->SetActorTickEnabled(false);
  Body->SetSimulatePhysics(false);Body->SetEnableGravity(false);
  GetCapsuleComponent()->IgnoreActorWhenMoving(Actor,true);
  Body->bDisallowNanite=true;
  for (int32 Index=0;Index<Saved.MaterialCount;++Index)
  {
   auto* Original=HeldOriginalMaterials[Saved.MaterialStart+Index].Get();
   if (!Original || !CarryTransparentMaterial) continue;
   auto* Transparent=UMaterialInstanceDynamic::Create(CarryTransparentMaterial,this);
   UTexture* Albedo=nullptr;
   if (Original->GetTextureParameterValue(FMaterialParameterInfo(TEXT("Albedo")),Albedo) && Albedo)
    Transparent->SetTextureParameterValue(TEXT("Albedo"),Albedo);
   float SamplingScale=1.f;Original->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SamplingScale")),SamplingScale);
   Transparent->SetScalarParameterValue(TEXT("SamplingScale"),SamplingScale);
   Transparent->SetScalarParameterValue(TEXT("CarryOpacity"),FMath::Clamp(CarryOpacity,.05f,1.f));
   Body->SetMaterial(Index,Transparent);
  }
  Body->FirstPersonPrimitiveType=EFirstPersonPrimitiveType::None;Body->MarkRenderStateDirty();
 }
 if (auto* Box=Cast<AWarehouseCargo>(Cargo)) Box->WakeStackAbove();
 PickupTransform=Initial;CarryProgressPose=Initial;CarryStallTime=0.f;PickupTime=0.f;bPickupPending=true;
 CarryMesh->SetComponentTickEnabled(true);CarryMesh->SetVisibility(true);
 FirstPersonMesh->SetVisibility(false,false);
 RemoteWorldMesh->SetComponentTickEnabled(true);RemoteWorldMesh->SetVisibility(true);
 GetMesh()->SetVisibility(false,false);FirstPersonCameraComponent->FirstPersonFieldOfView=90.f;
 UpdateCarryPose(0.f);
 return IsValid(HeldCargo);
}

void Amsc_vrCharacter::DropCargo()
{
 PlacementTime=-1.f;PlacementPallet=nullptr;bPickupPending=false;CarryStallTime=0.f;
 // Restore every body before restarting film joints. No stored pickup momentum
 // or synthetic grip velocity is injected into the released unit.
 for (const auto& Saved:HeldBodies) if (auto* Body=Saved.Body.Get())
 {
  Body->bDisallowNanite=Saved.bNanite;
  for (int32 Index=0;Index<Saved.MaterialCount;++Index)
   if (HeldOriginalMaterials.IsValidIndex(Saved.MaterialStart+Index)) Body->SetMaterial(Index,HeldOriginalMaterials[Saved.MaterialStart+Index]);
  Body->FirstPersonPrimitiveType=EFirstPersonPrimitiveType::None;Body->MarkRenderStateDirty();
  Body->SetCollisionEnabled(Saved.Collision);Body->SetEnableGravity(Saved.bGravity);
  Body->SetMassOverrideInKg(NAME_None,Saved.MassKg,Saved.bMassOverride);
  Body->SetSimulatePhysics(Saved.bSimulated);
  if (Saved.bSimulated)
  {
   Body->SetPhysicsLinearVelocity(FVector::ZeroVector);Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
   Body->WakeAllRigidBodies();
  }
 }
 for (const auto& Saved:HeldJoints) if (auto* Joint=Saved.Joint.Get())
 {
  Joint->InitComponentConstraint();
  Joint->SetConstraintReferenceFrame(EConstraintFrame::Frame1,Saved.Frame1);
  Joint->SetConstraintReferenceFrame(EConstraintFrame::Frame2,Saved.Frame2);
 }
 for (const auto& Saved:HeldBodies) if (auto* Actor=Saved.Actor.Get())
 {
  if (!Saved.bHadHeldTag) Actor->Tags.Remove(TEXT("PlayerHeldCargo"));
  GetCapsuleComponent()->IgnoreActorWhenMoving(Actor,Saved.bIgnoredByPlayer);
  Actor->SetActorTickEnabled(Saved.bTickEnabled);
 }
 if (HeldMovementMass>=0.f) GetCharacterMovement()->Mass=HeldMovementMass;
 HeldMovementMass=-1.f;
 HeldCargo=nullptr;HeldBodies.Reset();HeldJoints.Reset();HeldOriginalMaterials.Reset();
 UpdateCarryPose(0.f);
}

void Amsc_vrCharacter::CancelCarryAction(const TCHAR* Reason)
{
 UE_LOG(Logmsc_vr,Log,TEXT("Carry action cancelled: %s object=%s pickup=%.2f placement=%.2f"),
  Reason,*GetNameSafe(HeldCargo),PickupTime,PlacementTime);
 if (GEngine) GEngine->AddOnScreenDebugMessage(41,3.f,FColor::Yellow,Reason);
 DropCargo();
}

bool Amsc_vrCharacter::MoveHeldGroup(const FTransform& Target)
{
 if (!IsValid(HeldCargo) || HeldBodies.IsEmpty()) return false;
 const FTransform Start=HeldCargo->GetActorTransform();
 const float Distance=FVector::Dist(Start.GetLocation(),Target.GetLocation());
 const float Angle=FMath::RadiansToDegrees(Start.GetRotation().AngularDistance(Target.GetRotation()));
 if (Distance<.01f && Angle<.01f) return true;
 FComponentQueryParams Params(SCENE_QUERY_STAT(PlayerCarrySweep),this);
 for (const auto& Saved:HeldBodies) if (auto* Actor=Saved.Actor.Get()) Params.AddIgnoredActor(Actor);
 // Translation sweeps use the actual convex model. Rotation is sampled at two
 // degrees, checking every member, so a box atop a pallet cannot cross a wall.
 const int32 Steps=FMath::Clamp(FMath::CeilToInt(FMath::Max(Distance/12.f,Angle/2.f)),1,180);
 FTransform Previous=Start;
 for (int32 Step=1;Step<=Steps;++Step)
 {
  FTransform Next;Next.Blend(Start,Target,float(Step)/Steps);
  bool Clear=true;
  for (const auto& Saved:HeldBodies)
  {
   auto* Body=Saved.Body.Get();
   if (!Body || !Saved.Actor.IsValid()) { Clear=false;break; }
   const FTransform From=Saved.RelativePose*Previous,To=Saved.RelativePose*Next;
   const FVector Delta=To.GetLocation()-From.GetLocation();
   TArray<FHitResult> Sweeps;
   GetWorld()->ComponentSweepMulti(Sweeps,Body,From.GetLocation(),To.GetLocation(),From.GetRotation(),Params);
   for (const auto& Hit:Sweeps)
   {
    if (!Hit.bBlockingHit || !WarehousePhysics::IsPhysicalContact(Hit.GetComponent())) continue;
    const float Away=FVector::DotProduct(Delta,Hit.Normal);
    if ((Hit.bStartPenetrating || Hit.Time<=KINDA_SMALL_NUMBER) &&
        (Away>.001f || (Away>=-.001f && Hit.PenetrationDepth<=.25f))) continue;
    Clear=false;break;
   }
   if (!Clear) break;
   TArray<FOverlapResult> Overlaps;
   GetWorld()->ComponentOverlapMultiByChannel(Overlaps,Body,To.GetLocation(),To.GetRotation(),Body->GetCollisionObjectType(),Params);
   for (const auto& Hit:Overlaps) if (Hit.bBlockingHit && WarehousePhysics::IsPhysicalContact(Hit.GetComponent())) { Clear=false;break; }
   if (!Clear) break;
  }
  if (!Clear) return false;
  // All members passed before any is moved; the stored relative poses never stretch.
  for (const auto& Saved:HeldBodies) if (auto* Actor=Saved.Actor.Get())
   Actor->SetActorTransform(Saved.RelativePose*Next,false,nullptr,ETeleportType::TeleportPhysics);
  Previous=Next;
 }
 return true;
}

void Amsc_vrCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateRemoteControl();
	UpdateLocomotion();
	UpdatePlayerMovement();
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
 if (IsValid(RemoteForklift)) return;
 GetCharacterMovement()->MaxWalkSpeed=Sprint ? 500.f : 350.f;
 if (Crouching) Crouch(); else UnCrouch();
}

void Amsc_vrCharacter::OnStartCrouch(float HeightAdjust, float ScaledHeightAdjust)
{
 Super::OnStartCrouch(HeightAdjust,ScaledHeightAdjust);
 BaseEyeHeight=40.f;
 FirstPersonCameraComponent->SetRelativeLocation(FVector(0,0,BaseEyeHeight));
}

void Amsc_vrCharacter::OnEndCrouch(float HeightAdjust, float ScaledHeightAdjust)
{
 Super::OnEndCrouch(HeightAdjust,ScaledHeightAdjust);
 BaseEyeHeight=74.f;
 FirstPersonCameraComponent->SetRelativeLocation(FVector(0,0,BaseEyeHeight));
}

bool Amsc_vrCharacter::FindPlacement(AWarehousePallet* Pallet, const FVector& Aim, FTransform& Target) const
{
	if (!IsValid(Pallet) || !IsValid(HeldCargo) || Pallet==HeldCargo || Pallet->GetAttachParentActor() ||
		FVector::Dist(Aim,FirstPersonCameraComponent->GetComponentLocation())>260.f) return false;
	if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->HasFailed(Pallet)) return false;
	if (FMath::Abs(Pallet->GetActorRotation().Pitch)>2 || FMath::Abs(Pallet->GetActorRotation().Roll)>2) return false;
	const auto* HeldPallet=Cast<AWarehousePallet>(HeldCargo);
	if (HeldPallet)
	{
		if (Pallet->PayloadMassKg>0 || Pallet->GetVelocity().Size()>15.f ||
			Pallet->GetPalletBody()->GetPhysicsAngularVelocityInDegrees().Size()>5.f) return false;
		if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->GetSupportedMass(Pallet)>.01f) return false;
	}
	const auto Bounds=CarryBody(HeldCargo)->GetStaticMesh()->GetBounds();
	const FVector Scale=HeldCargo->GetActorScale3D();
	const FVector Extent=Bounds.BoxExtent*Scale.GetAbs();
	const FVector PalletScale=Pallet->GetActorScale3D().GetAbs();
	const FVector Half=FVector(55,55,0)*PalletScale;
	const float Margin=HeldPallet ? -.5f : 2.f;
	if (Extent.X>Half.X-Margin || Extent.Y>Half.Y-Margin) return false;
	const FQuat Rotation=FRotator(0,Pallet->GetActorRotation().Yaw,0).Quaternion();
	FVector Local=Rotation.UnrotateVector(Aim-Pallet->GetActorLocation());
	Local.X=HeldPallet ? 0.f : FMath::Clamp(Local.X,-Half.X+Extent.X+2,Half.X-Extent.X-2);
	Local.Y=HeldPallet ? 0.f : FMath::Clamp(Local.Y,-Half.Y+Extent.Y+2,Half.Y-Extent.Y-2);
	FVector Center=Pallet->GetActorLocation()+Rotation.RotateVector(FVector(Local.X,Local.Y,0));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PalletPlacement),true,this);
	Params.AddIgnoredActors(GetHeldCargoGroup());
	const float DeckTop=Pallet->GetPalletBody()->Bounds.Origin.Z+Pallet->GetPalletBody()->Bounds.BoxExtent.Z;
	float High=-FLT_MAX;
	TArray<FVector> Supports;
	// A carton bridges the gaps between pallet deck boards. Require support in
	// all four footprint quadrants at the highest contact plane, not solid wood at every ray.
	for (float X : {-.9f,-.5f,-.15f,0.f,.15f,.5f,.9f}) for (float Y : {-.9f,-.5f,-.15f,0.f,.15f,.5f,.9f})
	{
		FVector Start=Center+Rotation.RotateVector(FVector(X*Extent.X,Y*Extent.Y,0));
		Start.Z=FMath::Min(Aim.Z+Extent.Z*2+20,FirstPersonCameraComponent->GetComponentLocation().Z+40);
		FHitResult Hit;
		if (!GetWorld()->LineTraceSingleByChannel(Hit,Start,FVector(Start.X,Start.Y,Pallet->GetActorLocation().Z),ECC_Visibility,Params)) continue;
		if (HeldPallet && Hit.GetActor()!=Pallet)
		{
			// Deck gaps can expose the floor or a lower pallet. They are not cargo.
			if (Hit.ImpactPoint.Z>DeckTop-2.5f) return false;
			continue;
		}
		if (Hit.ImpactNormal.Z<.95f || (Hit.GetActor()!=Pallet && !Cast<AWarehouseCargo>(Hit.GetActor()))) continue;
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
	if (PlacementTime>=0 || (!Cast<AWarehouseCargo>(HeldCargo) && !Cast<AWarehousePallet>(HeldCargo)) || !FindPlacement(Pallet,Aim,PlacementTarget)) return false;
	PlacementPallet=Pallet;
	PlacementPalletPose=Pallet->GetActorTransform();
	PlacementStart=HeldCargo->GetActorTransform();
	PlacementTime=0.f;
	bPickupPending=false;
	CarryStallTime=0.f;
	CarryProgressPose=PlacementStart;
	UpdateCarryPose(0.f);
	return true;
}

void Amsc_vrCharacter::UpdateCarryPose(float DeltaSeconds)
{
 const bool Carrying=IsValid(HeldCargo),UsingPhone=IsValid(RemoteForklift);
 // Carry is an immediate player tool: a fixed hand pose, no pickup/drop blend wait.
 CarryBlend=Carrying ? 1.f : UsingPhone ? FMath::FInterpConstantTo(CarryBlend,1.f,DeltaSeconds,5.f) : 0.f;
 if (UsingPhone)
 {
  const FQuat Facing=FRotator(0,FirstPersonCameraComponent->GetComponentRotation().Yaw,0).Quaternion();
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
  if (!HeldBodies.IsEmpty()) DropCargo();
  if (CarryBlend<=0.f && CarryMesh->IsVisible())
  {
   CarryMesh->SetVisibility(false);CarryMesh->SetComponentTickEnabled(false);
   FirstPersonMesh->SetVisibility(true,false);
   RemoteWorldMesh->SetVisibility(false);RemoteWorldMesh->SetComponentTickEnabled(false);
   GetMesh()->SetVisibility(true,false);FirstPersonCameraComponent->FirstPersonFieldOfView=NormalFirstPersonFOV;
  }
  return;
 }
 auto* Body=CarryBody(HeldCargo);
 const auto Bounds=Body->GetStaticMesh()->GetBounds();
 const FVector NativeExtent=Bounds.BoxExtent*HeldCargo->GetActorScale3D().GetAbs();
 const FVector Origin=GetActorLocation();
 const bool Pallet=Cast<AWarehousePallet>(HeldCargo)!=nullptr;
 // Read control yaw directly: the camera component may not have updated until
 // CalcCamera later in this frame. No spring, turn-rate cap or mass lag.
 const FQuat Facing=FRotator(0,Controller ? Controller->GetControlRotation().Yaw : FirstPersonCameraComponent->GetComponentRotation().Yaw,0).Quaternion();
 // Pallets stay level, including their complete loose or wrapped payload.
 const FVector GoalCenter=Origin+Facing.RotateVector(FVector(GetCapsuleComponent()->GetScaledCapsuleRadius()+8.f+NativeExtent.X,0,10.f+NativeExtent.Z));
 FTransform Goal(Facing,GoalCenter-Facing.RotateVector(Bounds.Origin*HeldCargo->GetActorScale3D()),HeldCargo->GetActorScale3D());
 PickupTime+=DeltaSeconds;
 bool Reached=false;
 if (PlacementTime>=0.f)
 {
  if (!IsValid(PlacementPallet) || !PlacementPallet->GetActorLocation().Equals(PlacementPalletPose.GetLocation(),2.f) ||
      PlacementPallet->GetActorQuat().AngularDistance(PlacementPalletPose.GetRotation())>FMath::DegreesToRadians(2.f) ||
      !PlacementPallet->GetActorScale3D().Equals(PlacementPalletPose.GetScale3D(),.01f))
  { CancelCarryAction(TEXT("받침 위치가 바뀌어 놓기를 취소했습니다."));return; }
  PlacementTime+=DeltaSeconds;
  const FVector StartCenter=PlacementStart.TransformPosition(Bounds.Origin);
  const FVector EndCenter=PlacementTarget.TransformPosition(Bounds.Origin);
  const float ClearZ=FMath::Max(StartCenter.Z,EndCenter.Z+NativeExtent.Z+8.f);
  // Retain the clearance path and every native sweep, but execute it in the
  // input call instead of playing a timed lift/travel/lower animation.
  FTransform AboveStart=PlacementStart,AboveEnd=PlacementTarget;
  AboveStart.AddToTranslation(FVector(0,0,ClearZ-StartCenter.Z));
  AboveEnd.AddToTranslation(FVector(0,0,ClearZ-EndCenter.Z));
  Reached=MoveHeldGroup(AboveStart) && MoveHeldGroup(AboveEnd) && MoveHeldGroup(PlacementTarget);
 }
 else Reached=MoveHeldGroup(Goal);
 const FTransform ActualPose=HeldCargo->GetActorTransform();
 const FVector ActualCenter=ActualPose.TransformPosition(Bounds.Origin);
 if (Reached) { CarryStallTime=0.f;bPickupPending=false; }
 else
 {
  const bool Progress=FVector::Dist(ActualPose.GetLocation(),CarryProgressPose.GetLocation())>1.f ||
      ActualPose.GetRotation().AngularDistance(CarryProgressPose.GetRotation())>FMath::DegreesToRadians(1.f);
  CarryStallTime=Progress ? 0.f : CarryStallTime+DeltaSeconds;
 }
 CarryProgressPose=ActualPose;
 if (CarryStallTime>1.5f || (bPickupPending && PickupTime>3.f) || PlacementTime>3.f)
 { CancelCarryAction(TEXT("물체를 옮길 공간이 없어 동작을 취소했습니다."));return; }
 if (PlacementTime>=0.f && Reached)
 {
  // Verify the final contact before releasing independent physical bodies.
  FTransform Checked;
  if (FindPlacement(PlacementPallet,PlacementTarget.TransformPosition(Bounds.Origin),Checked) &&
      Checked.GetLocation().Equals(PlacementTarget.GetLocation(),2.f))
  { DropCargo();return; }
  CancelCarryAction(TEXT("받침 위 공간이 바뀌어 놓기를 취소했습니다."));return;
 }
 CarryFacing=HeldCargo->GetActorQuat();
 for (int I=0;I<2;++I)
 {
  const float Side=I==0 ? -1.f : 1.f;
  const FVector Grip=Pallet ? FVector(-NativeExtent.X+5.f,Side*23.f,0.f) :
      FVector(-NativeExtent.X+2.f,Side*FMath::Clamp(NativeExtent.Y-4.f,5.f,22.f),FMath::Clamp(20.f-NativeExtent.Z,-NativeExtent.Z+2.f,NativeExtent.Z-2.f));
  CarryHands[I]=ActualCenter+CarryFacing.RotateVector(Grip);
  CarryElbows[I]=Origin+Facing.RotateVector(FVector(5,Side*28.f,18));
 }
#if WITH_EDITOR
 if (!GetWorld()->IsGameWorld())
  for (USkeletalMeshComponent* PreviewMesh:{GetMesh(),FirstPersonMesh,CarryMesh.Get()})
  { PreviewMesh->TickAnimation(DeltaSeconds,false);PreviewMesh->RefreshBoneTransforms(); }
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
	if (IsValid(RemoteForklift))
		if (auto* PC=Cast<APlayerController>(Controller))
			if (auto* Camera=Cast<Amsc_vrCameraManager>(PC->PlayerCameraManager))
			{ Camera->AimRemoteCamera(Yaw,Pitch);return; }
	if (GetController())
	{
		// pass the rotation inputs
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void Amsc_vrCharacter::DoMove(float Right, float Forward)
{
	if (IsValid(RemoteForklift) || bObserverPresentation || PlacementTime>=0.f) return;
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
 if (bIsCrouched || IsValid(RemoteForklift) || bObserverPresentation || PlacementTime>=0.f || (Controller && Controller->IsMoveInputIgnored())) return;
 Jump();
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
