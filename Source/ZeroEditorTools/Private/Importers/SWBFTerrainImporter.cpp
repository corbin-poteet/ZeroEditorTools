// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFTerrainImporter.h"
#include "SWBFLevelWrapper.h"
#include "SWBFCoordinateUtils.h"
#include "SWBFNameSanitizer.h"
#include "ZeroEditorTools.h"
#include "ZeroEditorToolsSettings.h"

#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionLandscapeLayerBlend.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionLandscapeLayerCoords.h"
#include "Engine/Texture2D.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeConfigHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "DataLayer/DataLayerEditorSubsystem.h"

THIRD_PARTY_INCLUDES_START
#include <LibSWBF2/Wrappers/Level.h>
#include <LibSWBF2/Wrappers/Terrain.h>
#include <LibSWBF2/Types/LibString.h>
#include <LibSWBF2/Types/List.h>
#include <LibSWBF2/Types/Enums.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "SWBFTerrainImporter"

// ---------------------------------------------------------------------------
// FSWBFTerrainImporter::ShouldImport
// ---------------------------------------------------------------------------

bool FSWBFTerrainImporter::ShouldImport() const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	return Settings && Settings->bImportTerrain && GetWorkCount() > 0;
}

// ---------------------------------------------------------------------------
// FSWBFTerrainImporter::GetResolvedDestPath
// ---------------------------------------------------------------------------

FString FSWBFTerrainImporter::GetResolvedDestPath(const FString& LevelName) const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	if (!Settings)
	{
		return FString();
	}
	return SubstituteLevelName(Settings->TerrainDestinationPath.Path, LevelName);
}

// ---------------------------------------------------------------------------
// FSWBFTerrainData constructor
// ---------------------------------------------------------------------------

FSWBFTerrainData::FSWBFTerrainData(const LibSWBF2::Wrappers::Terrain& InTerrain)
{
	using LibSWBF2::Types::String;
	using LibSWBF2::Types::Vector2;
	using LibSWBF2::Types::Vector3;
	using LibSWBF2::Types::Color4u8;
	using LibSWBF2::ETopology;

	String TerrName = InTerrain.GetName();
	Name = FString(ANSI_TO_TCHAR(TerrName.Buffer()));

	// Vertices
	uint32_t VertCount = 0;
	Vector3* VertBuffer = nullptr;
	InTerrain.GetVertexBuffer(VertCount, VertBuffer);

	Vertices.SetNumUninitialized(static_cast<int32>(VertCount));
	for (uint32_t vi = 0; vi < VertCount; ++vi)
	{
		Vertices[vi] = FSWBFCoordinateUtils::ConvertPosition(
			VertBuffer[vi].m_X, VertBuffer[vi].m_Y, VertBuffer[vi].m_Z);
	}

	// Normals (same axis remap as mesh normals)
	uint32_t NormCount = 0;
	Vector3* NormBuffer = nullptr;
	InTerrain.GetNormalBuffer(NormCount, NormBuffer);

	Normals.SetNumUninitialized(static_cast<int32>(NormCount));
	for (uint32_t ni = 0; ni < NormCount; ++ni)
	{
		Normals[ni] = FVector(
			-NormBuffer[ni].m_X,
			-NormBuffer[ni].m_Z,
			NormBuffer[ni].m_Y);
	}

	// UVs
	uint32_t UVCount = 0;
	Vector2* UVBuffer = nullptr;
	InTerrain.GetUVBuffer(UVCount, UVBuffer);

	UVs.SetNumUninitialized(static_cast<int32>(UVCount));
	for (uint32_t ui = 0; ui < UVCount; ++ui)
	{
		UVs[ui] = FVector2D(UVBuffer[ui].m_X, UVBuffer[ui].m_Y);
	}

	// Vertex Colors
	uint32_t ColorCount = 0;
	Color4u8* ColorBuffer = nullptr;
	InTerrain.GetColorBuffer(ColorCount, ColorBuffer);

	VertexColors.SetNumUninitialized(static_cast<int32>(ColorCount));
	for (uint32_t ci = 0; ci < ColorCount; ++ci)
	{
		VertexColors[ci] = FColor(
			ColorBuffer[ci].m_Red, ColorBuffer[ci].m_Green,
			ColorBuffer[ci].m_Blue, ColorBuffer[ci].m_Alpha);
	}

	// Indices (already uint32 from Terrain API)
	uint32_t IdxCount = 0;
	uint32_t* IdxBuffer = nullptr;
	if (InTerrain.GetIndexBuffer(ETopology::TriangleList, IdxCount, IdxBuffer))
	{
		Indices.SetNumUninitialized(static_cast<int32>(IdxCount));
		FMemory::Memcpy(Indices.GetData(), IdxBuffer,
			IdxCount * sizeof(uint32_t));

		// Fix winding order after coordinate conversion (handedness flip)
		for (int32 ti = 0; ti + 2 < Indices.Num(); ti += 3)
		{
			Swap(Indices[ti + 1], Indices[ti + 2]);
		}
	}

	// Layer textures (all layers, not just dominant)
	const auto& LyrTextures = InTerrain.GetLayerTextures();
	for (size_t li = 0; li < LyrTextures.Size(); ++li)
	{
		LayerTextureNames.Add(FString(ANSI_TO_TCHAR(LyrTextures[li].Buffer())));
	}

	// Heightmap
	uint32_t HeightDim = 0;
	uint32_t HeightDimScale = 0;
	float* HeightData = nullptr;
	InTerrain.GetHeightMap(HeightDim, HeightDimScale, HeightData);
	if (HeightData && HeightDim > 0)
	{
		const int32 HeightmapSize = static_cast<int32>(HeightDim * HeightDim);
		HeightmapData.SetNumUninitialized(HeightmapSize);
		FMemory::Memcpy(HeightmapData.GetData(), HeightData,
			HeightmapSize * sizeof(float));
		HeightmapDim = HeightDim;
		HeightmapDimScale = HeightDimScale;
	}

	// Blend map
	uint32_t BlndDim = 0;
	uint32_t NumBlendLayers = 0;
	uint8_t* BlendData = nullptr;
	InTerrain.GetBlendMap(BlndDim, NumBlendLayers, BlendData);
	if (BlendData && BlndDim > 0 && NumBlendLayers > 0)
	{
		const int32 BlendMapSize = static_cast<int32>(BlndDim * BlndDim * NumBlendLayers);
		BlendMapData.SetNumUninitialized(BlendMapSize);
		FMemory::Memcpy(BlendMapData.GetData(), BlendData,
			BlendMapSize * sizeof(uint8_t));
		BlendMapDim = BlndDim;
		BlendLayerCount = NumBlendLayers;
	}

	// Height bounds
	float HFloor = 0.f;
	float HCeiling = 0.f;
	InTerrain.GetHeightBounds(HFloor, HCeiling);
	HeightFloor = HFloor;
	HeightCeiling = HCeiling;

	bValid = (VertCount > 0 && Indices.Num() > 0);
}

// ---------------------------------------------------------------------------
// FSWBFTerrainData::ToString
// ---------------------------------------------------------------------------

FString FSWBFTerrainData::ToString() const
{
	return FString::Printf(
		TEXT("[Terrain] '%s' verts=%d indices=%d heightmap=%dx%d layers=%d valid=%d"),
		*Name, Vertices.Num(), Indices.Num(),
		HeightmapDim, HeightmapDim, LayerTextureNames.Num(), bValid ? 1 : 0);
}

// ---------------------------------------------------------------------------
// GetData
// ---------------------------------------------------------------------------

void FSWBFTerrainImporter::GetData(const FSWBFLevelWrapper& LevelWrapper)
{
	if (!LevelWrapper.IsWorldLevel())
	{
		return;
	}

	const auto* LevelPtr = LevelWrapper.GetLevel();
	if (!LevelPtr)
	{
		return;
	}

	using LibSWBF2::Wrappers::Terrain;

	const auto& Terrains = LevelPtr->GetTerrains();
	if (Terrains.Size() == 0)
	{
		UE_LOG(LogZeroEditorTools, Verbose, TEXT("No terrains found in LVL"));
		return;
	}

	for (size_t i = 0; i < Terrains.Size(); ++i)
	{
		FSWBFTerrainData TerrData(Terrains[i]);

		UE_LOG(LogZeroEditorTools, Verbose,
			TEXT("[Terrain] '%s' verts=%d indices=%d normals=%d uvs=%d colors=%d heightmap=%ux%u height=[%.2f,%.2f] blendLayers=%u layerTextures=%d"),
			*TerrData.Name, TerrData.Vertices.Num(), TerrData.Indices.Num(),
			TerrData.Normals.Num(), TerrData.UVs.Num(), TerrData.VertexColors.Num(),
			TerrData.HeightmapDim, TerrData.HeightmapDim,
			TerrData.HeightFloor, TerrData.HeightCeiling,
			TerrData.BlendLayerCount, TerrData.LayerTextureNames.Num());
		for (int32 li = 0; li < TerrData.LayerTextureNames.Num(); ++li)
		{
			UE_LOG(LogZeroEditorTools, Verbose, TEXT("  [Terrain] layer[%d]='%s'"), li, *TerrData.LayerTextureNames[li]);
		}

		UE_LOG(LogZeroEditorTools, Log,
			TEXT("Terrain '%s': heightmap %ux%u, height [%.2f, %.2f], %u blend layers, %d layer textures"),
			*TerrData.Name, TerrData.HeightmapDim, TerrData.HeightmapDim,
			TerrData.HeightFloor, TerrData.HeightCeiling,
			TerrData.BlendLayerCount, TerrData.LayerTextureNames.Num());

		TerrainDataArray.Add(MoveTemp(TerrData));
	}
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

int32 FSWBFTerrainImporter::Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context)
{
	const ETerrainImportMode ImportMode = UZeroEditorToolsSettings::Get()->TerrainImportMode;
	UE_LOG(LogZeroEditorTools, Log, TEXT("Terrain import mode: %s"),
		*UEnum::GetDisplayValueAsText(ImportMode).ToString());

	if (ImportMode == ETerrainImportMode::None)
	{
		UE_LOG(LogZeroEditorTools, Log, TEXT("Terrain import skipped (mode: None)"));
		return 0;
	}

	int32 SuccessCount = 0;

	SlowTask.EnterProgressFrame(static_cast<float>(TerrainDataArray.Num()),
		FText::Format(LOCTEXT("ImportingTerrain", "Importing {0} terrain(s)..."),
			FText::AsNumber(TerrainDataArray.Num())));

	// --- Landscape creation ---
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	if (ImportMode == ETerrainImportMode::Landscape || ImportMode == ETerrainImportMode::Both)
	{
		int32 LandscapeSuccessCount = 0;
		for (int32 i = 0; i < TerrainDataArray.Num(); ++i)
		{
			const FSWBFTerrainData& TerrainData = TerrainDataArray[i];
			if (!TerrainData.bValid)
			{
				continue;
			}
			ALandscape* Landscape = CreateLandscapeActor(TerrainData, Context.LevelName, i);
			if (Landscape)
			{
				++SuccessCount;
				++LandscapeSuccessCount;
				UE_LOG(LogZeroEditorTools, Log, TEXT("Created landscape actor: %s"), *Landscape->GetActorLabel());

				// Assign landscape to DataLayer (if WP enabled) -- defensive, ALandscapeProxy may not support it
				if (Context.bWorldPartitionEnabled && !TerrainData.WorldName.IsEmpty())
				{
					if (UDataLayerInstance** FoundDL = Context.DataLayerLookup.Find(TerrainData.WorldName))
					{
						if (UDataLayerEditorSubsystem* DLSubsystem = UDataLayerEditorSubsystem::Get())
						{
							if (!DLSubsystem->AddActorToDataLayer(Landscape, *FoundDL))
							{
								UE_LOG(LogZeroEditorTools, Warning,
									TEXT("Failed to assign landscape '%s' to DataLayer '%s' (ALandscapeProxy may not support DataLayers)"),
									*Landscape->GetActorLabel(), *TerrainData.WorldName);
							}
						}
					}
				}

				// Create and assign landscape material
				const FString MatDestPath = SubstituteLevelName(Settings->MaterialDestinationPath.Path, Context.LevelName);
				const FString TexDestPath = SubstituteLevelName(Settings->TextureDestinationPath.Path, Context.LevelName);
				UMaterialInterface* LandscapeMat = CreateLandscapeMaterial(
					TerrainData, MatDestPath, TexDestPath, Context.LevelName, Context.SourceFilePath, i);
				if (LandscapeMat)
				{
					// Set landscape material directly (EditorSetLandscapeMaterial is not DLL-exported)
					Landscape->LandscapeMaterial = LandscapeMat;
					FPropertyChangedEvent MatChangedEvent(FindFieldChecked<FProperty>(Landscape->GetClass(), FName("LandscapeMaterial")));
					Landscape->PostEditChangeProperty(MatChangedEvent);
					UE_LOG(LogZeroEditorTools, Log, TEXT("Assigned landscape material: %s"), *LandscapeMat->GetName());
				}
			}
		}

		if (LandscapeSuccessCount > 0)
		{
			Context.WrittenFolders.AddUnique(SubstituteLevelName(Settings->MaterialDestinationPath.Path, Context.LevelName));
			Context.WrittenFolders.AddUnique(SubstituteLevelName(Settings->TextureDestinationPath.Path, Context.LevelName));
		}
	}

	// --- Static mesh creation ---
	const bool bCreateStaticMesh =
		(ImportMode == ETerrainImportMode::StaticMesh || ImportMode == ETerrainImportMode::Both);

	if (bCreateStaticMesh)
	{
		const FString TerrainDestPath = SubstituteLevelName(Settings->TerrainDestinationPath.Path, Context.LevelName);
		const FString MaterialDestPath = SubstituteLevelName(Settings->MaterialDestinationPath.Path, Context.LevelName);
		const FString TextureDestPath = SubstituteLevelName(Settings->TextureDestinationPath.Path, Context.LevelName);

		for (int32 i = 0; i < TerrainDataArray.Num(); ++i)
		{
			const FSWBFTerrainData& TerrainData = TerrainDataArray[i];

			if (!TerrainData.bValid)
			{
				UE_LOG(LogZeroEditorTools, Warning,
					TEXT("Skipping invalid terrain at index %d: '%s'"),
					i, *TerrainData.Name);
				continue;
			}

			const int32 TerrainNumber = i + 1;
			const FString AssetName = FString::Printf(TEXT("SM_%s_Terrain_%02d"), *Context.LevelName, TerrainNumber);
			const FString MaterialName = FString::Printf(TEXT("MI_%s_Terrain_%02d"), *Context.LevelName, TerrainNumber);

			// Create terrain material instance
			const FString DominantTextureName = TerrainData.LayerTextureNames.Num() > 0
				? TerrainData.LayerTextureNames[0] : FString();

			UMaterialInstanceConstant* TerrainMIC = CreateTerrainMaterial(
				DominantTextureName, MaterialDestPath, TextureDestPath, MaterialName, Context.LevelName, Context.SourceFilePath);

			// Create terrain mesh asset
			const FString PackagePath = FString::Printf(TEXT("%s/%s"), *TerrainDestPath, *AssetName);
			UStaticMesh* TerrainMesh = CreateTerrainMeshAsset(TerrainData, PackagePath, AssetName, TerrainMIC, Context.LevelName, Context.SourceFilePath);

			if (TerrainMesh)
			{
				// In Both mode, create the asset but do NOT spawn an actor -- landscape is the scene representation
				if (ImportMode != ETerrainImportMode::Both)
				{
					AActor* TerrainActor = SpawnTerrainActor(TerrainMesh, AssetName, i);

					// Assign terrain actor to DataLayer (if WP enabled)
					if (TerrainActor && Context.bWorldPartitionEnabled && !TerrainData.WorldName.IsEmpty())
					{
						if (UDataLayerInstance** FoundDL = Context.DataLayerLookup.Find(TerrainData.WorldName))
						{
							if (UDataLayerEditorSubsystem* DLSubsystem = UDataLayerEditorSubsystem::Get())
							{
								DLSubsystem->AddActorToDataLayer(TerrainActor, *FoundDL);
							}
						}
					}
				}
				++SuccessCount;

				UE_LOG(LogZeroEditorTools, Log,
					TEXT("Imported terrain: %s (%d verts, %d tris, material: %s)%s"),
					*AssetName, TerrainData.Vertices.Num(), TerrainData.Indices.Num() / 3,
					*MaterialName,
					ImportMode == ETerrainImportMode::Both ? TEXT(" (asset only, no actor)") : TEXT(""));
			}
			else
			{
				UE_LOG(LogZeroEditorTools, Warning, TEXT("Failed to create terrain mesh: %s"), *AssetName);
			}
		}

		UE_LOG(LogZeroEditorTools, Log, TEXT("Imported %d/%d terrains to %s"),
			SuccessCount, TerrainDataArray.Num(), *TerrainDestPath);

		if (SuccessCount > 0)
		{
			Context.WrittenFolders.AddUnique(TerrainDestPath);
			Context.WrittenFolders.AddUnique(MaterialDestPath);
			Context.WrittenFolders.AddUnique(TextureDestPath);
		}
	}

	return SuccessCount;
}

// ---------------------------------------------------------------------------
// BuildTerrainMeshDescription
// ---------------------------------------------------------------------------

FMeshDescription FSWBFTerrainImporter::BuildTerrainMeshDescription(const FSWBFTerrainData& TerrainData)
{
	FMeshDescription MeshDesc;
	FStaticMeshAttributes StaticMeshAttrs(MeshDesc);
	StaticMeshAttrs.Register();

	TVertexAttributesRef<FVector3f> VertexPositions =
		MeshDesc.VertexAttributes().GetAttributesRef<FVector3f>(MeshAttribute::Vertex::Position);
	TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals =
		MeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector3f>(MeshAttribute::VertexInstance::Normal);
	TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs =
		MeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector2f>(MeshAttribute::VertexInstance::TextureCoordinate);
	TVertexInstanceAttributesRef<FVector4f> VertexInstanceColors =
		MeshDesc.VertexInstanceAttributes().GetAttributesRef<FVector4f>(MeshAttribute::VertexInstance::Color);
	TPolygonGroupAttributesRef<FName> PolygonGroupMaterialSlotNames =
		MeshDesc.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);

	const int32 VertCount = TerrainData.Vertices.Num();
	const int32 TriCount = TerrainData.Indices.Num() / 3;

	const bool bRecalcNormals = UZeroEditorToolsSettings::Get()->bRecreateTerrainNormals;

	// If recalculating, compute smooth normals from triangle geometry
	TArray<FVector3f> RecalculatedNormals;
	if (bRecalcNormals)
	{
		RecalculatedNormals.SetNumZeroed(VertCount);

		for (int32 i = 0; i + 2 < TerrainData.Indices.Num(); i += 3)
		{
			const uint32 Idx0 = TerrainData.Indices[i];
			const uint32 Idx1 = TerrainData.Indices[i + 1];
			const uint32 Idx2 = TerrainData.Indices[i + 2];

			if (Idx0 >= static_cast<uint32>(VertCount) ||
				Idx1 >= static_cast<uint32>(VertCount) ||
				Idx2 >= static_cast<uint32>(VertCount))
			{
				continue;
			}

			const FVector3f V0(TerrainData.Vertices[Idx0]);
			const FVector3f V1(TerrainData.Vertices[Idx2]);
			const FVector3f V2(TerrainData.Vertices[Idx1]);

			const FVector3f Edge1 = V1 - V0;
			const FVector3f Edge2 = V2 - V0;
			const FVector3f FaceNormal = FVector3f::CrossProduct(Edge1, Edge2);

			RecalculatedNormals[Idx0] += FaceNormal;
			RecalculatedNormals[Idx1] += FaceNormal;
			RecalculatedNormals[Idx2] += FaceNormal;
		}

		for (int32 i = 0; i < VertCount; ++i)
		{
			if (!RecalculatedNormals[i].IsNearlyZero())
			{
				RecalculatedNormals[i].Normalize();
			}
			else
			{
				RecalculatedNormals[i] = FVector3f(0.f, 0.f, 1.f);
			}
		}

		UE_LOG(LogZeroEditorTools, Log, TEXT("Recalculated terrain normals from geometry (%d vertices)"), VertCount);
	}

	MeshDesc.ReserveNewVertices(VertCount);
	MeshDesc.ReserveNewVertexInstances(VertCount);
	MeshDesc.ReserveNewPolygons(TriCount);
	MeshDesc.ReserveNewEdges(TriCount * 3);
	MeshDesc.ReserveNewPolygonGroups(1);

	const FPolygonGroupID PolyGroupID = MeshDesc.CreatePolygonGroup();
	PolygonGroupMaterialSlotNames[PolyGroupID] = FName(TEXT("Terrain"));

	TArray<FVertexInstanceID> VertexInstanceIDs;
	VertexInstanceIDs.Reserve(VertCount);

	for (int32 i = 0; i < VertCount; ++i)
	{
		const FVertexID VertID = MeshDesc.CreateVertex();
		VertexPositions[VertID] = FVector3f(TerrainData.Vertices[i]);

		const FVertexInstanceID VertInstID = MeshDesc.CreateVertexInstance(VertID);

		if (bRecalcNormals)
		{
			VertexInstanceNormals[VertInstID] = RecalculatedNormals[i];
		}
		else if (i < TerrainData.Normals.Num())
		{
			VertexInstanceNormals[VertInstID] = FVector3f(TerrainData.Normals[i]);
		}
		else
		{
			VertexInstanceNormals[VertInstID] = FVector3f(0.f, 0.f, 1.f);
		}

		if (i < TerrainData.UVs.Num())
		{
			VertexInstanceUVs.Set(VertInstID, 0, FVector2f(TerrainData.UVs[i]));
		}
		else
		{
			VertexInstanceUVs.Set(VertInstID, 0, FVector2f(0.f, 0.f));
		}

		if (i < TerrainData.VertexColors.Num())
		{
			const FColor& C = TerrainData.VertexColors[i];
			VertexInstanceColors[VertInstID] = FVector4f(
				C.R / 255.f, C.G / 255.f, C.B / 255.f, C.A / 255.f);
		}
		else
		{
			VertexInstanceColors[VertInstID] = FVector4f(1.f, 1.f, 1.f, 1.f);
		}

		VertexInstanceIDs.Add(VertInstID);
	}

	for (int32 i = 0; i + 2 < TerrainData.Indices.Num(); i += 3)
	{
		const uint32 Idx0 = TerrainData.Indices[i];
		const uint32 Idx1 = TerrainData.Indices[i + 1];
		const uint32 Idx2 = TerrainData.Indices[i + 2];

		if (Idx0 >= static_cast<uint32>(VertexInstanceIDs.Num()) ||
			Idx1 >= static_cast<uint32>(VertexInstanceIDs.Num()) ||
			Idx2 >= static_cast<uint32>(VertexInstanceIDs.Num()))
		{
			continue;
		}

		if (Idx0 == Idx1 || Idx1 == Idx2 || Idx0 == Idx2)
		{
			continue;
		}

		TArray<FVertexInstanceID> TriVerts;
		TriVerts.Add(VertexInstanceIDs[Idx0]);
		TriVerts.Add(VertexInstanceIDs[Idx2]);
		TriVerts.Add(VertexInstanceIDs[Idx1]);

		MeshDesc.CreatePolygon(PolyGroupID, TriVerts);
	}

	return MeshDesc;
}

// ---------------------------------------------------------------------------
// CreateTerrainMeshAsset
// ---------------------------------------------------------------------------

UStaticMesh* FSWBFTerrainImporter::CreateTerrainMeshAsset(
	const FSWBFTerrainData& TerrainData,
	const FString& PackagePath,
	const FString& AssetName,
	UMaterialInstanceConstant* TerrainMIC,
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

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!StaticMesh)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Failed to create UStaticMesh: %s"), *AssetName);
		return nullptr;
	}

	StaticMesh->InitResources();
	StaticMesh->SetLightingGuid();

	FMeshDescription MeshDesc = BuildTerrainMeshDescription(TerrainData);

	StaticMesh->GetStaticMaterials().Add(
		FStaticMaterial(TerrainMIC, FName(TEXT("Terrain")), FName(TEXT("Terrain"))));

	TArray<const FMeshDescription*> MeshDescPtrs;
	MeshDescPtrs.Add(&MeshDesc);

	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	BuildParams.bBuildSimpleCollision = false;
	BuildParams.bMarkPackageDirty = true;
	StaticMesh->BuildFromMeshDescriptions(MeshDescPtrs, BuildParams);

	StaticMesh->CalculateExtendedBounds();

	StaticMesh->CreateBodySetup();
	UBodySetup* BodySetup = StaticMesh->GetBodySetup();
	if (BodySetup)
	{
		BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
	}

	FinalizeAsset(StaticMesh, AssetName, LevelName, SourceFilePath);

	return StaticMesh;
}

// ---------------------------------------------------------------------------
// CreateTerrainMaterial
// ---------------------------------------------------------------------------

UMaterialInstanceConstant* FSWBFTerrainImporter::CreateTerrainMaterial(
	const FString& DominantTextureName,
	const FString& MaterialDestPath,
	const FString& TextureDestPath,
	const FString& AssetName,
	const FString& LevelName,
	const FString& SourceFilePath)
{
	UMaterial* ParentTerrain = LoadObject<UMaterial>(nullptr,
		TEXT("/ZeroEditorTools/Materials/Terrain/M_SWBF_Terrain.M_SWBF_Terrain"));

	if (!ParentTerrain)
	{
		UE_LOG(LogZeroEditorTools, Error,
			TEXT("Failed to load terrain parent material: /ZeroEditorTools/Materials/Terrain/M_SWBF_Terrain. "
			     "Terrain material instance will not be created."));
		return nullptr;
	}

	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	if (Settings && Settings->bReuseExistingAssets)
	{
		FAssetData ExistingData;
		if (FindExistingAssetBySourceName(AssetName, UMaterialInstanceConstant::StaticClass()->GetClassPathName(), ExistingData))
		{
			UMaterialInstanceConstant* Existing = Cast<UMaterialInstanceConstant>(ExistingData.GetAsset());
			if (Existing)
			{
				if (!Settings->bOverwriteExistingAssets)
				{
					UE_LOG(LogZeroEditorTools, Log, TEXT("Reusing existing terrain MIC: %s"), *ExistingData.GetObjectPathString());
					return Existing;
				}
				// If overwriting, fall through to recreate
			}
		}
	}

	const FString PackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDestPath, *AssetName);

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Failed to create package: %s"), *PackagePath);
		return nullptr;
	}
	Package->FullyLoad();

	UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(
		Package, *AssetName, RF_Public | RF_Standalone);
	if (!MIC)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Failed to create terrain MIC: %s"), *AssetName);
		return nullptr;
	}

	MIC->SetParentEditorOnly(ParentTerrain);

	if (!DominantTextureName.IsEmpty())
	{
		const FString SanitizedTexName = FSWBFNameSanitizer::SanitizeTextureName(DominantTextureName, LevelName);
		const FString TexAssetPath = FString::Printf(TEXT("%s/%s.%s"),
			*TextureDestPath, *SanitizedTexName, *SanitizedTexName);

		UTexture2D* DominantTex = LoadObject<UTexture2D>(nullptr, *TexAssetPath);
		if (DominantTex)
		{
			MIC->SetTextureParameterValueEditorOnly(
				FMaterialParameterInfo(TEXT("DiffuseTexture")), DominantTex);
		}
		else
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Dominant layer texture not found for terrain material '%s': %s -- using defaults"),
				*AssetName, *TexAssetPath);
		}
	}

	MIC->SetScalarParameterValueEditorOnly(
		FMaterialParameterInfo(TEXT("TilingScale")), 1.0f);

	MIC->PostEditChange();

	const FString TerrainMaterialName = FString::Printf(TEXT("%s_Material"), *AssetName);
	FinalizeAsset(MIC, TerrainMaterialName, LevelName, SourceFilePath);

	return MIC;
}

// ---------------------------------------------------------------------------
// SpawnTerrainActor
// ---------------------------------------------------------------------------

AActor* FSWBFTerrainImporter::SpawnTerrainActor(UStaticMesh* TerrainMesh, const FString& ActorLabel, int32 TerrainIndex)
{
	if (GEditor == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("GEditor is null, cannot spawn terrain actor"));
		return nullptr;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Editor world is null, cannot spawn terrain actor"));
		return nullptr;
	}

	AStaticMeshActor* TerrainActor = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(), FTransform::Identity);

	if (TerrainActor)
	{
		TerrainActor->GetStaticMeshComponent()->SetStaticMesh(TerrainMesh);
		TerrainActor->SetActorLabel(ActorLabel);
	}

	return TerrainActor;
}

// ---------------------------------------------------------------------------
// CreateLandscapeActor
// ---------------------------------------------------------------------------

ALandscape* FSWBFTerrainImporter::CreateLandscapeActor(
	const FSWBFTerrainData& TerrainData, const FString& LevelName, int32 TerrainIndex)
{
	// 1. Validate input
	if (TerrainData.HeightmapData.Num() == 0 || TerrainData.HeightmapDim == 0)
	{
		UE_LOG(LogZeroEditorTools, Warning,
			TEXT("Terrain '%s': missing or zero-dimension heightmap, skipping landscape creation"),
			*TerrainData.Name);
		return nullptr;
	}

	// 2. Clean sentinels
	TArray<float> LocalHeightmap = TerrainData.HeightmapData;
	const int32 SentinelCount = CleanSentinelValues(LocalHeightmap, TerrainData.HeightmapDim);
	if (SentinelCount > 0)
	{
		UE_LOG(LogZeroEditorTools, Log,
			TEXT("Terrain '%s': replaced %d sentinel heightmap values via neighbor interpolation"),
			*TerrainData.Name, SentinelCount);
	}

	// 3. Convert to uint16 (remaps actual data range to full uint16 for max precision)
	float ActualMin, ActualMax;
	TArray<uint16> HeightmapU16 = ConvertHeightmapToUint16(LocalHeightmap, TerrainData.HeightmapDim,
		ActualMin, ActualMax);
	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Terrain '%s': heightmap data range [%.4f, %.4f] (nominal [0,1])"),
		*TerrainData.Name, ActualMin, ActualMax);

	// 4. Compute valid dimensions
	FLandscapeConfigResult Config = ComputeValidLandscapeDimension(TerrainData.HeightmapDim);
	if (Config.VertsPerAxis == 0)
	{
		UE_LOG(LogZeroEditorTools, Warning,
			TEXT("Terrain '%s': failed to compute valid landscape dimension from %u"),
			*TerrainData.Name, TerrainData.HeightmapDim);
		return nullptr;
	}

	// 5. Resize if needed
	const EHeightmapResizeMode ResizeMode = UZeroEditorToolsSettings::Get()->HeightmapResizeMode;
	TArray<uint16> FinalHeightmap = ResizeHeightmap(
		HeightmapU16, TerrainData.HeightmapDim, Config.VertsPerAxis, ResizeMode);

	// 5b. Flip heightmap 180 degrees -- SWBF stores data in opposite winding order to UE
	Algo::Reverse(FinalHeightmap);

	const TCHAR* ResizeModeStr = (ResizeMode == EHeightmapResizeMode::Resample)
		? TEXT("Resample") : TEXT("PadCrop");
	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Terrain '%s': heightmap %ux%u -> %dx%d (%s), config: %d components, %d subsections, %d quads"),
		*TerrainData.Name, TerrainData.HeightmapDim, TerrainData.HeightmapDim,
		Config.VertsPerAxis, Config.VertsPerAxis, ResizeModeStr,
		Config.NumComponents, Config.NumSubsections, Config.SubsectionSizeQuads);

	// 6. Spawn ALandscape
	if (GEditor == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("GEditor is null, cannot create landscape actor"));
		return nullptr;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Editor world is null, cannot create landscape actor"));
		return nullptr;
	}

	ALandscape* Landscape = World->SpawnActor<ALandscape>(ALandscape::StaticClass(), FTransform::Identity);
	if (Landscape == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Failed to spawn ALandscape actor for terrain '%s'"), *TerrainData.Name);
		return nullptr;
	}

	// 7. Call Import()
	// UE's Import() looks up heightmap data using a default FGuid() key,
	// NOT the landscape GUID passed as the first parameter.
	const int32 Verts = Config.VertsPerAxis;
	FGuid LandscapeGuid = FGuid::NewGuid();

	TMap<FGuid, TArray<uint16>> HeightDataMap;
	HeightDataMap.Add(FGuid(), MoveTemp(FinalHeightmap));

	// Build layer info objects and blend weight data
	// Layer info assets go alongside the landscape in a LayerInfos subfolder
	const UZeroEditorToolsSettings* LayerInfoSettings = UZeroEditorToolsSettings::Get();
	const FString TerrainBasePath = SubstituteLevelName(LayerInfoSettings->TerrainDestinationPath.Path, LevelName);
	const FString LayerInfoDestPath = TerrainBasePath / TEXT("LayerInfos");
	TArray<FLandscapeImportLayerInfo> LayerInfos = BuildLayerInfoAndWeights(
		TerrainData, LayerInfoDestPath, LevelName, Verts);

	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerMap;
	MaterialLayerMap.Add(FGuid(), MoveTemp(LayerInfos));

	Landscape->Import(
		LandscapeGuid,
		0, 0, Verts - 1, Verts - 1,
		Config.NumSubsections, Config.SubsectionSizeQuads,
		HeightDataMap, nullptr, MaterialLayerMap,
		ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());

	// 8. Set scale -- Z scale uses actual data range, not full [0,1]
	// UE formula: WorldZ = ((uint16 - 32768) / 128.0) * ActorScale.Z
	// uint16 range [0,65535] maps to local height [-256, +256) = 512 units
	// We need 512 * ZScale = ActualHeightRange in cm
	const float XYScale = static_cast<float>(TerrainData.HeightmapDimScale) * 100.0f;
	const float HeightRange = TerrainData.HeightCeiling - TerrainData.HeightFloor;
	const float ActualRange = (ActualMax - ActualMin) * HeightRange;  // actual height span in meters
	const float ZScale = ActualRange * 100.0f / 512.0f;  // convert to cm, divide by UE's local range
	const FVector LandscapeScale(XYScale, XYScale, ZScale);
	Landscape->SetActorScale3D(LandscapeScale);

	// 9. Set position -- UE landscapes have their origin at the corner, so offset
	// by half the extent to center the terrain at world origin like SWBF expects.
	const float HalfExtent = (Verts - 1) * XYScale * 0.5;
	const float ActualFloor = TerrainData.HeightFloor + ActualMin * HeightRange;
	const float ActualCeiling = TerrainData.HeightFloor + ActualMax * HeightRange;
	const float ZPosition = (ActualFloor + ActualCeiling) / 2.0f * 100.0f;
	Landscape->SetActorLocation(FVector(-HalfExtent, -HalfExtent, ZPosition));
	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Terrain '%s': landscape positioned at (%.1f, %.1f, %.1f) -- actual height range [%.2f, %.2f]m"),
		*TerrainData.Name, -HalfExtent, -HalfExtent, ZPosition, ActualFloor, ActualCeiling);

	// 10. Set label
	FString ActorLabel;
	if (TerrainIndex == 0)
	{
		ActorLabel = FString::Printf(TEXT("Landscape_%s"), *LevelName);
	}
	else
	{
		ActorLabel = FString::Printf(TEXT("Landscape_%s_%02d"), *LevelName, TerrainIndex + 1);
	}
	Landscape->SetActorLabel(ActorLabel);

	// 11. Log bounds comparison
	const float LandscapeXYExtent = (Verts - 1) * XYScale;
	const float LandscapeZRange = ActualRange * 100.0f;
	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Terrain '%s' landscape bounds: XY extent %.1f cm, Z range %.1f cm"),
		*TerrainData.Name, LandscapeXYExtent, LandscapeZRange);

	const float StaticMeshXYExtent = static_cast<float>(TerrainData.HeightmapDim) * static_cast<float>(TerrainData.HeightmapDimScale) * 100.0f;
	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Terrain '%s' static mesh would be: XY extent %.1f cm, Z range %.1f cm"),
		*TerrainData.Name, StaticMeshXYExtent, LandscapeZRange);

	// 12. Return
	return Landscape;
}

// ---------------------------------------------------------------------------
// CleanSentinelValues
// ---------------------------------------------------------------------------

int32 FSWBFTerrainImporter::CleanSentinelValues(TArray<float>& HeightmapData, uint32 Dim)
{
	static constexpr uint32 SentinelPattern = 0xf0f0f0f0;
	const int32 TotalSize = static_cast<int32>(Dim * Dim);
	int32 ReplacedCount = 0;

	// First pass: identify sentinels
	TArray<bool> IsSentinel;
	IsSentinel.SetNumZeroed(TotalSize);

	for (int32 Idx = 0; Idx < TotalSize; ++Idx)
	{
		uint32 Bits;
		FMemory::Memcpy(&Bits, &HeightmapData[Idx], sizeof(uint32));
		if (Bits == SentinelPattern)
		{
			IsSentinel[Idx] = true;
		}
	}

	// Second pass: replace sentinels with neighbor-interpolated values
	for (int32 Idx = 0; Idx < TotalSize; ++Idx)
	{
		if (!IsSentinel[Idx])
		{
			continue;
		}

		const int32 X = Idx % static_cast<int32>(Dim);
		const int32 Y = Idx / static_cast<int32>(Dim);

		float Sum = 0.f;
		int32 ValidCount = 0;

		// 4-connected neighbors
		const int32 Offsets[4][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
		for (const auto& Off : Offsets)
		{
			const int32 NX = X + Off[0];
			const int32 NY = Y + Off[1];
			if (NX >= 0 && NX < static_cast<int32>(Dim) && NY >= 0 && NY < static_cast<int32>(Dim))
			{
				const int32 NIdx = NY * static_cast<int32>(Dim) + NX;
				if (!IsSentinel[NIdx])
				{
					Sum += HeightmapData[NIdx];
					++ValidCount;
				}
			}
		}

		HeightmapData[Idx] = (ValidCount > 0) ? (Sum / static_cast<float>(ValidCount)) : 0.5f;
		++ReplacedCount;
	}

	return ReplacedCount;
}

// ---------------------------------------------------------------------------
// ConvertHeightmapToUint16
// ---------------------------------------------------------------------------

TArray<uint16> FSWBFTerrainImporter::ConvertHeightmapToUint16(const TArray<float>& FloatData, uint32 Dim,
	float& OutActualMin, float& OutActualMax)
{
	const int32 TotalSize = static_cast<int32>(Dim * Dim);
	TArray<uint16> Result;
	Result.SetNumUninitialized(TotalSize);

	// Find actual data range to maximize uint16 precision
	OutActualMin = FLT_MAX;
	OutActualMax = -FLT_MAX;
	for (int32 Idx = 0; Idx < TotalSize; ++Idx)
	{
		OutActualMin = FMath::Min(OutActualMin, FloatData[Idx]);
		OutActualMax = FMath::Max(OutActualMax, FloatData[Idx]);
	}

	// Avoid division by zero for perfectly flat terrain
	const float Range = FMath::Max(OutActualMax - OutActualMin, UE_SMALL_NUMBER);
	const float InvRange = 1.0f / Range;

	for (int32 Idx = 0; Idx < TotalSize; ++Idx)
	{
		const float Normalized = (FloatData[Idx] - OutActualMin) * InvRange;
		Result[Idx] = static_cast<uint16>(
			FMath::Clamp(FMath::RoundToInt(Normalized * 65535.0f), 0, 65535));
	}

	return Result;
}

// ---------------------------------------------------------------------------
// ComputeValidLandscapeDimension
// ---------------------------------------------------------------------------

FLandscapeConfigResult FSWBFTerrainImporter::ComputeValidLandscapeDimension(uint32 SourceDim)
{
	static constexpr int32 SubsectionSizeQuadsValues[] = { 7, 15, 31, 63, 127, 255 };
	static constexpr int32 NumSectionValues[] = { 1, 2 };
	static constexpr int32 MaxComponents = 32;

	FLandscapeConfigResult BestResult;
	int32 BestVerts = INT32_MAX;

	for (int32 SubsectionSizeQuads : SubsectionSizeQuadsValues)
	{
		for (int32 NumSubsections : NumSectionValues)
		{
			for (int32 NumComponents = 1; NumComponents <= MaxComponents; ++NumComponents)
			{
				const int32 Verts = NumComponents * NumSubsections * SubsectionSizeQuads + 1;

				if (Verts >= static_cast<int32>(SourceDim) && Verts < BestVerts)
				{
					BestVerts = Verts;
					BestResult.VertsPerAxis = Verts;
					BestResult.NumComponents = NumComponents;
					BestResult.NumSubsections = NumSubsections;
					BestResult.SubsectionSizeQuads = SubsectionSizeQuads;

					// Exact match -- return immediately
					if (Verts == static_cast<int32>(SourceDim))
					{
						return BestResult;
					}
				}
			}
		}
	}

	return BestResult;
}

// ---------------------------------------------------------------------------
// ResizeHeightmap
// ---------------------------------------------------------------------------

TArray<uint16> FSWBFTerrainImporter::ResizeHeightmap(
	const TArray<uint16>& SourceData, uint32 SourceDim, uint32 TargetDim, EHeightmapResizeMode Mode)
{
	if (SourceDim == TargetDim)
	{
		return SourceData;
	}

	TArray<uint16> Result;

	// Regions are in quads (vertices - 1); ResampleData/ExpandData add +1 internally
	const FIntRect SrcRegion(0, 0, SourceDim - 1, SourceDim - 1);
	const FIntRect DstRegion(0, 0, TargetDim - 1, TargetDim - 1);

	if (Mode == EHeightmapResizeMode::Resample)
	{
		FLandscapeConfigHelper::ResampleData<uint16>(SourceData, Result, SrcRegion, DstRegion);
	}
	else // PadCrop
	{
		FLandscapeConfigHelper::ExpandData<uint16>(SourceData, Result, SrcRegion, DstRegion, false);
	}

	return Result;
}

// ---------------------------------------------------------------------------
// ComputeLandscapeScale
// ---------------------------------------------------------------------------

FVector FSWBFTerrainImporter::ComputeLandscapeScale(const FSWBFTerrainData& TerrainData)
{
	const float XYScale = static_cast<float>(TerrainData.HeightmapDimScale) * 100.0f;
	const float ZScale = (TerrainData.HeightCeiling - TerrainData.HeightFloor) * 100.0f / 512.0f;

	return FVector(XYScale, XYScale, ZScale);
}

// ---------------------------------------------------------------------------
// CreateLandscapeMaterial
// ---------------------------------------------------------------------------

UMaterialInterface* FSWBFTerrainImporter::CreateLandscapeMaterial(
	const FSWBFTerrainData& TerrainData,
	const FString& MaterialDestPath,
	const FString& TextureDestPath,
	const FString& LevelName,
	const FString& SourceFilePath,
	int32 TerrainIndex)
{
	// Early-out: no layers means no material
	if (TerrainData.LayerTextureNames.Num() == 0)
	{
		UE_LOG(LogZeroEditorTools, Warning,
			TEXT("Terrain '%s': no layer textures, skipping landscape material creation"),
			*TerrainData.Name);
		return nullptr;
	}

	const bool bUseLayerFunction = UZeroEditorToolsSettings::Get()->bUseLandscapeLayerFunction;

	// Load the MF_SWBF_LandscapeLayer material function if enabled
	UMaterialFunction* LayerFunction = nullptr;
	if (bUseLayerFunction)
	{
		LayerFunction = LoadObject<UMaterialFunction>(
			nullptr, TEXT("/ZeroEditorTools/Materials/Landscape/MF_SWBF_LandscapeLayer.MF_SWBF_LandscapeLayer"));
		if (!LayerFunction)
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Failed to load MF_SWBF_LandscapeLayer -- falling back to simple texture sampling"));
		}
	}

	// Compute asset name: M_{LevelName}_Landscape_{NN}
	const int32 TerrainNumber = TerrainIndex + 1;
	const FString AssetName = FString::Printf(TEXT("M_%s_Landscape_%02d"), *LevelName, TerrainNumber);

	// Create package and material
	const UZeroEditorToolsSettings* LandscapeMatSettings = UZeroEditorToolsSettings::Get();
	if (LandscapeMatSettings && LandscapeMatSettings->bReuseExistingAssets)
	{
		FAssetData ExistingData;
		if (FindExistingAssetBySourceName(AssetName, UMaterial::StaticClass()->GetClassPathName(), ExistingData))
		{
			UMaterial* Existing = Cast<UMaterial>(ExistingData.GetAsset());
			if (Existing)
			{
				if (!LandscapeMatSettings->bOverwriteExistingAssets)
				{
					UE_LOG(LogZeroEditorTools, Log, TEXT("Reusing existing landscape material: %s"), *ExistingData.GetObjectPathString());
					return Existing;
				}
				// If overwriting, fall through to recreate
			}
		}
	}

	const FString PackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDestPath, *AssetName);
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Failed to create package for landscape material: %s"), *PackagePath);
		return nullptr;
	}
	Package->FullyLoad();

	UMaterial* Material = NewObject<UMaterial>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!Material)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Failed to create landscape material: %s"), *AssetName);
		return nullptr;
	}

	// Build unique layer names: SWBF can have duplicate texture names across slots.
	// UE landscape layers are identified by FName, so duplicates collide.
	TArray<FString> UniqueLayerNames;
	TMap<FString, int32> LayerNameCounts;
	for (int32 LayerIdx = 0; LayerIdx < TerrainData.LayerTextureNames.Num(); ++LayerIdx)
	{
		const FString SanitizedTexName = FSWBFNameSanitizer::SanitizeTextureName(
			TerrainData.LayerTextureNames[LayerIdx], LevelName);
		FString BaseName = SanitizedTexName;
		if (BaseName.StartsWith(TEXT("T_")))
		{
			BaseName = BaseName.Mid(2);
		}

		int32& Count = LayerNameCounts.FindOrAdd(BaseName, 0);
		++Count;
		// Append slot index if this name appeared before
		FString FinalName = (Count > 1) ? FString::Printf(TEXT("%s_%d"), *BaseName, LayerIdx) : BaseName;
		UniqueLayerNames.Add(FinalName);
	}

	// Create LandscapeLayerBlend node
	auto* LayerBlend = NewObject<UMaterialExpressionLandscapeLayerBlend>(Material);
	LayerBlend->MaterialExpressionEditorX = 0;
	LayerBlend->MaterialExpressionEditorY = 0;
	Material->GetExpressionCollection().AddExpression(LayerBlend);

	// Create a shared LandscapeLayerCoords node for the simple path
	UMaterialExpressionLandscapeLayerCoords* LayerCoords = nullptr;
	if (!LayerFunction)
	{
		LayerCoords = NewObject<UMaterialExpressionLandscapeLayerCoords>(Material);
		LayerCoords->MappingType = TCMT_Auto;
		LayerCoords->MappingScale = 1.0f;
		LayerCoords->MaterialExpressionEditorX = -900;
		LayerCoords->MaterialExpressionEditorY = 0;
		Material->GetExpressionCollection().AddExpression(LayerCoords);
	}

	// Build per-layer nodes and wire into LandscapeLayerBlend
	const int32 LayerSpacingY = 400;
	int32 LayersCreated = 0;
	for (int32 LayerIdx = 0; LayerIdx < TerrainData.LayerTextureNames.Num(); ++LayerIdx)
	{
		const FString SanitizedTexName = FSWBFNameSanitizer::SanitizeTextureName(
			TerrainData.LayerTextureNames[LayerIdx], LevelName);
		const FString& LayerName = UniqueLayerNames[LayerIdx];
		const int32 NodeY = LayerIdx * LayerSpacingY;

		// Load the layer texture
		const FString TexAssetPath = FString::Printf(TEXT("%s/%s.%s"),
			*TextureDestPath, *SanitizedTexName, *SanitizedTexName);
		UTexture2D* LoadedTex = LoadObject<UTexture2D>(nullptr, *TexAssetPath);

		UMaterialExpression* LayerOutputNode = nullptr;

		if (LayerFunction)
		{
			// Material function path: MF_SWBF_LandscapeLayer with TextureObject → DiffuseTexture input
			auto* FuncCall = NewObject<UMaterialExpressionMaterialFunctionCall>(Material);
			FuncCall->MaterialExpressionEditorX = -400;
			FuncCall->MaterialExpressionEditorY = NodeY;
			FuncCall->SetMaterialFunction(LayerFunction);
			Material->GetExpressionCollection().AddExpression(FuncCall);

			if (LoadedTex)
			{
				auto* TexObject = NewObject<UMaterialExpressionTextureObject>(Material);
				TexObject->Texture = LoadedTex;
				TexObject->MaterialExpressionEditorX = -700;
				TexObject->MaterialExpressionEditorY = NodeY;
				Material->GetExpressionCollection().AddExpression(TexObject);

				for (int32 InputIdx = 0; InputIdx < FuncCall->FunctionInputs.Num(); ++InputIdx)
				{
					if (FuncCall->FunctionInputs[InputIdx].ExpressionInput->InputName == TEXT("DiffuseTexture"))
					{
						FuncCall->FunctionInputs[InputIdx].Input.Connect(0, TexObject);
						break;
					}
				}
			}

			LayerOutputNode = FuncCall;
		}
		else
		{
			// Simple path: TextureSampleParameter2D with LandscapeLayerCoords UVs
			auto* TexSample = NewObject<UMaterialExpressionTextureSampleParameter2D>(Material);
			TexSample->ParameterName = FName(*LayerName);
			TexSample->MaterialExpressionEditorX = -400;
			TexSample->MaterialExpressionEditorY = NodeY;
			if (LoadedTex)
			{
				TexSample->Texture = LoadedTex;
			}
			TexSample->Coordinates.Connect(0, LayerCoords);
			Material->GetExpressionCollection().AddExpression(TexSample);

			LayerOutputNode = TexSample;
		}

		if (!LoadedTex)
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Layer texture not found: %s -- layer '%s' will use default"),
				*TexAssetPath, *LayerName);
		}

		// Add FLayerBlendInput to LandscapeLayerBlend
		FLayerBlendInput BlendInput;
		BlendInput.LayerName = FName(*LayerName);
		BlendInput.BlendType = LB_WeightBlend;
		BlendInput.PreviewWeight = (LayerIdx == 0) ? 1.0f : 0.0f;
		LayerBlend->Layers.Add(BlendInput);

		// Connect layer output to blend input
		LayerBlend->Layers[LayerIdx].LayerInput.Connect(0, LayerOutputNode);
		++LayersCreated;

		UE_LOG(LogZeroEditorTools, Log,
			TEXT("Layer %d '%s': %s with texture '%s'"),
			LayerIdx, *LayerName,
			LayerFunction ? TEXT("MF_SWBF_LandscapeLayer") : TEXT("TextureSample"),
			*SanitizedTexName);
	}

	// Connect LandscapeLayerBlend output to material BaseColor
	auto* EditorData = Material->GetEditorOnlyData();
	EditorData->BaseColor.Connect(0, LayerBlend);

	// Set Specular to 0.0
	auto* SpecularConst = NewObject<UMaterialExpressionConstant>(Material);
	SpecularConst->R = 0.0f;
	SpecularConst->MaterialExpressionEditorX = 200;
	SpecularConst->MaterialExpressionEditorY = 200;
	Material->GetExpressionCollection().AddExpression(SpecularConst);
	EditorData->Specular.Connect(0, SpecularConst);

	// Compile the material
	Material->PreEditChange(nullptr);
	Material->PostEditChange();

	// Finalize base material asset
	FinalizeAsset(Material, AssetName, LevelName, SourceFilePath);

	// Create a material instance of the landscape material
	const FString MIAssetName = FString::Printf(TEXT("MI_%s_Landscape_%02d"), *LevelName, TerrainNumber);

	if (LandscapeMatSettings && LandscapeMatSettings->bReuseExistingAssets)
	{
		FAssetData ExistingMIData;
		if (FindExistingAssetBySourceName(MIAssetName, UMaterialInstanceConstant::StaticClass()->GetClassPathName(), ExistingMIData))
		{
			UMaterialInstanceConstant* ExistingMI = Cast<UMaterialInstanceConstant>(ExistingMIData.GetAsset());
			if (ExistingMI)
			{
				if (!LandscapeMatSettings->bOverwriteExistingAssets)
				{
					UE_LOG(LogZeroEditorTools, Log, TEXT("Reusing existing landscape MIC: %s"), *ExistingMIData.GetObjectPathString());
					return ExistingMI;
				}
				// If overwriting, fall through to recreate
			}
		}
	}

	const FString MIPackagePath = FString::Printf(TEXT("%s/%s"), *MaterialDestPath, *MIAssetName);

	UPackage* MIPackage = CreatePackage(*MIPackagePath);
	if (!MIPackage)
	{
		UE_LOG(LogZeroEditorTools, Warning,
			TEXT("Failed to create package for landscape material instance: %s -- using base material"),
			*MIPackagePath);
		return Material;
	}
	MIPackage->FullyLoad();

	UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(
		MIPackage, *MIAssetName, RF_Public | RF_Standalone);
	if (!MIC)
	{
		UE_LOG(LogZeroEditorTools, Warning,
			TEXT("Failed to create landscape material instance -- using base material"));
		return Material;
	}

	MIC->SetParentEditorOnly(Material);
	MIC->PostEditChange();
	FinalizeAsset(MIC, MIAssetName, LevelName, SourceFilePath);

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Created landscape material '%s' + instance '%s' with %d layers using MF_SWBF_LandscapeLayer"),
		*AssetName, *MIAssetName, LayersCreated);

	return MIC;
}

// ---------------------------------------------------------------------------
// BuildLayerInfoAndWeights
// ---------------------------------------------------------------------------

TArray<FLandscapeImportLayerInfo> FSWBFTerrainImporter::BuildLayerInfoAndWeights(
	const FSWBFTerrainData& TerrainData,
	const FString& LayerInfoDestPath,
	const FString& LevelName,
	int32 TargetDim)
{
	TArray<FLandscapeImportLayerInfo> Result;

	const int32 NumLayers = TerrainData.LayerTextureNames.Num();
	if (NumLayers == 0)
	{
		return Result;
	}

	const bool bHasBlendData = TerrainData.BlendMapData.Num() > 0
		&& TerrainData.BlendMapDim > 0
		&& TerrainData.BlendLayerCount > 0;

	const int32 TargetSize = TargetDim * TargetDim;

	// Build unique layer names (same logic as CreateLandscapeMaterial)
	TArray<FString> UniqueLayerNames;
	TMap<FString, int32> LayerNameCounts;
	for (int32 i = 0; i < NumLayers; ++i)
	{
		const FString SanitizedTexName = FSWBFNameSanitizer::SanitizeTextureName(
			TerrainData.LayerTextureNames[i], LevelName);
		FString BaseName = SanitizedTexName;
		if (BaseName.StartsWith(TEXT("T_")))
		{
			BaseName = BaseName.Mid(2);
		}
		int32& Count = LayerNameCounts.FindOrAdd(BaseName, 0);
		++Count;
		FString FinalName = (Count > 1) ? FString::Printf(TEXT("%s_%d"), *BaseName, i) : BaseName;
		UniqueLayerNames.Add(FinalName);
	}

	// Warn if there are more texture layers than blend layers
	if (bHasBlendData && NumLayers > static_cast<int32>(TerrainData.BlendLayerCount))
	{
		FString UnpaintedNames;
		for (int32 i = static_cast<int32>(TerrainData.BlendLayerCount); i < NumLayers; ++i)
		{
			if (!UnpaintedNames.IsEmpty())
			{
				UnpaintedNames += TEXT(", ");
			}
			UnpaintedNames += FString::Printf(TEXT("'%s'"), *UniqueLayerNames[i]);
		}
		UE_LOG(LogZeroEditorTools, Warning,
			TEXT("Terrain '%s': %d texture layers but only %u blend layers from LibSWBF2. Layers %s have no blend data."),
			*TerrainData.Name, NumLayers, TerrainData.BlendLayerCount, *UnpaintedNames);
	}

	for (int32 LayerIdx = 0; LayerIdx < NumLayers; ++LayerIdx)
	{
		const FString& LayerName = UniqueLayerNames[LayerIdx];

		// Create ULandscapeLayerInfoObject as a saved asset
		const FString InfoAssetName = FString::Printf(TEXT("LI_%s"), *LayerName);
		const FString InfoPackagePath = FString::Printf(TEXT("%s/%s"), *LayerInfoDestPath, *InfoAssetName);

		UPackage* InfoPackage = CreatePackage(*InfoPackagePath);
		if (!InfoPackage)
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Failed to create package for layer info: %s"), *InfoPackagePath);
			continue;
		}
		InfoPackage->FullyLoad();

		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		ULandscapeLayerInfoObject* LayerInfo = NewObject<ULandscapeLayerInfoObject>(
			InfoPackage, *InfoAssetName, RF_Public | RF_Standalone);
		LayerInfo->LayerName = FName(*LayerName);
		LayerInfo->LayerUsageDebugColor = FLinearColor::MakeRandomColor();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		LayerInfo->MarkPackageDirty();

		// Build import layer info
		const FName LayerFName(*LayerName);
		FLandscapeImportLayerInfo ImportLayerInfo(LayerFName);
		ImportLayerInfo.LayerInfo = LayerInfo;

		// Extract and resample blend weight data for this layer
		if (bHasBlendData && LayerIdx < static_cast<int32>(TerrainData.BlendLayerCount))
		{
			// De-interleave this layer's weight channel from the blend map
			const uint32 BlendDim = TerrainData.BlendMapDim;
			const uint32 NumBlendLayers = TerrainData.BlendLayerCount;
			const int32 BlendPixelCount = static_cast<int32>(BlendDim * BlendDim);

			TArray<uint8> SingleLayerData;
			SingleLayerData.SetNumUninitialized(BlendPixelCount);

			int32 NonZeroCount = 0;
			uint8 MinWeight = 255;
			uint8 MaxWeight = 0;

			for (int32 PixelIdx = 0; PixelIdx < BlendPixelCount; ++PixelIdx)
			{
				// Read source pixels in reverse order (180-degree flip matching heightmap Algo::Reverse)
				const int32 SrcPixelIdx = (BlendPixelCount - 1) - PixelIdx;
				const uint8 Weight = TerrainData.BlendMapData[SrcPixelIdx * NumBlendLayers + LayerIdx];
				SingleLayerData[PixelIdx] = Weight;
				if (Weight > 0) { ++NonZeroCount; }
				MinWeight = FMath::Min(MinWeight, Weight);
				MaxWeight = FMath::Max(MaxWeight, Weight);
			}

			UE_LOG(LogZeroEditorTools, Log,
				TEXT("Layer %d '%s': blend data %ux%u, non-zero=%d/%d (%.1f%%), range=[%u,%u]"),
				LayerIdx, *LayerName, BlendDim, BlendDim,
				NonZeroCount, BlendPixelCount,
				BlendPixelCount > 0 ? (100.0f * NonZeroCount / BlendPixelCount) : 0.0f,
				MinWeight, MaxWeight);

			// Resample to match target landscape dimensions
			if (BlendDim == static_cast<uint32>(TargetDim))
			{
				ImportLayerInfo.LayerData = MoveTemp(SingleLayerData);
			}
			else
			{
				ImportLayerInfo.LayerData = ResampleWeightLayer(SingleLayerData, BlendDim, TargetDim);
			}
		}
		else
		{
			// No blend data for this layer: first layer gets full weight, others get zero
			ImportLayerInfo.LayerData.SetNumZeroed(TargetSize);
			if (LayerIdx == 0)
			{
				FMemory::Memset(ImportLayerInfo.LayerData.GetData(), 255, TargetSize);
			}

			UE_LOG(LogZeroEditorTools, Log,
				TEXT("Layer '%s': no blend data available, using %s weight"),
				*LayerName, LayerIdx == 0 ? TEXT("full") : TEXT("zero"));
		}

		Result.Add(MoveTemp(ImportLayerInfo));
	}

	// Per-pixel weight normalization pass: ensure all pixels sum to exactly 255
	if (Result.Num() > 0 && bHasBlendData)
	{
		int32 NormalizedCount = 0;
		const int32 NumResultLayers = Result.Num();

		for (int32 PixelIdx = 0; PixelIdx < TargetSize; ++PixelIdx)
		{
			// Sum weights across all layers at this pixel
			int32 WeightSum = 0;
			for (int32 Li = 0; Li < NumResultLayers; ++Li)
			{
				WeightSum += static_cast<int32>(Result[Li].LayerData[PixelIdx]);
			}

			if (WeightSum == 255)
			{
				continue; // Already correct
			}

			if (WeightSum == 0)
			{
				// All-zero fallback: first layer gets full weight
				Result[0].LayerData[PixelIdx] = 255;
				++NormalizedCount;
				continue;
			}

			// Proportional scale to 255
			int32 ScaledSum = 0;
			for (int32 Li = 0; Li < NumResultLayers; ++Li)
			{
				const int32 Scaled = (static_cast<int32>(Result[Li].LayerData[PixelIdx]) * 255) / WeightSum;
				Result[Li].LayerData[PixelIdx] = static_cast<uint8>(FMath::Clamp(Scaled, 0, 255));
				ScaledSum += Scaled;
			}

			// First layer absorbs rounding error
			const int32 Remainder = 255 - ScaledSum;
			const int32 AdjustedFirst = static_cast<int32>(Result[0].LayerData[PixelIdx]) + Remainder;
			Result[0].LayerData[PixelIdx] = static_cast<uint8>(FMath::Clamp(AdjustedFirst, 0, 255));
			++NormalizedCount;
		}

		UE_LOG(LogZeroEditorTools, Log,
			TEXT("Terrain '%s': normalized weights at %d/%d pixels (%.1f%% adjusted)"),
			*TerrainData.Name, NormalizedCount, TargetSize,
			TargetSize > 0 ? (100.0f * NormalizedCount / TargetSize) : 0.0f);
	}

	// Per-layer coverage diagnostics
	if (Result.Num() > 0 && bHasBlendData)
	{
		FString CoverageStr;
		for (int32 Li = 0; Li < Result.Num(); ++Li)
		{
			int32 CoveredPixels = 0;
			for (int32 PixelIdx = 0; PixelIdx < TargetSize; ++PixelIdx)
			{
				if (Result[Li].LayerData[PixelIdx] > 0)
				{
					++CoveredPixels;
				}
			}

			if (!CoverageStr.IsEmpty())
			{
				CoverageStr += TEXT(", ");
			}
			CoverageStr += FString::Printf(TEXT("%s: %.1f%%"),
				*Result[Li].LayerName.ToString(),
				TargetSize > 0 ? (100.0f * CoveredPixels / TargetSize) : 0.0f);
		}

		UE_LOG(LogZeroEditorTools, Log,
			TEXT("Terrain '%s': layer coverage: %s"),
			*TerrainData.Name, *CoverageStr);
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Created %d landscape layer info objects for %d terrain layers"),
		Result.Num(), NumLayers);

	return Result;
}

// ---------------------------------------------------------------------------
// ResampleWeightLayer
// ---------------------------------------------------------------------------

TArray<uint8> FSWBFTerrainImporter::ResampleWeightLayer(
	const TArray<uint8>& SourceData, uint32 SourceDim, uint32 TargetDim)
{
	const int32 TargetSize = static_cast<int32>(TargetDim * TargetDim);
	TArray<uint8> Result;
	Result.SetNumUninitialized(TargetSize);

	const float Scale = static_cast<float>(SourceDim - 1) / static_cast<float>(TargetDim - 1);

	for (uint32 TargetY = 0; TargetY < TargetDim; ++TargetY)
	{
		for (uint32 TargetX = 0; TargetX < TargetDim; ++TargetX)
		{
			const float SrcX = TargetX * Scale;
			const float SrcY = TargetY * Scale;

			const uint32 X0 = FMath::Min(static_cast<uint32>(SrcX), SourceDim - 2);
			const uint32 Y0 = FMath::Min(static_cast<uint32>(SrcY), SourceDim - 2);
			const uint32 X1 = X0 + 1;
			const uint32 Y1 = Y0 + 1;

			const float FracX = SrcX - static_cast<float>(X0);
			const float FracY = SrcY - static_cast<float>(Y0);

			const float V00 = static_cast<float>(SourceData[Y0 * SourceDim + X0]);
			const float V10 = static_cast<float>(SourceData[Y0 * SourceDim + X1]);
			const float V01 = static_cast<float>(SourceData[Y1 * SourceDim + X0]);
			const float V11 = static_cast<float>(SourceData[Y1 * SourceDim + X1]);

			const float Interpolated =
				V00 * (1.0f - FracX) * (1.0f - FracY) +
				V10 * FracX * (1.0f - FracY) +
				V01 * (1.0f - FracX) * FracY +
				V11 * FracX * FracY;

			Result[TargetY * TargetDim + TargetX] = static_cast<uint8>(
				FMath::Clamp(FMath::RoundToInt(Interpolated), 0, 255));
		}
	}

	return Result;
}

// ---------------------------------------------------------------------------
// CollectWorldNames
// ---------------------------------------------------------------------------

void FSWBFTerrainImporter::CollectWorldNames(TSet<FString>& OutWorldNames) const
{
	for (const auto& Data : TerrainDataArray)
	{
		if (!Data.WorldName.IsEmpty())
		{
			OutWorldNames.Add(Data.WorldName);
		}
	}
}

#undef LOCTEXT_NAMESPACE
