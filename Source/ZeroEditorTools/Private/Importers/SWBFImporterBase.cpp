// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFImporterBase.h"
#include "SWBFAssetUserData.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInterface.h"
#include "EditorFramework/AssetImportData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"

void FSWBFImporterBase::FinalizeAsset(UObject* Asset, const FString& SWBFName, const FString& LevelName, const FString& SourceFilePath, bool bSkipMetadata)
{
	if (!Asset)
	{
		return;
	}

	// 1. Attach SWBF provenance metadata (skip if caller attached a subclass)
	if (!bSkipMetadata)
	{
		USWBFAssetUserData::AttachToAsset(Asset, SWBFName, LevelName);
	}

	// 2. Store source .lvl file path in AssetImportData (standard UE source tracking)
	if (!SourceFilePath.IsEmpty())
	{
		UAssetImportData* ImportData = nullptr;

		if (UStaticMesh* SM = Cast<UStaticMesh>(Asset))
		{
			if (!SM->GetAssetImportData())
			{
				SM->SetAssetImportData(NewObject<UAssetImportData>(SM));
			}
			ImportData = SM->GetAssetImportData();
		}
		else if (UTexture* Tex = Cast<UTexture>(Asset))
		{
			if (!Tex->AssetImportData)
			{
				Tex->AssetImportData = NewObject<UAssetImportData>(Tex);
			}
			ImportData = Tex->AssetImportData;
		}
		else if (UMaterialInterface* Mat = Cast<UMaterialInterface>(Asset))
		{
			if (!Mat->AssetImportData)
			{
				Mat->AssetImportData = NewObject<UAssetImportData>(Mat);
			}
			ImportData = Mat->AssetImportData;
		}

		if (ImportData)
		{
			ImportData->Update(SourceFilePath);
		}
	}

	// 3. Register with asset system
	Asset->GetPackage()->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Asset);
}

bool FSWBFImporterBase::FindExistingAssetBySourceName(
	const FString& SourceName,
	const FTopLevelAssetPath& ClassPath,
	FAssetData& OutAssetData)
{
	FARFilter Filter;
	Filter.ClassPaths.Add(ClassPath);
	Filter.bRecursiveClasses = true;
	Filter.TagsAndValues.Add(FName(SWBFTagKeys::SourceName), TOptional<FString>(SourceName));

	TArray<FAssetData> Results;
	IAssetRegistry::GetChecked().GetAssets(Filter, Results);

	if (Results.Num() > 0)
	{
		OutAssetData = Results[0];
		return true;
	}
	return false;
}

bool FSWBFImporterBase::FindExistingAssetByDedupKey(
	const FString& DedupKey,
	const FTopLevelAssetPath& ClassPath,
	FAssetData& OutAssetData)
{
	FARFilter Filter;
	Filter.ClassPaths.Add(ClassPath);
	Filter.bRecursiveClasses = true;
	Filter.TagsAndValues.Add(FName(SWBFTagKeys::MaterialDedupKey), TOptional<FString>(DedupKey));

	TArray<FAssetData> Results;
	IAssetRegistry::GetChecked().GetAssets(Filter, Results);

	if (Results.Num() > 0)
	{
		OutAssetData = Results[0];
		return true;
	}
	return false;
}
