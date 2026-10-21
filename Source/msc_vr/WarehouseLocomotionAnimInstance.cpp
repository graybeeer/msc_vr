#include "WarehouseLocomotionAnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "AnimNodes/AnimNode_BlendSpacePlayer.h"
#include "AnimNodes/AnimNode_TwoWayBlend.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

bool UWarehouseLocomotionAnimInstance::PrepareMotionAsset(UAnimationAsset* Asset,USkeleton* Skeleton)
{
#if WITH_EDITOR
 if (!Asset || !Skeleton) return false;
 Asset->Modify();
 if (auto* Space=Cast<UBlendSpace>(Asset))
 {
  Space->SetSkeleton(Skeleton);
  Space->ValidateSampleData();Space->ResampleData();
 }
 else if (Asset->GetSkeleton()!=Skeleton && !Asset->ReplaceSkeleton(Skeleton,false)) return false;
 Asset->MarkPackageDirty();
 return true;
#else
 return false;
#endif
}

void UWarehouseLocomotionAnimInstance::NativeInitializeAnimation()
{
 Super::NativeInitializeAnimation();
 StandingMotion=LoadObject<UBlendSpace>(nullptr,TEXT("/Game/Warehouse/AnimationSample/BS_WarehouseStanding.BS_WarehouseStanding"));
 CrouchingMotion=LoadObject<UBlendSpace>(nullptr,TEXT("/Game/Warehouse/AnimationSample/BS_WarehouseCrouching.BS_WarehouseCrouching"));
 JumpStartMotion=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Warehouse/AnimationSample/Sequences/M_Neutral_Jump_F_Start_Stand_Lfoot.M_Neutral_Jump_F_Start_Stand_Lfoot"));
 FallMotion=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Warehouse/AnimationSample/Sequences/M_Relaxed_Jump_Loop_Fall.M_Relaxed_Jump_Loop_Fall"));
 LandingMotion=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Warehouse/AnimationSample/Sequences/M_Neutral_Jump_F_Land_Stand_Light_Lfoot.M_Neutral_Jump_F_Land_Stand_Light_Lfoot"));
 RootMotionMode=ERootMotionMode::IgnoreRootMotion;
 ensureMsgf(StandingMotion && CrouchingMotion && JumpStartMotion && FallMotion && LandingMotion,
  TEXT("Run configure_game_animation_sample.py after importing the Game Animation Sample motion assets."));
}

struct FWarehouseLocomotionProxy : FAnimInstanceProxy
{
 FAnimNode_BlendSpacePlayer_Standalone Standing,Crouching;
 FAnimNode_SequencePlayer_Standalone JumpStart,Fall,Land;
 FAnimNode_TwoWayBlend Stance,LandingPose,AirPose,Output;
 bool WasAirborne=false,HasSample=false;
 float AirSeconds=0,LandingSeconds=1;

 explicit FWarehouseLocomotionProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
 {
  Standing.SetLoop(true);Crouching.SetLoop(true);
  JumpStart.SetLoopAnimation(false);Fall.SetLoopAnimation(true);Land.SetLoopAnimation(false);
  Stance.A.SetLinkNode(&Standing);Stance.B.SetLinkNode(&Crouching);
  LandingPose.A.SetLinkNode(&Stance);LandingPose.B.SetLinkNode(&Land);
  AirPose.A.SetLinkNode(&JumpStart);AirPose.B.SetLinkNode(&Fall);
  Output.A.SetLinkNode(&LandingPose);Output.B.SetLinkNode(&AirPose);
 }
 virtual FAnimNode_Base* GetCustomRootNode() override { return &Output; }
 virtual void GetCustomNodes(TArray<FAnimNode_Base*>& Nodes) override
 { Nodes.Append({&Standing,&Crouching,&JumpStart,&Fall,&Land,&Stance,&LandingPose,&AirPose,&Output}); }
 virtual void PreUpdate(UAnimInstance* Instance,float Dt) override
 {
  FAnimInstanceProxy::PreUpdate(Instance,Dt);
  auto* Anim=CastChecked<UWarehouseLocomotionAnimInstance>(Instance);
  auto* Character=Cast<ACharacter>(Anim->GetOwningActor());
  if (!Character) return;
  if (Standing.GetBlendSpace()!=Anim->StandingMotion) Standing.SetBlendSpace(Anim->StandingMotion);
  if (Crouching.GetBlendSpace()!=Anim->CrouchingMotion) Crouching.SetBlendSpace(Anim->CrouchingMotion);
  JumpStart.SetSequence(Anim->JumpStartMotion);Fall.SetSequence(Anim->FallMotion);Land.SetSequence(Anim->LandingMotion);
  const FVector Velocity=Character->GetVelocity();
  const FVector Local=Character->GetActorQuat().UnrotateVector(Velocity);
  Anim->GroundSpeed=Velocity.Size2D();Anim->ShouldMove=Anim->GroundSpeed>3.f;
  Anim->InAir=Character->GetCharacterMovement()->IsFalling();
  Anim->Crouching=Character->bIsCrouched;
  const bool TakingOff=HasSample && !WasAirborne && Anim->InAir;
  const bool JustLanded=HasSample && WasAirborne && !Anim->InAir;
  if (TakingOff) { AirSeconds=0;JumpStart.SetAccumulatedTime(0); }
  if (JustLanded) { LandingSeconds=0;Land.SetAccumulatedTime(0); }
  AirSeconds=Anim->InAir ? AirSeconds+Dt : 0.f;
  LandingSeconds+=Dt;
  const float Direction=Anim->ShouldMove ? FMath::RadiansToDegrees(FMath::Atan2(Local.Y,Local.X)) : 0.f;
  Standing.SetPosition(FVector(Direction,Anim->GroundSpeed,0));
  Crouching.SetPosition(FVector(Direction,Anim->GroundSpeed,0));
  Stance.Alpha=FMath::FInterpConstantTo(Stance.Alpha,Anim->Crouching ? 1.f : 0.f,Dt,7.f);
  const float LandingWeight=!Anim->InAir && LandingSeconds<.35f ? FMath::Min(LandingSeconds/.06f,(.35f-LandingSeconds)/.18f) : 0.f;
  LandingPose.Alpha=FMath::Clamp(LandingWeight,0.f,1.f);
  Anim->Landing=LandingPose.Alpha>.01f;
  const float FallWeight=Velocity.Z<0.f || AirSeconds>.22f ? 1.f : 0.f;
  AirPose.Alpha=FMath::FInterpConstantTo(AirPose.Alpha,FallWeight,Dt,8.f);
  Output.Alpha=FMath::FInterpConstantTo(Output.Alpha,Anim->InAir ? 1.f : 0.f,Dt,12.f);
  WasAirborne=Anim->InAir;HasSample=true;
 }
};

FAnimInstanceProxy* UWarehouseLocomotionAnimInstance::CreateAnimInstanceProxy() { return new FWarehouseLocomotionProxy(this); }
void UWarehouseLocomotionAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
