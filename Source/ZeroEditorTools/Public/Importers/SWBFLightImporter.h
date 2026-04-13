// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SWBFImporterBase.h"

/**
 * Holds extracted light data from a SWBF lighting config.
 * Position, rotation, color, and range are converted to UE space/units.
 */
struct FSWBFLightData
{
	/** Light name from config field string value. */
	FString Name;

	/** SWBF World this light belongs to (for DataLayer assignment). */
	FString WorldName;

	/** World position converted to UE coordinate space (point/spot only). */
	FVector Position = FVector::ZeroVector;

	/** Rotation converted to UE coordinate space (directional/spot). */
	FQuat Rotation = FQuat::Identity;

	/** Light color in 0-1 range (used directly from config for local lights). */
	FLinearColor Color = FLinearColor::White;

	/** Attenuation radius in UE units (SWBF meters * UnitScale). */
	float Range = 0.0f;

	/** Spot light outer cone angle in degrees (converted from SWBF radians). */
	float OuterConeAngle = 0.0f;

	/** Spot light inner cone angle in degrees (80% of outer). */
	float InnerConeAngle = 0.0f;

	/** Light intensity. */
	float Intensity = 1.0f;

	/** SWBF light type: 1=Directional, 2=Point/Omni, 3=Spot. */
	int32 LightType = 0;

	/** True if light matches GlobalLights.Light1 or Light2 name. */
	bool bIsGlobal = false;

	/** Raw config fields from the SWBF lighting scope (field name -> data string). */
	TMap<FString, FString> ConfigFields;

	/** Whether the light data was successfully extracted. */
	bool bValid = false;
};

/**
 * Holds extracted ambient lighting data from a SWBF lighting config.
 * Top/Bottom colors are scaled from 0-255 to 0-1 range.
 */
struct FSWBFAmbientData
{
	/** SWBF World this ambient data belongs to. */
	FString WorldName;

	/** Ambient top color scaled from 0-255 to 0-1. */
	FLinearColor TopColor = FLinearColor::Black;

	/** Ambient bottom color scaled from 0-255 to 0-1. */
	FLinearColor BottomColor = FLinearColor::Black;

	/** Skydome DomeInfo.Ambient color (used for SkyLight LightColor). */
	FLinearColor DomeAmbientColor = FLinearColor::Black;

	/** Whether DomeAmbientColor was successfully extracted. */
	bool bHasDomeAmbient = false;

	/** Whether the ambient data was successfully extracted. */
	bool bValid = false;
};

/**
 * Extracts and imports lighting data from SWBF Config API (EConfigType::Lighting).
 * Handles directional, point, spot lights and ambient colors.
 * First config-based importer in the codebase.
 */
class FSWBFLightImporter : public FSWBFImporterBase
{
public:
	virtual FString GetDisplayName() const override { return TEXT("lights"); }
	virtual FString GetDestSubfolder() const override { return FString(); }
	virtual void GetData(const FSWBFLevelWrapper& LevelWrapper) override;
	virtual int32 GetWorkCount() const override { return LightData.Num() + AmbientData.Num(); }
	virtual bool ShouldImport() const override;
	virtual int32 Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context) override;
	virtual void CollectWorldNames(TSet<FString>& OutWorldNames) const override;

private:
	TArray<FSWBFLightData> LightData;
	TArray<FSWBFAmbientData> AmbientData;
};
