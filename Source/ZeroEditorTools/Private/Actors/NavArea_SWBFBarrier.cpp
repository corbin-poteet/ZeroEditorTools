// Copyright Epic Games, Inc. All Rights Reserved.

#include "NavArea_SWBFBarrier.h"

UNavArea_SWBFBarrier::UNavArea_SWBFBarrier(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultCost = FLT_MAX;
	AreaFlags = 0;
	DrawColor = FColor(255, 64, 64);  // Red - matches barrier visualization convention
}
