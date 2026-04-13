// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFTextureImporter.h"
#include "SWBFLevelWrapper.h"
#include "SWBFNameSanitizer.h"
#include "ZeroEditorTools.h"
#include "ZeroEditorToolsSettings.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"

THIRD_PARTY_INCLUDES_START
#include <LibSWBF2/Wrappers/Level.h>
#include <LibSWBF2/Wrappers/Texture.h>
#include <LibSWBF2/Types/LibString.h>
#include <LibSWBF2/Types/List.h>
#include <LibSWBF2/Types/Enums.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "SWBFTextureImporter"

// ---------------------------------------------------------------------------
// FSWBFTextureImporter::ShouldImport
// ---------------------------------------------------------------------------

bool FSWBFTextureImporter::ShouldImport() const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	return Settings && Settings->bImportTextures && GetWorkCount() > 0;
}

// ---------------------------------------------------------------------------
// FSWBFTextureImporter::GetResolvedDestPath
// ---------------------------------------------------------------------------

FString FSWBFTextureImporter::GetResolvedDestPath(const FString& LevelName) const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	if (!Settings)
	{
		return FString();
	}
	return SubstituteLevelName(Settings->TextureDestinationPath.Path, LevelName);
}

// ---------------------------------------------------------------------------
// FSWBFTextureData constructor
// ---------------------------------------------------------------------------

FSWBFTextureData::FSWBFTextureData(const LibSWBF2::Wrappers::Texture& InTexture)
{
	LibSWBF2::Types::String TexName = InTexture.GetName();
	Name = FString(ANSI_TO_TCHAR(TexName.Buffer()));

	uint16_t W = 0, H = 0;
	const uint8_t* DataPtr = nullptr;

	if (InTexture.GetImageData(LibSWBF2::ETextureFormat::R8_G8_B8_A8, 0, W, H, DataPtr))
	{
		Width = static_cast<uint16>(W);
		Height = static_cast<uint16>(H);
		const int32 DataSize = static_cast<int32>(W) * static_cast<int32>(H) * 4;
		PixelData.SetNumUninitialized(DataSize);
		FMemory::Memcpy(PixelData.GetData(), DataPtr, DataSize);
		bValid = true;
	}
	else
	{
		bValid = false;
	}
}

// ---------------------------------------------------------------------------
// FSWBFTextureData::ToString
// ---------------------------------------------------------------------------

FString FSWBFTextureData::ToString() const
{
	return FString::Printf(
		TEXT("[Texture] '%s' width=%d height=%d pixels=%d valid=%d"),
		*Name, Width, Height, PixelData.Num() / 4, bValid ? 1 : 0);
}

// ---------------------------------------------------------------------------
// GetData
// ---------------------------------------------------------------------------

void FSWBFTextureImporter::GetData(const FSWBFLevelWrapper& LevelWrapper)
{
	const auto* LevelPtr = LevelWrapper.GetLevel();
	if (!LevelPtr)
	{
		return;
	}

	const auto& Textures = LevelPtr->GetTextures();
	const size_t Count = Textures.Size();
	TextureDataArray.Reserve(static_cast<int32>(Count));

	for (size_t i = 0; i < Count; ++i)
	{
		FSWBFTextureData TexData(Textures[i]);

		if (TexData.bValid)
		{
			UE_LOG(LogZeroEditorTools, Verbose,
				TEXT("[Texture] '%s' %dx%d (%d bytes)"),
				*TexData.Name, TexData.Width, TexData.Height, TexData.PixelData.Num());
		}
		else
		{
			UE_LOG(LogZeroEditorTools, Warning, TEXT("Failed to extract texture data: %s"), *TexData.Name);
		}

		TextureDataArray.Add(MoveTemp(TexData));
	}
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

int32 FSWBFTextureImporter::Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context)
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	const FString DestPath = SubstituteLevelName(Settings->TextureDestinationPath.Path, Context.LevelName);
	int32 SuccessCount = 0;

	for (int32 i = 0; i < TextureDataArray.Num(); ++i)
	{
		if (SlowTask.ShouldCancel())
		{
			break;
		}

		const FSWBFTextureData& TexData = TextureDataArray[i];
		const FString SanitizedName = FSWBFNameSanitizer::SanitizeTextureName(TexData.Name, Context.LevelName);

		SlowTask.EnterProgressFrame(1.f,
			FText::Format(LOCTEXT("ImportTex", "Importing texture {0}/{1}: {2}"),
				FText::AsNumber(i + 1),
				FText::AsNumber(TextureDataArray.Num()),
				FText::FromString(SanitizedName)));

		if (!TexData.bValid)
		{
			UE_LOG(LogZeroEditorTools, Warning, TEXT("Skipping invalid texture: %s"), *TexData.Name);
			++Context.ErrorCount;
			continue;
		}

		// ------------------------------------------------------------------
		// Reuse check: skip or overwrite existing asset when enabled
		// ------------------------------------------------------------------
		if (Settings && Settings->bReuseExistingAssets)
		{
			FAssetData ExistingData;
			if (FindExistingAssetBySourceName(TexData.Name, UTexture2D::StaticClass()->GetClassPathName(), ExistingData))
			{
				UTexture2D* Existing = Cast<UTexture2D>(ExistingData.GetAsset());
				if (Existing)
				{
					if (Settings->bOverwriteExistingAssets)
					{
						// Overwrite in-place: reinitialize source with new pixel data
						Existing->Source.Init(TexData.Width, TexData.Height, 1, 1, TSF_BGRA8);

						uint8* MipData = Existing->Source.LockMip(0);
						const uint8* Src = TexData.PixelData.GetData();
						const int32 NumPixels = static_cast<int32>(TexData.Width) * static_cast<int32>(TexData.Height);
						for (int32 pi = 0; pi < NumPixels; ++pi)
						{
							MipData[pi * 4 + 0] = Src[pi * 4 + 2]; // B <- R
							MipData[pi * 4 + 1] = Src[pi * 4 + 1]; // G
							MipData[pi * 4 + 2] = Src[pi * 4 + 0]; // R <- B
							MipData[pi * 4 + 3] = Src[pi * 4 + 3]; // A
						}
						Existing->Source.UnlockMip(0);

						Existing->UpdateResource();
						Existing->PostEditChange();
						Existing->MarkPackageDirty();
						FAssetRegistryModule::AssetCreated(Existing);

						Context.AddAsset<UTexture2D>(TexData.Name, Existing);
						++Context.ReuseStats.Overwritten;

						UE_LOG(LogZeroEditorTools, Display,
							TEXT("Overwrote existing texture: %s"), *TexData.Name);
					}
					else
					{
						UE_LOG(LogZeroEditorTools, Display,
							TEXT("Reusing existing texture: %s"), *TexData.Name);
						Context.AddAsset<UTexture2D>(TexData.Name, Existing);
						++Context.ReuseStats.Reused;
					}

					++SuccessCount;
					if (!Context.FirstCreatedAsset)
					{
						Context.FirstCreatedAsset = Existing;
					}
					continue; // skip normal creation
				}
			}
		}

		const FString PackagePath = FString::Printf(TEXT("%s/%s"), *DestPath, *SanitizedName);
		UTexture2D* CreatedTexture = CreateTextureAsset(TexData, PackagePath, SanitizedName, Context.LevelName, Context.SourceFilePath);

		if (CreatedTexture)
		{
			++SuccessCount;
			Context.AddAsset<UTexture2D>(TexData.Name, CreatedTexture);
			++Context.ReuseStats.Created;
			if (!Context.FirstCreatedAsset)
			{
				Context.FirstCreatedAsset = CreatedTexture;
			}
		}
		else
		{
			++Context.ErrorCount;
			UE_LOG(LogZeroEditorTools, Warning, TEXT("Failed to create texture asset: %s"), *SanitizedName);
		}
	}

	if (SuccessCount > 0)
	{
		Context.WrittenFolders.AddUnique(DestPath);
	}

	UE_LOG(LogZeroEditorTools, Log, TEXT("Imported %d/%d textures to %s"),
		SuccessCount, TextureDataArray.Num(), *DestPath);
	return SuccessCount;
}

// ---------------------------------------------------------------------------
// CreateTextureAsset
// ---------------------------------------------------------------------------

UTexture2D* FSWBFTextureImporter::CreateTextureAsset(
	const FSWBFTextureData& TexData,
	const FString& PackagePath,
	const FString& AssetName,
	const FString& LevelName,
	const FString& SourceFilePath)
{
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Failed to create package: %s"), *PackagePath);
		return nullptr;
	}
	Package->FullyLoad();

	UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!Texture)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Failed to create UTexture2D: %s"), *AssetName);
		return nullptr;
	}

	// Initialize source art with BGRA8 format
	Texture->Source.Init(TexData.Width, TexData.Height, 1, 1, TSF_BGRA8);

	// Lock mip 0 and copy pixel data with RGBA -> BGRA channel swizzle
	uint8* MipData = Texture->Source.LockMip(0);
	const uint8* Src = TexData.PixelData.GetData();
	const int32 NumPixels = static_cast<int32>(TexData.Width) * static_cast<int32>(TexData.Height);

	for (int32 i = 0; i < NumPixels; ++i)
	{
		MipData[i * 4 + 0] = Src[i * 4 + 2]; // B <- R
		MipData[i * 4 + 1] = Src[i * 4 + 1]; // G
		MipData[i * 4 + 2] = Src[i * 4 + 0]; // R <- B
		MipData[i * 4 + 3] = Src[i * 4 + 3]; // A
	}
	Texture->Source.UnlockMip(0);

	// Configure texture settings
	Texture->CompressionSettings = TC_Default;
	Texture->MipGenSettings = TMGS_FromTextureGroup;
	Texture->SRGB = true;
	Texture->UpdateResource();
	Texture->PostEditChange();

	FinalizeAsset(Texture, TexData.Name, LevelName, SourceFilePath);

	return Texture;
}

#undef LOCTEXT_NAMESPACE
