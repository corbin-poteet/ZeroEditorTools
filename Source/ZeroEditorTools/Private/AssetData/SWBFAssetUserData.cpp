// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFAssetUserData.h"

#include "Interfaces/Interface_AssetUserData.h"
#include "UObject/AssetRegistryTagsContext.h"

USWBFAssetUserData::USWBFAssetUserData()
	: SWBFName()
	, LevelName()
	, WorldName()
{
}

void USWBFAssetUserData::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	// Note: intentionally not calling Super — the delegate-based call path in
	// FZeroEditorToolsModule::OnGetExtraAssetRegistryTags would cause infinite
	// recursion since UObject::GetAssetRegistryTags re-fires the delegate.

	if (!SWBFName.IsEmpty())
	{
		Context.AddTag(UObject::FAssetRegistryTag(SWBFTagKeys::SourceName, SWBFName, UObject::FAssetRegistryTag::TT_Alphabetical));
	}

	if (!LevelName.IsEmpty())
	{
		Context.AddTag(UObject::FAssetRegistryTag(SWBFTagKeys::SourceLevel, LevelName, UObject::FAssetRegistryTag::TT_Alphabetical));
	}

	if (!WorldName.IsEmpty())
	{
		Context.AddTag(UObject::FAssetRegistryTag(SWBFTagKeys::SourceWorld, WorldName, UObject::FAssetRegistryTag::TT_Alphabetical));
	}
}

USWBFAssetUserData* USWBFAssetUserData::AttachToAsset(UObject* Asset, const FString& InSWBFName, const FString& InLevelName, const FString& InWorldName)
{
	if (!Asset)
	{
		return nullptr;
	}

	IInterface_AssetUserData* UserDataInterface = Cast<IInterface_AssetUserData>(Asset);
	if (!UserDataInterface)
	{
		return nullptr;
	}

	USWBFAssetUserData* Metadata = NewObject<USWBFAssetUserData>(Asset);
	Metadata->SWBFName = InSWBFName;
	Metadata->LevelName = InLevelName;
	Metadata->WorldName = InWorldName;

	UserDataInterface->AddAssetUserData(Metadata);

	return Metadata;
}