// Copyright Epic Games, Inc. All Rights Reserved.


#include "msc_vrCameraManager.h"
#include "msc_vrCharacter.h"
#include "msc_vrPlayerController.h"
#include "WarehouseForklift.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

Amsc_vrCameraManager::Amsc_vrCameraManager()
{
	// set the min/max pitch
	ViewPitchMin = -70.0f;
	ViewPitchMax = 80.0f;
}

void Amsc_vrCameraManager::AimRemoteCamera(float Yaw,float Pitch)
{
 RemoteYaw=FMath::Clamp(RemoteYaw+Yaw*2.f,-80.f,80.f);
 RemotePitch=FMath::Clamp(RemotePitch-Pitch*2.f,-50.f,-5.f);
}

void Amsc_vrCameraManager::UpdateViewTarget(FTViewTarget& OutVT,float DeltaTime)
{
 const auto* Character=PCOwner ? Cast<Amsc_vrCharacter>(PCOwner->GetPawn()) : nullptr;
 auto* Vehicle=Character ? Character->GetRemoteForklift() : nullptr;
 if (!IsValid(Vehicle) || OutVT.Target!=Vehicle)
 { Super::UpdateViewTarget(OutVT,DeltaTime);return; }
 // Keep possession and the physical operator on the ground. Only the viewer
 // follows the selected vehicle, so first-person arms/phone do not cover it.
 const FVector Aim=Vehicle->GetActorLocation()+FVector(0,0,145);
 const FRotator Heading(RemotePitch,Vehicle->GetActorRotation().Yaw+RemoteYaw,0);
 FVector Eye=Aim-Heading.Vector()*480.f;
 FCollisionQueryParams Params(SCENE_QUERY_STAT(RemoteCameraBoom),false,Vehicle);
 Params.AddIgnoredActor(Character);
 Params.AddIgnoredActors(Vehicle->GetPhysicalLoads());
 FHitResult Hit;
 if (GetWorld()->SweepSingleByChannel(Hit,Aim,Eye,FQuat::Identity,ECC_Visibility,FCollisionShape::MakeSphere(12.f),Params))
  Eye=Hit.Location+Hit.Normal*2.f;
 OutVT.POV=FMinimalViewInfo();
 OutVT.POV.Location=Eye;
 OutVT.POV.Rotation=(Aim-Eye).Rotation();
 const auto* Controller=Cast<Amsc_vrPlayerController>(PCOwner);
 OutVT.POV.FOV=Controller ? FMath::Clamp(Controller->ViewFOV,70.f,110.f) : 90.f;
 OutVT.POV.bUseFirstPersonParameters=false;
}
