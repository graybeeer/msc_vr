#include "WarehouseCarryAnimInstance.h"
#include "msc_vrCharacter.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeSpaceConversions.h"
#include "AnimNodes/AnimNode_CopyPoseFromMesh.h"
#include "BoneControllers/AnimNode_TwoBoneIK.h"
#include "BoneControllers/AnimNode_ModifyBone.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

struct FWarehouseCarryProxy : FAnimInstanceProxy
{
 FAnimNode_CopyPoseFromMesh Copy;
 FAnimNode_ConvertLocalToComponentSpace Component;
 FAnimNode_ModifyBone Torso;
 FAnimNode_TwoBoneIK Arms[2];
 FAnimNode_ModifyBone Hands[2];
 FAnimNode_ConvertComponentToLocalSpace Output;
 FQuat PalmBasis[2] = {FQuat::Identity,FQuat::Identity};

 explicit FWarehouseCarryProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance)
 {
  Component.LocalPose.SetLinkNode(&Copy);
  Torso.BoneToModify.BoneName=TEXT("spine_01");
  Torso.RotationMode=BMM_Additive; Torso.RotationSpace=BCS_WorldSpace;
  Torso.ComponentPose.SetLinkNode(&Component);
  for (int I=0; I<2; ++I)
  {
   Arms[I].IKBone.BoneName=I==0 ? TEXT("hand_l") : TEXT("hand_r");
   Arms[I].EffectorLocationSpace=BCS_WorldSpace;
   Arms[I].JointTargetLocationSpace=BCS_WorldSpace;
   Arms[I].bAllowStretching=false;
   Arms[I].ComponentPose.SetLinkNode(I==0 ? static_cast<FAnimNode_Base*>(&Torso) : &Hands[0]);
   Hands[I].BoneToModify.BoneName=Arms[I].IKBone.BoneName;
   Hands[I].RotationMode=BMM_Replace;
   Hands[I].RotationSpace=BCS_WorldSpace;
   Hands[I].ComponentPose.SetLinkNode(&Arms[I]);
  }
  Output.ComponentPose.SetLinkNode(&Hands[1]);
 }
 virtual FAnimNode_Base* GetCustomRootNode() override { return &Output; }
 virtual void GetCustomNodes(TArray<FAnimNode_Base*>& Nodes) override
 {
  Nodes.Append({&Copy,&Component,&Torso,&Arms[0],&Hands[0],&Arms[1],&Hands[1],&Output});
 }
 virtual void Initialize(UAnimInstance* Instance) override
 {
  FAnimInstanceProxy::Initialize(Instance);
  if (auto* Character=Cast<Amsc_vrCharacter>(Instance->GetOwningActor()))
  {
   auto* Source=Instance->GetSkelMeshComponent()==Character->GetRemoteWorldMesh() ? Character->GetMesh() : Character->GetFirstPersonMesh();
   Copy.SourceMeshComponent=Source;
   if (!Source->GetSkeletalMeshAsset()) return;
   const FReferenceSkeleton& Ref=Source->GetSkeletalMeshAsset()->GetRefSkeleton();
   TArray<FTransform> Pose=Ref.GetRefBonePose();
   for (int B=1; B<Pose.Num(); ++B) Pose[B]=Pose[B]*Pose[Ref.GetParentIndex(B)];
   for (int I=0; I<2; ++I)
   {
    const FString Suffix=I==0 ? TEXT("_l") : TEXT("_r");
    const int H=Ref.FindBoneIndex(FName(*(TEXT("hand")+Suffix)));
    const int M=Ref.FindBoneIndex(FName(*(TEXT("middle_01")+Suffix)));
    const int Index=Ref.FindBoneIndex(FName(*(TEXT("index_01")+Suffix)));
    const int Pinky=Ref.FindBoneIndex(FName(*(TEXT("pinky_01")+Suffix)));
    if (H!=INDEX_NONE && M!=INDEX_NONE && Index!=INDEX_NONE && Pinky!=INDEX_NONE)
    {
     const FVector Finger=Pose[H].InverseTransformVectorNoScale(Pose[M].GetLocation()-Pose[H].GetLocation()).GetSafeNormal();
     const FVector Across=Pose[H].InverseTransformVectorNoScale(Pose[Index].GetLocation()-Pose[Pinky].GetLocation());
     const FVector Normal=FVector::CrossProduct(Finger,Across).GetSafeNormal()*(I==0 ? -1.f : 1.f);
     PalmBasis[I]=FRotationMatrix::MakeFromXZ(Finger,Normal).ToQuat();
    }
   }
  }
 }
 virtual void PreUpdate(UAnimInstance* Instance,float DeltaSeconds) override
 {
  FAnimInstanceProxy::PreUpdate(Instance,DeltaSeconds);
  if (auto* Character=Cast<Amsc_vrCharacter>(Instance->GetOwningActor()))
  {
   Copy.SourceMeshComponent=Instance->GetSkelMeshComponent()==Character->GetRemoteWorldMesh() ? Character->GetMesh() : Character->GetFirstPersonMesh();
   // CopyPose's game-thread snapshot is required for this native graph as well.
   Copy.PreUpdate(Instance);
   Torso.Alpha=Character->HasHeldCargo() ? Character->GetCarryBlend() : 0.f;
   Torso.Rotation=Character->GetCarryTorsoLean().Rotator();
   for (int I=0; I<2; ++I)
   {
    Arms[I].Alpha=Hands[I].Alpha=Character->GetCarryBlend();
    Arms[I].EffectorLocation=Character->GetCarryHandLocation(I);
    Arms[I].JointTargetLocation=Character->GetCarryElbowLocation(I);
    Hands[I].Rotation=(Character->GetHandFacing(I)*PalmBasis[I].Inverse()).Rotator();
   }
  }
 }
};

FAnimInstanceProxy* UWarehouseCarryAnimInstance::CreateAnimInstanceProxy() { return new FWarehouseCarryProxy(this); }
void UWarehouseCarryAnimInstance::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) { delete Proxy; }
