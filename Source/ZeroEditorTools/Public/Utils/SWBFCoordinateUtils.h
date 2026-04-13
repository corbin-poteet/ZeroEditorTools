// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Static utility functions for converting SWBF coordinate space to UE coordinate space.
 *
 * SWBF uses a left-handed Y-up coordinate system (similar to DirectX).
 * UE uses a left-handed Z-up coordinate system.
 *
 * Axis remapping: SWBF (X, Y, Z) -> UE (-X, -Z, Y)
 *   SWBF X (right)    -> UE -X
 *   SWBF Y (up)       -> UE Z (up)
 *   SWBF Z (forward)  -> UE -Y
 *
 * NOTE: These conversions are best-effort derived from indirect sources (Unity
 * reference implementations and coordinate system analysis). They WILL be
 * validated and refined in later phases when mesh vertex data provides ground
 * truth for verification.
 */
struct FSWBFCoordinateUtils
{
	/** 1 SWBF meter = 100 UE centimeters */
	static constexpr float UnitScale = 100.0f;

	/**
	 * Convert a SWBF position (Y-up) to UE position (Z-up).
	 * Applies axis remapping and unit scaling.
	 */
	static FVector ConvertPosition(float X, float Y, float Z);

	/**
	 * Convert a SWBF quaternion rotation (Y-up) to UE quaternion (Z-up).
	 * Applies axis remapping with handedness correction.
	 */
	static FQuat ConvertRotation(float X, float Y, float Z, float W);

	/**
	 * Convert a SWBF scale to UE scale. No unit scaling applied (scale is unitless).
	 * Axes are remapped to match the coordinate system change.
	 */
	static FVector ConvertScale(float X, float Y, float Z);

	/**
	 * Convert a SWBF size/extent to UE space.
	 * Applies axis remapping (SWBF Y-up to UE Z-up) and unit scaling.
	 * Unlike ConvertPosition, no sign negation is applied (sizes are always positive).
	 * SWBF (X, Y, Z) -> UE (X, Z, Y) * UnitScale
	 */
	static FVector ConvertSize(float X, float Y, float Z);
};
