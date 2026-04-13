// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFRegionImporter.h"
#include "SWBFLevelWrapper.h"
#include "SWBFCoordinateUtils.h"
#include "SWBFAssetUserData.h"
#include "SWBFRegionAssetUserData.h"
#include "ZeroEditorTools.h"
#include "ZeroEditorToolsSettings.h"

#include "DataLayer/DataLayerEditorSubsystem.h"

#include "Misc/ScopedSlowTask.h"
#include "Editor.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/TextRenderComponent.h"
#include "Sound/AudioVolume.h"
#include "GameFramework/KillZVolume.h"
#include "GameFramework/PainCausingVolume.h"
#include "ActorFactories/ActorFactory.h"
#include "Builders/CubeBuilder.h"
#include "Builders/CylinderBuilder.h"
#include "Builders/TetrahedronBuilder.h"

THIRD_PARTY_INCLUDES_START
#include <LibSWBF2/Wrappers/Level.h>
#include <LibSWBF2/Wrappers/World.h>
#include <LibSWBF2/Types/LibString.h>
#include <LibSWBF2/Types/List.h>
#include <LibSWBF2/Types/Vector3.h>
#include <LibSWBF2/Types/Vector4.h>
#include <LibSWBF2/Types/Enums.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "SWBFRegionImporter"

// ---------------------------------------------------------------------------
// FSWBFRegionImporter::ShouldImport
// ---------------------------------------------------------------------------

bool FSWBFRegionImporter::ShouldImport() const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	return Settings && Settings->bImportRegions && GetWorkCount() > 0;
}

// ---------------------------------------------------------------------------
// Color helper
// ---------------------------------------------------------------------------

static FColor GetRegionColor(const FString& Type)
{
	if (Type == TEXT("box"))      return FColor(0, 255, 128);    // Green
	if (Type == TEXT("sphere"))   return FColor(0, 128, 255);    // Blue
	if (Type == TEXT("cylinder")) return FColor(255, 200, 0);    // Yellow
	return FColor(200, 200, 200);                                // Grey fallback
}

// ---------------------------------------------------------------------------
// InitBrushGeometry -- configures brush builder geometry on a spawned AVolume
// ---------------------------------------------------------------------------

static void InitBrushGeometry(AVolume* Volume, const FSWBFRegionData& Data)
{
	if (Data.Type == TEXT("box"))
	{
		UCubeBuilder* Builder = NewObject<UCubeBuilder>();
		Builder->X = Data.Size.X * 2.0f;  // half-extents -> full extents
		Builder->Y = Data.Size.Y * 2.0f;
		Builder->Z = Data.Size.Z * 2.0f;
		UActorFactory::CreateBrushForVolumeActor(Volume, Builder);
	}
	else if (Data.Type == TEXT("sphere"))
	{
		UTetrahedronBuilder* Builder = NewObject<UTetrahedronBuilder>();
		Builder->Radius = Data.Radius;
		Builder->SphereExtrapolation = 2;
		UActorFactory::CreateBrushForVolumeActor(Volume, Builder);
	}
	else if (Data.Type == TEXT("cylinder"))
	{
		UCylinderBuilder* Builder = NewObject<UCylinderBuilder>();
		Builder->OuterRadius = Data.Radius;
		Builder->Z = Data.HalfHeight * 2.0f;  // half-height -> full height
		UActorFactory::CreateBrushForVolumeActor(Volume, Builder);
	}
}

// ---------------------------------------------------------------------------
// PostSpawnTypedVolume -- shared post-spawn steps for typed volumes
// ---------------------------------------------------------------------------

static void PostSpawnTypedVolume(AActor* Volume, const FSWBFRegionData& Data, FSWBFImportContext& Context)
{
	Volume->SetFolderPath(FName(TEXT("Regions")));
	USWBFRegionAssetUserData::AttachToActor(Volume, Data, Context.LevelName);
	if (Context.bWorldPartitionEnabled && !Data.WorldName.IsEmpty())
	{
		if (UDataLayerInstance** FoundDL = Context.DataLayerLookup.Find(Data.WorldName))
		{
			if (UDataLayerEditorSubsystem* DLSubsystem = UDataLayerEditorSubsystem::Get())
			{
				DLSubsystem->AddActorToDataLayer(Volume, *FoundDL);
			}
		}
	}
}

// ---------------------------------------------------------------------------
// FSWBFRegionData constructor
// ---------------------------------------------------------------------------

FSWBFRegionData::FSWBFRegionData(const LibSWBF2::Wrappers::Region& InRegion, const FString& InWorldName)
{
	using LibSWBF2::Types::String;
	using LibSWBF2::Types::Vector3;
	using LibSWBF2::Types::Vector4;
	using LibSWBF2::Types::List;

	// Copy name and type (LibSWBF2 string lifetime)
	Name = FString(ANSI_TO_TCHAR(InRegion.GetName().Buffer()));
	Type = FString(ANSI_TO_TCHAR(InRegion.GetType().Buffer()));

	// Convert position
	const Vector3& Pos = InRegion.GetPosition();
	Position = FSWBFCoordinateUtils::ConvertPosition(Pos.m_X, Pos.m_Y, Pos.m_Z);

	// Convert rotation (quaternion from XFRM)
	Vector4 Rot = InRegion.GetRotation();
	Rotation = FSWBFCoordinateUtils::ConvertRotation(Rot.m_X, Rot.m_Y, Rot.m_Z, Rot.m_W);

	// Convert size based on type
	const Vector3& Sz = InRegion.GetSize();

	if (Type == TEXT("box"))
	{
		// SWBF half-extents -> UE half-extents (UBoxComponent takes half-extents)
		Size = FSWBFCoordinateUtils::ConvertSize(Sz.m_X, Sz.m_Y, Sz.m_Z);
	}
	else if (Type == TEXT("sphere"))
	{
		// Sphere radius is the X component
		Radius = Sz.m_X * FSWBFCoordinateUtils::UnitScale;
	}
	else if (Type == TEXT("cylinder"))
	{
		// Cylinder radius from X,Z components, half-height from Y
		Radius = FMath::Sqrt(Sz.m_X * Sz.m_X + Sz.m_Z * Sz.m_Z) * FSWBFCoordinateUtils::UnitScale;
		HalfHeight = Sz.m_Y * FSWBFCoordinateUtils::UnitScale;
	}

	// Extract properties (game mode metadata, etc.)
	List<uint32_t> PropHashes;
	List<String> PropValues;
	InRegion.GetProperties(PropHashes, PropValues);

	const size_t PropCount = FMath::Min(PropHashes.Size(), PropValues.Size());
	Properties.Reserve(static_cast<int32>(PropCount));
	for (size_t pi = 0; pi < PropCount; ++pi)
	{
		uint32 Hash = PropHashes[pi];
		FString Value = FString(ANSI_TO_TCHAR(PropValues[pi].Buffer()));
		Properties.Add(TPair<uint32, FString>(Hash, MoveTemp(Value)));
	}

	WorldName = InWorldName;
	RegionClass = TEXT("default");
	// Phase 30 parser overwrites with parsed prefix for recognized types
	// InlineParamsRaw and InlineParams are default-constructed empty -- correct for Phase 29
	bValid = true;
}

// ---------------------------------------------------------------------------
// FSWBFRegionData::ToString
// ---------------------------------------------------------------------------

FString FSWBFRegionData::ToString() const
{
	return FString::Printf(
		TEXT("[Region] '%s' world='%s' type='%s' pos=(%.1f,%.1f,%.1f) radius=%.1f halfheight=%.1f props=%d valid=%d"),
		*Name, *WorldName, *Type,
		Position.X, Position.Y, Position.Z,
		Radius, HalfHeight, Properties.Num(), bValid ? 1 : 0);
}

// ---------------------------------------------------------------------------
// GetData
// ---------------------------------------------------------------------------

void FSWBFRegionImporter::GetData(const FSWBFLevelWrapper& LevelWrapper)
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
	using LibSWBF2::Wrappers::Region;

	const auto& Worlds = LevelPtr->GetWorlds();
	const size_t WorldCount = Worlds.Size();

	for (size_t wi = 0; wi < WorldCount; ++wi)
	{
		const World& Wrld = Worlds[wi];
		const FString CurrentWorldName = FString(ANSI_TO_TCHAR(Wrld.GetName().Buffer()));
		const auto& Regions = Wrld.GetRegions();
		const size_t RegionCount = Regions.Size();

		for (size_t ri = 0; ri < RegionCount; ++ri)
		{
			FSWBFRegionData Data(Regions[ri], CurrentWorldName);

			UE_LOG(LogZeroEditorTools, Verbose,
				TEXT("[Region] '%s' world='%s' type='%s' pos=(%.1f,%.1f,%.1f) size=(%.1f,%.1f,%.1f) radius=%.1f halfHeight=%.1f properties=%d"),
				*Data.Name, *CurrentWorldName, *Data.Type,
				Data.Position.X, Data.Position.Y, Data.Position.Z,
				Data.Size.X, Data.Size.Y, Data.Size.Z,
				Data.Radius, Data.HalfHeight, Data.Properties.Num());

			RegionData.Add(MoveTemp(Data));
		}
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Extracted %d regions from %d worlds"),
		RegionData.Num(), static_cast<int32>(WorldCount));

	ParseRegionNames();
}

// ---------------------------------------------------------------------------
// ParseRegionNames
// ---------------------------------------------------------------------------

void FSWBFRegionImporter::ParseRegionNames()
{
	// Known semantic prefixes -- matched token-exactly (case-insensitive).
	// Do NOT use StartsWith or Contains -- "deathregion1" must NOT match "deathregion".
	static const TSet<FString> KnownPrefixes =
	{
		TEXT("soundspace"),
		TEXT("deathregion"),
		TEXT("soundstream"),
		TEXT("mapbounds"),
		TEXT("damageregion"),
	};

	TMap<FString, int32> ClassCounts;

	for (FSWBFRegionData& Data : RegionData)
	{
		TArray<FString> Tokens;
		Data.Name.ParseIntoArray(Tokens, TEXT(" "), /*bCullEmpty=*/true);

		if (Tokens.IsEmpty())
		{
			// Leave RegionClass as "default" (set by constructor)
			ClassCounts.FindOrAdd(TEXT("default"))++;
			continue;
		}

		// Token-exact prefix match (case-insensitive via lowercase comparison)
		const FString FirstLower = Tokens[0].ToLower();
		if (KnownPrefixes.Contains(FirstLower))
		{
			Data.RegionClass = FirstLower;
		}
		// else: leave as "default" from constructor

		// Extract InlineParamsRaw: everything after the first token
		if (Tokens.Num() > 1)
		{
			Data.InlineParamsRaw = Data.Name.Mid(Tokens[0].Len()).TrimStart();
		}

		// Parse remaining tokens into InlineParams
		int32 ArgIndex = 0;
		for (int32 ti = 1; ti < Tokens.Num(); ++ti)
		{
			FString Key, Value;
			if (Tokens[ti].Split(TEXT("="), &Key, &Value))
			{
				// key=value pair: lowercase the key, preserve value case
				Data.InlineParams.Add(Key.ToLower(), Value);
			}
			else
			{
				// Positional argument: arg0, arg1, ...
				Data.InlineParams.Add(FString::Printf(TEXT("arg%d"), ArgIndex++), Tokens[ti]);
			}
		}

		// Build param string for per-region log
		FString ParamStr;
		for (const TPair<FString, FString>& Pair : Data.InlineParams)
		{
			if (!ParamStr.IsEmpty())
			{
				ParamStr += TEXT(", ");
			}
			ParamStr += Pair.Key + TEXT("=") + Pair.Value;
		}

		UE_LOG(LogZeroEditorTools, Log,
			TEXT("Region '%s': class=%s params={%s}"),
			*Data.Name, *Data.RegionClass, *ParamStr);

		ClassCounts.FindOrAdd(Data.RegionClass)++;
	}

	// Summary log: "Parsed N regions: X soundspace, Y deathregion, Z default ..."
	FString SummaryStr;
	for (const TPair<FString, int32>& Entry : ClassCounts)
	{
		if (!SummaryStr.IsEmpty())
		{
			SummaryStr += TEXT(", ");
		}
		SummaryStr += FString::Printf(TEXT("%d %s"), Entry.Value, *Entry.Key);
	}
	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Parsed %d regions: %s"),
		RegionData.Num(), *SummaryStr);
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

AActor* FSWBFRegionImporter::SpawnGenericRegionActor(const FSWBFRegionData& Data, UWorld* World, FSWBFImportContext& Context) const
{
	const FTransform SpawnTransform(Data.Rotation, Data.Position);
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnTransform);
	if (!Actor)
	{
		return nullptr;
	}

	// Root component (required for transform)
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Actor->SetActorTransform(SpawnTransform);

	const FColor RegionColor = GetRegionColor(Data.Type);

	// Shape component based on region type
	if (Data.Type == TEXT("box"))
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("Volume"));
		Box->SetupAttachment(Root);
		Box->SetBoxExtent(Data.Size);
		Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Box->ShapeColor = RegionColor;
		Box->SetHiddenInGame(true);
		Box->RegisterComponent();
	}
	else if (Data.Type == TEXT("sphere"))
	{
		USphereComponent* Sphere = NewObject<USphereComponent>(Actor, TEXT("Volume"));
		Sphere->SetupAttachment(Root);
		Sphere->SetSphereRadius(Data.Radius);
		Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sphere->ShapeColor = RegionColor;
		Sphere->SetHiddenInGame(true);
		Sphere->RegisterComponent();
	}
	else if (Data.Type == TEXT("cylinder"))
	{
		UCapsuleComponent* Capsule = NewObject<UCapsuleComponent>(Actor, TEXT("Volume"));
		Capsule->SetupAttachment(Root);
		Capsule->SetCapsuleHalfHeight(Data.HalfHeight);
		Capsule->SetCapsuleRadius(Data.Radius);
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Capsule->ShapeColor = RegionColor;
		Capsule->SetHiddenInGame(true);
		Capsule->RegisterComponent();
	}

	// Build properties text for label
	FString PropsText;
	for (const TPair<uint32, FString>& Prop : Data.Properties)
	{
		PropsText += FString::Printf(TEXT("\n0x%08X=%s"), Prop.Key, *Prop.Value);
		// Also store as actor tag for inspection
		Actor->Tags.Add(*FString::Printf(TEXT("SWBFProp:0x%08X=%s"), Prop.Key, *Prop.Value));
	}

	// Floating text label with name, type, and properties
	UTextRenderComponent* TextComp = NewObject<UTextRenderComponent>(Actor, TEXT("Label"));
	TextComp->SetupAttachment(Root);
	TextComp->SetText(FText::FromString(FString::Printf(TEXT("%s\n[%s]%s"), *Data.Name, *Data.Type, *PropsText)));
	TextComp->SetRelativeLocation(FVector(0.0f, 0.0f, 150.0f));
	TextComp->SetHorizontalAlignment(EHTA_Center);
	TextComp->SetWorldSize(24.0f);
	TextComp->SetTextRenderColor(RegionColor);
	TextComp->SetHiddenInGame(true);
	TextComp->RegisterComponent();

	// Actor label and folder organization
	Actor->SetActorLabel(Data.Name);
	Actor->SetFolderPath(FName(TEXT("Regions")));

	// Attach region-specific SWBF metadata
	USWBFRegionAssetUserData::AttachToActor(Actor, Data, Context.LevelName);

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

	return Actor;
}

// ---------------------------------------------------------------------------
// SpawnSoundSpaceVolume
// ---------------------------------------------------------------------------

AActor* FSWBFRegionImporter::SpawnSoundSpaceVolume(const FSWBFRegionData& Data, UWorld* World, FSWBFImportContext& Context, TMap<FString, int32>& ClassCounters) const
{
	const FTransform SpawnTransform(Data.Rotation, Data.Position);
	AAudioVolume* Volume = World->SpawnActor<AAudioVolume>(AAudioVolume::StaticClass(), SpawnTransform);
	if (!Volume)
	{
		UE_LOG(LogZeroEditorTools, Warning,
			TEXT("SpawnSoundSpaceVolume: failed to spawn AAudioVolume for region '%s', falling back to generic actor"),
			*Data.Name);
		return SpawnGenericRegionActor(Data, World, Context);
	}

	InitBrushGeometry(Volume, Data);

	const FString Label = FString::Printf(TEXT("SoundSpace_%02d"), ++ClassCounters.FindOrAdd(TEXT("soundspace")));
	Volume->SetActorLabel(Label);

	PostSpawnTypedVolume(Volume, Data, Context);

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Spawned AAudioVolume '%s' for soundspace region '%s'"),
		*Volume->GetActorLabel(), *Data.Name);

	return Volume;
}

// ---------------------------------------------------------------------------
// SpawnDeathRegionVolume
// ---------------------------------------------------------------------------

AActor* FSWBFRegionImporter::SpawnDeathRegionVolume(const FSWBFRegionData& Data, UWorld* World, FSWBFImportContext& Context, TMap<FString, int32>& ClassCounters) const
{
	const FTransform SpawnTransform(Data.Rotation, Data.Position);
	AKillZVolume* Volume = World->SpawnActor<AKillZVolume>(AKillZVolume::StaticClass(), SpawnTransform);
	if (!Volume)
	{
		UE_LOG(LogZeroEditorTools, Warning,
			TEXT("SpawnDeathRegionVolume: failed to spawn AKillZVolume for region '%s', falling back to generic actor"),
			*Data.Name);
		return SpawnGenericRegionActor(Data, World, Context);
	}

	InitBrushGeometry(Volume, Data);

	const FString Label = FString::Printf(TEXT("DeathRegion_%02d"), ++ClassCounters.FindOrAdd(TEXT("deathregion")));
	Volume->SetActorLabel(Label);

	PostSpawnTypedVolume(Volume, Data, Context);

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Spawned AKillZVolume '%s' for deathregion region '%s'"),
		*Volume->GetActorLabel(), *Data.Name);

	return Volume;
}

// ---------------------------------------------------------------------------
// SpawnDamageRegionVolume
// ---------------------------------------------------------------------------

AActor* FSWBFRegionImporter::SpawnDamageRegionVolume(const FSWBFRegionData& Data, UWorld* World, FSWBFImportContext& Context, TMap<FString, int32>& ClassCounters) const
{
	const FTransform SpawnTransform(Data.Rotation, Data.Position);
	APainCausingVolume* Volume = World->SpawnActor<APainCausingVolume>(APainCausingVolume::StaticClass(), SpawnTransform);
	if (!Volume)
	{
		UE_LOG(LogZeroEditorTools, Warning,
			TEXT("SpawnDamageRegionVolume: failed to spawn APainCausingVolume for region '%s', falling back to generic actor"),
			*Data.Name);
		return SpawnGenericRegionActor(Data, World, Context);
	}

	InitBrushGeometry(Volume, Data);

	Volume->bPainCausing = 1;
	Volume->bEntryPain = 1;
	const FString* DamageRateStr = Data.InlineParams.Find(TEXT("damagerate"));
	Volume->DamagePerSec = DamageRateStr ? FCString::Atof(**DamageRateStr) : 0.0f;

	const FString Label = FString::Printf(TEXT("DamageRegion_%02d"), ++ClassCounters.FindOrAdd(TEXT("damageregion")));
	Volume->SetActorLabel(Label);

	PostSpawnTypedVolume(Volume, Data, Context);

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Spawned APainCausingVolume '%s' for damageregion region '%s' (DamagePerSec=%.2f)"),
		*Volume->GetActorLabel(), *Data.Name, Volume->DamagePerSec);

	return Volume;
}

int32 FSWBFRegionImporter::Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context)
{
	SlowTask.EnterProgressFrame(static_cast<float>(RegionData.Num()),
		FText::Format(LOCTEXT("SpawningRegions", "Spawning {0} regions..."),
			FText::AsNumber(RegionData.Num())));

	if (GEditor == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("GEditor is null, cannot spawn region actors"));
		return 0;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Editor world is null, cannot spawn region actors"));
		return 0;
	}

	int32 SpawnedCount = 0;
	TMap<FString, int32> ClassCounters;

	for (const FSWBFRegionData& Data : RegionData)
	{
		if (!Data.bValid)
		{
			continue;
		}

		AActor* SpawnedActor = nullptr;
		if (Data.RegionClass == TEXT("soundspace"))
		{
			SpawnedActor = SpawnSoundSpaceVolume(Data, World, Context, ClassCounters);
		}
		else if (Data.RegionClass == TEXT("deathregion"))
		{
			SpawnedActor = SpawnDeathRegionVolume(Data, World, Context, ClassCounters);
		}
		else if (Data.RegionClass == TEXT("damageregion"))
		{
			SpawnedActor = SpawnDamageRegionVolume(Data, World, Context, ClassCounters);
		}
		else if (Data.RegionClass == TEXT("soundstream") || Data.RegionClass == TEXT("mapbounds"))
		{
			// Phase 32 stubs: route to generic path for now
			SpawnedActor = SpawnGenericRegionActor(Data, World, Context);
		}
		else
		{
			// "default" and any unrecognized class
			SpawnedActor = SpawnGenericRegionActor(Data, World, Context);
		}

		if (SpawnedActor)
		{
			++SpawnedCount;
		}
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Spawned %d region actors from %d regions"),
		SpawnedCount, RegionData.Num());

	return SpawnedCount;
}

// ---------------------------------------------------------------------------
// CollectWorldNames
// ---------------------------------------------------------------------------

void FSWBFRegionImporter::CollectWorldNames(TSet<FString>& OutWorldNames) const
{
	for (const auto& Data : RegionData)
	{
		if (!Data.WorldName.IsEmpty())
		{
			OutWorldNames.Add(Data.WorldName);
		}
	}
}

#undef LOCTEXT_NAMESPACE
