// LibSWBF2 windows.h stub for UE integration
// pch.h includes <LibSWBF2/windows.h> expecting a local override.
// We provide the real Windows headers here since this module compiles
// in isolation (NoPCHs) and does not inherit UE's MINIMAL_WINDOWS_HEADERS.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// Prevent conflicts with UE's platform headers if they ever leak in
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
