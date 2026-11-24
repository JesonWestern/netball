// src/taskbar_text.cpp —— 任务栏信息面板实现（v1.1.0 引入，v1.2.0 重写排版与材质）
//
// 设计要点（与 Explorer 相关的坑都写在这里，改前先读）：
//   1. 不把窗口 SetParent 进任务栏：Win11 任务栏是 XAML 渲染，跨进程子窗口层级
//      不稳定；本实现是独立的顶层分层窗，只负责“贴在任务栏上画字”。
//   2. WS_EX_LAYERED + WS_EX_TRANSPARENT：逐像素 alpha 负责透明背景，TRANSPARENT
//      负责鼠标点击穿透（文字区域也不挡任务栏/托盘操作）。
//   3. 位置每次 sync 都重新取：任务栏可能换边、自动隐藏、分辨率变化、Explorer 重启。
//      锚点优先 TrayNotifyWnd（托盘+时钟），失败时按任务栏右端固定预留兜底。
//   4. 只在“文本 / 颜色 / 字号 / 矩形 / 材质”变化时重绘；sync 由主消息窗口 250ms
//      定时器驱动，文本未变时只有几次数值比较，CPU 可忽略。
//   5. v1.2.0 起面板尺寸由内容决定（最多两行），允许向上凸出任务栏；材质三档：
//      纯色 / 亚克力（独立玻璃底窗）/ 毛玻璃（自绘模糊写进 DIB）/ 无底板。
#include "taskbar_text.h"
#include "glass.h"
#include "layout_model.h"
#include "render.h"
#include "surface_shape.h"
#include "text_render.h"

#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

using namespace Gdiplus;

namespace {

const int kPadDip        = 10;   // 文字左右内边距
const int kPadYDip       = 6;    // 文字上下内边距
const int kGapDip        = 8;    // 与托盘锚点的间距
const int kFallbackReserveDip = 300;  // 找不到 TrayNotifyWnd 时，从任务栏右端预留的宽度
const int kMinVisibleThickness = 6;   // 自动隐藏时任务栏可见厚度阈值
const int kFontMin       = 10;
const int kFontMax       = 48;
const int kTextCapacity  = 256;   // 拼接后的文本指纹缓冲

HWND      g_hwnd = nullptr;
HINSTANCE g_inst = nullptr;
AppConfig g_cfg  = {};
Metrics   g_metrics = {};
bool      g_haveMetrics = false;

HDC     g_memDc  = nullptr;
HBITMAP g_dib    = nullptr;
HGDIOBJ g_oldBmp = nullptr;
BYTE*   g_bits   = nullptr;
int     g_bufW   = 0;
int     g_bufH   = 0;
int     g_stride = 0;

// 液态玻璃动画帧缓存 =「底图 + 文字」（不含流光），动画帧只拷贝 + 叠流光 + 呈现。
std::vector<BYTE> g_liquidFrame;
bool g_liquidFrameValid = false;
int  g_liquidFrameW = 0, g_liquidFrameH = 0, g_liquidFrameStride = 0;

// 宽度高水位：面板右缘固定，宽度随内容自适应会把左边缘拉着每秒跳一次
// （"141 B/s" → "1.2 KB/s"）。只增不减，配置/DPI/分辨率变化时复位。
int      g_widthHwm = 0;

bool     g_shown     = false;
bool     g_lastValid = false;
RECT     g_lastRect  = {0, 0, 0, 0};
int      g_lastFont  = 0;
COLORREF g_lastTextColor = 0;
COLORREF g_lastTint  = 0;
int      g_lastMat   = -1;
int      g_lastAlpha = -1;
int      g_lastRadius = -1;
wchar_t  g_lastText[kTextCapacity] = {0};

UINT g_taskbarCreatedMsg = 0;

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
        if (d >= 48 && d <= 480) return d;
    }
    HDC dc = GetDC(nullptr);
    UINT d = 96;
    if (dc) {
        const int v = GetDeviceCaps(dc, LOGPIXELSX);
        if (v >= 48 && v <= 480) d = (UINT)v;
        ReleaseDC(nullptr, dc);
    }
    return d;
}

// ------------------------------------------------------------------ 任务栏信息

struct BarInfo {
    HWND tray;
    HWND notify;
    RECT bar;
    RECT anchor;
    bool hasAnchor;
};

bool RectHasArea(const RECT& r)
{
    return (r.right > r.left) && (r.bottom > r.top);
}

bool FindTaskbar(BarInfo* out)
{
    if (!out) return false;
    ZeroMemory(out, sizeof(*out));

    static HWND s_tray = nullptr;      // 缓存：只在 TaskbarCreated / 失效时重找
    if (s_tray && !IsWindow(s_tray)) s_tray = nullptr;

    HWND tray = s_tray ? s_tray : FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray || !IsWindow(tray)) return false;
    s_tray = tray;

    if (!GetWindowRect(tray, &out->bar) || !RectHasArea(out->bar)) return false;

    out->tray = tray;

    HWND notify = FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr);
    if (notify && IsWindow(notify)) {
        RECT nr;
        if (GetWindowRect(notify, &nr) && RectHasArea(nr) &&
            (nr.right - nr.left) >= 20 && (nr.bottom - nr.top) >= 10 &&
            nr.left < out->bar.right && nr.right > out->bar.left &&
            nr.top < out->bar.bottom && nr.bottom > out->bar.top) {
            out->notify    = notify;
            out->anchor    = nr;
            out->hasAnchor = true;
        }
    }
    return true;
}

bool TaskbarAutoHidden(const BarInfo& bi)
{
    if (!bi.tray || !IsWindowVisible(bi.tray)) return true;

    APPBARDATA abd;
    ZeroMemory(&abd, sizeof(abd));
    abd.cbSize = sizeof(abd);
    const DWORD state = (DWORD)SHAppBarMessage(ABM_GETSTATE, &abd);
    if ((state & ABS_AUTOHIDE) == 0) return false;

    HMONITOR mon = MonitorFromWindow(bi.tray, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    ZeroMemory(&mi, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoW(mon, &mi)) return false;

    const RECT& mr = mi.rcMonitor;
    const int visW = (std::min)(bi.bar.right, mr.right) - (std::max)(bi.bar.left, mr.left);
    const int visH = (std::min)(bi.bar.bottom, mr.bottom) - (std::max)(bi.bar.top, mr.top);
    if (visW <= 0 || visH <= 0) return true;

    // 按任务栏贴边方向看“还剩多厚”：自动隐藏后任务栏会滑出屏幕，只剩几像素。
    if (bi.bar.bottom >= mr.bottom - 2) return visH <= kMinVisibleThickness;
    if (bi.bar.top    <= mr.top + 2)    return visH <= kMinVisibleThickness;
    if (bi.bar.left   <= mr.left + 2)   return visW <= kMinVisibleThickness;
    if (bi.bar.right  >= mr.right - 2)  return visW <= kMinVisibleThickness;
    return false;
}

// 全屏应用 / 演示模式时隐藏，避免压在游戏和投影画面上
bool FullscreenActive()
{
    typedef HRESULT (WINAPI* QueryStateFn)(int*);
    static QueryStateFn fn     = nullptr;
    static bool         probed = false;
    if (!probed) {
        probed = true;
        HMODULE s = GetModuleHandleW(L"shell32.dll");
        if (s) fn = (QueryStateFn)(void*)GetProcAddress(s, "SHQueryUserNotificationState");
    }
    if (!fn) return false;

    int state = 0;
    if (FAILED(fn(&state))) return false;
    // QUNS_BUSY=2, QUNS_RUNNING_D3D_FULL_SCREEN=3, QUNS_PRESENTATION_MODE=4
    return state == 2 || state == 3 || state == 4;
}

bool TaskbarUsesLightTheme()
{
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS) {
        return false;
    }
    DWORD v = 0, cb = sizeof(v), type = 0;
    const LONG rc = RegQueryValueExW(hk, L"SystemUsesLightTheme", nullptr, &type,
                                     (LPBYTE)&v, &cb);
    RegCloseKey(hk);
    if (rc != ERROR_SUCCESS || type != REG_DWORD) return false;
    return v != 0;
}

COLORREF BarThemeTextColor()
{
    return TaskbarUsesLightTheme() ? RGB(18, 20, 24) : RGB(255, 255, 255);
}

// ------------------------------------------------------------------ 绘制

void DestroyBuffer()
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

bool EnsureBuffer(int w, int h)
{
    if (g_memDc && g_bits && g_bufW == w && g_bufH == h) return true;
    DestroyBuffer();

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
        DestroyBuffer();
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

bool PresentLayered(int x, int y, int w, int h)
{
    if (!g_hwnd || !g_memDc) return false;

    POINT         ptSrc = {0, 0};
    POINT         ptDst = {x, y};
    SIZE          size  = {w, h};
    BLENDFUNCTION bf;
    bf.BlendOp             = AC_SRC_OVER;
    bf.BlendFlags          = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat         = AC_SRC_ALPHA;

    // hdcDst 传 NULL 走默认调色板，省掉每帧 GetDC/ReleaseDC（动画帧 30Hz 调用）。
    if (UpdateLayeredWindow(g_hwnd, nullptr, &ptDst, &size, g_memDc, &ptSrc, 0, &bf, ULW_ALPHA)) {
        return true;
    }

    HDC screen = GetDC(nullptr);
    if (!screen) return false;
    const BOOL ok = UpdateLayeredWindow(g_hwnd, screen, &ptDst, &size,
                                        g_memDc, &ptSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    return ok != FALSE;
}

// ------------------------------------------------------------------ 布局

struct PanelLayout {
    layout_model::LayoutBlock spec;
    text_render::Block        block;
    int      fontPx;
    int      padX, padY;
    int      radiusPx;
    int      x, y, w, h;
    COLORREF textColor;
    COLORREF tint;
    int      mat;            // 0 纯色 / 1 亚克力 / 2 玻璃 / 3 液态玻璃 / 4 无底板
    int      tintAlpha;
    int      shape;          // 1 圆角方 / 2 胶囊
    int      cornerPx;
    int      blurPx;
    bool     border;
    bool     sheen;
    bool     useAcrylic;     // 系统实时亚克力（仅 mat==1 且可用）
};

// v1.4.3：液态玻璃动画复用「上一次真正画出去」的布局（Block 按值保存、文字色已解析），
// 动画帧只重合成材质和重画文字，不重新枚举任务栏、不重新采样背景。
PanelLayout g_lastPaint      = {};
COLORREF    g_lastPaintBase  = RGB(0, 0, 0);
bool        g_lastPaintValid = false;

// 面板在任务栏上的可用宽度与字号
bool BuildLayout(const BarInfo& bi, PanelLayout* out)
{
    if (!out) return false;

    layout_model::LayoutBlock spec;
    layout_model::parse(g_cfg.tbLayout, NETBALL_MAX_TB_ROWS, &spec);
    if (spec.count <= 0) return false;      // 一个项目都没勾 → 不显示面板

    const int barW = bi.bar.right - bi.bar.left;
    const int barH = bi.bar.bottom - bi.bar.top;
    if (barW <= 0 || barH <= 0) return false;

    const int dpi = (int)DpiOf(bi.tray);
    const int padX = MulDiv(kPadDip, dpi, 96);
    const int padY = MulDiv(kPadYDip, dpi, 96);
    const int gap  = MulDiv(kGapDip, dpi, 96);

    ZeroMemory(out, sizeof(*out));
    out->spec   = spec;
    out->padX   = padX;
    out->padY   = padY;
    out->mat    = (g_cfg.tbMat >= 0 && g_cfg.tbMat <= 4) ? g_cfg.tbMat : 1;
    out->tint   = g_cfg.tbTint;
    out->tintAlpha = (g_cfg.tbTintAlpha < 0) ? 0 : (g_cfg.tbTintAlpha > 100 ? 100 : g_cfg.tbTintAlpha);
    out->shape  = (g_cfg.tbShape == 2) ? 2 : 1;
    out->cornerPx = MulDiv((g_cfg.tbCorner < 0 ? 0 : (g_cfg.tbCorner > 40 ? 40 : g_cfg.tbCorner)),
                           dpi, 96);
    out->blurPx = MulDiv((g_cfg.tbBlur < 0 ? 0 : (g_cfg.tbBlur > 40 ? 40 : g_cfg.tbBlur)),
                         dpi, 96);
    out->border = (g_cfg.tbBorder != FALSE);
    out->sheen  = (g_cfg.tbSheen != FALSE);

    const bool horizontal = barW >= barH;

    // ---- 文字颜色（与材质底板联动）----
    COLORREF textColor;
    if (!g_cfg.tbTextAuto) {
        textColor = g_cfg.tbTextColor;
    } else if (out->mat == 4) {
        // 无底板：仍按任务栏主题选色（和 v1.1 一致）
        textColor = BarThemeTextColor();
    } else {
        textColor = render::autoTextColor(g_cfg.tbTint, g_cfg.tbTintAlpha);
    }
    out->textColor = textColor;

    if (horizontal) {
        const int anchorRight = bi.hasAnchor
            ? (bi.anchor.left - gap)
            : (bi.bar.right - MulDiv(kFallbackReserveDip, dpi, 96));
        const int leftLimit = bi.bar.left + MulDiv(4, dpi, 96);
        const int maxTextW  = anchorRight - leftLimit - padX * 2;
        if (maxTextW < 24) return false;

        const int rows = (spec.rows > 0) ? spec.rows : 1;
        int fontPx;
        if (g_cfg.tbFontPx > 0) {
            fontPx = MulDiv(g_cfg.tbFontPx, dpi, 96);
        } else {
            // 自适应：既不超过任务栏高度的 0.36，也要保证两行能立在任务栏里
            int byHeight = (barH * 36) / 100;
            const int maxByRows = (barH - padY * 2) * 100 / (g_cfg.tbLinePct * rows);
            if (byHeight > maxByRows) byHeight = maxByRows;
            fontPx = byHeight;
        }
        if (fontPx < kFontMin) fontPx = kFontMin;
        if (fontPx > kFontMax) fontPx = kFontMax;

        text_render::BuildBlock(spec, &g_cfg, &g_metrics, fontPx, g_cfg.tbLinePct,
                                MulDiv(g_cfg.tbItemGap, dpi, 96), &out->block);

        // 宽度放不下就逐级压字号（显式字号也压，宁可小一点也不能溢出任务栏）
        if (out->block.width > maxTextW) {
            const int fitted = text_render::FitFontPx(spec, &g_cfg, &g_metrics,
                                                     g_cfg.tbLinePct,
                                                     MulDiv(g_cfg.tbItemGap, dpi, 96),
                                                     kFontMin, fontPx, maxTextW);
            fontPx = fitted;
            text_render::BuildBlock(spec, &g_cfg, &g_metrics, fontPx, g_cfg.tbLinePct,
                                    MulDiv(g_cfg.tbItemGap, dpi, 96), &out->block);
        }
        if (out->block.width <= 0 || out->block.width > maxTextW) return false;

        out->fontPx = fontPx;
        out->h = out->block.height + padY * 2;

        // 胶囊：文字左右必须让开端部圆弧，否则会贴到曲线/被裁掉
        int padXUse = padX;
        if (out->shape == 2) {
            const double r = (double)out->h * 0.5;
            const double half = (double)out->block.height * 0.5;
            int inset = 0;
            if (half > 0.0 && half < r) {
                inset = (int)(r - std::sqrt(r * r - half * half));
            }
            const int need = inset + MulDiv(4, dpi, 96);
            if (padXUse < need) padXUse = need;
        }
        out->padX = padXUse;

        int natural = out->block.width + padXUse * 2;
        // 首次按当前内容定宽，之后只增不减：避免每秒数值变化把左边缘拉来拉去。
        // 宽度只会在「位数/单位变宽」时增长，不会因为一次突发被永久撑大。
        if (g_widthHwm == 0 || natural > g_widthHwm) g_widthHwm = natural;
        int panelW = g_widthHwm;
        const int hardLimit = anchorRight - leftLimit;
        if (panelW > hardLimit) panelW = hardLimit;
        out->w = panelW;
        out->x = anchorRight - out->w;

        // 垂直：贴任务栏外侧；内容高于任务栏时向上（底部任务栏）或向下（顶部任务栏）凸出
        MONITORINFO mi;
        ZeroMemory(&mi, sizeof(mi));
        mi.cbSize = sizeof(mi);
        HMONITOR mon = MonitorFromWindow(bi.tray, MONITOR_DEFAULTTONEAREST);
        const bool barAtTop = (mon && GetMonitorInfoW(mon, &mi)) && (bi.bar.top <= mi.rcMonitor.top + 2);
        out->y = barAtTop ? bi.bar.top : (bi.bar.bottom - out->h);

        if (out->x < leftLimit) return false;
        out->radiusPx = surface_shape::RadiusPx(out->shape, out->w, out->h, out->cornerPx);
        out->useAcrylic = (out->mat == 1) && (g_cfg.tbSysAcrylic != FALSE) &&
                          glass::acrylicAvailable();
        return true;
    }

    // 纵向任务栏：宽度就是任务栏宽度，字号压到放得下；放在通知区域上方
    int fontPx = g_cfg.tbFontPx > 0 ? MulDiv(g_cfg.tbFontPx, dpi, 96) : (barW * 30 / 100);
    if (fontPx < 8) fontPx = 8;
    if (fontPx > 24) fontPx = 24;

    const int maxTextW = barW - padX * 2;
    if (maxTextW < 20) return false;

    const int gapPx = MulDiv(g_cfg.tbItemGap, dpi, 96);
    const int fitted = text_render::FitFontPx(spec, &g_cfg, &g_metrics, g_cfg.tbLinePct,
                                              gapPx, 8, fontPx, maxTextW);
    text_render::BuildBlock(spec, &g_cfg, &g_metrics, fitted, g_cfg.tbLinePct, gapPx, &out->block);
    if (out->block.width <= 0 || out->block.width > maxTextW) return false;

    const int boxH = out->block.height + padY * 2;
    if (boxH > barH) return false;

    const int anchorTop = bi.hasAnchor ? (bi.anchor.top - gap) : (bi.bar.bottom - gap);
    const int y = anchorTop - boxH;
    if (y < bi.bar.top) return false;

    out->fontPx = fitted;
    out->w = barW;
    out->h = boxH;
    out->x = bi.bar.left;
    out->y = y;
    out->radiusPx = surface_shape::RadiusPx(out->shape, out->w, out->h, out->cornerPx);
    out->useAcrylic = (out->mat == 1) && (g_cfg.tbSysAcrylic != FALSE) &&
                      glass::acrylicAvailable();
    return true;
}

void JoinText(const layout_model::LayoutBlock& spec, wchar_t* out, size_t cch)
{
    if (!out || cch == 0) return;
    out[0] = 0;
    for (int i = 0; i < spec.count; ++i) {
        wchar_t item[96];
        layout_model::itemText(spec.item[i], &g_cfg, &g_metrics, item, 96);

        // wcsncat 的 n 是「最多追加 n 个字符」，不是剩余容量：必须按实际剩余空间传
        size_t used = wcslen(out);
        if (i > 0) {
            if (used + 1 >= cch) break;
            wcsncat(out, L"|", cch - 1 - used);
            used = wcslen(out);
        }
        if (used + 1 >= cch) break;
        wcsncat(out, item, cch - 1 - used);
    }
    out[cch - 1] = 0;
}

bool LayoutMatchesCache(const PanelLayout& L, const wchar_t* text)
{
    if (!g_lastValid) return false;
    if (L.fontPx != g_lastFont) return false;
    if (L.textColor != g_lastTextColor) return false;
    if (L.tint != g_lastTint) return false;
    if (L.mat != g_lastMat) return false;
    if (L.tintAlpha != g_lastAlpha) return false;
    if (L.radiusPx != g_lastRadius) return false;
    if (!text || wcscmp(g_lastText, text) != 0) return false;
    return L.x == g_lastRect.left && L.y == g_lastRect.top &&
           L.w == (g_lastRect.right - g_lastRect.left) &&
           L.h == (g_lastRect.bottom - g_lastRect.top);
}

void SaveCache(const PanelLayout& L, const wchar_t* text)
{
    g_lastValid     = true;
    g_lastFont      = L.fontPx;
    g_lastTextColor = L.textColor;
    g_lastTint      = L.tint;
    g_lastMat       = L.mat;
    g_lastAlpha     = L.tintAlpha;
    g_lastRadius    = L.radiusPx;
    g_lastRect.left   = L.x;
    g_lastRect.top    = L.y;
    g_lastRect.right  = L.x + L.w;
    g_lastRect.bottom = L.y + L.h;
    lstrcpynW(g_lastText, text, kTextCapacity);
}

void HideWindow()
{
    if (g_hwnd && g_shown) {
        ShowWindow(g_hwnd, SW_HIDE);
        g_shown = false;
    }
    glass::hideSystemAcrylic(0);
    g_lastValid = false;   // 再次显示时强制重画，避免残留旧帧
}

void PaintPanel(Graphics& g, const PanelLayout& L, COLORREF contrastBase)
{
    // 材质（含描边/高光）已由 glass::paint 写进同一张 DIB，这里只画文字，
    // 否则会二次叠加色调，把玻璃/亚克力又变回不透明。
    const int tx = (L.w - L.block.width) / 2;
    const int ty = (L.h - L.block.height) / 2;
    // contrastBase 是文字底下的真实颜色（采样成功时），否则退回底色近似
    const bool halo = (L.mat == 2) || (L.mat == 3) || (L.mat == 4) ||
                      (render::contrastRatio(L.textColor, contrastBase) < 7.5);
    text_render::DrawBlock(g, L.block, tx, ty, L.block.width, L.block.height,
                           L.textColor, halo);
}

// 材质 + 文字写进分层窗口。resolveText=true 时按真实呈现色重新解析文字色（正常重绘）；
// false 时直接复用 L 里已解析的颜色（液态玻璃动画帧，避免每帧重新采样背景）。
bool PaintFrame(const PanelLayout& L, const glass::MaterialSpec& spec, bool sys,
                bool resolveText)
{
    if (!EnsureBuffer(L.w, L.h)) return false;
    memset(g_bits, 0, (size_t)g_stride * (size_t)L.h);

    RECT rc = {L.x, L.y, L.x + L.w, L.y + L.h};
    const int dpi = (int)DpiOf(g_hwnd);

    BOOL hidSelf = FALSE;
    if (sys) {
        glass::MaterialSpec textSpec = spec;
        textSpec.type = glass::MAT_NONE;
        textSpec.concentration = 0;
        glass::paint(0, nullptr, rc, dpi, textSpec, g_bits, g_stride, L.w, L.h, &hidSelf);
    } else {
        glass::paint(0, g_hwnd, rc, dpi, spec, g_bits, g_stride, L.w, L.h, &hidSelf);
    }

    // 自动文字色：材质已经写进 DIB，这里按「文字底下的真实呈现色」重新判定黑/白，
    // 否则深色任务栏 + 浅色底会沿用 BuildLayout 里的启发式而选成黑字。
    // 无底板（mat=4）/系统亚克力没有表面像素，沿用主题色/启发式色。
    PanelLayout paintL = L;
    COLORREF contrastBase = resolveText ? L.tint : g_lastPaintBase;
    if (resolveText && g_cfg.tbTextAuto && L.mat != 4 && !sys) {
        const int tx = (L.w - L.block.width) / 2;
        const int ty = (L.h - L.block.height) / 2;
        const RECT textArea = { tx, ty, tx + L.block.width, ty + L.block.height };

        // 亚克力/玻璃/液态玻璃的材质抓屏刚做完，直接命中缓存（不额外抓屏）；
        // 纯色没有材质抓屏，允许按需抓一帧（几何不变时也只抓一次）。
        render::TextBackdrop bd = {};
        COLORREF bg = 0;
        const BOOL allowCapture = (L.mat == glass::MAT_SOLID) ? TRUE : FALSE;
        if (glass::backdropColor(0, g_hwnd, rc, dpi, L.w, L.h, L.shape, L.cornerPx,
                                 allowCapture, &bg)) {
            bd.color = bg;
            bd.valid = TRUE;
        }

        COLORREF presented = 0;
        BOOL havePresented = FALSE;
        paintL.textColor = render::resolveAutoTextColor(
            g_bits, g_stride, L.w, L.h, textArea, &bd,
            L.tint, L.tintAlpha, &presented, &havePresented);
        if (havePresented) contrastBase = presented;
    }

    {
        Bitmap canvas(L.w, L.h, g_stride, PixelFormat32bppPARGB, g_bits);
        Graphics g(&canvas);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
        g.SetCompositingMode(CompositingModeSourceOver);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        PaintPanel(g, paintL, contrastBase);
    }

    // 液态玻璃：先存「底图 + 文字」的一帧（无流光），再叠当前时刻的流光。
    // 后续动画帧只拷贝这一帧 + 重叠流光，不再重排/重画文字。
    const bool liquid = (!sys && L.mat == glass::MAT_LIQUID);
    if (liquid) {
        const size_t bytes = (size_t)g_stride * (size_t)L.h;
        g_liquidFrame.resize(bytes);
        memcpy(g_liquidFrame.data(), g_bits, bytes);
        g_liquidFrameValid  = true;
        g_liquidFrameW      = L.w;
        g_liquidFrameH      = L.h;
        g_liquidFrameStride = g_stride;
        glass::addLiquidBand(g_bits, g_stride, L.w, L.h);
    } else {
        g_liquidFrameValid = false;
    }

    if (!PresentLayered(L.x, L.y, L.w, L.h)) return false;

    if (resolveText) {
        g_lastPaint      = paintL;
        g_lastPaintBase  = contrastBase;
        g_lastPaintValid = true;
    }
    return true;
}

bool ApplyLayout(const PanelLayout& L)
{
    wchar_t text[kTextCapacity];
    JoinText(L.spec, text, kTextCapacity);

    glass::MaterialSpec spec;
    ZeroMemory(&spec, sizeof(spec));
    spec.type          = L.mat;
    spec.shape         = L.shape;
    spec.cornerPx      = L.cornerPx;
    spec.blurPx        = L.blurPx;
    spec.tint          = L.tint;
    spec.concentration = L.tintAlpha;
    spec.border        = L.border ? TRUE : FALSE;
    spec.sheen         = L.sheen ? TRUE : FALSE;
    spec.systemAcrylic = L.useAcrylic ? TRUE : FALSE;

    RECT rc = {L.x, L.y, L.x + L.w, L.y + L.h};
    const int dpi = (int)DpiOf(g_hwnd);

    if (g_shown && LayoutMatchesCache(L, text)) {
        // 位置/文本没变：只维持 z 序与系统亚克力底窗跟随
        if (L.useAcrylic) {
            glass::placeSystemAcrylic(0, g_hwnd, g_hwnd, rc, dpi, spec);
        } else {
            glass::hideSystemAcrylic(0);
        }
        return true;
    }

    // 先把窗口放到目标位置；glass::paint 在可见状态下用掩码补全抓屏，不再隐藏窗口
    if (!EnsureBuffer(L.w, L.h)) return false;
    SetWindowPos(g_hwnd, HWND_TOPMOST, L.x, L.y, L.w, L.h, SWP_NOACTIVATE);

    bool sys = L.useAcrylic;
    if (sys) {
        if (!glass::placeSystemAcrylic(0, g_hwnd, g_hwnd, rc, dpi, spec)) {
            sys = false;
        }
    } else {
        glass::hideSystemAcrylic(0);
    }

    if (!PaintFrame(L, spec, sys, true)) return false;

    if (!g_shown) {
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        g_shown = true;
    }

    SaveCache(L, text);
    return true;
}

// ------------------------------------------------------------------ 窗口过程

LRESULT CALLBACK TaskbarWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;

    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED: {
        g_lastValid  = false;
        g_widthHwm   = 0;
        glass::invalidateBackdrop(0);
        taskbar::sync();
        return 0;
    }

    case WM_DPICHANGED:
        g_lastValid  = false;
        g_widthHwm   = 0;
        glass::invalidateBackdrop(0);
        taskbar::sync();
        return 0;

    case WM_DESTROY:
        if (g_hwnd == hwnd) g_hwnd = nullptr;
        g_shown     = false;
        g_lastValid = false;
        glass::hideSystemAcrylic(0);
        return 0;

    default:
        break;
    }

    if (g_taskbarCreatedMsg != 0 && msg == g_taskbarCreatedMsg) {
        // Explorer 重启：任务栏窗口换了，重新定位并重申置顶
        g_lastValid  = false;
        g_widthHwm   = 0;
        glass::invalidateBackdrop(0);
        taskbar::sync();
        if (g_hwnd && IsWindow(g_hwnd)) {
            SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool RegisterTaskbarClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = TaskbarWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = WND_CLASS_TASKBAR;

    WNDCLASSEXW exist;
    ZeroMemory(&exist, sizeof(exist));
    exist.cbSize = sizeof(exist);
    if (GetClassInfoExW(hInst, WND_CLASS_TASKBAR, &exist)) return true;

    return RegisterClassExW(&wc) != 0;
}

void ClampConfig(AppConfig* c)
{
    layout_model::clampConfig(c, layout_model::SURF_TASKBAR);
}

} // namespace

// ============================================================================
// 契约实现：taskbar
// ============================================================================

bool taskbar::create(HINSTANCE hInst, const AppConfig* c)
{
    if (g_hwnd) return true;

    g_inst = hInst ? hInst : (HINSTANCE)GetModuleHandleW(nullptr);

    if (c) g_cfg = *c;
    else   ZeroMemory(&g_cfg, sizeof(g_cfg));
    ClampConfig(&g_cfg);

    ZeroMemory(&g_metrics, sizeof(g_metrics));
    g_haveMetrics = false;
    g_lastValid   = false;
    g_shown       = false;

    if (!RegisterTaskbarClass(g_inst)) return false;

    if (g_taskbarCreatedMsg == 0) {
        g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    }

    const DWORD ex = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST |
                     WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;

    HWND hwnd = CreateWindowExW(ex, WND_CLASS_TASKBAR, L"", WS_POPUP,
                                0, 0, 1, 1,
                                nullptr, nullptr, g_inst, nullptr);
    if (!hwnd) return false;

    g_hwnd = hwnd;
    sync();                    // 首次定位；不满足条件时保持隐藏
    return true;
}

void taskbar::destroy()
{
    if (g_hwnd && IsWindow(g_hwnd)) {
        DestroyWindow(g_hwnd);
    }
    g_hwnd      = nullptr;
    g_shown     = false;
    g_lastValid = false;
    glass::hideSystemAcrylic(0);
    DestroyBuffer();
}

void taskbar::applyConfig(const AppConfig* c)
{
    if (!c) return;

    const int oldFont   = g_cfg.tbFontPx;
    const int oldLine   = g_cfg.tbLinePct;
    const int oldGap    = g_cfg.tbItemGap;
    const int oldShape  = g_cfg.tbShape;
    const int oldCorner = g_cfg.tbCorner;
    wchar_t   oldLayout[NETBALL_SPEC_CCH] = {0};
    lstrcpynW(oldLayout, g_cfg.tbLayout, NETBALL_SPEC_CCH);

    g_cfg = *c;
    ClampConfig(&g_cfg);

    // 只有排版/字号/形状会改变面板几何，才重置宽度高水位；颜色/材质/模糊等
    // 外观滑块变化只重合成材质，不重抓屏、也不让左边缘跳动。
    if (oldFont != g_cfg.tbFontPx || oldLine != g_cfg.tbLinePct ||
        oldGap != g_cfg.tbItemGap || oldShape != g_cfg.tbShape ||
        oldCorner != g_cfg.tbCorner || wcscmp(oldLayout, g_cfg.tbLayout) != 0) {
        g_widthHwm = 0;
    }

    g_lastValid = false;        // 颜色/文字色/材质变化都要重画
    sync();
}

void taskbar::setMetrics(const Metrics* m)
{
    if (!m) return;
    g_metrics     = *m;
    g_haveMetrics = true;
    (void)g_haveMetrics;
    sync();
}

void taskbar::sync()
{
    if (!g_hwnd || !IsWindow(g_hwnd)) return;

    if (!g_cfg.taskbarShow) {
        HideWindow();
        return;
    }

    BarInfo bi;
    if (!FindTaskbar(&bi)) {
        HideWindow();
        return;
    }

    if (TaskbarAutoHidden(bi) || FullscreenActive()) {
        HideWindow();
        return;
    }

    PanelLayout layout;
    if (!BuildLayout(bi, &layout)) {
        HideWindow();
        return;
    }

    if (!ApplyLayout(layout)) {
        HideWindow();
    }
}

void taskbar::refreshMaterial()
{
    if (!g_hwnd || !IsWindow(g_hwnd) || !g_cfg.taskbarShow || !g_shown) return;

    // 任务栏面板尺寸小、背景基本静止：跟随 500ms 定时器但每两次才刷新一次（1s），
    // 把实时材质的 CPU 主要留给悬浮球。
    static unsigned s_tick = 0;
    if ((++s_tick % 2u) != 0u) return;

    const int mat = g_cfg.tbMat;
    if (mat != glass::MAT_ACRYLIC && mat != glass::MAT_GLASS && mat != glass::MAT_LIQUID) {
        return;
    }
    if (mat == glass::MAT_ACRYLIC && g_cfg.tbSysAcrylic &&
        glass::acrylicAvailable()) {
        return;
    }

    g_lastValid = false;
    glass::invalidateBackdrop(0);
    sync();
}

// 液态玻璃动画帧：拷贝缓存帧 + 叠流光 + 呈现，不重建材质、不重画文字。
void taskbar::animateLiquid()
{
    if (!g_hwnd || !IsWindow(g_hwnd) || !g_cfg.taskbarShow || !g_shown) return;
    if (!g_lastPaintValid || g_lastPaint.mat != glass::MAT_LIQUID) return;
    if (g_lastPaint.useAcrylic) return;

    if (g_liquidFrameValid && g_bits && !g_liquidFrame.empty() &&
        g_liquidFrameW == g_lastPaint.w && g_liquidFrameH == g_lastPaint.h &&
        g_liquidFrameStride == g_stride) {
        memcpy(g_bits, g_liquidFrame.data(),
               (size_t)g_liquidFrameStride * (size_t)g_liquidFrameH);
        glass::addLiquidBand(g_bits, g_stride, g_liquidFrameW, g_liquidFrameH);
        PresentLayered(g_lastPaint.x, g_lastPaint.y, g_lastPaint.w, g_lastPaint.h);
        return;
    }

    // 缓存不可用（刚显示/尺寸变了）：退回一次完整重绘，顺便重建缓存。
    glass::MaterialSpec spec;
    ZeroMemory(&spec, sizeof(spec));
    spec.type          = g_lastPaint.mat;
    spec.shape         = g_lastPaint.shape;
    spec.cornerPx      = g_lastPaint.cornerPx;
    spec.blurPx        = g_lastPaint.blurPx;
    spec.tint          = g_lastPaint.tint;
    spec.concentration = g_lastPaint.tintAlpha;
    spec.border        = g_lastPaint.border ? TRUE : FALSE;
    spec.sheen         = g_lastPaint.sheen ? TRUE : FALSE;
    spec.systemAcrylic = FALSE;

    PaintFrame(g_lastPaint, spec, false, false);
}

HWND taskbar::hwnd()
{
    return g_hwnd;
}
