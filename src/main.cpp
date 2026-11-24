// src/main.cpp —— NetBall 网速悬浮球 程序入口与主控（属主 builder-core-1）
//
// 职责：
//   单实例互斥 · GDI+ 生命周期 · 配置加载 · 建浮窗 · 托盘 · 采样定时器
//   · WM_COMMAND(IDM_*) 全部业务命令 · 退出清理
//
// 消息分工（重要）：
//   悬浮球窗口（ball_window.cpp）负责拖动/右键菜单/绘制，右键菜单结果通过
//   CommandFn 回调到这里；托盘菜单（tray.cpp 用 SetWindowSubclass 挂在球窗口上）
//   会把 IDM_* 以 WM_COMMAND 发到球窗口，再由球窗口转发过来。
//   采样定时器挂在本文件的隐藏消息窗口上，避免和浮窗的绘制逻辑耦合。
#include "app_common.h"
#include "ball_window.h"
#include "glass.h"
#include "layout_model.h"
#include "render.h"
#include "sensors.h"
#include "text_render.h"

#include <objidl.h>
#include <gdiplus.h>

#include <cstring>

namespace {

const wchar_t* kMainMsgClass = L"NetBallMainMsgWnd";

AppConfig   g_cfg       = {};
HINSTANCE   g_inst      = nullptr;
HWND        g_msgWnd    = nullptr;
HANDLE      g_mutex     = nullptr;
ULONG_PTR   g_gdiToken  = 0;
bool        g_gdiReady  = false;
bool        g_exiting   = false;

// ------------------------------------------------------------------ DPI

void EnablePerMonitorV2()
{
    typedef BOOL (WINAPI* SetCtxFn)(DPI_AWARENESS_CONTEXT);
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (!u32) return;
    SetCtxFn fn = (SetCtxFn)(void*)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    // manifest 里已声明 PerMonitorV2；重复设置会返回 FALSE，属正常
    if (fn) fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

// ------------------------------------------------------------------ 已有实例定位

BOOL CALLBACK FindBallProc(HWND hwnd, LPARAM lp)
{
    wchar_t cls[64] = {0};
    if (GetClassNameW(hwnd, cls, 63) > 0 && wcscmp(cls, WND_CLASS_BALL) == 0) {
        *(HWND*)lp = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND FindExistingBall()
{
    HWND h = FindWindowW(WND_CLASS_BALL, nullptr);
    if (h) return h;
    // 兜底：某些环境下 FindWindow 找不到 tool window，改为枚举顶层窗口
    HWND found = nullptr;
    EnumWindows(FindBallProc, (LPARAM)&found);
    return found;
}

// ------------------------------------------------------------------ 位置同步

void SyncPosFromWindow()
{
    HWND h = ballwin::hwnd();
    if (h) {
        RECT r;
        if (GetWindowRect(h, &r)) {
            g_cfg.posX = r.left;
            g_cfg.posY = r.top;
        }
    }
    g_cfg.savedScreenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_cfg.savedScreenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

// ------------------------------------------------------------------ 采样

// 常驻内存兜底（Leader 裁决：只允许启动后单次调用）：
// 让系统回收工作集里可回收的页，只影响驻留页、不改变提交量（私有提交约 3MB）。
// 之后靠"离屏缓冲复用 + 数值不变不重绘"把 WS 稳定在这个浅水位。
void TrimWorkingSetOnce()
{
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

// 托盘悬停提示：一次显示全部指标（多行）。shell 的 szTip 上限 128 wchar，
// 放不下的整行直接丢弃并补省略号，避免出现半行数字。
void AppendTipLine(wchar_t* out, size_t cch, bool* truncated, const wchar_t* line)
{
    if (!out || cch == 0 || !line || !line[0]) return;
    const int used = lstrlenW(out);
    const int need = lstrlenW(line) + (used > 0 ? 1 : 0);
    if ((size_t)(used + need) + 1 > cch) {
        if (truncated) *truncated = true;
        return;
    }
    if (used > 0) lstrcatW(out, L"\n");
    lstrcatW(out, line);
}

void BuildTrayTip(const Metrics& m, wchar_t* out, size_t cch)
{
    if (!out || cch == 0) return;
    out[0] = 0;

    wchar_t item[96];
    wchar_t line[320];
    bool truncated = false;

    auto itemOf = [&](int id) -> const wchar_t* {
        layout_model::itemText(id, &g_cfg, &m, item, 96);
        return item;
    };

    // 1) 下行 / 上行
    line[0] = 0;
    lstrcpynW(line, itemOf(layout_model::ITEM_DOWN), 320);
    lstrcatW(line, L"   ");
    lstrcatW(line, itemOf(layout_model::ITEM_UP));
    AppendTipLine(out, cch, &truncated, line);

    // 2) 内存 / CPU 负载
    line[0] = 0;
    lstrcpynW(line, itemOf(layout_model::ITEM_MEM), 320);
    lstrcatW(line, L"   ");
    lstrcatW(line, itemOf(layout_model::ITEM_CPULOAD));
    AppendTipLine(out, cch, &truncated, line);

    // 3) CPU 温度 / CPU 功率
    line[0] = 0;
    lstrcpynW(line, itemOf(layout_model::ITEM_CPUTEMP), 320);
    lstrcatW(line, L"   ");
    lstrcatW(line, itemOf(layout_model::ITEM_CPUPWR));
    AppendTipLine(out, cch, &truncated, line);

    // 4) 每张显卡一行：负载 / 温度 / 功率（只列实际探测到的卡）
    for (int i = 0; i < m.gpuCount && i < NETBALL_MAX_GPU; ++i) {
        line[0] = 0;
        lstrcpynW(line, itemOf(layout_model::ITEM_GPU0LOAD + i * 2), 320);
        lstrcatW(line, L"   ");
        lstrcatW(line, itemOf(layout_model::ITEM_GPU0TEMP + i * 2));
        lstrcatW(line, L"   ");
        lstrcatW(line, itemOf(layout_model::ITEM_GPU0PWR + i));
        AppendTipLine(out, cch, &truncated, line);
    }

    if (truncated) {
        const int used = lstrlenW(out);
        if ((size_t)used + 2 <= cch) lstrcatW(out, L"\u2026");
    }
}

void SampleOnce()
{
    Metrics m;
    ZeroMemory(&m, sizeof(m));
    metrics::sample(&m);
    ballwin::setMetrics(&m);
    taskbar::setMetrics(&m);

    wchar_t tip[128];
    BuildTrayTip(m, tip, 120);
    tray::updateTipText(tip);
}

// 液态玻璃流光：独立动画定时器（约 30fps），只在有液态玻璃表面时开启。
// 它不参与采样/数字刷新，因此数值变化不会打断流动，流动也不会拖慢采样。
void SyncAnimTimer()
{
    if (!g_msgWnd) return;

    // 只在状态切换时动 timer：设置窗口拖滑块会 16ms 一次调用预览，
    // 每次都 Kill/Set 会把动画帧不断推迟。
    static bool s_running = false;
    const bool need = (g_cfg.showBall && g_cfg.ballMat == glass::MAT_LIQUID) ||
                      (g_cfg.taskbarShow && g_cfg.tbMat == glass::MAT_LIQUID);
    if (need == s_running) return;

    KillTimer(g_msgWnd, TIMER_LIQUID);
    if (need) SetTimer(g_msgWnd, TIMER_LIQUID, 33, nullptr);
    s_running = need;
}

void RestartTimer()
{
    if (!g_msgWnd) return;
    KillTimer(g_msgWnd, TIMER_SAMPLE);
    UINT ms = (UINT)g_cfg.refreshMs;
    if (ms < 500)  ms = 500;
    if (ms > 3000) ms = 3000;
    SetTimer(g_msgWnd, TIMER_SAMPLE, ms, nullptr);

    // 任务栏位置/自动隐藏/全屏状态需要比采样更密地跟踪；250ms 只做几何查询，
    // 文本没变时 taskbar::sync() 内部不会重绘。
    KillTimer(g_msgWnd, TIMER_TASKBAR);
    SetTimer(g_msgWnd, TIMER_TASKBAR, 250, nullptr);

    // 自绘材质的实时背景：500ms 重抓一次（不隐藏窗口，不会断触/闪烁）。
    // 纯色/无底板/系统亚克力/隐藏状态由两个 refreshMaterial 内部直接跳过。
    KillTimer(g_msgWnd, TIMER_GLASS);
    SetTimer(g_msgWnd, TIMER_GLASS, 500, nullptr);

    SyncAnimTimer();
}

// ------------------------------------------------------------------ 配置应用

void ApplyNow(bool persist)
{
    // 显示时先在隐藏状态下把材质画好再 Show，避免露出未绘制的一帧；
    // 隐藏时先藏起来，applyConfig 内部的 PaintNow 会直接跳过抓屏。
    HWND ball = ballwin::hwnd();
    if (ball && IsWindow(ball) && !g_cfg.showBall) {
        ShowWindow(ball, SW_HIDE);
    }
    sensors::setConfig(&g_cfg);   // 温度数据源 / 采样开关变化
    ballwin::applyConfig(&g_cfg);
    ball = ballwin::hwnd();
    if (ball && IsWindow(ball)) {
        ShowWindow(ball, g_cfg.showBall ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
    taskbar::applyConfig(&g_cfg);
    tray::setMenuState(g_cfg.showBall != FALSE, g_cfg.taskbarShow != FALSE);
    RestartTimer();
    SampleOnce();                 // 配置变化后立即刷新一帧（含新指标）
    SyncPosFromWindow();          // applyConfig 可能纠正过越界位置
    if (persist) cfg::save(&g_cfg);
}

void OnSettingsApplied(const AppConfig* c, void* /*user*/)
{
    if (!c) return;
    g_cfg = *c;
    ApplyNow(true);
}

// 设置窗口实时预览：只把工作副本应用到两个表面，不写盘、不重启定时器、不采样。
// 拖动滑块时会被 16ms 合并后高频调用，因此这里必须保持轻量。
void OnSettingsPreview(const AppConfig* c, void* /*user*/)
{
    if (!c) return;
    g_cfg = *c;
    sensors::setConfig(&g_cfg);     // 温度源/显示项可能变了，只更新开关，不触发采样

    HWND ball = ballwin::hwnd();
    if (ball && IsWindow(ball) && !g_cfg.showBall) {
        ShowWindow(ball, SW_HIDE);
    }
    ballwin::applyConfig(&g_cfg);
    ball = ballwin::hwnd();
    if (ball && IsWindow(ball)) {
        ShowWindow(ball, g_cfg.showBall ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
    taskbar::applyConfig(&g_cfg);

    // 材质可能在预览中被切到/切走液态玻璃，动画定时器要跟着开关（只改 timer，不采样）。
    SyncAnimTimer();
}

// ------------------------------------------------------------------ 命令处理

void HandleCommand(int id)
{
    switch (id) {
    case IDM_SETTINGS:
        settings_ui::open(ballwin::hwnd(), g_inst, &g_cfg,
                          OnSettingsPreview, OnSettingsApplied, nullptr);
        break;

    case IDM_TOPMOST:
        g_cfg.alwaysOnTop = g_cfg.alwaysOnTop ? FALSE : TRUE;
        ApplyNow(true);
        break;

    case IDM_TASKBAR:
        g_cfg.taskbarShow = g_cfg.taskbarShow ? FALSE : TRUE;
        ApplyNow(true);
        break;

    case IDM_AUTOSTART:
        g_cfg.autoStart = g_cfg.autoStart ? FALSE : TRUE;
        cfg::setAutoStart(g_cfg.autoStart != FALSE);
        ApplyNow(true);
        break;

    case IDM_RESETPOS:
        g_cfg.posX = -1;
        g_cfg.posY = -1;
        ApplyNow(true);
        break;

    case IDM_ABOUT: {
        wchar_t text[512];
        _snwprintf(text, 511,
                   L"%ls\n\n\u7248\u672c %ls\n\n"
                   L"\u5e38\u9a7b\u5185\u5b58 \u2264 15MB\u3001CPU \u2264 0.5%%\u3001exe \u2264 2MB\n"
                   L"\u5de6\u952e\u62d6\u52a8\u3001\u53f3\u952e\u83dc\u5355\u3002",
                   NETBALL_APP_NAME, NETBALL_VERSION_STR);
        text[511] = 0;
        MessageBoxW(ballwin::hwnd(), text, NETBALL_APP_NAME, MB_OK | MB_ICONINFORMATION);
        break;
    }

    case IDM_TRAY_SHOW: {
        // 持久化「显示悬浮球」：隐藏球后仍可用托盘/任务栏文字，重启后保持
        g_cfg.showBall = IsWindowVisible(ballwin::hwnd()) ? FALSE : TRUE;
        HWND h = ballwin::hwnd();
        if (h && !g_cfg.showBall) ShowWindow(h, SW_HIDE);
        // 同步 ball_window 内部的 g_cfg.showBall，否则下一次数值刷新会把
        // 系统亚克力底窗重新显示出来（文字层已隐藏，只剩一块胶囊底色）。
        // 显示时先在隐藏状态下画好材质，再 Show，避免露出空白帧。
        ballwin::applyConfig(&g_cfg);
        h = ballwin::hwnd();
        if (h) ShowWindow(h, g_cfg.showBall ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (!g_cfg.showBall) glass::hideSystemAcrylic(1);
        tray::setMenuState(g_cfg.showBall != FALSE, g_cfg.taskbarShow != FALSE);
        cfg::save(&g_cfg);
        break;
    }

    case IDM_EXIT:
    case IDM_TRAY_EXIT:
        g_exiting = true;
        // 必须先取窗口位置再销毁：窗口没了就读不到 rect，位置记忆会保存成旧值
        SyncPosFromWindow();
        cfg::save(&g_cfg);
        // 先回收托盘图标：NIM_DELETE 的宿主窗口是悬浮球，销毁后 shell 不会再受理删除，
        // 会留下幽灵图标直到鼠标划过。Cleanup() 里的 tray::remove() 此时已是空操作。
        tray::remove();
        ballwin::destroy();       // 球窗口销毁后 PostQuitMessage，主循环退出
        break;

    default:
        break;
    }
}

// ------------------------------------------------------------------ 隐藏消息窗口

LRESULT CALLBACK MsgWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:
        if (wp == TIMER_SAMPLE) {
            SampleOnce();
            return 0;
        }
        if (wp == TIMER_TASKBAR) {
            taskbar::sync();
            return 0;
        }
        if (wp == TIMER_GLASS) {
            ballwin::refreshMaterial();
            taskbar::refreshMaterial();
            return 0;
        }
        if (wp == TIMER_LIQUID) {
            ballwin::animateLiquid();
            taskbar::animateLiquid();
            return 0;
        }
        break;

    case WM_NETBALL_POSCHANGED:
        SyncPosFromWindow();
        cfg::save(&g_cfg);        // 拖动结束后立即落盘
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND CreateMsgWindow(HINSTANCE hInst)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MsgWndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = kMainMsgClass;
    if (!RegisterClassExW(&wc)) return nullptr;
    return CreateWindowExW(0, kMainMsgClass, L"", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, nullptr, hInst, nullptr);
}

// ------------------------------------------------------------------ 清理

void Cleanup()
{
    if (g_msgWnd) {
        KillTimer(g_msgWnd, TIMER_SAMPLE);
        DestroyWindow(g_msgWnd);
        g_msgWnd = nullptr;
    }

    SyncPosFromWindow();
    cfg::save(&g_cfg);

    tray::remove();
    taskbar::destroy();
    ballwin::destroy();

    metrics::shutdown();      // 先停传感器后台线程，确保之后无人再绘制/采样
    text_render::shutdown();  // 释放按字号缓存的 GDI+ Font：必须在 GdiplusShutdown 之前
    glass::destroy();         // 销毁亚克力底窗（owner 已销毁，这里兜底清理）

    if (g_gdiReady) {
        Gdiplus::GdiplusShutdown(g_gdiToken);
        g_gdiReady  = false;
        g_gdiToken  = 0;
    }
    if (g_mutex) {
        CloseHandle(g_mutex);
        g_mutex = nullptr;
    }
}

} // namespace

// ============================================================================
// 入口（-municode 要求 wWinMain，且必须在匿名命名空间之外）
// ============================================================================

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    g_inst = hInst;
    EnablePerMonitorV2();

    // ---- 单实例 ----
    SetLastError(ERROR_SUCCESS);
    g_mutex = CreateMutexW(nullptr, FALSE, NETBALL_MUTEX_NAME);
    if (g_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND exist = FindExistingBall();
        if (exist) {
            ShowWindow(exist, SW_SHOWNORMAL);
            SetForegroundWindow(exist);
            PostMessageW(exist, WM_NULL, 0, 0);
        }
        CloseHandle(g_mutex);
        g_mutex = nullptr;
        return 0;
    }

    // ---- GDI+ ----
    Gdiplus::GdiplusStartupInput gsi;
    if (Gdiplus::GdiplusStartup(&g_gdiToken, &gsi, nullptr) != Gdiplus::Ok) {
        MessageBoxW(nullptr, L"GDI+ \u521d\u59cb\u5316\u5931\u8d25\u3002", NETBALL_APP_NAME,
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    g_gdiReady = true;

    // ---- 配置 + 数据层 ----
    cfg::load(&g_cfg);
    layout_model::clampConfig(&g_cfg, layout_model::SURF_TASKBAR);
    layout_model::clampConfig(&g_cfg, layout_model::SURF_BALL);
    metrics::init();                 // 内部会启动传感器后台线程
    sensors::setConfig(&g_cfg);

    // ---- 浮窗 ----
    if (!ballwin::create(g_inst, &g_cfg, HandleCommand)) {
        MessageBoxW(nullptr, L"\u521b\u5efa\u6d6e\u52a8\u7a97\u53e3\u5931\u8d25\u3002",
                    NETBALL_APP_NAME, MB_OK | MB_ICONERROR);
        Cleanup();
        return 2;
    }
    SyncPosFromWindow();

    // ---- 任务栏文字（独立于浮窗；默认与浮窗同时显示）----
    if (!g_cfg.showBall) {
        ShowWindow(ballwin::hwnd(), SW_HIDE);
    }
    taskbar::create(g_inst, &g_cfg);

    if (!g_cfg.autoStart) {
        // 以注册表实际状态为准（防止 external 修改后配置过期）
        const bool realAuto = cfg::getAutoStart();
        if (realAuto != (g_cfg.autoStart != FALSE)) {
            g_cfg.autoStart = realAuto ? TRUE : FALSE;
        }
    }

    // ---- 隐藏消息窗口 + 托盘 + 定时器 ----
    g_msgWnd = CreateMsgWindow(g_inst);
    tray::add(ballwin::hwnd(), g_inst);
    tray::setMenuState(g_cfg.showBall != FALSE, g_cfg.taskbarShow != FALSE);
    SampleOnce();                 // 先出一帧数据，别让用户看到 -- 一直挂着
    RestartTimer();
    TrimWorkingSetOnce();         // 冷启动后只收一次（Leader 裁决）

    // ---- 主消息循环 ----
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.hwnd == nullptr && msg.message == WM_NETBALL_POSCHANGED) {
            SyncPosFromWindow();
            cfg::save(&g_cfg);
            continue;
        }
        // ESC 关闭设置窗口：焦点在子控件时按键不会上浮到父窗口，这里统一兜底。
        // 走 WM_CLOSE 保证按「取消」回滚，而不是直接销毁。
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && settings_ui::isOpen()) {
            HWND sw = FindWindowW(WND_CLASS_SETTINGS, nullptr);
            if (sw) {
                PostMessageW(sw, WM_CLOSE, 0, 0);
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Cleanup();
    (void)g_exiting;
    return 0;
}
