#include <windows.h>
#include <shellapi.h>
#include <deque>
#include <string>
#include <algorithm>

constexpr UINT Tray = WM_APP + 1;
HWND window, searchBox, listBox, previous;
std::deque<std::wstring> history;
std::deque<size_t> visible;
std::wstring own;
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
            FindStringOrdinal(FIND_FROMSTART, history[i].c_str(), -1, query, -1, TRUE) < 0)
            continue;
        auto preview = history[i].substr(0, 160);
        for (auto &c : preview)
            if (c == L'\r' || c == L'\n' || c == L'\t')
                c = L' ';
        SendMessageW(listBox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(preview.c_str()));
        visible.push_back(i);
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
    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data)
    {
        size_t bytes = GlobalSize(data);
        if (bytes && bytes <= 256 * 1024)
        {
            auto p = static_cast<const wchar_t *>(GlobalLock(data));
            if (p)
            {
                size_t n = 0;
                while (n < bytes / 2 && p[n])
                    ++n;
                if (n < bytes / 2 && n)
                {
                    std::wstring text(p, n);
                    if (text != own)
                    {
                        history.erase(std::remove(history.begin(), history.end(), text),
                                      history.end());
                        history.push_front(text);
                        size_t total = 0;
                        for (auto &s : history)
                            total += s.size() * 2;
                        while (history.size() > 50 || total > 4 * 1024 * 1024)
                        {
                            total -= history.back().size() * 2;
                            history.pop_back();
                        }
                    }
                    own.clear();
                }
                GlobalUnlock(data);
            }
        }
    }
    CloseClipboard();
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
    auto text = history[visible[n]];
    if (!OpenClipboard(window))
        return;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * 2);
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
    memcpy(p, text.c_str(), (text.size() + 1) * 2);
    GlobalUnlock(mem);
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem))
        GlobalFree(mem);
    else
        own = text;
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
            KillTimer(h, 2);
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
                own.clear();
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
    SetProcessDPIAware();
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
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
    tray.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
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
