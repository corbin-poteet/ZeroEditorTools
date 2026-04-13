// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFBarrierActor.h"
#include "NavArea_SWBFBarrier.h"
#include "Components/BoxComponent.h"
#include "NavModifierComponent.h"

ASWBFBarrierActor::ASWBFBarrierActor()
{
	// Box component as root - defines barrier bounds
	BoxComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BarrierBounds"));
	SetRootComponent(BoxComponent);

	// QueryOnly collision - needed for UNavModifierComponent to discover bounds
	// No physical blocking per user decision (barriers affect AI pathfinding only)
	BoxComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	BoxComponent->SetCollisionResponseToAllChannels(ECR_Overlap);
	BoxComponent->SetCanEverAffectNavigation(true);
	BoxComponent->ShapeColor = FColor(255, 64, 64);  // Red wireframe
	BoxComponent->SetHiddenInGame(true);

	// Nav modifier component (non-scene, discovers collision-enabled primitives on owner)
	NavModifier = CreateDefaultSubobject<UNavModifierComponent>(TEXT("NavModifier"));
	NavModifier->AreaClass = UNavArea_SWBFBarrier::StaticClass();
}
