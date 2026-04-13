// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFImporterBase.h"

class UStaticMesh;
struct FMeshDescription;
namespace LibSWBF2 { namespace Wrappers { class Model; class Segment; class CollisionMesh; class CollisionPrimitive; } }

/**
 * Holds extracted segment geometry data (vertices, normals, UVs, indices) from a single model segment.
 * All spatial data is coordinate-converted to UE space.
 */
struct FSWBFSegmentData
{
	TArray<FVector> Vertices;       // Converted to UE coordinate space
	TArray<FVector> Normals;        // Converted to UE coordinate space (no unit scale)
	TArray<FVector2D> UVs;
	TArray<uint32> Indices;         // Triangle list (converted from strip if needed, uint16->uint32)
	FString MaterialName;           // Original SWBF material name
	TArray<FString> TextureNames;   // Referenced texture names from material
	FString MaterialDedupKey;       // Pre-computed dedup key for MaterialLookup (empty if textureless)
	bool bValid = false;

	/** Default constructor. */
	FSWBFSegmentData() = default;

	/** Construct from LibSWBF2 wrapper, populating all fields. */
	explicit FSWBFSegmentData(const LibSWBF2::Wrappers::Segment& InSegment);

	/** Returns a compact one-line summary for debug logging. */
	FString ToString() const;
};

/**
 * Holds extracted collision mesh data (triangle soup) from a model.
 * All spatial data is coordinate-converted to UE space.
 */
struct FSWBFCollisionMeshData
{
	TArray<FVector> Vertices;       // Converted to UE coordinate space
	TArray<uint32> Indices;         // Triangle list
	bool bValid = false;

	/** Default constructor. */
	FSWBFCollisionMeshData() = default;

	/** Construct from LibSWBF2 wrapper, populating all fields. */
	explicit FSWBFCollisionMeshData(const LibSWBF2::Wrappers::CollisionMesh& InCollisionMesh);

	/** Returns a compact one-line summary for debug logging. */
	FString ToString() const;
};

/**
 * Holds extracted collision primitive data (box, sphere, cylinder) from a model.
 * All spatial data is coordinate-converted to UE space.
 */
struct FSWBFCollisionPrimitiveData
{
	enum class EShape : uint8 { Box, Sphere, Cylinder };
	EShape Shape = EShape::Box;
	FVector Position = FVector::ZeroVector;  // Converted to UE space
	FQuat Rotation = FQuat::Identity;        // Converted to UE space
	FVector Dimensions = FVector::ZeroVector; // Box: half-extents; Sphere: X=radius; Cylinder: X=radius, Y=height (UE scaled)
	bool bValid = false;

	/** Default constructor. */
	FSWBFCollisionPrimitiveData() = default;

	/** Construct from LibSWBF2 wrapper, populating all fields. */
	explicit FSWBFCollisionPrimitiveData(const LibSWBF2::Wrappers::CollisionPrimitive& InPrimitive);

	/** Returns a compact one-line summary for debug logging. */
	FString ToString() const;
};

/**
 * Holds all extracted data for a single model from a SWBF LVL file.
 * Includes geometry segments, collision mesh, and collision primitives.
 */
struct FSWBFModelData
{
	FString Name;                   // Raw model name from LibSWBF2
	TArray<FSWBFSegmentData> Segments;
	FSWBFCollisionMeshData CollisionMesh;
	TArray<FSWBFCollisionPrimitiveData> CollisionPrimitives;
	bool bIsSkeletal = false;
	bool bValid = false;

	/** Default constructor. */
	FSWBFModelData() = default;

	/** Construct from LibSWBF2 wrapper, populating all fields. */
	explicit FSWBFModelData(const LibSWBF2::Wrappers::Model& InModel);

	/** Returns a compact one-line summary for debug logging. */
	FString ToString() const;
};

/**
 * Imports model data from a SWBF LVL file as UStaticMesh assets.
 * Handles LOD grouping, multi-segment meshes, collision primitives,
 * material slot assignment, and name deduplication.
 */
class FSWBFMeshImporter : public FSWBFImporterBase
{
public:
	/** Groups models by base name, associating LOD variants with their parent. */
	struct FLODGroup
	{
		FString BaseName;
		TArray<TPair<int32, const FSWBFModelData*>> LODs;
	};

	virtual FString GetDisplayName() const override { return TEXT("meshes"); }
	virtual FString GetDestSubfolder() const override { return TEXT("Meshes"); }
	virtual FString GetResolvedDestPath(const FString& LevelName) const override;
	virtual bool ShouldImport() const override;
	virtual void GetData(const FSWBFLevelWrapper& LevelWrapper) override;
	virtual int32 GetWorkCount() const override { return LODGroups.Num(); }
	virtual int32 Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context) override;

private:
	/** Validated models kept alive for LODGroup pointers. */
	TArray<FSWBFModelData> ValidModels;

	/** LOD groups built from ValidModels during GetData. */
	TArray<FLODGroup> LODGroups;

	static TArray<FLODGroup> GroupModelsByLOD(const TArray<FSWBFModelData>& Models);

	static UStaticMesh* CreateMeshAsset(
		const FLODGroup& LODGroup,
		const FString& PackagePath,
		const FString& AssetName,
		const FSWBFImportContext& Context);

	static FMeshDescription BuildMeshDescription(
		const TArray<FSWBFSegmentData>& Segments,
		const TArray<FName>& SlotNames);

	static TArray<FName> ComputeSlotNames(
		const TArray<FSWBFSegmentData>& Segments,
		const FString& AssetName,
		const FString& LevelName);

	static void SetupCollision(UStaticMesh* StaticMesh, const FSWBFModelData& PrimaryModel);
};