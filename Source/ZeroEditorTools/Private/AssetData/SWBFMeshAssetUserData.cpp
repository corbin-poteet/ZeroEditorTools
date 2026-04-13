// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFMeshAssetUserData.h"
#include "SWBFMeshImporter.h"

#include "Interfaces/Interface_AssetUserData.h"

USWBFMeshAssetUserData* USWBFMeshAssetUserData::AttachToMesh(
	UStaticMesh* Mesh,
	const FSWBFModelData& ModelData,
	int32 InLODCount,
	const TArray<FName>& SlotNames,
	const FString& InLevelName)
{
	if (!Mesh)
	{
		return nullptr;
	}

	IInterface_AssetUserData* UserDataInterface = Cast<IInterface_AssetUserData>(Mesh);
	if (!UserDataInterface)
	{
		return nullptr;
	}

	USWBFMeshAssetUserData* Metadata = NewObject<USWBFMeshAssetUserData>(Mesh);
	Metadata->SWBFName = ModelData.Name;
	Metadata->LevelName = InLevelName;

	// Count valid segments (must have geometry to be a real slot)
	int32 ValidSegCount = 0;
	for (const FSWBFSegmentData& Seg : ModelData.Segments)
	{
		if (Seg.bValid && Seg.Vertices.Num() > 0)
		{
			++ValidSegCount;
		}
	}
	Metadata->SegmentCount = ValidSegCount;

	Metadata->LODCount = InLODCount;
	Metadata->bHasCollisionMesh = ModelData.CollisionMesh.bValid;
	Metadata->CollisionPrimitiveCount = ModelData.CollisionPrimitives.Num();
	Metadata->bIsSkeletal = ModelData.bIsSkeletal;

	for (const FName& SlotName : SlotNames)
	{
		Metadata->MaterialSlotNames.Add(SlotName.ToString());
	}

	UserDataInterface->AddAssetUserData(Metadata);

	return Metadata;
}
