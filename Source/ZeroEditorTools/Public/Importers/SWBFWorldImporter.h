// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFImporterBase.h"

namespace LibSWBF2 { namespace Wrappers { class Instance; } }

/**
 * Holds extracted instance placement data from a SWBF world scene graph.
 * Position and rotation are coordinate-converted to UE space.
 */
struct FSWBFInstanceData
{
	/** Instance name (e.g., "tat3_bldg_bunker_01"). */
	FString Name;

	/** SWBF World this datum belongs to (for DataLayer assignment). */
	FString WorldName;

	/** ODF entity class name (e.g., "tat3_bldg_bunker"). */
	FString EntityClassName;

	/** World position converted to UE coordinate space. */
	FVector Position = FVector::ZeroVector;

	/** Rotation converted to UE coordinate space. */
	FQuat Rotation = FQuat::Identity;

	/** Scale. */
	FVector Scale = FVector(1.0f, 1.0f, 1.0f);

	/** Original SWBF position before coordinate conversion (Y-up space). */
	FVector OriginalPosition = FVector::ZeroVector;

	/** Original SWBF rotation as raw Vector4 (X, Y, Z, W) before coordinate conversion. */
	FVector4 OriginalRotation = FVector4(0, 0, 0, 1);

	/** Original SWBF 3x3 rotation matrix as a formatted string. */
	FString OriginalRotationMatrix;

	/** Override properties from the SWBF instance chunk (resolved name -> value). */
	TMap<FString, FString> OverrideProperties;

	/** Whether the instance data was successfully extracted. */
	bool bValid = false;

	/** Default constructor. */
	FSWBFInstanceData() = default;

	/** Construct from LibSWBF2 wrapper, populating all fields. */
	explicit FSWBFInstanceData(const LibSWBF2::Wrappers::Instance& InInstance, const FString& InWorldName);

	/** Returns a compact one-line summary for debug logging. */
	FString ToString() const;
};

/**
 * Spawns AStaticMeshActor instances in the current editor level from SWBF instance data.
 * Resolves entity class names to imported UStaticMesh assets via the shared MeshLookup.
 * Only runs for world-type LVL files.
 */
class FSWBFWorldImporter : public FSWBFImporterBase
{
public:
	virtual FString GetDisplayName() const override { return TEXT("actors"); }
	virtual FString GetDestSubfolder() const override { return FString(); }
	virtual bool ShouldImport() const override;
	virtual void GetData(const FSWBFLevelWrapper& LevelWrapper) override;
	virtual int32 GetWorkCount() const override { return InstanceData.Num(); }
	virtual int32 Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context) override;
	virtual void CollectWorldNames(TSet<FString>& OutWorldNames) const override;

private:
	TArray<FSWBFInstanceData> InstanceData;
};
