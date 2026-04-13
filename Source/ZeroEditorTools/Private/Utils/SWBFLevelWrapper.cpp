// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFLevelWrapper.h"
#include "ZeroEditorTools.h"
#include "Misc/Paths.h"

THIRD_PARTY_INCLUDES_START
#include <LibSWBF2/Wrappers/Level.h>
#include <LibSWBF2/Wrappers/World.h>
#include <LibSWBF2/Types/LibString.h>
THIRD_PARTY_INCLUDES_END

using LibSWBF2::Wrappers::Level;
using LibSWBF2::Wrappers::World;
using LibSWBF2::Types::String;

FSWBFLevelWrapper::~FSWBFLevelWrapper()
{
	Close();
}

FSWBFLevelWrapper::FSWBFLevelWrapper(FSWBFLevelWrapper&& Other) noexcept
	: LevelPtr(Other.LevelPtr)
{
	Other.LevelPtr = nullptr;
}

FSWBFLevelWrapper& FSWBFLevelWrapper::operator=(FSWBFLevelWrapper&& Other) noexcept
{
	if (this != &Other)
	{
		Close();
		LevelPtr = Other.LevelPtr;
		Other.LevelPtr = nullptr;
	}
	return *this;
}

bool FSWBFLevelWrapper::OpenFile(const FString& FilePath)
{
	// Close any previously opened level
	Close();

	// Resolve to absolute path so LibSWBF2 can find the file
	const FString AbsolutePath = FPaths::ConvertRelativePathToFull(FilePath);

	// Convert FString to ANSI for LibSWBF2
	const char* AnsiPath = TCHAR_TO_ANSI(*AbsolutePath);

	// LibSWBF2's FromFile takes a LibSWBF2::Types::String
	String LibPath(AnsiPath);
	LevelPtr = Level::FromFile(LibPath);

	if (LevelPtr == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Failed to open LVL file: %s"), *AbsolutePath);
		return false;
	}

	UE_LOG(LogZeroEditorTools, Log, TEXT("Successfully opened LVL file: %s"), *AbsolutePath);
	return true;
}

bool FSWBFLevelWrapper::IsWorldLevel() const
{
	if (LevelPtr == nullptr)
	{
		return false;
	}
	return LevelPtr->IsWorldLevel();
}

bool FSWBFLevelWrapper::IsValid() const
{
	return LevelPtr != nullptr;
}

FString FSWBFLevelWrapper::GetBaseWorldName() const
{
	if (LevelPtr == nullptr)
	{
		return FString();
	}

	const auto& Worlds = LevelPtr->GetWorlds();
	if (Worlds.Size() == 0)
	{
		return FString();
	}

	return FString(ANSI_TO_TCHAR(Worlds[0].GetName().Buffer()));
}

void FSWBFLevelWrapper::Close()
{
	if (LevelPtr != nullptr)
	{
		Level::Destroy(LevelPtr);
		LevelPtr = nullptr;
		UE_LOG(LogZeroEditorTools, Log, TEXT("LVL file closed and resources released."));
	}
}
