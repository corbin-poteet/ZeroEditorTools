// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFMeshImporter.h"
#include "SWBFLevelWrapper.h"
#include "SWBFCoordinateUtils.h"
#include "SWBFNameSanitizer.h"
#include "SWBFMeshAssetUserData.h"
#include "ZeroEditorTools.h"
#include "ZeroEditorToolsSettings.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "Engine/StaticMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"

THIRD_PARTY_INCLUDES_START
#include <LibSWBF2/Wrappers/Level.h>
#include <LibSWBF2/Wrappers/Model.h>
#include <LibSWBF2/Wrappers/Segment.h>
#include <LibSWBF2/Wrappers/CollisionMesh.h>
#include <LibSWBF2/Wrappers/CollisionPrimitive.h>
#include <LibSWBF2/Wrappers/Material.h>
#include <LibSWBF2/Types/LibString.h>
#include <LibSWBF2/Types/List.h>
#include <LibSWBF2/Types/Enums.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "SWBFMeshImporter"

// ---------------------------------------------------------------------------
// FSWBFMeshImporter::ShouldImport
// ---------------------------------------------------------------------------

bool FSWBFMeshImporter::ShouldImport() const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	return Settings && Settings->bImportMeshes && GetWorkCount() > 0;
}

// ---------------------------------------------------------------------------
// FSWBFMeshImporter::GetResolvedDestPath
// ---------------------------------------------------------------------------

FString FSWBFMeshImporter::GetResolvedDestPath(const FString& LevelName) const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	if (!Settings)
	{
		return FString();
	}
	return SubstituteLevelName(Settings->MeshDestinationPath.Path, LevelName);
}

// ---------------------------------------------------------------------------
// ConvertStripToList
// ---------------------------------------------------------------------------

/**
 * Convert a triangle strip index buffer to a triangle list.
 * Handles degenerate triangles (where any two indices are equal) by skipping them.
 * Alternates winding order for odd-indexed triangles as required by strip topology.
 */
static TArray<uint32> ConvertStripToList(const uint16_t* StripIndices, uint32_t Count)
{
	TArray<uint32> ListIndices;
	if (Count < 3)
	{
		return ListIndices;
	}

	ListIndices.Reserve(static_cast<int32>((Count - 2) * 3));

	for (uint32_t i = 0; i <= Count - 3; ++i)
	{
		const uint16_t i0 = StripIndices[i];
		const uint16_t i1 = StripIndices[i + 1];
		const uint16_t i2 = StripIndices[i + 2];

		// Skip degenerate triangles
		if (i0 == i1 || i1 == i2 || i0 == i2)
		{
			continue;
		}

		if (i % 2 == 0)
		{
			// Even: normal winding
			ListIndices.Add(static_cast<uint32>(i0));
			ListIndices.Add(static_cast<uint32>(i1));
			ListIndices.Add(static_cast<uint32>(i2));
		}
		else
		{
			// Odd: reversed winding for strip
			ListIndices.Add(static_cast<uint32>(i0));
			ListIndices.Add(static_cast<uint32>(i2));
			ListIndices.Add(static_cast<uint32>(i1));
		}
	}

	return ListIndices;
}

// ---------------------------------------------------------------------------
// FSWBFSegmentData constructor
// ---------------------------------------------------------------------------

FSWBFSegmentData::FSWBFSegmentData(const LibSWBF2::Wrappers::Segment& InSegment)
{
	using LibSWBF2::Wrappers::Material;
	using LibSWBF2::Types::String;
	using LibSWBF2::Types::Vector2;
	using LibSWBF2::Types::Vector3;
	using LibSWBF2::Types::Color4u8;
	using LibSWBF2::ETopology;
	using LibSWBF2::EMaterialFlags;

	// Get vertex buffer
	uint32_t VertCount = 0;
	Vector3* VertBuffer = nullptr;
	InSegment.GetVertexBuffer(VertCount, VertBuffer);

	// Get normal buffer
	uint32_t NormCount = 0;
	Vector3* NormBuffer = nullptr;
	InSegment.GetNormalBuffer(NormCount, NormBuffer);

	// Get UV buffer
	uint32_t UVCount = 0;
	Vector2* UVBuffer = nullptr;
	InSegment.GetUVBuffer(UVCount, UVBuffer);

	// Get index buffer
	uint32_t IdxCount = 0;
	uint16_t* IdxBuffer = nullptr;
	InSegment.GetIndexBuffer(IdxCount, IdxBuffer);

	// Skip segments with no geometry
	if (VertCount == 0 || IdxCount == 0)
	{
		bValid = false;
		return;
	}

	// Convert vertices to UE coordinate space
	Vertices.SetNumUninitialized(static_cast<int32>(VertCount));
	for (uint32_t vi = 0; vi < VertCount; ++vi)
	{
		Vertices[vi] = FSWBFCoordinateUtils::ConvertPosition(
			VertBuffer[vi].m_X, VertBuffer[vi].m_Y, VertBuffer[vi].m_Z);
	}

	// Convert normals to UE coordinate space (axis remap without unit scale)
	Normals.SetNumUninitialized(static_cast<int32>(NormCount));
	for (uint32_t ni = 0; ni < NormCount; ++ni)
	{
		// SWBF (X, Y, Z) -> UE (-X, -Z, Y), same axis remap as ConvertPosition but no UnitScale
		Normals[ni] = FVector(
			-NormBuffer[ni].m_X,
			-NormBuffer[ni].m_Z,
			NormBuffer[ni].m_Y);
	}

	// Copy UVs as-is
	UVs.SetNumUninitialized(static_cast<int32>(UVCount));
	for (uint32_t ui = 0; ui < UVCount; ++ui)
	{
		UVs[ui] = FVector2D(UVBuffer[ui].m_X, UVBuffer[ui].m_Y);
	}

	// Convert indices based on topology
	const ETopology Topology = InSegment.GetTopology();
	if (Topology == ETopology::TriangleStrip)
	{
		Indices = ConvertStripToList(IdxBuffer, IdxCount);
	}
	else
	{
		// TriangleList or other -- widen uint16 to uint32
		Indices.SetNumUninitialized(static_cast<int32>(IdxCount));
		for (uint32_t ii = 0; ii < IdxCount; ++ii)
		{
			Indices[ii] = static_cast<uint32>(IdxBuffer[ii]);
		}
	}

	// Fix winding order after coordinate conversion (handedness flip requires swap of i+1/i+2)
	for (int32 ti = 0; ti + 2 < Indices.Num(); ti += 3)
	{
		Swap(Indices[ti + 1], Indices[ti + 2]);
	}

	// Extract material info
	const Material& Mat = InSegment.GetMaterial();

	// Material has no GetName(), so use first texture name as material identifier
	// Extract texture slots 0-3, matching FSWBFMaterialData::GetData() gap-pad logic
	String TexName;
	for (uint8_t TexIdx = 0; TexIdx <= 3; ++TexIdx)
	{
		if (Mat.GetTextureName(TexIdx, TexName) && TexName.Length() > 0)
		{
			FString TexNameStr = FString(ANSI_TO_TCHAR(TexName.Buffer()));

			// Gap-pad to cover this index (matches material importer)
			while (TextureNames.Num() <= static_cast<int32>(TexIdx))
			{
				TextureNames.Add(FString());
			}
			TextureNames[TexIdx] = TexNameStr;

			// Use the first real texture name as material name fallback
			if (MaterialName.IsEmpty())
			{
				MaterialName = TexNameStr;
			}
		}
	}

	// Compute material dedup key for MaterialLookup.
	// CRITICAL: Format MUST be identical to FSWBFMaterialData::GetDedupKey() in SWBFMaterialImporter.cpp.
	if (TextureNames.Num() > 0)
	{
		const EMaterialFlags MatFlags = Mat.GetFlags();
		const Color4u8& DiffColor = Mat.GetDiffuseColor();
		const Color4u8& SpecColor = Mat.GetSpecularColor();
		const uint32_t SpecExp = Mat.GetSpecularExponent();

		// Parameters are stored as uint32 raw bit patterns in the SWBF chunk;
		// pass them directly to %08X (no float decode needed for the key).
		const uint32_t RawParam0 = Mat.GetFirstParameter();
		const uint32_t RawParam1 = Mat.GetSecondParameter();

		// Build texture slot string matching FSWBFMaterialData::GetDedupKey() format
		FString TexPart;
		for (int32 ti = 0; ti < TextureNames.Num(); ++ti)
		{
			if (ti > 0) TexPart += TEXT("|");
			TexPart += TextureNames[ti].ToLower();
		}

		const FString AttachedLightStr = FString(ANSI_TO_TCHAR(Mat.GetAttachedLight().Buffer())).ToLower();

		MaterialDedupKey = FString::Printf(
			TEXT("%s_%08X_%02X%02X%02X%02X_%02X%02X%02X%02X_%08X_%08X_%08X_%s"),
			*TexPart,
			static_cast<uint32>(MatFlags),
			DiffColor.m_Red,  DiffColor.m_Green,  DiffColor.m_Blue,  DiffColor.m_Alpha,
			SpecColor.m_Red,  SpecColor.m_Green,  SpecColor.m_Blue,  SpecColor.m_Alpha,
			SpecExp,
			RawParam0,
			RawParam1,
			*AttachedLightStr);
	}

	// If no texture names found, generate a material name from model + segment index
	if (MaterialName.IsEmpty())
	{
		// MaterialName = FString::Printf(TEXT("%s_seg%d"), *InModelName, InSegmentIndex);
		UE_LOG(LogZeroEditorTools, Warning, TEXT("Segment has no material texture names"));
	}

	bValid = true;
}

// ---------------------------------------------------------------------------
// FSWBFSegmentData::ToString
// ---------------------------------------------------------------------------

FString FSWBFSegmentData::ToString() const
{
	return FString::Printf(
		TEXT("[Segment] mat='%s' verts=%d indices=%d uvs=%d valid=%d"),
		*MaterialName, Vertices.Num(), Indices.Num(), UVs.Num(), bValid ? 1 : 0);
}

// ---------------------------------------------------------------------------
// FSWBFCollisionMeshData constructor
// ---------------------------------------------------------------------------

FSWBFCollisionMeshData::FSWBFCollisionMeshData(const LibSWBF2::Wrappers::CollisionMesh& InCollisionMesh)
{
	using LibSWBF2::Types::Vector3;
	using LibSWBF2::ETopology;

	uint32_t ColVertCount = 0;
	Vector3* ColVertBuffer = nullptr;
	InCollisionMesh.GetVertexBuffer(ColVertCount, ColVertBuffer);

	uint32_t ColIdxCount = 0;
	uint16_t* ColIdxBuffer = nullptr;
	InCollisionMesh.GetIndexBuffer(ETopology::TriangleList, ColIdxCount, ColIdxBuffer);

	if (ColVertCount == 0 || ColIdxCount == 0)
	{
		bValid = false;
		return;
	}

	// Convert collision vertices to UE coordinate space
	Vertices.SetNumUninitialized(static_cast<int32>(ColVertCount));
	for (uint32_t vi = 0; vi < ColVertCount; ++vi)
	{
		Vertices[vi] = FSWBFCoordinateUtils::ConvertPosition(
			ColVertBuffer[vi].m_X, ColVertBuffer[vi].m_Y, ColVertBuffer[vi].m_Z);
	}

	// Widen indices uint16 -> uint32
	Indices.SetNumUninitialized(static_cast<int32>(ColIdxCount));
	for (uint32_t ii = 0; ii < ColIdxCount; ++ii)
	{
		Indices[ii] = static_cast<uint32>(ColIdxBuffer[ii]);
	}

	// Fix winding order after coordinate conversion
	for (int32 ti = 0; ti + 2 < Indices.Num(); ti += 3)
	{
		Swap(Indices[ti + 1], Indices[ti + 2]);
	}

	bValid = true;
}

// ---------------------------------------------------------------------------
// FSWBFCollisionMeshData::ToString
// ---------------------------------------------------------------------------

FString FSWBFCollisionMeshData::ToString() const
{
	return FString::Printf(
		TEXT("[CollisionMesh] verts=%d indices=%d valid=%d"),
		Vertices.Num(), Indices.Num(), bValid ? 1 : 0);
}

// ---------------------------------------------------------------------------
// FSWBFCollisionPrimitiveData constructor
// ---------------------------------------------------------------------------

FSWBFCollisionPrimitiveData::FSWBFCollisionPrimitiveData(const LibSWBF2::Wrappers::CollisionPrimitive& InPrimitive)
{
	using LibSWBF2::Types::Vector3;
	using LibSWBF2::Types::Vector4;
	using LibSWBF2::ECollisionPrimitiveType;

	const ECollisionPrimitiveType PrimType = InPrimitive.GetPrimitiveType();
	const Vector3 Pos = InPrimitive.GetPosition();
	const Vector4 Rot = InPrimitive.GetRotation();

	Position = FSWBFCoordinateUtils::ConvertPosition(Pos.m_X, Pos.m_Y, Pos.m_Z);
	Rotation = FSWBFCoordinateUtils::ConvertRotation(Rot.m_X, Rot.m_Y, Rot.m_Z, Rot.m_W);

	switch (PrimType)
	{
	case ECollisionPrimitiveType::Cube:
	{
		Shape = EShape::Box;
		float CubeX = 0.f, CubeY = 0.f, CubeZ = 0.f;
		if (InPrimitive.GetCubeDims(CubeX, CubeY, CubeZ))
		{
			// Remap axes (SWBF XYZ -> UE XZY) and scale to UE units
			Dimensions = FVector(
				CubeX * FSWBFCoordinateUtils::UnitScale,
				CubeZ * FSWBFCoordinateUtils::UnitScale,
				CubeY * FSWBFCoordinateUtils::UnitScale);
		}
		bValid = true;
		break;
	}
	case ECollisionPrimitiveType::Sphere:
	{
		Shape = EShape::Sphere;
		float Radius = 0.f;
		if (InPrimitive.GetSphereRadius(Radius))
		{
			Dimensions = FVector(Radius * FSWBFCoordinateUtils::UnitScale, 0.f, 0.f);
		}
		bValid = true;
		break;
	}
	case ECollisionPrimitiveType::Cylinder:
	{
		Shape = EShape::Cylinder;
		float Radius = 0.f, Height = 0.f;
		if (InPrimitive.GetCylinderDims(Radius, Height))
		{
			Dimensions = FVector(
				Radius * FSWBFCoordinateUtils::UnitScale,
				Height * FSWBFCoordinateUtils::UnitScale,
				0.f);
		}
		bValid = true;
		break;
	}
	default:
		bValid = false;
		break;
	}
}

// ---------------------------------------------------------------------------
// FSWBFCollisionPrimitiveData::ToString
// ---------------------------------------------------------------------------

FString FSWBFCollisionPrimitiveData::ToString() const
{
	const TCHAR* ShapeName = TEXT("Box");
	switch (Shape)
	{
	case EShape::Sphere:   ShapeName = TEXT("Sphere"); break;
	case EShape::Cylinder: ShapeName = TEXT("Cylinder"); break;
	default: break;
	}
	return FString::Printf(
		TEXT("[CollisionPrimitive] shape=%s pos=(%.1f,%.1f,%.1f) dims=(%.1f,%.1f,%.1f) valid=%d"),
		ShapeName,
		Position.X, Position.Y, Position.Z,
		Dimensions.X, Dimensions.Y, Dimensions.Z, bValid ? 1 : 0);
}

// ---------------------------------------------------------------------------
// FSWBFModelData constructor
// ---------------------------------------------------------------------------

FSWBFModelData::FSWBFModelData(const LibSWBF2::Wrappers::Model& InModel)
{
	using LibSWBF2::Types::String;

	const String& MdlName = InModel.GetName();
	Name = FString(ANSI_TO_TCHAR(MdlName.Buffer()));
	bIsSkeletal = InModel.IsSkeletalMesh();

	// --- Segments ---
	const auto& Segs = InModel.GetSegments();
	const size_t SegCount = Segs.Size();
	Segments.Reserve(static_cast<int32>(SegCount));

	for (size_t si = 0; si < SegCount; ++si)
	{
		FSWBFSegmentData SegData(Segs[si]);
		if (!SegData.bValid && SegData.Vertices.Num() == 0)
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Model '%s' segment %d has no geometry, skipping"),
				*Name, static_cast<int32>(si));
		}
		Segments.Add(MoveTemp(SegData));
	}

	// --- Collision Mesh ---
	CollisionMesh = FSWBFCollisionMeshData(InModel.GetCollisionMesh());

	// --- Collision Primitives ---
	auto ColPrims = InModel.GetCollisionPrimitives();
	const size_t PrimCount = ColPrims.Size();
	CollisionPrimitives.Reserve(static_cast<int32>(PrimCount));

	for (size_t pi = 0; pi < PrimCount; ++pi)
	{
		FSWBFCollisionPrimitiveData PrimData(ColPrims[pi]);
		if (!PrimData.bValid)
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Model '%s' has unknown collision primitive type"),
				*Name);
		}
		CollisionPrimitives.Add(MoveTemp(PrimData));
	}

	// Model is valid if it has at least one valid segment
	bValid = false;
	for (const FSWBFSegmentData& Seg : Segments)
	{
		if (Seg.bValid)
		{
			bValid = true;
			break;
		}
	}

	// Log raw model details
	UE_LOG(LogZeroEditorTools, Verbose, TEXT("%s"), *ToString());
}

// ---------------------------------------------------------------------------
// FSWBFModelData::ToString
// ---------------------------------------------------------------------------

FString FSWBFModelData::ToString() const
{
	int32 TotalVerts = 0, TotalIndices = 0, ValidSegs = 0;
	for (const FSWBFSegmentData& Seg : Segments)
	{
		if (Seg.bValid) { TotalVerts += Seg.Vertices.Num(); TotalIndices += Seg.Indices.Num(); ++ValidSegs; }
	}
	return FString::Printf(
		TEXT("[Mesh] '%s' segments=%d(%d valid) verts=%d indices=%d skeletal=%d collision_mesh=%d collision_prims=%d"),
		*Name, Segments.Num(), ValidSegs, TotalVerts, TotalIndices,
		bIsSkeletal ? 1 : 0,
		CollisionMesh.bValid ? 1 : 0,
		CollisionPrimitives.Num());
}

// ---------------------------------------------------------------------------
// GetData
// ---------------------------------------------------------------------------

void FSWBFMeshImporter::GetData(const FSWBFLevelWrapper& LevelWrapper)
{
	const auto* LevelPtr = LevelWrapper.GetLevel();
	if (!LevelPtr)
	{
		return;
	}

	using LibSWBF2::Wrappers::Model;

	const auto& Models = LevelPtr->GetModels();
	const size_t ModelCount = Models.Size();

	TArray<FSWBFModelData> AllModels;
	AllModels.Reserve(static_cast<int32>(ModelCount));

	int32 SkeletalCount = 0;
	int32 CollisionCount = 0;

	for (size_t mi = 0; mi < ModelCount; ++mi)
	{
		FSWBFModelData ModelData(Models[mi]);

		if (ModelData.bIsSkeletal)
		{
			++SkeletalCount;
		}
		if (ModelData.CollisionMesh.bValid)
		{
			++CollisionCount;
		}

		AllModels.Add(MoveTemp(ModelData));
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Extracted %d models (%d skeletal, %d with collision mesh)"),
		AllModels.Num(), SkeletalCount, CollisionCount);

	// Filter out invalid, skeletal, segment-less, and collision-only models
	for (FSWBFModelData& Model : AllModels)
	{
		if (!Model.bValid)
		{
			UE_LOG(LogZeroEditorTools, Warning, TEXT("Skipping invalid model: %s"), *Model.Name);
			continue;
		}
		if (Model.bIsSkeletal)
		{
			UE_LOG(LogZeroEditorTools, Log, TEXT("Skipping skeletal model: %s"), *Model.Name);
			continue;
		}
		if (Model.Name.EndsWith(TEXT("_collision"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogZeroEditorTools, Log, TEXT("Skipping collision-only model: %s"), *Model.Name);
			continue;
		}

		bool bHasValidSegment = false;
		for (const FSWBFSegmentData& Seg : Model.Segments)
		{
			if (Seg.bValid && Seg.Vertices.Num() > 0)
			{
				bHasValidSegment = true;
				break;
			}
		}
		if (!bHasValidSegment)
		{
			UE_LOG(LogZeroEditorTools, Warning, TEXT("Skipping model with no valid segments: %s"), *Model.Name);
			continue;
		}

		ValidModels.Add(MoveTemp(Model));
	}

	UE_LOG(LogZeroEditorTools, Log, TEXT("Processing %d/%d models (filtered skeletal, collision-only, invalid)"),
		ValidModels.Num(), AllModels.Num());

	LODGroups = GroupModelsByLOD(ValidModels);
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

int32 FSWBFMeshImporter::Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context)
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	const FString DestPath = SubstituteLevelName(Settings->MeshDestinationPath.Path, Context.LevelName);
	int32 SuccessCount = 0;
	TSet<FString> UsedMeshNames;

	for (int32 i = 0; i < LODGroups.Num(); ++i)
	{
		if (SlowTask.ShouldCancel())
		{
			break;
		}

		const FLODGroup& Group = LODGroups[i];
		FString SanitizedName = FSWBFNameSanitizer::SanitizeMeshName(Group.BaseName, Context.LevelName);

		// Resolve name collisions within this import
		if (UsedMeshNames.Contains(SanitizedName))
		{
			int32 LastUnderscore = INDEX_NONE;
			SanitizedName.FindLastChar(TEXT('_'), LastUnderscore);
			FString BasePart = SanitizedName.Left(LastUnderscore + 1);
			FString NumPart = SanitizedName.Mid(LastUnderscore + 1);
			int32 Suffix = FCString::Atoi(*NumPart);
			do
			{
				++Suffix;
				SanitizedName = FString::Printf(TEXT("%s%02d"), *BasePart, Suffix);
			}
			while (UsedMeshNames.Contains(SanitizedName));
		}
		UsedMeshNames.Add(SanitizedName);

		SlowTask.EnterProgressFrame(1.f,
			FText::Format(LOCTEXT("ImportMesh", "Importing mesh {0}/{1}: {2}"),
				FText::AsNumber(i + 1),
				FText::AsNumber(LODGroups.Num()),
				FText::FromString(SanitizedName)));

		// ------------------------------------------------------------------
		// Reuse check: skip or overwrite existing asset when enabled
		// ------------------------------------------------------------------
		if (Settings && Settings->bReuseExistingAssets)
		{
			FAssetData ExistingData;
			if (FindExistingAssetBySourceName(Group.BaseName, UStaticMesh::StaticClass()->GetClassPathName(), ExistingData))
			{
				UStaticMesh* Existing = Cast<UStaticMesh>(ExistingData.GetAsset());
				if (Existing)
				{
					if (Settings->bOverwriteExistingAssets)
					{
						// Overwrite in-place: rebuild geometry and material slots
						const FSWBFModelData* LOD0Model = Group.LODs[0].Value;
						TArray<FName> SlotNames = ComputeSlotNames(LOD0Model->Segments, SanitizedName, Context.LevelName);

						// Clear stale material slots before rebuild
						Existing->GetStaticMaterials().Empty();

						// Rebuild FMeshDescriptions per LOD
						TArray<FMeshDescription> MeshDescriptions;
						for (const auto& LODEntry : Group.LODs)
						{
							MeshDescriptions.Add(BuildMeshDescription(LODEntry.Value->Segments, SlotNames));
						}

						// Rebuild material slots with MIC assignment
						{
							int32 ValidIdx = 0;
							for (const FSWBFSegmentData& Seg : LOD0Model->Segments)
							{
								if (!Seg.bValid || Seg.Vertices.Num() == 0) continue;
								if (ValidIdx >= SlotNames.Num()) break;

								UMaterialInstanceConstant* MIC = nullptr;
								if (!Seg.MaterialDedupKey.IsEmpty())
								{
									MIC = Context.GetAsset<UMaterialInstanceConstant>(Seg.MaterialDedupKey);
								}
								Existing->GetStaticMaterials().Add(FStaticMaterial(MIC, SlotNames[ValidIdx], SlotNames[ValidIdx]));
								++ValidIdx;
							}
						}

						TArray<const FMeshDescription*> MeshDescPtrs;
						for (const FMeshDescription& Desc : MeshDescriptions)
						{
							MeshDescPtrs.Add(&Desc);
						}

						UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
						BuildParams.bBuildSimpleCollision = false;
						BuildParams.bMarkPackageDirty = true;
						Existing->BuildFromMeshDescriptions(MeshDescPtrs, BuildParams);
						Existing->CalculateExtendedBounds();
						SetupCollision(Existing, *LOD0Model);

						Existing->MarkPackageDirty();
						FAssetRegistryModule::AssetCreated(Existing);

						Context.AddAsset<UStaticMesh>(Group.BaseName.ToLower(), Existing);
						++Context.ReuseStats.Overwritten;

						UE_LOG(LogZeroEditorTools, Display,
							TEXT("Overwrote existing mesh: %s"), *Group.BaseName);
					}
					else
					{
						UE_LOG(LogZeroEditorTools, Display,
							TEXT("Reusing existing mesh: %s"), *Group.BaseName);
						Context.AddAsset<UStaticMesh>(Group.BaseName.ToLower(), Existing);
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
		UStaticMesh* CreatedMesh = CreateMeshAsset(
			Group, PackagePath, SanitizedName, Context);

		if (CreatedMesh)
		{
			++SuccessCount;
			Context.AddAsset<UStaticMesh>(Group.BaseName.ToLower(), CreatedMesh);
			++Context.ReuseStats.Created;
			if (!Context.FirstCreatedAsset)
			{
				Context.FirstCreatedAsset = CreatedMesh;
			}
		}
		else
		{
			++Context.ErrorCount;
			UE_LOG(LogZeroEditorTools, Warning, TEXT("Failed to create mesh asset: %s"), *SanitizedName);
		}
	}

	if (SuccessCount > 0)
	{
		Context.WrittenFolders.AddUnique(DestPath);
	}

	UE_LOG(LogZeroEditorTools, Log, TEXT("Imported %d/%d meshes to %s"),
		SuccessCount, LODGroups.Num(), *DestPath);
	return SuccessCount;
}

// ---------------------------------------------------------------------------
// GroupModelsByLOD
// ---------------------------------------------------------------------------

TArray<FSWBFMeshImporter::FLODGroup> FSWBFMeshImporter::GroupModelsByLOD(
	const TArray<FSWBFModelData>& Models)
{
	TMap<FString, FLODGroup> GroupMap;

	for (const FSWBFModelData& Model : Models)
	{
		FString BaseName = Model.Name;
		int32 LODLevel = 0;

		if (BaseName.EndsWith(TEXT("_lod2"), ESearchCase::IgnoreCase))
		{
			LODLevel = 1;
			BaseName.LeftChopInline(5);
		}
		else if (BaseName.EndsWith(TEXT("_lod3"), ESearchCase::IgnoreCase))
		{
			LODLevel = 2;
			BaseName.LeftChopInline(5);
		}
		else if (BaseName.EndsWith(TEXT("_lowd"), ESearchCase::IgnoreCase))
		{
			LODLevel = 3;
			BaseName.LeftChopInline(5);
		}

		const FString Key = BaseName.ToLower();

		FLODGroup& Group = GroupMap.FindOrAdd(Key);
		if (Group.BaseName.IsEmpty())
		{
			Group.BaseName = BaseName;
		}
		Group.LODs.Add(TPair<int32, const FSWBFModelData*>(LODLevel, &Model));
	}

	TArray<FLODGroup> Result;
	for (auto& Pair : GroupMap)
	{
		FLODGroup& Group = Pair.Value;

		Group.LODs.Sort([](const TPair<int32, const FSWBFModelData*>& A,
		                   const TPair<int32, const FSWBFModelData*>& B)
		{
			return A.Key < B.Key;
		});

		if (Group.LODs[0].Key != 0)
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Orphan LOD model '%s' has no parent -- importing as standalone mesh"),
				*Group.BaseName);
			Group.LODs[0].Key = 0;
		}

		Result.Add(MoveTemp(Group));
	}

	return Result;
}

// ---------------------------------------------------------------------------
// CreateMeshAsset
// ---------------------------------------------------------------------------

UStaticMesh* FSWBFMeshImporter::CreateMeshAsset(
	const FLODGroup& LODGroup,
	const FString& PackagePath,
	const FString& AssetName,
	const FSWBFImportContext& Context)
{
	const FString& LevelName = Context.LevelName;
	const FString& SourceFilePath = Context.SourceFilePath;
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

	const FSWBFModelData* LOD0Model = LODGroup.LODs[0].Value;
	TArray<FName> SlotNames = ComputeSlotNames(LOD0Model->Segments, AssetName, LevelName);

	// Build FMeshDescription per LOD
	TArray<FMeshDescription> MeshDescriptions;
	for (const auto& LODEntry : LODGroup.LODs)
	{
		MeshDescriptions.Add(BuildMeshDescription(LODEntry.Value->Segments, SlotNames));
	}

	// Add material slots with MIC assignment via dedup key lookup
	{
		int32 ValidIdx = 0;
		for (const FSWBFSegmentData& Seg : LOD0Model->Segments)
		{
			if (!Seg.bValid || Seg.Vertices.Num() == 0)
			{
				continue;
			}
			if (ValidIdx >= SlotNames.Num())
			{
				break;
			}

			UMaterialInstanceConstant* MIC = nullptr;
			if (!Seg.MaterialDedupKey.IsEmpty())
			{
				MIC = Context.GetAsset<UMaterialInstanceConstant>(Seg.MaterialDedupKey);
				if (!MIC)
				{
					UE_LOG(LogZeroEditorTools, Warning,
						TEXT("Material not found for key: %s on mesh %s slot %s"),
						*Seg.MaterialDedupKey, *AssetName, *SlotNames[ValidIdx].ToString());
				}
			}

			StaticMesh->GetStaticMaterials().Add(FStaticMaterial(MIC, SlotNames[ValidIdx], SlotNames[ValidIdx]));
			++ValidIdx;
		}
	}

	TArray<const FMeshDescription*> MeshDescPtrs;
	for (const FMeshDescription& Desc : MeshDescriptions)
	{
		MeshDescPtrs.Add(&Desc);
	}

	UStaticMesh::FBuildMeshDescriptionsParams BuildParams;
	BuildParams.bBuildSimpleCollision = false;
	BuildParams.bMarkPackageDirty = true;
	StaticMesh->BuildFromMeshDescriptions(MeshDescPtrs, BuildParams);

	StaticMesh->CalculateExtendedBounds();

	SetupCollision(StaticMesh, *LOD0Model);

	USWBFMeshAssetUserData::AttachToMesh(StaticMesh, *LOD0Model, LODGroup.LODs.Num(), SlotNames, LevelName);
	FinalizeAsset(StaticMesh, LODGroup.BaseName, LevelName, SourceFilePath, /*bSkipMetadata=*/true);

	return StaticMesh;
}

// ---------------------------------------------------------------------------
// ComputeSlotNames
// ---------------------------------------------------------------------------

TArray<FName> FSWBFMeshImporter::ComputeSlotNames(
	const TArray<FSWBFSegmentData>& Segments,
	const FString& AssetName,
	const FString& LevelName)
{
	TArray<FName> SlotNames;
	int32 SlotIdx = 0;

	for (const FSWBFSegmentData& Seg : Segments)
	{
		if (!Seg.bValid || Seg.Vertices.Num() == 0)
		{
			continue;
		}

		FName SlotName;
		if (Seg.TextureNames.Num() > 0)
		{
			SlotName = FName(*FSWBFNameSanitizer::SanitizeMaterialName(Seg.TextureNames[0], LevelName));
		}
		else
		{
			SlotName = FName(*FString::Printf(TEXT("MI_%s_Seg%02d"), *AssetName, SlotIdx));
		}

		SlotNames.Add(SlotName);
		++SlotIdx;
	}

	return SlotNames;
}

// ---------------------------------------------------------------------------
// BuildMeshDescription
// ---------------------------------------------------------------------------

FMeshDescription FSWBFMeshImporter::BuildMeshDescription(
	const TArray<FSWBFSegmentData>& Segments,
	const TArray<FName>& SlotNames)
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
	TPolygonGroupAttributesRef<FName> PolygonGroupMaterialSlotNames =
		MeshDesc.PolygonGroupAttributes().GetAttributesRef<FName>(MeshAttribute::PolygonGroup::ImportedMaterialSlotName);

	int32 TotalVertices = 0;
	int32 TotalTriangles = 0;
	int32 ValidSegmentCount = 0;
	for (const FSWBFSegmentData& Seg : Segments)
	{
		if (!Seg.bValid || Seg.Vertices.Num() == 0)
		{
			continue;
		}
		TotalVertices += Seg.Vertices.Num();
		TotalTriangles += Seg.Indices.Num() / 3;
		++ValidSegmentCount;
	}

	MeshDesc.ReserveNewVertices(TotalVertices);
	MeshDesc.ReserveNewVertexInstances(TotalVertices);
	MeshDesc.ReserveNewPolygons(TotalTriangles);
	MeshDesc.ReserveNewEdges(TotalTriangles * 3);
	MeshDesc.ReserveNewPolygonGroups(ValidSegmentCount);

	int32 MaterialSlotIndex = 0;

	for (const FSWBFSegmentData& Seg : Segments)
	{
		if (!Seg.bValid || Seg.Vertices.Num() == 0)
		{
			continue;
		}

		const FPolygonGroupID PolyGroupID = MeshDesc.CreatePolygonGroup();
		FName SlotName = (MaterialSlotIndex < SlotNames.Num())
			? SlotNames[MaterialSlotIndex]
			: FName(*FString::Printf(TEXT("Material_%d"), MaterialSlotIndex));
		PolygonGroupMaterialSlotNames[PolyGroupID] = SlotName;
		++MaterialSlotIndex;

		TArray<FVertexID> VertexIDs;
		TArray<FVertexInstanceID> VertexInstanceIDs;
		VertexIDs.Reserve(Seg.Vertices.Num());
		VertexInstanceIDs.Reserve(Seg.Vertices.Num());

		for (int32 i = 0; i < Seg.Vertices.Num(); ++i)
		{
			const FVertexID VertID = MeshDesc.CreateVertex();
			VertexPositions[VertID] = FVector3f(Seg.Vertices[i]);
			VertexIDs.Add(VertID);

			const FVertexInstanceID VertInstID = MeshDesc.CreateVertexInstance(VertID);

			if (i < Seg.Normals.Num())
			{
				VertexInstanceNormals[VertInstID] = FVector3f(Seg.Normals[i]);
			}
			else
			{
				VertexInstanceNormals[VertInstID] = FVector3f(0.f, 0.f, 1.f);
			}

			if (i < Seg.UVs.Num())
			{
				VertexInstanceUVs.Set(VertInstID, 0, FVector2f(Seg.UVs[i]));
			}
			else
			{
				VertexInstanceUVs.Set(VertInstID, 0, FVector2f(0.f, 0.f));
			}

			VertexInstanceIDs.Add(VertInstID);
		}

		for (int32 i = 0; i + 2 < Seg.Indices.Num(); i += 3)
		{
			const uint32 Idx0 = Seg.Indices[i];
			const uint32 Idx1 = Seg.Indices[i + 1];
			const uint32 Idx2 = Seg.Indices[i + 2];

			if (Idx0 >= static_cast<uint32>(VertexInstanceIDs.Num()) ||
				Idx1 >= static_cast<uint32>(VertexInstanceIDs.Num()) ||
				Idx2 >= static_cast<uint32>(VertexInstanceIDs.Num()))
			{
				UE_LOG(LogZeroEditorTools, Warning,
					TEXT("Triangle index out of bounds: %u, %u, %u (max %d) -- skipping triangle"),
					Idx0, Idx1, Idx2, VertexInstanceIDs.Num() - 1);
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
	}

	return MeshDesc;
}

// ---------------------------------------------------------------------------
// SetupCollision
// ---------------------------------------------------------------------------

void FSWBFMeshImporter::SetupCollision(UStaticMesh* StaticMesh, const FSWBFModelData& PrimaryModel)
{
	const bool bHasCollisionMesh = PrimaryModel.CollisionMesh.bValid &&
		PrimaryModel.CollisionMesh.Vertices.Num() > 0 &&
		PrimaryModel.CollisionMesh.Indices.Num() > 0;
	const bool bHasPrimitives = PrimaryModel.CollisionPrimitives.Num() > 0;

	if (!bHasCollisionMesh && !bHasPrimitives)
	{
		return;
	}

	StaticMesh->CreateBodySetup();
	UBodySetup* BodySetup = StaticMesh->GetBodySetup();
	if (!BodySetup)
	{
		UE_LOG(LogZeroEditorTools, Warning, TEXT("Failed to create BodySetup for: %s"), *PrimaryModel.Name);
		return;
	}

	if (bHasCollisionMesh)
	{
		UE_LOG(LogZeroEditorTools, Log,
			TEXT("Model '%s' has dedicated collision mesh -- using render mesh as complex collision for v1"),
			*PrimaryModel.Name);
		BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
	}

	int32 PrimCount = 0;
	for (const FSWBFCollisionPrimitiveData& Prim : PrimaryModel.CollisionPrimitives)
	{
		if (!Prim.bValid)
		{
			continue;
		}

		switch (Prim.Shape)
		{
		case FSWBFCollisionPrimitiveData::EShape::Box:
		{
			FKBoxElem BoxElem;
			BoxElem.Center = Prim.Position;
			BoxElem.Rotation = Prim.Rotation.Rotator();
			BoxElem.X = Prim.Dimensions.X * 2.0f;
			BoxElem.Y = Prim.Dimensions.Y * 2.0f;
			BoxElem.Z = Prim.Dimensions.Z * 2.0f;
			BodySetup->AggGeom.BoxElems.Add(BoxElem);
			++PrimCount;
			break;
		}
		case FSWBFCollisionPrimitiveData::EShape::Sphere:
		{
			FKSphereElem SphereElem;
			SphereElem.Center = Prim.Position;
			SphereElem.Radius = Prim.Dimensions.X;
			BodySetup->AggGeom.SphereElems.Add(SphereElem);
			++PrimCount;
			break;
		}
		case FSWBFCollisionPrimitiveData::EShape::Cylinder:
		{
			FKSphylElem CapsuleElem;
			CapsuleElem.Center = Prim.Position;
			CapsuleElem.Rotation = Prim.Rotation.Rotator();
			CapsuleElem.Radius = Prim.Dimensions.X;
			CapsuleElem.Length = Prim.Dimensions.Y;
			BodySetup->AggGeom.SphylElems.Add(CapsuleElem);
			++PrimCount;
			break;
		}
		}
	}

	if (PrimCount > 0)
	{
		BodySetup->bCreatedPhysicsMeshes = false;

		if (!bHasCollisionMesh)
		{
			BodySetup->CollisionTraceFlag = CTF_UseDefault;
		}

		BodySetup->InvalidatePhysicsData();
		BodySetup->CreatePhysicsMeshes();

		UE_LOG(LogZeroEditorTools, Log, TEXT("Added %d collision primitives to: %s"),
			PrimCount, *PrimaryModel.Name);
	}
}

#undef LOCTEXT_NAMESPACE