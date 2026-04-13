// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFImporterBase.h"

namespace LibSWBF2 { namespace Wrappers { class Config; } }

/** Extracted SkyInfo entry from a Skydome config. */
struct FSWBFSkyInfoData
{
	FString Name;
	TMap<FString, FString> Fields;

	/** Parsed fog parameters for spawning ExponentialHeightFog. */
	bool bEnable = false;
	FLinearColor FogColor = FLinearColor::White;
	FVector2D FogRange = FVector2D::ZeroVector;
	FVector4 NearSceneRange = FVector4(0, 0, 0, 0);
	FVector2D FarSceneRange = FVector2D::ZeroVector;
};

/**
 * Holds extracted data for a single SWBF config entry.
 */
struct FSWBFConfigData
{
	/** Human-readable EConfigType name (e.g., "Lighting", "Sound"). */
	FString ConfigTypeName;

	/** Config's own name resolved from FNV hash, or hex fallback. */
	FString ConfigName;

	/** Top-level config fields. */
	TMap<FString, FString> Fields;

	/** SkyInfo entries (only populated for Skydome configs). */
	TArray<FSWBFSkyInfoData> SkyInfos;

	/** Whether the config data was successfully extracted. */
	bool bValid = false;

	/** Default constructor. */
	FSWBFConfigData() = default;

	/** Construct from LibSWBF2 wrapper, populating all fields. */
	explicit FSWBFConfigData(const LibSWBF2::Wrappers::Config& InConfig);

	/** Returns a compact one-line summary for debug logging. */
	FString ToString() const;
};

/**
 * Extracts all SWBF configs from a level (EConfigType::All) and attaches one
 * USWBFConfigAssetUserData per config to the WorldSettings root component.
 * Preserves Lighting, Skydome, Effect, Boundary, Path, Combo, Music, FoleyFX,
 * Sound, TriggerSoundRegion, and HUD configs as inspectable editor metadata.
 */
class FSWBFConfigImporter : public FSWBFImporterBase
{
public:
	virtual FString GetDisplayName() const override { return TEXT("configs"); }
	virtual FString GetDestSubfolder() const override { return FString(); }
	virtual void GetData(const FSWBFLevelWrapper& LevelWrapper) override;
	virtual int32 GetWorkCount() const override { return ConfigData.Num(); }
	virtual bool ShouldImport() const override;
	virtual int32 Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context) override;

private:
	TArray<FSWBFConfigData> ConfigData;
};
