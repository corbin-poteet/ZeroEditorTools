// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFConfigImporter.h"
#include "SWBFLevelWrapper.h"
#include "SWBFConfigAssetUserData.h"
#include "SWBFAssetUserData.h"
#include "ZeroEditorTools.h"
#include "ZeroEditorToolsSettings.h"

#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Editor.h"
#include "Misc/ScopedSlowTask.h"

THIRD_PARTY_INCLUDES_START
#include <LibSWBF2/Wrappers/Level.h>
#include <LibSWBF2/Wrappers/Config.h>
#include <LibSWBF2/Hashing.h>
#include <LibSWBF2/Types/LibString.h>
#include <LibSWBF2/Types/Vector2.h>
#include <LibSWBF2/Types/Vector3.h>
#include <LibSWBF2/Types/Vector4.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "SWBFConfigImporter"

// ---------------------------------------------------------------------------
// FSWBFConfigImporter::ShouldImport
// ---------------------------------------------------------------------------

bool FSWBFConfigImporter::ShouldImport() const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	return Settings && Settings->bImportConfig && GetWorkCount() > 0;
}

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

namespace
{
	/** Returns a human-readable string for an EConfigType enum value. */
	FString ConfigTypeToString(LibSWBF2::EConfigType Type)
	{
		using LibSWBF2::EConfigType;
		switch (Type)
		{
			case EConfigType::Lighting:           return TEXT("Lighting");
			case EConfigType::Effect:             return TEXT("Effect");
			case EConfigType::Boundary:           return TEXT("Boundary");
			case EConfigType::Skydome:            return TEXT("Skydome");
			case EConfigType::Path:               return TEXT("Path");
			case EConfigType::Combo:              return TEXT("Combo");
			case EConfigType::Music:              return TEXT("Music");
			case EConfigType::FoleyFX:            return TEXT("FoleyFX");
			case EConfigType::Sound:              return TEXT("Sound");
			case EConfigType::TriggerSoundRegion: return TEXT("TriggerSoundRegion");
			case EConfigType::HUD:                return TEXT("HUD");
			default:                              return TEXT("Unknown");
		}
	}

} // anonymous namespace

// ---------------------------------------------------------------------------
// FSWBFConfigData constructor
// ---------------------------------------------------------------------------

FSWBFConfigData::FSWBFConfigData(const LibSWBF2::Wrappers::Config& InConfig)
{
	using LibSWBF2::EConfigType;
	using LibSWBF2::FNV;
	using LibSWBF2::Types::String;

	// Resolve config type to human-readable string
	ConfigTypeName = ConfigTypeToString(InConfig.m_Type);

	// Resolve config name from FNV hash, fallback to hex
	{
		String ResolvedName;
		if (FNV::Lookup(InConfig.m_Name, ResolvedName))
		{
			ConfigName = FString(ANSI_TO_TCHAR(ResolvedName.Buffer()));
		}
		else
		{
			ConfigName = FString::Printf(TEXT("0x%08X"), static_cast<uint32>(InConfig.m_Name));
		}
	}

	// Extract all fields
	auto TopFields = InConfig.GetFields();
	for (size_t fi = 0; fi < TopFields.Size(); ++fi)
	{
		const LibSWBF2::Wrappers::Field* F = TopFields[fi];
		if (!F)
		{
			continue;
		}

		FString FieldName = FString(ANSI_TO_TCHAR(F->GetName().Buffer()));

		// Build value string
		const uint8_t NumVals = F->GetNumValues();
		FString FieldData;
		for (uint8_t vi = 0; vi < NumVals; ++vi)
		{
			if (vi > 0) { FieldData += TEXT(", "); }
			FString StrVal = FString(ANSI_TO_TCHAR(F->GetString(vi).Buffer()));
			if (StrVal.IsEmpty())
			{
				FieldData += FString::SanitizeFloat(F->GetFloat(vi));
			}
			else
			{
				FieldData += StrVal;
			}
		}

		Fields.Add(FieldName, FieldData);

		// Extract SkyInfo sub-scope fields for Skydome configs
		if (InConfig.m_Type == EConfigType::Skydome && FieldName == TEXT("SkyInfo") && !F->m_Scope.IsEmpty())
		{
			using LibSWBF2::Wrappers::Scope;
			using LibSWBF2::operator""_fnv;

			FSWBFSkyInfoData SkyInfo;
			SkyInfo.Name = FieldData;

			Scope SIScope = F->m_Scope;

			// Store all sub-scope fields as strings for the TMap
			auto ScopeFields = SIScope.GetFields();
			for (size_t si = 0; si < ScopeFields.Size(); ++si)
			{
				const LibSWBF2::Wrappers::Field* SF = ScopeFields[si];
				if (!SF)
				{
					continue;
				}

				FString SFName = FString(ANSI_TO_TCHAR(SF->GetName().Buffer()));
				const uint8_t SFNumVals = SF->GetNumValues();
				FString SFData;
				for (uint8_t svi = 0; svi < SFNumVals; ++svi)
				{
					if (svi > 0) { SFData += TEXT(", "); }
					FString SFStr = FString(ANSI_TO_TCHAR(SF->GetString(svi).Buffer()));
					if (SFStr.IsEmpty())
					{
						SFData += FString::SanitizeFloat(SF->GetFloat(svi));
					}
					else
					{
						SFData += SFStr;
					}
				}
				SkyInfo.Fields.Add(SFName, SFData);
			}

			// Parse known fog fields
			try { SkyInfo.bEnable = SIScope.GetField("Enable"_fnv).GetFloat() != 0.0f; } catch (...) {}
			try { auto ColorVec = SIScope.GetField("FogColor"_fnv).GetVector3(); SkyInfo.FogColor = FLinearColor(ColorVec.m_X / 255.0f, ColorVec.m_Y / 255.0f, ColorVec.m_Z / 255.0f, 1.0f); } catch (...) {}
			try { auto FR = SIScope.GetField("FogRange"_fnv).GetVector2(); SkyInfo.FogRange = FVector2D(FR.m_X, FR.m_Y); } catch (...) {}
			try { auto NSR = SIScope.GetField("NearSceneRange"_fnv).GetVector4(); SkyInfo.NearSceneRange = FVector4(NSR.m_X, NSR.m_Y, NSR.m_Z, NSR.m_W); } catch (...) {}
			try { auto FSR = SIScope.GetField("FarSceneRange"_fnv).GetVector2(); SkyInfo.FarSceneRange = FVector2D(FSR.m_X, FSR.m_Y); } catch (...) {}

			SkyInfos.Add(MoveTemp(SkyInfo));
		}
	}

	bValid = true;
}

// ---------------------------------------------------------------------------
// FSWBFConfigData::ToString
// ---------------------------------------------------------------------------

FString FSWBFConfigData::ToString() const
{
	return FString::Printf(
		TEXT("[Config] '%s' type='%s' fields=%d skyinfos=%d valid=%d"),
		*ConfigName, *ConfigTypeName, Fields.Num(), SkyInfos.Num(), bValid ? 1 : 0);
}

// ---------------------------------------------------------------------------
// GetData
// ---------------------------------------------------------------------------

void FSWBFConfigImporter::GetData(const FSWBFLevelWrapper& LevelWrapper)
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

	using LibSWBF2::EConfigType;
	using LibSWBF2::FNV;

	// Register known config field names for FNV hash resolution
	static bool bNamesRegistered = false;
	if (!bNamesRegistered)
	{
		const char* Names[] = {
			// Path config
			"Path", "PathCount", "PathType", "SplineType", "Node", "Nodes",
			"Knot", "Properties", "Time",
			// Effect/Particle config
			"ParticleEmitter", "Spawner", "Transformer", "Circle",
			"Size", "Scale", "Spread", "Offset",
			"PositionX", "PositionY", "PositionZ",
			"Velocity", "VelocityRange", "VelocityScale",
			"RotationRange", "RotationVelocity", "StartRotation",
			"ParticleSize", "ParticleDensity", "MaxParticles",
			"BurstCount", "BurstDelay", "StartDelay",
			"FadeInTime", "FadeLength", "DrawDistance", "Length",
			"BlendMode", "SoundName",
			// Color
			"Red", "Green", "Blue", "Saturation",
			// Lighting/Halo
			"Halo", "FlareIntensity", "MaxLodDist",
			// Misc
			"Alpha", "Version",
		};
		for (const char* Name : Names)
		{
			FNV::Register(Name);
		}
		bNamesRegistered = true;
	}

	const auto AllConfigs = LevelPtr->GetConfigs(EConfigType::All);
	const size_t ConfigCount = AllConfigs.Size();

	for (size_t ci = 0; ci < ConfigCount; ++ci)
	{
		const LibSWBF2::Wrappers::Config* Cfg = AllConfigs[ci];
		if (!Cfg)
		{
			continue;
		}

		FSWBFConfigData Data(*Cfg);

		UE_LOG(LogZeroEditorTools, Log,
			TEXT("[Config] '%s:%s' has %d fields"),
			*Data.ConfigTypeName, *Data.ConfigName, Data.Fields.Num());

		UE_LOG(LogZeroEditorTools, Log,
			TEXT("[Config] Type='%s' Name='%s' Fields=%d"),
			*Data.ConfigTypeName, *Data.ConfigName, Data.Fields.Num());

		for (const auto& Pair : Data.Fields)
		{
			UE_LOG(LogZeroEditorTools, Log,
				TEXT("[Config]   %s = %s"),
				*Pair.Key, *Pair.Value);
		}

		for (const FSWBFSkyInfoData& SkyInfo : Data.SkyInfos)
		{
			UE_LOG(LogZeroEditorTools, Log,
				TEXT("[SkyInfo] '%s' Enable=%d FogColor=(%.2f,%.2f,%.2f) FogRange=(%.1f,%.1f) NearScene=(%.1f,%.1f,%.1f,%.1f) FarScene=(%.1f,%.1f)"),
				*SkyInfo.Name, SkyInfo.bEnable,
				SkyInfo.FogColor.R, SkyInfo.FogColor.G, SkyInfo.FogColor.B,
				SkyInfo.FogRange.X, SkyInfo.FogRange.Y,
				SkyInfo.NearSceneRange.X, SkyInfo.NearSceneRange.Y, SkyInfo.NearSceneRange.Z, SkyInfo.NearSceneRange.W,
				SkyInfo.FarSceneRange.X, SkyInfo.FarSceneRange.Y);
		}

		ConfigData.Add(MoveTemp(Data));
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Extracted %d configs from LVL"), ConfigData.Num());
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

int32 FSWBFConfigImporter::Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context)
{
	SlowTask.EnterProgressFrame(static_cast<float>(GetWorkCount()),
		FText::Format(LOCTEXT("AttachingConfigs", "Attaching {0} config metadata to WorldSettings..."),
			FText::AsNumber(GetWorkCount())));

	if (GEditor == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("GEditor is null, cannot attach config metadata"));
		return 0;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Editor world is null, cannot attach config metadata"));
		return 0;
	}

	int32 AttachedCount = 0;

	for (const FSWBFConfigData& Data : ConfigData)
	{
		if (!Data.bValid)
		{
			continue;
		}

		USWBFConfigAssetUserData* Metadata = USWBFConfigAssetUserData::AddConfigToWorldSettings(
			World,
			Data.ConfigTypeName,
			Data.ConfigName,
			Data.Fields,
			Context.LevelName);

		if (Metadata)
		{
			// Append SkyInfo entries
			for (const FSWBFSkyInfoData& SkyInfoData : Data.SkyInfos)
			{
				FSWBFSkyInfo SkyInfo;
				SkyInfo.Name = SkyInfoData.Name;
				SkyInfo.Fields = SkyInfoData.Fields;
				Metadata->SkyInfos.Add(MoveTemp(SkyInfo));
			}

			++AttachedCount;

			UE_LOG(LogZeroEditorTools, Log,
				TEXT("[Config] Added '%s:%s' (%d fields) to WorldSettings"),
				*Data.ConfigTypeName, *Data.ConfigName, Data.Fields.Num());
		}
		else
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("[Config] Failed to add '%s:%s' to WorldSettings"),
				*Data.ConfigTypeName, *Data.ConfigName);
		}
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Attached %d config metadata entries to WorldSettings"), AttachedCount);

	// --- Spawn ExponentialHeightFog from SkyInfo ---
	{
		// Collect all SkyInfos across all Skydome configs
		TArray<const FSWBFSkyInfoData*> AllSkyInfos;
		for (const FSWBFConfigData& Data : ConfigData)
		{
			if (Data.bValid && Data.ConfigTypeName == TEXT("Skydome"))
			{
				for (const FSWBFSkyInfoData& SI : Data.SkyInfos)
				{
					AllSkyInfos.Add(&SI);
				}
			}
		}

		if (AllSkyInfos.Num() == 0)
		{
			UE_LOG(LogZeroEditorTools, Log, TEXT("[SkyInfo] No SkyInfo found, skipping fog spawn"));
		}
		else
		{
			if (AllSkyInfos.Num() > 1)
			{
				UE_LOG(LogZeroEditorTools, Warning,
					TEXT("[SkyInfo] Found %d SkyInfo entries, using first one ('%s')"),
					AllSkyInfos.Num(), *AllSkyInfos[0]->Name);
			}

			const FSWBFSkyInfoData& SI = *AllSkyInfos[0];

			if (SI.bEnable)
			{
				AExponentialHeightFog* HeightFog = World->SpawnActor<AExponentialHeightFog>(
					AExponentialHeightFog::StaticClass(), FTransform::Identity);
				if (HeightFog)
				{
					UExponentialHeightFogComponent* FogComp = HeightFog->GetComponent();
					if (FogComp)
					{
						FogComp->SetFogInscatteringColor(SI.FogColor);

						// FogRange: X = near fog distance, Y = far fog distance
						// Map to UE fog density from SWBF range
						if (SI.FogRange.Y > 0.0f)
						{
							// Approximate density from far fog distance (larger range = less dense)
							FogComp->SetFogDensity(1.0f / SI.FogRange.Y);
						}

						// NearSceneRange / FarSceneRange: start/end opacity curves
						FogComp->SetStartDistance(SI.FogRange.X);
						FogComp->SetFogMaxOpacity(1.0f);

						// FarSceneRange.Y as fog cutoff distance
						if (SI.FarSceneRange.Y > 0.0f)
						{
							FogComp->SetFogCutoffDistance(SI.FarSceneRange.Y);
						}
					}

					HeightFog->SetActorLabel(TEXT("HeightFog_SkyInfo"));
					HeightFog->SetFolderPath(FName(TEXT("GlobalLights")));
					USWBFAssetUserData::AttachToAsset(HeightFog, TEXT("SkyInfoFog"), Context.LevelName);

					UE_LOG(LogZeroEditorTools, Log,
						TEXT("[SkyInfo] Spawned ExponentialHeightFog from '%s': Color=(%.2f,%.2f,%.2f) FogRange=(%.1f,%.1f) FarScene=(%.1f,%.1f)"),
						*SI.Name, SI.FogColor.R, SI.FogColor.G, SI.FogColor.B,
						SI.FogRange.X, SI.FogRange.Y, SI.FarSceneRange.X, SI.FarSceneRange.Y);
				}
			}
			else
			{
				UE_LOG(LogZeroEditorTools, Log,
					TEXT("[SkyInfo] Fog disabled (Enable=0) for '%s', skipping fog spawn"),
					*SI.Name);
			}
		}
	}

	return AttachedCount;
}

#undef LOCTEXT_NAMESPACE
