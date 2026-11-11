# MultiPaste

Tiny Windows 10/11 x64 clipboard history app. Launch MultiPaste.exe, copy text normally, then press Ctrl+Shift+V. Type to filter, use arrows and Enter to paste, or double-click an item. Escape dismisses the popup. The tray menu opens or clears history and exits.

History stays in memory only: 50 Unicode text items, 4 MiB total, 256 KiB per item. Images are ignored. Exiting discards history. Elevated applications may require manual Ctrl+V after selecting an item. Launch manually; no installer or startup registration.

From an x64 Visual Studio developer shell, build with CMake and MSVC C++ Build Tools plus the Windows SDK:

```
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Executable: build/MultiPaste.exe. No separate runtime installation required.

