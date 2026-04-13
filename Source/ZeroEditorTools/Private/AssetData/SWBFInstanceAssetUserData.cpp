// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFInstanceAssetUserData.h"
#include "SWBFWorldImporter.h"

#include "Components/ActorComponent.h"

USWBFInstanceAssetUserData* USWBFInstanceAssetUserData::AttachToActor(
	AActor* Actor,
	const FSWBFInstanceData& InstData,
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

	USWBFInstanceAssetUserData* Metadata = NewObject<USWBFInstanceAssetUserData>(RootComp);
	Metadata->SWBFName = InstData.Name;
	Metadata->LevelName = InLevelName;
	Metadata->WorldName = InstData.WorldName;
	Metadata->EntityClassName = InstData.EntityClassName;
	Metadata->OriginalPosition = InstData.OriginalPosition;
	Metadata->OriginalRotation = InstData.OriginalRotation;
	Metadata->OriginalRotationMatrix = InstData.OriginalRotationMatrix;
	Metadata->ConvertedPosition = InstData.Position;
	Metadata->ConvertedRotation = InstData.Rotation;
	Metadata->OverrideProperties = InstData.OverrideProperties;

	// Negative determinant of the scale vector indicates a mirrored transform
	Metadata->bIsMirrored = (InstData.Scale.X * InstData.Scale.Y * InstData.Scale.Z) < 0.0f;

	RootComp->AddAssetUserData(Metadata);

	return Metadata;
}
