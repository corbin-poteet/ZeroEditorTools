// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWBFBarrierImporter.h"
#include "SWBFBarrierActor.h"
#include "Components/BoxComponent.h"
#include "SWBFLevelWrapper.h"
#include "SWBFCoordinateUtils.h"
#include "SWBFAssetUserData.h"
#include "ZeroEditorTools.h"
#include "ZeroEditorToolsSettings.h"

#include "DataLayer/DataLayerEditorSubsystem.h"

#include "Misc/ScopedSlowTask.h"
#include "Editor.h"

THIRD_PARTY_INCLUDES_START
#include <LibSWBF2/Wrappers/Level.h>
#include <LibSWBF2/Wrappers/World.h>
#include <LibSWBF2/Types/LibString.h>
#include <LibSWBF2/Types/List.h>
#include <LibSWBF2/Types/Vector3.h>
#include <LibSWBF2/Types/Vector4.h>
#include <LibSWBF2/Types/Matrix3x3.h>
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "SWBFBarrierImporter"

bool FSWBFBarrierImporter::ShouldImport() const
{
	const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
	return Settings && Settings->bImportBarriers && GetWorkCount() > 0;
}

// ---------------------------------------------------------------------------
// QuatFromMatrix
// ---------------------------------------------------------------------------

// Duplicated from SWBFWorldImporter.cpp -- Shepperd's method quaternion extraction
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
// FSWBFBarrierData constructor
// ---------------------------------------------------------------------------

FSWBFBarrierData::FSWBFBarrierData(const LibSWBF2::Wrappers::Barrier& InBarrier, const FString& InWorldName)
{
	using LibSWBF2::Types::Vector3;
	using LibSWBF2::Types::Matrix3x3;

	// Copy name (LibSWBF2 string lifetime)
	Name = FString(ANSI_TO_TCHAR(InBarrier.GetName().Buffer()));

	// Convert position -- negate SWBF Z so UE Y has correct sign
	// SWBF barriers store Z with opposite sign convention vs other entities
	const Vector3& Pos = InBarrier.GetPosition();
	Position = FSWBFCoordinateUtils::ConvertPosition(Pos.m_X, Pos.m_Y, -Pos.m_Z);

	// Rotation via determinant-check pattern (handles reflected transforms)
	const Matrix3x3& Mat = InBarrier.GetRotationMatrix();

	const float Det =
		Mat.matrix[0][0] * (Mat.matrix[1][1] * Mat.matrix[2][2] - Mat.matrix[1][2] * Mat.matrix[2][1]) -
		Mat.matrix[0][1] * (Mat.matrix[1][0] * Mat.matrix[2][2] - Mat.matrix[1][2] * Mat.matrix[2][0]) +
		Mat.matrix[0][2] * (Mat.matrix[1][0] * Mat.matrix[2][1] - Mat.matrix[1][1] * Mat.matrix[2][0]);

	if (Det < 0.0f)
	{
		float CleanMat[3][3];
		for (int r = 0; r < 3; r++)
		{
			CleanMat[r][0] = -Mat.matrix[r][0];
			CleanMat[r][1] = Mat.matrix[r][1];
			CleanMat[r][2] = Mat.matrix[r][2];
		}

		float Transposed[3][3];
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				Transposed[r][c] = CleanMat[c][r];

		FQuat SWBFQuat = QuatFromMatrix(Transposed);
		Rotation = FSWBFCoordinateUtils::ConvertRotation(
			static_cast<float>(SWBFQuat.X),
			static_cast<float>(SWBFQuat.Y),
			static_cast<float>(SWBFQuat.Z),
			static_cast<float>(SWBFQuat.W));
	}
	else
	{
		float Transposed[3][3];
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 3; c++)
				Transposed[r][c] = Mat.matrix[c][r];

		FQuat SWBFQuat = QuatFromMatrix(Transposed);
		Rotation = FSWBFCoordinateUtils::ConvertRotation(
			static_cast<float>(SWBFQuat.X),
			static_cast<float>(SWBFQuat.Y),
			static_cast<float>(SWBFQuat.Z),
			static_cast<float>(SWBFQuat.W));
	}

	// Convert size (half-extents -> UE half-extents for UBoxComponent)
	// Barriers are 2D planes in SWBF (X/Z extents, Y is typically 0).
	// Give a reasonable default height so they're visible in the editor.
	const Vector3& Sz = InBarrier.GetSize();
	const float BarrierHeight = (Sz.m_Y < 0.01f) ? 5.0f : Sz.m_Y; // Default 5m if flat
	Size = FSWBFCoordinateUtils::ConvertSize(Sz.m_X, BarrierHeight, Sz.m_Z);

	// Copy barrier flag (cast from LibSWBF2 uint32 to int32 bitmask)
	Flag = static_cast<int32>(InBarrier.GetFlag());

	WorldName = InWorldName;
	bValid = true;
}

// ---------------------------------------------------------------------------
// FSWBFBarrierData::ToString
// ---------------------------------------------------------------------------

FString FSWBFBarrierData::ToString() const
{
	return FString::Printf(
		TEXT("[Barrier] '%s' world='%s' pos=(%.1f,%.1f,%.1f) size=(%.1f,%.1f,%.1f) flag=%d valid=%d"),
		*Name, *WorldName,
		Position.X, Position.Y, Position.Z,
		Size.X, Size.Y, Size.Z, Flag, bValid ? 1 : 0);
}

// ---------------------------------------------------------------------------
// GetData
// ---------------------------------------------------------------------------

void FSWBFBarrierImporter::GetData(const FSWBFLevelWrapper& LevelWrapper)
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
	using LibSWBF2::Wrappers::Barrier;

	const auto& Worlds = LevelPtr->GetWorlds();
	const size_t WorldCount = Worlds.Size();

	for (size_t wi = 0; wi < WorldCount; ++wi)
	{
		const World& Wrld = Worlds[wi];
		const FString CurrentWorldName = FString(ANSI_TO_TCHAR(Wrld.GetName().Buffer()));
		const auto& Barriers = Wrld.GetBarriers();
		const size_t BarrierCount = Barriers.Size();

		for (size_t bi = 0; bi < BarrierCount; ++bi)
		{
			FSWBFBarrierData Data(Barriers[bi], CurrentWorldName);

			UE_LOG(LogZeroEditorTools, Verbose,
				TEXT("[Barrier] '%s' world='%s' pos=(%.1f,%.1f,%.1f) size=(%.1f,%.1f,%.1f) flag=0x%02X"),
				*Data.Name, *CurrentWorldName,
				Data.Position.X, Data.Position.Y, Data.Position.Z,
				Data.Size.X, Data.Size.Y, Data.Size.Z, Data.Flag);

			BarrierData.Add(MoveTemp(Data));
		}
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Extracted %d barriers from %d worlds"),
		BarrierData.Num(), static_cast<int32>(WorldCount));
}

// ---------------------------------------------------------------------------
// Import
// ---------------------------------------------------------------------------

int32 FSWBFBarrierImporter::Import(FScopedSlowTask& SlowTask, FSWBFImportContext& Context)
{
	SlowTask.EnterProgressFrame(static_cast<float>(BarrierData.Num()),
		FText::Format(LOCTEXT("SpawningBarriers", "Spawning {0} barriers..."),
			FText::AsNumber(BarrierData.Num())));

	if (GEditor == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("GEditor is null, cannot spawn barrier actors"));
		return 0;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (World == nullptr)
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Editor world is null, cannot spawn barrier actors"));
		return 0;
	}

	int32 SpawnedCount = 0;

	for (const FSWBFBarrierData& Data : BarrierData)
	{
		if (!Data.bValid)
		{
			continue;
		}

		const FTransform SpawnTransform(Data.Rotation, Data.Position);
		ASWBFBarrierActor* BarrierActor = World->SpawnActor<ASWBFBarrierActor>(
			ASWBFBarrierActor::StaticClass(), SpawnTransform);
		if (!BarrierActor)
		{
			continue;
		}

		// Set box extent from SWBF barrier size
		BarrierActor->BoxComponent->SetBoxExtent(Data.Size);

		// Store SWBF barrier flag
		BarrierActor->SWBFBarrierFlag = Data.Flag;

		// Actor label: "BarrierName [Flag=0xNN]"
		BarrierActor->SetActorLabel(FString::Printf(TEXT("%s [Flag=0x%02X]"), *Data.Name, Data.Flag));
		BarrierActor->SetFolderPath(FName(TEXT("Barriers")));

		// Attach SWBF metadata (WMETA-01)
		USWBFAssetUserData::AttachToAsset(BarrierActor, Data.Name, Context.LevelName, Data.WorldName);

		// Store barrier flag as actor tag for querying
		BarrierActor->Tags.Add(*FString::Printf(TEXT("SWBFBarrierFlag:%u"), Data.Flag));

		// Assign to DataLayer (if WP enabled)
		if (Context.bWorldPartitionEnabled && !Data.WorldName.IsEmpty())
		{
			if (UDataLayerInstance** FoundDL = Context.DataLayerLookup.Find(Data.WorldName))
			{
				if (UDataLayerEditorSubsystem* DLSubsystem = UDataLayerEditorSubsystem::Get())
				{
					DLSubsystem->AddActorToDataLayer(BarrierActor, *FoundDL);
				}
			}
		}

		++SpawnedCount;
	}

	UE_LOG(LogZeroEditorTools, Log,
		TEXT("Spawned %d barrier actors from %d barriers"),
		SpawnedCount, BarrierData.Num());

	return SpawnedCount;
}

// ---------------------------------------------------------------------------
// CollectWorldNames
// ---------------------------------------------------------------------------

void FSWBFBarrierImporter::CollectWorldNames(TSet<FString>& OutWorldNames) const
{
	for (const auto& Data : BarrierData)
	{
		if (!Data.WorldName.IsEmpty())
		{
			OutWorldNames.Add(Data.WorldName);
		}
	}
}

#undef LOCTEXT_NAMESPACE