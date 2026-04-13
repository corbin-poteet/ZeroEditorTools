// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFImporterBase.h"

namespace LibSWBF2 { namespace Wrappers { class Region; } }

/**
 * Holds extracted region data from a SWBF world.
 * Position, rotation, and size are coordinate-converted to UE space.
 */
struct FSWBFRegionData
{
	/** Region name from SWBF data. */
	FString Name;

	/** SWBF World this datum belongs to (for DataLayer assignment). */
	FString WorldName;

	/** World position converted to UE coordinate space. */
	FVector Position = FVector::ZeroVector;

	/** Rotation converted to UE coordinate space. */
	FQuat Rotation = FQuat::Identity;

	/** Region type string: "box", "sphere", or "cylinder". */
	FString Type;

	/** Converted half-extents in UE units (for box regions, used with UBoxComponent). */
	FVector Size = FVector::ZeroVector;

	/** Radius in UE units (for sphere/cylinder regions). */
	float Radius = 0.0f;

	/** Half-height in UE units (for cylinder regions, used with UCapsuleComponent). */
	float HalfHeight = 0.0f;

	/** SWBF properties as hash+value pairs (game mode metadata, etc). */
	TArray<TPair<uint32, FString>> Properties;

	/** Semantic class of this region (e.g. "soundspace", "deathregion", "generic").
	 *  NOT the geometry type (box/sphere/cylinder) -- that is Type/RegionType.
	 *  Set to "generic" in Phase 29; Phase 30 parser sets the real value. */
	FString RegionClass;

	/** Raw inline parameter string following the class token (e.g. "damagerate=60 personscale=1").
	 *  Transient intermediate for Phase 30 parser; not stored on UserData. */
	FString InlineParamsRaw;

	/** Parsed inline key=value parameters. Empty in Phase 29; Phase 30 populates. */
	TMap<FString, FString> InlineParams;

	/** Whether the region data was successfully extracted. */
	bool bValid = false;

	/** Default constructor. */
	FSWBFRegionData() = default;

	/** Construct from LibSWBF2 wrapper, populating all fields. */
	explicit FSWBFRegionData(const LibSWBF2::Wrappers::Region& InRegion, const FString& InWorldName);

	/** Returns a compact one-line summary for debug logging. */
	FString ToString() const;
};

/**
 * Spawns region trigger volumes in the editor level from SWBF region data.
 * Supports box (UBoxComponent), sphere (USphereComponent), and cylinder (UCapsuleComponent).
 * All regions use NoCollision, wireframe visualization, and floating name+type labels.
 * Only runs for world-type LVL files.
 */
class FSWBFRegionImporter : public FSWBFImporterBase
{
public:
	virtual FString GetDisplayName() const override { return TEXT("regions"); }
	virtual FString GetDestSubfolder() const override { return FString(); }
	virtual bool ShouldImport() const override;
	virtual void GetData(const FSWBFLevelWrapper& LevelWrapper) override;
	virtual int32 GetWorkCount() const override { return RegionData.Num(); }
	virtual int32 Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context) override;
	virtual void CollectWorldNames(TSet<FString>& OutWorldNames) const override;

private:
	TArray<FSWBFRegionData> RegionData;

	/** Spawns a generic trigger volume actor from the given region data.
	 *  Returns the spawned AActor, or nullptr on failure. */
	AActor* SpawnGenericRegionActor(const FSWBFRegionData& Data, UWorld* World, FSWBFImportContext& Context) const;

	/** Spawns an AAudioVolume for soundspace regions.
	 *  Falls back to SpawnGenericRegionActor on failure. */
	AActor* SpawnSoundSpaceVolume(const FSWBFRegionData& Data, UWorld* World, FSWBFImportContext& Context, TMap<FString, int32>& ClassCounters) const;

	/** Spawns an AKillZVolume for deathregion regions.
	 *  Falls back to SpawnGenericRegionActor on failure. */
	AActor* SpawnDeathRegionVolume(const FSWBFRegionData& Data, UWorld* World, FSWBFImportContext& Context, TMap<FString, int32>& ClassCounters) const;

	/** Spawns an APainCausingVolume for damageregion regions.
	 *  Falls back to SpawnGenericRegionActor on failure. */
	AActor* SpawnDamageRegionVolume(const FSWBFRegionData& Data, UWorld* World, FSWBFImportContext& Context, TMap<FString, int32>& ClassCounters) const;

	/** Parses each region's Name into RegionClass, InlineParamsRaw, and InlineParams.
	 *  Called at end of GetData() after all FSWBFRegionData entries are constructed. */
	void ParseRegionNames();
};
