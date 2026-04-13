// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "LVLFactory.generated.h"

/**
 * UFactory subclass for importing SWBF .lvl files via File > Import and Content Browser drag-and-drop.
 *
 * Orchestrates the full import pipeline: opens the LVL wrapper, extracts texture/mesh data,
 * imports each asset with per-asset progress feedback, shows a toast notification on completion,
 * and navigates the Content Browser to the output folder.
 */
UCLASS()
class ULVLFactory : public UFactory
{
	GENERATED_BODY()

public:
	ULVLFactory();

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateFile(
		UClass* InClass,
		UObject* InParent,
		FName InName,
		EObjectFlags Flags,
		const FString& Filename,
		const TCHAR* Parms,
		FFeedbackContext* Warn,
		bool& bOutOperationCanceled
	) override;
	//~ End UFactory Interface
};
