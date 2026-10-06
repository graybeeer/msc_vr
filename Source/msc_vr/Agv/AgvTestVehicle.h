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
 * Test vehicle: the teammate's three-wheel orange AGV on the force-driven drive. Drive-steer wheel under the rear body,
 * fixed support wheels beside the fork heels (nothing under the forks, so they reach the warehouse pallets and rack
 * beams). The meshes follow the drive.
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

	/**
	 * The teammate's forklift (WarehouseForklift, main): the refined model scaled uniformly to 215 cm high, actor origin at
	 * the fork heel face, forks along +X (VNSL14 115 x 18 x 6 cm, centres 50 cm apart). The warehouse pallets and racks
	 * are laid out for it. Refined source cm (origin at the floor centre, heel at x 34) -> vehicle cm: Refined().
	 */
	static constexpr double ModelScale = 215.0 / 282.55;
	static FVector Refined(double X, double Y, double Z) { return FVector((X - 34.0) * ModelScale, Y * ModelScale, Z * ModelScale); }

	/** Drive-steer axis and fixed support wheels as on the teammate's model (vehicle cm). */
	static constexpr double DriveWheelX = -56.3;
	static constexpr double SupportWheelX = -5.3;
	static constexpr double SupportWheelY = 45.0;

	/** Rated load (pallet + cargo), agreed 2026-10-06 with no counterweight (see FORKLIFT_NAVIGATION.md). */
	static constexpr double RatedLoadKg = 250.0;

private:
	/** Steer and roll the wheel meshes to the drive's actual state. */
	void UpdateWheelMeshes();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Chassis;

	/** Vertical steer axis of the drive unit; the unit's meshes hang off it. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SteerPivot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> DriveWheel;

	/** Support wheels on local +Y / -Y, in the order of the drive's PassiveWheels. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SupportWheelL;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SupportWheelR;
};
