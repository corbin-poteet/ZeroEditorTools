// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ZeroEditorTools : ModuleRules
{
	public ZeroEditorTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory, "Public/Importers"),
				System.IO.Path.Combine(ModuleDirectory, "Public/AssetData"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Actors"),
				System.IO.Path.Combine(ModuleDirectory, "Public/Utils"),
			}
			);


		PrivateIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(ModuleDirectory, "Private/Importers"),
				System.IO.Path.Combine(ModuleDirectory, "Private/AssetData"),
				System.IO.Path.Combine(ModuleDirectory, "Private/Actors"),
				System.IO.Path.Combine(ModuleDirectory, "Private/Utils"),
			}
			);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"EditorFramework",
				"UnrealEd",
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"ZeroEngineLibrary",
				"ContentBrowser",
				"AssetRegistry",
				"MeshDescription",
				"StaticMeshDescription",
				"DeveloperSettings",
				"Landscape",
				"DataLayerEditor",
				"NavigationSystem",
			}
			);


		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
