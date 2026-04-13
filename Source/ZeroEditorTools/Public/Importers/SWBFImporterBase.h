// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class FSWBFLevelWrapper;
class UDataLayerInstance;
struct FScopedSlowTask;

/** Accumulated counts of created, reused, and overwritten assets during an import. */
struct FSWBFImportReuseCounts
{
	int32 Created     = 0;
	int32 Reused      = 0;
	int32 Overwritten = 0;
};

/**
 * Shared context passed through the import pipeline.
 * Holds destination paths, cross-importer asset lookups, and result tracking.
 */
struct FSWBFImportContext
{
	/** Level name derived from filename (e.g., COR1). */
	FString LevelName;

	/** Full filesystem path to the source .lvl file. */
	FString SourceFilePath;

	/** Folders that received assets during import; used for Content Browser navigation. */
	TArray<FString> WrittenFolders;

	/** Store an imported asset for cross-importer lookup, keyed by type + name. */
	template<typename T>
	void AddAsset(const FString& Name, T* Asset)
	{
		AssetLookup.Add(MakeAssetKey<T>(Name), Asset);
	}

	/** Retrieve a previously stored asset by type and name. Returns nullptr if not found or type mismatch. */
	template<typename T>
	T* GetAsset(const FString& Name) const
	{
		if (UObject* const* Found = AssetLookup.Find(MakeAssetKey<T>(Name)))
		{
			return Cast<T>(*Found);
		}
		return nullptr;
	}

	/** First asset created during import (returned by factory). */
	UObject* FirstCreatedAsset = nullptr;

	/** Accumulated error count across all importers. */
	int32 ErrorCount = 0;

	/** Whether World Partition is enabled on the target level (DataLayers require WP). */
	bool bWorldPartitionEnabled = false;

	/** Number of SWBF Worlds in the LVL (for toast notification). */
	int32 WorldCount = 0;

	/** DataLayer lookup: SWBF World name -> UDataLayerInstance. Only populated when WP is enabled. */
	TMap<FString, UDataLayerInstance*> DataLayerLookup;

	/** Collect unique World names from all actor-data importers for DataLayer creation. */
	TSet<FString> UniqueWorldNames;

	/** Name of the first (base) SWBF World — actors in this world go straight into the level, no DataLayer. */
	FString BaseWorldName;

	/** Accumulated asset reuse counts across all importers. */
	FSWBFImportReuseCounts ReuseStats;

private:
	template<typename T>
	static FString MakeAssetKey(const FString& Name)
	{
		return FString::Printf(TEXT("%s::%s"), *T::StaticClass()->GetName(), *Name);
	}

	/** Type-namespaced asset lookup shared across importers. */
	TMap<FString, UObject*> AssetLookup;
};

/**
 * Base class for all SWBF asset importers.
 * Each subclass extracts one type of data from the LVL file and creates UE assets.
 * The factory loops through an array of importers calling GetData -> Import in order.
 */
class FSWBFImporterBase
{
public:
	virtual ~FSWBFImporterBase() = default;

	/** Plural display name for progress UI and toast messages (e.g., "textures", "meshes"). */
	virtual FString GetDisplayName() const = 0;

	/** Content Browser subfolder under the settings-configured base path (e.g., "Textures"). Empty for non-asset importers. */
	virtual FString GetDestSubfolder() const = 0;

	/** Returns the resolved destination path for Content Browser navigation.
	 *  Empty string for world-only importers that create no Content Browser assets. */
	virtual FString GetResolvedDestPath(const FString& LevelName) const { return FString(); }

	/** Extract data from the level wrapper into internal storage. Called once before Import. */
	virtual void GetData(const FSWBFLevelWrapper& LevelWrapper) = 0;

	/** Number of items to process, used for FScopedSlowTask total work calculation. */
	virtual int32 GetWorkCount() const = 0;

	/** Whether this importer should run. Default: true if there are items to process. */
	virtual bool ShouldImport() const { return GetWorkCount() > 0; }

	/**
	 * Import all extracted data, advancing SlowTask progress per item.
	 * @return Number of successfully imported items.
	 */
	virtual int32 Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context) = 0;

	/** Collect unique World names from extracted data. Default: no-op (asset importers). */
	virtual void CollectWorldNames(TSet<FString>& OutWorldNames) const {}

protected:
	/** Substitute {LevelName} placeholder in a path template. */
	static FString SubstituteLevelName(const FString& PathTemplate, const FString& LevelName)
	{
		return PathTemplate.Replace(TEXT("{LevelName}"), *LevelName);
	}

	/**
	 * Common finalization for imported assets:
	 * 1. Attaches USWBFAssetUserData (SWBFName + LevelName) -- skipped if bSkipMetadata is true
	 * 2. Sets AssetImportData source file path
	 * 3. Marks package dirty and registers with Asset Registry
	 *
	 * @param bSkipMetadata If true, skips USWBFAssetUserData attachment (caller attached a subclass instead).
	 */
	static void FinalizeAsset(UObject* Asset, const FString& SWBFName, const FString& LevelName, const FString& SourceFilePath, bool bSkipMetadata = false);

	/**
	 * Query the Asset Registry for an asset with a matching SourceName tag and the given class.
	 * @return true if a matching asset was found; OutAssetData is populated on success.
	 */
	static bool FindExistingAssetBySourceName(
		const FString& SourceName,
		const FTopLevelAssetPath& ClassPath,
		FAssetData& OutAssetData);

	/**
	 * Query the Asset Registry for a material asset with a matching MaterialDedupKey tag.
	 * @return true if a matching asset was found; OutAssetData is populated on success.
	 */
	static bool FindExistingAssetByDedupKey(
		const FString& DedupKey,
		const FTopLevelAssetPath& ClassPath,
		FAssetData& OutAssetData);
};
