#include "common.h"

static const wchar_t SLIDE_CLASS[] = L"MDesktop_Slide";
static const UINT   SLIDE_DONE = WM_APP + 9;

static HWND  g_slideWnd = NULL;

static struct {
    HBITMAP a;
    HDC     ma;
    bool    fwd;
    int     frame, frames;
    int     w, h;
    int     x0, y0;
    bool    ok;
} g_st;

HBITMAP CaptureScreen()
{
    HDC s = GetDC(NULL);
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    RECT wa = {};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0))
    {
        int deskBottom = wa.bottom - y;
        if (deskBottom > 0 && deskBottom < h) h = deskBottom;
    }
    HBITMAP bmp = CreateCompatibleBitmap(s, w, h);
    HDC mem = CreateCompatibleDC(s);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, s, x, y, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(NULL, s);
    return bmp;
}

static LRESULT CALLBACK SlideProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
    {

        if (!g_st.ok || !g_st.ma) { ValidateRect(hwnd, NULL); return 0; }
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        BitBlt(hdc, 0, 0, g_st.w, g_st.h, g_st.ma, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
    {

        g_st.frame++;
        if (g_st.frame >= g_st.frames)
        {
            KillTimer(hwnd, 1);
            PostMessageW(hwnd, SLIDE_DONE, 0, 0);
            return 0;
        }
        float t = (float)g_st.frame / g_st.frames;
        float e = t * t * (3 - 2 * t);
        int off = (int)((g_st.w - 1) * e);
        int x = g_st.x0 + (g_st.fwd ? -off : off);
        SetWindowPos(hwnd, NULL, x, g_st.y0, 0, 0,
                     SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void FreeAnimResources()
{
    if (g_st.ma) { DeleteDC(g_st.ma); g_st.ma = NULL; }
    if (g_st.a) { DeleteObject(g_st.a); g_st.a = NULL; }
    g_st.ok = false;
}

void SlideFreeze(HBITMAP capOld)
{
    static bool classRegistered = false;
    g_st.ok = false;
    if (!capOld) return;
    g_st.w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_st.h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (g_st.w <= 0 || g_st.h <= 0) { DeleteObject(capOld); return; }

    if (!classRegistered)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = SlideProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(NULL, IDC_WAIT);
        wc.lpszClassName = SLIDE_CLASS;
        if (!RegisterClassExW(&wc)) { DeleteObject(capOld); return; }
        classRegistered = true;
    }

    HDC screen = GetDC(NULL);
    g_st.ma = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!g_st.ma) { DeleteObject(capOld); return; }
    g_st.a = capOld;
    SelectObject(g_st.ma, g_st.a);

    g_slideWnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                 SLIDE_CLASS, L"", WS_POPUP,
                                 GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                                 g_st.w, g_st.h, NULL, NULL, g_hInst, NULL);
    if (!g_slideWnd) { FreeAnimResources(); return; }
    HDC wdc = GetDC(g_slideWnd);
    BitBlt(wdc, 0, 0, g_st.w, g_st.h, g_st.ma, 0, 0, SRCCOPY);
    ReleaseDC(g_slideWnd, wdc);
    ValidateRect(g_slideWnd, NULL);
    ShowWindow(g_slideWnd, SW_SHOWNA);
    g_st.ok = true;
}

void SlideRun(bool forward)
{
    if (!g_slideWnd || !g_st.ok)
    {
        FreeAnimResources();
        if (g_slideWnd) { DestroyWindow(g_slideWnd); g_slideWnd = NULL; }
        return;
    }
    g_st.fwd = forward;
    g_st.frame = 0; g_st.frames = 14;
    g_st.x0 = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_st.y0 = GetSystemMetrics(SM_YVIRTUALSCREEN);

    SetTimer(g_slideWnd, 1, 15, NULL);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0))
    {
        if (m.message == SLIDE_DONE) break;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    KillTimer(g_slideWnd, 1);
    DestroyWindow(g_slideWnd);
    g_slideWnd = NULL;
    FreeAnimResources();
}

static const wchar_t FLY_CLASS[] = L"MDesktop_Fly";

static struct
{
    HWND    wnd;
    HWND    target;
    HBITMAP snap;
    RECT    rcFrom, rcTo, rcCur;
    DWORD   start;
} g_fly;

static RECT FlyLerp(const RECT& a, const RECT& b, float e)
{
    RECT r;
    r.left   = a.left   + (LONG)((b.left   - a.left)   * e);
    r.top    = a.top    + (LONG)((b.top    - a.top)    * e);
    r.right  = a.right  + (LONG)((b.right  - a.right)  * e);
    r.bottom = a.bottom + (LONG)((b.bottom - a.bottom) * e);
    return r;
}

static void FlyEnd(HWND fw)
{
    KillTimer(fw, 1);
    HWND t = g_fly.target;
    g_fly.wnd = NULL;
    g_fly.snap = NULL;
    g_fly.target = NULL;
    DestroyWindow(fw);
    if (t && IsWindow(t))
    {
        auto it = g_winDesk.find(t);
        if (it == g_winDesk.end() || it->second == g_current)
        {
            ShowWindow(t, SW_SHOW);
            SetForegroundWindow(t);
            SetWindowPos(t, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
}

static LRESULT CALLBACK FlyProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TIMER)
    {
        const int DELAY = 150;
        const int DUR = 300;
        int t = (int)(GetTickCount() - g_fly.start) - DELAY;
        if (t < 0)
        {
            SetWindowRgn(hwnd, CreateRectRgn(0, 0, 0, 0), FALSE);
            return 0;
        }
        if (t >= DUR) { FlyEnd(hwnd); return 0; }
        float x = (float)t / DUR;
        float e = x * x * (3 - 2 * x);
        g_fly.rcCur = FlyLerp(g_fly.rcFrom, g_fly.rcTo, e);
        SetWindowRgn(hwnd, CreateRectRgnIndirect(&g_fly.rcCur), TRUE);
        return 0;
    }
    if (msg == WM_ERASEBKGND) return 1;
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        BITMAP bm;
        if (g_fly.snap && GetObjectW(g_fly.snap, sizeof(bm), &bm) && bm.bmWidth > 0)
        {
            int w = g_fly.rcCur.right - g_fly.rcCur.left;
            int h = g_fly.rcCur.bottom - g_fly.rcCur.top;
            if (w > 4 && h > 4)
            {
                HDC mem = CreateCompatibleDC(hdc);
                HGDIOBJ old = SelectObject(mem, g_fly.snap);
                SetStretchBltMode(hdc, HALFTONE);
                SetBrushOrgEx(hdc, 0, 0, NULL);
                StretchBlt(hdc, g_fly.rcCur.left, g_fly.rcCur.top, w, h,
                           mem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
                SelectObject(mem, old);
                DeleteDC(mem);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void FlyStart(HWND target, HBITMAP snap, RECT rcFrom, RECT rcTo)
{
    if (g_fly.wnd) return;
    static bool reg = false;
    if (!reg)
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = FlyProc;
        wc.hInstance = g_hInst;
        wc.lpszClassName = FLY_CLASS;
        if (!RegisterClassExW(&wc)) return;
        reg = true;
    }
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    OffsetRect(&rcFrom, -vx, -vy);
    OffsetRect(&rcTo, -vx, -vy);
    g_fly.target = target;
    g_fly.snap = snap;
    g_fly.rcFrom = rcFrom;
    g_fly.rcTo = rcTo;
    g_fly.rcCur = rcFrom;
    g_fly.start = GetTickCount();
    g_fly.wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                FLY_CLASS, L"", WS_POPUP, vx, vy, vw, vh,
                                NULL, NULL, g_hInst, NULL);
    if (!g_fly.wnd)
    {
        g_fly.snap = NULL;
        g_fly.target = NULL;
        ShowWindow(target, SW_SHOW);
        SetForegroundWindow(target);
        return;
    }
    SetWindowRgn(g_fly.wnd, CreateRectRgnIndirect(&rcFrom), FALSE);
    ShowWindow(g_fly.wnd, SW_SHOWNA);
    SetTimer(g_fly.wnd, 1, 15, NULL);
}
