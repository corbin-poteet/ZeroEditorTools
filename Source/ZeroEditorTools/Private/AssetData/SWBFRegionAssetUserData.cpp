// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFRegionAssetUserData.h"
#include "SWBFRegionImporter.h"

#include "Components/ActorComponent.h"

USWBFRegionAssetUserData* USWBFRegionAssetUserData::AttachToActor(
	AActor* Actor,
	const FSWBFRegionData& RegionData,
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

	USWBFRegionAssetUserData* Metadata = NewObject<USWBFRegionAssetUserData>(RootComp);
	Metadata->SWBFName   = RegionData.Name;
	Metadata->LevelName  = InLevelName;
	Metadata->WorldName  = RegionData.WorldName;
	Metadata->RegionType = RegionData.Type;
	Metadata->Position   = RegionData.Position;
	Metadata->Rotation   = RegionData.Rotation;
	Metadata->Size       = RegionData.Size;
	Metadata->Radius     = RegionData.Radius;
	Metadata->HalfHeight = RegionData.HalfHeight;

	for (const TPair<uint32, FString>& Prop : RegionData.Properties)
	{
		Metadata->Properties.Add(FString::Printf(TEXT("0x%08X"), Prop.Key), Prop.Value);
	}

	Metadata->RegionClass  = RegionData.RegionClass;
	Metadata->InlineParams = RegionData.InlineParams;

	RootComp->AddAssetUserData(Metadata);

	return Metadata;
}
