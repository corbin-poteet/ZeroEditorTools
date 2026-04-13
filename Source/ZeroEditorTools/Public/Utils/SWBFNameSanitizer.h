// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Utility class for converting raw SWBF asset names to UE-compliant names.
 *
 * Naming rules:
 * - Prefixes: T_ for textures, SM_ for meshes, M_ for materials
 * - Level prefix segments (3-4 alpha + 1+ digit, e.g. cor1, tat3) are ALL CAPS
 * - Remaining segments are PascalCase
 * - Trailing numbers on segments are separated with underscore (column01 -> Column_01)
 */
class FSWBFNameSanitizer
{
public:
	/** Convert a raw SWBF texture name to UE-compliant name with T_ prefix and level name. */
	static FString SanitizeTextureName(const FString& RawName, const FString& LevelName);

	/** Convert a raw SWBF mesh name to UE-compliant name with SM_ prefix and level name. */
	static FString SanitizeMeshName(const FString& RawName, const FString& LevelName);

	/** Convert a raw SWBF material name to UE-compliant name with M_ prefix and level name. */
	static FString SanitizeMaterialName(const FString& RawName, const FString& LevelName);

private:
	/** Core sanitization logic shared by all asset types. */
	static FString SanitizeName(const FString& RawName, const FString& Prefix, const FString& LevelName);

	/** Convert a segment to PascalCase (first char upper, rest lower). */
	static FString PascalCaseSegment(const FString& Segment);

	/** Separate trailing numeric digits from a segment with an underscore. */
	static FString SeparateTrailingNumbers(const FString& Segment);

	/** Check if a segment matches the level prefix pattern (3-4 alpha chars + 1+ digit). */
	static bool IsLevelPrefix(const FString& Segment);
};
