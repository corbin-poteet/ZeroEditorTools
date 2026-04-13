// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFConfigAssetUserData.h"

#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

USWBFConfigAssetUserData* USWBFConfigAssetUserData::AddConfigToWorldSettings(
	UWorld* World,
	const FString& InConfigTypeName,
	const FString& InConfigName,
	const TMap<FString, FString>& InConfigFields,
	const FString& InLevelName)
{
	if (!World)
	{
		return nullptr;
	}

	AWorldSettings* WS = World->GetWorldSettings();
	if (!WS)
	{
		return nullptr;
	}

	// Retrieve existing or create new
	USWBFConfigAssetUserData* Metadata = Cast<USWBFConfigAssetUserData>(
		WS->GetAssetUserDataOfClass(USWBFConfigAssetUserData::StaticClass()));

	if (!Metadata)
	{
		Metadata = NewObject<USWBFConfigAssetUserData>(WS);
		Metadata->LevelName = InLevelName;
		WS->AddAssetUserData(Metadata);
	}

	FSWBFConfigEntry Entry;
	Entry.ConfigTypeName = InConfigTypeName;
	Entry.ConfigName = InConfigName;
	Entry.ConfigFields = InConfigFields;

	Metadata->Configs.Add(MoveTemp(Entry));

	return Metadata;
}
