#include "AgvRouteNode.h"
#include "Components/ArrowComponent.h"
#include "Components/TextRenderComponent.h"
#include "DrawDebugHelpers.h"

AAgvRouteNode::AAgvRouteNode()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Arrow = CreateDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
	Arrow->SetupAttachment(RootComponent);
	Arrow->SetRelativeLocation(FVector(0, 0, 5));
	Arrow->ArrowSize = 0.6f;
	Arrow->ArrowColor = FColor::Cyan;

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(RootComponent);
	Label->SetRelativeLocation(FVector(0, 0, 40));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(24);
	Label->SetTextRenderColor(FColor::Cyan);
}

void AAgvRouteNode::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Label->SetText(FText::FromName(NodeId));
}

void AAgvRouteNode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bDrawLinks)
	{
		return;
	}
	const FVector Lift(0, 0, 3);
	for (const AAgvRouteNode* Link : Links)
	{
		if (Link)
		{
			DrawDebugLine(GetWorld(), GetActorLocation() + Lift, Link->GetActorLocation() + Lift, FColor::Cyan, false, -1.f, 0, 2.f);
		}
	}
}
