#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AgvSafetyComponent.generated.h"

class UAgvDriveComponent;
class UAgvLidarComponent;

UENUM(BlueprintType)
enum class EAgvSafetyState : uint8 { Clear, Warning, Stop };

/**
 * Safety laser scanner evaluation, as in a safety-rated scanner's field sets. Ahead of the body front (main travel
 * direction, local -X) it keeps a protective field (object inside -> stop) and a larger warning field (-> slow).
 * The protective length is switched by speed band: response distance + braking distance + margin, never shorter
 * than the field for the starting speed or MinProtectiveLengthCm. A protective stop also asks the drive for full braking. After a stop the vehicle restarts on its own once the field has been
 * clear for RestartDelaySeconds. Acts on the drive's safety speed limit, below navigation.
 * Only covers travel with the body leading; forks-first travel needs the fork-side sensors (not yet fitted).
 */
UCLASS(ClassGroup=(Agv), meta=(BlueprintSpawnableComponent))
class MSC_VR_API UAgvSafetyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAgvSafetyComponent();

	/** Evaluates the scanner's completed scans; called by the owning vehicle after the sensors have stepped. */
	void Step(float Dt);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety")
	TObjectPtr<UAgvLidarComponent> Scanner;

	/** Local X of the body's front face and half the body width (refined model). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety")
	float BodyFrontXCm = -94.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float BodyHalfWidthCm = 61.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float SideMarginCm = 10.f;

	/** Scanner + controller + brake reaction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float ResponseSeconds = 0.2f;

	/** Braking the fields are designed for: the worst case (full load), not the best. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="1"))
	float FieldDecelerationCm = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float FieldMarginCm = 50.f;

	/** The protective field is never shorter than this, so even a slow approach stops well clear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float MinProtectiveLengthCm = 100.f;

	/** Field sets switch in these speed steps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="1"))
	float SpeedBandCm = 20.f;

	/** Smallest field: the one needed to start moving again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float StartSpeedCm = 30.f;

	/**
	 * Fixed, so slowing down does not shrink it off the object (which would make the vehicle speed up again).
	 * Must reach beyond the protective field at full speed plus the distance to slow to WarningSpeedCm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float WarningFieldLengthCm = 350.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float WarningExtraWidthCm = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float WarningSpeedCm = 30.f;

	/** Beams that must hit inside a field in one scan (object resolution; ignores single noisy returns). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="1"))
	int32 MinObjectPoints = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float RestartDelaySeconds = 2.f;

	/** The warning field must stay clear this long before full speed is allowed again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Safety", meta=(ClampMin="0"))
	float WarningReleaseSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Debug")
	bool bDrawFields = true;

	// Status.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	EAgvSafetyState State = EAgvSafetyState::Clear;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	FString StatusText = TEXT("CLEAR");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	float ProtectiveLengthCm = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	float WarningLengthCm = 0.f;

	/** Nearest object ahead of the body front within the warning width, from the last scan (-1 = none). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	float NearestObjectCm = -1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AGV|Safety|Status")
	int32 SafetyStops = 0;

private:
	void Evaluate();
	void DrawFields() const;

	UPROPERTY(Transient)
	TObjectPtr<UAgvDriveComponent> Drive;

	TArray<FVector> ScanPoints;
	int32 SeenRevolution = 0;
	double ClearSeconds = 0.0;
	double WarningClearSeconds = 1e9; // starts released
	bool bProtectiveHit = false;
	bool bWarningHit = false;
	bool bActive = true;
};
