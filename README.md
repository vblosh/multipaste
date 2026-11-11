# MultiPaste

Tiny Windows 10/11 x64 clipboard history app. Launch MultiPaste.exe, copy text or images normally, then press Ctrl+Shift+V. Type to filter, use arrows and Enter to paste, or double-click an item. Escape dismisses the popup. The tray menu opens or clears history and exits.

History stays in memory only: 50 text/image items, 32 MiB of clipboard payloads total, 256 KiB per text item and 16 MiB per image. Images copied as Windows DIB/DIBV5 bitmaps or PNG are retained in their original format and shown as [Image] entries with the source program's executable name, dimensions in pixels, and clipboard payload size in KiB. Source programs or dimensions that cannot be determined are marked unknown. Recopying an identical image updates its source metadata; search for "image" to filter them. The open popup retains a stable snapshot, which can temporarily retain up to another 32 MiB. File copies are not captured as images. Exiting discards history. Elevated applications may require manual Ctrl+V after selecting an item. Launch manually; no installer or startup registration.

From an x64 Visual Studio developer shell, build with CMake and MSVC C++ Build Tools plus the Windows SDK:

```
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Executable: build/MultiPaste.exe. No separate runtime installation required.


Regression checks (from the same developer shell, after building):

```
cl /nologo /EHsc /std:c++17 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN tests/history_tests.cpp /Febuild/history_tests.exe /Fobuild/history_tests.obj /link /SUBSYSTEM:CONSOLE user32.lib shell32.lib
build\history_tests.exe
```

These checks use a simulated clipboard and do not modify your clipboard. They cover text and image payload capture, duplicates, stable popup snapshots, repeated lock retries, self-update suppression, and memory limits. Actual pasting into other applications requires an interactive check.
