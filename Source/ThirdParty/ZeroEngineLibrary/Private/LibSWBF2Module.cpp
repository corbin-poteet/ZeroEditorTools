// LibSWBF2 UE Module - loads FNV hash lookup table on startup
#include "Modules/ModuleManager.h"

THIRD_PARTY_INCLUDES_START
#include <LibSWBF2/req.h>
#include <LibSWBF2/Hashing.h>
THIRD_PARTY_INCLUDES_END

class FZeroEngineLibraryModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#ifdef LOOKUP_CSV_PATH
		LibSWBF2::FNV::ReadLookupTable();
#endif
	}

	virtual void ShutdownModule() override
	{
#ifdef LOOKUP_CSV_PATH
		LibSWBF2::FNV::ReleaseLookupTable();
#endif
	}
};

IMPLEMENT_MODULE(FZeroEngineLibraryModule, ZeroEngineLibrary);
