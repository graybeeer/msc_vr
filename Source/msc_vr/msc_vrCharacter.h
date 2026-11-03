// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "msc_vrCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class AWarehouseCargo;
class AWarehousePallet;
class AWarehouseForklift;
class UStaticMeshComponent;
class UMaterialInterface;
class UPhysicsConstraintComponent;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 *  A basic first person character
 */
UCLASS(abstract)
class Amsc_vrCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn mesh: first person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

protected:

	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	class UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category ="Input")
	class UInputAction* MouseLookAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CarryAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RemoteAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> CarryMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<AActor> HeldCargo;
	UPROPERTY(Transient) TObjectPtr<AWarehouseForklift> RemoteForklift;
	UPROPERTY(VisibleAnywhere, Category="Remote") TObjectPtr<UStaticMeshComponent> RemotePhone;
	UPROPERTY(VisibleAnywhere, Category="Remote") TObjectPtr<UStaticMeshComponent> RemoteWorldPhone;
	UPROPERTY(VisibleAnywhere, Category="Remote") TObjectPtr<USkeletalMeshComponent> RemoteWorldMesh;
	bool bRemoteHadViewportFocus = false;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> RemoteCarryMaterials;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> RemoteWorldMaterials;
	bool bObserverPresentation = false;
	void UpdateRemoteControl();
	bool InitializeCarryPresentation();
	void SetRemoteCharacterFade(bool Enabled);
	void SetRemotePhoneVisible(bool Visible);

	UPROPERTY(EditDefaultsOnly, Category="Carry", meta=(ClampMin="0.05", ClampMax="1.0"))
	float CarryOpacity = .3f;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> CarryTransparentMaterial;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> HeldOriginalMaterials;
	struct FHeldBodyState
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UStaticMeshComponent> Body;
		FTransform RelativePose;
		ECollisionEnabled::Type Collision = ECollisionEnabled::QueryAndPhysics;
		float MassKg = 0.f;
		int32 MaterialStart = 0, MaterialCount = 0;
		bool bSimulated = false, bGravity = true, bMassOverride = false, bNanite = false;
		bool bTickEnabled = false, bIgnoredByPlayer = false, bHadHeldTag = false;
	};
	struct FHeldJointState
	{
		TWeakObjectPtr<UPhysicsConstraintComponent> Joint;
		FTransform Frame1, Frame2;
	};
	TArray<FHeldBodyState> HeldBodies;
	TArray<FHeldJointState> HeldJoints;
	float HeldMovementMass = -1.f;
	bool MoveHeldGroup(const FTransform& Target,bool Instant=false);
	bool CanPlaceHeldGroup(const FTransform& Target) const;
	bool FindCarryTarget(FTransform& Target) const;
	bool bInstantCarryPickup=false;

	UPROPERTY(VisibleAnywhere, Category="Carry")
	TObjectPtr<USkeletalMeshComponent> CarryMesh;
	float CarryBlend = 0.f;
	float NormalFirstPersonFOV = 70.f;
	FVector CarryHands[2] = {FVector::ZeroVector,FVector::ZeroVector};
	FVector CarryElbows[2] = {FVector::ZeroVector,FVector::ZeroVector};
	FQuat CarryFacing = FQuat::Identity;
	FTransform PickupTransform;
	FTransform CarryProgressPose;
	float CarryStallTime = 0.f;
	bool bPickupPending = false;
	void CancelCarryAction(const TCHAR* Reason);
	bool TryPickupObject(AActor* Object);
	float PickupTime = 0.f;
	bool InitializeCarryMesh();
	UPROPERTY(Transient) TObjectPtr<AWarehousePallet> PlacementPallet;
	FTransform PlacementStart, PlacementTarget, PlacementPalletPose;
	float PlacementTime = -1.f;
	UPROPERTY(VisibleAnywhere, Category="Carry") TObjectPtr<UPhysicsConstraintComponent> CarryGrip;
	UPROPERTY(EditAnywhere, Category="Human Physics") float HumanMassKg=80.f;
	void UpdatePlayerMovement();
	void UpdateLocomotion();
	bool FindPlacement(AWarehousePallet* Pallet, const FVector& Aim, FTransform& Target) const;
	
public:
	Amsc_vrCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void Tick(float DeltaSeconds) override;
	UFUNCTION(BlueprintPure, Category="Carry") AActor* GetHeldCargo() const { return HeldCargo; }
	UFUNCTION(BlueprintPure, Category="Carry") TArray<AActor*> GetHeldCargoGroup() const;
	UFUNCTION(BlueprintPure, Category="Carry") bool IsPlacingCargo() const { return PlacementTime>=0.f; }
	UFUNCTION(BlueprintPure, Category="Human Physics") bool IsPhysicalRagdoll() const { return false; }
	FQuat GetCarryTorsoLean() const;
	virtual void OnStartCrouch(float HeightAdjust, float ScaledHeightAdjust) override;
	virtual void OnEndCrouch(float HeightAdjust, float ScaledHeightAdjust) override;
	UFUNCTION(BlueprintCallable, Category="Input") void SetLocomotionInput(bool Sprint, bool Crouching);
	void SetObserverPresentation(bool Observing);
	UFUNCTION(BlueprintCallable, Category="Remote") void ToggleRemoteControl();
	UFUNCTION(BlueprintCallable, Category="Remote") bool BeginRemoteControl(AWarehouseForklift* Forklift);
	UFUNCTION(BlueprintCallable, Category="Remote") void EndRemoteControl();
	UFUNCTION(BlueprintPure, Category="Remote") AWarehouseForklift* GetRemoteForklift() const { return RemoteForklift; }
	UFUNCTION(BlueprintPure, Category="Remote") bool HasHeldCargo() const { return IsValid(HeldCargo); }
	UFUNCTION(BlueprintPure, Category="Input") AActor* GetInteractionTarget(float RangeCm=300.f) const;
	USkeletalMeshComponent* GetRemoteWorldMesh() const { return RemoteWorldMesh; }
	FQuat GetHandFacing(int Index) const;
	UFUNCTION(BlueprintCallable, Category="Carry")
	bool TryPickupCargo(AWarehouseCargo* Cargo);
	UFUNCTION(BlueprintCallable, Category="Carry") bool TryPickupPallet(AWarehousePallet* Pallet);
	UFUNCTION(BlueprintCallable, Category="Carry") bool TryPlaceOnSurface(FVector Aim);
	UFUNCTION(BlueprintCallable, Category="Carry") bool TryPlaceOnRack(AActor* Rack,FVector Aim);
	UFUNCTION(BlueprintCallable, Category="Carry")
	void DropCargo();
	UFUNCTION(BlueprintCallable, Category="Carry")
	bool TryPlaceOnPallet(AWarehousePallet* Pallet, FVector Aim);
	UFUNCTION(BlueprintCallable, Category="Carry")
	void UpdateCarryPose(float DeltaSeconds);
	UFUNCTION(BlueprintPure, Category="Carry")
	float GetCarryBlend() const { return CarryBlend; }
	UFUNCTION(BlueprintPure, Category="Carry")
	FVector GetCarryHandLocation(int Index) const { return CarryHands[FMath::Clamp(Index,0,1)]; }
	FVector GetCarryElbowLocation(int Index) const { return CarryElbows[Index]; }
	FQuat GetCarryFacing() const { return CarryFacing; }

protected:

	/** Called from Input Actions for movement input */
	void MoveInput(const FInputActionValue& Value);

	/** Called from Input Actions for looking input */
	void LookInput(const FInputActionValue& Value);

	/** Handles aim inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoAim(float Yaw, float Pitch);

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles jump start inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpStart();

	/** Handles jump end inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category="Input")
	virtual void DoJumpEnd();

	/** Pick up or drop a nearby tagged box or crate. */
	UFUNCTION(BlueprintCallable, Category="Input")
	void ToggleCarry();

protected:

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	

public:

	/** Returns the first person mesh **/
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	/** Returns first person camera component **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

};

