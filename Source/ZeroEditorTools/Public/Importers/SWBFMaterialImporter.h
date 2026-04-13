// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFImporterBase.h"

class UMaterialInstanceConstant;
namespace LibSWBF2 { namespace Wrappers { class Material; } }

/**
 * Holds extracted material properties from a SWBF LVL file segment.
 * Populated during GetData(), consumed by Import().
 */
struct FSWBFMaterialData
{
	/** Texture names per slot (index = slot number, empty string = gap). Size = highest populated slot + 1. */
	TArray<FString> TextureNames;

	/** Material flags bitmap from LibSWBF2 (EMaterialFlags). */
	uint32 Flags = 0;

	/** Diffuse color tint (RGBA, 0-255). White = no tint. */
	FColor DiffuseColor = FColor::White;

	/** Specular color (RGBA, 0-255). Black full-alpha = no specular. */
	FColor SpecularColor = FColor::Black;

	/** Specular exponent (raw SWBF uint32). */
	uint32 SpecularExponent = 0;

	/** Raw uint32 parameters from MTRL chunk. Interpretation depends on material flags:
	 *  - TiledNormalmap: low byte = tiling multiplier (uint8, 0-255)
	 *  - Scrolling: low byte = scroll speed (uint8, 0-255)
	 *  - Other: unused or flag-specific. */
	uint32 RawParam0 = 0;
	uint32 RawParam1 = 0;

	/** Attached light name. Empty when not set. */
	FString AttachedLight;

	/** True when TextureNames[0] is a real texture (not a generated name like modelname_seg0). */
	bool bHasTexture = false;

	/** True when the Doublesided flag is set (extracted from Flags for convenience). */
	bool bDoublesided = false;

	/** True when the material data was successfully extracted. */
	bool bValid = false;

	/**
	 * Generate deduplication key combining all material properties.
	 * Same key = same material instance should be reused.
	 * Both sites (SWBFMaterialImporter and SWBFMeshImporter) MUST produce identical output.
	 */
	FString GetDedupKey() const;

	/** Equality operator using bit-safe float comparison (NaN-stable). */
	bool operator==(const FSWBFMaterialData& Other) const;

	/** Default constructor. */
	FSWBFMaterialData() = default;

	/** Construct from LibSWBF2 wrapper, populating all fields. */
	explicit FSWBFMaterialData(const LibSWBF2::Wrappers::Material& InMaterial, const FString& InModelName, int32 InSegmentIndex);

	/** Returns a compact one-line summary for debug logging. */
	FString ToString() const;
};

/**
 * Creates UMaterialInstanceConstant assets from SWBF material data.
 * Uses a single master parent material (M_SWBF_MasterMaterial) with per-instance
 * base property overrides for blend mode, two-sided, and opacity mask clip.
 * Assigns textures, diffuse colors, and populates the shared MaterialLookup.
 */
class FSWBFMaterialImporter : public FSWBFImporterBase
{
public:
	virtual FString GetDisplayName() const override { return TEXT("materials"); }
	virtual FString GetDestSubfolder() const override { return TEXT("Materials"); }
	virtual FString GetResolvedDestPath(const FString& LevelName) const override;
	virtual bool ShouldImport() const override;
	virtual void GetData(const FSWBFLevelWrapper& LevelWrapper) override;
	virtual int32 GetWorkCount() const override { return MaterialDataArray.Num(); }
	virtual int32 Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context) override;

private:
	TArray<FSWBFMaterialData> MaterialDataArray;
};
