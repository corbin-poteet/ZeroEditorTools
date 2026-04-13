// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "UObject/Object.h"

class FAssetRegistryTagsContext;

DECLARE_LOG_CATEGORY_EXTERN(LogZeroEditorTools, Log, All);

class FZeroEditorToolsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Handle for the OnGetExtraObjectTagsWithContext delegate */
	FDelegateHandle TagsDelegateHandle;

	/** Static callback that propagates USWBFAssetUserData tags onto owning assets */
	static void OnGetExtraAssetRegistryTags(FAssetRegistryTagsContext Context);
};