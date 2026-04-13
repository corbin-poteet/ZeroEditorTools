// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SWBFBarrierActor.generated.h"

class UBoxComponent;
class UNavModifierComponent;

/** SWBF barrier AI-type flags (6-bit bitmask). */
UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ESWBFBarrierFlag : uint8
{
	Soldier   = 1 << 0,
	Hover     = 1 << 1,
	Small     = 1 << 2,
	Medium    = 1 << 3,
	Huge      = 1 << 4,
	Flyer     = 1 << 5,
};
ENUM_CLASS_FLAGS(ESWBFBarrierFlag)

/**
 * Barrier actor with navigation modification.
 * Uses UBoxComponent for shape/bounds and UNavModifierComponent to mark
 * the area as non-navigable on the navmesh via UNavArea_SWBFBarrier.
 * Does NOT use physical blocking collision -- navigation only.
 */
UCLASS()
class ASWBFBarrierActor : public AActor
{
	GENERATED_BODY()

public:
	ASWBFBarrierActor();

	/** Box extent defining the barrier bounds for navmesh modification. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWBF")
	TObjectPtr<UBoxComponent> BoxComponent;

	/** Navigation modifier component - applies NavArea to the box bounds. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Navigation")
	TObjectPtr<UNavModifierComponent> NavModifier;

	/** Original SWBF barrier flag bitmask (6-bit AI type filter). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SWBF", meta = (Bitmask, BitmaskEnum = "/Script/ZeroEditorTools.ESWBFBarrierFlag"))
	int32 SWBFBarrierFlag = 0;
};
