// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFAssetUserData.h"
#include "SWBFMaterialAssetUserData.generated.h"

struct FSWBFMaterialData;

/** SWBF material render flags (bitmask from EMaterialFlags in LibSWBF2). */
UENUM(meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ESWBFMaterialFlag : uint32
{
	Normal          = 1,
	Hardedged       = 2,
	Transparent     = 4,
	Glossmap        = 8,
	Glow            = 16,
	BumpMap         = 32,
	Additive        = 64,
	Specular        = 128,
	EnvMap          = 256,
	VertexLighting  = 512,
	TiledNormalmap  = 2048,
	Doublesided     = 65536,
	Scrolling       = 16777216,
	Energy          = 33554432,
	Animated        = 67108864,
	AttachedLight   = 134217728,
};
ENUM_CLASS_FLAGS(ESWBFMaterialFlag)

/**
 * Material-specific metadata for imported SWBF assets.
 * Extends base SWBF metadata with the original material render flags bitmask.
 */
UCLASS()
class ZEROEDITORTOOLS_API USWBFMaterialAssetUserData : public USWBFAssetUserData
{
	GENERATED_BODY()

public:
	/** Original SWBF material render flags bitmask. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import", meta = (Bitmask, BitmaskEnum = "/Script/ZeroEditorTools.ESWBFMaterialFlag"))
	int32 MaterialFlags = 0;

	/** Texture names per slot (index = slot number, empty string = gap/unused). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	TArray<FString> TextureNames;

	/** Specular color from SWBF material. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FColor SpecularColor = FColor::Black;

	/** Specular exponent from SWBF material. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	uint32 SpecularExponent = 0;

	/** Raw parameter 0 from MTRL chunk (low byte is meaningful value). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	uint32 RawParam0 = 0;

	/** Raw parameter 1 from MTRL chunk (low byte is meaningful value). */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	uint32 RawParam1 = 0;

	/** Attached light name. Empty when not set. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FString AttachedLight;

	/** Deduplication key used to identify materials with identical appearance across levels. */
	UPROPERTY(VisibleAnywhere, Category = "SWBF Import")
	FString MaterialDedupKey;

	/** Emits MaterialDedupKey as an Asset Registry tag in addition to base class tags. */
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

	/**
	 * Creates and attaches a USWBFMaterialAssetUserData instance to the given asset.
	 * Returns nullptr if the asset does not implement IInterface_AssetUserData.
	 */
	static USWBFMaterialAssetUserData* AttachToMaterial(UObject* Asset, const FSWBFMaterialData& MatData, const FString& InLevelName);
};
