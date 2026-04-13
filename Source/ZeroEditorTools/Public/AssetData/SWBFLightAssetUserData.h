// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFAssetUserData.h"
#include "SWBFLightAssetUserData.generated.h"

struct FSWBFLightData;
class AActor;

/**
 * Light-specific metadata for actors spawned from SWBF lighting configs.
 * Extends base SWBF metadata with light type, color, range, and cone angles.
 */
UCLASS()
class ZEROEDITORTOOLS_API USWBFLightAssetUserData : public USWBFAssetUserData
{
	GENERATED_BODY()

public:
	/** SWBF light type: 1=Directional, 2=Point/Omni, 3=Spot. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	int32 LightType = 0;

	/** Light color in 0-1 range. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FLinearColor Color = FLinearColor::White;

	/** Attenuation radius in UE units. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	float Range = 0.0f;

	/** Spot light outer cone angle in degrees. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	float OuterConeAngle = 0.0f;

	/** Spot light inner cone angle in degrees. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	float InnerConeAngle = 0.0f;

	/** Light intensity. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	float Intensity = 1.0f;

	/** True if light matches GlobalLights.Light1 or Light2 name. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	bool bIsGlobal = false;

	/** Raw config fields from the SWBF lighting scope (field name -> data string). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	TMap<FString, FString> ConfigFields;

	/**
	 * Creates and attaches a USWBFLightAssetUserData instance to the given actor's root component.
	 * Returns nullptr if Actor is null or has no root component.
	 */
	static USWBFLightAssetUserData* AttachToActor(
		AActor* Actor,
		const FSWBFLightData& LightData,
		const FString& InLevelName);
};
