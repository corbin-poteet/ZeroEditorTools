// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ZeroEditorToolsSettings.generated.h"

/** Controls how heightmap dimensions are adjusted to valid UE landscape sizes. */
UENUM()
enum class EHeightmapResizeMode : uint8
{
	Resample  UMETA(DisplayName = "Resample (Bilinear)"),
	PadCrop   UMETA(DisplayName = "Pad / Crop")
};

/** Controls how SWBF terrain is imported into the UE project. */
UENUM()
enum class ETerrainImportMode : uint8
{
	None       UMETA(DisplayName = "None (Skip Terrain)"),
	Landscape  UMETA(DisplayName = "Landscape"),
	StaticMesh UMETA(DisplayName = "Static Mesh"),
	Both       UMETA(DisplayName = "Both (Landscape + Static Mesh)")
};

/**
 * Editor settings for ZeroEditorTools plugin.
 * Accessible via Editor Settings > Plugins > ZeroEditorTools.
 */
UCLASS(Config=EditorPerProjectUserSettings, DefaultConfig, meta=(DisplayName="Zero Engine Tools"))
class UZeroEditorToolsSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UZeroEditorToolsSettings();

	/** Get the singleton settings object. */
	static const UZeroEditorToolsSettings* Get();

	//~ Begin UDeveloperSettings
	virtual FName GetContainerName() const override { return TEXT("Editor"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("ZeroEditorTools"); }
	//~ End UDeveloperSettings

	//~ Begin UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject
	
	// -------------------------------------------------------------------------
	// Asset Import
	// -------------------------------------------------------------------------

	UPROPERTY(Config, EditAnywhere, Category="Asset Import",
		meta=(DisplayName="Reuse Existing Assets",
			  ToolTip="Search for existing assets before importing. When found, reuses the existing asset instead of reimporting."))
	bool bReuseExistingAssets;

	UPROPERTY(Config, EditAnywhere, Category="Asset Import",
		meta=(DisplayName="Overwrite Existing Assets",
			  ToolTip="When reuse is enabled and an existing asset is found, overwrite it with a fresh import instead of reusing it."))
	bool bOverwriteExistingAssets;

	// Asset Import | Meshes

	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Meshes",
		meta=(DisplayName="Import Meshes",
			  ToolTip="Import static meshes from LVL files."))
	bool bImportMeshes;

	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Meshes",
		meta=(DisplayName="Mesh Destination Path", LongPackageName,
			  ToolTip="Default destination folder for imported meshes.",
			  EditCondition="bImportMeshes"))
	FDirectoryPath MeshDestinationPath;

	// Asset Import | Textures

	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Textures",
		meta=(DisplayName="Import Textures",
			  ToolTip="Import textures from LVL files."))
	bool bImportTextures;

	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Textures",
		meta=(DisplayName="Texture Destination Path", LongPackageName,
			  ToolTip="Default destination folder for imported textures.",
			  EditCondition="bImportTextures"))
	FDirectoryPath TextureDestinationPath;

	// Asset Import | Materials

	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Materials",
		meta=(DisplayName="Import Materials",
			  ToolTip="Import materials from LVL files."))
	bool bImportMaterials;

	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Materials",
		meta=(DisplayName="Material Destination Path", LongPackageName,
			  ToolTip="Default destination folder for imported materials.",
			  EditCondition="bImportMaterials"))
	FDirectoryPath MaterialDestinationPath;

	/** Default EmissiveStrength scalar for Glow/Energy flagged materials. */
	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Materials",
		meta=(DisplayName="Default Emissive Strength",
			  ToolTip="Default EmissiveStrength scalar applied to Glow/Energy materials on import. 5.0 triggers visible bloom without whiteout.",
			  ClampMin="0.0", ClampMax="50.0"))
	float DefaultEmissiveStrength = 5.0f;

	// Asset Import | Terrain

	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Terrain",
		meta=(DisplayName="Import Terrain",
			  ToolTip="Import terrain from LVL files."))
	bool bImportTerrain;

	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Terrain",
		meta=(DisplayName="Terrain Destination Path", LongPackageName,
			  ToolTip="Default destination folder for imported terrain assets.",
			  EditCondition="bImportTerrain"))
	FDirectoryPath TerrainDestinationPath;

	/**
	 * When true, landscape layers use the MF_SWBF_LandscapeLayer material function
	 * for micro/far tiling and variation effects. When false, layers use simple
	 * TextureSample nodes with LandscapeLayerCoords UVs.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Terrain",
		meta=(DisplayName="Use Landscape Layer Material Function",
			  ToolTip="When enabled, landscape layers use MF_SWBF_LandscapeLayer for tiling variation effects. When disabled, layers use simple texture sampling."))
	bool bUseLandscapeLayerFunction;

	/**
	 * When true, terrain normals are recalculated from geometry during import
	 * (matching the Unity SWBF plugin behavior). When false, normals from
	 * LibSWBF2's terrain data are imported as-is.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Terrain", meta=(DisplayName="Recalculate Terrain Normals"))
	bool bRecreateTerrainNormals;

	/**
	 * Controls how SWBF terrain is imported. Landscape creates a UE Landscape actor.
	 * Static Mesh uses the legacy mesh-based import.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Terrain",
		meta=(DisplayName="Terrain Import Mode",
			  ToolTip="Controls how SWBF terrain is imported. Landscape creates a UE Landscape actor. Static Mesh uses the legacy mesh-based import."))
	ETerrainImportMode TerrainImportMode;

	/**
	 * Controls how SWBF heightmap dimensions are adjusted to valid UE landscape sizes.
	 * Resample uses bilinear interpolation. Pad/Crop extends edges or trims excess.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Terrain",
		meta=(DisplayName="Heightmap Resize Mode",
			  ToolTip="Controls how SWBF heightmap dimensions are adjusted to valid UE landscape sizes. Resample uses bilinear interpolation. Pad/Crop extends edges or trims excess."))
	EHeightmapResizeMode HeightmapResizeMode;

	/** Default UV tiling scale for landscape layer textures.
	 *  Controls how many times textures repeat across the terrain.
	 *  Lower values = more stretched, higher = more tiled.
	 *  Set to 0 to auto-derive from SWBF terrain dimensions on import. */
	UPROPERTY(Config, EditAnywhere, Category="Asset Import|Terrain",
		meta=(DisplayName="Landscape Texture Tiling",
			  ToolTip="UV mapping scale for landscape layer textures. Set to 0 to auto-derive from terrain size. Lower = more stretched, higher = more tiled."))
	float LandscapeTextureTiling;
	// -------------------------------------------------------------------------
	// World Import
	// -------------------------------------------------------------------------

	/**
	 * When true, SWBF barrier data is imported as ASWBFBarrierActor with NavModifier
	 * components. Barrier positioning and navigation integration are still being refined.
	 */
	UPROPERTY(Config, EditAnywhere, Category="World Import",
		meta=(DisplayName="Import Barriers (Experimental)",
			  ToolTip="Import SWBF barrier data as navigation barrier actors. Experimental: barrier positioning and navmesh integration may not be fully accurate."))
	bool bImportBarriers;

	/**
	 * When true, SWBF lighting data is imported as directional, point, spot,
	 * and sky light actors from EConfigType::Lighting configs.
	 */
	UPROPERTY(Config, EditAnywhere, Category="World Import",
		meta=(DisplayName="Import Lighting",
			  ToolTip="Import SWBF lighting data as directional, point, spot, and sky light actors."))
	bool bImportLighting;

	/**
	 * When true, SWBF skydome data is imported as static mesh actors for dome geometry
	 * and sky objects (sun/moon) from EConfigType::Skydome configs.
	 */
	UPROPERTY(Config, EditAnywhere, Category="World Import",
		meta=(DisplayName="Import Skydome",
			  ToolTip="Import SWBF skydome dome geometry and sky objects as static mesh actors."))
	bool bImportSkydome;

	UPROPERTY(Config, EditAnywhere, Category="World Import",
		meta=(DisplayName="Import Regions",
			  ToolTip="Import SWBF region volumes from LVL files."))
	bool bImportRegions;

	UPROPERTY(Config, EditAnywhere, Category="World Import",
		meta=(DisplayName="Import Hint Nodes",
			  ToolTip="Import SWBF hint node markers from LVL files."))
	bool bImportHintNodes;

	UPROPERTY(Config, EditAnywhere, Category="World Import",
		meta=(DisplayName="Import World Layout",
			  ToolTip="Import SWBF world instance placement from LVL files."))
	bool bImportWorldLayout;

	UPROPERTY(Config, EditAnywhere, Category="World Import",
		meta=(DisplayName="Import Config",
			  ToolTip="Import SWBF configuration data from LVL files."))
	bool bImportConfig;

};