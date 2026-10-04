#include "common.h"

vector<DesktopInfo> g_desktops;
map<HWND, int>      g_winDesk;
int                 g_current = 0;
bool                g_switching = false;
HINSTANCE           g_hInst   = NULL;
HWND                g_hMsgWnd = NULL;

static bool IsOurWindow(HWND h)
{
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    return wcscmp(cls, L"MDesktop_Msg") == 0 || wcscmp(cls, L"MDesktop_TaskView") == 0;
}

bool ManageableWindow(HWND h)
{
    if (!IsWindow(h) || !IsWindowVisible(h)) return false;
    if (GetAncestor(h, GA_ROOT) != h) return false;
    if (IsOurWindow(h)) return false;

    LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);

    wchar_t cls[64];
    GetClassNameW(h, cls, 64);

    static const wchar_t* kSkip[] = {
        L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd",
        L"DV2ControlHost", L"BaseBar", L"tooltips_class32",
        L"NotifyIconOverflowWindow", L"TaskListOverlayWnd", L"TaskListThumbnailWnd",
        L"Windows.UI.Core.CoreWindow", L"ApplicationFrameWindow", NULL };
    for (int i = 0; kSkip[i]; i++)
        if (wcscmp(cls, kSkip[i]) == 0) return false;

    if (GetWindow(h, GW_OWNER) != NULL) return false;
    if (ex & WS_EX_TOOLWINDOW) return false;
    if (ex & WS_EX_TRANSPARENT) return false;
    return true;
}

wstring ConfigDir()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (slash) *(slash + 1) = 0;
    return wstring(path);
}

wstring DesktopName(int idx)
{
    if (idx >= 0 && idx < (int)g_desktops.size() && !g_desktops[idx].name.empty())
        return g_desktops[idx].name;
    wchar_t buf[32];
    swprintf(buf, 32, L"桌面 %d", idx + 1);
    return buf;
}

void PruneWindows()
{
    for (auto it = g_winDesk.begin(); it != g_winDesk.end(); )
    {
        if (!IsWindow(it->first)) { DiscardSnapshot(it->first); it = g_winDesk.erase(it); }
        else ++it;
    }
}

static bool AnimEnabled()
{
    wchar_t buf[16];
    GetPrivateProfileStringW(L"Settings", L"SlideAnimation", L"1", buf, 16, IniPath().c_str());
    return _wtoi(buf) != 0;
}

bool SwitchDesktopEx(int idx, bool keepTaskView, bool animate)
{
    static bool busy = false;
    if (busy) return false;
    if (idx < 0 || idx >= (int)g_desktops.size() || idx == g_current) return false;
    busy = true;
    g_switching = true;
    int old = g_current;

    PruneWindows();
    if (keepTaskView) animate = false;
    if (!keepTaskView && TaskViewVisible()) TaskViewHide();
    HBITMAP capOld = animate ? CaptureScreen() : NULL;

    if (animate) SlideFreeze(capOld);

    for (auto& kv : g_winDesk)
    {
        if (kv.second != old) continue;
        HWND h = kv.first;
        if (!IsWindowVisible(h)) continue;
        if (!keepTaskView) UpdateSnapshot(h);
        ShowWindow(h, SW_HIDE);
    }

    for (auto& kv : g_winDesk)
    {
        if (kv.second != idx) continue;
        HWND h = kv.first;
        if (IsWindow(h) && !IsWindowVisible(h)) ShowWindow(h, SW_SHOW);
    }

    g_current = idx;

    for (auto& kv : g_winDesk)
        if (kv.second != idx && IsWindow(kv.first) && IsWindowVisible(kv.first))
            ShowWindow(kv.first, SW_HIDE);

    WriteRecoveryFile();
    SaveState();
    g_switching = false;

    TaskViewRefresh();

    if (animate) SlideRun(idx > old);

    TaskViewRestoreFocus();
    busy = false;
    return true;
}

bool SwitchDesktop(int idx)
{
    return SwitchDesktopEx(idx, false, AnimEnabled());
}

int NewDesktop()
{
    g_desktops.push_back(DesktopInfo());
    SaveState();
    TaskViewRefresh();
    return (int)g_desktops.size() - 1;
}

bool CloseDesktop(int idx)
{
    int n = (int)g_desktops.size();
    if (n <= 1 || idx < 0 || idx >= n) return false;

    int dest;
    if (idx == g_current)
        dest = (idx + 1 < n) ? idx + 1 : idx - 1;
    else
        dest = (idx > 0) ? idx - 1 : idx + 1;

    vector<HWND> moved;
    for (auto& kv : g_winDesk)
        if (kv.second == idx) moved.push_back(kv.first);

    for (auto& kv : g_winDesk)
        if (kv.second == idx) kv.second = dest;

    for (auto& kv : g_winDesk)
        if (kv.second > idx) kv.second--;

    g_desktops.erase(g_desktops.begin() + idx);

    bool wasCurrent = (idx == g_current);
    if (g_current == idx)  g_current = dest > idx ? dest - 1 : dest;
    else if (g_current > idx) g_current--;

    if (wasCurrent)
    {

        for (auto& kv : g_winDesk)
            if (kv.second == g_current && IsWindow(kv.first) && !IsWindowVisible(kv.first))
                ShowWindow(kv.first, SW_SHOW);
    }
    else if (dest == g_current)
    {

        for (HWND h : moved)
            if (IsWindow(h) && !IsWindowVisible(h))
                ShowWindow(h, SW_SHOW);
    }

    WriteRecoveryFile();
    SaveState();
    TaskViewRefresh();
    return true;
}

void MoveWindowToDesktop(HWND h, int idx)
{
    if (!IsWindow(h) || idx < 0 || idx >= (int)g_desktops.size()) return;
    g_winDesk[h] = idx;
    SaveState();
}

void SendWindowToDesktop(HWND h, int idx)
{
    if (!IsWindow(h) || idx < 0 || idx >= (int)g_desktops.size()) return;
    if (g_winDesk.find(h) == g_winDesk.end()) return;
    if (idx == g_current)
    {

        ShowWindow(h, SW_SHOW);
    }
    else
    {
        UpdateSnapshot(h);
        ShowWindow(h, SW_HIDE);
    }
    g_winDesk[h] = idx;
    WriteRecoveryFile();
    SaveState();

}

void AssignInitialWindows()
{
    g_winDesk.clear();
    HWND h = GetTopWindow(NULL);
    while (h)
    {
        if (ManageableWindow(h)) g_winDesk[h] = 0;
        h = GetNextWindow(h, GW_HWNDNEXT);
    }
}

void MergeAllWindowsToCurrent()
{
    for (auto& kv : g_winDesk)
    {
        if (!IsWindow(kv.first)) continue;
        kv.second = g_current;
        if (!IsWindowVisible(kv.first)) ShowWindow(kv.first, SW_SHOW);
    }
    SaveState();
    TaskViewRefresh();
}

void RestoreAllWindows()
{
    for (auto& kv : g_winDesk)
        if (IsWindow(kv.first) && !IsWindowVisible(kv.first))
            ShowWindow(kv.first, SW_SHOW);
    ClearRecoveryFile();
}

wstring IniPath()  { return ConfigDir() + L"MDesktop.ini"; }
static wstring RecoPath() { return ConfigDir() + L"MDesktop.recover"; }

void SaveState()
{
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", (int)g_desktops.size());
    WritePrivateProfileStringW(L"Desktops", L"Count", buf, IniPath().c_str());
    swprintf(buf, 16, L"%d", g_current);
    WritePrivateProfileStringW(L"Desktops", L"Current", buf, IniPath().c_str());
    for (int i = 0; i < (int)g_desktops.size(); i++)
    {
        wchar_t key[16];
        swprintf(key, 16, L"Name%d", i);
        WritePrivateProfileStringW(L"Desktops", key, g_desktops[i].name.c_str(), IniPath().c_str());
    }
}

void LoadState()
{
    g_desktops.clear();
    wchar_t buf[256];
    int count = GetPrivateProfileIntW(L"Desktops", L"Count", 0, IniPath().c_str());
    if (count <= 0 || count > 10000) count = 1;
    for (int i = 0; i < count; i++)
    {
        DesktopInfo d;
        wchar_t key[16];
        swprintf(key, 16, L"Name%d", i);
        if (GetPrivateProfileStringW(L"Desktops", key, L"", buf, 256, IniPath().c_str()) > 0)
            d.name = buf;
        g_desktops.push_back(d);
    }
    g_current = 0;

    GetPrivateProfileStringW(L"Settings", L"SlideAnimation", L"1", buf, 16, IniPath().c_str());
    WritePrivateProfileStringW(L"Settings", L"SlideAnimation", buf, IniPath().c_str());
}

void WriteRecoveryFile()
{
    FILE* f = _wfopen(RecoPath().c_str(), L"w, ccs=UTF-8");
    if (!f) return;
    for (auto& kv : g_winDesk)
    {
        HWND h = kv.first;
        if (kv.second != g_current || !IsWindow(h) || IsWindowVisible(h)) continue;
        wchar_t title[256] = L"";
        GetWindowTextW(h, title, 256);

        for (wchar_t* p = title; *p; p++) if (*p == L'|') *p = L' ';
        fwprintf(f, L"%p|%ls\n", (void*)h, title);
    }
    fclose(f);
}

void ClearRecoveryFile()
{
    DeleteFileW(RecoPath().c_str());
}

void RecoverHiddenWindows()
{
    FILE* f = _wfopen(RecoPath().c_str(), L"r, ccs=UTF-8");
    if (!f) return;
    wchar_t line[600];
    int restored = 0;
    while (fgetws(line, 600, f))
    {

        wchar_t* sep = wcschr(line, L'|');
        if (!sep) continue;
        *sep = 0;
        HWND h = (HWND)(uintptr_t)wcstoull(line, NULL, 16);
        wstring title = sep + 1;
        if (!title.empty() && title[title.size() - 1] == L'\n') title.erase(title.size() - 1);
        if (!IsWindow(h) || IsWindowVisible(h)) continue;
        wchar_t cur[256] = L"";
        GetWindowTextW(h, cur, 256);
        if (cur != title) continue;
        ShowWindow(h, SW_SHOW);
        restored++;
    }
    fclose(f);
    ClearRecoveryFile();
    (void)restored;
}
