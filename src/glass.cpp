// src/glass.cpp —— 表面材质合成器（v1.4.0 重写）
//
// 主路径（自绘材质）：
//   可见状态直接 BitBlt 抓取「表面矩形 + 模糊边距」→ 用形状 SDF 掩码把球体自身
//   区域按行/列干净像素补全 → 3 次可分离盒式模糊逼近高斯 → 饱和度/色调/透射 →
//   亚克力颗粒 / 玻璃高光与边缘折射 / 液态玻璃高光带与色散 → SDF 形状遮罩 →
//   预乘 ARGB → GDI+ 描边/高光叠加。
//
// 为什么不再 ShowWindow(SW_HIDE)：
//   隐藏持有鼠标捕获的窗口会触发 WM_CAPTURECHANGED，拖动直接中断；同时也会闪。
//   v1.4.1 改成「掩码补全」后，可见/拖动中都能每 500ms 刷新背景，不丢捕获。
//   首次绘制时窗口本就隐藏，直接抓干净背景，不需要补全。
//
// 背景缓存与材质缓存分开：抓屏代次变化才重模糊/重合成；色调/浓度变化只重合成。
//
// 系统亚克力（实验性）：
//   独立非分层底窗 + SetWindowCompositionAttribute。关键顺序是「先 accent、后
//   region」，且每次 SetWindowPos 后补一次 region；否则 Win11 会把胶囊/圆角
//   重置成矩形。Win11 对非激活窗口的 accent 可能退化为纯色，调用方需回退自绘。
#include "glass.h"
#include "surface_shape.h"
#include "surface_style.h"

#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

// ---------------------------------------------------------------- 能力探测

bool g_probed  = false;
bool g_acrylic = false;

bool TransparencyEnabled()
{
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                      0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS) {
        return true;   // 老系统没有这个键，按开启处理
    }
    DWORD v = 1, cb = sizeof(v), type = 0;
    const LONG rc = RegQueryValueExW(hk, L"EnableTransparency", nullptr, &type,
                                     (LPBYTE)&v, &cb);
    RegCloseKey(hk);
    if (rc != ERROR_SUCCESS || type != REG_DWORD) return true;
    return v != 0;
}

void Probe()
{
    if (g_probed) return;
    g_probed = true;

    BOOL comp = FALSE;
    if (FAILED(DwmIsCompositionEnabled(&comp)) || !comp) return;
    if (!TransparencyEnabled()) return;

    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (!u32) return;
    if (!GetProcAddress(u32, "SetWindowCompositionAttribute")) return;

    g_acrylic = true;
}

// ---------------------------------------------------------------- 小工具

int ClampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

float ClampF(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

BYTE ClampByteFromFloat(float v)
{
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return (BYTE)(v + 0.5f);
}

inline unsigned Hash2(int x, int y, unsigned seed)
{
    unsigned h = (unsigned)x * 374761393u
               ^ (unsigned)y * 668265263u
               ^ seed * 362437u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

inline float Noise01(int x, int y, unsigned seed)
{
    return (float)(Hash2(x, y, seed) & 0xFFFFu) / 65535.0f;
}

// ---------------------------------------------------------------- 抓屏缓存

struct BackdropCache {
    bool     valid;
    RECT     rect;
    int      dpi;
    int      w, h;
    int      margin;
    int      shape, corner;
    int      capW, capH;
    unsigned seq;
    bool     avgValid;              // 表面矩形均色是否已算（自动文字色用）
    COLORREF avgColor;
    std::vector<BYTE> pixels;       // 扩展后的背景，BGRA，top-down
};

BackdropCache g_backdrop[2] = {};
unsigned     g_seqCounter = 0;

void ExtendEdges(BYTE* px, int capW, int capH, int x0, int y0, int cw, int ch)
{
    if (!px || capW <= 0 || capH <= 0 || cw <= 0 || ch <= 0) return;
    const size_t stride = (size_t)capW * 4u;

    // 先横向把有效区域补满
    for (int y = y0; y < y0 + ch; ++y) {
        BYTE* row = px + (size_t)y * stride;
        const BYTE* left  = row + (size_t)x0 * 4u;
        const BYTE* right = row + (size_t)(x0 + cw - 1) * 4u;
        for (int x = 0; x < x0; ++x) {
            memcpy(row + (size_t)x * 4u, left, 4u);
        }
        for (int x = x0 + cw; x < capW; ++x) {
            memcpy(row + (size_t)x * 4u, right, 4u);
        }
    }

    // 再纵向复制首尾行
    for (int y = 0; y < y0; ++y) {
        memcpy(px + (size_t)y * stride, px + (size_t)y0 * stride, stride);
    }
    for (int y = y0 + ch; y < capH; ++y) {
        memcpy(px + (size_t)y * stride, px + (size_t)(y0 + ch - 1) * stride, stride);
    }
}

// 可见状态下抓屏：球体自身会出现在截图里。这里用形状 SDF 生成掩码，
// 再用行/列两个方向的干净像素做线性插值，把球体区域“抹掉”。
// 结果不需要隐藏窗口，因此不会触发 WM_CAPTURECHANGED，也不会闪。
void InpaintSurfaceArea(BYTE* px, int capW, int capH,
                        int sx, int sy, int sw, int sh,
                        int shape, int cornerPx)
{
    if (!px || capW <= 0 || capH <= 0 || sw <= 0 || sh <= 0) return;

    const int inflate = 4;
    const int bx0 = ClampInt(sx - inflate, 0, capW);
    const int by0 = ClampInt(sy - inflate, 0, capH);
    const int bx1 = ClampInt(sx + sw + inflate, 0, capW);
    const int by1 = ClampInt(sy + sh + inflate, 0, capH);
    if (bx1 <= bx0 || by1 <= by0) return;

    const float hw = (float)sw * 0.5f;
    const float hh = (float)sh * 0.5f;
    const float rr = (float)surface_shape::RadiusPx(shape, sw, sh, cornerPx);

    auto masked = [&](int x, int y) -> bool {
        const float d = surface_shape::Distance(
            shape, (float)(x - sx) + 0.5f, (float)(y - sy) + 0.5f, hw, hh, rr);
        return d < (float)inflate;
    };

    const size_t capStride = (size_t)capW * 4u;

    // 干净环的平均色：极端情况下（球贴屏幕边）行列都找不到干净像素时兜底。
    long long sumB = 0, sumG = 0, sumR = 0;
    int samples = 0;
    const int ring = 14;
    for (int y = (std::max)(0, by0 - ring); y < (std::min)(capH, by1 + ring); y += 2) {
        for (int x = (std::max)(0, bx0 - ring); x < (std::min)(capW, bx1 + ring); x += 2) {
            if (x >= bx0 && x < bx1 && y >= by0 && y < by1) continue;
            const BYTE* p = px + (size_t)y * capStride + (size_t)x * 4u;
            sumB += p[0]; sumG += p[1]; sumR += p[2];
            ++samples;
        }
    }
    const BYTE avgB = samples ? (BYTE)(sumB / samples) : 0;
    const BYTE avgG = samples ? (BYTE)(sumG / samples) : 0;
    const BYTE avgR = samples ? (BYTE)(sumR / samples) : 0;
    const BYTE avg[3] = { avgB, avgG, avgR };

    const int fw = bx1 - bx0;
    const int fh = by1 - by0;
    // 复用容量：每 500ms 刷新一次，避免反复分配两块 ~100KB 的缓冲。
    static std::vector<BYTE> hFillStore;
    static std::vector<BYTE> vFillStore;
    const size_t fillBytes = (size_t)fw * (size_t)fh * 4u;
    hFillStore.assign(fillBytes, 0);
    vFillStore.assign(fillBytes, 0);
    std::vector<BYTE>& hFill = hFillStore;
    std::vector<BYTE>& vFill = vFillStore;

    auto writeFill = [&](std::vector<BYTE>& buf, int x, int y,
                         const BYTE* left, const BYTE* right, float t) {
        BYTE* d = buf.data() + (((size_t)(y - by0) * (size_t)fw + (size_t)(x - bx0)) * 4u);
        for (int c = 0; c < 3; ++c) {
            const float l = left ? (float)left[c] : (float)avg[c];
            const float r = right ? (float)right[c] : (float)avg[c];
            d[c] = (BYTE)(l + (r - l) * t + 0.5f);
        }
        d[3] = 255;
    };

    // 横向：每一行把被遮住的连续段用左右干净像素插值
    for (int y = by0; y < by1; ++y) {
        int runStart = -1;
        for (int x = bx0; x <= bx1; ++x) {
            const bool inside = (x < bx1) && masked(x, y);
            if (inside) {
                if (runStart < 0) runStart = x;
                continue;
            }
            if (runStart >= 0) {
                const int lx = runStart - 1;
                const int rx = x;
                const BYTE* left  = (lx >= 0)   ? px + (size_t)y * capStride + (size_t)lx * 4u : nullptr;
                const BYTE* right = (rx < capW) ? px + (size_t)y * capStride + (size_t)rx * 4u : nullptr;
                const float denom = (float)(rx - lx);
                for (int xx = runStart; xx < x; ++xx) {
                    const float t = (denom > 0.0f) ? ((float)(xx - lx) / denom) : 0.5f;
                    writeFill(hFill, xx, y, left, right, t);
                }
                runStart = -1;
            }
        }
    }

    // 纵向：每一列同理
    for (int x = bx0; x < bx1; ++x) {
        int runStart = -1;
        for (int y = by0; y <= by1; ++y) {
            const bool inside = (y < by1) && masked(x, y);
            if (inside) {
                if (runStart < 0) runStart = y;
                continue;
            }
            if (runStart >= 0) {
                const int ty = runStart - 1;
                const int by = y;
                const BYTE* top    = (ty >= 0)   ? px + (size_t)ty * capStride + (size_t)x * 4u : nullptr;
                const BYTE* bottom = (by < capH) ? px + (size_t)by * capStride + (size_t)x * 4u : nullptr;
                const float denom = (float)(by - ty);
                for (int yy = runStart; yy < y; ++yy) {
                    const float t = (denom > 0.0f) ? ((float)(yy - ty) / denom) : 0.5f;
                    writeFill(vFill, x, yy, top, bottom, t);
                }
                runStart = -1;
            }
        }
    }

    // 合并两个方向；都不可用时用干净环平均色
    for (int y = by0; y < by1; ++y) {
        for (int x = bx0; x < bx1; ++x) {
            if (!masked(x, y)) continue;
            const size_t fi = ((size_t)(y - by0) * (size_t)fw + (size_t)(x - bx0)) * 4u;
            const BYTE* h = hFill.data() + fi;
            const BYTE* v = vFill.data() + fi;
            BYTE* p = px + (size_t)y * capStride + (size_t)x * 4u;
            const bool haveH = (h[3] != 0);
            const bool haveV = (v[3] != 0);
            if (haveH && haveV) {
                p[0] = (BYTE)(((int)h[0] + (int)v[0] + 1) / 2);
                p[1] = (BYTE)(((int)h[1] + (int)v[1] + 1) / 2);
                p[2] = (BYTE)(((int)h[2] + (int)v[2] + 1) / 2);
            } else if (haveH) {
                p[0] = h[0]; p[1] = h[1]; p[2] = h[2];
            } else if (haveV) {
                p[0] = v[0]; p[1] = v[1]; p[2] = v[2];
            } else {
                p[0] = avgB; p[1] = avgG; p[2] = avgR;
            }
            p[3] = 255;
        }
    }
}

// 表面区域的背景均色（BGRA，未预乘）。自动文字色用它还原半透明表面的实际呈现色；
// 只统计表面矩形本身，不含模糊外扩的边距，避免把桌面外圈算进来。
COLORREF AverageSurfaceColor(const BYTE* px, int capW, int capH,
                             int x0, int y0, int w, int h)
{
    if (!px || capW <= 0 || capH <= 0 || w <= 0 || h <= 0) return RGB(0, 0, 0);

    const int xs = ClampInt(x0, 0, capW);
    const int ys = ClampInt(y0, 0, capH);
    const int xe = ClampInt(x0 + w, 0, capW);
    const int ye = ClampInt(y0 + h, 0, capH);
    if (xe <= xs || ye <= ys) return RGB(0, 0, 0);

    long long sb = 0, sg = 0, sr = 0, n = 0;
    const size_t stride = (size_t)capW * 4u;
    for (int y = ys; y < ye; ++y) {
        const BYTE* row = px + (size_t)y * stride;
        for (int x = xs; x < xe; ++x) {
            const BYTE* p = row + (size_t)x * 4u;
            sb += p[0]; sg += p[1]; sr += p[2];
            ++n;
        }
    }
    if (n <= 0) return RGB(0, 0, 0);
    return RGB((int)(sr / n), (int)(sg / n), (int)(sb / n));
}

bool CaptureBackdrop(const RECT& surface, int margin, int w, int h,
                     int shape, int cornerPx, bool inpaint,
                     std::vector<BYTE>* out, int* outCapW, int* outCapH)
{
    if (!out || !outCapW || !outCapH || w <= 0 || h <= 0) return false;

    const int capW = w + margin * 2;
    const int capH = h + margin * 2;
    if (capW <= 0 || capH <= 0) return false;

    out->assign((size_t)capW * (size_t)capH * 4u, 0);
    *outCapW = capW;
    *outCapH = capH;

    RECT cap;
    cap.left   = surface.left - margin;
    cap.top    = surface.top - margin;
    cap.right  = surface.right + margin;
    cap.bottom = surface.bottom + margin;

    RECT virt;
    virt.left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    virt.top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    virt.right  = virt.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    virt.bottom = virt.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    RECT inter;
    if (!IntersectRect(&inter, &cap, &virt)) return false;

    const int cw = inter.right - inter.left;
    const int ch = inter.bottom - inter.top;
    if (cw <= 0 || ch <= 0) return false;

    HDC screen = GetDC(nullptr);
    bool ownScreen = false;
    if (!screen) {
        screen = CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
        ownScreen = true;
    }
    if (!screen) return false;

    HDC mem = CreateCompatibleDC(screen);
    if (!mem) {
        if (ownScreen) DeleteDC(screen);
        else           ReleaseDC(nullptr, screen);
        return false;
    }

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = capW;
    bmi.bmiHeader.biHeight      = -capH;          // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = false;

    if (dib && bits) {
        HGDIOBJ old = SelectObject(mem, dib);
        const int dx = inter.left - cap.left;
        const int dy = inter.top - cap.top;
        // 不用 CAPTUREBLT：那会把分层窗口（我们自己）也截进来
        if (BitBlt(mem, dx, dy, cw, ch, screen, inter.left, inter.top, SRCCOPY)) {
            memcpy(out->data(), bits, (size_t)capW * (size_t)capH * 4u);
            ExtendEdges(out->data(), capW, capH, dx, dy, cw, ch);
            if (inpaint) {
                InpaintSurfaceArea(out->data(), capW, capH, margin, margin,
                                   w, h, shape, cornerPx);
            }
            ok = true;
        }
        SelectObject(mem, old);
    }

    if (dib) DeleteObject(dib);
    DeleteDC(mem);
    if (ownScreen) DeleteDC(screen);
    else           ReleaseDC(nullptr, screen);

    return ok;
}

bool EnsureBackdrop(int slot, HWND self, const RECT& screenRect, int dpi,
                    int w, int h, int shape, int cornerPx, bool* outHidSelf)
{
    if (outHidSelf) *outHidSelf = false;   // v1.4.1：不再隐藏窗口
    if (slot < 0 || slot > 1) return false;
    BackdropCache& b = g_backdrop[slot];

    // 固定按最大模糊预留边距：拖动「模糊」滑块只重算模糊，不重抓屏。
    const int margin = ClampInt(MulDiv(40, dpi, 96) + 16, 24, 96);
    if (b.valid && b.w == w && b.h == h && b.dpi == dpi && b.margin == margin &&
        b.shape == shape && b.corner == cornerPx &&
        b.rect.left == screenRect.left && b.rect.top == screenRect.top &&
        b.rect.right == screenRect.right && b.rect.bottom == screenRect.bottom) {
        return true;
    }

    // 可见状态下抓屏会把球体自己拍进去；由 CaptureBackdrop 用形状掩码补全，
    // 不调用 ShowWindow(SW_HIDE)，因此不会丢鼠标捕获、也不会闪烁。
    const bool wasVisible = (self && IsWindow(self)) ? (IsWindowVisible(self) != FALSE) : false;

    std::vector<BYTE> pixels;
    int capW = 0, capH = 0;
    const bool ok = CaptureBackdrop(screenRect, margin, w, h, shape, cornerPx,
                                    wasVisible, &pixels, &capW, &capH);

    if (!ok) {
        // 同一矩形/尺寸下抓屏失败时沿用上一帧有效背景，避免整块退回纯色。
        if (b.pixels.size() == (size_t)b.capW * (size_t)b.capH * 4u &&
            b.w == w && b.h == h && b.dpi == dpi && b.margin == margin &&
            b.shape == shape && b.corner == cornerPx &&
            b.rect.left == screenRect.left && b.rect.top == screenRect.top &&
            b.rect.right == screenRect.right && b.rect.bottom == screenRect.bottom) {
            b.valid = true;
            return true;
        }
        b.valid = false;
        return false;
    }

    b.valid  = true;
    b.rect   = screenRect;
    b.dpi    = dpi;
    b.w      = w;
    b.h      = h;
    b.margin = margin;
    b.shape  = shape;
    b.corner = cornerPx;
    b.capW   = capW;
    b.capH   = capH;
    b.seq    = ++g_seqCounter;
    b.pixels.swap(pixels);

    // 自动文字色：表面背后到底是什么颜色，抓完顺手算一次，之后 O(1) 读取。
    b.avgColor = AverageSurfaceColor(b.pixels.data(), b.capW, b.capH,
                                     b.margin, b.margin, w, h);
    b.avgValid = true;
    return true;
}

// ---------------------------------------------------------------- 高斯模糊

struct BlurCache {
    bool     valid;
    unsigned backdropSeq;
    int      capW, capH;
    int      sigma100;              // sigma * 100
    unsigned seq;
    std::vector<BYTE> pixels;
};

BlurCache g_blur[2] = {};

void BoxesForGauss(double sigma, int n, int* boxes)
{
    if (!boxes || n <= 0) return;
    const double wIdeal = std::sqrt((12.0 * sigma * sigma / (double)n) + 1.0);
    int wl = (int)std::floor(wIdeal);
    if ((wl & 1) == 0) --wl;
    if (wl < 1) wl = 1;
    const int wu = wl + 2;

    const double mIdeal = (12.0 * sigma * sigma - (double)n * wl * wl
                           - 4.0 * (double)n * wl - 3.0 * (double)n)
                        / (-4.0 * (double)wl - 4.0);
    const int m = (int)std::floor(mIdeal + 0.5);

    for (int i = 0; i < n; ++i) {
        boxes[i] = (i < m) ? wl : wu;
    }
}

void BoxBlurH(const BYTE* src, BYTE* dst, int w, int h, int r)
{
    if (r <= 0 || w <= 0 || h <= 0) {
        memcpy(dst, src, (size_t)w * (size_t)h * 4u);
        return;
    }
    const int win = r * 2 + 1;
    for (int y = 0; y < h; ++y) {
        const BYTE* row = src + (size_t)y * (size_t)w * 4u;
        BYTE*       out = dst + (size_t)y * (size_t)w * 4u;

        int s0 = 0, s1 = 0, s2 = 0;
        for (int i = -r; i <= r; ++i) {
            const int ix = ClampInt(i, 0, w - 1);
            s0 += row[ix * 4];
            s1 += row[ix * 4 + 1];
            s2 += row[ix * 4 + 2];
        }

        for (int x = 0; x < w; ++x) {
            out[x * 4]     = (BYTE)(s0 / win);
            out[x * 4 + 1] = (BYTE)(s1 / win);
            out[x * 4 + 2] = (BYTE)(s2 / win);
            out[x * 4 + 3] = 255;

            const int addX = ClampInt(x + r + 1, 0, w - 1);
            const int subX = ClampInt(x - r,     0, w - 1);
            s0 += row[addX * 4]     - row[subX * 4];
            s1 += row[addX * 4 + 1] - row[subX * 4 + 1];
            s2 += row[addX * 4 + 2] - row[subX * 4 + 2];
        }
    }
}

void BoxBlurV(const BYTE* src, BYTE* dst, int w, int h, int r)
{
    if (r <= 0 || w <= 0 || h <= 0) {
        memcpy(dst, src, (size_t)w * (size_t)h * 4u);
        return;
    }
    const int win = r * 2 + 1;
    const size_t stride = (size_t)w * 4u;

    for (int x = 0; x < w; ++x) {
        int s0 = 0, s1 = 0, s2 = 0;
        for (int i = -r; i <= r; ++i) {
            const int iy = ClampInt(i, 0, h - 1);
            const BYTE* p = src + (size_t)iy * stride + (size_t)x * 4u;
            s0 += p[0];
            s1 += p[1];
            s2 += p[2];
        }

        for (int y = 0; y < h; ++y) {
            BYTE* out = dst + (size_t)y * stride + (size_t)x * 4u;
            out[0] = (BYTE)(s0 / win);
            out[1] = (BYTE)(s1 / win);
            out[2] = (BYTE)(s2 / win);
            out[3] = 255;

            const int addY = ClampInt(y + r + 1, 0, h - 1);
            const int subY = ClampInt(y - r,     0, h - 1);
            const BYTE* pa = src + (size_t)addY * stride + (size_t)x * 4u;
            const BYTE* ps = src + (size_t)subY * stride + (size_t)x * 4u;
            s0 += pa[0] - ps[0];
            s1 += pa[1] - ps[1];
            s2 += pa[2] - ps[2];
        }
    }
}

double EffectiveSigma(const glass::MaterialSpec& spec)
{
    const double b = (double)ClampInt(spec.blurPx, 0, 160);
    switch (spec.type) {
    case glass::MAT_ACRYLIC: return 3.0 + b * 0.55;   // 磨砂：重模糊
    case glass::MAT_GLASS:   return 1.5 + b * 0.32;   // 玻璃：中等模糊
    case glass::MAT_LIQUID:  return 0.8 + b * 0.22;   // 液态玻璃：轻模糊 + 强折射
    default:                 return 0.0;
    }
}

bool EnsureBlur(int slot, int sigma100)
{
    if (slot < 0 || slot > 1) return false;
    BackdropCache& bd = g_backdrop[slot];
    if (!bd.valid) return false;

    BlurCache& bl = g_blur[slot];
    if (bl.valid && bl.backdropSeq == bd.seq && bl.capW == bd.capW &&
        bl.capH == bd.capH && bl.sigma100 == sigma100) {
        return true;
    }

    std::vector<BYTE> a = bd.pixels;      // 原始背景副本
    const double sigma = (double)sigma100 / 100.0;

    if (sigma >= 0.6 && bd.capW > 2 && bd.capH > 2) {
        const int maxSigma = (std::min)(bd.capW, bd.capH) / 6;
        const double s = (sigma > (double)maxSigma) ? (double)maxSigma : sigma;
        int boxes[3] = {1, 1, 1};
        BoxesForGauss(s, 3, boxes);

        std::vector<BYTE> tmp((size_t)bd.capW * (size_t)bd.capH * 4u, 0);
        for (int i = 0; i < 3; ++i) {
            const int r = (boxes[i] - 1) / 2;
            BoxBlurH(a.data(), tmp.data(), bd.capW, bd.capH, r);
            BoxBlurV(tmp.data(), a.data(), bd.capW, bd.capH, r);
        }
    }

    bl.valid       = true;
    bl.backdropSeq = bd.seq;
    bl.capW        = bd.capW;
    bl.capH        = bd.capH;
    bl.sigma100    = sigma100;
    bl.seq         = ++g_seqCounter;
    bl.pixels.swap(a);
    return true;
}

// ---------------------------------------------------------------- 合成

void SampleBilinear(const BYTE* src, int capW, int capH,
                    float sx, float sy, float* outB, float* outG, float* outR)
{
    if (!src || capW <= 0 || capH <= 0) {
        if (outB) *outB = 0.0f;
        if (outG) *outG = 0.0f;
        if (outR) *outR = 0.0f;
        return;
    }

    sx = ClampF(sx, 0.0f, (float)(capW - 1));
    sy = ClampF(sy, 0.0f, (float)(capH - 1));

    const int x0 = (int)sx;
    const int y0 = (int)sy;
    const int x1 = (x0 + 1 < capW) ? (x0 + 1) : x0;
    const int y1 = (y0 + 1 < capH) ? (y0 + 1) : y0;
    const float fx = sx - (float)x0;
    const float fy = sy - (float)y0;
    const float ifx = 1.0f - fx;
    const float ify = 1.0f - fy;

    const BYTE* p00 = src + ((size_t)y0 * (size_t)capW + (size_t)x0) * 4u;
    const BYTE* p10 = src + ((size_t)y0 * (size_t)capW + (size_t)x1) * 4u;
    const BYTE* p01 = src + ((size_t)y1 * (size_t)capW + (size_t)x0) * 4u;
    const BYTE* p11 = src + ((size_t)y1 * (size_t)capW + (size_t)x1) * 4u;

    if (outB) *outB = (p00[0] * ifx + p10[0] * fx) * ify + (p01[0] * ifx + p11[0] * fx) * fy;
    if (outG) *outG = (p00[1] * ifx + p10[1] * fx) * ify + (p01[1] * ifx + p11[1] * fx) * fy;
    if (outR) *outR = (p00[2] * ifx + p10[2] * fx) * ify + (p01[2] * ifx + p11[2] * fx) * fy;
}

void CompositePixels(int slot, const glass::MaterialSpec& spec, int dpi,
                     BYTE* bits, int stride, int w, int h)
{
    (void)dpi;
    const float hw = (float)w * 0.5f;
    const float hh = (float)h * 0.5f;
    const int   corner = surface_shape::RadiusPx(spec.shape, w, h, spec.cornerPx);
    const float fr = (float)corner;

    const int   t = ClampInt(spec.concentration, 0, 100);
    const float tintA = (float)t / 100.0f;

    const float tr = (float)GetRValue(spec.tint);
    const float tg = (float)GetGValue(spec.tint);
    const float tb = (float)GetBValue(spec.tint);

    const BackdropCache& bd = g_backdrop[slot];
    const BlurCache&     bl = g_blur[slot];
    const bool haveBg = bd.valid && bl.valid && !bl.pixels.empty();
    const int ox = bd.margin;
    const int oy = bd.margin;
    const int capW = bd.capW;
    const int capH = bd.capH;
    const BYTE* src = bl.pixels.data();

    const double sigma = EffectiveSigma(spec);
    float edgeW = 0.0f;
    if (spec.type == glass::MAT_GLASS) {
        edgeW = (float)(std::min)(16.0, sigma * 1.4 + 4.0);
    } else if (spec.type == glass::MAT_LIQUID) {
        edgeW = (float)(std::min)(24.0, sigma * 1.8 + 6.0);
    }

    float baseAlpha = 0.0f;
    float tintMix   = tintA;
    float sat       = 1.0f;
    float grain     = 0.0f;

    switch (spec.type) {
    case glass::MAT_ACRYLIC:
        baseAlpha = 0.90f + 0.08f * (1.0f - tintA);
        tintMix   = tintA * 0.90f;
        sat       = 1.06f;
        grain     = 3.2f;
        break;
    case glass::MAT_GLASS:
        baseAlpha = 0.55f + 0.30f * tintA;
        tintMix   = tintA * 0.65f;
        sat       = 1.16f;
        break;
    case glass::MAT_LIQUID:
        baseAlpha = 0.48f + 0.28f * tintA;
        tintMix   = tintA * 0.45f;
        sat       = 1.22f;
        break;
    case glass::MAT_SOLID:
        baseAlpha = tintA;
        tintMix   = 1.0f;
        sat       = 1.0f;
        break;
    default:
        baseAlpha = 0.0f;
        break;
    }

    const unsigned seed = 0x9E3779B9u;   // 静态颗粒：不随时间变化，避免整块底板闪烁

    for (int y = 0; y < h; ++y) {
        BYTE* row = bits + (size_t)y * (size_t)stride;
        const float py = (float)y + 0.5f;
        const float yn = (h > 1) ? (float)y / (float)(h - 1) : 0.0f;

        for (int x = 0; x < w; ++x) {
            BYTE* p = row + (size_t)x * 4u;
            const float px = (float)x + 0.5f;
            const float d = surface_shape::Distance(spec.shape, px, py, hw, hh, fr);
            const float cov = ClampF(0.5f - d, 0.0f, 1.0f);
            if (cov <= 0.0f) {
                p[0] = p[1] = p[2] = p[3] = 0;
                continue;
            }

            float sb = tb, sg = tg, sr = tr;
            if (haveBg) {
                float sx = px + (float)ox;
                float sy = py + (float)oy;

                if (edgeW > 0.5f && d > -edgeW && d < 0.0f) {
                    float nx = 0.0f, ny = 0.0f;
                    surface_shape::Normal(spec.shape, px, py, hw, hh, fr, &nx, &ny);
                    const float k = (-d) / edgeW;                    // 0 在边缘，1 在内边界
                    const float disp = (1.0f - k) * edgeW *
                                       ((spec.type == glass::MAT_LIQUID) ? 0.85f : 0.60f);
                    sx = px + (float)ox + nx * disp;
                    sy = py + (float)oy + ny * disp;

                    if (spec.type == glass::MAT_LIQUID) {
                        // 轻微色散：红/蓝通道采样点错开
                        float b1, g1, r1, b2, g2, r2;
                        SampleBilinear(src, capW, capH, sx + nx * 1.2f, sy + ny * 1.2f, &b1, &g1, &r1);
                        SampleBilinear(src, capW, capH, sx - nx * 1.2f, sy - ny * 1.2f, &b2, &g2, &r2);
                        sb = (b1 + b2) * 0.5f;
                        sg = (g1 + g2) * 0.5f;
                        sr = (r1 + r2) * 0.5f;
                    } else {
                        SampleBilinear(src, capW, capH, sx, sy, &sb, &sg, &sr);
                    }
                } else {
                    SampleBilinear(src, capW, capH, sx, sy, &sb, &sg, &sr);
                }
            }

            // 饱和度增强：让模糊后的背景仍然“有颜色”
            const float lum = 0.2126f * sr + 0.7152f * sg + 0.0722f * sb;
            sr = lum + (sr - lum) * sat;
            sg = lum + (sg - lum) * sat;
            sb = lum + (sb - lum) * sat;

            // 色调
            sr = sr * (1.0f - tintMix) + tr * tintMix;
            sg = sg * (1.0f - tintMix) + tg * tintMix;
            sb = sb * (1.0f - tintMix) + tb * tintMix;

            // 材质自带光照：纯色桌面上也要能看出质感
            if (spec.type == glass::MAT_ACRYLIC) {
                if (grain > 0.0f) {
                    const float n = (Noise01(x, y, seed) - 0.5f) * 2.0f * grain;
                    sr += n;
                    sg += n;
                    sb += n;
                }
                if (yn < 0.10f) {
                    const float k = (1.0f - yn / 0.10f) * 20.0f;
                    sr += (255.0f - sr) * k / 255.0f;
                    sg += (255.0f - sg) * k / 255.0f;
                    sb += (255.0f - sb) * k / 255.0f;
                } else if (yn > 0.82f) {
                    const float k = ((yn - 0.82f) / 0.18f) * 0.16f;
                    sr *= (1.0f - k);
                    sg *= (1.0f - k);
                    sb *= (1.0f - k);
                }
                if (d > -3.0f) {
                    const float k = (1.0f + d / 3.0f) * 0.10f;
                    sr *= (1.0f - k);
                    sg *= (1.0f - k);
                    sb *= (1.0f - k);
                }
            } else if (spec.type == glass::MAT_GLASS) {
                // 玻璃：静态高光带（液态玻璃的流动带由 AddLiquidBand 逐帧叠加）
                const float u = (px / (float)w) * 0.72f + (py / (float)h) * 0.28f;
                float band = 1.0f - std::fabs((u - 0.22f) / 0.16f);
                if (band > 0.0f) {
                    band = band * band * (3.0f - 2.0f * band);
                    const float k = band * 30.0f;
                    sr += (255.0f - sr) * k / 255.0f;
                    sg += (255.0f - sg) * k / 255.0f;
                    sb += (255.0f - sb) * k / 255.0f;
                }
                if (yn < 0.05f) {
                    const float k = (1.0f - yn / 0.05f) * 28.0f;
                    sr += (255.0f - sr) * k / 255.0f;
                    sg += (255.0f - sg) * k / 255.0f;
                    sb += (255.0f - sb) * k / 255.0f;
                }
                if (yn > 0.80f) {
                    const float k = ((yn - 0.80f) / 0.20f) * 0.14f;
                    sr *= (1.0f - k);
                    sg *= (1.0f - k);
                    sb *= (1.0f - k);
                }
            } else if (spec.type == glass::MAT_LIQUID) {
                // 液态玻璃：只留静态的顶部/底部明暗，流动高光带见 AddLiquidBand
                if (yn < 0.05f) {
                    const float k = (1.0f - yn / 0.05f) * 40.0f;
                    sr += (255.0f - sr) * k / 255.0f;
                    sg += (255.0f - sg) * k / 255.0f;
                    sb += (255.0f - sb) * k / 255.0f;
                }
                if (yn > 0.80f) {
                    const float k = ((yn - 0.80f) / 0.20f) * 0.18f;
                    sr *= (1.0f - k);
                    sg *= (1.0f - k);
                    sb *= (1.0f - k);
                }
            }

            // 玻璃/液态玻璃的边缘亮边（折射之外再给一条 1-2px 的锐利高光）
            if (edgeW > 0.5f && d > -1.8f && d < 0.0f) {
                const float k = (spec.type == glass::MAT_LIQUID) ? 70.0f : 52.0f;
                sr += (255.0f - sr) * k / 255.0f;
                sg += (255.0f - sg) * k / 255.0f;
                sb += (255.0f - sb) * k / 255.0f;
            }

            float a = baseAlpha * cov;
            if (a < 0.0f) a = 0.0f;
            if (a > 1.0f) a = 1.0f;
            const int ai = (int)(a * 255.0f + 0.5f);

            // 预乘 ARGB：UpdateLayeredWindow 与 PixelFormat32bppPARGB 都要求预乘
            p[0] = ClampByteFromFloat(ClampF(sb, 0.0f, 255.0f) * (float)ai / 255.0f);
            p[1] = ClampByteFromFloat(ClampF(sg, 0.0f, 255.0f) * (float)ai / 255.0f);
            p[2] = ClampByteFromFloat(ClampF(sr, 0.0f, 255.0f) * (float)ai / 255.0f);
            p[3] = (BYTE)ai;
        }
    }
}

// 液态玻璃的流光位置只由时间决定，与「数字刷新 / 抓屏」完全解耦：
// 在 0.18~0.60 之间往返（三角波，避免扫到底后突然跳回起点），一个来回 4.2s。
// 毫秒时间基保证任何帧率下都是连续的，不会出现 120ms 一格的台阶。
float LiquidBandCenter()
{
    const ULONGLONG kPeriodMs = 4200;
    const double t = (double)(GetTickCount64() % kPeriodMs) / (double)kPeriodMs;
    const double tri = (t < 0.5) ? (t * 2.0) : ((1.0 - t) * 2.0);
    return (float)(0.18 + 0.42 * tri);
}

// 液态玻璃的流动高光带：在已合成（含描边/高光）的预乘 DIB 上逐像素叠加。
// 对外暴露为 glass::addLiquidBand；paint() 只负责底图，叠加时机由调用方决定
// （先缓存「底图+文字」，再逐帧叠带，避免每帧重画文字）。
//   · 只依赖当前时间，所以动画帧不需要重抓屏、重模糊、重做 SDF/背景采样；
//   · 预乘空间里「把直通色向白色混 k」等价于向 alpha 靠拢：C' = C + (a − C)·k。
void AddLiquidBandPixels(BYTE* bits, int stride, int w, int h)
{
    if (!bits || w <= 0 || h <= 0 || stride < w * 4) return;

    const float center = LiquidBandCenter();
    const float bandW  = 0.13f;
    const float invW   = 1.0f / (float)w;
    const float invH   = 1.0f / (float)h;

    for (int y = 0; y < h; ++y) {
        BYTE* row = bits + (size_t)y * (size_t)stride;
        const float py = (float)y + 0.5f;
        for (int x = 0; x < w; ++x) {
            BYTE* p = row + (size_t)x * 4u;
            if (p[3] == 0) continue;

            const float u = (float)x * invW * 0.72f + py * invH * 0.28f;
            float band = 1.0f - std::fabs((u - center) / bandW);
            if (band <= 0.0f) continue;

            band = band * band * (3.0f - 2.0f * band);
            const float k = band * 46.0f / 255.0f;
            const float a = (float)p[3];
            p[0] = (BYTE)((float)p[0] + (a - (float)p[0]) * k + 0.5f);
            p[1] = (BYTE)((float)p[1] + (a - (float)p[1]) * k + 0.5f);
            p[2] = (BYTE)((float)p[2] + (a - (float)p[2]) * k + 0.5f);
        }
    }
}

void DrawOverlay(BYTE* bits, int stride, int w, int h, const glass::MaterialSpec& spec)
{
    if (!bits || w <= 0 || h <= 0) return;
    if (!spec.border && !spec.sheen) return;

    int alpha = 80;
    switch (spec.type) {
    case glass::MAT_ACRYLIC: alpha = 62;  break;
    case glass::MAT_GLASS:   alpha = 96;  break;
    case glass::MAT_LIQUID:  alpha = 120; break;
    case glass::MAT_NONE:    alpha = 140; break;
    default:                 alpha = 80;  break;
    }

    Gdiplus::Bitmap canvas(w, h, stride, PixelFormat32bppPARGB, bits);
    Gdiplus::Graphics g(&canvas);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);

    Gdiplus::GraphicsPath path;
    surface_shape::BuildPath(&path, w, h, spec.shape, spec.cornerPx);
    surface_style::drawOverlay(g, path, w, h,
                               spec.border != FALSE, spec.sheen != FALSE, alpha);
}

// ---------------------------------------------------------------- 材质缓存

struct MaterialCache {
    bool         valid;
    bool         usedFallback;      // 抓屏失败时缓存的纯色回退
    unsigned     blurSeq;
    glass::MaterialSpec spec;
    int          dpi;
    int          w, h;
    RECT         rect;
    std::vector<BYTE> pixels;       // w*h，预乘；液态玻璃不含流动高光带
};

MaterialCache g_material[2] = {};

bool SpecEqual(const glass::MaterialSpec& a, const glass::MaterialSpec& b)
{
    return a.type == b.type && a.shape == b.shape && a.cornerPx == b.cornerPx &&
           a.blurPx == b.blurPx && a.tint == b.tint &&
           a.concentration == b.concentration && a.border == b.border &&
           a.sheen == b.sheen && a.systemAcrylic == b.systemAcrylic;
}

void CopyToBits(BYTE* bits, int stride, const std::vector<BYTE>& pixels, int w, int h)
{
    for (int y = 0; y < h; ++y) {
        memcpy(bits + (size_t)y * (size_t)stride,
               pixels.data() + (size_t)y * (size_t)w * 4u,
               (size_t)w * 4u);
    }
}

// ---------------------------------------------------------------- 系统亚克力底窗

struct AccentPolicy {
    int  nAccentState;
    int  nFlags;
    int  nColor;          // 0xAABBGGRR
    int  nAnimationId;
};

struct WinCompAttrData {
    int   nAttribute;
    void* pData;
    ULONG ulDataSize;
};

const int kWcaAccentPolicy        = 19;
const int kAccentEnableBlurBehind = 3;
const int kAccentEnableAcrylic    = 4;

typedef BOOL (WINAPI* PFN_SetWindowCompositionAttribute)(HWND, WinCompAttrData*);

PFN_SetWindowCompositionAttribute ResolveAccentFn()
{
    static PFN_SetWindowCompositionAttribute fn = nullptr;
    static bool probed = false;
    if (!probed) {
        probed = true;
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        if (u32) fn = (PFN_SetWindowCompositionAttribute)(void*)GetProcAddress(u32, "SetWindowCompositionAttribute");
    }
    return fn;
}

struct SysSlot {
    HWND     hwnd;
    RECT     rect;
    int      shape;
    int      radius;
    COLORREF tint;
    int      alpha;
    bool     visible;
    bool     accentApplied;
};

SysSlot g_sys[2] = {};

LRESULT CALLBACK SysGlassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_DESTROY:
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool RegisterSysGlassClass(HINSTANCE inst)
{
    WNDCLASSEXW exist;
    ZeroMemory(&exist, sizeof(exist));
    exist.cbSize = sizeof(exist);
    if (GetClassInfoExW(inst, WND_CLASS_GLASS, &exist)) return true;

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = 0;
    wc.lpfnWndProc   = SysGlassProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = WND_CLASS_GLASS;
    return RegisterClassExW(&wc) != 0;
}

void ApplyRegion(HWND hwnd, int w, int h, int shape, int cornerPx)
{
    if (!hwnd || !IsWindow(hwnd) || w <= 0 || h <= 0) return;

    HRGN rgn = nullptr;
    const int r = surface_shape::RadiusPx(shape, w, h, cornerPx);
    if (shape == 0) {
        rgn = CreateEllipticRgn(0, 0, w + 1, h + 1);
    } else if (r > 0) {
        // CreateRoundRectRgn 的参数是椭圆宽高（直径），不是半径
        rgn = CreateRoundRectRgn(0, 0, w + 1, h + 1, r * 2, r * 2);
    }

    // SetWindowRgn 成功后由系统接管 region，不要再 DeleteObject
    if (rgn) SetWindowRgn(hwnd, rgn, TRUE);
    else     SetWindowRgn(hwnd, nullptr, TRUE);
}

void ApplyAccent(HWND hwnd, COLORREF tint, int alphaPercent)
{
    PFN_SetWindowCompositionAttribute fn = ResolveAccentFn();
    if (!fn) return;

    const int a = ClampInt(alphaPercent, 0, 100);

    AccentPolicy policy;
    policy.nAccentState   = kAccentEnableAcrylic;
    policy.nFlags         = 2;   // 允许在无边框窗口上绘制
    policy.nColor         = ((a * 255 / 100) << 24)
                          | (GetBValue(tint) << 16)
                          | (GetGValue(tint) << 8)
                          |  GetRValue(tint);
    policy.nAnimationId   = 0;

    WinCompAttrData data;
    data.nAttribute  = kWcaAccentPolicy;
    data.pData       = &policy;
    data.ulDataSize  = sizeof(policy);

    if (!fn(hwnd, &data)) {
        policy.nAccentState = kAccentEnableBlurBehind;   // 退一步：只模糊、不带色调
        fn(hwnd, &data);
    }
}

bool EnsureSysWindow(int slot, HWND owner)
{
    if (slot < 0 || slot > 1) return false;
    if (g_sys[slot].hwnd && IsWindow(g_sys[slot].hwnd)) return true;

    HINSTANCE inst = (HINSTANCE)GetModuleHandleW(nullptr);
    if (!RegisterSysGlassClass(inst)) return false;

    const DWORD ex = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT;
    HWND hwnd = CreateWindowExW(ex, WND_CLASS_GLASS, L"", WS_POPUP,
                                0, 0, 1, 1, owner, nullptr, inst, nullptr);
    if (!hwnd) return false;

    ZeroMemory(&g_sys[slot], sizeof(g_sys[slot]));
    g_sys[slot].hwnd = hwnd;
    return true;
}

} // namespace

// ============================================================================
// 对外接口
// ============================================================================

bool glass::acrylicAvailable()
{
    Probe();
    return g_acrylic;
}

bool glass::backdropColor(int slot, HWND self, const RECT& screenRect, int dpi,
                          int w, int h, int shape, int cornerPx,
                          BOOL allowCapture, COLORREF* out)
{
    if (!out || slot < 0 || slot > 1 || w <= 0 || h <= 0) return false;
    if (dpi < 48) dpi = 96;

    BackdropCache& b = g_backdrop[slot];

    if (!allowCapture) {
        // 只读缓存：几何不一致说明存的是别的位置/尺寸，宁可不给也不现场抓屏
        if (!b.valid || !b.avgValid || b.w != w || b.h != h || b.dpi != dpi ||
            b.shape != shape || b.corner != cornerPx ||
            b.rect.left != screenRect.left || b.rect.top != screenRect.top ||
            b.rect.right != screenRect.right || b.rect.bottom != screenRect.bottom) {
            return false;
        }
        *out = b.avgColor;
        return true;
    }

    // 纯色/无底板没有材质抓屏：按需抓一帧。几何不变时 EnsureBackdrop 直接命中缓存；
    // 可见状态下由形状掩码补全，球体/面板自己不会被算进背景。
    if (!EnsureBackdrop(slot, self, screenRect, dpi, w, h, shape, cornerPx, nullptr)) {
        return false;
    }
    if (!b.valid || !b.avgValid) return false;
    *out = b.avgColor;
    return true;
}

bool glass::paint(int slot, HWND self, const RECT& screenRect, int dpi,
                  const MaterialSpec& spec, BYTE* bits, int stride, int w, int h,
                  BOOL* outHidSelf)
{
    if (outHidSelf) *outHidSelf = FALSE;
    if (slot < 0 || slot > 1 || !bits || w <= 0 || h <= 0 || stride < w * 4) {
        return false;
    }
    if (dpi < 48) dpi = 96;

    // 无底板：只画描边/高光，背景由调用方清零
    if (spec.type == MAT_NONE) {
        DrawOverlay(bits, stride, w, h, spec);
        g_material[slot].valid = false;
        return true;
    }

    MaterialCache& mc = g_material[slot];

    const bool needsBg = (spec.type == MAT_ACRYLIC ||
                          spec.type == MAT_GLASS ||
                          spec.type == MAT_LIQUID);

    // 液态玻璃的流动高光带不参与缓存：命中缓存后按当前时间补一条即可逐帧流动，
    // 因此缓存键里没有「相位」，动画帧的开销只有一次整块拷贝 + 一条高光带。
    if (mc.valid && mc.w == w && mc.h == h && mc.dpi == dpi &&
        mc.rect.left == screenRect.left && mc.rect.top == screenRect.top &&
        mc.rect.right == screenRect.right && mc.rect.bottom == screenRect.bottom &&
        SpecEqual(mc.spec, spec) &&
        (!needsBg || mc.usedFallback ||
         (g_blur[slot].valid && mc.blurSeq == g_blur[slot].seq))) {
        CopyToBits(bits, stride, mc.pixels, w, h);
        return true;
    }

    bool haveBg = false;
    bool hidSelf = false;
    if (needsBg) {
        haveBg = EnsureBackdrop(slot, self, screenRect, dpi, w, h,
                                spec.shape, spec.cornerPx, &hidSelf);
        if (haveBg) {
            const int sigma100 = (int)(EffectiveSigma(spec) * 100.0 + 0.5);
            haveBg = EnsureBlur(slot, sigma100);
        }
    }
    if (outHidSelf && hidSelf) *outHidSelf = TRUE;

    CompositePixels(slot, spec, dpi, bits, stride, w, h);
    DrawOverlay(bits, stride, w, h, spec);

    mc.valid        = true;
    mc.usedFallback = (needsBg && !haveBg);
    mc.blurSeq      = (needsBg && haveBg) ? g_blur[slot].seq : 0;
    mc.spec         = spec;
    mc.dpi      = dpi;
    mc.w        = w;
    mc.h        = h;
    mc.rect     = screenRect;
    mc.pixels.resize((size_t)w * (size_t)h * 4u);
    for (int y = 0; y < h; ++y) {
        memcpy(mc.pixels.data() + (size_t)y * (size_t)w * 4u,
               bits + (size_t)y * (size_t)stride,
               (size_t)w * 4u);
    }

    // 缓存与输出的都是「无流动带」底图；流动带由 glass::addLiquidBand 叠加，
    // 调用方在画完文字后先缓存一帧，再叠带，这样动画帧不必重画文字。
    return true;
}

// v1.4.3：液态玻璃流光逐帧叠加。调用方先缓存「底图 + 文字」，每个动画帧拷贝
// 缓存后用本函数补上当前时刻的高光带，再 UpdateLayeredWindow。
void glass::addLiquidBand(BYTE* bits, int stride, int w, int h)
{
    AddLiquidBandPixels(bits, stride, w, h);
}

bool glass::placeSystemAcrylic(int slot, HWND owner, HWND textHwnd,
                               const RECT& screenRect, int dpi,
                               const MaterialSpec& spec)
{
    if (slot < 0 || slot > 1) return false;
    if (spec.type != MAT_ACRYLIC || !spec.systemAcrylic) return false;
    if (!acrylicAvailable()) return false;
    (void)dpi;   // accent 底窗按物理像素放置，DPI 已在调用方换算
    if (!EnsureSysWindow(slot, owner)) return false;

    SysSlot& s = g_sys[slot];
    if (!s.hwnd || !IsWindow(s.hwnd)) return false;

    const int w = screenRect.right - screenRect.left;
    const int h = screenRect.bottom - screenRect.top;
    if (w <= 0 || h <= 0) {
        hideSystemAcrylic(slot);
        return false;
    }

    // 先定位/定尺寸，再 accent，最后 region：
    // Win11 的 accent 会重置窗口 region，顺序反了胶囊就会变矩形。
    SetWindowPos(s.hwnd, HWND_TOPMOST, screenRect.left, screenRect.top, w, h,
                 SWP_NOACTIVATE);
    ApplyAccent(s.hwnd, spec.tint, spec.concentration);
    ApplyRegion(s.hwnd, w, h, spec.shape, spec.cornerPx);

    // 玻璃底窗紧贴文字层之下
    if (textHwnd && IsWindow(textHwnd)) {
        SetWindowPos(s.hwnd, textHwnd, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // z 序变化后再补一次 region，防止被系统重置
        ApplyRegion(s.hwnd, w, h, spec.shape, spec.cornerPx);
    }

    if (!s.visible) {
        ShowWindow(s.hwnd, SW_SHOWNOACTIVATE);
        s.visible = true;
    }

    s.rect   = screenRect;
    s.shape  = spec.shape;
    s.radius = spec.cornerPx;
    s.tint   = spec.tint;
    s.alpha  = spec.concentration;
    s.accentApplied = true;
    return true;
}

void glass::hideSystemAcrylic(int slot)
{
    if (slot < 0 || slot > 1) return;
    SysSlot& s = g_sys[slot];
    if (s.hwnd && IsWindow(s.hwnd) && s.visible) {
        ShowWindow(s.hwnd, SW_HIDE);
        s.visible = false;
    }
}

void glass::invalidateBackdrop(int slot)
{
    if (slot < 0 || slot > 1) return;
    g_backdrop[slot].valid = false;
    g_blur[slot].valid     = false;
    g_material[slot].valid = false;
}

void glass::invalidateAll()
{
    invalidateBackdrop(0);
    invalidateBackdrop(1);
}

void glass::destroy()
{
    for (int i = 0; i < 2; ++i) {
        if (g_sys[i].hwnd && IsWindow(g_sys[i].hwnd)) {
            DestroyWindow(g_sys[i].hwnd);
        }
        ZeroMemory(&g_sys[i], sizeof(g_sys[i]));

        g_backdrop[i].valid = false;
        g_backdrop[i].pixels.clear();
        g_blur[i].valid = false;
        g_blur[i].pixels.clear();
        g_material[i].valid = false;
        g_material[i].pixels.clear();
    }
}
