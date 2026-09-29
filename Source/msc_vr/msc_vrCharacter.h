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
class UMaterialInterface;
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

	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> CarryMappingContext;

	UPROPERTY(Transient)
	TObjectPtr<AWarehouseCargo> HeldCargo;

	UPROPERTY(EditDefaultsOnly, Category="Carry", meta=(ClampMin="0.05", ClampMax="1.0"))
	float CarryOpacity = .3f;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> CarryTransparentMaterial;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> HeldOriginalMaterials;
	bool HeldDisallowedNanite = false;

	UPROPERTY(VisibleAnywhere, Category="Carry")
	TObjectPtr<USkeletalMeshComponent> CarryMesh;
	float CarryBlend = 0.f;
	float CarryPhase = 0.f;
	float NormalWalkSpeed = 0.f;
	float NormalFirstPersonFOV = 70.f;
	FVector CarryHands[2] = {FVector::ZeroVector,FVector::ZeroVector};
	FVector CarryElbows[2] = {FVector::ZeroVector,FVector::ZeroVector};
	FQuat CarryFacing = FQuat::Identity;
	FTransform PickupTransform;
	float PickupTime = 0.f;
	bool InitializeCarryMesh();
	
public:
	Amsc_vrCharacter();
	virtual void Tick(float DeltaSeconds) override;
	UFUNCTION(BlueprintCallable, Category="Carry")
	bool TryPickupCargo(AWarehouseCargo* Cargo);
	UFUNCTION(BlueprintCallable, Category="Carry")
	void DropCargo();
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
	void ToggleCarry();

protected:

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	

public:

	/** Returns the first person mesh **/
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }

	/** Returns first person camera component **/
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }

};

