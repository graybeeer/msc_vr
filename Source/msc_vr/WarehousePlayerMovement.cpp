#include "WarehousePlayerMovement.h"
#include "msc_vrCharacter.h"
#include "GameFramework/Character.h"

UWarehousePlayerMovement::UWarehousePlayerMovement(const FObjectInitializer& ObjectInitializer)
 : Super(ObjectInitializer)
{
 MaxWalkSpeed=350.f;
 MinAnalogWalkSpeed=0.f;
 AirControl=1.f;
 AirControlBoostMultiplier=0.f;
 bOrientRotationToMovement=false;
 bUseControllerDesiredRotation=false;
 bUseRVOAvoidance=false;
 bTickBeforeOwner=true;
}

void UWarehousePlayerMovement::CalcVelocity(float DeltaTime,float Friction,bool bFluid,float BrakingDeceleration)
{
 if (!IsMovingOnGround() && !IsFalling())
 {
  Super::CalcVelocity(DeltaTime,Friction,bFluid,BrakingDeceleration);
  return;
 }
 // Preserve the engine's ownership/root-motion guards. The player uses in-place
 // poses; simulated network proxies receive velocity from their authority.
 if (!HasValidData() || DeltaTime<MIN_TICK_TIME || HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity() ||
     (CharacterOwner->GetLocalRole()==ROLE_SimulatedProxy && !bWasSimulatingRootMotion)) return;

 const FVector Input=ProjectToGravityFloor(Acceleration);
 const double MaxAccel=GetMaxAcceleration();
 const double InputScale=MaxAccel>UE_SMALL_NUMBER ? FMath::Clamp(Input.Size()/MaxAccel,0.,1.) : 0.;
 const FVector Desired=Input.GetSafeNormal()*FMath::Max(0.f,GetMaxSpeed())*InputScale;
 // This is a velocity, not a displacement: PhysWalking/PhysFalling integrate
 // DeltaTime and retain capsule sweeps, wall sliding, stairs and moving bases.
 // Only the gravity axis retains momentum for jumping and falling.
 Velocity=Desired+GetGravitySpaceComponentZ(Velocity);
}

void UWarehousePlayerMovement::ApplyAccumulatedForces(float DeltaSeconds)
{
 // Contact impulses do not turn the player into a loose rigid body. Ordinary
 // Jump() and gravity are handled separately by CharacterMovement; deliberate
 // LaunchCharacter calls also retain the engine's separate pending-launch path.
 PendingImpulseToApply=FVector::ZeroVector;
 PendingForceToApply=FVector::ZeroVector;
}

void UWarehousePlayerMovement::PostPhysicsTickComponent(float DeltaTime,FCharacterMovementComponentPostPhysicsTickFunction& ThisTickFunction)
{
 const bool HadDeferredBase=bDeferUpdateBasedMovement;
 Super::PostPhysicsTickComponent(DeltaTime,ThisTickFunction);
 // The normal owner tick follows input movement, but a simulated elevator base
 // moves the capsule again after physics. Refresh held cargo/phone placement at
 // that final pose without advancing carry animation or timeout clocks twice.
 if (HadDeferredBase && HasValidData())
  if (auto* Player=Cast<Amsc_vrCharacter>(CharacterOwner)) Player->UpdateCarryPose(0.f);
}
