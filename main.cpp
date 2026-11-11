#include <windows.h>
#include <shellapi.h>
#include <deque>
#include <string>
#include <algorithm>
#include <memory>
#include <vector>

constexpr UINT Tray = WM_APP + 1;
HWND window, searchBox, listBox, previous;
struct Entry
{
    UINT format = CF_UNICODETEXT;
    std::vector<BYTE> data;
    std::wstring label;
    std::wstring source;
    unsigned width = 0, height = 0;
};
using Item = std::shared_ptr<const Entry>;
std::deque<Item> history;
// Shared immutable snapshots remain valid when live history changes.
std::deque<Item> visible;
DWORD ownSequence = 0;
constexpr size_t HistoryLimit = 32 * 1024 * 1024;
constexpr size_t ImageLimit = 16 * 1024 * 1024;
UINT pngFormat;

std::wstring clipboardSource()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetClipboardOwner(), &pid);
    HANDLE process = pid ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
    if (!process)
        return L"Unknown app";
    wchar_t path[32768]{};
    DWORD length = ARRAYSIZE(path);
    const BOOL found = QueryFullProcessImageNameW(process, 0, path, &length);
    CloseHandle(process);
    if (!found)
        return L"Unknown app";
    std::wstring name(path, length);
    auto slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        name.erase(0, slash + 1);
    if (name.size() > 4 && _wcsicmp(name.c_str() + name.size() - 4, L".exe") == 0)
        name.resize(name.size() - 4);
    return name.empty() ? L"Unknown app" : name;
}

void imageDimensions(Entry &entry)
{
    const auto &data = entry.data;
    if (entry.format == pngFormat)
    {
        const BYTE signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
        if (data.size() < 24 || memcmp(data.data(), signature, 8) != 0 ||
            memcmp(data.data() + 8, "\0\0\0\rIHDR", 8) != 0)
            return;
        auto bigEndian = [&](size_t offset) {
            return (unsigned(data[offset]) << 24) | (unsigned(data[offset + 1]) << 16) |
                   (unsigned(data[offset + 2]) << 8) | unsigned(data[offset + 3]);
        };
        const unsigned width = bigEndian(16), height = bigEndian(20);
        if (width && height && width <= INT_MAX && height <= INT_MAX)
        {
            entry.width = width;
            entry.height = height;
        }
        return;
    }
    if (data.size() < sizeof(DWORD))
        return;
    DWORD headerSize;
    memcpy(&headerSize, data.data(), sizeof(headerSize));
    if (headerSize > data.size())
        return;
    if (headerSize == sizeof(BITMAPCOREHEADER) && data.size() >= sizeof(BITMAPCOREHEADER))
    {
        BITMAPCOREHEADER header{};
        memcpy(&header, data.data(), sizeof(header));
        if (header.bcWidth && header.bcHeight)
        {
            entry.width = header.bcWidth;
            entry.height = header.bcHeight;
        }
    }
    else if (headerSize >= sizeof(BITMAPINFOHEADER) && data.size() >= sizeof(BITMAPINFOHEADER))
    {
        BITMAPINFOHEADER header{};
        memcpy(&header, data.data(), sizeof(header));
        if (header.biWidth > 0 && header.biHeight && header.biHeight != LONG_MIN)
        {
            entry.width = static_cast<unsigned>(header.biWidth);
            entry.height =
                static_cast<unsigned>(header.biHeight < 0 ? -header.biHeight : header.biHeight);
        }
    }
}

NOTIFYICONDATAW tray{};
bool pendingPaste = false;
void refresh()
{
    wchar_t query[1024]{};
    GetWindowTextW(searchBox, query, 1024);
    SendMessageW(listBox, LB_RESETCONTENT, 0, 0);
    visible.clear();
    for (size_t i = 0; i < history.size(); ++i)
    {
        if (*query &&
            FindStringOrdinal(FIND_FROMSTART, history[i]->label.c_str(), -1, query, -1, TRUE) < 0)
            continue;
        auto preview = history[i]->label.substr(0, 160);
        for (auto &c : preview)
            if (c == L'\r' || c == L'\n' || c == L'\t')
                c = L' ';
        SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(preview.c_str()));
        visible.push_back(history[i]);
    }
    if (!visible.empty())
        SendMessageW(listBox, LB_SETCURSEL, 0, 0);
}
void capture()
{
    if (!OpenClipboard(window))
    {
        SetTimer(window, 2, 40, nullptr);
        return;
    }
    KillTimer(window, 2);
    const DWORD sequence = GetClipboardSequenceNumber();
    if (ownSequence && sequence == ownSequence)
    {
        CloseClipboard();
        return;
    }
    ownSequence = 0;
    // Prefer an image when a copy operation also advertises descriptive text.
    UINT format = IsClipboardFormatAvailable(CF_DIBV5)                 ? CF_DIBV5
                  : IsClipboardFormatAvailable(CF_DIB)                 ? CF_DIB
                  : pngFormat && IsClipboardFormatAvailable(pngFormat) ? pngFormat
                                                                       : CF_UNICODETEXT;
    HANDLE data = GetClipboardData(format);
    auto entry = std::make_shared<Entry>();
    entry->format = format;
    if (data)
    {
        size_t bytes = GlobalSize(data);
        const size_t limit = format == CF_UNICODETEXT ? 256 * 1024 : ImageLimit;
        if (bytes && bytes <= limit)
        {
            auto p = static_cast<const BYTE *>(GlobalLock(data));
            if (p)
            {
                if (format == CF_UNICODETEXT)
                {
                    auto text = reinterpret_cast<const wchar_t *>(p);
                    size_t n = 0;
                    while (n < bytes / sizeof(wchar_t) && text[n])
                        ++n;
                    if (n && n < bytes / sizeof(wchar_t))
                    {
                        entry->label.assign(text, n);
                        entry->data.assign(p, p + (n + 1) * sizeof(wchar_t));
                    }
                }
                else
                {
                    // Preserve the original clipboard payload, including palettes and alpha.
                    entry->data.assign(p, p + bytes);
                    entry->source = clipboardSource();
                    imageDimensions(*entry);
                    const auto dimensions = entry->width && entry->height
                                                ? std::to_wstring(entry->width) + L" x " +
                                                      std::to_wstring(entry->height) + L" px"
                                                : L"Unknown dimensions";
                    entry->label = L"[Image] " + entry->source + L" | " + dimensions + L" | " +
                                   std::to_wstring((bytes + 1023) / 1024) + L" KiB";
                }
                GlobalUnlock(data);
            }
        }
    }
    CloseClipboard();
    if (entry->data.empty())
        return;
    history.erase(std::remove_if(history.begin(), history.end(),
                                 [&](const Item &item) {
                                     return item->format == entry->format &&
                                            item->data == entry->data;
                                 }),
                  history.end());
    history.push_front(entry);
    size_t total = 0;
    for (const auto &item : history)
        total += item->data.size();
    while (history.size() > 50 || total > HistoryLimit)
    {
        total -= history.back()->data.size();
        history.pop_back();
    }
}

void show()
{
    previous = GetForegroundWindow();
    POINT p;
    GetCursorPos(&p);
    RECT r;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi);
    r = mi.rcWork;
    int w = 400, h = 300;
    SetWindowPos(window, HWND_TOPMOST, std::clamp(p.x, r.left, std::max(r.left, r.right - w)),
                 std::clamp(p.y, r.top, std::max(r.top, r.bottom - h)), w, h, SWP_SHOWWINDOW);
    SetWindowTextW(searchBox, L"");
    refresh();
    SetForegroundWindow(window);
    SetFocus(searchBox);
}
void paste()
{
    LRESULT n = SendMessageW(listBox, LB_GETCURSEL, 0, 0);
    if (n < 0 || size_t(n) >= visible.size())
        return;
    auto item = visible[n];
    if (!OpenClipboard(window))
        return;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, item->data.size());
    if (!mem)
    {
        CloseClipboard();
        return;
    }
    void *p = GlobalLock(mem);
    if (!p)
    {
        GlobalFree(mem);
        CloseClipboard();
        return;
    }
    memcpy(p, item->data.data(), item->data.size());
    GlobalUnlock(mem);
    if (!EmptyClipboard() || !SetClipboardData(item->format, mem))
    {
        GlobalFree(mem);
        CloseClipboard();
        return;
    }
    ownSequence = GetClipboardSequenceNumber();
    CloseClipboard();
    ShowWindow(window, SW_HIDE);
    if (IsWindow(previous) && SetForegroundWindow(previous))
    {
        pendingPaste = true;
        SetTimer(window, 3, 30, nullptr);
    }
}
LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_CREATE:
        searchBox =
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                            8, 8, 368, 26, h, (HMENU)1, nullptr, nullptr);
        listBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY, 8, 42, 368, 210,
                                  h, (HMENU)2, nullptr, nullptr);
        break;
    case WM_SIZE:
        MoveWindow(searchBox, 8, 8, LOWORD(l) - 16, 26, TRUE);
        MoveWindow(listBox, 8, 42, LOWORD(l) - 16, HIWORD(l) - 50, TRUE);
        break;
    case WM_COMMAND:
        if (LOWORD(w) == 1 && HIWORD(w) == EN_CHANGE)
            refresh();
        if (LOWORD(w) == 2 && HIWORD(w) == LBN_DBLCLK)
            paste();
        break;
    case WM_HOTKEY:
        show();
        break;
    case WM_CLIPBOARDUPDATE:
        capture();
        break;
    case WM_TIMER:
        if (w == 2)
        {
            KillTimer(h, 2);
            capture();
        }
        if (w == 3 && pendingPaste && !(GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            !(GetAsyncKeyState(VK_SHIFT) & 0x8000) && !(GetAsyncKeyState('V') & 0x8000))
        {
            KillTimer(h, 3);
            pendingPaste = false;
            if (GetForegroundWindow() == previous)
            {
                INPUT in[4]{};
                for (auto &i : in)
                    i.type = INPUT_KEYBOARD;
                in[0].ki.wVk = VK_CONTROL;
                in[1].ki.wVk = 'V';
                in[2].ki.wVk = 'V';
                in[2].ki.dwFlags = KEYEVENTF_KEYUP;
                in[3].ki.wVk = VK_CONTROL;
                in[3].ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(4, in, sizeof(INPUT));
            }
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE)
            ShowWindow(h, SW_HIDE);
        break;
    case Tray:
        if (l == WM_LBUTTONUP)
            show();
        if (l == WM_RBUTTONUP)
        {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 10, L"Open history");
            AppendMenuW(menu, MF_STRING, 11, L"Clear history");
            AppendMenuW(menu, MF_STRING, 12, L"Exit");
            POINT p;
            GetCursorPos(&p);
            SetForegroundWindow(h);
            int cmd =
                TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr);
            DestroyMenu(menu);
            if (cmd == 10)
                show();
            if (cmd == 11)
            {
                history.clear();
                ownSequence = 0;
                refresh();
            }
            if (cmd == 12)
                DestroyWindow(h);
        }
        break;
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &tray);
        RemoveClipboardFormatListener(h);
        UnregisterHotKey(h, 1);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(h, m, w, l);
    }
    return 0;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\MultiPaste.Singleton");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;
    pngFormat = RegisterClipboardFormatW(L"PNG");
    SetProcessDPIAware();
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.lpszClassName = L"MultiPaste";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    window = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"MultiPaste",
                             WS_OVERLAPPED | WS_CAPTION | WS_THICKFRAME, 0, 0, 400, 300, nullptr,
                             nullptr, instance, nullptr);
    tray.cbSize = sizeof(tray);
    tray.hWnd = window;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = Tray;
    tray.hIcon = wc.hIcon;
    lstrcpyW(tray.szTip, L"MultiPaste - Ctrl+Shift+V");
    Shell_NotifyIconW(NIM_ADD, &tray);
    AddClipboardFormatListener(window);
    if (!RegisterHotKey(window, 1, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'V'))
        MessageBoxW(nullptr, L"Ctrl+Shift+V is already in use. Use the tray icon to open history.",
                    L"MultiPaste", MB_OK);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        if (IsWindowVisible(window) && msg.message == WM_KEYDOWN)
        {
            if (msg.wParam == VK_ESCAPE)
            {
                ShowWindow(window, SW_HIDE);
                continue;
            }
            if (msg.wParam == VK_RETURN)
            {
                paste();
                continue;
            }
            if (msg.hwnd == searchBox && (msg.wParam == VK_DOWN || msg.wParam == VK_UP))
            {
                int n = (int)SendMessageW(listBox, LB_GETCURSEL, 0, 0);
                n += msg.wParam == VK_DOWN ? 1 : -1;
                if (!visible.empty())
                    SendMessageW(listBox, LB_SETCURSEL, std::clamp(n, 0, (int)visible.size() - 1),
                                 0);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CloseHandle(mutex);
    return 0;
}
