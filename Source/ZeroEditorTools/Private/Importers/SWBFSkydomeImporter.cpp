// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFSkydomeImporter.h"
#include "SWBFLevelWrapper.h"
#include "SWBFCoordinateUtils.h"
#include "SWBFAssetUserData.h"
#include "ZeroEditorTools.h"
#include "ZeroEditorToolsSettings.h"

#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceConstant.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "Editor.h"
#include "Misc/ScopedSlowTask.h"

THIRD_PARTY_INCLUDES_START
#include <LibSWBF2/Wrappers/Level.h>
#include <LibSWBF2/Wrappers/World.h>
#include <LibSWBF2/Wrappers/Config.h>
#include <LibSWBF2/Hashing.h>
#include <LibSWBF2/Types/LibString.h>
#include <LibSWBF2/Types/Vector2.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "SWBFSkydomeImporter"

bool FSWBFSkydomeImporter::ShouldImport() const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	return Settings && Settings->bImportSkydome && GetWorkCount() > 0;
}

// ---------------------------------------------------------------------------
// GetData
// ---------------------------------------------------------------------------

void FSWBFSkydomeImporter::GetData(const FSWBFLevelWrapper& LevelWrapper)
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
	using LibSWBF2::Wrappers::Config;
	using LibSWBF2::Wrappers::Field;
	using LibSWBF2::Wrappers::Scope;
	using LibSWBF2::Types::String;
	using LibSWBF2::Types::Vector2;
	using LibSWBF2::FNV;
	using LibSWBF2::EConfigType;
	using LibSWBF2::operator""_fnv;

	// Register known field names for FNV hash lookup
	static bool bNamesRegistered = false;
	if (!bNamesRegistered)
	{
		const char* Names[] = {
			"DomeInfo", "DomeModel", "Geometry", "Height",
			"SkyObject", "Ambient", "Terrain", "FogRange",
			"FogColor", "NearSceneRange", "FarSceneRange",
			"LowResTerrain", "Texture", "TextureSpeed",
			"SkyInfo", "Enable", "Softness", "SoftnessParam",
			"TerrainBumpTexture", "RotationSpeed", "Effect",
			"PatchResolution", "MaxDistance", "DetailTexture",
			"DetailTextureScale", "NumObjects", "VelocityZ",
			"VelocityY", "Distance", "InDirectionFactor",
			"Angle", "Intensity", "Filter", "Threshold",
			"stars", "NumStars", "RandomSeed", "BrightStarPercent",
			"AlphaMin", "ColorSaturation", "TwinkleFactor",
			"TwinkleFrequency", "EnvTexture", "Acceleration",
			"LifeTime", "Color",
		};
		for (const char* Name : Names)
		{
			FNV::Register(Name);
		}
		bNamesRegistered = true;
	}

	const auto& Worlds = LevelPtr->GetWorlds();
	const size_t WorldCount = Worlds.Size();

	for (size_t wi = 0; wi < WorldCount; ++wi)
	{
		const World& Wrld = Worlds[wi];
		const FString CurrentWorldName = FString(ANSI_TO_TCHAR(Wrld.GetName().Buffer()));

		// Copy sky name by value (MSVC temporary lifetime decision from 15-01)
		String SkyNameRaw = Wrld.GetSkyName();
		FString SkyNameStr = FString(ANSI_TO_TCHAR(SkyNameRaw.Buffer()));

		// Deduplication: skip if sky name is empty or already processed
		if (SkyNameStr.IsEmpty() || SeenSkyNames.Contains(SkyNameStr))
		{
			continue;
		}
		SeenSkyNames.Add(SkyNameStr);

		const Config* SkyConfig = LevelPtr->GetConfig(EConfigType::Skydome, SkyNameRaw);
		if (SkyConfig == nullptr)
		{
			UE_LOG(LogZeroEditorTools, Verbose,
				TEXT("No skydome config for sky '%s' in world '%s'"),
				*SkyNameStr, *CurrentWorldName);
			continue;
		}

		// Log skydome config fields for debugging
		{
			auto AllFields = SkyConfig->GetFields();
			UE_LOG(LogZeroEditorTools, Log, TEXT("Skydome config for sky '%s' has %d top-level fields:"),
				*SkyNameStr, static_cast<int32>(AllFields.Size()));
			for (size_t fi = 0; fi < AllFields.Size(); ++fi)
			{
				const Field* F = AllFields[fi];
				if (F)
				{
					FString DataStr = FString(ANSI_TO_TCHAR(F->GetDataString().Buffer()));
					UE_LOG(LogZeroEditorTools, Log, TEXT("  [%d] %s"),
						static_cast<int32>(fi), *DataStr);
					if (!F->m_Scope.IsEmpty())
					{
						FString ScopeStr = FString(ANSI_TO_TCHAR(F->m_Scope.ToString().Buffer()));
						UE_LOG(LogZeroEditorTools, Log, TEXT("  Scope:\n%s"), *ScopeStr);
					}
				}
			}
		}

		// --- Extract DomeModel entries (under DomeInfo scope) ---
		try
		{
			const Field& DomeInfoField = SkyConfig->GetField("DomeInfo"_fnv);
			Scope DIScope = DomeInfoField.m_Scope;
			auto DomeModelFields = DIScope.GetFields("DomeModel"_fnv);

			for (size_t di = 0; di < DomeModelFields.Size(); ++di)
			{
				const Field* DMField = DomeModelFields[di];
				if (!DMField)
				{
					continue;
				}

				try
				{
					Scope DMScope = DMField->m_Scope;
					String GeomStr = DMScope.GetField("Geometry"_fnv).GetString();

					FSWBFDomeModelData Data;
					Data.GeometryName = FString(ANSI_TO_TCHAR(GeomStr.Buffer())).ToLower();
					Data.WorldName = CurrentWorldName;
					Data.SkyName = SkyNameStr;
					Data.bValid = true;

					UE_LOG(LogZeroEditorTools, Verbose,
						TEXT("[DomeModel] '%s' sky='%s' world='%s'"),
						*Data.GeometryName, *SkyNameStr, *CurrentWorldName);

					DomeModelData.Add(MoveTemp(Data));
				}
				catch (...)
				{
					UE_LOG(LogZeroEditorTools, Warning,
						TEXT("Failed to extract DomeModel at index %d for sky '%s'"),
						static_cast<int32>(di), *SkyNameStr);
				}
			}
		}
		catch (...)
		{
			UE_LOG(LogZeroEditorTools, Verbose,
				TEXT("No DomeInfo scope in skydome config for sky '%s'"), *SkyNameStr);
		}

		// --- Extract SkyObject entries (top-level in config, NOT under DomeInfo) ---
		try
		{
			auto SkyObjectFields = SkyConfig->GetFields("SkyObject"_fnv);

			for (size_t si = 0; si < SkyObjectFields.Size(); ++si)
			{
				const Field* SOField = SkyObjectFields[si];
				if (!SOField)
				{
					continue;
				}

				try
				{
					Scope SOScope = SOField->m_Scope;
					String GeomStr = SOScope.GetField("Geometry"_fnv).GetString();
					Vector2 HeightVec = SOScope.GetField("Height"_fnv).GetVector2();

					FSWBFSkyObjectData Data;
					Data.GeometryName = FString(ANSI_TO_TCHAR(GeomStr.Buffer())).ToLower();
					Data.WorldName = CurrentWorldName;
					Data.Height = HeightVec.m_X * FSWBFCoordinateUtils::UnitScale;
					Data.bValid = true;

					UE_LOG(LogZeroEditorTools, Verbose,
						TEXT("[SkyObject] '%s' height=%.1f sky='%s' world='%s'"),
						*Data.GeometryName, Data.Height, *SkyNameStr, *CurrentWorldName);

					SkyObjectData.Add(MoveTemp(Data));
				}
				catch (...)
				{
					UE_LOG(LogZeroEditorTools, Warning,
						TEXT("Failed to extract SkyObject at index %d for sky '%s'"),
						static_cast<int32>(si), *SkyNameStr);
				}
			}
		}
		catch (...)
		{
			UE_LOG(LogZeroEditorTools, Verbose,
				TEXT("No SkyObject fields in skydome config for sky '%s'"), *SkyNameStr);
		}
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Extracted %d dome models and %d sky objects from %d worlds"),
		DomeModelData.Num(), SkyObjectData.Num(), static_cast<int32>(WorldCount));
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

namespace
{
	/**
	 * Assigns actor to a DataLayer if World Partition is enabled.
	 */
	void AssignActorToDataLayer(AActor* Actor, const FString& WorldName, FSWBFImportContext& Context)
	{
		if (!Context.bWorldPartitionEnabled || WorldName.IsEmpty())
		{
			return;
		}

		if (UDataLayerInstance** FoundDL = Context.DataLayerLookup.Find(WorldName))
		{
			if (UDataLayerEditorSubsystem* DLSubsystem = UDataLayerEditorSubsystem::Get())
			{
				DLSubsystem->AddActorToDataLayer(Actor, *FoundDL);
			}
		}
	}
} // anonymous namespace

int32 FSWBFSkydomeImporter::Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context)
{
	SlowTask.EnterProgressFrame(static_cast<float>(GetWorkCount()),
		FText::Format(LOCTEXT("SpawningSkydome", "Spawning {0} skydome actors..."),
			FText::AsNumber(GetWorkCount())));

	if (GEditor == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("GEditor is null, cannot spawn skydome actors"));
		return 0;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Editor world is null, cannot spawn skydome actors"));
		return 0;
	}

	int32 DomeCount = 0;
	int32 SkyObjCount = 0;

	// -----------------------------------------------------------------------
	// Spawn dome model actors (300x uniform scale at origin)
	// -----------------------------------------------------------------------
	for (const FSWBFDomeModelData& Data : DomeModelData)
	{
		if (!Data.bValid)
		{
			continue;
		}

		UStaticMesh* Mesh = Context.GetAsset<UStaticMesh>(Data.GeometryName);
		if (!Mesh)
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Dome geometry '%s' not found in imported meshes -- skipping"), *Data.GeometryName);
			continue;
		}

		// 300x uniform scale at world origin
		const FTransform DomeTransform(FQuat::Identity, FVector::ZeroVector, FVector(300.0f));

		AStaticMeshActor* DomeActor = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), DomeTransform);
		if (!DomeActor)
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Failed to spawn dome actor for geometry '%s'"), *Data.GeometryName);
			continue;
		}

		UStaticMeshComponent* MeshComp = DomeActor->GetStaticMeshComponent();
		MeshComp->SetStaticMesh(Mesh);
		MeshComp->SetMobility(EComponentMobility::Static);

		// Set UseSkydome=true on dome MIC (shading model comes from material flags like all other materials)
		{
			UMaterialInterface* ExistingMat = MeshComp->GetMaterial(0);
			UMaterialInstanceConstant* DomeMIC = Cast<UMaterialInstanceConstant>(ExistingMat);
			if (DomeMIC)
			{
				DomeMIC->SetStaticSwitchParameterValueEditorOnly(
					FMaterialParameterInfo(TEXT("UseSkydome")), true);
				DomeMIC->UpdateStaticPermutation();
				DomeMIC->PostEditChange();
				DomeMIC->MarkPackageDirty();
			}
			else
			{
				UE_LOG(LogZeroEditorTools, Warning,
					TEXT("Dome geometry '%s' material is not a MIC -- cannot set UseSkydome"),
					*Data.GeometryName);
			}
		}

		DomeActor->SetActorLabel(Data.GeometryName);
		DomeActor->SetFolderPath(FName(TEXT("Skydome/Dome")));
		USWBFAssetUserData::AttachToAsset(DomeActor, Data.GeometryName, Context.LevelName, Data.WorldName);
		AssignActorToDataLayer(DomeActor, Data.WorldName, Context);

		++DomeCount;

		UE_LOG(LogZeroEditorTools, Verbose,
			TEXT("Spawned dome actor '%s' (sky '%s', world '%s')"),
			*Data.GeometryName, *Data.SkyName, *Data.WorldName);
	}

	// -----------------------------------------------------------------------
	// Spawn sky object actors (no scale, positioned at (0, 0, Height))
	// -----------------------------------------------------------------------
	for (const FSWBFSkyObjectData& Data : SkyObjectData)
	{
		if (!Data.bValid)
		{
			continue;
		}

		UStaticMesh* Mesh = Context.GetAsset<UStaticMesh>(Data.GeometryName);
		if (!Mesh)
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Sky object geometry '%s' not found in imported meshes -- skipping"), *Data.GeometryName);
			continue;
		}

		// No scale override, positioned at (0, 0, Height)
		const FTransform SkyObjTransform(FQuat::Identity, FVector(0.0f, 0.0f, Data.Height), FVector::OneVector);

		AStaticMeshActor* SkyObjActor = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), SkyObjTransform);
		if (!SkyObjActor)
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Failed to spawn sky object actor for geometry '%s'"), *Data.GeometryName);
			continue;
		}

		UStaticMeshComponent* MeshComp = SkyObjActor->GetStaticMeshComponent();
		MeshComp->SetStaticMesh(Mesh);
		MeshComp->SetMobility(EComponentMobility::Static);

		SkyObjActor->SetActorLabel(Data.GeometryName);
		SkyObjActor->SetFolderPath(FName(TEXT("Skydome/SkyObjects")));
		USWBFAssetUserData::AttachToAsset(SkyObjActor, Data.GeometryName, Context.LevelName, Data.WorldName);
		AssignActorToDataLayer(SkyObjActor, Data.WorldName, Context);

		++SkyObjCount;

		UE_LOG(LogZeroEditorTools, Verbose,
			TEXT("Spawned sky object actor '%s' height=%.1f (world '%s')"),
			*Data.GeometryName, Data.Height, *Data.WorldName);
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Spawned %d dome models and %d sky objects"), DomeCount, SkyObjCount);

	return DomeCount + SkyObjCount;
}

// ---------------------------------------------------------------------------
// CollectWorldNames
// ---------------------------------------------------------------------------

void FSWBFSkydomeImporter::CollectWorldNames(TSet<FString>& OutWorldNames) const
{
	for (const auto& Data : DomeModelData)
	{
		if (!Data.WorldName.IsEmpty())
		{
			OutWorldNames.Add(Data.WorldName);
		}
	}

	for (const auto& Data : SkyObjectData)
	{
		if (!Data.WorldName.IsEmpty())
		{
			OutWorldNames.Add(Data.WorldName);
		}
	}
}

#undef LOCTEXT_NAMESPACE
