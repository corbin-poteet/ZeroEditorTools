// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFAssetUserData.h"
#include "SWBFRegionAssetUserData.generated.h"

struct FSWBFRegionData;
class AActor;

/**
 * Region-specific metadata for actors spawned from SWBF region volumes.
 * Extends base SWBF metadata with region type, geometry, and named properties.
 */
UCLASS()
class ZEROEDITORTOOLS_API USWBFRegionAssetUserData : public USWBFAssetUserData
{
	GENERATED_BODY()

public:
	/** Region shape type: "box", "sphere", or "cylinder". */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FString RegionType;

	/** UE-converted world position of the region. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FVector Position = FVector::ZeroVector;

	/** UE-converted rotation of the region. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FQuat Rotation = FQuat::Identity;

	/** Box half-extents in UE units (only meaningful for box type). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FVector Size = FVector::ZeroVector;

	/** Sphere or cylinder radius in UE units. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	float Radius = 0.0f;

	/** Cylinder half-height in UE units. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	float HalfHeight = 0.0f;

	/** Region properties keyed by hex hash string (e.g. "0x12345678" -> "value"). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	TMap<FString, FString> Properties;

	/** Named class of this region (e.g. "soundspace", "deathregion", "generic").
	 *  NOT the geometry shape type -- that is RegionType. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FString RegionClass;

	/** Inline parameters from the region name string (key=value pairs). Empty for generic regions. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	TMap<FString, FString> InlineParams;

	/**
	 * Creates and attaches a USWBFRegionAssetUserData instance to the given actor's root component.
	 * Returns nullptr if Actor is null or has no root component.
	 */
	static USWBFRegionAssetUserData* AttachToActor(
		AActor* Actor,
		const FSWBFRegionData& RegionData,
		const FString& InLevelName);
};
