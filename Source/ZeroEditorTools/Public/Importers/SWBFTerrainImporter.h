// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFImporterBase.h"
#include "ZeroEditorToolsSettings.h"
#include "LandscapeProxy.h"

struct FMeshDescription;
class ALandscape;
class UMaterial;
class UMaterialInterface;
class UStaticMesh;
class UMaterialInstanceConstant;
class ULandscapeLayerInfoObject;
namespace LibSWBF2 { namespace Wrappers { class Terrain; } }

/**
 * Holds extracted terrain geometry data from a SWBF world LVL file.
 * All spatial data is coordinate-converted to UE space.
 * Terrain is a single continuous mesh (no segments) with vertex colors.
 */
struct FSWBFTerrainData
{
	/** Terrain name from LibSWBF2 Terrain::GetName(). */
	FString Name;

	/** SWBF World this terrain belongs to (for DataLayer assignment). Empty if not world-scoped. */
	FString WorldName;

	/** Vertex positions converted to UE coordinate space. */
	TArray<FVector> Vertices;

	/** Vertex normals converted to UE coordinate space. */
	TArray<FVector> Normals;

	/** Per-vertex UVs from LibSWBF2 (per-patch, 0-2 range). */
	TArray<FVector2D> UVs;

	/** Vertex colors from Color4u8 (baked lighting/AO data). */
	TArray<FColor> VertexColors;

	/** Triangle list indices (already uint32 from Terrain API, winding order fixed). */
	TArray<uint32> Indices;

	// --- Landscape data ---

	/** Heightmap as normalized floats (0.0-1.0), dim x dim grid. */
	TArray<float> HeightmapData;
	uint32 HeightmapDim = 0;
	uint32 HeightmapDimScale = 0;

	/** Blend map weights per layer, dim x dim x LayerCount interleaved. */
	TArray<uint8> BlendMapData;
	uint32 BlendMapDim = 0;
	uint32 BlendLayerCount = 0;

	/** Height bounds from terrain metadata. */
	float HeightFloor = 0.f;
	float HeightCeiling = 0.f;

	/** All layer texture names. Index 0 is the dominant layer. */
	TArray<FString> LayerTextureNames;

	/** Whether the terrain data was successfully extracted. */
	bool bValid = false;

	/** Default constructor. */
	FSWBFTerrainData() = default;

	/** Construct from LibSWBF2 wrapper, populating all fields except WorldName. */
	explicit FSWBFTerrainData(const LibSWBF2::Wrappers::Terrain& InTerrain);

	/** Returns a compact one-line summary for debug logging. */
	FString ToString() const;
};

/** Result of computing a valid UE landscape dimension from a source heightmap size. */
struct FLandscapeConfigResult
{
	int32 VertsPerAxis = 0;
	int32 NumComponents = 1;
	int32 NumSubsections = 1;
	int32 SubsectionSizeQuads = 63;
};

/**
 * Imports terrain data from a SWBF LVL file as UStaticMesh assets with
 * terrain material instances and placed AStaticMeshActors.
 * Only runs for world-type LVL files.
 */
class FSWBFTerrainImporter : public FSWBFImporterBase
{
public:
	virtual FString GetDisplayName() const override { return TEXT("terrains"); }
	virtual FString GetDestSubfolder() const override { return TEXT("Terrain"); }
	virtual FString GetResolvedDestPath(const FString& LevelName) const override;
	virtual bool ShouldImport() const override;
	virtual void GetData(const FSWBFLevelWrapper& LevelWrapper) override;
	virtual int32 GetWorkCount() const override { return TerrainDataArray.Num(); }
	virtual int32 Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context) override;
	virtual void CollectWorldNames(TSet<FString>& OutWorldNames) const override;

private:
	TArray<FSWBFTerrainData> TerrainDataArray;

	static FMeshDescription BuildTerrainMeshDescription(const FSWBFTerrainData& TerrainData);

	static UStaticMesh* CreateTerrainMeshAsset(
		const FSWBFTerrainData& TerrainData,
		const FString& PackagePath,
		const FString& AssetName,
		UMaterialInstanceConstant* TerrainMIC,
		const FString& LevelName,
		const FString& SourceFilePath);

	static UMaterialInstanceConstant* CreateTerrainMaterial(
		const FString& DominantTextureName,
		const FString& MaterialDestPath,
		const FString& TextureDestPath,
		const FString& AssetName,
		const FString& LevelName,
		const FString& SourceFilePath);

	static AActor* SpawnTerrainActor(UStaticMesh* TerrainMesh, const FString& ActorLabel, int32 TerrainIndex);

	/** Create an ALandscape actor from terrain heightmap data. Returns the spawned landscape or nullptr on failure. */
	static ALandscape* CreateLandscapeActor(const FSWBFTerrainData& TerrainData, const FString& LevelName, int32 TerrainIndex);

	// --- Landscape helper methods ---

	/** Replace sentinel heightmap values (0xf0f0f0f0) with neighbor-interpolated values. Returns count replaced. */
	static int32 CleanSentinelValues(TArray<float>& HeightmapData, uint32 Dim);

	/** Convert float heightmap to uint16 [0,65535], remapping the actual data range to maximize precision.
	 *  OutActualMin/Max return the detected range so the caller can adjust Z scale/position. */
	static TArray<uint16> ConvertHeightmapToUint16(const TArray<float>& FloatData, uint32 Dim,
		float& OutActualMin, float& OutActualMax);

	/** Find the smallest valid UE landscape dimension >= SourceDim. */
	static FLandscapeConfigResult ComputeValidLandscapeDimension(uint32 SourceDim);

	/** Resize a uint16 heightmap from SourceDim to TargetDim using the specified mode. */
	static TArray<uint16> ResizeHeightmap(const TArray<uint16>& SourceData, uint32 SourceDim, uint32 TargetDim, EHeightmapResizeMode Mode);

	/** Compute the landscape actor scale from terrain metadata. */
	static FVector ComputeLandscapeScale(const FSWBFTerrainData& TerrainData);

	/** Create a landscape material with LandscapeLayerBlend and a material instance. Returns nullptr if no layers. */
	static UMaterialInterface* CreateLandscapeMaterial(
		const FSWBFTerrainData& TerrainData,
		const FString& MaterialDestPath,
		const FString& TextureDestPath,
		const FString& LevelName,
		const FString& SourceFilePath,
		int32 TerrainIndex);

	/** Build FLandscapeImportLayerInfo array with ULandscapeLayerInfoObject and resampled blend weights.
	 *  Creates layer info asset packages under LayerInfoDestPath. Returns empty array if no blend data. */
	static TArray<FLandscapeImportLayerInfo> BuildLayerInfoAndWeights(
		const FSWBFTerrainData& TerrainData,
		const FString& LayerInfoDestPath,
		const FString& LevelName,
		int32 TargetDim);

	/** Resample a single-channel uint8 weight map from SourceDim to TargetDim using bilinear interpolation. */
	static TArray<uint8> ResampleWeightLayer(const TArray<uint8>& SourceData, uint32 SourceDim, uint32 TargetDim);

};
