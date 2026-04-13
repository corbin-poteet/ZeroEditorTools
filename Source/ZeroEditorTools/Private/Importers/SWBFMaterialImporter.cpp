// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFMaterialImporter.h"
#include "SWBFMaterialAssetUserData.h"
#include "SWBFLevelWrapper.h"
#include "SWBFNameSanitizer.h"
#include "ZeroEditorTools.h"
#include "ZeroEditorToolsSettings.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "CollectionManagerModule.h"
#include "ICollectionManager.h"
#include "ICollectionContainer.h"

THIRD_PARTY_INCLUDES_START
#include <LibSWBF2/Wrappers/Level.h>
#include <LibSWBF2/Wrappers/Model.h>
#include <LibSWBF2/Wrappers/Segment.h>
#include <LibSWBF2/Wrappers/Material.h>
#include <LibSWBF2/Types/LibString.h>
#include <LibSWBF2/Types/List.h>
#include <LibSWBF2/Types/Enums.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "SWBFMaterialImporter"

// ---------------------------------------------------------------------------
// FSWBFMaterialImporter::ShouldImport
// ---------------------------------------------------------------------------

bool FSWBFMaterialImporter::ShouldImport() const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	return Settings && Settings->bImportMaterials && GetWorkCount() > 0;
}

// ---------------------------------------------------------------------------
// FSWBFMaterialImporter::GetResolvedDestPath
// ---------------------------------------------------------------------------

FString FSWBFMaterialImporter::GetResolvedDestPath(const FString& LevelName) const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	if (!Settings)
	{
		return FString();
	}
	return SubstituteLevelName(Settings->MaterialDestinationPath.Path, LevelName);
}

// ---------------------------------------------------------------------------
// Flag-to-collection tagging
// ---------------------------------------------------------------------------

namespace SWBFMaterialFlagTags
{
	struct FFlagDef { uint32 Bit; const TCHAR* Name; };

	static const FFlagDef AllFlags[] =
	{
		{ 1,         TEXT("Normal") },
		{ 2,         TEXT("Hardedged") },
		{ 4,         TEXT("Transparent") },
		{ 8,         TEXT("Glossmap") },
		{ 16,        TEXT("Glow") },
		{ 32,        TEXT("BumpMap") },
		{ 64,        TEXT("Additive") },
		{ 128,       TEXT("Specular") },
		{ 256,       TEXT("EnvMap") },
		{ 512,       TEXT("VertexLighting") },
		{ 2048,      TEXT("TiledNormalmap") },
		{ 65536,     TEXT("Doublesided") },
		{ 16777216,  TEXT("Scrolling") },
		{ 33554432,  TEXT("Energy") },
		{ 67108864,  TEXT("Animated") },
		{ 134217728, TEXT("AttachedLight") },
	};

	/** Add a material to "SWBF: <FlagName>" collections for each active flag. */
	void TagAssetWithFlags(UObject* Asset, uint32 Flags)
	{
		if (!Asset || Flags == 0 || !FCollectionManagerModule::IsModuleAvailable())
		{
			return;
		}

		ICollectionManager& Manager = FCollectionManagerModule::GetModule().Get();
		const TSharedRef<ICollectionContainer>& Container = Manager.GetProjectCollectionContainer();
		const FSoftObjectPath AssetPath(Asset);

		for (const FFlagDef& Flag : AllFlags)
		{
			if ((Flags & Flag.Bit) == 0)
			{
				continue;
			}

			const FName CollectionName(*FString::Printf(TEXT("SWBF: %s"), Flag.Name));

			// Create collection if it doesn't exist (Static = simple asset list)
			if (!Container->CollectionExists(CollectionName, ECollectionShareType::CST_Local))
			{
				Container->CreateCollection(CollectionName, ECollectionShareType::CST_Local, ECollectionStorageMode::Static);
			}

			Container->AddToCollection(CollectionName, ECollectionShareType::CST_Local, AssetPath);
		}
	}
}

// EMaterialFlags values from LibSWBF2/Types/Enums.h (bitmask)
namespace SWBFMaterialFlags
{
	constexpr uint32 Normal          = 1;
	constexpr uint32 Hardedged       = 2;
	constexpr uint32 Transparent     = 4;
	constexpr uint32 Glossmap        = 8;
	constexpr uint32 Glow            = 16;
	constexpr uint32 BumpMap         = 32;
	constexpr uint32 Additive        = 64;
	constexpr uint32 Specular        = 128;
	constexpr uint32 EnvMap          = 256;
	constexpr uint32 VertexLighting  = 512;
	constexpr uint32 TiledNormalmap  = 2048;
	constexpr uint32 Doublesided     = 65536;
	constexpr uint32 Scrolling       = 16777216;
	constexpr uint32 Energy          = 33554432;
	constexpr uint32 Animated        = 67108864;
	constexpr uint32 AttachedLight   = 134217728;
}

// ---------------------------------------------------------------------------
// FSWBFMaterialData::ToString
// ---------------------------------------------------------------------------

FString FSWBFMaterialData::ToString() const
{
	return FString::Printf(
		TEXT("[Material] textures=%d flags=0x%X diffuse=(%d,%d,%d) doublesided=%d has_texture=%d valid=%d"),
		TextureNames.Num(), Flags,
		DiffuseColor.R, DiffuseColor.G, DiffuseColor.B,
		bDoublesided ? 1 : 0, bHasTexture ? 1 : 0, bValid ? 1 : 0);
}

// ---------------------------------------------------------------------------
// GetDedupKey
// ---------------------------------------------------------------------------

FString FSWBFMaterialData::GetDedupKey() const
{
	// Build texture slot string: "tex0|tex1|tex2|tex3" (lower-cased, empty string for gaps)
	FString TexPart;
	for (int32 i = 0; i < TextureNames.Num(); ++i)
	{
		if (i > 0)
		{
			TexPart += TEXT("|");
		}
		TexPart += TextureNames[i].ToLower();
	}

	// Encode all stored fields deterministically.
	// SWBFMeshImporter.cpp inline key MUST produce an identical format string.
	return FString::Printf(
		TEXT("%s_%08X_%02X%02X%02X%02X_%02X%02X%02X%02X_%08X_%08X_%08X_%s"),
		*TexPart,
		Flags,
		DiffuseColor.R,    DiffuseColor.G,    DiffuseColor.B,    DiffuseColor.A,
		SpecularColor.R,   SpecularColor.G,   SpecularColor.B,   SpecularColor.A,
		SpecularExponent,
		RawParam0,
		RawParam1,
		*AttachedLight.ToLower());
}

// ---------------------------------------------------------------------------
// operator==
// ---------------------------------------------------------------------------

bool FSWBFMaterialData::operator==(const FSWBFMaterialData& Other) const
{
	return TextureNames       == Other.TextureNames
		&& Flags              == Other.Flags
		&& DiffuseColor       == Other.DiffuseColor
		&& SpecularColor      == Other.SpecularColor
		&& SpecularExponent   == Other.SpecularExponent
		&& RawParam0          == Other.RawParam0
		&& RawParam1          == Other.RawParam1
		&& bHasTexture        == Other.bHasTexture
		&& bDoublesided       == Other.bDoublesided
		&& AttachedLight      == Other.AttachedLight;
}

// ---------------------------------------------------------------------------
// FSWBFMaterialData constructor
// ---------------------------------------------------------------------------

FSWBFMaterialData::FSWBFMaterialData(const LibSWBF2::Wrappers::Material& InMaterial, const FString& InModelName, int32 InSegmentIndex)
{
	using LibSWBF2::Types::String;
	using LibSWBF2::Types::Color4u8;
	using LibSWBF2::EMaterialFlags;

	// ---------------------------------------------------------------
	// Extract texture slots 0-3
	// ---------------------------------------------------------------
	// Generated fallback name for this segment (slot 0 only)
	const FString GeneratedName = FString::Printf(
		TEXT("%s_seg%d"), *InModelName, InSegmentIndex);

	for (uint8_t TexIdx = 0; TexIdx <= 3; ++TexIdx)
	{
		String SlotName;
		if (InMaterial.GetTextureName(TexIdx, SlotName) && SlotName.Length() > 0)
		{
			// Grow array to cover this index, padding gaps with empty strings
			while (TextureNames.Num() <= static_cast<int32>(TexIdx))
			{
				TextureNames.Add(FString());
			}
			TextureNames[TexIdx] = FString(ANSI_TO_TCHAR(SlotName.Buffer()));
		}
	}

	// bHasTexture is true when slot 0 is a real (non-generated) texture name
	if (TextureNames.Num() > 0 && !TextureNames[0].IsEmpty())
	{
		if (TextureNames[0] != GeneratedName)
		{
			bHasTexture = true;
		}
		else
		{
			// Slot 0 is a generated fallback -- clear it so downstream sees no texture
			TextureNames[0] = FString();
		}
	}

	// Skip further extraction for textureless segments
	if (!bHasTexture)
	{
		bValid = false;
		return;
	}

	// ---------------------------------------------------------------
	// Extract flags and diffuse color
	// ---------------------------------------------------------------
	const EMaterialFlags MatFlags = InMaterial.GetFlags();
	Flags = static_cast<uint32>(MatFlags);

	const Color4u8& DiffColor = InMaterial.GetDiffuseColor();
	DiffuseColor = FColor(DiffColor.m_Red, DiffColor.m_Green, DiffColor.m_Blue, DiffColor.m_Alpha);

	bDoublesided = (MatFlags & EMaterialFlags::Doublesided) != 0u;

	// ---------------------------------------------------------------
	// Extract specular color, exponent
	// ---------------------------------------------------------------
	const Color4u8& SpecColor = InMaterial.GetSpecularColor();
	SpecularColor = FColor(SpecColor.m_Red, SpecColor.m_Green, SpecColor.m_Blue, SpecColor.m_Alpha);
	SpecularExponent = InMaterial.GetSpecularExponent();

	// ---------------------------------------------------------------
	// Extract raw parameters (uint32, low byte is the meaningful value)
	// ---------------------------------------------------------------
	RawParam0 = static_cast<uint32>(InMaterial.GetFirstParameter());
	RawParam1 = static_cast<uint32>(InMaterial.GetSecondParameter());

	// ---------------------------------------------------------------
	// Extract attached light name
	// ---------------------------------------------------------------
	AttachedLight = FString(ANSI_TO_TCHAR(InMaterial.GetAttachedLight().Buffer()));

	bValid = true;
}

// ---------------------------------------------------------------------------
// GetData
// ---------------------------------------------------------------------------

void FSWBFMaterialImporter::GetData(const FSWBFLevelWrapper& LevelWrapper)
{
	const auto* LevelPtr = LevelWrapper.GetLevel();
	if (!LevelPtr)
	{
		return;
	}

	using LibSWBF2::Wrappers::Model;
	using LibSWBF2::Wrappers::Segment;
	using LibSWBF2::Wrappers::Material;
	using LibSWBF2::Types::String;

	TSet<FString> SeenKeys;

	const auto& Models = LevelPtr->GetModels();
	const size_t ModelCount = Models.Size();

	for (size_t mi = 0; mi < ModelCount; ++mi)
	{
		const Model& Mdl = Models[mi];
		const String& MdlName = Mdl.GetName();
		const FString ModelName = FString(ANSI_TO_TCHAR(MdlName.Buffer()));

		const auto& Segments = Mdl.GetSegments();
		const size_t SegCount = Segments.Size();

		for (size_t si = 0; si < SegCount; ++si)
		{
			const Segment& Seg = Segments[si];
			const Material& Mat = Seg.GetMaterial();

			FSWBFMaterialData MatData(Mat, ModelName, static_cast<int32>(si));

			// Skip textureless segments
			if (!MatData.bHasTexture)
			{
				continue;
			}

			// ---------------------------------------------------------------
			// Multi-line log: header + conditional detail lines
			// ---------------------------------------------------------------
			{
				// Build comma-joined texture list for the header
				FString TexList;
				for (int32 ti = 0; ti < MatData.TextureNames.Num(); ++ti)
				{
					if (ti > 0) TexList += TEXT(", ");
					TexList += MatData.TextureNames[ti].IsEmpty() ? TEXT("(empty)") : MatData.TextureNames[ti];
				}

				UE_LOG(LogZeroEditorTools, Verbose,
					TEXT("[Material] model='%s' seg=%d tex=[%s] flags=0x%X"),
					*ModelName, static_cast<int32>(si), *TexList, MatData.Flags);

				// Specular detail line (skip if black with full alpha = default "no specular")
				const FColor DefaultSpecular(0, 0, 0, 255);
				if (MatData.SpecularColor != DefaultSpecular || MatData.SpecularExponent != 0)
				{
					UE_LOG(LogZeroEditorTools, Verbose,
						TEXT("    specular=(%d,%d,%d,%d) specExp=%u"),
						MatData.SpecularColor.R, MatData.SpecularColor.G,
						MatData.SpecularColor.B, MatData.SpecularColor.A,
						MatData.SpecularExponent);
				}

				// Parameters detail line (skip if both zero)
				if (MatData.RawParam0 != 0 || MatData.RawParam1 != 0)
				{
					UE_LOG(LogZeroEditorTools, Verbose,
						TEXT("    params=(0x%08X [%u], 0x%08X [%u])"),
						MatData.RawParam0, static_cast<uint8>(MatData.RawParam0),
						MatData.RawParam1, static_cast<uint8>(MatData.RawParam1));
				}

				// Light detail line (skip if no attached light)
				if (!MatData.AttachedLight.IsEmpty())
				{
					UE_LOG(LogZeroEditorTools, Verbose,
						TEXT("    light='%s'"), *MatData.AttachedLight);
				}
			}

			// ---------------------------------------------------------------
			// Deduplicate
			// ---------------------------------------------------------------
			const FString DedupKey = MatData.GetDedupKey();
			if (!SeenKeys.Contains(DedupKey))
			{
				SeenKeys.Add(DedupKey);
				MaterialDataArray.Add(MoveTemp(MatData));
			}
		}
	}

	// -----------------------------------------------------------------------
	// Extraction summary with flag type breakdown
	// -----------------------------------------------------------------------
	{
		int32 GlowCount          = 0;
		int32 EnergyCount        = 0;
		int32 SpecularCount      = 0;
		int32 GlossmapCount      = 0;
		int32 BumpMapCount       = 0;
		int32 TiledNormalmapCount= 0;
		int32 ScrollingCount     = 0;
		int32 AnimatedCount      = 0;
		int32 EnvMapCount        = 0;
		int32 VertexLightingCount= 0;
		int32 MultiTextureCount  = 0;

		auto HasFlag = [](const FSWBFMaterialData& D, uint32 Bit) { return (D.Flags & Bit) != 0; };

		for (const FSWBFMaterialData& D : MaterialDataArray)
		{
			if (HasFlag(D, SWBFMaterialFlags::Glow))           ++GlowCount;
			if (HasFlag(D, SWBFMaterialFlags::Energy))         ++EnergyCount;
			if (HasFlag(D, SWBFMaterialFlags::Specular))       ++SpecularCount;
			if (HasFlag(D, SWBFMaterialFlags::Glossmap))       ++GlossmapCount;
			if (HasFlag(D, SWBFMaterialFlags::BumpMap))        ++BumpMapCount;
			if (HasFlag(D, SWBFMaterialFlags::TiledNormalmap)) ++TiledNormalmapCount;
			if (HasFlag(D, SWBFMaterialFlags::Scrolling))      ++ScrollingCount;
			if (HasFlag(D, SWBFMaterialFlags::Animated))       ++AnimatedCount;
			if (HasFlag(D, SWBFMaterialFlags::EnvMap))         ++EnvMapCount;
			if (HasFlag(D, SWBFMaterialFlags::VertexLighting)) ++VertexLightingCount;
			if (D.TextureNames.Num() > 1)                      ++MultiTextureCount;
		}

		UE_LOG(LogZeroEditorTools, Log,
			TEXT("Extracted %d unique materials from %d models -- Glow:%d Energy:%d Specular:%d Glossmap:%d BumpMap:%d TiledNorm:%d Scrolling:%d Animated:%d EnvMap:%d VtxLit:%d MultiTex:%d"),
			MaterialDataArray.Num(), static_cast<int32>(ModelCount),
			GlowCount, EnergyCount, SpecularCount, GlossmapCount, BumpMapCount,
			TiledNormalmapCount, ScrollingCount, AnimatedCount, EnvMapCount,
			VertexLightingCount, MultiTextureCount);
	}
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

int32 FSWBFMaterialImporter::Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context)
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	const FString DestPath = SubstituteLevelName(Settings->MaterialDestinationPath.Path, Context.LevelName);
	const FString TexturePath = SubstituteLevelName(Settings->TextureDestinationPath.Path, Context.LevelName);

	SlowTask.EnterProgressFrame(static_cast<float>(MaterialDataArray.Num()),
		FText::Format(LOCTEXT("ImportMat", "Creating {0} materials..."),
			FText::AsNumber(MaterialDataArray.Num())));

	// Load single master parent material from plugin content
	UMaterial* ParentMaster = LoadObject<UMaterial>(nullptr,
		TEXT("/ZeroEditorTools/Materials/M_SWBF_MasterMaterial.M_SWBF_MasterMaterial"));

	if (!ParentMaster)
	{
		UE_LOG(LogZeroEditorTools, Error,
			TEXT("Failed to load master parent material: /ZeroEditorTools/Materials/M_SWBF_MasterMaterial"));
		return 0;
	}

	int32 SuccessCount = 0;
	TSet<FString> UsedAssetNames;

	for (const FSWBFMaterialData& MatData : MaterialDataArray)
	{
		if (!MatData.bValid || !MatData.bHasTexture)
		{
			continue;
		}

		// Determine blend mode from flags
		const bool bAdditive    = (MatData.Flags & SWBFMaterialFlags::Additive) != 0;
		const bool bTransparent = (MatData.Flags & SWBFMaterialFlags::Transparent) != 0;
		const bool bHardedged   = (MatData.Flags & SWBFMaterialFlags::Hardedged) != 0;

		EBlendMode DesiredBlendMode = BLEND_Opaque;
		FString BlendModeName = TEXT("Opaque");

		if (bAdditive && (bTransparent || bHardedged))
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Material '%s' has multiple transparency flags (Flags=0x%X) -- using Additive"),
				*MatData.TextureNames[0], MatData.Flags);
		}
		else if (bTransparent && bHardedged)
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Material '%s' has both Transparent and Hardedged flags (Flags=0x%X) -- using Translucent"),
				*MatData.TextureNames[0], MatData.Flags);
		}

		if (bAdditive)
		{
			DesiredBlendMode = BLEND_Additive;
			BlendModeName = TEXT("Additive");
		}
		else if (bTransparent)
		{
			DesiredBlendMode = BLEND_Translucent;
			BlendModeName = TEXT("Translucent");
		}
		else if (bHardedged)
		{
			DesiredBlendMode = BLEND_Masked;
			BlendModeName = TEXT("Masked");
		}

		// ------------------------------------------------------------------
		// Reuse check: skip or overwrite existing asset when enabled
		// ------------------------------------------------------------------
		if (Settings && Settings->bReuseExistingAssets)
		{
			FAssetData ExistingData;
			if (FindExistingAssetByDedupKey(MatData.GetDedupKey(), UMaterialInstanceConstant::StaticClass()->GetClassPathName(), ExistingData))
			{
				UMaterialInstanceConstant* Existing = Cast<UMaterialInstanceConstant>(ExistingData.GetAsset());
				if (Existing)
				{
					if (Settings->bOverwriteExistingAssets)
					{
						// Overwrite in-place: clear all parameters then reapply

						// Determine blend mode from flags (same logic as normal creation path)
						const bool bAdditive_O    = (MatData.Flags & SWBFMaterialFlags::Additive) != 0;
						const bool bTransparent_O = (MatData.Flags & SWBFMaterialFlags::Transparent) != 0;
						const bool bHardedged_O   = (MatData.Flags & SWBFMaterialFlags::Hardedged) != 0;

						EBlendMode OverwriteBlendMode = BLEND_Opaque;
						if (bAdditive_O)       OverwriteBlendMode = BLEND_Additive;
						else if (bTransparent_O) OverwriteBlendMode = BLEND_Translucent;
						else if (bHardedged_O)   OverwriteBlendMode = BLEND_Masked;

						Existing->ClearParameterValuesEditorOnly();
						Existing->BasePropertyOverrides = FMaterialInstanceBasePropertyOverrides();

						// Reapply base property overrides
						FMaterialInstanceBasePropertyOverrides& OvOverrides = Existing->BasePropertyOverrides;
						if (OverwriteBlendMode != BLEND_Opaque)
						{
							OvOverrides.bOverride_BlendMode = true;
							OvOverrides.BlendMode = OverwriteBlendMode;
						}
						if (MatData.bDoublesided)
						{
							OvOverrides.bOverride_TwoSided = true;
							OvOverrides.TwoSided = true;
						}
						if (OverwriteBlendMode == BLEND_Masked)
						{
							OvOverrides.bOverride_OpacityMaskClipValue = true;
							OvOverrides.OpacityMaskClipValue = 0.5f;
						}

						// Reapply DiffuseTexture parameter
						const FString SanitizedTexName_O = FSWBFNameSanitizer::SanitizeTextureName(MatData.TextureNames[0], Context.LevelName);
						const FString TexAssetPath_O = FString::Printf(TEXT("%s/%s.%s"),
							*TexturePath, *SanitizedTexName_O, *SanitizedTexName_O);
						UTexture2D* DiffuseTex_O = LoadObject<UTexture2D>(nullptr, *TexAssetPath_O);
						if (DiffuseTex_O)
						{
							Existing->SetTextureParameterValueEditorOnly(
								FMaterialParameterInfo(TEXT("DiffuseTexture")), DiffuseTex_O);
						}

						// Reapply DiffuseColor
						if (MatData.DiffuseColor != FColor::White)
						{
							Existing->SetVectorParameterValueEditorOnly(
								FMaterialParameterInfo(TEXT("DiffuseColor")), FLinearColor(MatData.DiffuseColor));
						}

						// Reapply emissive
						const bool bIsEmissive_O = (MatData.Flags & SWBFMaterialFlags::Glow) != 0
						                        || (MatData.Flags & SWBFMaterialFlags::Energy) != 0;
						const float DefaultStrength_O = UZeroEditorToolsSettings::Get()->DefaultEmissiveStrength;
						if (bIsEmissive_O)
						{
							Existing->SetScalarParameterValueEditorOnly(
								FMaterialParameterInfo(TEXT("EmissiveStrength")), DefaultStrength_O);
						}

						// Reapply normal map
						const bool bBumpMap_O     = (MatData.Flags & SWBFMaterialFlags::BumpMap) != 0;
						const bool bTiledNormal_O = (MatData.Flags & SWBFMaterialFlags::TiledNormalmap) != 0;
						if (bBumpMap_O || bTiledNormal_O)
						{
							if (MatData.TextureNames.Num() > 1 && !MatData.TextureNames[1].IsEmpty())
							{
								const FString SanitizedNormalName_O = FSWBFNameSanitizer::SanitizeTextureName(MatData.TextureNames[1], Context.LevelName);
								const FString NormalTexPath_O = FString::Printf(TEXT("%s/%s.%s"),
									*TexturePath, *SanitizedNormalName_O, *SanitizedNormalName_O);
								UTexture2D* NormalTex_O = LoadObject<UTexture2D>(nullptr, *NormalTexPath_O);
								if (NormalTex_O)
								{
									float TilingU_O = 1.0f, TilingV_O = 1.0f;
									if (bTiledNormal_O)
									{
										const uint8 RawTileU_O = static_cast<uint8>(MatData.RawParam0);
										const uint8 RawTileV_O = static_cast<uint8>(MatData.RawParam1);
										TilingU_O = (RawTileU_O > 0) ? static_cast<float>(RawTileU_O) : 1.0f;
										TilingV_O = (RawTileV_O > 0) ? static_cast<float>(RawTileV_O) : 1.0f;
									}
									Existing->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("NormalTexture")), NormalTex_O);
									Existing->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("NormalStrength")), 1.0f);
									Existing->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("NormalTilingU")), TilingU_O);
									Existing->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("NormalTilingV")), TilingV_O);
									Existing->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(TEXT("UseNormalMap")), true);
								}
							}
						}

						// Reapply scrolling UV
						const bool bScrolling_O = (MatData.Flags & SWBFMaterialFlags::Scrolling) != 0;
						const bool bAnimated_O  = (MatData.Flags & SWBFMaterialFlags::Animated) != 0;
						if (bScrolling_O && !bAnimated_O)
						{
							const float ScrollU_O = -static_cast<float>(static_cast<uint8>(MatData.RawParam0)) / 255.0f;
							const float ScrollV_O = -static_cast<float>(static_cast<uint8>(MatData.RawParam1)) / 255.0f;
							Existing->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("ScrollSpeedU")), ScrollU_O);
							Existing->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("ScrollSpeedV")), ScrollV_O);
							Existing->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(TEXT("UseScrolling")), true);
						}

						// Reapply animated
						if (bAnimated_O)
						{
							const uint8 RawFrameCount_O = static_cast<uint8>(MatData.RawParam0);
							const uint8 RawAnimSpeed_O  = static_cast<uint8>(MatData.RawParam1);
							const float AnimFrameCount_O = (RawFrameCount_O > 0) ? static_cast<float>(RawFrameCount_O) : 1.0f;
							const float AnimSpeed_O      = (RawAnimSpeed_O  > 0) ? static_cast<float>(RawAnimSpeed_O)  : 1.0f;
							Existing->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("AnimFrameCount")), AnimFrameCount_O);
							Existing->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("AnimSpeed")), AnimSpeed_O);
							Existing->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(TEXT("UseAnimated")), true);
						}

						// Reapply detail map
						const bool bParamsConsumedByOther_O = bTiledNormal_O || bScrolling_O || bAnimated_O;
						if (MatData.TextureNames.Num() > 2 && !MatData.TextureNames[2].IsEmpty())
						{
							const FString SanitizedDetailName_O = FSWBFNameSanitizer::SanitizeTextureName(MatData.TextureNames[2], Context.LevelName);
							const FString DetailTexPath_O = FString::Printf(TEXT("%s/%s.%s"),
								*TexturePath, *SanitizedDetailName_O, *SanitizedDetailName_O);
							UTexture2D* DetailTex_O = LoadObject<UTexture2D>(nullptr, *DetailTexPath_O);
							if (DetailTex_O)
							{
								float DetailTileU_O = 1.0f, DetailTileV_O = 1.0f;
								if (!bParamsConsumedByOther_O)
								{
									DetailTileU_O = static_cast<float>(static_cast<uint8>(MatData.RawParam0));
									DetailTileV_O = static_cast<float>(static_cast<uint8>(MatData.RawParam1));
								}
								Existing->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("DetailTexture")), DetailTex_O);
								Existing->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("DetailStrength")), 1.0f);
								Existing->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("DetailTilingU")), DetailTileU_O);
								Existing->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("DetailTilingV")), DetailTileV_O);
								Existing->SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(TEXT("UseDetailMap")), true);
							}
						}

						Existing->UpdateStaticPermutation();
						Existing->PostEditChange();
						Existing->MarkPackageDirty();
						FAssetRegistryModule::AssetCreated(Existing);

						Context.AddAsset<UMaterialInstanceConstant>(MatData.GetDedupKey(), Existing);
						++Context.ReuseStats.Overwritten;

						UE_LOG(LogZeroEditorTools, Display,
							TEXT("Overwrote existing material: %s"), *MatData.TextureNames[0]);
					}
					else
					{
						UE_LOG(LogZeroEditorTools, Display,
							TEXT("Reusing existing material: %s"), *MatData.TextureNames[0]);
						Context.AddAsset<UMaterialInstanceConstant>(MatData.GetDedupKey(), Existing);
						++Context.ReuseStats.Reused;
					}

					++SuccessCount;
					continue; // skip normal creation
				}
			}
		}

		// Create package and MIC -- increment trailing index to avoid name collisions
		FString AssetName = FSWBFNameSanitizer::SanitizeMaterialName(MatData.TextureNames[0], Context.LevelName);
		if (UsedAssetNames.Contains(AssetName))
		{
			// Strip trailing _NN and increment until unique
			int32 LastUnderscore;
			AssetName.FindLastChar(TEXT('_'), LastUnderscore);
			const FString BasePart = AssetName.Left(LastUnderscore);
			int32 Idx = FCString::Atoi(*AssetName.Mid(LastUnderscore + 1)) + 1;
			do
			{
				AssetName = FString::Printf(TEXT("%s_%02d"), *BasePart, Idx++);
			} while (UsedAssetNames.Contains(AssetName));
		}
		UsedAssetNames.Add(AssetName);
		const FString PackagePath = FString::Printf(TEXT("%s/%s"), *DestPath, *AssetName);

		UPackage* Package = CreatePackage(*PackagePath);
		if (!Package)
		{
			UE_LOG(LogZeroEditorTools, Error, TEXT("Failed to create package: %s"), *PackagePath);
			continue;
		}
		Package->FullyLoad();

		UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(
			Package, *AssetName, RF_Public | RF_Standalone);
		if (!MIC)
		{
			UE_LOG(LogZeroEditorTools, Error, TEXT("Failed to create UMaterialInstanceConstant: %s"), *AssetName);
			continue;
		}

		MIC->SetParentEditorOnly(ParentMaster);

		// Set base property overrides for blend mode, two-sided, opacity mask clip
		FMaterialInstanceBasePropertyOverrides& Overrides = MIC->BasePropertyOverrides;

		if (DesiredBlendMode != BLEND_Opaque)
		{
			Overrides.bOverride_BlendMode = true;
			Overrides.BlendMode = DesiredBlendMode;
		}

		if (MatData.bDoublesided)
		{
			Overrides.bOverride_TwoSided = true;
			Overrides.TwoSided = true;
		}

		if (DesiredBlendMode == BLEND_Masked)
		{
			Overrides.bOverride_OpacityMaskClipValue = true;
			Overrides.OpacityMaskClipValue = 0.5f;
		}

		// Set DiffuseTexture parameter
		const FString SanitizedTexName = FSWBFNameSanitizer::SanitizeTextureName(MatData.TextureNames[0], Context.LevelName);
		const FString TexAssetPath = FString::Printf(TEXT("%s/%s.%s"),
			*TexturePath, *SanitizedTexName, *SanitizedTexName);

		UTexture2D* DiffuseTex = LoadObject<UTexture2D>(nullptr, *TexAssetPath);
		if (DiffuseTex)
		{
			MIC->SetTextureParameterValueEditorOnly(
				FMaterialParameterInfo(TEXT("DiffuseTexture")), DiffuseTex);
		}
		else
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Texture not found for material '%s': %s -- using default"),
				*AssetName, *TexAssetPath);
		}

		// Set DiffuseColor parameter (only if not white)
		if (MatData.DiffuseColor != FColor::White)
		{
			const FLinearColor LinearColor = FLinearColor(MatData.DiffuseColor);
			MIC->SetVectorParameterValueEditorOnly(
				FMaterialParameterInfo(TEXT("DiffuseColor")), LinearColor);
		}


		// Set emissive parameters for Glow/Energy flagged materials
		const bool bIsEmissive = (MatData.Flags & SWBFMaterialFlags::Glow) != 0
		                      || (MatData.Flags & SWBFMaterialFlags::Energy) != 0;
		const float DefaultStrength = UZeroEditorToolsSettings::Get()->DefaultEmissiveStrength;

		if (bIsEmissive)
		{
			MIC->SetScalarParameterValueEditorOnly(
				FMaterialParameterInfo(TEXT("EmissiveStrength")), DefaultStrength);
		}

		// Set normal map parameters for BumpMap/TiledNormalmap flagged materials
		const bool bBumpMap      = (MatData.Flags & SWBFMaterialFlags::BumpMap) != 0;
		const bool bTiledNormal  = (MatData.Flags & SWBFMaterialFlags::TiledNormalmap) != 0;
		const bool bHasNormalFlag = bBumpMap || bTiledNormal;

		bool  bNormalTexApplied = false;
		float TilingU = 1.0f;
		float TilingV = 1.0f;

		if (bHasNormalFlag)
		{
			if (MatData.TextureNames.Num() <= 1 || MatData.TextureNames[1].IsEmpty())
			{
				UE_LOG(LogZeroEditorTools, Warning,
					TEXT("BumpMap flag set but no slot 1 texture found for material '%s'"),
					*AssetName);
			}
			else
			{
				const FString SanitizedNormalName = FSWBFNameSanitizer::SanitizeTextureName(MatData.TextureNames[1], Context.LevelName);
				const FString NormalTexAssetPath = FString::Printf(TEXT("%s/%s.%s"),
					*TexturePath, *SanitizedNormalName, *SanitizedNormalName);

				UTexture2D* NormalTex = LoadObject<UTexture2D>(nullptr, *NormalTexAssetPath);
				if (NormalTex)
				{
					// Fix up compression settings (idempotent)
					if (NormalTex->CompressionSettings != TC_Normalmap || NormalTex->SRGB)
					{
						NormalTex->CompressionSettings = TC_Normalmap;
						NormalTex->SRGB = false;
						NormalTex->UpdateResource();
						NormalTex->MarkPackageDirty();
						UE_LOG(LogZeroEditorTools, Log,
							TEXT("Fixed up texture %s: TC_Normalmap, sRGB=false"), *SanitizedNormalName);
					}

					// Determine tiling values (raw params are uint8 tiling multipliers)
					if (bTiledNormal)
					{
						const uint8 RawTileU = static_cast<uint8>(MatData.RawParam0);
						const uint8 RawTileV = static_cast<uint8>(MatData.RawParam1);
						TilingU = (RawTileU > 0) ? static_cast<float>(RawTileU) : 1.0f;
						TilingV = (RawTileV > 0) ? static_cast<float>(RawTileV) : 1.0f;
						if (RawTileU == 0 || RawTileV == 0)
						{
							UE_LOG(LogZeroEditorTools, Verbose,
								TEXT("NormalTilingU/V was 0 for '%s' -- clamped to 1.0"), *AssetName);
						}
					}
					// bBumpMap (non-TiledNormalmap): TilingU/V stay at default 1.0

					// Set MIC parameters
					MIC->SetTextureParameterValueEditorOnly(
						FMaterialParameterInfo(TEXT("NormalTexture")), NormalTex);
					MIC->SetScalarParameterValueEditorOnly(
						FMaterialParameterInfo(TEXT("NormalStrength")), 1.0f);
					MIC->SetScalarParameterValueEditorOnly(
						FMaterialParameterInfo(TEXT("NormalTilingU")), TilingU);
					MIC->SetScalarParameterValueEditorOnly(
						FMaterialParameterInfo(TEXT("NormalTilingV")), TilingV);
					MIC->SetStaticSwitchParameterValueEditorOnly(
						FMaterialParameterInfo(TEXT("UseNormalMap")), true);

					bNormalTexApplied = true;
				}
				else
				{
					UE_LOG(LogZeroEditorTools, Warning,
						TEXT("Normal map texture not found for '%s': %s"),
						*AssetName, *NormalTexAssetPath);
				}
			}
		}

		// Set scrolling UV parameters for Scrolling-flagged materials
		// Raw params are uint8 scroll speed values (0-255)
		const bool bScrolling = (MatData.Flags & SWBFMaterialFlags::Scrolling) != 0;

		if (bScrolling)
		{
			const float ScrollU = -static_cast<float>(static_cast<uint8>(MatData.RawParam0)) / 255.0f;
			const float ScrollV = -static_cast<float>(static_cast<uint8>(MatData.RawParam1)) / 255.0f;

			MIC->SetScalarParameterValueEditorOnly(
				FMaterialParameterInfo(TEXT("ScrollSpeedU")), ScrollU);
			MIC->SetScalarParameterValueEditorOnly(
				FMaterialParameterInfo(TEXT("ScrollSpeedV")), ScrollV);
			MIC->SetStaticSwitchParameterValueEditorOnly(
				FMaterialParameterInfo(TEXT("UseScrolling")), true);

			if (ScrollU == 0.0f && ScrollV == 0.0f)
			{
				UE_LOG(LogZeroEditorTools, Verbose,
					TEXT("Scrolling speeds are (0,0) for material '%s'"), *AssetName);
			}
		}

		// Animated flag: SubUV flipbook atlas cycling
		// Animated wins over Scrolling if both flags are set
		const bool bAnimated = (MatData.Flags & SWBFMaterialFlags::Animated) != 0;

		if (bAnimated)
		{
			// Animated wins over Scrolling: undo UseScrolling if it was set
			if (bScrolling)
			{
				UE_LOG(LogZeroEditorTools, Warning,
					TEXT("Material '%s' has both Animated and Scrolling flags (Flags=0x%X) -- using Animated"),
					*AssetName, MatData.Flags);
				MIC->SetStaticSwitchParameterValueEditorOnly(
					FMaterialParameterInfo(TEXT("UseScrolling")), false);
			}

			// Decode: uint8 low byte of RawParam0 = frame count, RawParam1 = speed (FPS)
			const uint8 RawFrameCount = static_cast<uint8>(MatData.RawParam0);
			const uint8 RawAnimSpeed  = static_cast<uint8>(MatData.RawParam1);

			// Clamp zero values to prevent degenerate material graph behavior
			const float AnimFrameCount = (RawFrameCount > 0) ? static_cast<float>(RawFrameCount) : 1.0f;
			const float AnimSpeed      = (RawAnimSpeed  > 0) ? static_cast<float>(RawAnimSpeed)  : 1.0f;

			if (RawFrameCount == 0)
			{
				UE_LOG(LogZeroEditorTools, Verbose,
					TEXT("AnimFrameCount was 0 for '%s' -- clamped to 1"), *AssetName);
			}
			if (RawAnimSpeed == 0)
			{
				UE_LOG(LogZeroEditorTools, Verbose,
					TEXT("AnimSpeed was 0 for '%s' -- clamped to 1 FPS"), *AssetName);
			}

			MIC->SetScalarParameterValueEditorOnly(
				FMaterialParameterInfo(TEXT("AnimFrameCount")), AnimFrameCount);
			MIC->SetScalarParameterValueEditorOnly(
				FMaterialParameterInfo(TEXT("AnimSpeed")), AnimSpeed);
			MIC->SetStaticSwitchParameterValueEditorOnly(
				FMaterialParameterInfo(TEXT("UseAnimated")), true);
		}

		// Detail map -- slot 2 texture, params consumed by TiledNormalmap/Scrolling/Animated
		// fall back to 1.0 tiling. 0 is valid (stretch-to-fit) -- do NOT clamp.
		const bool bHasDetailSlot       = MatData.TextureNames.Num() > 2 && !MatData.TextureNames[2].IsEmpty();
		const bool bParamsConsumedByOther = bTiledNormal || bScrolling || bAnimated;

		bool bDetailTexApplied = false;
		FString SanitizedDetailName;
		float DetailTileU = 1.0f;
		float DetailTileV = 1.0f;

		if (bHasDetailSlot)
		{
			SanitizedDetailName = FSWBFNameSanitizer::SanitizeTextureName(MatData.TextureNames[2], Context.LevelName);
			const FString DetailTexPath = FString::Printf(TEXT("%s/%s.%s"),
				*TexturePath, *SanitizedDetailName, *SanitizedDetailName);

			UTexture2D* DetailTex = LoadObject<UTexture2D>(nullptr, *DetailTexPath);
			if (DetailTex)
			{
				if (!bParamsConsumedByOther)
				{
					DetailTileU = static_cast<float>(static_cast<uint8>(MatData.RawParam0));
					DetailTileV = static_cast<float>(static_cast<uint8>(MatData.RawParam1));
					// Note: 0 is NOT clamped -- valid per Blender IO reference (stretch-to-fit)
				}
				// else: bParamsConsumedByOther=true -> DetailTileU/V stay at 1.0 fallback

				MIC->SetTextureParameterValueEditorOnly(
					FMaterialParameterInfo(TEXT("DetailTexture")), DetailTex);
				MIC->SetScalarParameterValueEditorOnly(
					FMaterialParameterInfo(TEXT("DetailStrength")), 1.0f);
				MIC->SetScalarParameterValueEditorOnly(
					FMaterialParameterInfo(TEXT("DetailTilingU")), DetailTileU);
				MIC->SetScalarParameterValueEditorOnly(
					FMaterialParameterInfo(TEXT("DetailTilingV")), DetailTileV);
				MIC->SetStaticSwitchParameterValueEditorOnly(
					FMaterialParameterInfo(TEXT("UseDetailMap")), true);

				bDetailTexApplied = true;
			}
			else
			{
				UE_LOG(LogZeroEditorTools, Warning,
					TEXT("Detail texture not found for '%s': %s"), *AssetName, *DetailTexPath);
			}
		}

		MIC->UpdateStaticPermutation();
		MIC->PostEditChange();

		// Attach material-specific metadata (subclass with render flags) before FinalizeAsset
		USWBFMaterialAssetUserData::AttachToMaterial(MIC, MatData, Context.LevelName);
		FinalizeAsset(MIC, MatData.TextureNames[0], Context.LevelName, Context.SourceFilePath, /*bSkipMetadata=*/true);

		// Tag material in Content Browser collections by active flags
		SWBFMaterialFlagTags::TagAssetWithFlags(MIC, MatData.Flags);

		Context.AddAsset<UMaterialInstanceConstant>(MatData.GetDedupKey(), MIC);
		++SuccessCount;
		++Context.ReuseStats.Created;

		UE_LOG(LogZeroEditorTools, Log, TEXT("Created material instance: %s (Parent: %s, Tex: %s)"),
			*AssetName, *BlendModeName,
			DiffuseTex ? *SanitizedTexName : TEXT("MISSING"));

		if (bIsEmissive)
		{
			const TCHAR* FlagName = (MatData.Flags & SWBFMaterialFlags::Glow) != 0 ? TEXT("Glow") : TEXT("Energy");
			UE_LOG(LogZeroEditorTools, Verbose,
				TEXT("  emissive flag=%s strength=%.2f"), FlagName, DefaultStrength);
		}

		if (bHasNormalFlag && bNormalTexApplied)
		{
			const TCHAR* NormalFlagName = bTiledNormal ? TEXT("TiledNormalmap") : TEXT("BumpMap");
			UE_LOG(LogZeroEditorTools, Verbose,
				TEXT("  normal flag=%s tilingU=%.2f tilingV=%.2f"), NormalFlagName, TilingU, TilingV);
		}

		if (bScrolling)
		{
			UE_LOG(LogZeroEditorTools, Verbose,
				TEXT("  scrolling speedU=%u speedV=%u"),
				static_cast<uint8>(MatData.RawParam0), static_cast<uint8>(MatData.RawParam1));
		}

		if (bAnimated)
		{
			UE_LOG(LogZeroEditorTools, Verbose,
				TEXT("  animated frameCount=%u speed=%u FPS"),
				static_cast<uint8>(MatData.RawParam0), static_cast<uint8>(MatData.RawParam1));
		}

		if (bHasDetailSlot && bDetailTexApplied)
		{
			UE_LOG(LogZeroEditorTools, Verbose,
				TEXT("  detail slot2='%s' tilingU=%.0f tilingV=%.0f paramsConsumed=%s"),
				*SanitizedDetailName, DetailTileU, DetailTileV,
				bParamsConsumedByOther ? TEXT("yes") : TEXT("no"));
		}
	}

	if (SuccessCount > 0)
	{
		Context.WrittenFolders.AddUnique(DestPath);
	}

	UE_LOG(LogZeroEditorTools, Log, TEXT("Created %d material instances in %s"),
		SuccessCount, *DestPath);

	return SuccessCount;
}

#undef LOCTEXT_NAMESPACE
