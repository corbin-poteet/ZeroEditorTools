// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFWorldImporter.h"
#include "SWBFLevelWrapper.h"
#include "SWBFCoordinateUtils.h"
#include "SWBFNameSanitizer.h"
#include "SWBFInstanceAssetUserData.h"
#include "ZeroEditorTools.h"
#include "ZeroEditorToolsSettings.h"

#include "DataLayer/DataLayerEditorSubsystem.h"

#include "Misc/ScopedSlowTask.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"

THIRD_PARTY_INCLUDES_START
#include <LibSWBF2/Wrappers/Level.h>
#include <LibSWBF2/Wrappers/World.h>
#include <LibSWBF2/Wrappers/Instance.h>
#include <LibSWBF2/Types/LibString.h>
#include <LibSWBF2/Types/List.h>
#include <LibSWBF2/Types/Matrix3x3.h>
#include <LibSWBF2/Hashing.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "SWBFWorldImporter"

// ---------------------------------------------------------------------------
// FSWBFWorldImporter::ShouldImport
// ---------------------------------------------------------------------------

bool FSWBFWorldImporter::ShouldImport() const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	return Settings && Settings->bImportWorldLayout && GetWorkCount() > 0;
}

// ---------------------------------------------------------------------------
// QuatFromMatrix
// ---------------------------------------------------------------------------

/**
 * Convert a 3x3 rotation matrix (SWBF space, row-major) to a quaternion (SWBF space).
 * Uses Shepperd's method for numerical stability.
 */
static FQuat QuatFromMatrix(const float M[3][3])
{
	const float Trace = M[0][0] + M[1][1] + M[2][2];
	float X, Y, Z, W;

	if (Trace > 0.0f)
	{
		const float S = FMath::Sqrt(Trace + 1.0f) * 2.0f;
		W = S / 4.0f;
		X = (M[2][1] - M[1][2]) / S;
		Y = (M[0][2] - M[2][0]) / S;
		Z = (M[1][0] - M[0][1]) / S;
	}
	else if (M[0][0] > M[1][1] && M[0][0] > M[2][2])
	{
		const float S = FMath::Sqrt(1.0f + M[0][0] - M[1][1] - M[2][2]) * 2.0f;
		W = (M[2][1] - M[1][2]) / S;
		X = S / 4.0f;
		Y = (M[0][1] + M[1][0]) / S;
		Z = (M[0][2] + M[2][0]) / S;
	}
	else if (M[1][1] > M[2][2])
	{
		const float S = FMath::Sqrt(1.0f + M[1][1] - M[0][0] - M[2][2]) * 2.0f;
		W = (M[0][2] - M[2][0]) / S;
		X = (M[0][1] + M[1][0]) / S;
		Y = S / 4.0f;
		Z = (M[1][2] + M[2][1]) / S;
	}
	else
	{
		const float S = FMath::Sqrt(1.0f + M[2][2] - M[0][0] - M[1][1]) * 2.0f;
		W = (M[1][0] - M[0][1]) / S;
		X = (M[0][2] + M[2][0]) / S;
		Y = (M[1][2] + M[2][1]) / S;
		Z = S / 4.0f;
	}

	return FQuat(X, Y, Z, W);
}

// ---------------------------------------------------------------------------
// FSWBFInstanceData constructor
// ---------------------------------------------------------------------------

FSWBFInstanceData::FSWBFInstanceData(const LibSWBF2::Wrappers::Instance& InInstance, const FString& InWorldName)
{
	using LibSWBF2::Types::String;
	using LibSWBF2::Types::Vector3;
	using LibSWBF2::Types::Vector4;
	using LibSWBF2::Types::Matrix3x3;
	using LibSWBF2::Types::List;
	using LibSWBF2::FNV;
	using LibSWBF2::FNVHash;

	// Copy name into FString immediately (LibSWBF2 string lifetime)
	const String& InstName = InInstance.GetName();
	Name = FString(ANSI_TO_TCHAR(InstName.Buffer()));

	// Copy entity class name
	const String& ECName = InInstance.GetEntityClassName();
	EntityClassName = FString(ANSI_TO_TCHAR(ECName.Buffer()));

	// Position from Instance wrapper
	Vector3 Pos = InInstance.GetPosition();
	OriginalPosition = FVector(Pos.m_X, Pos.m_Y, Pos.m_Z);
	Position = FSWBFCoordinateUtils::ConvertPosition(Pos.m_X, Pos.m_Y, Pos.m_Z);
	Scale = FVector(1.0f, 1.0f, 1.0f);

	// Check rotation matrix determinant -- det<0 means the SWBF transform
	// includes an X-reflection (mirrored instance).
	const Matrix3x3& Mat = InInstance.GetRotationMatrix();

	// Store original rotation matrix as formatted string for debugging
	OriginalRotationMatrix = FString::Printf(
		TEXT("[%.4f %.4f %.4f] [%.4f %.4f %.4f] [%.4f %.4f %.4f]"),
		Mat.matrix[0][0], Mat.matrix[0][1], Mat.matrix[0][2],
		Mat.matrix[1][0], Mat.matrix[1][1], Mat.matrix[1][2],
		Mat.matrix[2][0], Mat.matrix[2][1], Mat.matrix[2][2]);

	const float Det =
		Mat.matrix[0][0] * (Mat.matrix[1][1] * Mat.matrix[2][2] - Mat.matrix[1][2] * Mat.matrix[2][1]) -
		Mat.matrix[0][1] * (Mat.matrix[1][0] * Mat.matrix[2][2] - Mat.matrix[1][2] * Mat.matrix[2][0]) +
		Mat.matrix[0][2] * (Mat.matrix[1][0] * Mat.matrix[2][1] - Mat.matrix[1][1] * Mat.matrix[2][0]);

	if (Det < 0.0f)
	{
		// Det<0 = rotation * X-reflection in SWBF data.
		// Negate column 0 to extract pure rotation, then apply
		// Scale(-1,1,1) so UE reproduces the reflection at instance level.
		float CleanMat[3][3];
		for (int r = 0; r < 3; r++)
		{
			CleanMat[r][0] = -Mat.matrix[r][0];
			CleanMat[r][1] = Mat.matrix[r][1];
			CleanMat[r][2] = Mat.matrix[r][2];
		}

		// Transpose to match LibSWBF2's row/column convention
		float Transposed[3][3];
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				Transposed[r][c] = CleanMat[c][r];

		FQuat SWBFQuat = QuatFromMatrix(Transposed);
		OriginalRotation = FVector4(SWBFQuat.X, SWBFQuat.Y, SWBFQuat.Z, SWBFQuat.W);
		Rotation = FSWBFCoordinateUtils::ConvertRotation(
			static_cast<float>(SWBFQuat.X),
			static_cast<float>(SWBFQuat.Y),
			static_cast<float>(SWBFQuat.Z),
			static_cast<float>(SWBFQuat.W));
		Scale = FVector(-1.0f, 1.0f, 1.0f);
	}
	else
	{
		Vector4 Rot = InInstance.GetRotation();
		OriginalRotation = FVector4(Rot.m_X, Rot.m_Y, Rot.m_Z, Rot.m_W);
		Rotation = FSWBFCoordinateUtils::ConvertRotation(Rot.m_X, Rot.m_Y, Rot.m_Z, Rot.m_W);
	}

	WorldName = InWorldName;

	// -- Override properties (PROP chunks) --
	List<FNVHash> PropHashes;
	List<String> PropValues;
	InInstance.GetOverriddenProperties(PropHashes, PropValues);
	const size_t PropCount = PropHashes.Size();

	for (size_t pi = 0; pi < PropCount; ++pi)
	{
		String ResolvedName;
		FString PropName;
		if (FNV::Lookup(PropHashes[pi], ResolvedName))
		{
			PropName = FString(ANSI_TO_TCHAR(ResolvedName.Buffer()));
		}
		else
		{
			PropName = FString::Printf(TEXT("0x%08X"), PropHashes[pi]);
		}
		FString PropVal = FString(ANSI_TO_TCHAR(PropValues[pi].Buffer()));
		OverrideProperties.Add(PropName, PropVal);
	}

	bValid = true;
}

// ---------------------------------------------------------------------------
// FSWBFInstanceData::ToString
// ---------------------------------------------------------------------------

FString FSWBFInstanceData::ToString() const
{
	return FString::Printf(
		TEXT("[Instance] '%s' class='%s' world='%s' pos=(%.1f,%.1f,%.1f) props=%d valid=%d"),
		*Name, *EntityClassName, *WorldName,
		Position.X, Position.Y, Position.Z,
		OverrideProperties.Num(), bValid ? 1 : 0);
}

// ---------------------------------------------------------------------------
// GetData
// ---------------------------------------------------------------------------

void FSWBFWorldImporter::GetData(const FSWBFLevelWrapper& LevelWrapper)
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
	using LibSWBF2::Wrappers::Instance;
	using LibSWBF2::Types::String;
	using LibSWBF2::Types::Vector3;
	using LibSWBF2::Types::Matrix3x3;
	using LibSWBF2::FNV;

	// Register known instance property names for FNV hash lookup
	static bool bNamesRegistered = false;
	if (!bNamesRegistered)
	{
		const char* Names[] = {
			"Team", "Label", "Layer", "spawnpath", "AllyCount", "Radius",
			"valuebleed_alliance", "valuebleed_cis", "valuebleed_empire",
			"valuebleed_republic", "valuebleed_neutral", "valuebleed_locals",
			"Value_ATK_Alliance", "Value_ATK_CIS", "Value_ATK_Empire",
			"Value_ATK_Republic", "value_atk_locals",
			"Value_DEF_Alliance", "Value_DEF_CIS", "Value_DEF_Empire",
			"Value_DEF_Republic", "value_def_locals",
			"vo_all_AllCapture", "vo_all_AllLost", "vo_all_ImpCapture",
			"vo_imp_ImpCapture", "vo_imp_ImpLost", "vo_imp_AllCapture",
			"vo_rep_RepCapture", "vo_rep_RepLost", "vo_rep_CISCapture",
			"vo_cis_CISCapture", "vo_cis_CISLost", "vo_cis_RepCapture",
			"hudindex", "hudindexdisplay",
			"captureregion", "controlregion",
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
		const auto& Instances = Wrld.GetInstances();
		const size_t InstCount = Instances.Size();

		for (size_t ii = 0; ii < InstCount; ++ii)
		{
			const Instance& Inst = Instances[ii];
			FSWBFInstanceData Data(Inst, CurrentWorldName);

			// -- Instance chunk-level info (p_Info equivalent) --
			const Matrix3x3& Mat = Inst.GetRotationMatrix();
			const Vector3 Pos = Inst.GetPosition();

			UE_LOG(LogZeroEditorTools, Log,
				TEXT("[Instance %d/%d] world='%s'"),
				static_cast<int32>(ii + 1), static_cast<int32>(InstCount), *CurrentWorldName);
			UE_LOG(LogZeroEditorTools, Log,
				TEXT("  TYPE: %s"), *Data.EntityClassName);
			UE_LOG(LogZeroEditorTools, Log,
				TEXT("  NAME: %s"), *Data.Name);
			UE_LOG(LogZeroEditorTools, Log,
				TEXT("  XFRM rotation: [%.4f %.4f %.4f] [%.4f %.4f %.4f] [%.4f %.4f %.4f]"),
				Mat.matrix[0][0], Mat.matrix[0][1], Mat.matrix[0][2],
				Mat.matrix[1][0], Mat.matrix[1][1], Mat.matrix[1][2],
				Mat.matrix[2][0], Mat.matrix[2][1], Mat.matrix[2][2]);
			UE_LOG(LogZeroEditorTools, Log,
				TEXT("  XFRM position: (%.4f, %.4f, %.4f)"),
				Pos.m_X, Pos.m_Y, Pos.m_Z);

			if (Data.OverrideProperties.Num() > 0)
			{
				UE_LOG(LogZeroEditorTools, Log,
					TEXT("  Override properties (%d):"), Data.OverrideProperties.Num());
				int32 pi = 0;
				for (const auto& Pair : Data.OverrideProperties)
				{
					UE_LOG(LogZeroEditorTools, Log,
						TEXT("    [%d] %s = %s"),
						pi, *Pair.Key, *Pair.Value);
					++pi;
				}
			}
			else
			{
				UE_LOG(LogZeroEditorTools, Log, TEXT("  Override properties: (none)"));
			}

			InstanceData.Add(MoveTemp(Data));
		}
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Extracted %d instances from %d worlds"),
		InstanceData.Num(), static_cast<int32>(WorldCount));
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

int32 FSWBFWorldImporter::Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context)
{
	SlowTask.EnterProgressFrame(static_cast<float>(InstanceData.Num()),
		FText::Format(LOCTEXT("SpawningActors", "Spawning {0} actors..."),
			FText::AsNumber(InstanceData.Num())));

	if (GEditor == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("GEditor is null, cannot spawn actors"));
		return 0;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Editor world is null, cannot spawn actors"));
		return 0;
	}

	int32 SpawnedCount = 0;

	for (const FSWBFInstanceData& Inst : InstanceData)
	{
		if (!Inst.bValid)
		{
			continue;
		}

		const FString LookupKey = Inst.EntityClassName.ToLower();
		UStaticMesh* FoundMesh = Context.GetAsset<UStaticMesh>(LookupKey);
		if (!FoundMesh)
		{
			UE_LOG(LogZeroEditorTools, Verbose,
				TEXT("No mesh found for instance '%s' (class '%s'), skipping"),
				*Inst.Name, *Inst.EntityClassName);
			continue;
		}

		const FTransform SpawnTransform(Inst.Rotation, Inst.Position);
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), SpawnTransform);

		if (Actor)
		{
			Actor->SetActorScale3D(Inst.Scale);
			Actor->GetStaticMeshComponent()->SetStaticMesh(FoundMesh);
			Actor->SetActorLabel(FSWBFNameSanitizer::SanitizeMeshName(Inst.Name, Context.LevelName));
			Actor->SetFolderPath(FName(TEXT("Meshes")));

			// Attach SWBF metadata
			USWBFInstanceAssetUserData::AttachToActor(Actor, Inst, Context.LevelName);

			// Assign to DataLayer (if WP enabled)
			if (Context.bWorldPartitionEnabled && !Inst.WorldName.IsEmpty())
			{
				if (UDataLayerInstance** FoundDL = Context.DataLayerLookup.Find(Inst.WorldName))
				{
					if (UDataLayerEditorSubsystem* DLSubsystem = UDataLayerEditorSubsystem::Get())
					{
						DLSubsystem->AddActorToDataLayer(Actor, *FoundDL);
					}
				}
			}

			++SpawnedCount;
		}
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Spawned %d actors from %d instances"),
		SpawnedCount, InstanceData.Num());

	return SpawnedCount;
}

// ---------------------------------------------------------------------------
// CollectWorldNames
// ---------------------------------------------------------------------------

void FSWBFWorldImporter::CollectWorldNames(TSet<FString>& OutWorldNames) const
{
	for (const auto& Data : InstanceData)
	{
		if (!Data.WorldName.IsEmpty())
		{
			OutWorldNames.Add(Data.WorldName);
		}
	}
}

#undef LOCTEXT_NAMESPACE
