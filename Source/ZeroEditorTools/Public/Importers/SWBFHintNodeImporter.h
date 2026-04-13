// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFImporterBase.h"

namespace LibSWBF2 { namespace Wrappers { class HintNode; } }

/**
 * Holds extracted hint node data from a SWBF world.
 * Position and rotation are coordinate-converted to UE space.
 */
struct FSWBFHintNodeData
{
	/** Hint node name from SWBF data. */
	FString Name;

	/** SWBF World this datum belongs to (for DataLayer assignment). */
	FString WorldName;

	/** World position converted to UE coordinate space. */
	FVector Position = FVector::ZeroVector;

	/** Rotation converted to UE coordinate space. */
	FQuat Rotation = FQuat::Identity;

	/** Hint node type ID (0=Generic, 1=Access, 2=JetJump, 3=Snipe). */
	uint16 Type = 0;

	/** Optional SWBF properties (FNVHash + value string pairs). */
	TArray<TPair<uint32, FString>> Properties;

	/** Whether the hint node data was successfully extracted. */
	bool bValid = false;

	/** Default constructor. */
	FSWBFHintNodeData() = default;

	/** Construct from LibSWBF2 wrapper, populating all fields. */
	explicit FSWBFHintNodeData(const LibSWBF2::Wrappers::HintNode& InHintNode, const FString& InWorldName);

	/** Returns a compact one-line summary for debug logging. */
	FString ToString() const;
};

/**
 * Spawns hint node marker actors in the editor level from SWBF hint node data.
 * Each actor has a color-coded billboard icon and floating type label text.
 * Known types display human-readable names; unknown types show "Type N".
 * Only runs for world-type LVL files.
 */
class FSWBFHintNodeImporter : public FSWBFImporterBase
{
public:
	virtual FString GetDisplayName() const override { return TEXT("hint nodes"); }
	virtual FString GetDestSubfolder() const override { return FString(); }
	virtual bool ShouldImport() const override;
	virtual void GetData(const FSWBFLevelWrapper& LevelWrapper) override;
	virtual int32 GetWorkCount() const override { return HintNodeData.Num(); }
	virtual int32 Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context) override;
	virtual void CollectWorldNames(TSet<FString>& OutWorldNames) const override;

private:
	TArray<FSWBFHintNodeData> HintNodeData;
};
