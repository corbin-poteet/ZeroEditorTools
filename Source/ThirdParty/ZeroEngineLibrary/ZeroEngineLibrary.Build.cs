// LibSWBF2 Build.cs - Compiles LibSWBF2 as an isolated UE module with RTTI, exceptions, and C++20.
// This module is separate from ZeroEditorTools to keep non-standard compiler settings contained.

using UnrealBuildTool;
using System.IO;

public class ZeroEngineLibrary : ModuleRules
{
	public ZeroEngineLibrary(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;

		// LibSWBF2 requires these non-default compiler settings:
		bUseRTTI = true;              // dynamic_cast usage
		bEnableExceptions = true;      // try/catch usage
		CppStandard = CppStandardVersion.Cpp20;

		// Disable unity builds -- third-party source with its own include patterns breaks unity
		bUseUnity = false;

		// Suppress warnings broadly -- this is vendored third-party code
		bEnableUndefinedIdentifierWarnings = false;
		ShadowVariableWarningLevel = WarningLevel.Off;
		bDisableStaticAnalysis = true;
		UnsafeTypeCastWarningLevel = WarningLevel.Off;

		// Use LibSWBF2's own export mechanism:
		// req.h checks LibSWBF2_EXPORTS → dllexport (when compiling), dllimport (for consumers)
		PrivateDefinitions.Add("LibSWBF2_EXPORTS");

		// Required by LibSWBF2 CMake config for memory-mapped file I/O
		PublicDefinitions.Add("MEMORY_MAPPED_READER");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// MSVC doesn't define GCC byte order macros -- detex.h needs them
			PrivateDefinitions.Add("__ORDER_LITTLE_ENDIAN__=1");
			PrivateDefinitions.Add("__BYTE_ORDER__=1");
			// LibSWBF2 uses WIN32 (no underscore) to guard Windows includes; MSVC only defines _WIN32
			PrivateDefinitions.Add("WIN32");
		}

		// Use fmt in header-only mode to avoid compiling fmt source files
		PublicDefinitions.Add("FMT_HEADER_ONLY=1");

		// FNV hash lookup table for resolving config field names
		string LookupCsvPath = Path.Combine(ModuleDirectory, "lookup.csv").Replace("\\", "/");
		PrivateDefinitions.Add("LOOKUP_CSV_PATH=\"" + LookupCsvPath + "\"");

		// Public include paths -- consumers of this module get these
		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "include"));
		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "ThirdParty", "glm"));
		PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "ThirdParty", "fmt", "include"));

		// Private include paths -- only for compiling LibSWBF2 source files
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "src"));

		// Module dependencies
		PublicDependencyModuleNames.Add("Core");
	}
}
