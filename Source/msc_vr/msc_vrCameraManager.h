// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "msc_vrCameraManager.generated.h"

/**
 *  Basic First Person camera manager.
 *  Limits min/max look pitch.
 */
UCLASS()
class Amsc_vrCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()
	
public:

	/** Constructor */
	Amsc_vrCameraManager();
	void ResetRemoteCamera() { RemoteYaw=0.f;RemotePitch=-15.f; }
	void AimRemoteCamera(float Yaw,float Pitch);

protected:
	virtual void UpdateViewTarget(FTViewTarget& OutVT,float DeltaTime) override;
	float RemoteYaw=0.f,RemotePitch=-15.f;
};
