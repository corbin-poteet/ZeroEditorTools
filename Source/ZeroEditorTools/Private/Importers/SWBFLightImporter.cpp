// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFLightImporter.h"
#include "SWBFLevelWrapper.h"
#include "SWBFCoordinateUtils.h"
#include "SWBFAssetUserData.h"
#include "SWBFLightAssetUserData.h"
#include "ZeroEditorTools.h"
#include "ZeroEditorToolsSettings.h"

#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "Editor.h"
#include "Misc/ScopedSlowTask.h"

THIRD_PARTY_INCLUDES_START
#include <LibSWBF2/Wrappers/Level.h>
#include <LibSWBF2/Wrappers/World.h>
#include <LibSWBF2/Wrappers/Config.h>
#include <LibSWBF2/Hashing.h>
#include <LibSWBF2/Types/LibString.h>
#include <LibSWBF2/Types/Vector2.h>
#include <LibSWBF2/Types/Vector3.h>
#include <LibSWBF2/Types/Vector4.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "SWBFLightImporter"

bool FSWBFLightImporter::ShouldImport() const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	return Settings && Settings->bImportLighting && GetWorkCount() > 0;
}

// ---------------------------------------------------------------------------
// GetData
// ---------------------------------------------------------------------------

void FSWBFLightImporter::GetData(const FSWBFLevelWrapper& LevelWrapper)
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
	using LibSWBF2::Types::Vector3;
	using LibSWBF2::Types::Vector4;
	using LibSWBF2::EConfigType;
	using LibSWBF2::FNV;
	using LibSWBF2::operator""_fnv;

	// Register known field names for FNV hash lookup
	static bool bNamesRegistered = false;
	if (!bNamesRegistered)
	{
		const char* Names[] = {
			"GlobalLights", "Light", "Light1", "Light2",
			"Top", "Bottom", "Ambient", "DomeInfo",
			"Type", "Color", "Rotation", "Position",
			"Range", "Cone", "CastShadow", "Static",
			"Region", "PS2BlendMode", "Bidirectional",
			"CastSpecular", "TileUV", "OffsetUV",
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

		// Find the lighting config for this world
		const Config* LightConfig = LevelPtr->GetConfig(
			EConfigType::Lighting,
			Wrld.GetName());

		if (LightConfig == nullptr)
		{
			UE_LOG(LogZeroEditorTools, Verbose, TEXT("No lighting config for world '%s'"), *CurrentWorldName);
			continue;
		}

		// Log lighting config fields for debugging
		{
			auto AllFields = LightConfig->GetFields();
			UE_LOG(LogZeroEditorTools, Log, TEXT("Lighting config for world '%s' has %d top-level fields:"),
				*CurrentWorldName, static_cast<int32>(AllFields.Size()));
			for (size_t fi = 0; fi < AllFields.Size(); ++fi)
			{
				const Field* F = AllFields[fi];
				if (F)
				{
					FString DataStr = FString(ANSI_TO_TCHAR(F->GetDataString().Buffer()));
					UE_LOG(LogZeroEditorTools, Log, TEXT("  [%d] %s"),
						static_cast<int32>(fi), *DataStr);
				}
			}
		}

		// --- Extract GlobalLights scope ---
		FString Light1Name;
		FString Light2Name;

		try
		{
			const Field& GlobalLights = LightConfig->GetField("GlobalLights"_fnv);
			Scope GLScope = GlobalLights.m_Scope;

			// Read global directional light names
			try
			{
				Light1Name = FString(ANSI_TO_TCHAR(GLScope.GetField("Light1"_fnv).GetString().Buffer()));
			}
			catch (...)
			{
				UE_LOG(LogZeroEditorTools, Verbose, TEXT("No Light1 in GlobalLights for world '%s'"), *CurrentWorldName);
			}

			try
			{
				Light2Name = FString(ANSI_TO_TCHAR(GLScope.GetField("Light2"_fnv).GetString().Buffer()));
			}
			catch (...)
			{
				UE_LOG(LogZeroEditorTools, Verbose, TEXT("No Light2 in GlobalLights for world '%s'"), *CurrentWorldName);
			}

			// Read ambient Top color (0-255 range -> 0-1)
			FSWBFAmbientData Ambient;
			Ambient.WorldName = CurrentWorldName;

			try
			{
				Vector3 TopVec = GLScope.GetField("Top"_fnv).GetVector3();
				Ambient.TopColor = FLinearColor(
					TopVec.m_X / 255.0f,
					TopVec.m_Y / 255.0f,
					TopVec.m_Z / 255.0f,
					1.0f);
			}
			catch (...)
			{
				UE_LOG(LogZeroEditorTools, Warning, TEXT("Failed to read ambient Top color for world '%s'"), *CurrentWorldName);
			}

			// Read ambient Bottom color (0-255 range -> 0-1)
			try
			{
				Vector3 BottomVec = GLScope.GetField("Bottom"_fnv).GetVector3();
				Ambient.BottomColor = FLinearColor(
					BottomVec.m_X / 255.0f,
					BottomVec.m_Y / 255.0f,
					BottomVec.m_Z / 255.0f,
					1.0f);
			}
			catch (...)
			{
				UE_LOG(LogZeroEditorTools, Warning, TEXT("Failed to read ambient Bottom color for world '%s'"), *CurrentWorldName);
			}

			// Extract DomeInfo.Ambient from Skydome config for SkyLight color
			try
			{
				const Config* SkyConfig = LevelPtr->GetConfig(
					EConfigType::Skydome,
					Wrld.GetSkyName());
				if (SkyConfig)
				{
					const Field& DomeInfo = SkyConfig->GetField("DomeInfo"_fnv);
					Scope DIScope = DomeInfo.m_Scope;
					Vector3 DomeAmbient = DIScope.GetField("Ambient"_fnv).GetVector3();
					Ambient.DomeAmbientColor = FLinearColor(DomeAmbient.m_X, DomeAmbient.m_Y, DomeAmbient.m_Z, 1.0f);
					Ambient.bHasDomeAmbient = true;
					UE_LOG(LogZeroEditorTools, Log, TEXT("Skydome ambient for world '%s': R=%.3f G=%.3f B=%.3f"),
						*CurrentWorldName, DomeAmbient.m_X, DomeAmbient.m_Y, DomeAmbient.m_Z);
				}
			}
			catch (...)
			{
				UE_LOG(LogZeroEditorTools, Verbose, TEXT("No Skydome DomeInfo.Ambient for world '%s'"), *CurrentWorldName);
			}

			Ambient.bValid = true;
			AmbientData.Add(MoveTemp(Ambient));
		}
		catch (...)
		{
			UE_LOG(LogZeroEditorTools, Warning, TEXT("No GlobalLights scope in lighting config for world '%s'"), *CurrentWorldName);
		}

		// --- Extract Light fields ---
		auto LightFields = LightConfig->GetFields("Light"_fnv);

		for (size_t li = 0; li < LightFields.Size(); ++li)
		{
			const Field* LightField = LightFields[li];
			if (!LightField)
			{
				continue;
			}

			try
			{
				FSWBFLightData Data;
				Data.Name = FString(ANSI_TO_TCHAR(LightField->GetString().Buffer()));
				Data.WorldName = CurrentWorldName;

				Scope LS = LightField->m_Scope;

				// Log the light's scope data
				UE_LOG(LogZeroEditorTools, Log, TEXT("Light '%s' scope:\n%s"),
					*Data.Name, *FString(ANSI_TO_TCHAR(LS.ToString().Buffer())));

				// Store all scope fields as key-value pairs
				{
					auto ScopeFields = LS.GetFields();
					for (size_t fi = 0; fi < ScopeFields.Size(); ++fi)
					{
						const Field* F = ScopeFields[fi];
						if (F)
						{
							FString FieldName = FString(ANSI_TO_TCHAR(F->GetName().Buffer()));
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
							Data.ConfigFields.Add(FieldName, FieldData);
						}
					}
				}

				// Light type: 1=Directional, 2=Point/Omni, 3=Spot
				Data.LightType = static_cast<int32>(LS.GetField("Type"_fnv).GetFloat());

				// Color (0-1 range, used directly)
				try
				{
					Vector3 ColorVec = LS.GetField("Color"_fnv).GetVector3();
					Data.Color = FLinearColor(ColorVec.m_X, ColorVec.m_Y, ColorVec.m_Z, 1.0f);
				}
				catch (...)
				{
					UE_LOG(LogZeroEditorTools, Verbose, TEXT("No Color for light '%s'"), *Data.Name);
				}
				
				try
				{
					Vector4 Rot = LS.GetField("Rotation"_fnv).GetVector4();
					UE_LOG(LogZeroEditorTools, Log, TEXT("Light '%s' raw rotation: X=%.4f Y=%.4f Z=%.4f W=%.4f"),
						*Data.Name, Rot.m_X, Rot.m_Y, Rot.m_Z, Rot.m_W);
					static const FQuat Yaw90(FVector::UpVector, FMath::DegreesToRadians(90.0f));
					Data.Rotation = Yaw90 * FQuat(Rot.m_W, Rot.m_Y, Rot.m_Z, Rot.m_X);
					FRotator DbgRot = Data.Rotation.Rotator();
					UE_LOG(LogZeroEditorTools, Log,
						TEXT("Light '%s' quat=(%.6f,%.6f,%.6f,%.6f) euler=(P=%.4f,Y=%.4f,R=%.4f)"),
						*Data.Name, Data.Rotation.X, Data.Rotation.Y, Data.Rotation.Z, Data.Rotation.W,
						DbgRot.Pitch, DbgRot.Yaw, DbgRot.Roll);
				}
				catch (...)
				{
					UE_LOG(LogZeroEditorTools, Verbose, TEXT("No Rotation for light '%s'"), *Data.Name);
				}

				// Position -- negate Z before conversion (same pattern as barriers)
				try
				{
					Vector3 Pos = LS.GetField("Position"_fnv).GetVector3();
					Data.Position = FSWBFCoordinateUtils::ConvertPosition(Pos.m_X, Pos.m_Y, -Pos.m_Z);
				}
				catch (...)
				{
					UE_LOG(LogZeroEditorTools, Verbose, TEXT("No Position for light '%s'"), *Data.Name);
				}

				// Range -- multiply by UnitScale (meters to centimeters)
				try
				{
					float RangeVal = LS.GetField("Range"_fnv).GetFloat();
					Data.Range = RangeVal * FSWBFCoordinateUtils::UnitScale;
				}
				catch (...)
				{
					UE_LOG(LogZeroEditorTools, Verbose, TEXT("No Range for light '%s'"), *Data.Name);
				}

				// Cone angles (spot lights only)
				if (Data.LightType == 3)
				{
					try
					{
						Vector2 ConeVec = LS.GetField("Cone"_fnv).GetVector2();
						Data.OuterConeAngle = FMath::RadiansToDegrees(ConeVec.m_X);
						Data.InnerConeAngle = Data.OuterConeAngle * 0.8f;
					}
					catch (...)
					{
						UE_LOG(LogZeroEditorTools, Warning, TEXT("No Cone data for spot light '%s'"), *Data.Name);
					}
				}

				// Check if this is a global light
				Data.bIsGlobal = (!Light1Name.IsEmpty() && Data.Name == Light1Name) ||
								 (!Light2Name.IsEmpty() && Data.Name == Light2Name);

				Data.bValid = true;

				UE_LOG(LogZeroEditorTools, Verbose,
					TEXT("[Light] '%s' world='%s' type=%d color=(%.2f,%.2f,%.2f) pos=(%.1f,%.1f,%.1f) range=%.1f global=%d"),
					*Data.Name, *Data.WorldName, Data.LightType,
					Data.Color.R, Data.Color.G, Data.Color.B,
					Data.Position.X, Data.Position.Y, Data.Position.Z,
					Data.Range, Data.bIsGlobal ? 1 : 0);

				LightData.Add(MoveTemp(Data));
			}
			catch (...)
			{
				UE_LOG(LogZeroEditorTools, Warning, TEXT("Failed to extract light at index %d for world '%s'"),
					static_cast<int32>(li), *CurrentWorldName);
			}
		}
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Extracted %d lights and %d ambient configs from %d worlds"),
		LightData.Num(), AmbientData.Num(), static_cast<int32>(WorldCount));
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

int32 FSWBFLightImporter::Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context)
{
	SlowTask.EnterProgressFrame(static_cast<float>(GetWorkCount()),
		FText::Format(LOCTEXT("SpawningLights", "Spawning {0} lights..."),
			FText::AsNumber(GetWorkCount())));

	if (GEditor == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("GEditor is null, cannot spawn light actors"));
		return 0;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Editor world is null, cannot spawn light actors"));
		return 0;
	}

	int32 GlobalCount = 0;
	int32 LocalCount = 0;

	// --- Spawn ambient SkyLights ---
	for (const FSWBFAmbientData& Data : AmbientData)
	{
		if (!Data.bValid)
		{
			continue;
		}

		ASkyLight* SkyLight = World->SpawnActor<ASkyLight>(
			ASkyLight::StaticClass(), FTransform::Identity);
		if (!SkyLight)
		{
			continue;
		}

		USkyLightComponent* SkyComp = SkyLight->GetLightComponent();
		if (SkyComp)
		{
			SkyComp->SetLightColor(Data.bHasDomeAmbient ? Data.DomeAmbientColor : Data.TopColor);
			SkyComp->bLowerHemisphereIsBlack = true;
			SkyComp->SetLowerHemisphereColor(Data.BottomColor);
			SkyComp->SetMobility(EComponentMobility::Movable);
			SkyComp->SetRealTimeCapture(true);
		}

		SkyLight->SetActorLabel(FString::Printf(TEXT("SkyLight_%s"), *Data.WorldName));
		SkyLight->SetFolderPath(FName(TEXT("GlobalLights")));
		USWBFAssetUserData::AttachToAsset(SkyLight, TEXT("Ambient"), Context.LevelName, Data.WorldName);

		// Note: ExponentialHeightFog is now spawned by FSWBFConfigImporter from SkyInfo data

		// Spawn SkyAtmosphere with ground albedo from ambient bottom color
		ASkyAtmosphere* SkyAtmo = World->SpawnActor<ASkyAtmosphere>(
			ASkyAtmosphere::StaticClass(), FTransform::Identity);
		if (SkyAtmo)
		{
			USkyAtmosphereComponent* AtmoComp = SkyAtmo->GetComponent();
			if (AtmoComp)
			{
				AtmoComp->SetGroundAlbedo(Data.BottomColor.ToFColor(true));
			}
			SkyAtmo->SetActorLabel(FString::Printf(TEXT("SkyAtmosphere_%s"), *Data.WorldName));
			SkyAtmo->SetFolderPath(FName(TEXT("GlobalLights")));
			USWBFAssetUserData::AttachToAsset(SkyAtmo, TEXT("Atmosphere"), Context.LevelName, Data.WorldName);

			if (Context.bWorldPartitionEnabled && !Data.WorldName.IsEmpty())
			{
				if (UDataLayerInstance** FoundDL = Context.DataLayerLookup.Find(Data.WorldName))
				{
					if (UDataLayerEditorSubsystem* DLSubsystem = UDataLayerEditorSubsystem::Get())
					{
						DLSubsystem->AddActorToDataLayer(SkyAtmo, *FoundDL);
					}
				}
			}
		}

		// Assign SkyLight to DataLayer (if WP enabled)
		if (Context.bWorldPartitionEnabled && !Data.WorldName.IsEmpty())
		{
			if (UDataLayerInstance** FoundDL = Context.DataLayerLookup.Find(Data.WorldName))
			{
				if (UDataLayerEditorSubsystem* DLSubsystem = UDataLayerEditorSubsystem::Get())
				{
					DLSubsystem->AddActorToDataLayer(SkyLight, *FoundDL);
				}
			}
		}

		++GlobalCount;
	}

	// --- Spawn light actors ---
	for (const FSWBFLightData& Data : LightData)
	{
		if (!Data.bValid)
		{
			continue;
		}

		AActor* LightActor = nullptr;

		switch (Data.LightType)
		{
		case 1: // Directional
		{
			// Use SpawnActorAbsolute to bypass CDO default rotation (-46 pitch)
			// that would otherwise compose with our spawn transform
			const FTransform DirTransform(Data.Rotation, FVector::ZeroVector);
			ADirectionalLight* DirLight = World->SpawnActorAbsolute<ADirectionalLight>(
				ADirectionalLight::StaticClass(), DirTransform);
			if (DirLight)
			{
				UDirectionalLightComponent* DirComp = DirLight->GetComponent();
				if (DirComp)
				{
					DirComp->SetLightColor(Data.Color, false);
					DirComp->SetMobility(EComponentMobility::Movable);
					DirComp->SetEnableLightShaftOcclusion(true);
				}
				FRotator FinalRot = DirLight->GetActorRotation();
				UE_LOG(LogZeroEditorTools, Log,
					TEXT("Light '%s' final editor rotation: P=%.4f Y=%.4f R=%.4f"),
					*Data.Name, FinalRot.Pitch, FinalRot.Yaw, FinalRot.Roll);
				LightActor = DirLight;
			}
			break;
		}
		case 2: // Point/Omni
		{
			const FTransform SpawnTransform(FQuat::Identity, Data.Position);
			APointLight* PtLight = World->SpawnActor<APointLight>(
				APointLight::StaticClass(), SpawnTransform);
			if (PtLight)
			{
				UPointLightComponent* PtComp = PtLight->PointLightComponent;
				if (PtComp)
				{
					PtComp->SetLightColor(Data.Color, false);
					PtComp->SetAttenuationRadius(Data.Range);
					PtComp->SetMobility(EComponentMobility::Stationary);
				}
				LightActor = PtLight;
			}
			break;
		}
		case 3: // Spot
		{
			// Use SpawnActorAbsolute to bypass CDO default rotation (-90 pitch)
			const FTransform SpawnTransform(Data.Rotation, Data.Position);
			ASpotLight* SpLight = World->SpawnActorAbsolute<ASpotLight>(
				ASpotLight::StaticClass(), SpawnTransform);
			if (SpLight)
			{
				USpotLightComponent* SpComp = SpLight->SpotLightComponent;
				if (SpComp)
				{
					SpComp->SetLightColor(Data.Color, false);
					SpComp->SetAttenuationRadius(Data.Range);
					SpComp->SetOuterConeAngle(Data.OuterConeAngle);
					SpComp->SetInnerConeAngle(Data.InnerConeAngle);
					SpComp->SetMobility(EComponentMobility::Stationary);
				}
				LightActor = SpLight;
			}
			break;
		}
		default:
			UE_LOG(LogZeroEditorTools, Warning, TEXT("Unknown light type %d for light '%s'"),
				Data.LightType, *Data.Name);
			continue;
		}

		if (!LightActor)
		{
			continue;
		}

		// Common setup for all light types
		LightActor->SetActorLabel(Data.Name);

		if (Data.bIsGlobal || Data.LightType == 1)
		{
			LightActor->SetFolderPath(FName(TEXT("GlobalLights")));
			++GlobalCount;
		}
		else
		{
			LightActor->SetFolderPath(FName(TEXT("LocalLights")));
			++LocalCount;
		}

		// Attach SWBF light metadata to root component
		USWBFLightAssetUserData::AttachToActor(LightActor, Data, Context.LevelName);

		// Assign to DataLayer (if WP enabled)
		if (Context.bWorldPartitionEnabled && !Data.WorldName.IsEmpty())
		{
			if (UDataLayerInstance** FoundDL = Context.DataLayerLookup.Find(Data.WorldName))
			{
				if (UDataLayerEditorSubsystem* DLSubsystem = UDataLayerEditorSubsystem::Get())
				{
					DLSubsystem->AddActorToDataLayer(LightActor, *FoundDL);
				}
			}
		}
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Spawned %d global lights and %d local lights"),
		GlobalCount, LocalCount);

	return GlobalCount + LocalCount;
}

// ---------------------------------------------------------------------------
// CollectWorldNames
// ---------------------------------------------------------------------------

void FSWBFLightImporter::CollectWorldNames(TSet<FString>& OutWorldNames) const
{
	for (const auto& Data : LightData)
	{
		if (!Data.WorldName.IsEmpty())
		{
			OutWorldNames.Add(Data.WorldName);
		}
	}

	for (const auto& Data : AmbientData)
	{
		if (!Data.WorldName.IsEmpty())
		{
			OutWorldNames.Add(Data.WorldName);
		}
	}
}

#undef LOCTEXT_NAMESPACE
