// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFCoordinateUtils.h"

FVector FSWBFCoordinateUtils::ConvertPosition(float X, float Y, float Z)
{
	// SWBF is Y-up, UE is Z-up — remap axes with X/Z negation to maintain left-handed orientation
	// SWBF (X, Y, Z) -> UE (-X, -Z, Y) with unit scaling
	return FVector(
		-X * UnitScale,
		-Z * UnitScale,
		Y * UnitScale
	);
}

FQuat FSWBFCoordinateUtils::ConvertRotation(float X, float Y, float Z, float W)
{
	// Position mapping: UE = (-SWBF_X, -SWBF_Z, SWBF_Y)
	// SWBF quat (X, Y, Z, W) -> UE quat (X, -Z, Y, W)
	return FQuat(X, -Z, Y, W);
}

FVector FSWBFCoordinateUtils::ConvertScale(float X, float Y, float Z)
{
	// Scale remaps axes but no unit scaling (scale is unitless)
	// SWBF (X, Y, Z) -> UE (X, Z, Y)
	// No sign flip for scale -- negative scale would flip geometry
	return FVector(X, Z, Y);
}

FVector FSWBFCoordinateUtils::ConvertSize(float X, float Y, float Z)
{
	// Size/extent remap: axis swap only (no sign negation, sizes are positive magnitudes)
	// SWBF (X, Y, Z) -> UE (X, Z, Y) with unit scaling (meters to cm)
	return FVector(X * UnitScale, Z * UnitScale, Y * UnitScale);
}