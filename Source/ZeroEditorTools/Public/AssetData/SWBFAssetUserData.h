// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "SWBFAssetUserData.generated.h"

/** Shared tag key constants for Asset Registry queries and tag emission. */
namespace SWBFTagKeys
{
	static constexpr TCHAR SourceName[]       = TEXT("SourceName");
	static constexpr TCHAR SourceLevel[]      = TEXT("SourceLevel");
	static constexpr TCHAR SourceWorld[]      = TEXT("SourceWorld");
	static constexpr TCHAR MaterialDedupKey[] = TEXT("MaterialDedupKey");
}

/**
 * Metadata attached to imported SWBF assets.
 * Stores origin information (source name, level, file path) and exposes
 * SourceName / SourceLevel as Asset Registry tags for Content Browser columns.
 */
UCLASS()
class ZEROEDITORTOOLS_API USWBFAssetUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	USWBFAssetUserData();

	/** Original asset name from the SWBF LVL file */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FString SWBFName;

	/** SWBF level this asset was imported from */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FString LevelName;

	/** SWBF World this actor was imported from (maps to DataLayer) */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FString WorldName;

	/** Adds SourceName and SourceLevel tags to the Asset Registry */
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

	/**
	 * Creates and attaches a USWBFAssetUserData instance to the given asset.
	 * Returns nullptr if the asset does not implement IInterface_AssetUserData.
	 */
	static USWBFAssetUserData* AttachToAsset(UObject* Asset, const FString& InSWBFName, const FString& InLevelName, const FString& InWorldName = FString());
};
