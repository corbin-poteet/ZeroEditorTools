// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Forward-declare LibSWBF2 types to keep third-party headers out of public interface
namespace LibSWBF2::Wrappers
{
	class Level;
}

/**
 * RAII wrapper for LibSWBF2 Level lifetime management.
 *
 * Opens a munged LVL file and exposes the raw Level pointer for importers
 * to extract their own data. Automatically calls Level::Destroy on
 * scope exit or explicit Close().
 *
 * Non-copyable, movable.
 */
class FSWBFLevelWrapper
{
public:
	FSWBFLevelWrapper() = default;
	~FSWBFLevelWrapper();

	// Non-copyable
	FSWBFLevelWrapper(const FSWBFLevelWrapper&) = delete;
	FSWBFLevelWrapper& operator=(const FSWBFLevelWrapper&) = delete;

	// Movable
	FSWBFLevelWrapper(FSWBFLevelWrapper&& Other) noexcept;
	FSWBFLevelWrapper& operator=(FSWBFLevelWrapper&& Other) noexcept;

	/** Open a LVL file. Returns false on failure. */
	bool OpenFile(const FString& FilePath);

	/** Returns true if this is a world-type LVL (contains world/instance scene graph). */
	bool IsWorldLevel() const;

	/** Returns true if a LVL file is currently loaded. */
	bool IsValid() const;

	/** Explicitly close the loaded LVL file and release resources. */
	void Close();

	/** Access the raw LibSWBF2 Level pointer. Only valid while this wrapper is alive. */
	const LibSWBF2::Wrappers::Level* GetLevel() const { return LevelPtr; }

	/** Get the name of the first (base) world in m_Worlds. Empty if no worlds. */
	FString GetBaseWorldName() const;

private:
	LibSWBF2::Wrappers::Level* LevelPtr = nullptr;
};
