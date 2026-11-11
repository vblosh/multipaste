# MultiPaste

A tiny native Windows clipboard history app for Windows 10/11 x64. One executable, no installer, no separate runtime, and no third-party UI framework.

## Usage

1. Launch `MultiPaste.exe`. It runs in the system tray.
2. Copy text or images normally.
3. Press **Ctrl+Shift+V** to open history. Type to filter, use the arrow keys and **Enter** to paste, or double-click an entry.
4. Press **Escape** or switch to another window to dismiss the popup.

The tray menu opens history, clears retained entries, or exits. If another application uses the hotkey, MultiPaste reports the conflict and remains accessible from the tray. Startup is manual.

## Text and images

Unicode text is retained in full. Image entries show the source program's executable name, pixel dimensions, and clipboard payload size, for example:

```text
[Image] chrome | 1920 x 1080 px | 245 KiB
```

Search matches the displayed text, including image source names. Unavailable source names or dimensions are marked unknown. Recopying an identical image moves it to the top and updates its source metadata. The open popup keeps a stable snapshot so incoming copies cannot change the content of a displayed entry.

Images support Windows DIBV5, DIB, and registered PNG clipboard formats. Capture tries them in that order, skipping representations that cannot be retrieved, locked, or retained within the size limit. For example, a 4K bitmap exceeding 16 MiB can still be captured as PNG when the source application offers a smaller PNG representation. Unicode text is the last fallback if no image representation can be captured. If none of the offered formats can be retained, history is unchanged.

Pasting restores the retained representation as image data. MultiPaste does not convert formats, save images to disk, or capture copied files as image entries. The destination app must support the retained clipboard format. Elevated applications may require manual **Ctrl+V** after selecting an entry.

## Limits and storage

- Up to **50 entries** and **32 MiB** of retained clipboard payloads; oldest entries are evicted first.
- Up to **256 KiB per text item** and **16 MiB per image representation**.
- Size labels use **KiB** (1,024 bytes) rounded up; bitmap and compressed PNG sizes can differ substantially.
- An open popup snapshot can temporarily retain up to another **32 MiB**.
- History stays in memory only. Clearing history or exiting discards retained entries; it does not clear the Windows clipboard.

## Build

Install MSVC C++ Build Tools, the Windows SDK, and CMake. From an **x64 Visual Studio developer shell**:

```powershell
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Executable: `build/MultiPaste.exe`. The C/C++ runtime is linked statically. Exit a running copy before rebuilding its executable, or use a different build directory.

## Regression checks

From the same developer shell, after building:

```powershell
cl /nologo /EHsc /std:c++17 /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN tests/history_tests.cpp /Febuild/history_tests.exe /Fobuild/history_tests.obj /link /SUBSYSTEM:CONSOLE user32.lib shell32.lib
build\history_tests.exe
```

The checks use a simulated clipboard and do not modify your clipboard. They cover text and image payload capture, pixel dimensions, duplicates, stable snapshots, repeated lock retries, self-update suppression, memory limits, and fallback between offered formats (including oversized 4K bitmaps with a smaller PNG). Actual pasting into other applications requires an interactive check.
