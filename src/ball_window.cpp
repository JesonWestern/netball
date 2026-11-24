// src/ball_window.cpp —— 悬浮球分层窗口实现（属主 builder-core-1）
//
// 技术要点：
//   WS_POPUP + WS_EX_LAYERED | WS_EX_TOOLWINDOW (| WS_EX_TOPMOST)
//   32bpp top-down DIB + GDI+ 绘制 + UpdateLayeredWindow(ULW_ALPHA) → per-pixel alpha
//   任务栏无按钮（TOOLWINDOW），不抢焦点（ShowWindow SW_SHOWNOACTIVATE）
//
// v1.2.0：
//   · 宽高独立（ballW/ballH），不再是单一 size；形状 0 在非等宽高时是椭圆。
//   · 文字改走 layout_model + text_render，支持多行、字号/行距/项目间距可调。
//   · 最小尺寸由 render::computeBallSize 抬升（受当前字号下文字占用限制）。
//   · 材质：0 纯色 / 1 亚克力（独立玻璃底窗）/ 2 毛玻璃（自绘，写进 DIB）/ 3 无底板。
#include "ball_window.h"
#include "glass.h"
#include "layout_model.h"
#include "render.h"
#include "sensors.h"
#include "text_render.h"

#include <algorithm>
#include <cstring>
#include <cwchar>
#include <vector>

namespace {

const int kEdgeMargin  = 24;   // 默认定位时距屏幕边缘的像素
const int kMinVisibleEdge = 32; // 球体至少要有这么多像素露在虚拟屏内（可被抓到）

HWND                 g_hwnd      = nullptr;
HINSTANCE            g_inst      = nullptr;
ballwin::CommandFn   g_onCommand = nullptr;
AppConfig            g_cfg        = {};
Metrics              g_metrics    = {};
bool                 g_haveMetrics = false;

bool   g_dragging   = false;
POINT  g_dragCursor = {0, 0};
POINT  g_dragOrigin = {0, 0};

// 离屏缓存（尺寸变化时重建，避免每帧 CreateDIBSection）
HDC     g_memDc  = nullptr;
HBITMAP g_dib    = nullptr;
HGDIOBJ g_oldBmp = nullptr;
BYTE*   g_bits   = nullptr;
int     g_bufW   = 0;
int     g_bufH   = 0;
int     g_stride = 0;

// v1.4.3：液态玻璃动画帧缓存 =「底图 + 文字」（不含流光）。动画帧只做
// 拷贝 + 叠流光 + UpdateLayeredWindow，不重画文字、不重合成材质。
std::vector<BYTE> g_liquidFrame;
bool g_liquidFrameValid = false;
int  g_liquidFrameW = 0, g_liquidFrameH = 0, g_liquidFrameStride = 0;
int  g_liquidFrameX = 0, g_liquidFrameY = 0;

// ------------------------------------------------------------------ DPI

typedef UINT (WINAPI* GetDpiForWindowFn)(HWND);

GetDpiForWindowFn ResolveGetDpiForWindow()
{
    static GetDpiForWindowFn fn     = nullptr;
    static bool              probed = false;
    if (!probed) {
        probed = true;
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        if (u32) fn = (GetDpiForWindowFn)(void*)GetProcAddress(u32, "GetDpiForWindow");
    }
    return fn;
}

UINT DpiOf(HWND hwnd)
{
    GetDpiForWindowFn fn = ResolveGetDpiForWindow();
    if (fn && hwnd) {
        UINT d = fn(hwnd);
        if (d != 0) return d;
    }
    HDC dc = GetDC(nullptr);
    UINT d = 96;
    if (dc) {
        const int v = GetDeviceCaps(dc, LOGPIXELSX);
        if (v > 0) d = (UINT)v;
        ReleaseDC(nullptr, dc);
    }
    return d;
}

// ------------------------------------------------------------------ 几何

void ClampConfig(AppConfig* c)
{
    layout_model::clampConfig(c, layout_model::SURF_BALL);
}

// 当前配置 + 当前指标所需的窗口尺寸（物理像素）
void ComputeSize(const AppConfig& c, UINT dpi, int* outW, int* outH)
{
    render::computeBallSize(&c, &g_metrics, (int)(dpi ? dpi : 96), outW, outH);
}

int BallRadiusPx(const AppConfig& c, int w, int h, int dpi)
{
    if (c.ballCorner <= 0) return 0;
    const int r = MulDiv(c.ballCorner, dpi, 96);
    const int lim = (std::min)(w, h) / 2;
    return (r > lim) ? lim : r;
}

glass::MaterialSpec BuildMaterialSpec(const AppConfig& c, int dpi, int w, int h)
{
    glass::MaterialSpec s;
    ZeroMemory(&s, sizeof(s));
    s.type          = (c.ballMat >= 0 && c.ballMat <= 4) ? c.ballMat : 0;
    s.shape         = (c.ballShape >= 0 && c.ballShape <= 2) ? c.ballShape : 0;
    s.cornerPx      = BallRadiusPx(c, w, h, dpi);
    s.blurPx        = MulDiv((c.ballBlur < 0 ? 0 : (c.ballBlur > 40 ? 40 : c.ballBlur)), dpi, 96);
    s.tint          = c.ballTint;
    s.concentration = (c.ballTintAlpha < 0) ? 0 : (c.ballTintAlpha > 100 ? 100 : c.ballTintAlpha);
    s.border        = c.ballBorder;
    s.sheen         = c.ballSheen;
    s.systemAcrylic = c.ballSysAcrylic;
    return s;
}

void DefaultPosition(int w, int h, int* outX, int* outY)
{
    RECT wa = {0, 0, 0, 0};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) {
        wa.left = 0;
        wa.top = 0;
        wa.right = GetSystemMetrics(SM_CXSCREEN);
        wa.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    *outX = wa.right - w - kEdgeMargin;
    *outY = wa.bottom - h - kEdgeMargin;
    if (*outX < wa.left) *outX = wa.left;
    if (*outY < wa.top)  *outY = wa.top;
}

// 坐标哨兵：只有 posX == -1 且 posY == -1 才表示「未设置」。
// 单独的 -1 是合法虚拟屏坐标（副屏在主屏左侧时就会出现），不能当成未设置。
bool IsPosUnset(int x, int y)
{
    return x == -1 && y == -1;
}

// 恢复路径的最小露出像素：球至少要有 32px（不足则取球自身尺寸）露在虚拟屏内
int MinVisiblePx(int size)
{
    return (size < kMinVisibleEdge) ? size : kMinVisibleEdge;
}

// 【恢复口径 / 宽松】窗口是否还算「在屏」——一律以虚拟屏幕为准
// 判定：与虚拟屏幕的交集面积 ≥ 窗口面积的 1/3，或横竖两个方向都至少露出 MinVisiblePx 像素。
bool IsPositionUsable(int x, int y, int w, int h)
{
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0 || w <= 0 || h <= 0) return false;

    const int ix1 = (std::max)(x, vx);
    const int iy1 = (std::max)(y, vy);
    const int ix2 = (std::min)(x + w, vx + vw);
    const int iy2 = (std::min)(y + h, vy + vh);
    if (ix2 <= ix1 || iy2 <= iy1) return false;

    const int visW = ix2 - ix1;
    const int visH = iy2 - iy1;
    if (visW >= MinVisiblePx(w) && visH >= MinVisiblePx(h)) return true;

    const double inter = (double)visW * (double)visH;
    const double total = (double)w * (double)h;
    return total > 0.0 && inter / total >= (1.0 / 3.0);
}

// 【拖动口径 / 严格】球体必须 100% 落在虚拟屏内，0% 出屏。
void ClampFullyInsideVirtualScreen(int* x, int* y, int w, int h)
{
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vw <= 0 || vh <= 0) return;

    if (*x + w > vx + vw) *x = vx + vw - w;
    if (*y + h > vy + vh) *y = vy + vh - h;
    if (*x < vx) *x = vx;
    if (*y < vy) *y = vy;
}

bool ClampWindowFullyInside(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return false;
    RECT wr;
    if (!GetWindowRect(hwnd, &wr)) return false;

    int x = wr.left;
    int y = wr.top;
    ClampFullyInsideVirtualScreen(&x, &y, wr.right - wr.left, wr.bottom - wr.top);
    if (x == wr.left && y == wr.top) return false;

    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
}

// ------------------------------------------------------------------ 离屏缓冲

void DestroyBackBuffer()
{
    if (g_memDc) {
        if (g_oldBmp) SelectObject(g_memDc, g_oldBmp);
        DeleteDC(g_memDc);
    }
    if (g_dib) DeleteObject(g_dib);
    g_memDc  = nullptr;
    g_dib    = nullptr;
    g_oldBmp = nullptr;
    g_bits   = nullptr;
    g_bufW   = 0;
    g_bufH   = 0;
    g_stride = 0;
    g_liquidFrameValid = false;
    g_liquidFrame.clear();
}

bool EnsureBackBuffer(int w, int h)
{
    if (g_memDc && g_bits && g_bufW == w && g_bufH == h) return true;
    DestroyBackBuffer();

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;          // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    g_dib   = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, (void**)&g_bits, nullptr, 0);
    g_memDc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);

    if (!g_dib || !g_memDc || !g_bits) {
        DestroyBackBuffer();
        return false;
    }
    g_oldBmp = (HGDIOBJ)SelectObject(g_memDc, g_dib);
    g_bufW   = w;
    g_bufH   = h;
    g_stride = w * 4;

    DIBSECTION ds;
    ZeroMemory(&ds, sizeof(ds));
    if (GetObjectW(g_dib, sizeof(ds), &ds) == (int)sizeof(ds) && ds.dsBm.bmWidthBytes > 0) {
        g_stride = ds.dsBm.bmWidthBytes;
    }
    return true;
}

// ------------------------------------------------------------------ 绘制

void PresentLayered(int x, int y, int w, int h)
{
    POINT         ptSrc = {0, 0};
    POINT         ptDst = {x, y};
    SIZE          size  = {w, h};
    BLENDFUNCTION bf;
    bf.BlendOp             = AC_SRC_OVER;
    bf.BlendFlags          = 0;
    bf.SourceConstantAlpha = 255;              // 透明度已烘进 per-pixel alpha
    bf.AlphaFormat         = AC_SRC_ALPHA;

    // hdcDst 传 NULL 走默认调色板：省掉每帧 GetDC/ReleaseDC，
    // 液态玻璃动画帧每秒调用 30 次，这个开销不可忽略。失败再回退到屏幕 DC。
    if (UpdateLayeredWindow(g_hwnd, nullptr, &ptDst, &size, g_memDc, &ptSrc, 0, &bf, ULW_ALPHA)) {
        return;
    }

    HDC screen = GetDC(nullptr);
    if (!screen) return;
    UpdateLayeredWindow(g_hwnd, screen, &ptDst, &size, g_memDc, &ptSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

bool PaintNow()
{
    if (!g_hwnd || !IsWindow(g_hwnd)) return false;

    // 关闭「显示悬浮球」时不做任何抓屏/合成，避免后台空转；
    // 再次显示时会走 applyConfig → PaintNow，重新生成完整材质。
    if (!g_cfg.showBall) {
        glass::hideSystemAcrylic(1);
        return true;
    }

    const UINT dpi = DpiOf(g_hwnd);
    int w = 0, h = 0;
    ComputeSize(g_cfg, dpi, &w, &h);
    if (w <= 0 || h <= 0) return false;

    RECT wr;
    if (!GetWindowRect(g_hwnd, &wr)) return false;

    // v1.4.0：尺寸严格等于配置。旧版“只增不减”会让切换形状后胶囊宽高比失真。
    if (wr.right - wr.left != w || wr.bottom - wr.top != h) {
        int nx = (int)wr.left;
        int ny = (int)wr.top;
        ClampFullyInsideVirtualScreen(&nx, &ny, w, h);
        SetWindowPos(g_hwnd, nullptr, nx, ny, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        GetWindowRect(g_hwnd, &wr);
        w = wr.right - wr.left;
        h = wr.bottom - wr.top;
    }

    if (!EnsureBackBuffer(w, h)) return false;
    memset(g_bits, 0, (size_t)g_stride * (size_t)h);

    RECT rc = {wr.left, wr.top, wr.left + w, wr.top + h};
    const int dpiInt = (int)dpi;
    glass::MaterialSpec spec = BuildMaterialSpec(g_cfg, dpiInt, w, h);

    // 系统实时亚克力：底窗先到位，文字层只画文字/描边，避免双层色调叠加
    bool sysAcrylic = g_cfg.showBall &&
                      (spec.type == glass::MAT_ACRYLIC) && spec.systemAcrylic &&
                      glass::acrylicAvailable();
    if (sysAcrylic) {
        if (!glass::placeSystemAcrylic(1, g_hwnd, g_hwnd, rc, dpiInt, spec)) {
            sysAcrylic = false;
            spec.systemAcrylic = FALSE;
        }
    } else {
        glass::hideSystemAcrylic(1);
    }

    BOOL hidSelf = FALSE;
    if (sysAcrylic) {
        glass::MaterialSpec textSpec = spec;
        textSpec.type = glass::MAT_NONE;
        textSpec.concentration = 0;
        glass::paint(1, nullptr, rc, dpiInt, textSpec, g_bits, g_stride, w, h, &hidSelf);
    } else {
        glass::paint(1, g_hwnd, rc, dpiInt, spec, g_bits, g_stride, w, h, &hidSelf);
    }

    // 自动文字色：材质已写进 DIB，这里再取一次「表面背后的真实背景均色」，
    // 让 render 能把半透明表面还原成实际呈现色（否则深色壁纸上的浅色球会选黑字）。
    // 纯色/无底板没有材质抓屏，允许按需抓一帧并缓存；其余材质直接命中材质缓存。
    render::TextBackdrop backdrop = {};
    if (g_cfg.ballTextAuto && !sysAcrylic) {
        const BOOL allowCapture = ((spec.type == glass::MAT_SOLID ||
                                    spec.type == glass::MAT_NONE) ? TRUE : FALSE);
        COLORREF bg = 0;
        if (glass::backdropColor(1, g_hwnd, rc, dpiInt, w, h, spec.shape, spec.cornerPx,
                                 allowCapture, &bg)) {
            backdrop.color = bg;
            backdrop.valid = TRUE;
        }
    }

    render::drawBallTextEx(g_memDc, w, h, dpiInt, &g_cfg, &g_metrics, &backdrop);

    // 液态玻璃：先存「底图 + 文字」的一帧（无流光），再叠当前时刻的流光。
    // 之后的动画帧只拷贝这一帧 + 重叠流光，不再走文字排版/GDI+ 绘制。
    if (spec.type == glass::MAT_LIQUID && !sysAcrylic) {
        const size_t bytes = (size_t)g_stride * (size_t)h;
        g_liquidFrame.resize(bytes);
        memcpy(g_liquidFrame.data(), g_bits, bytes);
        g_liquidFrameValid  = true;
        g_liquidFrameW      = w;
        g_liquidFrameH      = h;
        g_liquidFrameStride = g_stride;
        g_liquidFrameX      = wr.left;
        g_liquidFrameY      = wr.top;
        glass::addLiquidBand(g_bits, g_stride, w, h);
    } else {
        g_liquidFrameValid = false;
    }

    PresentLayered(wr.left, wr.top, w, h);

    // v1.4.1：glass::paint 不再隐藏窗口，hidSelf 恒为 FALSE；保留分支兼容旧接口。
    if (hidSelf) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        if (g_cfg.alwaysOnTop) {
            SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    return true;
}

// ------------------------------------------------------------------ 右键菜单

void ShowContextMenu()
{
    if (!g_hwnd) return;

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"\u8bbe\u7f6e(&S)...");                     // 设置
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (g_cfg.alwaysOnTop ? MF_CHECKED : MF_UNCHECKED),
                IDM_TOPMOST, L"\u603b\u5728\u6700\u524d(&T)");                              // 总在最前
    AppendMenuW(menu, MF_STRING | (g_cfg.autoStart ? MF_CHECKED : MF_UNCHECKED),
                IDM_AUTOSTART, L"\u5f00\u673a\u81ea\u542f(&A)");                            // 开机自启
    AppendMenuW(menu, MF_STRING | (g_cfg.taskbarShow ? MF_CHECKED : MF_UNCHECKED),
                IDM_TASKBAR, L"\u5728\u4efb\u52a1\u680f\u663e\u793a(&K)");                  // 在任务栏显示
    AppendMenuW(menu, MF_STRING, IDM_RESETPOS, L"\u91cd\u7f6e\u4f4d\u7f6e(&R)");            // 重置位置
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"\u5173\u4e8e(&B)");                           // 关于
    AppendMenuW(menu, MF_STRING, IDM_EXIT,  L"\u9000\u51fa(&X)");                           // 退出
    SetMenuDefaultItem(menu, IDM_SETTINGS, FALSE);

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    const int cmd = (int)TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                        pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);      // 菜单关闭后补一条，避免前台窗口异常

    if (cmd > 0 && g_onCommand) g_onCommand(cmd);
}

// ------------------------------------------------------------------ 位置记忆

void OnDragFinished()
{
    if (!g_hwnd) return;
    RECT wr = {0, 0, 0, 0};
    const BOOL ok = GetWindowRect(g_hwnd, &wr);
    if (ok) {
        g_cfg.posX = (int)wr.left;
        g_cfg.posY = (int)wr.top;
    }
    // 交给 UI 线程去 cfg::save（本文件不碰配置持久化）
    PostThreadMessageW(GetCurrentThreadId(), WM_NETBALL_POSCHANGED, 0, 0);
}

// ------------------------------------------------------------------ 窗口过程

LRESULT CALLBACK BallWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        g_dragging = true;
        RECT wr;
        GetWindowRect(hwnd, &wr);
        g_dragOrigin = POINT{wr.left, wr.top};
        GetCursorPos(&g_dragCursor);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (g_dragging) {
            POINT pt;
            GetCursorPos(&pt);
            const int nx = g_dragOrigin.x + (pt.x - g_dragCursor.x);
            const int ny = g_dragOrigin.y + (pt.y - g_dragCursor.y);
            // 分层窗口移动后内容自动跟随，无需重绘
            SetWindowPos(hwnd, nullptr, nx, ny, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            // 拖动过程中就把球整体钳在虚拟屏内（0% 出屏）
            ClampWindowFullyInside(hwnd);

            // 系统亚克力底窗跟随；自绘材质的 DIB 随窗口整体移动，拖动时不重抓屏
            if (g_cfg.ballMat == glass::MAT_ACRYLIC && g_cfg.ballSysAcrylic) {
                RECT wr;
                if (GetWindowRect(hwnd, &wr)) {
                    const int dpi = (int)DpiOf(hwnd);
                    glass::MaterialSpec spec = BuildMaterialSpec(
                        g_cfg, dpi, wr.right - wr.left, wr.bottom - wr.top);
                    glass::placeSystemAcrylic(1, hwnd, hwnd, wr, dpi, spec);
                }
            }
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_dragging) {
            g_dragging = false;
            if (GetCapture() == hwnd) ReleaseCapture();
            ClampWindowFullyInside(hwnd);   // 拖动结束兜底：球必须完全落在虚拟屏内
            OnDragFinished();
            glass::invalidateBackdrop(1);   // 位置变了，材质背景需要重抓
            PaintNow();
        }
        return 0;

    // 捕获被系统/其它窗口抢走（Alt+Tab 等）时也当作拖动结束，补钳制 + 保存
    case WM_CAPTURECHANGED:
        if (g_dragging) {
            g_dragging = false;
            ClampWindowFullyInside(hwnd);
            OnDragFinished();
            glass::invalidateBackdrop(1);
            PaintNow();
        }
        return 0;

    case WM_RBUTTONUP:
        ShowContextMenu();
        return 0;

    case WM_COMMAND:
        // tray.cpp 的托盘菜单就是把结果以 WM_COMMAND 发到本窗口的
        if (HIWORD(wp) == 0 && g_onCommand && LOWORD(wp) > 0) {
            g_onCommand((int)LOWORD(wp));
            return 0;
        }
        break;

    case WM_DPICHANGED: {
        const RECT* sugg = (const RECT*)lp;
        int w = 0, h = 0;
        ComputeSize(g_cfg, (UINT)HIWORD(wp), &w, &h);
        if (sugg) {
            SetWindowPos(hwnd, nullptr, sugg->left, sugg->top, w, h,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            SetWindowPos(hwnd, nullptr, 0, 0, w, h,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        g_cfg.posX = sugg ? sugg->left : g_cfg.posX;
        g_cfg.posY = sugg ? sugg->top : g_cfg.posY;
        DestroyBackBuffer();
        glass::invalidateBackdrop(1);
        PaintNow();
        return 0;
    }

    case WM_THEMECHANGED:
        glass::invalidateBackdrop(1);
        DestroyBackBuffer();
        PaintNow();
        return 0;

    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE: {
        if (msg == WM_SETTINGCHANGE &&
            wp != SPI_SETWORKAREA && wp != SPI_SETDESKWALLPAPER) {
            break;
        }
        int w = 0, h = 0;
        ComputeSize(g_cfg, DpiOf(hwnd), &w, &h);
        RECT wr;
        GetWindowRect(hwnd, &wr);
        if (!IsPositionUsable(wr.left, wr.top, w, h)) {
            int nx = 0, ny = 0;
            DefaultPosition(w, h, &nx, &ny);
            SetWindowPos(hwnd, nullptr, nx, ny, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
            g_cfg.posX = nx;
            g_cfg.posY = ny;
            OnDragFinished();     // 位置已被动改动，同样通知保存
        }
        if (msg == WM_DISPLAYCHANGE) {
            sensors::rescanGpus();   // 显卡增减 / 显示器变化后重新枚举
        }
        glass::invalidateBackdrop(1);
        DestroyBackBuffer();
        PaintNow();
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;                 // 分层窗口不需要擦背景

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CLOSE:
        // 覆盖安装时 Restart Manager 会发 WM_CLOSE；显式销毁让主循环干净退出并落盘配置
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        glass::hideSystemAcrylic(1);
        g_hwnd = nullptr;
        PostQuitMessage(0);       // 悬浮球就是主窗口，销毁即退出程序
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool RegisterBallClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = BallWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = WND_CLASS_BALL;

    HICON icon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                   0, 0, LR_DEFAULTSIZE | LR_SHARED);
    if (!icon) icon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIcon   = icon;
    wc.hIconSm = icon;

    WNDCLASSEXW exist;
    ZeroMemory(&exist, sizeof(exist));
    exist.cbSize = sizeof(exist);
    if (GetClassInfoExW(hInst, WND_CLASS_BALL, &exist)) return true;

    return RegisterClassExW(&wc) != 0;
}

// 指标是否有可见变化（用于「数值没变就不重画」）
bool MetricsChanged(const Metrics& a, const Metrics& b)
{
    if (a.netValid != b.netValid) return true;
    if (a.upBps != b.upBps || a.downBps != b.downBps) return true;
    if (a.memPercent != b.memPercent || a.memTotalBytes != b.memTotalBytes) return true;
    if (a.cpuPercent != b.cpuPercent) return true;
    if (a.cpuTempValid != b.cpuTempValid || a.cpuTempC != b.cpuTempC) return true;
    if (a.gpuCount != b.gpuCount) return true;
    for (int i = 0; i < NETBALL_MAX_GPU; ++i) {
        if (a.gpuLoad[i] != b.gpuLoad[i]) return true;
        if (a.gpuTempValid[i] != b.gpuTempValid[i]) return true;
        if (a.gpuTempC[i] != b.gpuTempC[i]) return true;
    }
    return false;
}

} // namespace

// ============================================================================
// 契约实现：ballwin
// ============================================================================

bool ballwin::create(HINSTANCE hInst, const AppConfig* c, CommandFn onCommand)
{
    if (g_hwnd) return true;

    g_inst      = hInst ? hInst : (HINSTANCE)GetModuleHandleW(nullptr);
    g_onCommand = onCommand;

    if (c) g_cfg = *c;
    else   ZeroMemory(&g_cfg, sizeof(g_cfg));
    ClampConfig(&g_cfg);

    ZeroMemory(&g_metrics, sizeof(g_metrics));
    g_haveMetrics = false;

    if (!RegisterBallClass(g_inst)) return false;

    const UINT dpi = DpiOf(nullptr);
    int w = 0, h = 0;
    ComputeSize(g_cfg, dpi, &w, &h);

    int x = g_cfg.posX;
    int y = g_cfg.posY;
    // 只在「posX==posY==-1（未设置）」或「与虚拟屏毫无可见交集」时回默认位置。
    if (IsPosUnset(x, y) || !IsPositionUsable(x, y, w, h)) {
        DefaultPosition(w, h, &x, &y);
    }

    DWORD ex = WS_EX_LAYERED | WS_EX_TOOLWINDOW;
    if (g_cfg.alwaysOnTop) ex |= WS_EX_TOPMOST;

    HWND hwnd = CreateWindowExW(ex, WND_CLASS_BALL, NETBALL_APP_NAME, WS_POPUP,
                                x, y, w, h,
                                nullptr, nullptr, g_inst, nullptr);
    if (!hwnd) return false;

    g_hwnd     = hwnd;
    g_cfg.posX = x;
    g_cfg.posY = y;

    // 首次绘制放在 ShowWindow 之前，避免闪一帧空白；showBall=false 时保持隐藏
    PaintNow();
    if (g_cfg.showBall) ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    // 创建时就记下屏幕尺寸，供下次启动判断分辨率是否变化
    g_cfg.savedScreenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    g_cfg.savedScreenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return true;
}

void ballwin::destroy()
{
    if (g_hwnd && IsWindow(g_hwnd)) {
        DestroyWindow(g_hwnd);
    }
    g_hwnd = nullptr;
    glass::hideSystemAcrylic(1);
    DestroyBackBuffer();
}

void ballwin::applyConfig(const AppConfig* c)
{
    if (!c) return;
    g_cfg = *c;
    ClampConfig(&g_cfg);

    if (!g_hwnd || !IsWindow(g_hwnd)) return;

    int w = 0, h = 0;
    ComputeSize(g_cfg, DpiOf(g_hwnd), &w, &h);

    RECT wr;
    GetWindowRect(g_hwnd, &wr);

    int x = g_cfg.posX;
    int y = g_cfg.posY;
    if (IsPosUnset(x, y)) {
        // posX==posY==-1：调用方明确要求「重置位置」→ 回默认位置
        DefaultPosition(w, h, &x, &y);
    } else if (!IsPositionUsable(x, y, w, h)) {
        // 配置位置已不可用（显示器被拔 / 分辨率变了）→ 当前窗口还在屏上就保持不动，否则回默认
        if (IsPositionUsable(wr.left, wr.top, w, h)) {
            x = wr.left;
            y = wr.top;
        } else {
            DefaultPosition(w, h, &x, &y);
        }
    }

    HWND insertAfter = g_cfg.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST;
    SetWindowPos(g_hwnd, insertAfter, x, y, w, h, SWP_NOACTIVATE);

    g_cfg.posX = x;
    g_cfg.posY = y;

    DestroyBackBuffer();     // 尺寸/材质可能变了，强制按新尺寸重建
    PaintNow();
}

void ballwin::setMetrics(const Metrics* m)
{
    if (!m) return;

    const bool changed = !g_haveMetrics || MetricsChanged(g_metrics, *m);

    g_metrics     = *m;
    g_haveMetrics = true;

    if (!changed) return;    // 数值没变就不重画，省 CPU

    // 拖动期间不做数据驱动的整窗重绘：材质刷新由 TIMER_GLASS 负责，
    // 松手时 WM_LBUTTONUP 会完整重绘一次，避免高频采样干扰拖动。
    if (g_dragging) return;

    PaintNow();
}

void ballwin::refreshMaterial()
{
    if (!g_hwnd || !IsWindow(g_hwnd) || !g_cfg.showBall) return;
    if (!IsWindowVisible(g_hwnd)) return;

    const int mat = g_cfg.ballMat;
    if (mat != glass::MAT_ACRYLIC && mat != glass::MAT_GLASS && mat != glass::MAT_LIQUID) {
        return;   // 纯色 / 无底板不需要抓屏
    }
    if (mat == glass::MAT_ACRYLIC && g_cfg.ballSysAcrylic &&
        glass::acrylicAvailable()) {
        return;   // 系统实时亚克力由 DWM 刷新，不跑自绘抓屏
    }

    // 只让背景缓存失效，材质/文字仍走正常合成；抓屏路径不隐藏窗口，
    // 因此可以在拖动过程中安全刷新。
    glass::invalidateBackdrop(1);
    PaintNow();
}

// 液态玻璃流光动画帧：只做「拷贝缓存帧 + 叠一条高光带 + 呈现」，
// 不抓屏、不重算 SDF、不重排/重画文字；数值刷新仍走 TIMER_SAMPLE。
// 拖动期间不重绘，避免和窗口移动抢 CPU。
void ballwin::animateLiquid()
{
    if (!g_hwnd || !IsWindow(g_hwnd) || !g_cfg.showBall) return;
    if (g_cfg.ballMat != glass::MAT_LIQUID) return;
    if (g_dragging || !IsWindowVisible(g_hwnd)) return;

    if (g_liquidFrameValid && g_bits && !g_liquidFrame.empty()) {
        RECT wr;
        const UINT dpi = DpiOf(g_hwnd);
        int w = 0, h = 0;
        ComputeSize(g_cfg, dpi, &w, &h);
        if (w == g_liquidFrameW && h == g_liquidFrameH &&
            g_stride == g_liquidFrameStride &&
            GetWindowRect(g_hwnd, &wr) &&
            wr.left == g_liquidFrameX && wr.top == g_liquidFrameY) {
            memcpy(g_bits, g_liquidFrame.data(), (size_t)g_liquidFrameStride * (size_t)h);
            glass::addLiquidBand(g_bits, g_stride, w, h);
            PresentLayered(wr.left, wr.top, w, h);
            return;
        }
    }

    // 缓存不可用（刚启动/尺寸或位置变了）：退回完整重绘，顺便重建缓存。
    PaintNow();
}

HWND ballwin::hwnd()
{
    return g_hwnd;
}
