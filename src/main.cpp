#include "common.h"
#include <windowsx.h>

enum {
    HK_TASKVIEW = 1,
    HK_NEXT     = 2,
    HK_PREV     = 3,
    HK_NEW      = 4,
    HK_CLOSE    = 5,
    HK_MVNEXT   = 6,
    HK_MVPREV   = 7,
    HK_D1       = 10,
};
#define MOD_NOREPEAT 0x4000

struct HotkeyDef {
    UINT id;
    const wchar_t* iniKey;
    const wchar_t* defSpec;
    UINT mods, vk;
    wstring spec;
};
static HotkeyDef g_hotkeys[] = {
    { HK_TASKVIEW, L"TaskView",     L"Ctrl+Shift+Space" },
    { HK_NEXT,     L"DesktopNext",  L"Ctrl+Alt+PgDn" },
    { HK_PREV,     L"DesktopPrev",  L"Ctrl+Alt+PgUp" },
    { HK_NEW,      L"DesktopNew",   L"Ctrl+Alt+N" },
    { HK_CLOSE,    L"DesktopClose", L"Ctrl+Alt+X" },
    { HK_MVNEXT,   L"WindowNext",   L"Ctrl+Alt+Shift+PgDn" },
    { HK_MVPREV,   L"WindowPrev",   L"Ctrl+Alt+Shift+PgUp" },
    { HK_D1,       L"Desktop1",     L"Ctrl+Alt+1" }, { HK_D1+1, L"Desktop2", L"Ctrl+Alt+2" },
    { HK_D1+2,     L"Desktop3",     L"Ctrl+Alt+3" }, { HK_D1+3, L"Desktop4", L"Ctrl+Alt+4" },
    { HK_D1+4,     L"Desktop5",     L"Ctrl+Alt+5" }, { HK_D1+5, L"Desktop6", L"Ctrl+Alt+6" },
    { HK_D1+6,     L"Desktop7",     L"Ctrl+Alt+7" }, { HK_D1+7, L"Desktop8", L"Ctrl+Alt+8" },
    { HK_D1+8,     L"Desktop9",     L"Ctrl+Alt+9" },
};

static UINT VkFromName(wstring s)
{
    static const struct { const wchar_t* name; UINT vk; } kNamed[] = {
        { L"SPACE", VK_SPACE }, { L"TAB", VK_TAB }, { L"ENTER", VK_RETURN }, { L"ESC", VK_ESCAPE },
        { L"PGUP", VK_PRIOR }, { L"PAGEUP", VK_PRIOR }, { L"PGDN", VK_NEXT }, { L"PAGEDOWN", VK_NEXT },
        { L"HOME", VK_HOME }, { L"END", VK_END }, { L"INS", VK_INSERT }, { L"INSERT", VK_INSERT },
        { L"DEL", VK_DELETE }, { L"DELETE", VK_DELETE }, { L"BACKSPACE", VK_BACK },
        { L"UP", VK_UP }, { L"DOWN", VK_DOWN }, { L"LEFT", VK_LEFT }, { L"RIGHT", VK_RIGHT },
        { L"PAUSE", VK_PAUSE }, { NULL, 0 } };
    for (size_t i = 0; i < s.size(); i++) s[i] = towupper(s[i]);
    if (s.size() == 1)
    {
        wchar_t c = s[0];
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9')) return c;
        return 0;
    }
    if (s.size() <= 3 && s[0] == L'F' && s[1] >= L'0' && s[1] <= L'9')
    {
        int n = _wtoi(s.c_str() + 1);
        if (n >= 1 && n <= 12) return VK_F1 + n - 1;
        return 0;
    }
    for (int i = 0; kNamed[i].name; i++)
        if (s == kNamed[i].name) return kNamed[i].vk;
    return 0;
}

static bool ParseHotkey(const wstring& text, UINT& mods, UINT& vk, wstring& norm)
{
    mods = 0; vk = 0;
    wstring key;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t plus = text.find(L'+', start);
        wstring tok = (plus == wstring::npos) ? text.substr(start) : text.substr(start, plus - start);
        wstring t;
        for (size_t i = 0; i < tok.size(); i++) if (!iswspace(tok[i])) t += tok[i];
        if (!t.empty())
        {
            wstring low = t;
            for (size_t i = 0; i < low.size(); i++) low[i] = towlower(low[i]);
            if      (low == L"ctrl" || low == L"control") mods |= MOD_CONTROL;
            else if (low == L"alt")                       mods |= MOD_ALT;
            else if (low == L"shift")                     mods |= MOD_SHIFT;
            else if (low == L"win" || low == L"windows")  mods |= MOD_WIN;
            else if (key.empty()) { key = t; for (size_t i = 0; i < key.size(); i++) key[i] = towupper(key[i]); }
            else return false;
        }
        if (plus == wstring::npos) break;
        start = plus + 1;
    }
    vk = VkFromName(key);
    if (!vk) return false;
    norm.clear();
    if (mods & MOD_CONTROL) norm += L"Ctrl+";
    if (mods & MOD_ALT)     norm += L"Alt+";
    if (mods & MOD_SHIFT)   norm += L"Shift+";
    if (mods & MOD_WIN)     norm += L"Win+";
    norm += key;
    return true;
}

static void LoadAndRegisterHotkeys(HWND w)
{
    wchar_t buf[128];
    for (auto& hk : g_hotkeys)
    {
        hk.mods = 0; hk.vk = 0; hk.spec.clear();
        GetPrivateProfileStringW(L"Hotkeys", hk.iniKey, hk.defSpec, buf, 128, IniPath().c_str());

        wchar_t low[128];
        wcsncpy(low, buf, 127); low[127] = 0;
        for (wchar_t* q = low; *q; q++) *q = towlower(*q);
        if (wcscmp(low, L"ctrl+alt+space") == 0)
        {
            hk.defSpec = L"Ctrl+Shift+Space";
            wcscpy(buf, hk.defSpec);
        }
        if (!ParseHotkey(buf, hk.mods, hk.vk, hk.spec))
            ParseHotkey(hk.defSpec, hk.mods, hk.vk, hk.spec);
        WritePrivateProfileStringW(L"Hotkeys", hk.iniKey, hk.spec.c_str(), IniPath().c_str());
        if (hk.vk)
            RegisterHotKey(w, hk.id, hk.mods | MOD_NOREPEAT, hk.vk);
    }
}

static const wchar_t* HotkeySpec(UINT id)
{
    for (auto& hk : g_hotkeys)
        if (hk.id == id && !hk.spec.empty()) return hk.spec.c_str();
    return L"";
}

static void UnregisterHotkeys(HWND w)
{
    for (auto& hk : g_hotkeys) UnregisterHotKey(w, hk.id);
}

static const UINT TRAY_MSG = WM_APP + 1;
static const UINT APPMSG_SHOWTV = WM_APP + 3;
static const UINT APPMSG_NEXT   = WM_APP + 4;
static const UINT APPMSG_PREV   = WM_APP + 5;
static NOTIFYICONDATAW g_nid = {};
static HWINEVENTHOOK g_hookShow = NULL;
static HWINEVENTHOOK g_hookFg = NULL;
static HWINEVENTHOOK g_hookDestroy = NULL;

enum { CMD_TASKVIEW = 200, CMD_NEW = 201, CMD_CLOSE = 202, CMD_RESTORE = 203, CMD_EXIT = 204, CMD_DESK0 = 300 };

static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND h, LONG idObject, LONG idChild, DWORD, DWORD)
{
    if (idObject != OBJID_WINDOW || idChild != 0 || !h) return;
    if (event == EVENT_OBJECT_SHOW)
    {
        if (ManageableWindow(h) && g_winDesk.find(h) == g_winDesk.end())
        {
            g_winDesk[h] = g_current;
            PruneWindows();
        }
    }
    else if (event == EVENT_OBJECT_DESTROY)
    {
        if (g_winDesk.find(h) != g_winDesk.end())
            PostMessageW(g_hMsgWnd, WM_APP + 10, 0, 0);
    }
    else if (event == EVENT_SYSTEM_FOREGROUND)
    {

        if (IsWindow(h) && !IsWindowVisible(h))
        {
            auto it = g_winDesk.find(h);
            if (it != g_winDesk.end() && it->second != g_current)
                SwitchDesktop(it->second);
        }
    }
}

static LONG WINAPI CrashFilter(LPEXCEPTION_POINTERS)
{
    RestoreAllWindows();
    return EXCEPTION_CONTINUE_SEARCH;
}

static void AddDeskItem(HMENU menu, int idx)
{
    wchar_t text[320];
    int cnt = 0;
    for (auto& kv : g_winDesk) if (kv.second == idx) cnt++;
    swprintf(text, 320, L"%ls(%d 个窗口)", DesktopName(idx).c_str(), cnt);
    AppendMenuW(menu, MF_STRING | (idx == g_current ? MF_CHECKED : 0), CMD_DESK0 + idx, text);
}

static void AddHotkeyItem(HMENU menu, UINT cmd, UINT hkId, const wchar_t* label)
{
    wchar_t text[160];
    swprintf(text, 160, L"%ls\t%ls", label, HotkeySpec(hkId));
    AppendMenuW(menu, MF_STRING, cmd, text);
}

void ShowTrayMenu()
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    HMENU desks = CreatePopupMenu();
    for (int i = 0; i < (int)g_desktops.size(); i++) AddDeskItem(desks, i);
    AddHotkeyItem(menu, CMD_TASKVIEW, HK_TASKVIEW, L"任务视图");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)desks, L"切换桌面");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AddHotkeyItem(menu, CMD_NEW, HK_NEW, L"新建桌面");
    AddHotkeyItem(menu, CMD_CLOSE, HK_CLOSE, L"关闭当前桌面");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, CMD_RESTORE, L"合并所有窗口到当前桌面");
    AppendMenuW(menu, MF_STRING, CMD_EXIT, L"退出");
    SetForegroundWindow(g_hMsgWnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hMsgWnd, NULL);
    DestroyMenu(menu);
}

static void DoCleanup()
{
    RestoreAllWindows();
    SaveState();
    UnregisterHotkeys(g_hMsgWnd);
    if (g_hookShow) UnhookWinEvent(g_hookShow);
    if (g_hookFg) UnhookWinEvent(g_hookFg);
    if (g_hookDestroy) UnhookWinEvent(g_hookDestroy);
    if (g_nid.uID) { Shell_NotifyIconW(NIM_DELETE, &g_nid); g_nid.uID = 0; }
}

static LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_HOTKEY:
        switch (wp)
        {
        case HK_TASKVIEW: TaskViewToggle(); break;
        case HK_NEXT: if (g_current + 1 < (int)g_desktops.size()) SwitchDesktop(g_current + 1); break;
        case HK_PREV: if (g_current > 0) SwitchDesktop(g_current - 1); break;
        case HK_NEW:  SwitchDesktop(NewDesktop()); break;
        case HK_CLOSE: CloseDesktop(g_current); break;
        case HK_MVNEXT:
            if (g_current + 1 >= (int)g_desktops.size()) NewDesktop();
            SendWindowToDesktop(GetForegroundWindow(), g_current + 1);
            break;
        case HK_MVPREV:
            if (g_current > 0) SendWindowToDesktop(GetForegroundWindow(), g_current - 1);
            break;
        default:
            if (wp >= HK_D1 && wp < HK_D1 + 9)
            {
                int idx = (int)wp - HK_D1;
                if (idx < (int)g_desktops.size()) SwitchDesktop(idx);
            }
        }
        return 0;

    case TRAY_MSG:
        switch (lp)
        {
        case WM_LBUTTONUP: TaskViewToggle(); break;
        case WM_RBUTTONUP: ShowTrayMenu(); break;
        }
        return 0;

    case APPMSG_SHOWTV: TaskViewShow(); return 0;
    case APPMSG_NEXT: if (g_current + 1 < (int)g_desktops.size()) SwitchDesktop(g_current + 1); return 0;
    case APPMSG_PREV: if (g_current > 0) SwitchDesktop(g_current - 1); return 0;

    case WM_COMMAND:
        switch (LOWORD(wp))
        {
        case CMD_TASKVIEW: TaskViewShow(); break;
        case CMD_NEW: SwitchDesktop(NewDesktop()); break;
        case CMD_CLOSE: CloseDesktop(g_current); break;
        case CMD_RESTORE: MergeAllWindowsToCurrent(); break;
        case CMD_EXIT: DoCleanup(); PostQuitMessage(0); break;
        default:
            if (LOWORD(wp) >= CMD_DESK0 && LOWORD(wp) < CMD_DESK0 + (int)g_desktops.size())
                SwitchDesktop(LOWORD(wp) - CMD_DESK0);
        }
        return 0;

    case WM_TIMER:
        if (wp == 9)
        {
            KillTimer(hwnd, 9);
            SnapshotInitAll();
        }
        else if (wp == 12)
        {
            KillTimer(hwnd, 12);
            PruneWindows();
            TaskViewRefresh();
        }
        return 0;

    case WM_APP + 10:
        SetTimer(hwnd, 12, 100, NULL);
        return 0;

    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION:
        if (wp) RestoreAllWindows();
        return 0;

    case WM_DESTROY: DoCleanup(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{

    HANDLE mutex = CreateMutexW(NULL, TRUE, L"MDesktop_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {

        HWND w = FindWindowW(L"MDesktop_Msg", NULL);
        if (w)
        {
            LPCWSTR cmd = GetCommandLineW();
            UINT m = APPMSG_SHOWTV;
            if (wcsstr(cmd, L"/next") || wcsstr(cmd, L"-next")) m = APPMSG_NEXT;
            else if (wcsstr(cmd, L"/prev") || wcsstr(cmd, L"-prev")) m = APPMSG_PREV;
            PostMessageW(w, m, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }

    SetUnhandledExceptionFilter(CrashFilter);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    typedef BOOL (WINAPI *FN_SetDPI)(void);
    FN_SetDPI fDpi = (FN_SetDPI)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware");
    if (fDpi) fDpi();

    LoadState();
    if (g_desktops.empty()) g_desktops.push_back(DesktopInfo());
    if (g_current < 0 || g_current >= (int)g_desktops.size()) g_current = 0;
    RecoverHiddenWindows();
    AssignInitialWindows();

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MsgWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"MDesktop_Msg";
    RegisterClassExW(&wc);
    g_hMsgWnd = CreateWindowExW(0, L"MDesktop_Msg", L"MDesktop", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInst, NULL);

    SetTimer(g_hMsgWnd, 9, 600, NULL);
    LoadAndRegisterHotkeys(g_hMsgWnd);
    g_hookShow = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, NULL, WinEventProc, 0, 0,
                                 WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_hookFg = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, WinEventProc, 0, 0,
                               WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    g_hookDestroy = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, NULL, WinEventProc, 0, 0,
                                    WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hMsgWnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = TRAY_MSG;
    g_nid.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (!g_nid.hIcon) g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    swprintf(g_nid.szTip, 128, L"MDesktop - %ls 打开任务视图", HotkeySpec(HK_TASKVIEW));
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DoCleanup();
    CoUninitialize();
    CloseHandle(mutex);
    return 0;
}
