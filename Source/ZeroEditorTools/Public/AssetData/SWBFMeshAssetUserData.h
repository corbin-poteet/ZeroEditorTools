// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFAssetUserData.h"
#include "SWBFMeshAssetUserData.generated.h"

struct FSWBFModelData;
class UStaticMesh;

/**
 * Mesh-specific metadata for imported SWBF static mesh assets.
 * Extends base SWBF metadata with geometry and collision properties from the original model.
 */
UCLASS()
class ZEROEDITORTOOLS_API USWBFMeshAssetUserData : public USWBFAssetUserData
{
	GENERATED_BODY()

public:
	/** Number of valid geometry segments in the LOD0 model. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	int32 SegmentCount = 0;

	/** Number of LOD levels imported for this mesh. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	int32 LODCount = 0;

	/** True if the model had a collision mesh (SWBF collision geometry). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	bool bHasCollisionMesh = false;

	/** Number of collision primitives (boxes, spheres, cylinders) on the model. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	int32 CollisionPrimitiveCount = 0;

	/** True if the original model was flagged as skeletal in SWBF. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	bool bIsSkeletal = false;

	/** Material slot names assigned to this mesh (one per valid segment). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	TArray<FString> MaterialSlotNames;

	/**
	 * Creates and attaches a USWBFMeshAssetUserData instance to the given static mesh.
	 * Returns nullptr if Mesh is null or does not implement IInterface_AssetUserData.
	 */
	static USWBFMeshAssetUserData* AttachToMesh(
		UStaticMesh* Mesh,
		const FSWBFModelData& ModelData,
		int32 InLODCount,
		const TArray<FName>& SlotNames,
		const FString& InLevelName);
};
