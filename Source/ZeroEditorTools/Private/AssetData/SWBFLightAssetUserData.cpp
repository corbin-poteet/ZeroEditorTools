// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFLightAssetUserData.h"
#include "SWBFLightImporter.h"

#include "Components/ActorComponent.h"

USWBFLightAssetUserData* USWBFLightAssetUserData::AttachToActor(
	AActor* Actor,
	const FSWBFLightData& LightData,
	const FString& InLevelName)
{
	if (!Actor)
	{
		return nullptr;
	}

	UActorComponent* RootComp = Actor->GetRootComponent();
	if (!RootComp)
	{
		return nullptr;
	}

	USWBFLightAssetUserData* Metadata = NewObject<USWBFLightAssetUserData>(RootComp);
	Metadata->SWBFName = LightData.Name;
	Metadata->LevelName = InLevelName;
	Metadata->WorldName = LightData.WorldName;
	Metadata->LightType = LightData.LightType;
	Metadata->Color = LightData.Color;
	Metadata->Range = LightData.Range;
	Metadata->OuterConeAngle = LightData.OuterConeAngle;
	Metadata->InnerConeAngle = LightData.InnerConeAngle;
	Metadata->Intensity = LightData.Intensity;
	Metadata->bIsGlobal = LightData.bIsGlobal;
	Metadata->ConfigFields = LightData.ConfigFields;

	RootComp->AddAssetUserData(Metadata);

	return Metadata;
}
