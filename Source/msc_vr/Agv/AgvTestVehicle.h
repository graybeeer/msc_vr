#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AgvTestVehicle.generated.h"

class UAgvDynamicDriveComponent;
class UAgvLidarComponent;
class UAgvLidarLocalizerComponent;
class UAgvSafetyComponent;
class UAgvNavigatorComponent;
class UStaticMeshComponent;

/** Test vehicle on the refined orange AGV model with the force-driven tricycle drive; the wheel meshes follow the drive state. */
UCLASS()
class MSC_VR_API AAgvTestVehicle : public AActor
{
	GENERATED_BODY()

public:
	AAgvTestVehicle();
	virtual void Tick(float DeltaSeconds) override;

	/** One fixed cycle: navigation decides, then the drive moves. Also used by headless verification. */
	UFUNCTION(BlueprintCallable, Category="AGV")
	void StepSimulation(float Dt);

	/** Steps until navigation stops being active or MaxSeconds pass; returns the simulated seconds. */
	UFUNCTION(BlueprintCallable, Category="AGV")
	float SimulateUntilIdle(float MaxSeconds, float Dt = 0.0166667f);

	/** Places the reference point at a world position with the given vehicle yaw, at rest. */
	UFUNCTION(BlueprintCallable, Category="AGV")
	void TeleportReference(FVector2D Position, float YawDeg);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV")
	TObjectPtr<UAgvNavigatorComponent> Navigator;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV")
	TObjectPtr<UAgvDynamicDriveComponent> Drive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV")
	TObjectPtr<UAgvLidarLocalizerComponent> Localizer;

	/** 3D LiDAR on top of the sensor tower (localization). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV")
	TObjectPtr<UAgvLidarComponent> TopLidar;

	/** 2D safety laser scanner low in the body front (assumed built in; not in the mesh). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV")
	TObjectPtr<UAgvLidarComponent> FrontScanner;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV")
	TObjectPtr<UAgvSafetyComponent> Safety;

private:
	/** Steer and roll the wheel meshes to the drive's actual state. */
	void UpdateWheelMeshes();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Chassis;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> DriveSteer;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> DriveWheel;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SupportWheelL;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SupportWheelR;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> MastInner;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> LiftCarriage;
};
