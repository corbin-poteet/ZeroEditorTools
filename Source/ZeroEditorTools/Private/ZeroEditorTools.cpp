// Copyright Epic Games, Inc. All Rights Reserved.

#include "ZeroEditorTools.h"
#include "SWBFAssetUserData.h"
#include "UObject/AssetRegistryTagsContext.h"

DEFINE_LOG_CATEGORY(LogZeroEditorTools);

#define LOCTEXT_NAMESPACE "FZeroEditorToolsModule"

void FZeroEditorToolsModule::StartupModule()
{
	// Register delegate to propagate USWBFAssetUserData tags onto owning assets
	TagsDelegateHandle = UObject::FAssetRegistryTag::OnGetExtraObjectTagsWithContext.AddStatic(
		&FZeroEditorToolsModule::OnGetExtraAssetRegistryTags);
}

void FZeroEditorToolsModule::ShutdownModule()
{
	// Unregister tag propagation delegate
	UObject::FAssetRegistryTag::OnGetExtraObjectTagsWithContext.Remove(TagsDelegateHandle);
}

void FZeroEditorToolsModule::OnGetExtraAssetRegistryTags(FAssetRegistryTagsContext Context)
{
	const UObject* Object = Context.GetObject();
	if (!Object || !Object->IsAsset())
	{
		return;
	}

	const IInterface_AssetUserData* UserDataInterface = Cast<IInterface_AssetUserData>(Object);
	if (!UserDataInterface)
	{
		return;
	}

	const TArray<UAssetUserData*>* UserDataArray = UserDataInterface->GetAssetUserDataArray();
	if (!UserDataArray)
	{
		return;
	}

	for (UAssetUserData* UserData : *UserDataArray)
	{
		if (const USWBFAssetUserData* SWBFData = Cast<USWBFAssetUserData>(UserData))
		{
			SWBFData->GetAssetRegistryTags(Context);
			break;
		}
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FZeroEditorToolsModule, ZeroEditorTools)