#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AgvRouteNode.generated.h"

class UArrowComponent;
class UTextRenderComponent;

/** A point of the AGV route network. Place on the lane centre line and link to its neighbours. */
UCLASS()
class MSC_VR_API AAgvRouteNode : public AActor
{
	GENERATED_BODY()

public:
	AAgvRouteNode();
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Name used by commands, e.g. "A-03". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Route")
	FName NodeId;

	/** Lanes to other nodes. A link listed on either node is driven in both directions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Route")
	TArray<TObjectPtr<AAgvRouteNode>> Links;

	/** Arc radius when a route turns here. 0 = stop and pivot in place, negative = navigator default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Route")
	float CornerRadiusCm = -1.f;

	/** When a route ends here, turn the vehicle to this actor's yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Route")
	bool bAlignOnArrival = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AGV|Route")
	bool bDrawLinks = true;

	FVector2D GetPoint() const { return FVector2D(GetActorLocation()); }

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UArrowComponent> Arrow;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> Label;
};
