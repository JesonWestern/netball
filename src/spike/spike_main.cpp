// src/spike/spike_main.cpp —— T1 技术验证 spike（临时文件，M6 前保留）
//
// 验证目标：
//   1. MinGW-w64 g++ 13.1.0 + GDI+ + UpdateLayeredWindow 32bpp DIB per-pixel alpha 可编译可运行
//   2. 200x200 分层窗口，四角完全透明（alpha=0）→ 无黑边/黑底
//   3. GDI+ 抗锯齿圆 + 白色文字 "↓ 1.2 MB/s" 边缘有部分 alpha 过渡像素
//   4. 鼠标左键拖动、右键弹出退出菜单、托盘图标可见
//
// 两种模式：
//   spike.exe              正常 GUI 模式（浮球显示，写 spike_gui.txt 记录样式/托盘结果）
//   spike.exe --selftest   无人值守自检（逐像素校验 + UpdateLayeredWindow 返回值，写 spike_selftest.txt）
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>

using namespace Gdiplus;

namespace {

const int   kSize      = 200;          // 窗口边长
const UINT  kTrayMsg   = WM_APP + 1;   // 托盘回调消息
const UINT  kMenuExit  = 40001;        // 退出菜单项
const int   kCircleAlpha = 210;        // 圆的整体不透明度（20..255）

HWND      g_hwnd      = nullptr;
ULONG_PTR g_token     = 0;
bool      g_dragging  = false;
POINT     g_dragCursor = {0, 0};
POINT     g_dragWin    = {0, 0};
bool      g_trayAdded = false;
NOTIFYICONDATAW g_nid = {};
bool      g_selfTest  = false;
wchar_t   g_exeDir[MAX_PATH] = {0};

// ---------------------------------------------------------------- 绘制

// 把浮球内容画到 32bpp 预乘 ARGB 位图上。w/h 为位图尺寸。
void DrawContent(Graphics& g, int w, int h)
{
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    // 透明背景上不能用 ClearType，只能用灰度抗锯齿
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

    const REAL m = 8.0f;
    RectF circle(m, m, (REAL)w - 2 * m, (REAL)h - 2 * m);

    SolidBrush fill(Color(kCircleAlpha, 24, 96, 200));   // 半透明蓝
    g.FillEllipse(&fill, circle);

    Pen pen(Color(255, 150, 210, 255), 2.0f);            // 边缘高光描边
    g.DrawEllipse(&pen, circle);

    Font font(L"Segoe UI", 22.0f, FontStyleBold, UnitPixel);
    SolidBrush textBrush(Color(255, 255, 255, 255));     // 纯白文字
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    RectF textRect(0.0f, 0.0f, (REAL)w, (REAL)h);
    g.DrawString(L"\x2193 1.2 MB/s", -1, &font, textRect, &sf, &textBrush);
}

// ---------------------------------------------------------------- 分层窗口绘制

// 用 UpdateLayeredWindow 提交 32bpp DIB（per-pixel alpha）。返回是否成功。
bool PaintLayered(HWND hwnd)
{
    if (!hwnd) return false;

    RECT wr;
    if (!GetWindowRect(hwnd, &wr)) return false;
    const int w = wr.right - wr.left;
    const int h = wr.bottom - wr.top;
    if (w <= 0 || h <= 0) return false;

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h;      // 负高度 = top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) return false;

    void*   bits   = nullptr;
    HBITMAP dib    = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC     memDc  = CreateCompatibleDC(screenDc);
    bool    ok     = false;

    if (dib && memDc && bits) {
        HGDIOBJ oldBmp = SelectObject(memDc, dib);
        ZeroMemory(bits, (size_t)w * (size_t)h * 4);   // 全透明

        {
            // 直接包住 DIB 内存绘制，GDI+ 输出的就是预乘 ARGB，正好是 ULW 需要的格式
            Bitmap bmp(w, h, w * 4, PixelFormat32bppPARGB, (BYTE*)bits);
            Graphics g(&bmp);
            DrawContent(g, w, h);
        }

        POINT ptSrc = {0, 0};
        POINT ptDst = {wr.left, wr.top};
        SIZE  size  = {w, h};
        BLENDFUNCTION bf;
        bf.BlendOp             = AC_SRC_OVER;
        bf.BlendFlags          = 0;
        bf.SourceConstantAlpha = 255;   // 透明度已烘焙进 per-pixel alpha
        bf.AlphaFormat         = AC_SRC_ALPHA;

        ok = UpdateLayeredWindow(hwnd, screenDc, &ptDst, &size,
                                 memDc, &ptSrc, 0, &bf, ULW_ALPHA) != FALSE;

        SelectObject(memDc, oldBmp);
    }

    if (memDc) DeleteDC(memDc);
    if (dib)   DeleteObject(dib);
    ReleaseDC(nullptr, screenDc);
    return ok;
}

// ---------------------------------------------------------------- 托盘

bool AddTray(HWND hwnd)
{
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = kTrayMsg;
    g_nid.hIcon            = (HICON)LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON, 16, 16, LR_SHARED);
    lstrcpynW(g_nid.szTip, L"NetBall T1 spike", (int)(sizeof(g_nid.szTip) / sizeof(wchar_t)));
    return Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}

void ShowExitMenu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuExit, L"\x9000\x51fa");   // 退出
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

// ---------------------------------------------------------------- 窗口过程

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN: {
        SetCapture(hwnd);
        g_dragging = true;
        POINT pt;
        GetCursorPos(&pt);
        RECT r;
        GetWindowRect(hwnd, &r);
        g_dragWin    = POINT{r.left, r.top};
        g_dragCursor = pt;
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_dragging) {
            POINT pt;
            GetCursorPos(&pt);
            const int nx = g_dragWin.x + (pt.x - g_dragCursor.x);
            const int ny = g_dragWin.y + (pt.y - g_dragCursor.y);
            SetWindowPos(hwnd, nullptr, nx, ny, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_dragging) {
            g_dragging = false;
            ReleaseCapture();
        }
        return 0;
    case WM_RBUTTONUP:
        ShowExitMenu(hwnd);
        return 0;
    case kTrayMsg:
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU)
            ShowExitMenu(hwnd);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == kMenuExit) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_trayAdded) {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            g_trayAdded = false;
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- 公共

bool RegisterBallClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;             // 分层窗口自己画，不需要背景刷
    wc.lpszClassName = L"NetBallSpikeWnd";
    return RegisterClassExW(&wc) != 0;
}

HWND CreateBallWindow(HINSTANCE hInst, int x, int y)
{
    return CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                           L"NetBallSpikeWnd", L"NetBall Spike",
                           WS_POPUP,
                           x, y, kSize, kSize,
                           nullptr, nullptr, hInst, nullptr);
}

void EnablePerMonitorV2()
{
    typedef BOOL (WINAPI* SetCtxFn)(DPI_AWARENESS_CONTEXT);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    SetCtxFn fn = (SetCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (fn) fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

FILE* OpenReport(const wchar_t* name)
{
    wchar_t path[MAX_PATH];
    lstrcpynW(path, g_exeDir, MAX_PATH);
    lstrcatW(path, L"\\");
    lstrcatW(path, name);
    return _wfopen(path, L"w");
}

// ---------------------------------------------------------------- 自检

int RunSelfTest(HINSTANCE hInst)
{
    FILE* f = OpenReport(L"spike_selftest.txt");
    int   fails = 0;

    auto log = [&](const wchar_t* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        if (f) vfwprintf(f, fmt, ap);
        va_end(ap);
    };

    log(L"NetBall T1 spike selftest\n");

    // 1) 离屏位图：抗锯齿 + 预乘 alpha 正确性
    const int w = kSize, h = kSize;
    Bitmap bmp(w, h, PixelFormat32bppPARGB);
    {
        Graphics g(&bmp);
        g.Clear(Color(0, 0, 0, 0));
        DrawContent(g, w, h);
    }

    BitmapData bd;
    Rect lockRect(0, 0, w, h);
    if (bmp.LockBits(&lockRect, ImageLockModeRead, PixelFormat32bppPARGB, &bd) != Ok) {
        log(L"[FAIL] LockBits\n");
        if (f) fclose(f);
        return 1;
    }

    const BYTE* p0 = (const BYTE*)bd.Scan0;
    auto px = [&](int x, int y) -> const BYTE* {
        return p0 + (size_t)y * bd.Stride + (size_t)x * 4;
    };

    // 1a. 四角 alpha 必须为 0（否则 ULW 会出现黑边/黑底）
    int cornerBad = 0;
    const int cx[4] = {0, w - 1, 0, w - 1};
    const int cy[4] = {0, 0, h - 1, h - 1};
    for (int i = 0; i < 4; ++i) {
        const BYTE* q = px(cx[i], cy[i]);
        if (q[3] != 0) ++cornerBad;
    }
    log(L"[%ls] corners alpha==0  bad=%d/4\n", cornerBad == 0 ? L"PASS" : L"FAIL", cornerBad);
    if (cornerBad) ++fails;

    // 1b. 预乘约束：每个通道值 <= alpha（违例即灰边/黑边来源）
    int premulBad = 0;
    int partialAlpha = 0;   // 0<a<255 的像素数 = 抗锯齿有效
    int opaqueWhite = 0;    // 白色文字像素数
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const BYTE* q = px(x, y);
            const int a = q[3];
            if (q[0] > a || q[1] > a || q[2] > a) ++premulBad;
            if (a > 0 && a < 255) ++partialAlpha;
            if (a >= 250 && q[0] >= 250 && q[1] >= 250 && q[2] >= 250) ++opaqueWhite;
        }
    }
    log(L"[%ls] premultiplied r,g,b<=a  violations=%d\n", premulBad == 0 ? L"PASS" : L"FAIL", premulBad);
    if (premulBad) ++fails;

    log(L"[%ls] antialias partial-alpha pixels=%d (expect >500)\n",
        partialAlpha > 500 ? L"PASS" : L"FAIL", partialAlpha);
    if (partialAlpha <= 500) ++fails;

    log(L"[%ls] white text pixels=%d (expect >100)\n",
        opaqueWhite > 100 ? L"PASS" : L"FAIL", opaqueWhite);
    if (opaqueWhite <= 100) ++fails;

    // 1c. 圆心不透明度 = kCircleAlpha（20..100% 真实 alpha 的基础）
    const BYTE* c = px(w / 2, h / 2);
    log(L"[%ls] center alpha=%d (expect %d)\n",
        (c[3] >= kCircleAlpha - 12 && c[3] <= kCircleAlpha + 12) ? L"PASS" : L"FAIL", c[3], kCircleAlpha);
    if (!(c[3] >= kCircleAlpha - 12 && c[3] <= kCircleAlpha + 12)) ++fails;

    bmp.UnlockBits(&bd);

    // 2) 分层窗口路径：UpdateLayeredWindow 必须返回 TRUE
    if (RegisterBallClass(hInst)) {
        HWND hwnd = CreateBallWindow(hInst, 400, 300);
        if (hwnd) {
            const bool okLayered = PaintLayered(hwnd);
            LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            log(L"[%ls] UpdateLayeredWindow -> %d\n", okLayered ? L"PASS" : L"FAIL", okLayered ? 1 : 0);
            if (!okLayered) ++fails;
            const bool layered   = (ex & WS_EX_LAYERED) != 0;
            const bool toolwin   = (ex & WS_EX_TOOLWINDOW) != 0;
            const bool topmost   = (ex & WS_EX_TOPMOST) != 0;
            log(L"[%ls] exstyle LAYERED=%d TOOLWINDOW=%d TOPMOST=%d (raw=0x%IX)\n",
                (layered && toolwin && topmost) ? L"PASS" : L"FAIL",
                layered ? 1 : 0, toolwin ? 1 : 0, topmost ? 1 : 0, (unsigned __int64)ex);
            if (!(layered && toolwin && topmost)) ++fails;

            DestroyWindow(hwnd);
        } else {
            log(L"[FAIL] CreateWindowEx\n");
            ++fails;
        }
    } else {
        log(L"[FAIL] RegisterClassEx\n");
        ++fails;
    }

    log(L"RESULT: %ls  fails=%d\n", fails == 0 ? L"PASS" : L"FAIL", fails);
    if (f) fclose(f);
    return fails == 0 ? 0 : 1;
}

} // namespace helpers

// ---------------------------------------------------------------- 入口
// 注意：-municode 需要外部链接的 wWinMain，不能放在匿名命名空间里

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int)
{
    if (lpCmdLine && wcsstr(lpCmdLine, L"--selftest"))
        g_selfTest = true;

    // 记录 exe 所在目录，报告文件写在那里
    GetModuleFileNameW(nullptr, g_exeDir, MAX_PATH);
    {
        wchar_t* p = wcsrchr(g_exeDir, L'\\');
        if (p) *p = L'\0';
    }

    if (!g_selfTest)
        EnablePerMonitorV2();

    GdiplusStartupInput gsi;
    if (GdiplusStartup(&g_token, &gsi, nullptr) != Ok) return 2;

    int rc = 0;
    if (g_selfTest) {
        rc = RunSelfTest(hInst);
    } else if (RegisterBallClass(hInst)) {
        g_hwnd = CreateBallWindow(hInst, 400, 300);
        if (g_hwnd) {
            const bool layeredOk = PaintLayered(g_hwnd);
            ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
            g_trayAdded = AddTray(g_hwnd);

            // 写一次运行状态，作为无法截图时的机读证据
            if (FILE* f = OpenReport(L"spike_gui.txt")) {
                RECT r;
                GetWindowRect(g_hwnd, &r);
                LONG_PTR ex = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
                fwprintf(f, L"hwnd=0x%p\n", (void*)g_hwnd);
                fwprintf(f, L"rect=%ld,%ld,%ld,%ld\n", r.left, r.top, r.right, r.bottom);
                fwprintf(f, L"exstyle=0x%IX layered=%d toolwindow=%d topmost=%d\n",
                         (unsigned __int64)ex,
                         (ex & WS_EX_LAYERED) ? 1 : 0,
                         (ex & WS_EX_TOOLWINDOW) ? 1 : 0,
                         (ex & WS_EX_TOPMOST) ? 1 : 0);
                fwprintf(f, L"updated_layered=%d\n", layeredOk ? 1 : 0);
                fwprintf(f, L"tray_icon_added=%d\n", g_trayAdded ? 1 : 0);
                fclose(f);
            }

            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            rc = 3;
        }
    } else {
        rc = 4;
    }

    GdiplusShutdown(g_token);
    return rc;
}
