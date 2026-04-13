// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFAssetUserData.h"
#include "SWBFConfigAssetUserData.generated.h"

class UWorld;

/** SkyInfo entry from a Skydome config's SkyInfo scope. */
USTRUCT(BlueprintType)
struct FSWBFSkyInfo
{
	GENERATED_BODY()

	/** SkyInfo name (the field's string value). */
	UPROPERTY(EditAnywhere, Category = "SWBF Import")
	FString Name;

	/** Sub-scope fields from this SkyInfo entry. */
	UPROPERTY(EditAnywhere, Category = "SWBF Import")
	TMap<FString, FString> Fields;
};

/** A single SWBF config entry with its type, name, and top-level fields. */
USTRUCT(BlueprintType)
struct FSWBFConfigEntry
{
	GENERATED_BODY()

	/** Human-readable EConfigType name (e.g., "Lighting", "Skydome", "Sound"). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FString ConfigTypeName;

	/** Config's own name resolved via FNV::Lookup, or hex hash if unresolved. */
	UPROPERTY()
	FString ConfigName;

	/** Top-level config fields (no recursive sub-scope traversal). */
	UPROPERTY(EditAnywhere, Category = "SWBF Import")
	TMap<FString, FString> ConfigFields;
};

/**
 * Aggregated config metadata for the SWBF import pipeline.
 * Stores all config data (any EConfigType) on the WorldSettings actor as a single
 * AssetUserData instance, since AddAssetUserData replaces per-class.
 */
UCLASS()
class ZEROEDITORTOOLS_API USWBFConfigAssetUserData : public USWBFAssetUserData
{
	GENERATED_BODY()

public:
	/** All config entries from the imported LVL. */
	UPROPERTY(EditAnywhere, Category = "SWBF Import", meta = (TitleProperty = "ConfigTypeName"))
	TArray<FSWBFConfigEntry> Configs;

	/** SkyInfo entries extracted from Skydome configs. */
	UPROPERTY(EditAnywhere, Category = "SWBF Import", meta = (TitleProperty = "Name"))
	TArray<FSWBFSkyInfo> SkyInfos;

	/**
	 * Creates (or retrieves existing) USWBFConfigAssetUserData on WorldSettings,
	 * then adds a config entry. Returns the AssetUserData instance.
	 */
	static USWBFConfigAssetUserData* AddConfigToWorldSettings(
		UWorld* World,
		const FString& InConfigTypeName,
		const FString& InConfigName,
		const TMap<FString, FString>& InConfigFields,
		const FString& InLevelName);
};
