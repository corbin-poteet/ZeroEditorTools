// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFHintNodeImporter.h"
#include "SWBFLevelWrapper.h"
#include "SWBFCoordinateUtils.h"
#include "SWBFAssetUserData.h"
#include "ZeroEditorTools.h"
#include "ZeroEditorToolsSettings.h"

#include "DataLayer/DataLayerEditorSubsystem.h"

#include "Misc/ScopedSlowTask.h"
#include "Editor.h"
#include "Components/BillboardComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Texture2D.h"

THIRD_PARTY_INCLUDES_START
#include <LibSWBF2/Wrappers/Level.h>
#include <LibSWBF2/Wrappers/World.h>
#include <LibSWBF2/Types/LibString.h>
#include <LibSWBF2/Types/List.h>
#include <LibSWBF2/Types/Vector3.h>
#include <LibSWBF2/Types/Vector4.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "SWBFHintNodeImporter"

// ---------------------------------------------------------------------------
// FSWBFHintNodeImporter::ShouldImport
// ---------------------------------------------------------------------------

bool FSWBFHintNodeImporter::ShouldImport() const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	return Settings && Settings->bImportHintNodes && GetWorkCount() > 0;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

// TYPE chunk stores a single ASCII digit + null terminator, read as uint16.
// Values map to ZeroEditor hint type dropdown order.
static FString GetHintTypeName(uint16 Type)
{
	switch (Type)
	{
		case '0': return TEXT("Snipe");
		case '1': return TEXT("Patrol");
		case '2': return TEXT("Cover");
		case '3': return TEXT("Access");
		case '4': return TEXT("JetJump");
		case '5': return TEXT("Mine");
		case '6': return TEXT("Land");
		case '7': return TEXT("Defend");
		default: return FString::Printf(TEXT("Type %d"), Type);
	}
}

static FColor GetHintColor(uint16 Type)
{
	switch (Type)
	{
		case '0': return FColor::Red;      // Snipe
		case '1': return FColor::Yellow;   // Patrol
		case '2': return FColor::Green;    // Cover
		case '3': return FColor::Cyan;     // Access
		case '4': return FColor::Magenta;  // JetJump
		case '5': return FColor::Orange;   // Mine
		case '6': return FColor::Blue;     // Land
		case '7': return FColor(128, 255, 128); // Defend
		default: return FColor::White;     // Unknown
	}
}

// ---------------------------------------------------------------------------
// FSWBFHintNodeData constructor
// ---------------------------------------------------------------------------

FSWBFHintNodeData::FSWBFHintNodeData(const LibSWBF2::Wrappers::HintNode& InHintNode, const FString& InWorldName)
{
	using LibSWBF2::Types::String;
	using LibSWBF2::Types::Vector3;
	using LibSWBF2::Types::Vector4;
	using LibSWBF2::Types::List;

	// Copy name (LibSWBF2 string lifetime)
	Name = FString(ANSI_TO_TCHAR(InHintNode.GetName().Buffer()));

	// Convert position
	const Vector3& Pos = InHintNode.GetPosition();
	Position = FSWBFCoordinateUtils::ConvertPosition(Pos.m_X, Pos.m_Y, Pos.m_Z);

	// Convert rotation (quaternion from XFRM)
	Vector4 Rot = InHintNode.GetRotation();
	Rotation = FSWBFCoordinateUtils::ConvertRotation(Rot.m_X, Rot.m_Y, Rot.m_Z, Rot.m_W);

	// Copy type
	Type = InHintNode.GetType();

	// Extract properties (FNVHash + String pairs)
	List<uint32_t> PropHashes;
	List<String> PropValues;
	InHintNode.GetProperties(PropHashes, PropValues);

	const size_t PropCount = FMath::Min(PropHashes.Size(), PropValues.Size());
	Properties.Reserve(static_cast<int32>(PropCount));
	for (size_t pi = 0; pi < PropCount; ++pi)
	{
		uint32 Hash = PropHashes[pi];
		FString Value = FString(ANSI_TO_TCHAR(PropValues[pi].Buffer()));
		Properties.Add(TPair<uint32, FString>(Hash, MoveTemp(Value)));
	}

	WorldName = InWorldName;
	bValid = true;
}

// ---------------------------------------------------------------------------
// FSWBFHintNodeData::ToString
// ---------------------------------------------------------------------------

FString FSWBFHintNodeData::ToString() const
{
	return FString::Printf(
		TEXT("[HintNode] '%s' world='%s' type=%d pos=(%.1f,%.1f,%.1f) props=%d valid=%d"),
		*Name, *WorldName, static_cast<int32>(Type),
		Position.X, Position.Y, Position.Z,
		Properties.Num(), bValid ? 1 : 0);
}

// ---------------------------------------------------------------------------
// GetData
// ---------------------------------------------------------------------------

void FSWBFHintNodeImporter::GetData(const FSWBFLevelWrapper& LevelWrapper)
{
	if (!LevelWrapper.IsWorldLevel())
	{
		return;
	}

	const auto* LevelPtr = LevelWrapper.GetLevel();
	if (!LevelPtr)
	{
		return;
	}

	using LibSWBF2::Wrappers::World;
	using LibSWBF2::Wrappers::HintNode;

	const auto& Worlds = LevelPtr->GetWorlds();
	const size_t WorldCount = Worlds.Size();

	for (size_t wi = 0; wi < WorldCount; ++wi)
	{
		const World& Wrld = Worlds[wi];
		const FString CurrentWorldName = FString(ANSI_TO_TCHAR(Wrld.GetName().Buffer()));
		const auto& HintNodes = Wrld.GetHintNodes();
		const size_t HintCount = HintNodes.Size();

		for (size_t hi = 0; hi < HintCount; ++hi)
		{
			FSWBFHintNodeData Data(HintNodes[hi], CurrentWorldName);

			UE_LOG(LogZeroEditorTools, Verbose,
				TEXT("[HintNode] '%s' world='%s' type=%u pos=(%.1f,%.1f,%.1f) properties=%d"),
				*Data.Name, *CurrentWorldName, Data.Type,
				Data.Position.X, Data.Position.Y, Data.Position.Z,
				Data.Properties.Num());

			HintNodeData.Add(MoveTemp(Data));
		}
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Extracted %d hint nodes from %d worlds"),
		HintNodeData.Num(), static_cast<int32>(WorldCount));
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

int32 FSWBFHintNodeImporter::Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context)
{
	SlowTask.EnterProgressFrame(static_cast<float>(HintNodeData.Num()),
		FText::Format(LOCTEXT("SpawningHintNodes", "Spawning {0} hint nodes..."),
			FText::AsNumber(HintNodeData.Num())));

	if (GEditor == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("GEditor is null, cannot spawn hint node actors"));
		return 0;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Editor world is null, cannot spawn hint node actors"));
		return 0;
	}

	// Load billboard sprite texture
	UTexture2D* SpriteTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EditorResources/S_Note.S_Note"));
	if (!SpriteTexture)
	{
		SpriteTexture = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EditorResources/S_Sphere.S_Sphere"));
	}

	int32 SpawnedCount = 0;

	for (const FSWBFHintNodeData& Data : HintNodeData)
	{
		if (!Data.bValid)
		{
			continue;
		}

		const FTransform SpawnTransform(Data.Rotation, Data.Position);
		AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnTransform);
		if (!Actor)
		{
			continue;
		}

		// Root component (required for transform)
		USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Actor->SetActorTransform(SpawnTransform);

		const FColor HintColor = GetHintColor(Data.Type);
		const FString TypeName = GetHintTypeName(Data.Type);

		// Billboard icon for visual marker
		UBillboardComponent* Billboard = NewObject<UBillboardComponent>(Actor, TEXT("Icon"));
		Billboard->SetupAttachment(Root);
		if (SpriteTexture)
		{
			Billboard->SetSprite(SpriteTexture);
		}
		Billboard->SetRelativeScale3D(FVector(0.5f));
		Billboard->bIsScreenSizeScaled = true;
#if WITH_EDITORONLY_DATA
		Billboard->SpriteInfo.Category = TEXT("SWBF");
		Billboard->SpriteInfo.DisplayName = FText::FromString(TypeName);
#endif
		Billboard->RegisterComponent();

		// Floating type label text
		UTextRenderComponent* TextComp = NewObject<UTextRenderComponent>(Actor, TEXT("TypeLabel"));
		TextComp->SetupAttachment(Root);
		TextComp->SetText(FText::FromString(TypeName));
		TextComp->SetRelativeLocation(FVector(0.0f, 0.0f, 50.0f));
		TextComp->SetHorizontalAlignment(EHTA_Center);
		TextComp->SetWorldSize(16.0f);
		TextComp->SetTextRenderColor(HintColor);
		TextComp->SetHiddenInGame(true);
		TextComp->RegisterComponent();

		// Actor label with name and type
		Actor->SetActorLabel(FString::Printf(TEXT("%s [%s]"), *Data.Name, *TypeName));
		Actor->SetFolderPath(FName(TEXT("HintNodes")));

		// Attach SWBF metadata (WMETA-01)
		USWBFAssetUserData::AttachToAsset(Actor, Data.Name, Context.LevelName, Data.WorldName);

		// Assign to DataLayer (if WP enabled)
		if (Context.bWorldPartitionEnabled && !Data.WorldName.IsEmpty())
		{
			if (UDataLayerInstance** FoundDL = Context.DataLayerLookup.Find(Data.WorldName))
			{
				if (UDataLayerEditorSubsystem* DLSubsystem = UDataLayerEditorSubsystem::Get())
				{
					DLSubsystem->AddActorToDataLayer(Actor, *FoundDL);
				}
			}
		}

		// Store hint type as actor tag
		Actor->Tags.Add(*FString::Printf(TEXT("SWBFHintType:%u"), Data.Type));

		// Store hint properties as actor tags
		for (const TPair<uint32, FString>& Prop : Data.Properties)
		{
			Actor->Tags.Add(*FString::Printf(TEXT("SWBFProp:%u=%s"), Prop.Key, *Prop.Value));
		}

		++SpawnedCount;
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Spawned %d hint node actors from %d hint nodes"),
		SpawnedCount, HintNodeData.Num());

	return SpawnedCount;
}

// ---------------------------------------------------------------------------
// CollectWorldNames
// ---------------------------------------------------------------------------

void FSWBFHintNodeImporter::CollectWorldNames(TSet<FString>& OutWorldNames) const
{
	for (const auto& Data : HintNodeData)
	{
		if (!Data.WorldName.IsEmpty())
		{
			OutWorldNames.Add(Data.WorldName);
		}
	}
}

#undef LOCTEXT_NAMESPACE
