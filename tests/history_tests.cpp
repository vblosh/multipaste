#include <windows.h>
#include <shellapi.h>
#include <cassert>
#include <iostream>

static HGLOBAL payload;
static UINT available;
static DWORD sequence = 1;
static bool locked = false, retry = false;
static BOOL WINAPI testOpen(HWND) { return !locked; }
static BOOL WINAPI testClose() { return TRUE; }
static HANDLE WINAPI testData(UINT format) { return format == available ? payload : nullptr; }
static BOOL WINAPI testAvailable(UINT format) { return format == available; }
static DWORD WINAPI testSequence() { return sequence; }
static UINT_PTR WINAPI testTimer(HWND, UINT_PTR id, UINT, TIMERPROC) { if (id == 2) retry = true; return id; }
static BOOL WINAPI testKill(HWND, UINT_PTR id) { if (id == 2) retry = false; return TRUE; }
#define OpenClipboard testOpen
#define CloseClipboard testClose
#define GetClipboardData testData
#define IsClipboardFormatAvailable testAvailable
#define GetClipboardSequenceNumber testSequence
#define SetTimer testTimer
#define KillTimer testKill
#include "../main.cpp"

static void copyPayload(UINT format, const void *data, size_t size)
{
    if (payload) GlobalFree(payload);
    payload = GlobalAlloc(GMEM_MOVEABLE, size);
    auto p = GlobalLock(payload);
    memcpy(p, data, size);
    GlobalUnlock(payload);
    available = format;
    ++sequence;
    capture();
}
int main()
{
    pngFormat = 0xC001;
    const wchar_t text[] = L"Unicode text \u03a9";
    copyPayload(CF_UNICODETEXT, text, sizeof(text));
    assert(history.size() == 1 && history.front()->label == text);
    visible = history;
    const BYTE png[] = {137, 80, 78, 71, 13, 10, 26, 10};
    copyPayload(pngFormat, png, sizeof(png));
    assert(history.size() == 2 && history.front()->format == pngFormat);
    assert(history.front()->data == std::vector<BYTE>(png, png + sizeof(png)));
    assert(visible.front()->label == text);
    assert(history.front()->label.find(L"KiB") != std::wstring::npos);
    assert(!history.front()->source.empty());
    assert(history.front()->width == 0); // Truncated PNG header is safe.
    copyPayload(pngFormat, png, sizeof(png));
    assert(history.size() == 2);
    Entry dimensions;
    dimensions.format = pngFormat;
    dimensions.data = {137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,7,128,0,0,4,56};
    imageDimensions(dimensions);
    assert(dimensions.width == 1920 && dimensions.height == 1080);
    BITMAPINFOHEADER bitmap{};
    bitmap.biSize = sizeof(bitmap);
    bitmap.biWidth = 640;
    bitmap.biHeight = -480;
    dimensions = Entry{};
    dimensions.format = CF_DIBV5;
    auto bytes = reinterpret_cast<const BYTE *>(&bitmap);
    dimensions.data.assign(bytes, bytes + sizeof(bitmap));
    imageDimensions(dimensions);
    assert(dimensions.width == 640 && dimensions.height == 480);
    bitmap.biHeight = LONG_MIN;
    memcpy(dimensions.data.data(), &bitmap, sizeof(bitmap));
    dimensions.width = dimensions.height = 0;
    imageDimensions(dimensions);
    assert(dimensions.width == 0 && dimensions.height == 0);
    ownSequence = sequence;
    capture();
    assert(history.size() == 2);
    locked = true;
    proc(nullptr, WM_TIMER, 2, 0);
    assert(retry);
    proc(nullptr, WM_TIMER, 2, 0);
    assert(retry);
    locked = false;
    capture();
    assert(!retry);
    std::vector<BYTE> large(ImageLimit, 0);
    for (int i = 0; i < 4; ++i)
    {
        large[0] = static_cast<BYTE>(i);
        copyPayload(CF_DIB, large.data(), large.size());
    }
    assert(history.size() == 2);
    assert(visible.front()->label == text);
    large.push_back(0);
    auto front = history.front();
    copyPayload(CF_DIBV5, large.data(), large.size());
    assert(history.front() == front);
    GlobalFree(payload);
    std::cout << "Capture, image round-trip payload, deduplication, snapshots, retries, and limits passed\n";
}
