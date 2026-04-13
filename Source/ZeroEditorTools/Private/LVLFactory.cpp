// Copyright Epic Games, Inc. All Rights Reserved.

#include "LVLFactory.h"
#include "SWBFLevelWrapper.h"
#include "ZeroEditorToolsSettings.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "SWBFImporterBase.h"
#include "SWBFTextureImporter.h"
#include "SWBFMaterialImporter.h"
#include "SWBFMeshImporter.h"
#include "SWBFTerrainImporter.h"
#include "SWBFWorldImporter.h"
#include "SWBFRegionImporter.h"
#include "SWBFBarrierImporter.h"
#include "SWBFHintNodeImporter.h"
#include "SWBFLightImporter.h"
#include "SWBFSkydomeImporter.h"
#include "SWBFConfigImporter.h"
#include "ZeroEditorTools.h"

#include "Engine/StaticMesh.h"
#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "LVLFactory"

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ULVLFactory::ULVLFactory()
{
	Formats.Add(TEXT("lvl;SWBF Level File"));
	SupportedClass = UStaticMesh::StaticClass();
	bEditorImport = true;
	bCreateNew = false;
	bText = false;
}

// ---------------------------------------------------------------------------
// CreateDataLayersForWorlds
// ---------------------------------------------------------------------------

/**
 * Create or find Private DataLayers for each unique SWBF World name.
 * Only operates when World Partition is enabled.
 * Populates Context.DataLayerLookup with World name -> UDataLayerInstance* mappings.
 */
static void CreateDataLayersForWorlds(UWorld* EditorWorld, FSWBFImportContext& Context)
{
	// Count non-base worlds — only those get DataLayers
	int32 NonBaseWorldCount = Context.UniqueWorldNames.Num();
	if (Context.UniqueWorldNames.Contains(Context.BaseWorldName))
	{
		--NonBaseWorldCount;
	}

	if (NonBaseWorldCount <= 0)
	{
		UE_LOG(LogZeroEditorTools, Log,
			TEXT("No non-base SWBF Worlds -- importing flat without DataLayers"));
		return;
	}

	if (!EditorWorld || !EditorWorld->IsPartitionedWorld())
	{
		UE_LOG(LogZeroEditorTools, Warning,
			TEXT("World Partition not enabled -- skipping DataLayer assignment. %d SWBF Worlds will be imported flat."),
			Context.UniqueWorldNames.Num());
		return;
	}

	Context.bWorldPartitionEnabled = true;
	UDataLayerEditorSubsystem* Subsystem = UDataLayerEditorSubsystem::Get();
	if (!Subsystem)
	{
		UE_LOG(LogZeroEditorTools, Warning,
			TEXT("UDataLayerEditorSubsystem not available -- skipping DataLayer assignment"));
		return;
	}

	// Pre-fetch all existing DataLayers for re-import lookup by short name
	TMap<FString, UDataLayerInstance*> ExistingByName;
	TArray<UDataLayerInstance*> AllDLs = Subsystem->GetAllDataLayers();
	for (UDataLayerInstance* DL : AllDLs)
	{
		ExistingByName.Add(DL->GetDataLayerShortName(), DL);
	}

	for (const FString& WorldName : Context.UniqueWorldNames)
	{
		// First world (base level) goes straight into the level — no DataLayer
		if (WorldName == Context.BaseWorldName)
		{
			continue;
		}

		// Check for existing DataLayer with same short name (re-import support)
		if (UDataLayerInstance** Found = ExistingByName.Find(WorldName))
		{
			Context.DataLayerLookup.Add(WorldName, *Found);
			UE_LOG(LogZeroEditorTools, Log,
				TEXT("Reusing existing DataLayer '%s'"), *WorldName);
			continue;
		}

		// Create new Private DataLayer
		FDataLayerCreationParameters Params;
		Params.bIsPrivate = true;

		UDataLayerInstance* NewDL = Subsystem->CreateDataLayerInstance(Params);
		if (NewDL)
		{
			Subsystem->SetDataLayerShortName(NewDL, WorldName);
			Subsystem->SetDataLayerInitialRuntimeState(NewDL, EDataLayerRuntimeState::Unloaded);
			Context.DataLayerLookup.Add(WorldName, NewDL);
			UE_LOG(LogZeroEditorTools, Log,
				TEXT("Created Private DataLayer '%s'"), *WorldName);
		}
		else
		{
			UE_LOG(LogZeroEditorTools, Warning,
				TEXT("Failed to create DataLayer for World '%s'"), *WorldName);
		}
	}
}

// ---------------------------------------------------------------------------
// FactoryCreateFile -- import pipeline
// ---------------------------------------------------------------------------

UObject* ULVLFactory::FactoryCreateFile(
	UClass* InClass,
	UObject* InParent,
	FName InName,
	EObjectFlags Flags,
	const FString& Filename,
	const TCHAR* Parms,
	FFeedbackContext* Warn,
	bool& bOutOperationCanceled)
{
	// -----------------------------------------------------------------------
	// 1. Open LVL file
	// -----------------------------------------------------------------------
	FSWBFLevelWrapper LevelWrapper;
	if (!LevelWrapper.OpenFile(Filename))
	{
		UE_LOG(LogZeroEditorTools, Error, TEXT("Failed to open LVL file: %s"), *Filename);
		return nullptr;
	}

	// -----------------------------------------------------------------------
	// 1.5. Refresh Asset Registry for reuse queries
	// -----------------------------------------------------------------------
	{
		const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
		if (Settings && Settings->bReuseExistingAssets)
		{
			IAssetRegistry& Registry = IAssetRegistry::GetChecked();
			Registry.ScanPathsSynchronous(TArray<FString>{ TEXT("/Game") }, /*bForceRescan=*/ true);
		}
	}

	// -----------------------------------------------------------------------
	// 2. Setup import context
	// -----------------------------------------------------------------------
	FSWBFImportContext Context;
	Context.LevelName = FPaths::GetBaseFilename(Filename).ToUpper();
	Context.SourceFilePath = Filename;

	// -----------------------------------------------------------------------
	// 3. Build importer pipeline
	// -----------------------------------------------------------------------
	TArray<TUniquePtr<FSWBFImporterBase>> Importers;
	Importers.Add(MakeUnique<FSWBFTextureImporter>());
	Importers.Add(MakeUnique<FSWBFMaterialImporter>());
	Importers.Add(MakeUnique<FSWBFMeshImporter>());
	Importers.Add(MakeUnique<FSWBFTerrainImporter>());
	Importers.Add(MakeUnique<FSWBFWorldImporter>());
	Importers.Add(MakeUnique<FSWBFRegionImporter>());
	Importers.Add(MakeUnique<FSWBFBarrierImporter>());
	Importers.Add(MakeUnique<FSWBFHintNodeImporter>());
	Importers.Add(MakeUnique<FSWBFLightImporter>());
	Importers.Add(MakeUnique<FSWBFSkydomeImporter>());
	Importers.Add(MakeUnique<FSWBFConfigImporter>());

	// -----------------------------------------------------------------------
	// 4. Extract data from LVL
	// -----------------------------------------------------------------------
	for (const auto& Importer : Importers)
	{
		Importer->GetData(LevelWrapper);
	}

	// Log summary
	UE_LOG(LogZeroEditorTools, Log, TEXT("LVL contains: %s"),
		*FString::JoinBy(Importers, TEXT(", "), [](const TUniquePtr<FSWBFImporterBase>& Imp)
		{
			return FString::Printf(TEXT("%d %s"), Imp->GetWorkCount(), *Imp->GetDisplayName());
		}));

	// -----------------------------------------------------------------------
	// 4b. Collect unique World names and create DataLayers
	// -----------------------------------------------------------------------
	Context.BaseWorldName = LevelWrapper.GetBaseWorldName();
	for (const auto& Importer : Importers)
	{
		Importer->CollectWorldNames(Context.UniqueWorldNames);
	}
	Context.WorldCount = Context.UniqueWorldNames.Num();

	// Create or find DataLayers (only operates when WP is enabled)
	UWorld* EditorWorld = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	CreateDataLayersForWorlds(EditorWorld, Context);

	// -----------------------------------------------------------------------
	// 5. Calculate total work for progress bar
	// -----------------------------------------------------------------------
	float TotalWork = 0.f;
	for (const auto& Importer : Importers)
	{
		if (Importer->ShouldImport())
		{
			TotalWork += static_cast<float>(Importer->GetWorkCount());
		}
	}

	// -----------------------------------------------------------------------
	// 6. Run import pipeline
	// -----------------------------------------------------------------------
	FScopedSlowTask SlowTask(TotalWork,
		FText::Format(LOCTEXT("ImportingLVL", "Importing {0}.lvl"), FText::FromString(Context.LevelName)));
	SlowTask.MakeDialog(true);

	TArray<TPair<FString, int32>> Results;

	for (const auto& Importer : Importers)
	{
		if (SlowTask.ShouldCancel())
		{
			bOutOperationCanceled = true;
			break;
		}

		if (!Importer->ShouldImport())
		{
			if (Importer->GetWorkCount() > 0)
			{
				UE_LOG(LogZeroEditorTools, Log,
					TEXT("Skipping %s (disabled in settings)"),
					*Importer->GetDisplayName());
			}
			else
			{
				UE_LOG(LogZeroEditorTools, Log,
					TEXT("Skipping %s (no data found)"),
					*Importer->GetDisplayName());
			}
			continue;
		}

		const int32 Count = Importer->Import(SlowTask, Context);
		Results.Add({ Importer->GetDisplayName(), Count });
	}

	// -----------------------------------------------------------------------
	// 6b. Per-World actor count breakdown for debugging
	// -----------------------------------------------------------------------
	if (Context.WorldCount > 1)
	{
		UE_LOG(LogZeroEditorTools, Log, TEXT("Per-World breakdown:"));
		for (const auto& Pair : Context.DataLayerLookup)
		{
			UE_LOG(LogZeroEditorTools, Log, TEXT("  World '%s' -> DataLayer assigned"), *Pair.Key);
		}
		if (Context.DataLayerLookup.Num() == 0)
		{
			for (const FString& WorldName : Context.UniqueWorldNames)
			{
				UE_LOG(LogZeroEditorTools, Log, TEXT("  World '%s' (no DataLayer -- WP disabled)"), *WorldName);
			}
		}
	}

	// -----------------------------------------------------------------------
	// 7. Toast notification
	// -----------------------------------------------------------------------
	{
		// Build result parts: "50 textures, 30 materials, 20 meshes"
		FString ResultParts;
		for (const auto& Result : Results)
		{
			if (Result.Value > 0)
			{
				if (!ResultParts.IsEmpty())
				{
					ResultParts += TEXT(", ");
				}
				ResultParts += FString::Printf(TEXT("%d %s"), Result.Value, *Result.Key);
			}
		}

		if (ResultParts.IsEmpty())
		{
			ResultParts = TEXT("no assets");
		}

		// Build reuse summary: " — X created, Y reused, Z overwritten" (empty when reuse is off)
		FString ReuseSummary;
		{
			const UZeroEditorToolsSettings* Settings = UZeroEditorToolsSettings::Get();
			if (Settings && Settings->bReuseExistingAssets)
			{
				const FSWBFImportReuseCounts& Stats = Context.ReuseStats;
				ReuseSummary = FString::Printf(TEXT(" \u2014 %d created, %d reused, %d overwritten"),
					Stats.Created, Stats.Reused, Stats.Overwritten);
			}
		}

		// Combine result parts with optional reuse summary for all toast branches
		const FString ResultWithReuse = ResultParts + ReuseSummary;

		FText Message;
		if (bOutOperationCanceled && Context.ErrorCount > 0)
		{
			if (Context.WorldCount > 1)
			{
				Message = FText::Format(
					LOCTEXT("ImportCancelledWithErrorsMultiWorld", "Partial import: {0} from {1}.lvl ({2} worlds, cancelled, {3} errors)"),
					FText::FromString(ResultWithReuse),
					FText::FromString(Context.LevelName),
					FText::AsNumber(Context.WorldCount),
					FText::AsNumber(Context.ErrorCount));
			}
			else
			{
				Message = FText::Format(
					LOCTEXT("ImportCancelledWithErrors", "Partial import: {0} from {1}.lvl (cancelled, {2} errors)"),
					FText::FromString(ResultWithReuse),
					FText::FromString(Context.LevelName),
					FText::AsNumber(Context.ErrorCount));
			}
		}
		else if (bOutOperationCanceled)
		{
			if (Context.WorldCount > 1)
			{
				Message = FText::Format(
					LOCTEXT("ImportCancelledMultiWorld", "Partial import: {0} from {1}.lvl ({2} worlds, cancelled)"),
					FText::FromString(ResultWithReuse),
					FText::FromString(Context.LevelName),
					FText::AsNumber(Context.WorldCount));
			}
			else
			{
				Message = FText::Format(
					LOCTEXT("ImportCancelled", "Partial import: {0} from {1}.lvl (cancelled)"),
					FText::FromString(ResultWithReuse),
					FText::FromString(Context.LevelName));
			}
		}
		else if (Context.ErrorCount > 0)
		{
			if (Context.WorldCount > 1)
			{
				Message = FText::Format(
					LOCTEXT("ImportCompleteWithErrorsMultiWorld", "Imported {0} from {1}.lvl ({2} worlds, {3} errors)"),
					FText::FromString(ResultWithReuse),
					FText::FromString(Context.LevelName),
					FText::AsNumber(Context.WorldCount),
					FText::AsNumber(Context.ErrorCount));
			}
			else
			{
				Message = FText::Format(
					LOCTEXT("ImportCompleteWithErrors", "Imported {0} from {1}.lvl ({2} errors)"),
					FText::FromString(ResultWithReuse),
					FText::FromString(Context.LevelName),
					FText::AsNumber(Context.ErrorCount));
			}
		}
		else
		{
			if (Context.WorldCount > 1)
			{
				Message = FText::Format(
					LOCTEXT("ImportCompleteMultiWorld", "Imported {0} from {1}.lvl ({2} worlds)"),
					FText::FromString(ResultWithReuse),
					FText::FromString(Context.LevelName),
					FText::AsNumber(Context.WorldCount));
			}
			else
			{
				Message = FText::Format(
					LOCTEXT("ImportComplete", "Imported {0} from {1}.lvl"),
					FText::FromString(ResultWithReuse),
					FText::FromString(Context.LevelName));
			}
		}

		FNotificationInfo Info(Message);
		Info.bFireAndForget = true;
		Info.ExpireDuration = 5.0f;
		Info.bUseLargeFont = false;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	// -----------------------------------------------------------------------
	// 8. Content Browser navigation to per-type destination paths
	// -----------------------------------------------------------------------
	{
		if (Context.WrittenFolders.Num() > 0)
		{
			FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			CBModule.Get().SyncBrowserToFolders(Context.WrittenFolders, false);
		}
	}

	// Log final summary (include reuse counts when reuse is enabled)
	{
		FString LogReuseSummary;
		const UZeroEditorToolsSettings* LogSettings = UZeroEditorToolsSettings::Get();
		if (LogSettings && LogSettings->bReuseExistingAssets)
		{
			const FSWBFImportReuseCounts& Stats = Context.ReuseStats;
			LogReuseSummary = FString::Printf(TEXT(" [%d created, %d reused, %d overwritten]"),
				Stats.Created, Stats.Reused, Stats.Overwritten);
		}

		UE_LOG(LogZeroEditorTools, Log, TEXT("LVL import complete: %s from %s.lvl%s%s%s"),
			*FString::JoinBy(Results, TEXT(", "), [](const TPair<FString, int32>& R)
			{
				return FString::Printf(TEXT("%d %s"), R.Value, *R.Key);
			}),
			*Context.LevelName,
			*LogReuseSummary,
			Context.ErrorCount > 0 ? *FString::Printf(TEXT(" (%d errors)"), Context.ErrorCount) : TEXT(""),
			bOutOperationCanceled ? TEXT(" (cancelled)") : TEXT(""));
	}

	return Context.FirstCreatedAsset;
}

#undef LOCTEXT_NAMESPACE
