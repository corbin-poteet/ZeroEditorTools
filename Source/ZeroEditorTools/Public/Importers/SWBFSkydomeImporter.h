// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFImporterBase.h"

/**
 * Holds extracted dome model data from a SWBF skydome config.
 * GeometryName is lowercased for Context.GetAsset<UStaticMesh> lookup compatibility.
 */
struct FSWBFDomeModelData
{
	/** Raw SWBF geometry name (lowercased for Context.GetAsset lookup). */
	FString GeometryName;

	/** SWBF World this dome model belongs to (for DataLayer assignment). */
	FString WorldName;

	/** Sky config name this dome model was extracted from (for logging). */
	FString SkyName;

	/** Whether the dome model data was successfully extracted. */
	bool bValid = false;
};

/**
 * Holds extracted sky object data from a SWBF skydome config.
 * GeometryName is lowercased for Context.GetAsset<UStaticMesh> lookup compatibility.
 */
struct FSWBFSkyObjectData
{
	/** Raw SWBF geometry name (lowercased for Context.GetAsset lookup). */
	FString GeometryName;

	/** SWBF World this sky object belongs to (for DataLayer assignment). */
	FString WorldName;

	/** Vertical height in UE centimeters (Height.X * UnitScale, converted in GetData). */
	float Height = 0.0f;

	/** Whether the sky object data was successfully extracted. */
	bool bValid = false;
};

/**
 * Extracts and imports skydome data from SWBF Config API (EConfigType::Skydome).
 * Handles dome model geometry (surrounding domes) and sky objects (sun/moon).
 * Deduplicates by sky name to prevent duplicate extraction across worlds.
 */
class FSWBFSkydomeImporter : public FSWBFImporterBase
{
public:
	virtual FString GetDisplayName() const override { return TEXT("skydome actors"); }
	virtual FString GetDestSubfolder() const override { return FString(); }
	virtual void GetData(const FSWBFLevelWrapper& LevelWrapper) override;
	virtual int32 GetWorkCount() const override { return DomeModelData.Num() + SkyObjectData.Num(); }
	virtual bool ShouldImport() const override;
	virtual int32 Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context) override;
	virtual void CollectWorldNames(TSet<FString>& OutWorldNames) const override;

private:
	TArray<FSWBFDomeModelData> DomeModelData;
	TArray<FSWBFSkyObjectData> SkyObjectData;

	/** Tracks sky names already extracted to prevent duplicate processing across worlds. */
	TSet<FString> SeenSkyNames;
};
