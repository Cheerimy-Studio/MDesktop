#ifndef COMMON_H
#define COMMON_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <map>
#include <vector>
#include <string>
#include <cstdio>
#include <cwchar>

using std::map;
using std::vector;
using std::wstring;

struct DesktopInfo {
    wstring name;
};

extern vector<DesktopInfo> g_desktops;
extern map<HWND, int>      g_winDesk;
extern int                 g_current;
extern bool                g_switching;
extern HINSTANCE           g_hInst;
extern HWND                g_hMsgWnd;

wstring ConfigDir();
wstring IniPath();
wstring DesktopName(int idx);

bool  ManageableWindow(HWND h);
void  AssignInitialWindows();
int   NewDesktop();
bool  CloseDesktop(int idx);
bool  SwitchDesktop(int idx);
bool  SwitchDesktopEx(int idx, bool keepTaskView, bool animate);
void  MoveWindowToDesktop(HWND h, int idx);
void  SendWindowToDesktop(HWND h, int idx);
void  PruneWindows();
void  RestoreAllWindows();
void  MergeAllWindowsToCurrent();
void  SaveState();
void  LoadState();
void  WriteRecoveryFile();
void  RecoverHiddenWindows();
void  ClearRecoveryFile();

void  TaskViewToggle();
void  TaskViewShow();
void  TaskViewHide();
bool  TaskViewVisible();
void  TaskViewRestoreFocus();
void  TaskViewRefresh();

void        UpdateSnapshot(HWND h);
void        DiscardSnapshot(HWND h);
HBITMAP     GetSnapshot(HWND h);
void        SnapshotInitAll();

HBITMAP CaptureScreen();
void    SlideFreeze(HBITMAP capOld);
void    SlideRun(bool forward);

void    FlyStart(HWND target, HBITMAP snap, RECT rcFrom, RECT rcTo);

void  ShowTrayMenu();
#endif
