#pragma once

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

#ifdef HKX_EXTRACTOR_BUILD
#define HKX_EXTRACTOR_API
#else
#define HKX_EXTRACTOR_API __declspec(dllimport)
#endif

// Paths are UTF-8. outputPath is the directory that receives the patch files.
// Returns true for a successful comparison, including inputs with no changes.
HKX_EXTRACTOR_API bool __cdecl GenerateBehaviorPatchFiles(
	const char* modCode, const char* originalFile, const char* editedFile, const char* outputPath);

#undef HKX_EXTRACTOR_API

#ifdef __cplusplus
}
#endif
