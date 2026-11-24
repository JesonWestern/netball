// src/tray.cpp —— T4 托盘图标 + 右键菜单（属主 builder-ui-1）
// 只依赖 Win32：shell32(Shell_NotifyIconW) + comctl32(SetWindowSubclass)。
//
// 【已知坑 / 维护须知】—— 改动前先读完
//  1. TrackPopupMenu 三连坑（缺一个就会出现「菜单关不掉 / 点别处不消失 / 点完卡住」）：
//     弹菜单前 SetForegroundWindow(hwnd) → 用 TPM_RETURNCMD 拿返回值 →
//     弹完必须 PostMessage(hwnd, WM_NULL, 0, 0)。顺序和 WM_NULL 都不能省。
//  2. 菜单命令统一 SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(id,0), 0) 交回宿主：
//     本模块不自己退出进程、不自己开关浮窗，避免和 ball_window 的状态打架。
//  3. TaskbarCreated 只能重新 NIM_ADD（Explorer 重启后旧图标已不存在，NIM_MODIFY 救不回来）。
//  4. updateTip 的 NIM_MODIFY 失败时视为图标丢失，直接重加一次；szTip 上限 128 wchar，超长必须截断。
//  5. 图标用 LoadImageW(..., LR_SHARED)，不需要也不允许 DestroyIcon；资源缺失时兜底 IDI_APPLICATION。
//  6. 消息归属：本文件用 SetWindowSubclass 接管 WM_APP+100，宿主不得重复转发（见 tray.h）。
#include "tray.h"

#include <shellapi.h>
#include <commctrl.h>
#include <cstdio>
#include <cwchar>

namespace {

const UINT_PTR kSubclassId = 0x4E42;   // 'NB'
const UINT      kIconId     = 1;

HWND      g_ball          = NULL;
HINSTANCE g_inst          = NULL;
UINT      g_taskbarCreated = 0;
bool      g_added         = false;
bool      g_subclassed    = false;
wchar_t   g_tip[128]      = NETBALL_APP_NAME;
bool      g_showBall      = true;
bool      g_taskbarShow   = false;

// ---------------------------------------------------------------- 图标
HICON LoadTrayIcon(HINSTANCE inst) {
    HICON ic = NULL;
    if (inst) {
        int cx = GetSystemMetrics(SM_CXSMICON);
        int cy = GetSystemMetrics(SM_CYSMICON);
        if (cx <= 0) cx = 16;
        if (cy <= 0) cy = 16;
        ic = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, cx, cy, LR_SHARED);
        if (!ic) {
            ic = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                   0, 0, LR_DEFAULTSIZE | LR_SHARED);
        }
    }
    if (!ic) ic = LoadIconW(NULL, IDI_APPLICATION);
    return ic;
}

// ---------------------------------------------------------------- 速度格式化（自带，不依赖 render）
void FormatSpeed(double bps, wchar_t* out, size_t cch) {
    if (!(bps > 0.0)) bps = 0.0;
    if (bps >= 1024.0 * 1024.0) {
        _snwprintf(out, cch, L"%.2f MB/s", bps / (1024.0 * 1024.0));
    } else if (bps >= 1024.0) {
        _snwprintf(out, cch, L"%.0f KB/s", bps / 1024.0);
    } else {
        _snwprintf(out, cch, L"%.0f B/s", bps);
    }
    out[cch - 1] = 0;
}

// ---------------------------------------------------------------- 菜单
void ShowTrayMenu(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;

    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS,   L"设置(&S)...");
    AppendMenuW(menu, MF_STRING | (g_showBall ? MF_CHECKED : MF_UNCHECKED),
                IDM_TRAY_SHOW, L"\u663e\u793a\u60ac\u6d6e\u7403(&H)");                 // 显示悬浮球
    AppendMenuW(menu, MF_STRING | (g_taskbarShow ? MF_CHECKED : MF_UNCHECKED),
                IDM_TASKBAR, L"\u5728\u4efb\u52a1\u680f\u663e\u793a(&K)");              // 在任务栏显示
    // 重置位置：与球窗口右键菜单同一命令 ID，托盘也统一以 WM_COMMAND 交回宿主分发，
    // 保证「球被拖丢后」有恢复入口（单实例机制下重跑一次不会复位）。
    AppendMenuW(menu, MF_STRING, IDM_RESETPOS,   L"\u91cd\u7f6e\u4f4d\u7f6e(&R)");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT,       L"退出(&X)");
    SetMenuDefaultItem(menu, IDM_SETTINGS, FALSE);

    POINT pt;
    GetCursorPos(&pt);

    // Win32 经典坑：先 SetForegroundWindow，结束后必须 PostMessage(WM_NULL)
    SetForegroundWindow(hwnd);
    int cmd = (int)TrackPopupMenu(menu,
                                  TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                                  pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    if (cmd > 0) {
        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
    }
}

// ---------------------------------------------------------------- 子类化（自动接管托盘消息）
LRESULT CALLBACK TraySubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                  UINT_PTR id, DWORD_PTR refData) {
    (void)id;
    (void)refData;
    if (msg == NETBALL_TRAY_MSG) {
        tray::onTrayMessage(hwnd, wParam, lParam);
        return 0;
    }
    if (g_taskbarCreated != 0 && msg == g_taskbarCreated) {
        tray::onTaskbarCreated();
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

} // namespace

// ============================================================================
// 契约实现：tray
// ============================================================================
bool tray::add(HWND ball, HINSTANCE hInst) {
    if (!ball || !IsWindow(ball)) return false;

    g_ball = ball;
    g_inst = hInst ? hInst : (HINSTANCE)GetModuleHandleW(NULL);
    if (g_taskbarCreated == 0) {
        g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    }

    if (!g_subclassed) {
        if (SetWindowSubclass(ball, TraySubclassProc, kSubclassId, 0)) {
            g_subclassed = true;
        }
    }

    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = ball;
    nid.uID = kIconId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = NETBALL_TRAY_MSG;
    nid.hIcon = LoadTrayIcon(g_inst);
    lstrcpynW(nid.szTip, g_tip, ARRAYSIZE(nid.szTip));

    BOOL ok = Shell_NotifyIconW(NIM_ADD, &nid);
    if (!ok) {
        // Explorer 尚未就绪时重试一次
        Sleep(120);
        ok = Shell_NotifyIconW(NIM_ADD, &nid);
    }
    if (!ok) {
        nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
        ok = Shell_NotifyIconW(NIM_ADD, &nid);
    }

    g_added = (ok != FALSE);
    return g_added;
}

void tray::remove() {
    // 不能用 IsWindow(g_ball) 做门槛：退出流程里悬浮球可能已被销毁，
    // 但 shell 仍持有该 hwnd 注册的图标，必须按注册时的 hwnd/uID 发送 NIM_DELETE。
    if (g_added && g_ball) {
        NOTIFYICONDATAW nid;
        ZeroMemory(&nid, sizeof(nid));
        nid.cbSize = sizeof(nid);
        nid.hWnd = g_ball;
        nid.uID = kIconId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }
    g_added = false;

    if (g_subclassed && g_ball && IsWindow(g_ball)) {
        RemoveWindowSubclass(g_ball, TraySubclassProc, kSubclassId);
    }
    g_subclassed = false;
    g_ball = NULL;
}

void tray::updateTip(const Metrics* m) {
    if (!g_ball || !IsWindow(g_ball)) return;

    if (!m) {
        lstrcpynW(g_tip, NETBALL_APP_NAME, ARRAYSIZE(g_tip));
    } else {
        wchar_t down[32];
        wchar_t up[32];
        FormatSpeed(m->downBps, down, 32);
        FormatSpeed(m->upBps, up, 32);
        int mem = m->memPercent;
        if (mem < 0) mem = 0;
        if (mem > 100) mem = 100;
        if (m->netValid) {
            _snwprintf(g_tip, ARRAYSIZE(g_tip),
                       L"\u2193 %ls   \u2191 %ls   \u5185\u5b58 %d%%", down, up, mem);
        } else {
            _snwprintf(g_tip, ARRAYSIZE(g_tip),
                       L"\u2193 --   \u2191 --   \u5185\u5b58 %d%%", mem);
        }
        g_tip[ARRAYSIZE(g_tip) - 1] = 0;
    }
    tray::updateTipText(g_tip);
}

// v1.4.3：多行提示（网速 + 内存 + CPU/GPU 温度功率），由宿主拼好后送进来。
// szTip 上限 128 wchar，这里统一截断，避免超长文本被 shell 直接丢弃。
void tray::updateTipText(const wchar_t* text) {
    if (!g_ball || !IsWindow(g_ball)) return;

    if (text && text[0]) {
        lstrcpynW(g_tip, text, ARRAYSIZE(g_tip));
    } else {
        lstrcpynW(g_tip, NETBALL_APP_NAME, ARRAYSIZE(g_tip));
    }
    g_tip[ARRAYSIZE(g_tip) - 1] = 0;

    if (!g_added) return;

    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_ball;
    nid.uID = kIconId;
    nid.uFlags = NIF_TIP;
    lstrcpynW(nid.szTip, g_tip, ARRAYSIZE(nid.szTip));
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid)) {
        // 图标可能因 Explorer 重启丢失，重新添加
        tray::add(g_ball, g_inst);
    }
}

void tray::setMenuState(bool showBall, bool taskbarShow) {
    g_showBall    = showBall;
    g_taskbarShow = taskbarShow;
}

void tray::onTrayMessage(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    (void)wParam;
    UINT evt = LOWORD(lParam);
    switch (evt) {
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        ShowTrayMenu(hwnd);
        break;
    default:
        break;
    }
}

void tray::onTaskbarCreated() {
    if (!g_ball || !IsWindow(g_ball)) return;
    g_added = false;
    tray::add(g_ball, g_inst);
}
