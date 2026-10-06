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

/**
 * Test vehicle: the five-wheel orange AGV (original model + drive-steer unit) on the force-driven drive. Drive-steer
 * wheel in the middle, casters at the rear corners, fixed load rollers in the fork legs; the meshes follow the drive.
 */
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

	/** The model's two low safety laser scanners at the body-end corners (+Y, -Y). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV")
	TObjectPtr<UAgvLidarComponent> ScannerL;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV")
	TObjectPtr<UAgvLidarComponent> ScannerR;

	/** Fork-side 3D obstacle sensors in the crossbar lenses (+Y behind the mast, -Y beside it). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV")
	TObjectPtr<UAgvLidarComponent> ForkSensorL;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV")
	TObjectPtr<UAgvLidarComponent> ForkSensorR;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV")
	TObjectPtr<UAgvSafetyComponent> Safety;

private:
	/**
	 * Local X of the drive-steer axis: centred under the body on the fork side, near the mast (the layout the user
	 * confirmed); as far forward as its steering circle clears the fork legs (from x 17, |y| 21).
	 */
	static constexpr double DriveWheelX = 5.0;

	/** Steer and roll the wheel meshes to the drive's actual state. */
	void UpdateWheelMeshes();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Chassis;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> DriveSteer;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> DriveWheel;

	/** Rear casters and fork-leg load rollers, in the order of the drive's PassiveWheels. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> RearWheelL;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> RearWheelR;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> LoadRollerL;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> LoadRollerR;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> LiftStage;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> LiftCarriage;
};
