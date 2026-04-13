// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFImporterBase.h"

class UTexture2D;
namespace LibSWBF2 { namespace Wrappers { class Texture; } }

/**
 * Holds extracted texture pixel data from a SWBF LVL file.
 * Pixel data is copied from LibSWBF2 into a TArray so it survives after the Level is closed.
 */
struct FSWBFTextureData
{
	/** Original SWBF texture name (unsanitized). */
	FString Name;

	/** Texture width in pixels. */
	uint16 Width = 0;

	/** Texture height in pixels. */
	uint16 Height = 0;

	/** Raw pixel data in R8G8B8A8 format, copied from LibSWBF2. */
	TArray<uint8> PixelData;

	/** Whether the texture data was successfully extracted. */
	bool bValid = false;

	/** Default constructor. */
	FSWBFTextureData() = default;

	/** Construct from LibSWBF2 wrapper, populating all fields. */
	explicit FSWBFTextureData(const LibSWBF2::Wrappers::Texture& InTexture);

	/** Returns a compact one-line summary for debug logging. */
	FString ToString() const;
};

/**
 * Imports textures from a SWBF LVL file into the UE Content Browser as UTexture2D assets.
 * Handles RGBA->BGRA channel swizzle, name sanitization, and deduplication by overwrite.
 */
class FSWBFTextureImporter : public FSWBFImporterBase
{
public:
	virtual FString GetDisplayName() const override { return TEXT("textures"); }
	virtual FString GetDestSubfolder() const override { return TEXT("Textures"); }
	virtual FString GetResolvedDestPath(const FString& LevelName) const override;
	virtual bool ShouldImport() const override;
	virtual void GetData(const FSWBFLevelWrapper& LevelWrapper) override;
	virtual int32 GetWorkCount() const override { return TextureDataArray.Num(); }
	virtual int32 Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context) override;

private:
	TArray<FSWBFTextureData> TextureDataArray;

	static UTexture2D* CreateTextureAsset(
		const FSWBFTextureData& TexData,
		const FString& PackagePath,
		const FString& AssetName,
		const FString& LevelName,
		const FString& SourceFilePath);
};
