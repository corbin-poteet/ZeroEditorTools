// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZeroEditorToolsSettings.h"

namespace
{
	/**
	 * Normalize a destination path field in-place:
	 * 1. Empty (after trim) -> reset to DefaultValue
	 * 2. Starts with "/All/" -> strip to "/Game/..." equivalent
	 * 3. Does not start with "/Game/" -> prepend "/Game/"
	 * 4. Strip trailing slashes
	 */
	void NormalizeDestPath(FDirectoryPath& InOutPath, const TCHAR* DefaultValue)
	{
		FString& Path = InOutPath.Path;
		Path = Path.TrimStartAndEnd();

		if (Path.IsEmpty())
		{
			Path = DefaultValue;
			return;
		}

		// Strip "/All/" prefix (turns "/All/Game/..." into "/Game/...")
		if (Path.StartsWith(TEXT("/All/")))
		{
			Path = Path.Mid(4); // removes "/All" (4 chars), leaving "/Game/..."
		}

		// Ensure path starts with "/Game/"
		if (!Path.StartsWith(TEXT("/Game/")))
		{
			// Remove leading "/" if present to avoid double-slash
			if (Path.StartsWith(TEXT("/")))
			{
				Path = Path.Mid(1);
			}
			Path = TEXT("/Game/") + Path;
		}

		// Strip trailing slashes
		while (Path.EndsWith(TEXT("/")))
		{
			Path = Path.LeftChop(1);
		}
	}
} // namespace

UZeroEditorToolsSettings::UZeroEditorToolsSettings()
	// Terrain Import
	: bRecreateTerrainNormals(true)
	, TerrainImportMode(ETerrainImportMode::Landscape)
	, HeightmapResizeMode(EHeightmapResizeMode::Resample)
	, LandscapeTextureTiling(0.0f)
	, bUseLandscapeLayerFunction(true)
	// Asset Import
	, bReuseExistingAssets(true)
	, bOverwriteExistingAssets(false)
	// Asset Import | Meshes
	, bImportMeshes(true)
	, MeshDestinationPath(FDirectoryPath{TEXT("/Game/SWBF/{LevelName}/Meshes")})
	// Asset Import | Textures
	, bImportTextures(true)
	, TextureDestinationPath(FDirectoryPath{TEXT("/Game/SWBF/{LevelName}/Textures")})
	// Asset Import | Materials
	, bImportMaterials(true)
	, MaterialDestinationPath(FDirectoryPath{TEXT("/Game/SWBF/{LevelName}/Materials")})
	// Asset Import | Terrain
	, bImportTerrain(true)
	, TerrainDestinationPath(FDirectoryPath{TEXT("/Game/SWBF/{LevelName}/Terrain")})
	// World Import (migrated)
	, bImportBarriers(false)
	, bImportLighting(true)
	, bImportSkydome(true)
	// World Import (new)
	, bImportRegions(true)
	, bImportHintNodes(true)
	, bImportWorldLayout(true)
	, bImportConfig(true)
{
}

const UZeroEditorToolsSettings* UZeroEditorToolsSettings::Get()
{
	return GetDefault<UZeroEditorToolsSettings>();
}

void UZeroEditorToolsSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Normalize all 4 destination path fields on every property change.
	// Normalization is idempotent so running all 4 unconditionally is safe and simple.
	NormalizeDestPath(MeshDestinationPath,     TEXT("/Game/SWBF/{LevelName}/Meshes"));
	NormalizeDestPath(TextureDestinationPath,  TEXT("/Game/SWBF/{LevelName}/Textures"));
	NormalizeDestPath(MaterialDestinationPath, TEXT("/Game/SWBF/{LevelName}/Materials"));
	NormalizeDestPath(TerrainDestinationPath,  TEXT("/Game/SWBF/{LevelName}/Terrain"));
}