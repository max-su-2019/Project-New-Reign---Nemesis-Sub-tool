# HKX Extractor

HKX Extractor is a Windows command-line tool that compares an original Behavior, AnimData, or AnimSetData file with an edited copy and writes Nemesis patch text.

## Build

Use Visual Studio 2022 with MSVC `v143`, the Windows SDK, and Boost 1.72 headers and libraries for the target architecture. The project looks for Boost under `.deps/boost/` by default. Set the MSBuild properties `BoostRoot` and `BoostLibraryDir` to use a different installation.

Run this from the repository root in a Visual Studio Developer PowerShell.

```powershell
MSBuild.exe .\T2\T2.sln /m /p:Configuration=Release /p:Platform=x64
```

The executable is generated at `T2/x64/Release/HKX Extractor.exe`. The Release x64 executable uses the Microsoft Visual C++ runtime.

## Run

Use the original and edited copies of the same file. For Behavior files, match the first-person or third-person variant. Choose a unique `modcode` of 4 to 6 alphanumeric characters.

```powershell
& ".\T2\x64\Release\HKX Extractor.exe" mymod ".\original.xml" ".\edited.xml"
```

Patch files are written under `mod/<modcode>/` in the working directory. For binary `.hkx` input, place `hkxcmd.exe` in the working directory before running the extractor. To use interactive mode with the bundled `output.txt` and `output2.txt` samples, run the executable from `T2` without arguments.

[Implementation review](IMPLEMENTATION_REVIEW.md)
