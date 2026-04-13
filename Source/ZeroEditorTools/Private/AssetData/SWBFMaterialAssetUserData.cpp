// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFMaterialAssetUserData.h"
#include "SWBFMaterialImporter.h"

#include "Interfaces/Interface_AssetUserData.h"
#include "UObject/AssetRegistryTagsContext.h"

USWBFMaterialAssetUserData* USWBFMaterialAssetUserData::AttachToMaterial(UObject* Asset, const FSWBFMaterialData& MatData, const FString& InLevelName)
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

	USWBFMaterialAssetUserData* Metadata = NewObject<USWBFMaterialAssetUserData>(Asset);
	Metadata->SWBFName = MatData.TextureNames.Num() > 0 ? MatData.TextureNames[0] : FString();
	Metadata->LevelName = InLevelName;
	Metadata->MaterialFlags = static_cast<int32>(MatData.Flags);
	Metadata->TextureNames = MatData.TextureNames;
	Metadata->SpecularColor = MatData.SpecularColor;
	Metadata->SpecularExponent = MatData.SpecularExponent;
	Metadata->RawParam0 = MatData.RawParam0;
	Metadata->RawParam1 = MatData.RawParam1;
	Metadata->AttachedLight = MatData.AttachedLight;
	Metadata->MaterialDedupKey = MatData.GetDedupKey();

	UserDataInterface->AddAssetUserData(Metadata);

	return Metadata;
}

void USWBFMaterialAssetUserData::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);
	if (!MaterialDedupKey.IsEmpty())
	{
		Context.AddTag(UObject::FAssetRegistryTag(
			SWBFTagKeys::MaterialDedupKey,
			MaterialDedupKey,
			UObject::FAssetRegistryTag::TT_Alphabetical));
	}
}
