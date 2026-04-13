// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NavAreas/NavArea.h"
#include "NavArea_SWBFBarrier.generated.h"

/**
 * Non-navigable NavArea for SWBF barrier volumes.
 * Marks areas as completely non-traversable (FLT_MAX cost, no area flags).
 * Follows the UNavArea_Null pattern from UE engine source.
 */
UCLASS(Config=Engine)
class UNavArea_SWBFBarrier : public UNavArea
{
	GENERATED_UCLASS_BODY()
};
