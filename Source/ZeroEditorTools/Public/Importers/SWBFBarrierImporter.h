// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFImporterBase.h"

namespace LibSWBF2 { namespace Wrappers { class Barrier; } }

/**
 * Holds extracted barrier data from a SWBF world.
 * Position, rotation, and size are coordinate-converted to UE space.
 */
struct FSWBFBarrierData
{
	/** Barrier name from SWBF data. */
	FString Name;

	/** SWBF World this datum belongs to (for DataLayer assignment). */
	FString WorldName;

	/** World position converted to UE coordinate space. */
	FVector Position = FVector::ZeroVector;

	/** Rotation converted to UE coordinate space. */
	FQuat Rotation = FQuat::Identity;

	/** Half-extents in UE units (for UBoxComponent). */
	FVector Size = FVector::ZeroVector;

	/** SWBF barrier flag bitmask (6-bit AI type filter). */
	int32 Flag = 0;

	/** Whether the barrier data was successfully extracted. */
	bool bValid = false;

	/** Default constructor. */
	FSWBFBarrierData() = default;

	/** Construct from LibSWBF2 wrapper, populating all fields. */
	explicit FSWBFBarrierData(const LibSWBF2::Wrappers::Barrier& InBarrier, const FString& InWorldName);

	/** Returns a compact one-line summary for debug logging. */
	FString ToString() const;
};

/**
 * Spawns barrier navigation volumes in the editor level from SWBF barrier data.
 * Uses ASWBFBarrierActor with UBoxComponent + UNavModifierComponent for navmesh modification.
 * Barriers affect AI pathfinding (non-navigable areas), not physical collision.
 * Only runs for world-type LVL files.
 */
class FSWBFBarrierImporter : public FSWBFImporterBase
{
public:
	virtual FString GetDisplayName() const override { return TEXT("barriers"); }
	virtual FString GetDestSubfolder() const override { return FString(); }
	virtual void GetData(const FSWBFLevelWrapper& LevelWrapper) override;
	virtual int32 GetWorkCount() const override { return BarrierData.Num(); }
	virtual bool ShouldImport() const override;
	virtual int32 Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context) override;
	virtual void CollectWorldNames(TSet<FString>& OutWorldNames) const override;

private:
	TArray<FSWBFBarrierData> BarrierData;
};