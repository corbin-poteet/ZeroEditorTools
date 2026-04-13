// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFAssetUserData.h"
#include "SWBFInstanceAssetUserData.generated.h"

struct FSWBFInstanceData;
class AActor;

/**
 * Instance-specific metadata for actors spawned from SWBF world instance data.
 * Extends base SWBF metadata with entity class name and mirror flag.
 */
UCLASS()
class ZEROEDITORTOOLS_API USWBFInstanceAssetUserData : public USWBFAssetUserData
{
	GENERATED_BODY()

public:
	/** ODF entity class name from the SWBF world scene graph (e.g., "tat3_bldg_bunker"). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FString EntityClassName;

	/** True if the instance has a mirrored (negative determinant) scale transform. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	bool bIsMirrored = false;

	/** Original SWBF position before coordinate conversion (Y-up space). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import|Original Transform")
	FVector OriginalPosition = FVector::ZeroVector;

	/** Original SWBF rotation as raw Vector4 (X, Y, Z, W) before coordinate conversion. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import|Original Transform")
	FVector4 OriginalRotation = FVector4(0, 0, 0, 1);

	/** Original SWBF 3x3 rotation matrix (row-major, as stored in the LVL file). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import|Original Transform")
	FString OriginalRotationMatrix;

	/** UE-converted position (for comparison with actor world position). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import|Converted Transform")
	FVector ConvertedPosition = FVector::ZeroVector;

	/** UE-converted rotation (for comparison with actor world rotation). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import|Converted Transform")
	FQuat ConvertedRotation = FQuat::Identity;

	/** Override properties from the SWBF instance chunk (property name -> value). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	TMap<FString, FString> OverrideProperties;

	/**
	 * Creates and attaches a USWBFInstanceAssetUserData instance to the given actor's root component.
	 * Returns nullptr if Actor is null or has no root component.
	 */
	static USWBFInstanceAssetUserData* AttachToActor(
		AActor* Actor,
		const FSWBFInstanceData& InstData,
		const FString& InLevelName);
};
