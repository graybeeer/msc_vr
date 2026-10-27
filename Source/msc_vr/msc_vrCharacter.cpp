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
	CarryGrip->SetLinearDriveAccelerationMode(false); CarryGrip->SetLinearDriveParams(90000.f,3200.f,120000.f);
	CarryGrip->SetAngularDriveMode(EAngularDriveMode::SLERP); CarryGrip->SetOrientationDriveSLERP(true); CarryGrip->SetAngularVelocityDriveSLERP(true);
	CarryGrip->SetAngularDriveAccelerationMode(false); CarryGrip->SetAngularDriveParams(2000000.f,250000.f,1200000.f);
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
	FirstPersonCameraComponent->SetupAttachment(GetMesh(), FName("head"));
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
	if (bPhysicalRagdoll || IsValid(RemoteForklift)) return;
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
		Params.AddIgnoredActor(HeldCargo);
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
 if (bPhysicalRagdoll) return nullptr;
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
	SetRemotePhoneVisible(false); // Also reset visibility saved in the player Blueprint.
	bUseControllerRotationYaw=false; bUseControllerRotationPitch=false; bUseControllerRotationRoll=false;
	Tags.AddUnique(TEXT("WarehousePhysicalHuman"));
	GetCapsuleComponent()->SetCapsuleSize(30.f,96.f);
	StandingMeshRelative=GetMesh()->GetRelativeTransform();
	// The first-person render copy has template head stabilization. Its socket
	// must not keep the eyes at standing height while the real body crouches.
	FirstPersonCameraComponent->AttachToComponent(GetMesh(),FAttachmentTransformRules::KeepRelativeTransform,TEXT("head"));
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
	if (bPhysicalRagdoll) { UpdateRagdollRecovery(DeltaSeconds);return; }
	JumpSupportDelay=FMath::Max(0.f,JumpSupportDelay-DeltaSeconds);
	FVector Input=ConsumeMovementInputVector().GetClampedToMaxSize(1.f);
	if (IsValid(RemoteForklift) || bObserverPresentation || (Controller && Controller->IsMoveInputIgnored())) Input=FVector::ZeroVector;
	const float Speed=bIsCrouched ? 60.f : (IsValid(HeldCargo) ? 120.f : (bPhysicalSprint ? 500.f : 350.f));
	float GroundDistance=-1.f;
	const float DesiredHalf=bPhysicalCrouchRequested ? 58.f : 96.f;
	const float HeldMass=IsValid(HeldCargo) ? CarryBody(HeldCargo)->GetMass() : 0.f;
	const FVector LoadOffset=IsValid(HeldCargo) ? CarryBody(HeldCargo)->GetCenterOfMass()-GetCapsuleComponent()->GetCenterOfMass() : FVector::ZeroVector;
	bPhysicalGround=WarehouseHumanPhysics::DriveCapsule(this,Input*Speed,650.f,JumpSupportDelay<=0.f,DesiredHalf,&GroundDistance,HeldMass,LoadOffset);
	auto* Movement=GetCharacterMovement();
	Movement->Velocity=GetVelocity();
	// Chaos owns displacement; retain the template's animation telemetry only.
	Movement->UpdateProxyAcceleration();
	Movement->MovementMode=bPhysicalGround ? MOVE_Walking : MOVE_Falling;
	Movement->MaxWalkSpeed=Speed;
	const float CurrentHalf=GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	float ClearHalf=DesiredHalf;
	if (GroundDistance<0.f) ClearHalf=CurrentHalf; // Defer stance geometry changes while truly airborne.
	else if (DesiredHalf>CurrentHalf)
		ClearHalf=FMath::Min(DesiredHalf,FMath::Max(CurrentHalf,GroundDistance-1.f));
	else if (DesiredHalf<CurrentHalf)
		// Lower the physical centre with gravity before shortening its feet. Shrinking
		// ahead of it lost floor support and played a fall/landing in the crouch.
		ClearHalf=FMath::Max(DesiredHalf,FMath::Min(CurrentHalf,GroundDistance-FMath::Clamp(DeltaSeconds*150.f+1.f,1.f,6.f)));
	const float Half=FMath::FInterpConstantTo(CurrentHalf,ClearHalf,DeltaSeconds,150.f);
	// Resizing rewrites Chaos geometry and contacts. Preserve them when unchanged.
	if (Half!=CurrentHalf)
	{
		GetCapsuleComponent()->SetCapsuleHalfHeight(Half,true);
	}
	// During a stance change the capsule may have a small support gap. Keep the
	// visual soles on the same physical floor, not floating at the shortened shape.
	const float FeetHeight=bPhysicalGround ? GroundDistance-1.f : Half;
	GetMesh()->SetRelativeLocation(StandingMeshRelative.GetLocation()+FVector(0,0,96.f-FeetHeight));
	SetIsCrouched(bPhysicalCrouchRequested || Half<95.f);
	if (IsValid(HeldCargo) && (bPickupPending || PlacementTime>=0.f))
	{
		const float Upright=GetCapsuleComponent()->GetUpVector().Z;
		const bool Confined=bPickupPending && PickupTime>.15f &&
			FVector::Dist(HeldCargo->GetActorLocation(),PickupTransform.GetLocation())<3.f;
		if (Upright<.75f || (Upright<.94f && (Confined || CarryStallTime>.15f)))
			CancelCarryAction(TEXT("무리하게 당겨 균형을 잃기 전에 동작을 취소했습니다."));
	}
	const bool LostBalance=GetCapsuleComponent()->GetUpVector().Z<.5f ||
		(GetVelocity().Size()>650.f && (bPhysicalGround || GetVelocity().Size2D()>650.f));
	BalanceLostSeconds=LostBalance ? BalanceLostSeconds+DeltaSeconds : 0.f;
	if (BalanceLostSeconds>.12f)
	{
		DropCargo(); EndRemoteControl();
		WarehouseHumanPhysics::StartRagdoll(this,HumanMassKg);
		bPhysicalRagdoll=GetMesh()->IsSimulatingPhysics();
		if (bPhysicalRagdoll)
		{
			RagdollElapsed=RagdollStableSeconds=RagdollRetrySeconds=0.f;bGetUpRequested=false;
			FirstPersonMesh->SetHiddenInGame(true,true);GetMesh()->SetOwnerNoSee(false);
			GetMesh()->FirstPersonPrimitiveType=EFirstPersonPrimitiveType::None;GetMesh()->MarkRenderStateDirty();
			if (GEngine) GEngine->AddOnScreenDebugMessage(43,4.f,FColor::Yellow,TEXT("넘어졌습니다. 안전한 공간에서 자동으로 일어납니다. Space로 다시 시도할 수 있습니다."));
		}
	}
}

void Amsc_vrCharacter::CalcCamera(float DeltaSeconds,FMinimalViewInfo& OutResult)
{
	if (!bPhysicalRagdoll) { Super::CalcCamera(DeltaSeconds,OutResult);return; }
	FirstPersonCameraComponent->GetCameraView(DeltaSeconds,OutResult);
	// Sweep from the physical pelvis, not an elevated pivot that can already be
	// inside a low ceiling. Prefer the rear view, then a level view with headroom.
	const FVector Aim=GetMesh()->GetSocketLocation(TEXT("pelvis"));
	const FRotator Heading(-20,GetControlRotation().Yaw,0);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(FallenPlayerCamera),false,this);
	auto ClearEye=[&](const FVector& Desired)
	{
		FHitResult Hit;
		return GetWorld()->SweepSingleByChannel(Hit,Aim,Desired,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(12),Query) ? Hit.Location+Hit.Normal*2.f : Desired;
	};
	FVector Eye=ClearEye(Aim-Heading.Vector()*260.f);
	if (FVector::DistSquared(Eye,Aim)<FMath::Square(60.f))
		for (const float Side : {0.f,90.f,-90.f,180.f})
		{
			const FVector Candidate=ClearEye(Aim-FRotator(0,Heading.Yaw+Side,0).Vector()*180.f);
			if (FVector::DistSquared(Candidate,Aim)>FVector::DistSquared(Eye,Aim)) Eye=Candidate;
			if (FVector::DistSquared(Eye,Aim)>=FMath::Square(60.f)) break;
		}
	// If the whole body is confined, hide only its owner's render copy rather
	// than showing its interior; physical bodies and other viewers are unchanged.
	GetMesh()->SetOwnerNoSee(FVector::DistSquared(Eye,Aim)<FMath::Square(45.f));
	OutResult.Location=Eye;OutResult.Rotation=(Aim-Eye).Rotation();OutResult.bUseFirstPersonParameters=false;
}

void Amsc_vrCharacter::UpdateRagdollRecovery(float DeltaSeconds)
{
	RagdollElapsed+=DeltaSeconds;RagdollRetrySeconds-=DeltaSeconds;
	const bool Settled=GetMesh()->GetPhysicsLinearVelocity(TEXT("pelvis")).Size()<80.f &&
		GetMesh()->GetPhysicsAngularVelocityInRadians(TEXT("pelvis")).Size()<1.5f;
	RagdollStableSeconds=Settled ? RagdollStableSeconds+DeltaSeconds : 0.f;
	if (auto* PC=Cast<APlayerController>(Controller);PC && PC->IsInputKeyDown(EKeys::SpaceBar)) bGetUpRequested=true;
	if ((RagdollElapsed<1.5f && !bGetUpRequested) || RagdollStableSeconds<.4f || RagdollRetrySeconds>0.f) return;
	RagdollRetrySeconds=1.f;
	if (!TryRecoverFromRagdoll() && GEngine)
		GEngine->AddOnScreenDebugMessage(43,2.f,FColor::Yellow,TEXT("일어날 바닥이나 머리 위 공간이 없습니다. 주변 공간을 확보하면 다시 시도합니다. Space: 일어서기 재시도"));
}

bool Amsc_vrCharacter::TryRecoverFromRagdoll()
{
	const FVector Pelvis=GetMesh()->GetSocketLocation(TEXT("pelvis"));
	const FQuat Upright=FRotator(0,GetControlRotation().Yaw,0).Quaternion();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PlayerGetUp),false,this);
	FVector SafeCenter=FVector::ZeroVector;bool Found=false;
	for (int I=0;I<9 && !Found;++I)
	{
		const FVector Offset=I==0 ? FVector::ZeroVector : FVector(FMath::Cos((I-1)*PI/4),FMath::Sin((I-1)*PI/4),0)*60.f;
		FHitResult Floor;
		if (!GetWorld()->LineTraceSingleByChannel(Floor,Pelvis+Offset+FVector(0,0,35),Pelvis+Offset-FVector(0,0,220),ECC_Visibility,Query) ||
			Floor.ImpactNormal.Z<.65f || !WarehousePhysics::IsPhysicalContact(Floor.GetComponent())) continue;
		const FVector Candidate=Floor.ImpactPoint+FVector(0,0,98);
		if (GetWorld()->OverlapBlockingTestByChannel(Candidate,Upright,ECC_Pawn,FCollisionShape::MakeCapsule(30,96),Query)) continue;
		SafeCenter=Candidate;Found=true;
	}
	if (!Found) return false;
	FVector MomentumVelocity=GetMesh()->GetPhysicsLinearVelocity(TEXT("pelvis"));MomentumVelocity.Z=0;
	GetMesh()->SetAllBodiesSimulatePhysics(false);GetMesh()->SetPhysicsBlendWeight(0.f);GetMesh()->SetEnablePhysicsBlending(false);
	GetMesh()->SetCollisionProfileName(TEXT("NoCollision"));
	auto* Capsule=GetCapsuleComponent();Capsule->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	SetRootComponent(Capsule);GetMesh()->AttachToComponent(Capsule,FAttachmentTransformRules::KeepWorldTransform);
	Capsule->SetWorldLocationAndRotation(SafeCenter,Upright,false,nullptr,ETeleportType::TeleportPhysics);
	Capsule->SetCapsuleSize(30,96);Capsule->SetCollisionProfileName(TEXT("Pawn"));
	WarehousePhysics::ConfigureContact(Capsule,EWarehouseSurface::Rubber,HumanMassKg);
	Capsule->SetCollisionResponseToAllChannels(ECR_Block);Capsule->SetEnableGravity(true);Capsule->SetSimulatePhysics(true);
	Capsule->SetPhysicsLinearVelocity(MomentumVelocity);
	GetMesh()->SetRelativeTransform(StandingMeshRelative);GetMesh()->SetOwnerNoSee(!bObserverPresentation);
	GetMesh()->FirstPersonPrimitiveType=EFirstPersonPrimitiveType::WorldSpaceRepresentation;GetMesh()->MarkRenderStateDirty();
	FirstPersonMesh->SetHiddenInGame(bObserverPresentation,true);
	SetRemotePhoneVisible(false);
	bPhysicalRagdoll=bPhysicalCrouchRequested=bPhysicalGround=bGetUpRequested=false;
	BalanceLostSeconds=JumpSupportDelay=0.f;SetIsCrouched(false);
	GetCharacterMovement()->MovementMode=MOVE_Walking;GetCharacterMovement()->Velocity=MomentumVelocity;
	if (GEngine) GEngine->AddOnScreenDebugMessage(43,2.f,FColor::Green,TEXT("안전한 바닥에서 일어났습니다."));
	return true;
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
	if (bPhysicalRagdoll || PlacementTime>=0 || bObserverPresentation || (Controller && Controller->IsMoveInputIgnored())) return false;
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
 if (IsValid(Cargo) && Cargo->ActorHasTag(TEXT("LogisticsInTransit"))) return false;
 if (auto* Box=Cast<AWarehouseCargo>(Cargo);Box && Box->GetWrappedPallet())
 {
  if (GEngine) GEngine->AddOnScreenDebugMessage(41,2.f,FColor::Yellow,TEXT("입고 확정된 랩 포장 물류입니다. 팔레트 전체를 지게차로 운반하세요."));
  return false;
 }
 if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->HasFailed(Cargo)) return false;
	if (IsValid(HeldCargo) || !IsValid(Cargo) || Cargo->GetAttachParentActor() || !CarryBody(Cargo)->GetStaticMesh() || !InitializeCarryPresentation()) return false;
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
	// Firm, damped hands, still limited to 1200 N / 120 Nm with real collisions.
	// A broad 25 kg pallet has greater inertia than a small carton.
	const bool CarryingPallet=Cast<AWarehousePallet>(Cargo)!=nullptr;
	CarryGrip->SetLinearDriveParams(90000.f,3200.f,120000.f);
	CarryGrip->SetAngularDriveParams(CarryingPallet ? 6000000.f : 2000000.f,CarryingPallet ? 800000.f : 250000.f,1200000.f);
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
	CarryProgressPose=PickupTransform;
	CarryStallTime=0.f;
	bPickupPending=true;
	PickupTime=0.f;
	CarryTargetYaw=FirstPersonCameraComponent->GetComponentRotation().Yaw;
	CarryPhase=0.f;
	NormalWalkSpeed=GetCharacterMovement()->MaxWalkSpeed;
	GetCharacterMovement()->MaxWalkSpeed=FMath::Min(NormalWalkSpeed,260.f);
	Body->FirstPersonPrimitiveType=EFirstPersonPrimitiveType::None;
	Body->MarkRenderStateDirty();
	CarryMesh->SetComponentTickEnabled(true);
	CarryMesh->SetVisibility(true);
	FirstPersonMesh->SetVisibility(false,false);
	RemoteWorldMesh->SetComponentTickEnabled(true); RemoteWorldMesh->SetVisibility(true);
	GetMesh()->SetVisibility(false,false);
	FirstPersonCameraComponent->FirstPersonFieldOfView=90.f;
	UpdateCarryPose(0.f);
	return true;
}

void Amsc_vrCharacter::DropCargo()
{
 PlacementTime=-1.f; PlacementRotateTime=-1.f; PlacementLowerTime=-1.f; PlacementPallet=nullptr;
 bPickupPending=false; CarryStallTime=0.f; PlacementContactTime=0.f; GripSlipTime=0.f;
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

void Amsc_vrCharacter::CancelCarryAction(const TCHAR* Reason)
{
 UE_LOG(Logmsc_vr,Log,TEXT("Carry action cancelled: %s object=%s pickup=%.2f placement=%.2f"),
     Reason,*GetNameSafe(HeldCargo),PickupTime,PlacementTime);
 if (GEngine) GEngine->AddOnScreenDebugMessage(41,3.f,FColor::Yellow,Reason);
 // Release at the actual contact pose with its existing momentum. Never pull
 // through an obstruction or teleport back to a previously clear transform.
 DropCargo();
}

void Amsc_vrCharacter::UpdateGripTarget(const FTransform& Pose)
{
 const FQuat Parent=GetCapsuleComponent()->GetComponentQuat();
 const FVector Center=Pose.TransformPosition(CarryBody(HeldCargo)->GetStaticMesh()->GetBounds().Origin);
 CarryGrip->SetConstraintReferenceFrame(EConstraintFrame::Frame1,FTransform(Parent.Inverse()*Pose.GetRotation(),Parent.UnrotateVector(Center-GetCapsuleComponent()->GetComponentLocation())));
 CarryGrip->SetLinearPositionTarget(FVector::ZeroVector);
 CarryGrip->SetAngularOrientationTarget(FRotator::ZeroRotator);
 // Placement aims at a stationary world support. Damping relative to a swaying
 // player instead of the world makes the cargo follow that sway and overshoot.
 // Compensate only the drive target; both bodies still receive the reaction.
 const bool Placing=PlacementTime>=0.f;
 // Chaos solves Body1 relative to Body2 (the held body's frame here).
 const FQuat DriveFrame=CarryBody(HeldCargo)->GetComponentQuat();
 CarryGrip->SetLinearVelocityTarget(Placing ? DriveFrame.UnrotateVector(GetCapsuleComponent()->GetPhysicsLinearVelocityAtPoint(Center)) : FVector::ZeroVector);
 CarryGrip->SetAngularVelocityTarget(Placing ? DriveFrame.UnrotateVector(GetCapsuleComponent()->GetPhysicsAngularVelocityInRadians())/(2.f*PI) : FVector::ZeroVector);
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
	if (!IsValid(Pallet) || !IsValid(HeldCargo) || Pallet==HeldCargo || Pallet->GetAttachParentActor() ||
		FVector::Dist(Aim,FirstPersonCameraComponent->GetComponentLocation())>260.f) return false;
	if (auto* Strength=AWarehouseDamageSystem::Find(this); Strength && Strength->HasFailed(Pallet)) return false;
	if (FMath::Abs(Pallet->GetActorRotation().Pitch)>2 || FMath::Abs(Pallet->GetActorRotation().Roll)>2) return false;
	const auto* HeldPallet=Cast<AWarehousePallet>(HeldCargo);
	if (HeldPallet)
	{
		if (HeldPallet->PayloadMassKg>0 || Pallet->PayloadMassKg>0 || Pallet->GetVelocity().Size()>15.f ||
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
	Params.AddIgnoredActor(HeldCargo);
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
	PlacementRotateTime=-1.f;
	PlacementLowerTime=-1.f;
	bPickupPending=false;
	CarryStallTime=0.f;
	PlacementContactTime=0.f;
	CarryProgressPose=PlacementStart;
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
			RemoteWorldMesh->SetVisibility(false); RemoteWorldMesh->SetComponentTickEnabled(false);
			GetMesh()->SetVisibility(true,false);
			FirstPersonCameraComponent->FirstPersonFieldOfView=NormalFirstPersonFOV;
		}
		return;
	}
 // The grip is a finite force between two dynamic bodies; only its target moves.
 auto* Body=CarryBody(HeldCargo);
 const auto Bounds=Body->GetStaticMesh()->GetBounds();
 const FVector NativeExtent=Bounds.BoxExtent*HeldCargo->GetActorScale3D().GetAbs();
 const FVector Origin=GetActorLocation();
 const bool Pallet=Cast<AWarehousePallet>(HeldCargo)!=nullptr;
 // Limit target slew without adding the old slow, floating follow motion.
 CarryTargetYaw=FMath::FixedTurn(CarryTargetYaw,FirstPersonCameraComponent->GetComponentRotation().Yaw,DeltaSeconds*(Pallet ? 90.f : 360.f));
 const FQuat Facing=FRotator(0,CarryTargetYaw,0).Quaternion();
 const FQuat DesiredRotation=Facing*(Pallet ? FRotator(90,0,0).Quaternion() : FQuat::Identity);
 const float Depth=Pallet ? NativeExtent.Z : NativeExtent.X;
 const float Height=Pallet ? 15.f : 10.f+NativeExtent.Z;
 const FVector GoalCenter=Origin+Facing.RotateVector(FVector(GetCapsuleComponent()->GetScaledCapsuleRadius()+8.f+Depth,0,Height));
 FTransform Goal(DesiredRotation,GoalCenter-DesiredRotation.RotateVector(Bounds.Origin*HeldCargo->GetActorScale3D()),HeldCargo->GetActorScale3D());
 PickupTime+=DeltaSeconds;
 if (PlacementTime>=0.f)
 {
  if (!IsValid(PlacementPallet) || !PlacementPallet->GetActorLocation().Equals(PlacementPalletPose.GetLocation(),2.f) ||
      PlacementPallet->GetActorQuat().AngularDistance(PlacementPalletPose.GetRotation())>FMath::DegreesToRadians(2.f) ||
      !PlacementPallet->GetActorScale3D().Equals(PlacementPalletPose.GetScale3D(),.01f))
  { CancelCarryAction(TEXT("받침 위치가 바뀌어 놓기를 취소했습니다.")); return; }
  else
  {
   PlacementTime+=DeltaSeconds;
   Goal=PlacementTarget;
   {
    // Centre the load above the deck before lowering with the finite grip.
    // A diagonal carton descent can strike a board edge and move its support.
    const FVector StartCenter=PlacementStart.TransformPosition(Bounds.Origin);
    const FVector EndCenter=PlacementTarget.TransformPosition(Bounds.Origin);
    // Move the upright pallet above its support before rotating. Rotating while
    // crossing the support can hook a foot underneath a board and lift both pallets.
    const float Transit=FMath::Max(.4f,FVector::Dist2D(StartCenter,EndCenter)/120.f);
    const float Across=FMath::SmoothStep(0.f,1.f,FMath::Min(PlacementTime/Transit,1.f));
    FVector Center=FMath::Lerp(StartCenter,EndCenter,Across);
    Center.Z=FMath::Max(StartCenter.Z,EndCenter.Z+(Pallet ? NativeExtent.Size() : NativeExtent.Z)+8.f);
    const FQuat ActualRotation=HeldCargo->GetActorQuat();
    if (PlacementRotateTime<0.f && PlacementTime>=Transit &&
        FVector::Dist2D(Body->Bounds.Origin,EndCenter)<4.f &&
        FMath::Abs(Body->Bounds.Origin.Z-Center.Z)<5.f && Body->GetPhysicsLinearVelocity().Size()<25.f)
     PlacementRotateTime=0.f;
    if (PlacementRotateTime<0.f) Goal.SetRotation(PlacementStart.GetRotation());
    else
    {
     PlacementRotateTime+=DeltaSeconds;
     Goal.SetRotation(FQuat::Slerp(PlacementStart.GetRotation(),PlacementTarget.GetRotation(),
       FMath::SmoothStep(0.f,1.f,FMath::Min(PlacementRotateTime/(Pallet ? .9f : .25f),1.f))));
    }
    if (PlacementLowerTime<0.f && PlacementRotateTime>(Pallet ? .9f : .25f) &&
        ActualRotation.AngularDistance(PlacementTarget.GetRotation())<FMath::DegreesToRadians(3.f) &&
        FVector::Dist2D(Body->Bounds.Origin,EndCenter)<3.f && Body->GetPhysicsLinearVelocity().Size2D()<20.f)
    {
     PlacementLowerTime=0.f;
     PlacementLowerStartZ=Body->Bounds.Origin.Z;
     // Lower with support from gravity, without driving a blocked load hard
     // downward and levering the carrier upward at full extraction strength.
     CarryGrip->SetLinearDriveParams(90000.f,3200.f,60000.f);
    }
    if (PlacementLowerTime<0.f)
    {
     const float VerticalExtent=FMath::Abs(ActualRotation.GetAxisX().Z)*NativeExtent.X+
         FMath::Abs(ActualRotation.GetAxisY().Z)*NativeExtent.Y+FMath::Abs(ActualRotation.GetAxisZ().Z)*NativeExtent.Z;
     // Finish rotating, centring and braking above the deck before making contact.
     // Otherwise the wooden feet can catch a board gap while still sliding sideways.
     Center.Z=FMath::Max(Center.Z,EndCenter.Z+VerticalExtent-NativeExtent.Z+8.f);
    }
    else
    {
     PlacementLowerTime+=DeltaSeconds;
     // Leave enough braking distance for a 30 kg load at the finite 600 N
     // support limit, rather than accelerating it into the deck at the end.
     Center.Z=FMath::Lerp(PlacementLowerStartZ,EndCenter.Z,FMath::SmoothStep(0.f,1.f,FMath::Min(PlacementLowerTime/.8f,1.f)));
    }
    Goal.SetLocation(Center-Goal.GetRotation().RotateVector(Bounds.Origin*Goal.GetScale3D()));
   }
   const FVector Actual=HeldCargo->GetActorTransform().TransformPosition(Bounds.Origin);
   const FVector Target=PlacementTarget.TransformPosition(Bounds.Origin);
   FCollisionQueryParams Query(SCENE_QUERY_STAT(HandPlacementSupport),true,this); Query.AddIgnoredActor(HeldCargo);
   bool Supported=false;
   const bool Aligned=!Pallet || HeldCargo->GetActorQuat().AngularDistance(PlacementTarget.GetRotation())<FMath::DegreesToRadians(2.f);
   const bool Arrived=FVector::Dist2D(Actual,Target)<3.f && FMath::Abs(Actual.Z-Target.Z)<2.f;
   const bool Settled=Body->GetPhysicsLinearVelocity().Size()<12.f && Body->GetPhysicsAngularVelocityInDegrees().Size()<20.f;
   if (Arrived && Settled && Aligned)
   {
    bool Quadrants[4]={false,false,false,false};
    for (float X : {-.9f,-.5f,-.15f,.15f,.5f,.9f}) for (float Y : {-.9f,-.5f,-.15f,.15f,.5f,.9f})
    {
     const FVector Point=Actual+HeldCargo->GetActorQuat().RotateVector(FVector(X*NativeExtent.X,Y*NativeExtent.Y,0));
     FHitResult Contact;
     if (GetWorld()->LineTraceSingleByChannel(Contact,Point,Point-FVector(0,0,NativeExtent.Z+5.f),ECC_Visibility,Query) &&
         (Contact.GetActor()==PlacementPallet || (!Pallet && Cast<AWarehouseCargo>(Contact.GetActor()))) &&
         Contact.ImpactNormal.Z>.95f && Contact.ImpactPoint.Z>=Actual.Z-NativeExtent.Z-2.5f)
      Quadrants[(X>0 ? 1 : 0)+(Y>0 ? 2 : 0)]=true;
    }
    Supported=Quadrants[0] && Quadrants[1] && Quadrants[2] && Quadrants[3];
   }
   if (PlacementTime>.65f && Arrived && Settled && Supported && Aligned)
   { DropCargo(); return; }
   if (PlacementTime>6.f) { CancelCarryAction(TEXT("놓기가 지연되어 취소했습니다. 주변 공간을 확인하세요.")); return; }
  }
 }
 else
 {
  const float Alpha=FMath::SmoothStep(0.f,1.f,FMath::Min(PickupTime/.3f,1.f));
  FTransform Interpolated; Interpolated.Blend(PickupTransform,Goal,Alpha); Goal=Interpolated;
 }
 UpdateGripTarget(Goal);
 const FVector ActualCenter=HeldCargo->GetActorTransform().TransformPosition(Bounds.Origin);
 const float Error=FVector::Dist(ActualCenter,Goal.TransformPosition(Bounds.Origin));
 const float AngleError=HeldCargo->GetActorQuat().AngularDistance(Goal.GetRotation());
 const bool LoweringBlocked=PlacementLowerTime>=0.f &&
     ActualCenter.Z-Goal.TransformPosition(Bounds.Origin).Z>5.f && Body->GetPhysicsLinearVelocity().Z>-5.f;
 PlacementContactTime=LoweringBlocked ? PlacementContactTime+DeltaSeconds : 0.f;
 if (PlacementContactTime>.05f)
 { CancelCarryAction(TEXT("내려놓는 경로가 막혀 취소했습니다. 받침 위 공간을 확인하세요.")); return; }
 // A trapped load must not lever the carrier over while the stronger hands
 // wait for the ordinary timeout. Detect failed extraction before that impulse builds.
 if (bPickupPending && PickupTime>.12f && Error>15.f &&
     FVector::Dist(HeldCargo->GetActorLocation(),PickupTransform.GetLocation())<3.f)
 { CancelCarryAction(TEXT("물체가 끼어 집기를 취소했습니다. 주변 공간을 확인하세요.")); return; }
 if (bPickupPending && PickupTime>=.3f && Error<4.f && AngleError<FMath::DegreesToRadians(5.f)) bPickupPending=false;
 if (bPickupPending && PickupTime>3.f)
 { CancelCarryAction(TEXT("집기가 지연되어 취소했습니다. 끼인 물체를 확인하세요.")); return; }
 // Measure actual progress, not elapsed target animation. Small contact jitter
 // cannot keep a blocked action alive; the absolute deadlines also bound drift.
 const bool Progress=FVector::Dist(ActualCenter,CarryProgressPose.TransformPosition(Bounds.Origin))>1.f ||
     HeldCargo->GetActorQuat().AngularDistance(CarryProgressPose.GetRotation())>FMath::DegreesToRadians(1.f);
 if (Progress || (Error<4.f && AngleError<FMath::DegreesToRadians(5.f)))
 { CarryProgressPose=HeldCargo->GetActorTransform(); CarryStallTime=0.f; }
 else CarryStallTime+=DeltaSeconds;
 if (CarryStallTime>1.5f || (PlacementTime>=0.f && CarryStallTime>.15f && Error>10.f))
 { CancelCarryAction(TEXT("물체가 막혀 동작을 취소했습니다. 공간을 확보한 뒤 다시 시도하세요.")); return; }
 GripSlipTime=PickupTime>1.f && Error>40.f ? GripSlipTime+DeltaSeconds : 0.f;
 if (GripSlipTime>.35f)
 {
  UE_LOG(Logmsc_vr,Warning,TEXT("Physical grip slipped: %s placement=%.2f error=%.2f actual=%s goal=%s"),
      *HeldCargo->GetName(),PlacementTime,Error,*ActualCenter.ToString(),*Goal.TransformPosition(Bounds.Origin).ToString());
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
	if (bPhysicalRagdoll) { bGetUpRequested=true;RagdollRetrySeconds=0.f;return; }
	if (bPhysicalRagdoll || !bPhysicalGround || bIsCrouched || IsValid(RemoteForklift) || PlacementTime>=0.f || (Controller && Controller->IsMoveInputIgnored())) return;
	// Input callbacks may run twice before the next physics/movement tick.
	// Consume the supported state now so one take-off adds only one impulse.
	bPhysicalGround=false;
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
