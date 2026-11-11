# HKX Extractor

HKX Extractor is a Windows DLL that compares an original Behavior, AnimData, or AnimSetData file with an edited copy and writes Nemesis patch text.

## Build

Use Visual Studio 2022 with MSVC `v143`, the Windows SDK, and Boost 1.72 headers and libraries for the target architecture. The project looks for Boost under `.deps/boost/` by default. Set the MSBuild properties `BoostRoot` and `BoostLibraryDir` to use a different installation.

Run this from the repository root in a Visual Studio Developer PowerShell.

```powershell
MSBuild.exe .\T2\T2.sln /m /p:Configuration=Release /p:Platform=x64
```

The build produces `T2/x64/Release/HKX_Extractor.dll` and `T2/x64/Release/HKX_Extractor.lib`. The DLL and its host must have the same architecture. The Release x64 DLL uses the Microsoft Visual C++ runtime.

## Call from C++

Include `HKX_Extractor.h`, link against `HKX_Extractor.lib`, and make the DLL available to your application. Dynamic loaders can resolve the C export `GenerateBehaviorPatchFiles` by name with `GetProcAddress`. [Public header](T2/HKX_Extractor.h)

```cpp
#include "HKX_Extractor.h"

bool ok = GenerateBehaviorPatchFiles(
    "mymod",
    R"(D:\inputs\original.xml)",
    R"(D:\inputs\edited.xml)",
    R"(D:\patches\mymod\staggerbehavior)");
```

Use 4 to 6 ASCII letters or digits for the mod code. The DLL lowercases it before writing the patch. All three paths are UTF-8. Pass the desired output directory as the fourth argument. Relative paths are resolved against the host's working directory.

Behavior patches are written directly into the requested directory. AnimData and AnimSetData keep their project subdirectories inside it. The DLL creates missing directories and replaces files whose names match generated patches. Other files in the directory are preserved. Calls in the same process can run consecutively or from multiple threads.

The function returns `true` when comparison succeeds, including inputs with no changes, and `false` on failure. Pair inputs of the same type and use the same first-person or third-person variant for Behavior. The DLL runs `hkxcmd.exe` on each `.hkx` input before comparing it, including files that already contain Havok XML. Place the converter beside the DLL or in the host's working directory. Use the `.xml` extension for Havok XML you want the DLL to read directly.

## Read an error

After `GenerateBehaviorPatchFiles` returns `false`, call `GetBehaviorPatchLastError()` on the same thread. It returns a NUL-terminated message of up to 4095 bytes. The DLL owns the string, which is valid until the next generation call on that thread or thread exit. The function returns an empty string before the first call and after a successful call.

```cpp
if (!ok) {
    const char* message = GetBehaviorPatchLastError();
    // Display the message or copy it into your application's log.
}
```

For Python `ctypes`, set the getter's `argtypes` to `()` and its `restype` to `ctypes.c_char_p`. The getter returns the message as Python bytes.

[Implementation review](IMPLEMENTATION_REVIEW.md)
