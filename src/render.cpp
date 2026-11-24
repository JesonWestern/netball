// src/render.cpp —— 渲染层实现（属主 builder-core-1）
// 纯 GDI+ 软件绘制，无第三方依赖。v1.2.0 起文字改走 text_render（字体/测量缓存），
// 排版改走 layout_model（与任务栏共用同一套「项目 + 行」模型）。
//
// 【自动文字色的取舍规则（AC-8）】—— v1.4.3 起以真实画面为准
//   1) 主路径 render::resolveAutoTextColor：文字绘制前采样已合成好的表面像素
//      （材质/模糊/透明度/壁纸全部计入），按 WCAG 取黑/白更高者；透出桌面的部分
//      用 glass::backdropColor 给出的背景均色还原，因此深色壁纸上的半透明球也不会
//      被误判成浅色底。
//   2) 无底板（无表面像素）时直接用背景均色判断明暗。
//   3) 采样与背景都不可用（系统亚克力/抓屏失败）才回退 render::autoTextColor：
//      maximin ≥ 4.5 取胜者；maximin < 4.5 的物理不可行区按典型中灰桌面退化，
//      并由绘制层额外加反色柔光提升实际可读性。
#include "render.h"
#include "layout_model.h"
#include "surface_shape.h"
#include "surface_style.h"
#include "text_render.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>

using namespace Gdiplus;

namespace {

const double kSrgbThresh = 0.03928;

double SrgbToLinear(double c)
{
    return (c <= kSrgbThresh) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
}

double RelLuminance(COLORREF c)
{
    const double r = SrgbToLinear(GetRValue(c) / 255.0);
    const double g = SrgbToLinear(GetGValue(c) / 255.0);
    const double b = SrgbToLinear(GetBValue(c) / 255.0);
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

BYTE MixByte(BYTE fg, BYTE bg, double a)
{
    double v = fg * a + bg * (1.0 - a);
    if (v < 0.0)   v = 0.0;
    if (v > 255.0) v = 255.0;
    return (BYTE)(v + 0.5);
}

COLORREF CompositeOver(COLORREF fg, COLORREF backdrop, double a)
{
    return RGB(MixByte(GetRValue(fg), GetRValue(backdrop), a),
               MixByte(GetGValue(fg), GetGValue(backdrop), a),
               MixByte(GetBValue(fg), GetBValue(backdrop), a));
}

// 按 WCAG 选黑/白：谁对比度更高用谁（灰区两边都不达标时由反色柔光兜底）。
COLORREF BestTextColor(COLORREF bg)
{
    const COLORREF white = RGB(255, 255, 255);
    const COLORREF black = RGB(0, 0, 0);
    return (render::contrastRatio(black, bg) > render::contrastRatio(white, bg)) ? black : white;
}

// 采样文字区域的实际呈现色。表面 DIB 是预乘 BGRA，最终显示到屏幕上的是：
//     displayed = premultRGB + backdrop × (1 − alpha)
// 半透明材质（玻璃/液态玻璃/低浓度纯色）如果忽略后一项，就会把“透出深色壁纸的
// 表面”当成不透明浅色，从而选黑字。alpha 覆盖不足时返回 false 交给上层兜底。
bool SamplePresentedColorFromBits(const BYTE* bits, int stride, int w, int h,
                                  const RECT& area, const render::TextBackdrop* backdrop,
                                  COLORREF* out)
{
    if (!bits || !out || w <= 0 || h <= 0 || stride < w * 4) return false;
    if (area.right <= area.left || area.bottom <= area.top) return false;

    const int x0 = (std::max)(0, (int)area.left);
    const int y0 = (std::max)(0, (int)area.top);
    const int x1 = (std::min)(w, (int)area.right);
    const int y1 = (std::min)(h, (int)area.bottom);
    if (x1 <= x0 || y1 <= y0) return false;

    const COLORREF fallbackBg = RGB(0, 0, 0);
    const COLORREF bg = (backdrop && backdrop->valid) ? backdrop->color : fallbackBg;

    double sb = 0.0, sg = 0.0, sr = 0.0, sa = 0.0;
    long long n = 0;
    for (int y = y0; y < y1; ++y) {
        const BYTE* row = bits + (size_t)y * (size_t)stride;
        for (int x = x0; x < x1; ++x) {
            const BYTE* p = row + (size_t)x * 4u;
            if (p[3] < 8) continue;          // 全透明像素没有可判定信息
            sb += p[0]; sg += p[1]; sr += p[2]; sa += p[3];
            ++n;
        }
    }
    if (n <= 0) return false;

    const double invN = 1.0 / (double)n;
    const double a = (sa * invN) / 255.0;
    if (a < 0.10) return false;              // 几乎全透明：无底板/系统亚克力

    const double k = 1.0 - a;
    double r = sr * invN + (double)GetRValue(bg) * k;
    double g = sg * invN + (double)GetGValue(bg) * k;
    double b = sb * invN + (double)GetBValue(bg) * k;
    if (r < 0.0) r = 0.0;
    if (r > 255.0) r = 255.0;
    if (g < 0.0) g = 0.0;
    if (g > 255.0) g = 255.0;
    if (b < 0.0) b = 0.0;
    if (b > 255.0) b = 255.0;

    *out = RGB((int)(r + 0.5), (int)(g + 0.5), (int)(b + 0.5));
    return true;
}

BYTE AlphaByte(int opacityPercent)
{
    int v = opacityPercent;
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    return (BYTE)((v * 255 + 50) / 100);
}

int PadPx(int dpi) { return (std::max)(2, MulDiv(6, dpi <= 0 ? 96 : dpi, 96)); }

int BallCornerPx(const AppConfig* c, int dpi)
{
    const int corner = c ? c->ballCorner : 12;
    return MulDiv(corner, dpi <= 0 ? 96 : dpi, 96);
}

int BallShapeOf(const AppConfig* c)
{
    if (!c) return 0;
    return (c->ballShape >= 0 && c->ballShape <= 2) ? c->ballShape : 0;
}

int GapPx(const AppConfig* c, int dpi)
{
    int g = c ? c->ballItemGap : 6;
    if (g < 0) g = 0;
    return MulDiv(g, dpi <= 0 ? 96 : dpi, 96);
}

// 用「最坏情况」指标测量字号：速度取 1023.9 MB/s、CPU/GPU/内存取 100%、
// 温度取 100°C、GPU 按 4 张卡。这样运行期数字变长时字号不会跟着缩。
const Metrics& StableMetrics()
{
    static const Metrics kStable = [] {
        Metrics m;
        ZeroMemory(&m, sizeof(m));
        m.netValid      = true;
        m.downBps       = 1023.9 * 1024.0 * 1024.0;
        m.upBps         = m.downBps;
        m.memPercent    = 100;
        m.memTotalBytes = 16ull * 1024 * 1024 * 1024;
        m.memUsedBytes  = m.memTotalBytes;
        m.cpuPercent    = 100;
        m.cpuTempC      = 100;
        m.cpuTempValid  = TRUE;
        m.gpuCount      = NETBALL_MAX_GPU;
        for (int i = 0; i < NETBALL_MAX_GPU; ++i) {
            m.gpuLoad[i]      = 100;
            m.gpuTempC[i]     = 100;
            m.gpuTempValid[i] = TRUE;
        }
        return m;
    }();
    return kStable;
}

// 按「实际文字块高度」计算形状内可用宽度：
//   · 椭圆：blockH 越大，顶部/底部越靠近椭圆收口，可用宽度按椭圆弦长收窄；
//   · 胶囊：端部圆弧内缩按 blockH 计算，而不是按整个可用高度；
//   · 圆角方：保持可用宽度，交给文字裁剪兜底。
int ShapeTextWidth(int shape, int w, int h, int pad, int blockH, int cornerPx)
{
    const int availW = (w > pad * 2) ? (w - pad * 2) : 0;
    const int availH = (h > pad * 2) ? (h - pad * 2) : 0;
    if (availW <= 0 || availH <= 0) return 0;

    if (shape == 0) {
        const double fy = (std::min)(1.0, (std::max)(0.0,
                            (double)blockH / (double)availH));
        const double fx = std::sqrt((std::max)(0.0, 1.0 - fy * fy));
        const int tw = (int)((double)availW * fx * 0.95) - 2;
        return (tw > 0) ? tw : 0;
    }

    if (shape == 2) {
        const double r = (double)h * 0.5;
        const double half = (std::min)((double)blockH, (double)h) * 0.5;
        double inset = 0.0;
        if (half > 0.0 && half < r) {
            inset = r - std::sqrt(r * r - half * half);
        }
        const int tw = (int)((double)availW - inset * 2.0) - 2;
        return (tw > 0) ? tw : 0;
    }

    (void)cornerPx;
    return (availW > 2) ? (availW - 2) : availW;
}

int FitBallFontPx(const AppConfig* c, const layout_model::LayoutBlock& spec,
                  int dpi, int w, int h)
{
    const int pad = PadPx(dpi);
    const int availH = (h > pad * 2) ? (h - pad * 2) : 0;
    if (availH <= 0) return 6;

    const int rows = (spec.rows > 0) ? spec.rows : 1;
    int maxByHeight = availH * 100 / (c->ballLinePct * rows);
    if (maxByHeight < 6)   maxByHeight = 6;
    if (maxByHeight > 160) maxByHeight = 160;

    const int shape  = BallShapeOf(c);
    const int corner = BallCornerPx(c, dpi);
    const int gap    = GapPx(c, dpi);
    const Metrics& worst = StableMetrics();

    int lo = 6, hi = maxByHeight, best = 6;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        text_render::Block block;
        text_render::BuildBlock(spec, c, &worst, mid, c->ballLinePct, gap, &block);
        if (block.height > availH) {
            hi = mid - 1;
            continue;
        }
        const int areaW = ShapeTextWidth(shape, w, h, pad, block.height, corner);
        if (block.width <= areaW) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

struct BallFontCache {
    bool     valid;
    int      dpi, w, h;
    int      shape, corner;
    int      fontSetting, linePct, gap;
    wchar_t  layout[NETBALL_SPEC_CCH];
    int      fontPx;
};

BallFontCache g_ballFontCache = {};

int ComputeBallFontPx(const AppConfig* c, const Metrics* m, int dpi, int w, int h,
                      const layout_model::LayoutBlock& spec)
{
    (void)m;   // 字号只由球体/排版决定，不再随实时数值变化

    const int shape  = BallShapeOf(c);
    const int corner = BallCornerPx(c, dpi);
    const int gap    = GapPx(c, dpi);

    BallFontCache& cache = g_ballFontCache;
    if (cache.valid && cache.dpi == dpi && cache.w == w && cache.h == h &&
        cache.shape == shape && cache.corner == corner &&
        cache.fontSetting == c->ballFontPx && cache.linePct == c->ballLinePct &&
        cache.gap == gap && wcscmp(cache.layout, c->ballLayout) == 0) {
        return cache.fontPx;
    }

    const int fit = FitBallFontPx(c, spec, dpi, w, h);
    int font = fit;
    if (c->ballFontPx > 0) {
        const int user = MulDiv(c->ballFontPx, dpi, 96);
        if (user < font) font = user;
        if (font < 6) font = 6;
    }

    cache.valid       = true;
    cache.dpi         = dpi;
    cache.w           = w;
    cache.h           = h;
    cache.shape       = shape;
    cache.corner      = corner;
    cache.fontSetting = c->ballFontPx;
    cache.linePct     = c->ballLinePct;
    cache.gap         = gap;
    lstrcpynW(cache.layout, c->ballLayout, NETBALL_SPEC_CCH);
    cache.fontPx      = font;
    return font;
}

struct BallContent {
    layout_model::LayoutBlock spec;
    text_render::Block        block;
};

bool BuildBallContent(const AppConfig* c, const Metrics* m, int dpi, int w, int h, BallContent* out)
{
    if (!c || !out) return false;
    ZeroMemory(out, sizeof(*out));

    layout_model::parse(c->ballLayout, NETBALL_MAX_ITEMS, &out->spec);
    if (out->spec.count <= 0) return false;

    int fontPx = ComputeBallFontPx(c, m, dpi, w, h, out->spec);
    text_render::BuildBlock(out->spec, c, m, fontPx, c->ballLinePct, GapPx(c, dpi), &out->block);

    // v1.4.1：字号已按最坏情况一次性固定，这里绝不再按实际数值缩字号。
    // 极少数超长文本由 DrawBallText 的形状裁剪兜底（宁可轻微裁切，也不让字号跳动）。
    return out->block.count > 0;
}

void ApplyBallGraphics(Graphics& g)
{
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetCompositingMode(CompositingModeSourceOver);
    // 透明背景不能开 ClearType
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
}

// 只画文字。材质已由 glass::paint 写进同一张 DIB，这里绝不能再填一层底色，
// 否则亚克力/玻璃会被二次色调叠加变回不透明。
// surfaceBits/surfaceStride 是同一张 DIB 的像素（可为 NULL）：自动文字色在画字
// 之前先采样它，拿到“文字底下真实长什么样”，再决定黑/白。
void DrawBallText(Graphics& g, int w, int h, int dpi, const AppConfig* c, const Metrics* m,
                  const BYTE* surfaceBits, int surfaceStride,
                  const render::TextBackdrop* backdrop)
{
    BallContent content;
    if (!BuildBallContent(c, m, dpi, w, h, &content)) return;

    const int mat = (c->ballMat >= 0 && c->ballMat <= 4) ? c->ballMat : 0;
    const COLORREF base = c->ballTint;
    const int baseAlpha = c->ballTintAlpha;

    const int tx = (w - content.block.width) / 2;
    const int ty = (h - content.block.height) / 2;

    const RECT textArea = { tx, ty, tx + content.block.width, ty + content.block.height };
    COLORREF presented = 0;
    BOOL havePresented = FALSE;

    const COLORREF textColor = c->ballTextAuto
        ? render::resolveAutoTextColor(surfaceBits, surfaceStride, w, h, textArea,
                                       backdrop, base, baseAlpha,
                                       &presented, &havePresented)
        : c->ballTextColor;

    // halo：判定用的是文字底下的真实颜色；没有真实背景时退回「合成到纯黑」的近似。
    const COLORREF contrastBase = havePresented
        ? presented
        : CompositeOver(base, RGB(0, 0, 0), baseAlpha / 100.0);
    const bool lowContrast = (mat == 2 || mat == 3 || mat == 4) ||
                             render::contrastRatio(textColor, contrastBase) < 7.5;

    // 文字严格裁剪在球体形状内：字号固定后不再为了“放得下”而缩小，
    // 极端长文本只会在边缘被裁掉，不会画到球体外面。
    GraphicsPath clipPath;
    surface_shape::BuildPath(&clipPath, w, h, BallShapeOf(c), BallCornerPx(c, dpi));
    const GraphicsState st = g.Save();
    g.SetClip(&clipPath);
    text_render::DrawBlock(g, content.block, tx, ty, content.block.width, content.block.height,
                           textColor, lowContrast);
    g.Restore(st);
}

// 完整绘制（兼容入口）：不抓屏，材质降级为纯色；悬浮球窗口请用 drawBallTextEx。
void DrawBallContent(Graphics& g, int w, int h, int dpi, const AppConfig* c, const Metrics* m,
                     const BYTE* surfaceBits, int surfaceStride)
{
    ApplyBallGraphics(g);

    GraphicsPath shape;
    surface_shape::BuildPath(&shape, w, h, BallShapeOf(c), BallCornerPx(c, dpi));

    const int mat = (c->ballMat >= 0 && c->ballMat <= 4) ? c->ballMat : 0;
    if (mat != 4) {
        SolidBrush fill(Color(AlphaByte(c->ballTintAlpha),
                              GetRValue(c->ballTint), GetGValue(c->ballTint), GetBValue(c->ballTint)));
        g.FillPath(&fill, &shape);
    }
    surface_style::drawOverlay(g, shape, w, h,
                               c->ballBorder != FALSE, c->ballSheen != FALSE,
                               (mat == 4) ? 150 : 80);

    DrawBallText(g, w, h, dpi, c, m, surfaceBits, surfaceStride, nullptr);
}

bool Is32bppDib(HDC memDc, DIBSECTION* ds)
{
    if (!memDc || !ds) return false;
    ZeroMemory(ds, sizeof(*ds));
    HBITMAP hbmp = (HBITMAP)GetCurrentObject(memDc, OBJ_BITMAP);
    return hbmp != NULL &&
           GetObjectW(hbmp, sizeof(*ds), ds) == (int)sizeof(*ds) &&
           ds->dsBm.bmBits != NULL &&
           ds->dsBm.bmBitsPixel == 32;
}

} // namespace

// ============================================================================
// 契约实现：render
// ============================================================================

double render::contrastRatio(COLORREF a, COLORREF b)
{
    double la = RelLuminance(a);
    double lb = RelLuminance(b);
    if (la < lb) { const double t = la; la = lb; lb = t; }
    return (la + 0.05) / (lb + 0.05);
}

bool render::samplePresentedColor(const BYTE* bits, int stride, int w, int h,
                                  const RECT& area, const TextBackdrop* backdrop,
                                  COLORREF* out)
{
    return SamplePresentedColorFromBits(bits, stride, w, h, area, backdrop, out);
}

COLORREF render::resolveAutoTextColor(const BYTE* bits, int stride, int w, int h,
                                      const RECT& area, const TextBackdrop* backdrop,
                                      COLORREF tint, int opacityPercent,
                                      COLORREF* outPresented, BOOL* outHavePresented)
{
    // 1) 已合成表面：材质、模糊、透明度、壁纸都在像素里，直接按呈现色判定
    COLORREF presented = 0;
    bool have = SamplePresentedColorFromBits(bits, stride, w, h, area, backdrop, &presented);

    // 2) 没有表面像素（无底板）：直接按表面背后的真实背景明暗判定
    if (!have && backdrop && backdrop->valid) {
        presented = backdrop->color;
        have = true;
    }

    if (outPresented)     *outPresented = presented;
    if (outHavePresented) *outHavePresented = have ? TRUE : FALSE;

    // 3) 采样与背景都不可用：回退到最坏情况推导
    if (!have) return render::autoTextColor(tint, opacityPercent);
    return BestTextColor(presented);
}

COLORREF render::autoTextColor(COLORREF bg, int opacityPercent)
{
    double a = opacityPercent / 100.0;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;

    // 仅在拿不到真实画面时使用（系统亚克力/抓屏失败）：
    // 桌面底色未知，按最坏情况取——分别合成到纯黑 / 纯白背景上。
    const COLORREF effOnBlack = CompositeOver(bg, RGB(0, 0, 0), a);
    const COLORREF effOnWhite = CompositeOver(bg, RGB(255, 255, 255), a);

    const COLORREF white = RGB(255, 255, 255);
    const COLORREF black = RGB(0, 0, 0);

    const double scoreWhite = std::min(contrastRatio(white, effOnBlack),
                                       contrastRatio(white, effOnWhite));
    const double scoreBlack = std::min(contrastRatio(black, effOnBlack),
                                       contrastRatio(black, effOnWhite));

    if (std::max(scoreWhite, scoreBlack) >= 4.5) {
        return (scoreWhite >= scoreBlack) ? white : black;
    }

    // 透明度极低时物理上不可能对纯黑/纯白两种桌面同时保证 4.5:1，
    // 退化为按典型中性桌面底色取更优者（绘制层会额外加反色柔光）。
    const COLORREF effTypical = CompositeOver(bg, RGB(96, 112, 128), a);
    return (contrastRatio(effTypical, white) >= contrastRatio(effTypical, black)) ? white : black;
}

void render::formatSpeed(double bps, wchar_t* out, size_t cch)
{
    if (!out || cch == 0) return;
    out[0] = 0;

    if (!(bps > 0.0)) bps = 0.0;          // 含 NaN 与负数
    if (bps > 1.0e12) bps = 1.0e12;

    static const wchar_t* kUnits[4] = {L"B/s", L"KB/s", L"MB/s", L"GB/s"};

    double v      = bps;
    int    unitIx = 0;
    while (v >= 1024.0 && unitIx < 3) { v /= 1024.0; ++unitIx; }
    const wchar_t* unit = kUnits[unitIx];

    wchar_t num[32] = {0};
    if (unitIx == 0)           _snwprintf(num, 31, L"%.0f", v);   // 字节/秒不显示小数
    else if (v >= 100.0)       _snwprintf(num, 31, L"%.0f", v);   // 128 KB/s
    else if (v >= 10.0)        _snwprintf(num, 31, L"%.1f", v);   // 12.5 MB/s
    else                       _snwprintf(num, 31, L"%.2f", v);   // 1.25 MB/s
    num[31] = 0;

    // 去掉尾随 0 与孤立小数点：1.20 -> 1.2，2.00 -> 2
    wchar_t* dot = wcschr(num, L'.');
    if (dot) {
        wchar_t* p = num + wcslen(num) - 1;
        while (p > dot && *p == L'0') { *p = 0; --p; }
        if (p == dot) *p = 0;
    }

    _snwprintf(out, cch - 1, L"%ls %ls", num, unit);
    out[cch - 1] = 0;
}

void render::computeBallSize(const AppConfig* c, const Metrics* m, int dpi, int* ioW, int* ioH)
{
    if (!c || !ioW || !ioH) return;
    if (dpi < 48) dpi = 96;
    (void)m;   // 尺寸不再依赖实时指标：数字长度变化不会让球伸缩

    int w = MulDiv(c->ballW, dpi, 96);
    int h = MulDiv(c->ballH, dpi, 96);
    if (w < 16) w = 16;
    if (h < 16) h = 16;

    // v1.4.1：球体尺寸是「用户设定值」，字号按最坏情况一次性固定；
    // 运行期既不抬球体，也不缩字号。
    *ioW = w;
    *ioH = h;
}

void render::ballMinSizeForFont(const AppConfig* c, const Metrics* m, int dpi, int fontPx,
                                int* outW, int* outH)
{
    (void)m;   // 用最坏情况指标，避免 "--" 的窄文本把最小尺寸算小
    if (!c || !outW || !outH) return;
    if (dpi < 48) dpi = 96;

    layout_model::LayoutBlock spec;
    layout_model::parse(c->ballLayout, NETBALL_MAX_ITEMS, &spec);
    if (spec.count <= 0) {
        *outW = c->ballW;
        *outH = c->ballH;
        return;
    }

    int px = (fontPx > 0) ? MulDiv(fontPx, dpi, 96) : 0;
    if (px < 6) px = 6;

    text_render::Block block;
    text_render::BuildBlock(spec, c, &StableMetrics(), px, c->ballLinePct,
                            GapPx(c, dpi), &block);

    int w = 40;
    int h = 40;
    text_render::MinBallSize(BallShapeOf(c), block.width, block.height,
                             PadPx(dpi), 0, 0, &w, &h);
    *outW = MulDiv(w, 96, dpi);
    *outH = MulDiv(h, 96, dpi);
}

int render::maxFontForBall(const AppConfig* c, const Metrics* m, int dpi, int w, int h)
{
    (void)m;
    if (!c || dpi < 48) dpi = 96;
    if (w <= 0 || h <= 0) return 8;

    layout_model::LayoutBlock spec;
    layout_model::parse(c->ballLayout, NETBALL_MAX_ITEMS, &spec);
    if (spec.count <= 0) return 48;

    const int px = FitBallFontPx(c, spec, dpi, w, h);
    int dip = MulDiv(px, 96, dpi);
    if (dip < 8)  dip = 8;
    if (dip > 48) dip = 48;
    return dip;
}

void render::drawBallEx(HDC memDc, int w, int h, int dpi, const AppConfig* c, const Metrics* m)
{
    if (!memDc || !c || w <= 0 || h <= 0) return;
    if (dpi < 48) dpi = 96;

    DIBSECTION ds;
    if (!Is32bppDib(memDc, &ds)) {
        // 退化路径：直接画到给定 DC（无 alpha，仅保证不崩）
        Graphics g(memDc);
        DrawBallContent(g, w, h, dpi, c, m, nullptr, 0);
        return;
    }

    // 主路径：把 DIB 内存包成 GDI+ 预乘 ARGB 位图来画（UpdateLayeredWindow 要的格式）
    BYTE* const bits   = (BYTE*)ds.dsBm.bmBits;
    const int   stride = ds.dsBm.bmWidthBytes;

    Bitmap canvas(w, h, stride, PixelFormat32bppPARGB, bits);
    Graphics g(&canvas);
    DrawBallContent(g, w, h, dpi, c, m, bits, stride);
}

void render::drawBallTextEx(HDC memDc, int w, int h, int dpi, const AppConfig* c,
                            const Metrics* m, const TextBackdrop* backdrop)
{
    if (!memDc || !c || w <= 0 || h <= 0) return;
    if (dpi < 48) dpi = 96;

    DIBSECTION ds;
    if (!Is32bppDib(memDc, &ds)) {
        Graphics g(memDc);
        ApplyBallGraphics(g);
        DrawBallText(g, w, h, dpi, c, m, nullptr, 0, backdrop);
        return;
    }

    BYTE* const bits   = (BYTE*)ds.dsBm.bmBits;
    const int   stride = ds.dsBm.bmWidthBytes;

    Bitmap canvas(w, h, stride, PixelFormat32bppPARGB, bits);
    Graphics g(&canvas);
    ApplyBallGraphics(g);
    DrawBallText(g, w, h, dpi, c, m, bits, stride, backdrop);
}

void render::drawBall(HDC memDc, int w, int h, const AppConfig* c, const Metrics* m)
{
    render::drawBallEx(memDc, w, h, 96, c, m);
}
