#include "common.h"
#include <shlobj.h>
#include <ocidl.h>
#include <olectl.h>

#include <dwmapi.h>

static const wchar_t TV_CLASS[] = L"MDesktop_TaskView";

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

static const int CARD_W   = 180;
static const int CARD_H   = 110;
static const int CARD_GAP = 12;
static const int TOP_M    = 24;
static const int LABEL_H  = 24;
static const int GRID_M   = 32;
static const int TITLE_H  = 28;
static const int CELL_GAP = 16;
static const int TILE_GAP_H = 14;
static const int BOTTOM_M = 52;

static int   g_structAnim = 0;
static DWORD g_structStart = 0;
static int   g_topFrom = 0;

static int GridTop()
{
    int base;
    if ((int)g_desktops.size() <= 1) base = TOP_M + 34 + 22;
    else base = TOP_M + LABEL_H + CARD_H + 28;
    if (g_structAnim)
    {
        int el = (int)(GetTickCount() - g_structStart);
        if (el < 0) el = 0;
        if (el > 280) el = 280;
        float t = el / 280.0f;
        float e = t * t * (3 - 2 * t);
        return g_topFrom + (int)((base - g_topFrom) * e);
    }
    return base;
}

struct CardRC { RECT rc; int desk; bool isPlus; };
struct CellRC { RECT rcAll, rcTitle, rcThumb; HWND hwnd; HTHUMBNAIL thumb;
                RECT rcFrom; bool hasFrom; };

#define MY_DWM_TNP_RECTDESTINATION      0x00000001
#define MY_DWM_TNP_SOURCECLIENTAREAONLY 0x00000002
#define MY_DWM_TNP_RECTSOURCE           0x00000004
#define MY_DWM_TNP_OPACITY              0x00000008
#define MY_DWM_TNP_VISIBLE              0x00000010

static HWND        g_tv        = NULL;
static vector<CardRC> g_cards;
static vector<CellRC> g_cells;
static vector<std::pair<HWND, HTHUMBNAIL> > g_reg;
static int         g_cardScroll = 0;
static int         g_gridScroll = 0;
static int         g_hoverCard  = -1;
static bool        g_hoverClose = false;
static int         g_hoverCell  = -1;
static bool        g_hoverCellClose = false;
static bool        g_closing    = false;
static HFONT       g_fLabel = NULL, g_fCell = NULL, g_fPlus = NULL;

static int   g_dragCell  = -1;
static HWND  g_dragHwnd  = NULL;
static bool  g_dragging  = false;
static POINT g_dragPt;
static float g_dragScale  = 1.0f;
static float g_dragScaleV = 0.0f;
static float g_dragScaleT = 1.0f;
static HTHUMBNAIL g_dragThumb = NULL;
static bool g_scrollDrag = false;
static int  g_scrollDragOff = 0;

static int   g_renderDesk = 0;

static const int  OPEN_ANIM_MS   = 320;
static DWORD      g_openAnimStart = 0;
static bool       g_animClose = false;
static bool       g_closeAnim = false;
static HWND       g_openFg = NULL;

static RECT LerpRect(const RECT& a, const RECT& b, float e)
{
    RECT r;
    r.left   = a.left   + (LONG)((b.left   - a.left)   * e);
    r.top    = a.top    + (LONG)((b.top    - a.top)    * e);
    r.right  = a.right  + (LONG)((b.right  - a.right)  * e);
    r.bottom = a.bottom + (LONG)((b.bottom - a.bottom) * e);
    return r;
}

static void FinalizeHide();

bool TaskViewEnsureClass(HINSTANCE hInst);

static map<HWND, HBITMAP> g_snaps;

void DiscardSnapshot(HWND h)
{
    auto it = g_snaps.find(h);
    if (it != g_snaps.end()) { if (it->second) DeleteObject(it->second); g_snaps.erase(it); }
}

void UpdateSnapshot(HWND h)
{

    if (!IsWindow(h) || IsIconic(h) || !IsWindowVisible(h)) return;

    RECT cr = { 0, 0, 0, 0 };
    if (!GetClientRect(h, &cr)) return;
    int w = cr.right, ht = cr.bottom;
    if (w < 8 || ht < 8 || w > 4096 || ht > 4096) return;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -ht;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(NULL);
    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bmp)
    {
        HDC mem = CreateCompatibleDC(screen);
        HGDIOBJ old = SelectObject(mem, bmp);

        RECT full = { 0, 0, w, ht };
        HBRUSH b = CreateSolidBrush(RGB(16, 16, 16));
        FillRect(mem, &full, b);
        DeleteObject(b);

        PrintWindow(h, mem, PW_RENDERFULLCONTENT);
        SelectObject(mem, old);
        DeleteDC(mem);
        DiscardSnapshot(h);
        g_snaps[h] = bmp;
    }
    ReleaseDC(NULL, screen);
}

HBITMAP GetSnapshot(HWND h)
{
    auto it = g_snaps.find(h);
    return it == g_snaps.end() ? NULL : it->second;
}

static void SnapshotForCell(HWND h, bool allowRestore)
{
    if (g_switching) return;
    if (IsIconic(h))
    {
        if (GetSnapshot(h)) return;
        if (!allowRestore) return;
        ShowWindow(h, SW_SHOWNOACTIVATE);
        UpdateWindow(h);
        Sleep(150);
        UpdateSnapshot(h);
        ShowWindow(h, SW_MINIMIZE);
    }
    else
        UpdateSnapshot(h);
}

static void UnregisterThumbs()
{
    for (auto& r : g_reg)
        if (r.second) DwmUnregisterThumbnail(r.second);
    g_reg.clear();
    for (auto& c : g_cells) c.thumb = NULL;
}

static HRESULT RegThumbSafe(HWND dst, HWND src, HTHUMBNAIL* out)
{
    typedef HRESULT (WINAPI *FN)(HWND, HWND, HTHUMBNAIL*);
    static FN fn = NULL;
    if (!fn)
    {
        HMODULE d = GetModuleHandleW(L"dwmapi.dll");
        if (!d) return E_NOTIMPL;
        fn = (FN)(void*)GetProcAddress(d, "DwmRegisterThumbnail");
        if (!fn) return E_NOTIMPL;
    }
    return fn(dst, src, out);
}

static void UnregisterThumbOf(HWND target)
{
    for (size_t i = 0; i < g_reg.size(); )
    {
        if (g_reg[i].first == target)
        {
            DwmUnregisterThumbnail(g_reg[i].second);
            g_reg.erase(g_reg.begin() + i);
        }
        else ++i;
    }
    for (auto& c : g_cells) if (c.hwnd == target) c.thumb = NULL;
}

static RECT g_dragGhostRect = { 0 };

static void UpdateDragGhost()
{
    g_dragGhostRect = RECT();
    if (!g_dragging || !IsWindow(g_dragHwnd)) return;
    if (g_dragCell < 0 || g_dragCell >= (int)g_cells.size()) return;
    CellRC& dc = g_cells[g_dragCell];
    int dw = (int)((dc.rcAll.right - dc.rcAll.left) * g_dragScale);
    int dh = (int)((dc.rcAll.bottom - dc.rcAll.top) * g_dragScale);
    g_dragGhostRect.left = g_dragPt.x - dw / 2;
    g_dragGhostRect.top = g_dragPt.y - dh / 2;
    g_dragGhostRect.right = g_dragGhostRect.left + dw;
    g_dragGhostRect.bottom = g_dragGhostRect.top + dh;
    if (g_dragThumb)
    {
        DWM_THUMBNAIL_PROPERTIES p = {};
        p.dwFlags = MY_DWM_TNP_RECTDESTINATION | MY_DWM_TNP_OPACITY | MY_DWM_TNP_VISIBLE;
        p.rcDestination = g_dragGhostRect;
        p.opacity = 200;
        p.fVisible = TRUE;
        DwmUpdateThumbnailProperties(g_dragThumb, &p);
    }
}

static void RegisterThumbs()
{
    BOOL on = FALSE;
    if (!SUCCEEDED(DwmIsCompositionEnabled(&on)) || !on) { UnregisterThumbs(); return; }

    for (auto& c : g_cells) c.thumb = NULL;

    for (size_t i = 0; i < g_reg.size(); )
    {
        HWND h = g_reg[i].first;
        bool want = IsWindow(h) && IsWindowVisible(h) && !IsIconic(h);
        if (want)
        {
            want = false;
            for (auto& c : g_cells)
                if (c.hwnd == h && c.rcThumb.top >= GridTop()) { want = true; break; }
        }
        if (!want)
        {
            DwmUnregisterThumbnail(g_reg[i].second);
            g_reg.erase(g_reg.begin() + i);
        }
        else ++i;
    }

    for (auto& c : g_cells)
    {
        if (IsIconic(c.hwnd)) continue;
        if (!IsWindowVisible(c.hwnd)) continue;
        if (c.rcThumb.top < GridTop()) continue;
        HTHUMBNAIL th = NULL;
        for (auto& r : g_reg)
            if (r.first == c.hwnd) { th = r.second; break; }
        if (!th)
        {
            if (RegThumbSafe(g_tv, c.hwnd, &th) != S_OK) continue;
            g_reg.push_back(std::make_pair(c.hwnd, th));
        }
        DWM_THUMBNAIL_PROPERTIES p = {};

        RECT cr = { 0, 0, 0, 0 };
        GetClientRect(c.hwnd, &cr);
        p.dwFlags = MY_DWM_TNP_RECTDESTINATION | MY_DWM_TNP_RECTSOURCE
                  | MY_DWM_TNP_OPACITY | MY_DWM_TNP_VISIBLE;
        p.rcDestination = c.rcThumb;
        p.rcSource = cr;
        p.opacity = 255;
        p.fVisible = TRUE;
        DwmUpdateThumbnailProperties(th, &p);
        c.thumb = th;
    }
}

static HICON WindowIcon(HWND h)
{
    HICON ic = (HICON)SendMessageW(h, WM_GETICON, ICON_SMALL, 0);
    if (!ic) ic = (HICON)SendMessageW(h, WM_GETICON, ICON_BIG, 0);
    if (!ic) ic = (HICON)GetClassLongPtrW(h, GCLP_HICONSM);
    if (!ic) ic = (HICON)GetClassLongPtrW(h, GCLP_HICON);
    return ic;
}

static void DrawSnapshotFade(HDC hdc, HBITMAP bmp, const RECT& dest, BYTE alpha)
{
    if (!bmp || alpha == 0) return;
    BITMAP bm;
    if (!GetObjectW(bmp, sizeof(bm), &bm)) return;
    int dw = dest.right - dest.left, dh = dest.bottom - dest.top;
    if (dw < 4 || dh < 4 || bm.bmWidth < 1 || bm.bmHeight < 1) return;
    double sx = (double)dw / bm.bmWidth, sy = (double)dh / bm.bmHeight;
    double sc = sx < sy ? sx : sy;
    int w = (int)(bm.bmWidth * sc), h = (int)(bm.bmHeight * sc);
    int x = dest.left + (dw - w) / 2, y = dest.top + (dh - h) / 2;

    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(mem, bmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, alpha, 0 };
    AlphaBlend(hdc, x, y, w, h, mem, 0, 0, bm.bmWidth, bm.bmHeight, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void DrawSnapshotFit(HDC hdc, HBITMAP bmp, const RECT& dest)
{
    if (!bmp) return;
    BITMAP bm;
    if (!GetObjectW(bmp, sizeof(bm), &bm)) return;
    int dw = dest.right - dest.left, dh = dest.bottom - dest.top;
    if (dw < 4 || dh < 4 || bm.bmWidth < 1 || bm.bmHeight < 1) return;
    double sx = (double)dw / bm.bmWidth, sy = (double)dh / bm.bmHeight;
    double s = sx < sy ? sx : sy;
    int w = (int)(bm.bmWidth * s), h = (int)(bm.bmHeight * s);
    int x = dest.left + (dw - w) / 2, y = dest.top + (dh - h) / 2;

    HDC mem = CreateCompatibleDC(hdc);
    HGDIOBJ old = SelectObject(mem, bmp);
    SetStretchBltMode(hdc, HALFTONE);
    SetBrushOrgEx(hdc, 0, 0, NULL);
    StretchBlt(hdc, x, y, w, h, mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

static int snapWidth(HBITMAP b)  { BITMAP bm; return GetObjectW(b, sizeof(bm), &bm) ? bm.bmWidth  : 1; }
static int snapHeight(HBITMAP b) { BITMAP bm; return GetObjectW(b, sizeof(bm), &bm) ? bm.bmHeight : 1; }

static RECT ScreenRect()
{

    RECT r = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    RECT work = {};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0))
    {
        r.bottom = work.bottom;
        if (work.left < 0 || work.top < 0) {}
    }
    return r;
}

static int CardsContentWidth(int ndesktops)
{
    ndesktops++;
    return ndesktops * CARD_W + ndesktops * CARD_GAP;
}

static void Layout(HWND hwnd)
{
    g_cards.clear();
    g_cells.clear();
    if (!hwnd) return;

    RECT scr = ScreenRect();
    int W = scr.right - scr.left;

    int maxScroll = 0;
    int content = CardsContentWidth((int)g_desktops.size());
    if (content > W - TOP_M * 2)
        maxScroll = content - (W - TOP_M * 2);
    if (g_cardScroll > maxScroll) g_cardScroll = maxScroll;
    if (g_cardScroll < 0) g_cardScroll = 0;

    if ((int)g_desktops.size() <= 1)
    {

        CardRC plus;
        plus.isPlus = true; plus.desk = -1;
        plus.rc.left = GRID_M; plus.rc.top = TOP_M;
        plus.rc.right = plus.rc.left + 150; plus.rc.bottom = plus.rc.top + 34;
        g_cards.push_back(plus);
        g_cardScroll = 0;
    }
    else
    {
        int x = TOP_M - g_cardScroll;
        for (int i = 0; i < (int)g_desktops.size(); i++)
        {
            CardRC c;
            c.isPlus = false;
            c.desk = i;
            c.rc.left = x; c.rc.top = TOP_M + LABEL_H;
            c.rc.right = x + CARD_W; c.rc.bottom = c.rc.top + CARD_H;
            g_cards.push_back(c);
            x += CARD_W + CARD_GAP;
        }
        CardRC plus;
        plus.isPlus = true; plus.desk = -1;
        plus.rc.left = x; plus.rc.top = TOP_M + LABEL_H;
        plus.rc.right = x + CARD_W; plus.rc.bottom = plus.rc.top + CARD_H;
        g_cards.push_back(plus);
    }

    int gridTop = GridTop();
    RECT area = { GRID_M, gridTop, W - GRID_M, scr.bottom - BOTTOM_M };
    int aw = area.right - area.left, ah = area.bottom - area.top;

    vector<HWND> wins;
    for (HWND h = GetTopWindow(NULL); h; h = GetNextWindow(h, GW_HWNDNEXT))
    {
        auto it = g_winDesk.find(h);
        if (it != g_winDesk.end() && it->second == g_renderDesk && IsWindow(h))
            wins.push_back(h);
    }
    int n = (int)wins.size();
    if (n == 0) return;

    vector<double> ar(n);
    for (int i = 0; i < n; i++)
    {

        double a = 1.6;
        if (!IsIconic(wins[i]))
        {
            RECT cr = { 0, 0, 0, 0 };
            GetClientRect(wins[i], &cr);
            if (cr.bottom > 0) a = (double)cr.right / cr.bottom;
        }
        HBITMAP snap = GetSnapshot(wins[i]);
        if (snap)
        {
            BITMAP bm;
            if (GetObjectW(snap, sizeof(bm), &bm) && bm.bmHeight > 0)
                a = (double)bm.bmWidth / bm.bmHeight;
        }
        if (a < 0.4) a = 0.4;
        if (a > 4.0) a = 4.0;
        ar[i] = a;
    }

    int maxR = n < 6 ? n : 6;
    bool chosen = false;
    vector<vector<int>> bestRows;
    vector<double> bestH;
    for (int R = 1; R <= maxR && !chosen; R++)
    {
        double H0 = (ah - (R - 1) * CELL_GAP) / R;
        if (H0 < 110) break;
        if (H0 > 420) H0 = 420;

        vector<vector<int>> rows(1);
        double rowW = 0;
        for (int i = 0; i < n; i++)
        {
            double w = ar[i] * H0;
            if (!rows.back().empty() && rowW + w > aw)
            {
                rows.push_back(vector<int>());
                rowW = 0;
            }
            rows.back().push_back(i);
            rowW += w + TILE_GAP_H;
        }
        if ((int)rows.size() > R) continue;

        double Hsum = 0;
        vector<double> Hs;
        for (int k = 0; k < (int)rows.size(); k++)
        {
            double wnat = 0;
            for (int t = 0; t < (int)rows[k].size(); t++) wnat += ar[rows[k][t]] * H0;
            wnat += (rows[k].size() - 1) * TILE_GAP_H;
            double Hr = (wnat > 0) ? H0 * aw / wnat : H0;
            if (Hr > 560) Hr = 560;
            if (Hr < 100) Hr = 100;
            Hs.push_back(Hr);
            Hsum += Hr + TITLE_H;
        }
        Hsum += (rows.size() - 1) * CELL_GAP;
        if (Hsum > ah)
        {
            double k2 = (double)ah / Hsum;
            for (auto& h : Hs) h *= k2;
            for (auto& h : Hs) if (h < 100) h = 100;
        }
        chosen = true;
        bestRows = rows;
        bestH = Hs;
    }
    if (bestRows.empty())
    {
        bestRows.assign(1, vector<int>());
        for (int i = 0; i < n; i++) bestRows[0].push_back(i);
        bestH.push_back(240);
    }

    int rowsCnt = (int)bestRows.size();
    int totalH = 0;
    for (int k = 0; k < rowsCnt; k++) totalH += (int)bestH[k] + TITLE_H;
    totalH += (rowsCnt - 1) * CELL_GAP;
    int maxGridScroll = totalH > ah ? totalH - ah : 0;
    if (g_gridScroll > maxGridScroll) g_gridScroll = maxGridScroll;
    if (g_gridScroll < 0) g_gridScroll = 0;
    int y = area.top - g_gridScroll;

    for (int k = 0; k < rowsCnt; k++)
    {
        int rowH = (int)bestH[k];
        int cnt = (int)bestRows[k].size();
        int rowW2 = 0;
        for (size_t t = 0; t < bestRows[k].size(); t++) rowW2 += (int)(ar[bestRows[k][t]] * rowH);
        rowW2 += (cnt - 1) * TILE_GAP_H;
        if (rowW2 > aw) { rowH = (int)((double)rowH * aw / rowW2); }
        int x = area.left;
        for (size_t t = 0; t < bestRows[k].size(); t++)
        {
            int idx = bestRows[k][t];
            int tileW = (int)(ar[idx] * rowH);
            if (tileW < 40) break;
            CellRC cell;
            cell.hwnd = wins[idx];
            cell.thumb = NULL;
            cell.hasFrom = false;
            cell.rcAll.left = x;
            cell.rcAll.top = y;
            cell.rcAll.right = x + tileW;
            cell.rcAll.bottom = y + TITLE_H + rowH;
            cell.rcTitle = cell.rcAll;
            cell.rcTitle.bottom = cell.rcTitle.top + TITLE_H;
            cell.rcThumb = cell.rcAll;
            cell.rcThumb.top = cell.rcTitle.bottom;
            g_cells.push_back(cell);
            x += tileW + TILE_GAP_H;
        }
        y += rowH + TITLE_H + CELL_GAP;
    }
}

bool TaskViewVisible() { return g_tv && IsWindowVisible(g_tv); }

void TaskViewRestoreFocus()
{
    if (g_tv && IsWindowVisible(g_tv))
    {
        SetForegroundWindow(g_tv);
        SetFocus(g_tv);
    }
}

void SnapshotInitAll()
{
    for (auto& kv : g_winDesk)
    {
        if (!IsWindow(kv.first)) continue;
        if (kv.second != g_current) continue;
        SnapshotForCell(kv.first, true);
    }
    PruneWindows();
}

static HBITMAP g_wallBlur = NULL;
static HBITMAP g_wallCard = NULL;
static HBITMAP g_darkBmp   = NULL;
static HBITMAP g_lightBmp  = NULL;
static HBITMAP g_wallDim   = NULL;
static HBITMAP g_wallTile  = NULL;

static HBITMAP ScaleBitmap(HDC screen, HBITMAP src, int dw, int dh)
{
    BITMAP bm;
    if (!GetObjectW(src, sizeof(bm), &bm)) return NULL;
    HBITMAP out = CreateCompatibleBitmap(screen, dw, dh);
    HDC dst = CreateCompatibleDC(screen);
    HDC sdc = CreateCompatibleDC(screen);
    HGDIOBJ od = SelectObject(dst, out);
    HGDIOBJ os = SelectObject(sdc, src);
    SetStretchBltMode(dst, HALFTONE);
    SetBrushOrgEx(dst, 0, 0, NULL);
    StretchBlt(dst, 0, 0, dw, dh, sdc, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SelectObject(dst, od); SelectObject(sdc, os);
    DeleteDC(dst); DeleteDC(sdc);
    return out;
}

static HBITMAP LoadWallpaperFile(const wchar_t* path, int w, int h)
{
    IPicture* pic = NULL;
    HRESULT hr = OleLoadPicturePath((OLECHAR*)path, NULL, 0, (OLE_COLOR)NULL,
                                    IID_IPicture, (LPVOID*)&pic);
    if (FAILED(hr) || !pic) return NULL;

    OLE_XSIZE_HIMETRIC hmW = 0, hmH = 0;
    pic->get_Width(&hmW);
    pic->get_Height(&hmH);
    HDC screen = GetDC(NULL);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(mem, bmp);
    RECT rc = { 0, 0, w, h };
    HBRUSH b = CreateSolidBrush(RGB(16, 16, 16));
    FillRect(mem, &rc, b);
    DeleteObject(b);
    pic->Render(mem, 0, 0, w, h, 0, hmH, hmW, -hmH, &rc);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, screen);
    pic->Release();
    return bmp;
}

static HBITMAP CaptureWallpaperScreen()
{
    HWND prog = FindWindowW(L"Progman", NULL);
    if (!prog) return NULL;
    RECT rc = { 0 };
    GetWindowRect(prog, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w < 64 || h < 64) return NULL;
    HWND dv = FindWindowExW(prog, NULL, L"SHELLDLL_DefView", NULL);
    if (dv) ShowWindow(dv, SW_HIDE);
    HDC scr = GetDC(NULL);
    HBITMAP bmp = CreateCompatibleBitmap(scr, w, h);
    HDC mem = CreateCompatibleDC(scr);
    HGDIOBJ old = SelectObject(mem, bmp);
    BOOL ok = PrintWindow(prog, mem, 0);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
    if (dv) ShowWindow(dv, SW_SHOW);
    if (!ok) { DeleteObject(bmp); return NULL; }
    return bmp;
}

static void BuildBackground(int w, int h)
{
    if (g_wallDim) return;

    wchar_t appdata[MAX_PATH] = L"";
    wstring path;
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata)))
        path = wstring(appdata) + L"\\Microsoft\\Windows Themes\\TranscodedWallpaper";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        wchar_t reg[MAX_PATH] = L"";
        DWORD sz = sizeof(reg);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"Wallpaper",
                         RRF_RT_REG_SZ, NULL, reg, &sz) == ERROR_SUCCESS && reg[0]
            && GetFileAttributesW(reg) != INVALID_FILE_ATTRIBUTES)
            path = reg;
        else
            path.clear();
    }

    HDC screen = GetDC(NULL);
    HBITMAP wall = path.empty() ? NULL : LoadWallpaperFile(path.c_str(), w, h);
    if (wall)
    {
        g_wallBlur = wall;
    }
    else
    {
        HBITMAP cap = CaptureWallpaperScreen();
        if (cap)
        {
            g_wallBlur = ScaleBitmap(screen, cap, w, h);
            DeleteObject(cap);
        }
    }
    if (g_wallBlur)
    {
        g_wallCard = ScaleBitmap(screen, g_wallBlur, CARD_W, CARD_H);
        g_wallTile = ScaleBitmap(screen, g_wallBlur, 480, 360);
    }

    if (!g_lightBmp)
    {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = 64;
        bi.bmiHeader.biHeight = -64;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = NULL;
        HBITMAP light = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (light)
        {
            DWORD* px = (DWORD*)bits;
            for (int i = 0; i < 64 * 64; i++) px[i] = 0x1EFFFFFF;
            g_lightBmp = light;
        }
    }
    if (!g_darkBmp)
    {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = 64;
        bi.bmiHeader.biHeight = -64;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = NULL;
        HBITMAP dark = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (dark)
        {
            DWORD* px = (DWORD*)bits;
            for (int i = 0; i < 64 * 64; i++) px[i] = 0x6E000000;
            g_darkBmp = dark;
        }
    }

    if (g_wallBlur && g_darkBmp && !g_wallDim)
    {
        g_wallDim = CreateCompatibleBitmap(screen, w, h);
        if (g_wallDim)
        {
            HDC d = CreateCompatibleDC(screen);
            HGDIOBJ od = SelectObject(d, g_wallDim);
            HDC s = CreateCompatibleDC(screen);
            HGDIOBJ os = SelectObject(s, g_wallBlur);
            BitBlt(d, 0, 0, w, h, s, 0, 0, SRCCOPY);
            SelectObject(s, os);
            DeleteDC(s);
            HDC k = CreateCompatibleDC(screen);
            HGDIOBJ ok = SelectObject(k, g_darkBmp);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(d, 0, 0, w, h, k, 0, 0, 64, 64, bf);
            SelectObject(k, ok);
            DeleteDC(k);
            SelectObject(d, od);
            DeleteDC(d);
        }
    }
    ReleaseDC(NULL, screen);
}

static void DrawBackground(HDC buf, int w, int h, BYTE fade)
{
    if (g_wallDim)
    {
        if (fade >= 255)
        {
            HDC dc = CreateCompatibleDC(buf);
            HGDIOBJ old = SelectObject(dc, g_wallDim);
            BitBlt(buf, 0, 0, w, h, dc, 0, 0, SRCCOPY);
            SelectObject(dc, old);
            DeleteDC(dc);
        }
        else if (fade > 0)
        {
            HDC dc = CreateCompatibleDC(buf);
            HGDIOBJ old = SelectObject(dc, g_wallDim);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, fade, 0 };
            AlphaBlend(buf, 0, 0, w, h, dc, 0, 0, w, h, bf);
            SelectObject(dc, old);
            DeleteDC(dc);
        }
        else
        {
            RECT rc = { 0, 0, w, h };
            FillRect(buf, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
    }
    else
    {
        RECT rc = { 0, 0, w, h };
        HBRUSH bg = CreateSolidBrush(RGB(20, 20, 20));
        FillRect(buf, &rc, bg);
        DeleteObject(bg);
    }
}

static void EnsureFonts()
{
    if (g_fLabel) return;
    g_fLabel = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_fCell = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    g_fPlus = CreateFontW(-44, 0, 0, 0, FW_LIGHT, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
}

void TaskViewRefresh()
{
    static int lastCount = 1;
    int now = (int)g_desktops.size();
    if (!g_tv || !IsWindowVisible(g_tv))
    {
        UnregisterThumbs();
        g_cells.clear();
        g_cards.clear();
        lastCount = now;
        return;
    }
    if ((lastCount > 1) != (now > 1))
    {
        g_topFrom = GridTop();
        g_structAnim = 1;
        g_structStart = GetTickCount();
        SetTimer(g_tv, 11, 16, NULL);
        g_renderDesk = g_current;
        Layout(g_tv);
        RegisterThumbs();
        InvalidateRect(g_tv, NULL, FALSE);
    }
    else
    {
        g_renderDesk = g_current;
        Layout(g_tv);
        RegisterThumbs();
        InvalidateRect(g_tv, NULL, FALSE);
    }
    lastCount = now;
}

void TaskViewShow()
{
    if (TaskViewVisible()) { SetForegroundWindow(g_tv); return; }
    if (g_closeAnim) FinalizeHide();
    if (!g_tv)
    {
        if (!TaskViewEnsureClass(g_hInst)) return;
        RECT scr = ScreenRect();
        g_tv = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, TV_CLASS, L"",
                               WS_POPUP, scr.left, scr.top, scr.right - scr.left, scr.bottom - scr.top,
                               NULL, NULL, g_hInst, NULL);
        if (!g_tv) return;
    }
    g_cardScroll = 0;
    g_gridScroll = 0;
    {
        RECT scr = ScreenRect();
        int W = scr.right - scr.left;
        int nd = (int)g_desktops.size();
        if (nd > 1)
        {
            int avail = W - TOP_M * 2;
            int content = CardsContentWidth(nd);
            if (content > avail)
            {
                int curX = g_current * (CARD_W + CARD_GAP);
                g_cardScroll = curX + CARD_W / 2 - avail / 2;
                int maxScroll = content - avail;
                if (g_cardScroll > maxScroll) g_cardScroll = maxScroll;
                if (g_cardScroll < 0) g_cardScroll = 0;
            }
        }
    }
    g_hoverCard = -1; g_hoverCell = -1; g_hoverClose = false;
    g_structAnim = 0;
    g_renderDesk = g_current;
    EnsureFonts();
    {
        RECT scr = ScreenRect();
        BuildBackground(scr.right - scr.left, scr.bottom - scr.top);
    }

    Layout(g_tv);
    for (auto& c : g_cells)
    {
        auto it = g_winDesk.find(c.hwnd);
        if (it == g_winDesk.end()) continue;
        if (IsIconic(c.hwnd) && !GetSnapshot(c.hwnd) && it->second == g_current)
            SnapshotForCell(c.hwnd, true);
    }

    g_openFg = GetForegroundWindow();
    {
        RECT scr = ScreenRect();
        for (auto& c : g_cells)
        {
            c.hasFrom = false;
            if (c.hwnd == g_openFg && !IsIconic(c.hwnd))
            {
                RECT wr;
                if (GetWindowRect(c.hwnd, &wr) && wr.right - wr.left > 8)
                {
                    c.rcFrom.left = wr.left - scr.left; c.rcFrom.top = wr.top - scr.top;
                    c.rcFrom.right = c.rcFrom.left + (wr.right - wr.left);
                    c.rcFrom.bottom = c.rcFrom.top + (wr.bottom - wr.top);
                    c.hasFrom = true;
                }
            }
        }
    }

    ShowWindow(g_tv, SW_SHOW);

    g_openAnimStart = GetTickCount();
    SetTimer(g_tv, 5, 16, NULL);
    RegisterThumbs();
    SetForegroundWindow(g_tv);
    SetFocus(g_tv);
    InvalidateRect(g_tv, NULL, FALSE);
}

static void FinalizeHide()
{
    g_closing = true;
    g_scrollDrag = false;
    UnregisterThumbs();
    g_dragThumb = NULL;
    KillTimer(g_tv, 11);
    g_structAnim = 0;
    KillTimer(g_tv, 5);
    KillTimer(g_tv, 2);
    g_openAnimStart = 0;
    g_animClose = false;
    g_closeAnim = false;
    g_hoverCellClose = false;
    ShowWindow(g_tv, SW_HIDE);
    g_closing = false;
    g_renderDesk = g_current;
}

void TaskViewHide()
{
    if (!g_tv || g_closing || g_closeAnim) return;
    if (IsWindowVisible(g_tv))
    {

        g_closeAnim = true;
        UnregisterThumbs();
        KillTimer(g_tv, 2);
        g_openAnimStart = GetTickCount();
        g_animClose = true;
        SetTimer(g_tv, 5, 16, NULL);
        InvalidateRect(g_tv, NULL, FALSE);
        return;
    }
    FinalizeHide();
}

void TaskViewToggle() { TaskViewVisible() ? TaskViewHide() : TaskViewShow(); }

static RECT CellCloseRect(const CellRC& cell)
{
    RECT r;
    int side = TITLE_H - 4;
    r.right = cell.rcTitle.right - 2;
    r.left = r.right - side;
    r.top = cell.rcTitle.top + 2;
    r.bottom = r.top + side;
    return r;
}

static RECT CardFullRect(const CardRC& card)
{
    RECT r = card.rc;
    if (g_desktops.size() > 1) r.top = card.rc.top - LABEL_H;
    return r;
}

static int CardScrollbarInfo(int W, int& thumbX, int& thumbW)
{
    int nd = (int)g_desktops.size();
    if (nd <= 1) return 0;
    int content = CardsContentWidth(nd);
    int avail = W - TOP_M * 2;
    if (content <= avail) return 0;
    int maxScroll = content - avail;
    thumbW = avail * avail / content;
    if (thumbW < 20) thumbW = 20;
    thumbX = TOP_M + (int)((double)g_cardScroll / maxScroll * (avail - thumbW));
    return avail;
}

static void PaintCardScrollbar(HDC hdc, int W)
{
    int thumbX, thumbW;
    int barW = CardScrollbarInfo(W, thumbX, thumbW);
    if (barW <= 0) return;
    int y = TOP_M + LABEL_H + CARD_H + 5;
    HBRUSH track = CreateSolidBrush(RGB(70, 70, 70));
    RECT rcT = { TOP_M, y, TOP_M + barW, y + 4 };
    FillRect(hdc, &rcT, track);
    DeleteObject(track);
    HBRUSH thumb = CreateSolidBrush(RGB(190, 190, 190));
    RECT rcS = { thumbX, y, thumbX + thumbW, y + 4 };
    FillRect(hdc, &rcS, thumb);
    DeleteObject(thumb);
}

static void DrawXMark(HDC hdc, const RECT& r)
{
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    int hs = ((r.right - r.left) / 2) - 4;
    if (hs < 2) hs = 2;
    HPEN xp = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    HGDIOBJ op = SelectObject(hdc, xp);
    MoveToEx(hdc, cx - hs, cy - hs, NULL);
    LineTo(hdc, cx + hs + 1, cy + hs + 1);
    MoveToEx(hdc, cx - hs, cy + hs, NULL);
    LineTo(hdc, cx + hs + 1, cy - hs - 1);
    SelectObject(hdc, op);
    DeleteObject(xp);
}

static HWND TopWindowOfDesktop(int desk)
{
    HWND first = NULL;
    for (HWND h = GetTopWindow(NULL); h; h = GetNextWindow(h, GW_HWNDNEXT))
    {
        auto it = g_winDesk.find(h);
        if (it != g_winDesk.end() && it->second == desk && IsWindow(h))
        {
            if (!first) first = h;
            if (GetSnapshot(h)) return h;
        }
    }
    return first;
}

static RECT CardCloseRect(const CardRC& card)
{
    RECT r;
    int side = LABEL_H - 4;
    r.right = card.rc.right - 2;
    r.left = r.right - side;
    r.top = card.rc.top - LABEL_H + 2;
    r.bottom = r.top + side;
    return r;
}

static void PaintCard(HDC hdc, const CardRC& card, int i)
{
    wchar_t label[64];
    bool singleMode = (int)g_desktops.size() <= 1;
    if (card.isPlus) wcscpy(label, L"新建桌面");
    else wcsncpy(label, DesktopName(card.desk).c_str(), 63), label[63] = 0;

    RECT rcLabel = card.rc;
    if (card.isPlus && singleMode) rcLabel.top = rcLabel.bottom;
    rcLabel.top -= LABEL_H; rcLabel.bottom = card.rc.top - 4;
    HGDIOBJ oldF = SelectObject(hdc, g_fLabel);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(240, 240, 240));
    DrawTextW(hdc, label, -1, &rcLabel, DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_END_ELLIPSIS);

    int cw = card.rc.right - card.rc.left, ch = card.rc.bottom - card.rc.top;

    if (card.isPlus && singleMode)
    {

        SelectObject(hdc, g_fCell);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, g_hoverCard == i ? RGB(255, 255, 255) : RGB(228, 228, 228));
        RECT rcT = card.rc; rcT.left += 10;
        DrawTextW(hdc, L"+  新建桌面", -1, &rcT, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    else if (card.isPlus)
    {

        bool hov = (g_hoverCard == i);
        HPEN pen = CreatePen(PS_SOLID, hov ? 2 : 1, hov ? RGB(255, 255, 255) : RGB(150, 150, 150));
        HGDIOBJ oldP = SelectObject(hdc, pen);
        HGDIOBJ oldB = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RECT rcB = card.rc;
        if (hov) { rcB.left++; rcB.top++; rcB.right--; rcB.bottom--; }
        Rectangle(hdc, rcB.left, rcB.top, rcB.right, rcB.bottom);
        SelectObject(hdc, oldP); SelectObject(hdc, oldB);
        DeleteObject(pen);
        SelectObject(hdc, g_fPlus);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(240, 240, 240));
        DrawTextW(hdc, L"+", -1, (RECT*)&card.rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    else
    {

        if (g_wallCard)
        {
            HDC wdc = CreateCompatibleDC(hdc);
            HGDIOBJ oldW = SelectObject(wdc, g_wallCard);
            BitBlt(hdc, card.rc.left, card.rc.top, cw, ch, wdc, 0, 0, SRCCOPY);
            SelectObject(wdc, oldW);
            DeleteDC(wdc);

            HDC ddc = CreateCompatibleDC(hdc);
            HGDIOBJ oldD = SelectObject(ddc, g_darkBmp);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 50, AC_SRC_ALPHA };
            AlphaBlend(hdc, card.rc.left, card.rc.top, cw, ch, ddc, 0, 0, 64, 64, bf);
            SelectObject(ddc, oldD);
            DeleteDC(ddc);
        }
        else
        {
            HBRUSH bg = CreateSolidBrush(RGB(30, 30, 30));
            FillRect(hdc, &card.rc, bg);
            DeleteObject(bg);
        }

        HWND top = TopWindowOfDesktop(card.desk);
        HBITMAP snap = top ? GetSnapshot(top) : NULL;
        if (snap)
        {
            RECT inner;
            inner.left = card.rc.left + cw * 10 / 100;
            inner.top = card.rc.top + ch * 10 / 100;
            inner.right = card.rc.left + cw * 90 / 100;
            inner.bottom = card.rc.top + ch * 90 / 100;

            HBRUSH frame = CreateSolidBrush(RGB(12, 12, 12));
            FillRect(hdc, &inner, frame);
            DeleteObject(frame);
            DrawSnapshotFit(hdc, snap, inner);
            HPEN pb = CreatePen(PS_SOLID, 1, RGB(15, 15, 15));
            HGDIOBJ oldPp = SelectObject(hdc, pb);
            HGDIOBJ oldBp = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, inner.left, inner.top, inner.right, inner.bottom);
            SelectObject(hdc, oldPp); SelectObject(hdc, oldBp);
            DeleteObject(pb);
        }

    }

    bool isCur = !card.isPlus && card.desk == g_renderDesk;
    bool hovered = (g_hoverCard == i);
    HPEN pen = CreatePen(PS_SOLID, isCur ? 2 : 1,
                         isCur ? RGB(255, 255, 255) : hovered ? RGB(210, 210, 210) : RGB(90, 90, 90));
    HGDIOBJ oldP = SelectObject(hdc, pen);
    HGDIOBJ oldB = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RECT rcB = card.rc;
    if (isCur) { rcB.left++; rcB.top++; rcB.right--; rcB.bottom--; }
    Rectangle(hdc, rcB.left, rcB.top, rcB.right, rcB.bottom);
    SelectObject(hdc, oldP);
    SelectObject(hdc, oldB);
    DeleteObject(pen);

    if (!card.isPlus && hovered)
    {
        RECT cr = CardCloseRect(card);
        if (g_hoverClose)
        {
            HBRUSH rb = CreateSolidBrush(RGB(229, 57, 53));
            FillRect(hdc, &cr, rb);
            DeleteObject(rb);
        }
        DrawXMark(hdc, cr);
    }
    SelectObject(hdc, oldF);
}

static void PaintCell(HDC hdc, const CellRC& cell, int i)
{
    bool hovered = (g_hoverCell == i);
    int tw = cell.rcThumb.right - cell.rcThumb.left;
    int th = cell.rcThumb.bottom - cell.rcThumb.top;

    SaveDC(hdc);
    IntersectClipRect(hdc, cell.rcAll.left, cell.rcAll.top,
                       cell.rcAll.right, cell.rcAll.bottom);

    HBRUSH strip = CreateSolidBrush(RGB(22, 22, 22));
    FillRect(hdc, &cell.rcTitle, strip);
    DeleteObject(strip);

    if (g_wallTile)
    {
        HDC wdc = CreateCompatibleDC(hdc);
        HGDIOBJ oldW = SelectObject(wdc, g_wallTile);
        SetStretchBltMode(hdc, HALFTONE);
        StretchBlt(hdc, cell.rcThumb.left, cell.rcThumb.top, tw, th,
                   wdc, 0, 0, 480, 360, SRCCOPY);
        SelectObject(wdc, oldW);
        DeleteDC(wdc);
    }
    else
    {
        HBRUSH bg = CreateSolidBrush(RGB(20, 20, 20));
        FillRect(hdc, &cell.rcThumb, bg);
        DeleteObject(bg);
    }

    HBITMAP snap = GetSnapshot(cell.hwnd);
    if (!cell.thumb && snap)
        DrawSnapshotFit(hdc, snap, cell.rcThumb);
    else if (!cell.thumb)
    {
        HICON ic = WindowIcon(cell.hwnd);
        if (ic)
        {
            int cx = cell.rcThumb.left + (tw - 48) / 2;
            int cy = cell.rcThumb.top + (th - 48) / 2;
            DrawIconEx(hdc, cx, cy, ic, 48, 48, 0, NULL, DI_NORMAL);
        }
    }

    wchar_t title[128];
    GetWindowTextW(cell.hwnd, title, 128);
    HICON icon = WindowIcon(cell.hwnd);
    if (icon) DrawIconEx(hdc, cell.rcTitle.left + 4, cell.rcTitle.top + (TITLE_H - 16) / 2, icon, 16, 16, 0, NULL, DI_NORMAL);
    RECT rcT = cell.rcTitle; rcT.left += 28; rcT.right -= 8;
    SelectObject(hdc, g_fCell);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, hovered ? RGB(255, 255, 255) : RGB(216, 216, 216));
    DrawTextW(hdc, title, -1, &rcT, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (hovered)
    {
        RECT cr = CellCloseRect(cell);
        if (g_hoverCellClose)
        {
            HBRUSH rb = CreateSolidBrush(RGB(229, 57, 53));
            FillRect(hdc, &cr, rb);
            DeleteObject(rb);
        }
        DrawXMark(hdc, cr);
    }

    if (g_dragging && g_dragCell == i)
    {
        HDC ddc = CreateCompatibleDC(hdc);
        HGDIOBJ oldD = SelectObject(ddc, g_darkBmp);
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 140, AC_SRC_ALPHA };
        AlphaBlend(hdc, cell.rcAll.left, cell.rcAll.top,
                   cell.rcAll.right - cell.rcAll.left, cell.rcAll.bottom - cell.rcAll.top,
                   ddc, 0, 0, 64, 64, bf);
        SelectObject(ddc, oldD);
        DeleteDC(ddc);
    }

    HPEN pen = CreatePen(PS_SOLID, hovered ? 2 : 1, hovered ? RGB(255, 255, 255) : RGB(80, 80, 80));
    HGDIOBJ oldP = SelectObject(hdc, pen);
    HGDIOBJ oldB = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    RECT rcB = cell.rcAll;
    if (hovered) { rcB.left++; rcB.top++; rcB.right--; rcB.bottom--; }
    Rectangle(hdc, rcB.left, rcB.top, rcB.right, rcB.bottom);
    SelectObject(hdc, oldP);
    SelectObject(hdc, oldB);
    DeleteObject(pen);

    RestoreDC(hdc, -1);
}

static LRESULT CALLBACK TV_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC buf = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ oldB = SelectObject(buf, bmp);
        bool animating = g_openAnimStart != 0;
        float animE = 1.0f;
        if (animating)
        {
            float t = (float)(GetTickCount() - g_openAnimStart) / OPEN_ANIM_MS;
            if (t >= 1.0f)
            {

                animating = false;
            }
            else
            {
                if (t < 0) t = 0;
                float e = t * t * (3 - 2 * t);
                animE = g_animClose ? (1.0f - e) : e;
                DrawBackground(buf, rc.right, rc.bottom, (BYTE)(255 * animE));
            }
        }
        if (!animating)
            DrawBackground(buf, rc.right, rc.bottom, 255);
        {

            HDC ddc = CreateCompatibleDC(buf);
            HGDIOBJ oldD = SelectObject(ddc, g_lightBmp);
            BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
            AlphaBlend(buf, 0, 0, rc.right, GridTop() - 12, ddc, 0, 0, 64, 64, bf);
            SelectObject(ddc, oldD);
            DeleteDC(ddc);

            int sy = GridTop() - 12;
            HPEN sep = CreatePen(PS_SOLID, 1, RGB(90, 90, 90));
            HGDIOBJ oldPen = SelectObject(buf, sep);
            MoveToEx(buf, GRID_M, sy, NULL);
            LineTo(buf, rc.right - GRID_M, sy);
            SelectObject(buf, oldPen);
            DeleteObject(sep);
        }
        for (int i = 0; i < (int)g_cards.size(); i++) PaintCard(buf, g_cards[i], i);
        PaintCardScrollbar(buf, rc.right);
        SaveDC(buf);
        IntersectClipRect(buf, 0, GridTop(), rc.right, rc.bottom);
        if (animating)
        {

            for (int i = 0; i < (int)g_cells.size(); i++)
            {
                CellRC c = g_cells[i];
                HBITMAP snap = GetSnapshot(c.hwnd);
                if (!snap) continue;
                if (c.hwnd == g_openFg && c.hasFrom) continue;
                DrawSnapshotFade(buf, snap, c.rcThumb, (BYTE)(255 * animE));
            }
        }
        else
        {
            for (int i = 0; i < (int)g_cells.size(); i++) PaintCell(buf, g_cells[i], i);
        }
        RestoreDC(buf, -1);
        if (animating)
        {
            for (int i = 0; i < (int)g_cells.size(); i++)
            {
                CellRC c = g_cells[i];
                if (c.hwnd != g_openFg || !c.hasFrom) continue;
                HBITMAP snap = GetSnapshot(c.hwnd);
                if (!snap) break;
                RECT cur = LerpRect(c.rcFrom, c.rcAll, animE);
                RECT fr = cur; InflateRect(&fr, 2, 2);
                HBRUSH f = CreateSolidBrush(RGB(8, 8, 8));
                FillRect(buf, &fr, f);
                DeleteObject(f);
                DrawSnapshotFit(buf, snap, cur);
                break;
            }
        }
        if (g_dragging && g_dragGhostRect.right > g_dragGhostRect.left)
        {
            HWND h = g_dragHwnd;
            int dw = g_dragGhostRect.right - g_dragGhostRect.left;
            int dh = g_dragGhostRect.bottom - g_dragGhostRect.top;
            int x = g_dragGhostRect.left, y = g_dragGhostRect.top;
            if (!g_dragThumb)
            {
                HBITMAP snap = GetSnapshot(h);
                if (snap)
                {
                    HDC mem = CreateCompatibleDC(buf);
                    HGDIOBJ old = SelectObject(mem, snap);
                    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 200, 0 };
                    AlphaBlend(buf, x, y, dw, dh, mem, 0, 0, snapWidth(snap), snapHeight(snap), bf);
                    SelectObject(mem, old);
                    DeleteDC(mem);
                }
            }
            HPEN gp = CreatePen(PS_SOLID, g_dragThumb ? 2 : 1, RGB(255, 255, 255));
            HGDIOBJ oldP2 = SelectObject(buf, gp);
            HGDIOBJ oldB2 = SelectObject(buf, GetStockObject(NULL_BRUSH));
            Rectangle(buf, x, y, x + dw, y + dh);
            SelectObject(buf, oldP2); SelectObject(buf, oldB2);
            DeleteObject(gp);

            for (int k = 0; k < (int)g_cards.size(); k++)
            {
                RECT fr = CardFullRect(g_cards[k]);
                if (PtInRect(&fr, g_dragPt))
                {
                    HPEN hp = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
                    HGDIOBJ oldP3 = SelectObject(buf, hp);
                    HGDIOBJ oldB3 = SelectObject(buf, GetStockObject(NULL_BRUSH));
                    Rectangle(buf, g_cards[k].rc.left, g_cards[k].rc.top, g_cards[k].rc.right, g_cards[k].rc.bottom);
                    SelectObject(buf, oldP3); SelectObject(buf, oldB3);
                    DeleteObject(hp);
                    break;
                }
            }
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, buf, 0, 0, SRCCOPY);
        SelectObject(buf, oldB);
        DeleteObject(bmp);
        DeleteDC(buf);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSELEAVE:
        KillTimer(hwnd, 2);
        g_hoverCard = -1; g_hoverCell = -1; g_hoverClose = false;
        g_hoverCellClose = false;

        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_TIMER:
        if (wp == 5)
        {

            if (g_openAnimStart && GetTickCount() - g_openAnimStart >= (DWORD)OPEN_ANIM_MS)
            {
                KillTimer(hwnd, 5);
                g_openAnimStart = 0;
                bool wasClose = g_animClose;
                g_animClose = false;
                if (wasClose) FinalizeHide();
                else RegisterThumbs();
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (wp == 2)
        {

            KillTimer(hwnd, 2);
            if (g_hoverCard >= 0 && !g_cards[g_hoverCard].isPlus && !g_hoverClose)
            {
                int d = g_cards[g_hoverCard].desk;
                if (d != g_renderDesk)
                {
                    g_renderDesk = d;
                    Layout(hwnd);
                    RegisterThumbs();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }
        if (wp == 7)
        {

            if (g_dragCell >= 0)
            {
                float target = 1.0f;
                for (int k = 0; k < (int)g_cards.size(); k++)
                {
                    RECT fr = CardFullRect(g_cards[k]);
                    if (PtInRect(&fr, g_dragPt)) { target = 0.45f; break; }
                }
                g_dragScaleT = target;
            }
            g_dragScaleV += (g_dragScaleT - g_dragScale) * 0.5f;
            g_dragScaleV *= 0.68f;
            g_dragScale += g_dragScaleV;
            if (g_dragScale < 0.05f) g_dragScale = 0.05f;
            UpdateDragGhost();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (wp == 11)
        {
            int el = (int)(GetTickCount() - g_structStart);
            if (el >= 280)
            {
                g_structAnim = 0;
                KillTimer(hwnd, 11);
            }
            Layout(hwnd);
            RegisterThumbs();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        return 0;
    case WM_MOUSEWHEEL:
    {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (pt.y < GridTop())
            g_cardScroll -= delta / 3;
        else
            g_gridScroll -= delta;
        Layout(hwnd);
        RegisterThumbs();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        if (g_closeAnim || g_structAnim) return 0;
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

        RECT scr = ScreenRect();
        int nd = (int)g_desktops.size();
        if (nd > 1)
        {
            int content = CardsContentWidth(nd);
            int avail = scr.right - TOP_M * 2;
            if (content > avail)
            {
                int y = TOP_M + LABEL_H + CARD_H + 5;
                int maxScroll = content - avail;
                int thumbW = avail * avail / content;
                if (thumbW < 20) thumbW = 20;
                int thumbX = TOP_M + (int)((double)g_cardScroll / maxScroll * (avail - thumbW));
                RECT rcThumb = { thumbX, y, thumbX + thumbW, y + 4 };
                RECT rcBar = { TOP_M, y, TOP_M + avail, y + 4 };
                if (PtInRect(&rcThumb, pt))
                {
                    g_scrollDrag = true;
                    g_scrollDragOff = pt.x - thumbX;
                    SetCapture(hwnd);
                    return 0;
                }
                if (PtInRect(&rcBar, pt))
                {
                    g_cardScroll = (int)((double)(pt.x - TOP_M - thumbW / 2) / (avail - thumbW) * maxScroll);
                    if (g_cardScroll > maxScroll) g_cardScroll = maxScroll;
                    if (g_cardScroll < 0) g_cardScroll = 0;
                    Layout(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
            }
        }

        for (int i = 0; i < (int)g_cards.size(); i++)
        {
            RECT full = CardFullRect(g_cards[i]);
            if (!PtInRect(&full, pt)) continue;
            KillTimer(hwnd, 2);
            if (g_cards[i].isPlus) { NewDesktop(); return 0; }
            RECT cr = CardCloseRect(g_cards[i]);
            if (PtInRect(&cr, pt))
            {
                CloseDesktop(g_cards[i].desk);
                g_renderDesk = g_current;
                return 0;
            }
            if (g_cards[i].desk == g_current)
                TaskViewHide();
            else
            {
                FinalizeHide();
                SwitchDesktop(g_cards[i].desk);
            }
            g_renderDesk = g_current;
            return 0;
        }

        for (int i = 0; i < (int)g_cells.size(); i++)
        {
            if (!PtInRect(&g_cells[i].rcAll, pt)) continue;
            RECT cr = CellCloseRect(g_cells[i]);
            if (PtInRect(&cr, pt))
            {

                g_switching = true;
                PostMessageW(g_cells[i].hwnd, WM_CLOSE, 0, 0);
                PruneWindows();
                TaskViewRefresh();
                g_switching = false;
                TaskViewRestoreFocus();
                InvalidateRect(hwnd, NULL, TRUE);
                return 0;
            }
            KillTimer(hwnd, 2);
            g_dragCell = i; g_dragHwnd = g_cells[i].hwnd; g_dragging = false;
            g_dragPt = pt;
            g_dragScale = 1.0f; g_dragScaleV = 0.0f; g_dragScaleT = 1.0f;
            SetCapture(hwnd);
            return 0;
        }

        return 0;
    }
    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (g_scrollDrag)
        {
            RECT scr = ScreenRect();
            int nd = (int)g_desktops.size();
            int content = CardsContentWidth(nd);
            int avail = scr.right - TOP_M * 2;
            int maxScroll = content - avail;
            int thumbW = avail * avail / content;
            if (thumbW < 20) thumbW = 20;
            if (maxScroll > 0)
            {
                g_cardScroll = (int)((double)(pt.x - g_scrollDragOff - TOP_M) / (avail - thumbW) * maxScroll);
                if (g_cardScroll > maxScroll) g_cardScroll = maxScroll;
                if (g_cardScroll < 0) g_cardScroll = 0;
                Layout(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        if (g_dragCell >= 0 && GetCapture() == hwnd)
        {
            if (!g_dragging)
            {
                int dx = pt.x - g_dragPt.x, dy = pt.y - g_dragPt.y;
                if (dx * dx + dy * dy > 64)
                {
                    g_dragging = true;
                    g_dragThumb = NULL;
                    for (auto& r : g_reg)
                        if (r.first == g_dragHwnd) { g_dragThumb = r.second; break; }
                    if (g_dragThumb)
                    {
                        for (auto& c : g_cells) if (c.hwnd == g_dragHwnd) c.thumb = NULL;
                    }
                    else
                        UnregisterThumbOf(g_dragHwnd);
                    SetTimer(hwnd, 7, 16, NULL);
                }
            }
            if (g_dragging)
            {
                g_dragPt = pt;
                UpdateDragGhost();
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        int oldC = g_hoverCard, oldCell = g_hoverCell;
        bool oldX = g_hoverClose;
        bool oldCX = g_hoverCellClose;
        g_hoverCard = -1; g_hoverCell = -1; g_hoverClose = false;
        g_hoverCellClose = false;
        for (int i = 0; i < (int)g_cards.size(); i++)
        {
            RECT full = CardFullRect(g_cards[i]);
            if (PtInRect(&full, pt))
            {
                g_hoverCard = i;
                RECT cr = CardCloseRect(g_cards[i]);
                if (PtInRect(&cr, pt)) g_hoverClose = true;
                break;
            }
        }
        for (int i = 0; i < (int)g_cells.size(); i++)
            if (PtInRect(&g_cells[i].rcAll, pt))
            {
                g_hoverCell = i;
                RECT cr = CellCloseRect(g_cells[i]);
                g_hoverCellClose = PtInRect(&cr, pt) ? true : false;
                break;
            }
        if (g_hoverCard != oldC || g_hoverCell != oldCell || g_hoverClose != oldX
            || g_hoverCellClose != oldCX)
            InvalidateRect(hwnd, NULL, FALSE);

        int hoverDesk = -1;
        if (g_hoverCard >= 0 && !g_cards[g_hoverCard].isPlus && !g_hoverClose)
            hoverDesk = g_cards[g_hoverCard].desk;
        if (g_hoverCard != oldC || g_dragging) KillTimer(hwnd, 2);
        if (!g_dragging && g_hoverCard != oldC && hoverDesk >= 0 && hoverDesk != g_renderDesk)
            SetTimer(hwnd, 2, 500, NULL);
        TRACKMOUSEEVENT te = { sizeof(te), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&te);
        return 0;
    }
    case WM_LBUTTONUP:
    {
        if (g_closeAnim) return 0;
        if (g_scrollDrag) { g_scrollDrag = false; ReleaseCapture(); return 0; }
        if (g_dragCell >= 0)
        {
            HWND h = g_dragHwnd;
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (g_dragging) g_dragPt = pt;
            bool wasDragging = g_dragging;
            g_dragCell = -1; g_dragging = false;
            KillTimer(hwnd, 7);
            ReleaseCapture();
            if (wasDragging)
            {
                g_dragThumb = NULL;
                RegisterThumbs();

                for (int k = 0; k < (int)g_cards.size(); k++)
                {
                    RECT fr = CardFullRect(g_cards[k]);
                    if (!PtInRect(&fr, g_dragPt)) continue;
                    int target = g_cards[k].isPlus ? NewDesktop() : g_cards[k].desk;
                    int previewDesk = g_renderDesk;
                    g_switching = true;
                    SendWindowToDesktop(h, target);
                    g_switching = false;
                    g_renderDesk = previewDesk;
                    Layout(hwnd);
                    RegisterThumbs();
                    TaskViewRestoreFocus();
                    InvalidateRect(hwnd, NULL, FALSE);
                    return 0;
                }
                Layout(hwnd);
                RegisterThumbs();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
            if (!IsWindow(h)) return 0;
            KillTimer(hwnd, 2);
            {
                auto it = g_winDesk.find(h);
                int desk = (it != g_winDesk.end()) ? it->second : g_current;
                bool wasIconic = (IsIconic(h) != 0);
                HBITMAP snap = GetSnapshot(h);
                if (!snap && wasIconic)
                {
                    SnapshotForCell(h, true);
                    if (desk != g_current) ShowWindow(h, SW_HIDE);
                    snap = GetSnapshot(h);
                }
                else if (!snap && IsWindowVisible(h) && desk == g_current)
                {
                    UpdateSnapshot(h);
                    snap = GetSnapshot(h);
                }
                RECT rcFrom = { 0, 0, 0, 0 };
                for (auto& c : g_cells)
                    if (c.hwnd == h) { rcFrom = c.rcAll; break; }
                if (desk == g_current && wasIconic)
                    ShowWindow(h, SW_RESTORE);
                FinalizeHide();
                if (desk != g_current)
                    SwitchDesktopEx(desk, false, true);
                if (wasIconic && desk != g_current)
                    ShowWindow(h, SW_RESTORE);
                RECT rcTo = { 0, 0, 0, 0 };
                GetWindowRect(h, &rcTo);
                ShowWindow(h, SW_HIDE);
                if (snap && rcTo.right > rcTo.left && rcTo.bottom > rcTo.top
                    && rcFrom.right > rcFrom.left)
                    FlyStart(h, snap, rcFrom, rcTo);
                else
                {
                    g_switching = true;
                    ShowWindow(h, wasIconic ? SW_RESTORE : SW_SHOW);
                    SetForegroundWindow(h);
                    SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                    g_switching = false;
                }
            }
            return 0;
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        KillTimer(hwnd, 7);
        g_dragThumb = NULL;
        g_dragCell = -1; g_dragging = false;
        RegisterThumbs();
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_RBUTTONDOWN: TaskViewHide(); return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) TaskViewHide();
        return 0;
    case WM_ACTIVATE:
        return 0;
    case WM_DESTROY:
        UnregisterThumbs();
        g_tv = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool TaskViewEnsureClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    if (GetClassInfoExW(hInst, TV_CLASS, &wc)) return true;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TV_WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = TV_CLASS;
    return RegisterClassExW(&wc) != 0;
}
