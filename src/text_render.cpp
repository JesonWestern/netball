// src/text_render.cpp —— 文字块引擎实现（v1.2.0）
//
// 缓存策略（改前先读）：
//   · Font：以「像素字号」为 key，进程内只增不重建；text_render::shutdown() 统一释放。
//   · 宽度：以 (字号, 文本) 为 key。数值每秒变化，缓存会增长，超过上限整体清空，
//     不做 LRU（条目少、清空代价低）。
//   · 测量使用的 1×1 Bitmap/Graphics 常驻，避免旧实现每次 MeasureLine 都新建。
#include "text_render.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>

using namespace Gdiplus;

namespace {

const wchar_t* kFontFamily = L"Microsoft YaHei UI";

const size_t kMeasureCacheMax = 512;

Bitmap*       g_probeBmp = nullptr;
Graphics*     g_probeG   = nullptr;
StringFormat* g_fmt      = nullptr;

std::unordered_map<int, Font*> g_fonts;
std::map<std::pair<int, std::wstring>, int> g_measure;

void EnsureProbe()
{
    if (!g_probeBmp) {
        g_probeBmp = new Bitmap(1, 1, PixelFormat32bppPARGB);
    }
    if (!g_probeG && g_probeBmp) {
        g_probeG = new Graphics(g_probeBmp);
        if (g_probeG) {
            g_probeG->SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        }
    }
    if (!g_fmt) {
        g_fmt = new StringFormat();
        if (g_fmt) {
            g_fmt->SetAlignment(StringAlignmentCenter);
            g_fmt->SetLineAlignment(StringAlignmentCenter);
            g_fmt->SetFormatFlags(StringFormatFlagsNoWrap |
                                  StringFormatFlagsMeasureTrailingSpaces);
        }
    }
}

Font* GetFont(int fontPx)
{
    if (fontPx < 1) fontPx = 1;
    auto it = g_fonts.find(fontPx);
    if (it != g_fonts.end()) return it->second;

    Font* f = new Font(kFontFamily, (REAL)fontPx, FontStyleBold, UnitPixel);
    if (!f || f->GetLastStatus() != Ok) {
        delete f;
        // 兜底：雅黑不存在时用系统默认 sans-serif
        f = new Font(FontFamily::GenericSansSerif(), (REAL)fontPx, FontStyleBold, UnitPixel);
    }
    g_fonts[fontPx] = f;
    return f;
}

int MeasureRaw(const wchar_t* text, int fontPx)
{
    if (!text || !text[0]) return 0;
    EnsureProbe();
    if (!g_probeG) return 0;

    Font* font = GetFont(fontPx);
    if (!font) return 0;

    RectF box;
    if (g_probeG->MeasureString(text, -1, font, PointF(0.0f, 0.0f), g_fmt, &box) != Ok) {
        return 0;
    }
    if (box.Width < 0.0f) return 0;
    return (int)std::ceil(box.Width);
}

} // namespace

// ============================================================================

void text_render::shutdown()
{
    for (auto& kv : g_fonts) {
        delete kv.second;
    }
    g_fonts.clear();
    g_measure.clear();

    delete g_fmt;
    g_fmt = nullptr;
    delete g_probeG;
    g_probeG = nullptr;
    delete g_probeBmp;
    g_probeBmp = nullptr;
}

int text_render::LineHeight(int fontPx, int linePct)
{
    if (fontPx < 1) fontPx = 1;
    if (linePct < 50) linePct = 50;
    if (linePct > 300) linePct = 300;
    const int h = (fontPx * linePct + 99) / 100;
    return (h < 1) ? 1 : h;
}

int text_render::MeasureText(const wchar_t* text, int fontPx)
{
    if (!text || !text[0]) return 0;
    if (fontPx < 1) fontPx = 1;

    if (g_measure.size() > kMeasureCacheMax) g_measure.clear();

    const std::pair<int, std::wstring> key(fontPx, std::wstring(text));
    auto it = g_measure.find(key);
    if (it != g_measure.end()) return it->second;

    const int w = MeasureRaw(text, fontPx);
    g_measure[key] = w;
    return w;
}

void text_render::BuildBlock(const layout_model::LayoutBlock& spec,
                             const AppConfig* cfg, const Metrics* metrics,
                             int fontPx, int linePct, int gapPx, Block* out)
{
    if (!out) return;
    ZeroMemory(out, sizeof(*out));
    if (fontPx < 1) fontPx = 1;
    if (gapPx < 0) gapPx = 0;

    out->fontPx = fontPx;
    out->lineH  = LineHeight(fontPx, linePct);
    out->gapPx  = gapPx;
    out->count  = (spec.count > NETBALL_MAX_ITEMS) ? NETBALL_MAX_ITEMS : spec.count;
    out->rows   = spec.rows;
    if (out->count <= 0 || out->rows <= 0) {
        out->count = 0;
        out->rows  = 0;
        return;
    }
    if (out->rows > out->count) out->rows = out->count;

    for (int i = 0; i < out->count; ++i) {
        layout_model::itemText(spec.item[i], cfg, metrics, out->text[i], 96);
        out->itemW[i] = MeasureText(out->text[i], fontPx);
        out->rowOf[i] = spec.rowOf[i];
    }

    int widest = 0;
    for (int r = 0; r < out->rows; ++r) {
        int total = 0;
        int n = 0;
        for (int i = 0; i < out->count; ++i) {
            if (out->rowOf[i] != r) continue;
            if (n > 0) total += out->gapPx;
            total += out->itemW[i];
            ++n;
        }
        out->rowW[r] = total;
        if (total > widest) widest = total;
    }
    out->width  = widest;
    out->height = out->lineH * out->rows;
}

int text_render::FitFontPx(const layout_model::LayoutBlock& spec,
                           const AppConfig* cfg, const Metrics* metrics,
                           int linePct, int gapPx, int minPx, int maxPx, int maxWidth)
{
    if (maxPx < minPx) maxPx = minPx;
    if (maxWidth <= 0) return maxPx;

    Block b;
    BuildBlock(spec, cfg, metrics, maxPx, linePct, gapPx, &b);
    if (b.width <= maxWidth) return maxPx;

    int lo = minPx;
    int hi = maxPx;
    int best = minPx;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        BuildBlock(spec, cfg, metrics, mid, linePct, gapPx, &b);
        if (b.width <= maxWidth) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return best;
}

void text_render::DrawBlock(Graphics& g, const Block& block,
                            int x, int y, int w, int h,
                            COLORREF color, bool halo)
{
    if (block.count <= 0 || block.rows <= 0) return;

    Font* font = GetFont(block.fontPx);
    EnsureProbe();
    if (!font || !g_fmt) return;
    (void)h;

    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetCompositingMode(CompositingModeSourceOver);

    SolidBrush textBrush(Color(255, GetRValue(color), GetGValue(color), GetBValue(color)));

    SolidBrush haloBrush(Color(0, 0, 0, 0));
    REAL off = 0.0f;
    if (halo) {
        const int lum = GetRValue(color) + GetGValue(color) + GetBValue(color);
        const COLORREF haloColor = (lum > 384) ? RGB(0, 0, 0) : RGB(255, 255, 255);
        haloBrush.SetColor(Color(120, GetRValue(haloColor), GetGValue(haloColor), GetBValue(haloColor)));
        off = (REAL)(std::max)(1.0, block.fontPx / 14.0);
    }

    for (int r = 0; r < block.rows; ++r) {
        int cx = x + (w - block.rowW[r]) / 2;
        const REAL top = (REAL)(y + r * block.lineH);
        for (int i = 0; i < block.count; ++i) {
            if (block.rowOf[i] != r) continue;
            const int iw = block.itemW[i];
            if (iw <= 0) { cx += block.gapPx; continue; }

            const RectF rc((REAL)cx, top, (REAL)iw, (REAL)block.lineH);
            if (halo) {
                g.DrawString(block.text[i], -1, font, RectF(rc.X - off, rc.Y, rc.Width, rc.Height), g_fmt, &haloBrush);
                g.DrawString(block.text[i], -1, font, RectF(rc.X + off, rc.Y, rc.Width, rc.Height), g_fmt, &haloBrush);
                g.DrawString(block.text[i], -1, font, RectF(rc.X, rc.Y - off, rc.Width, rc.Height), g_fmt, &haloBrush);
                g.DrawString(block.text[i], -1, font, RectF(rc.X, rc.Y + off, rc.Width, rc.Height), g_fmt, &haloBrush);
            }
            g.DrawString(block.text[i], -1, font, rc, g_fmt, &textBrush);

            cx += iw + block.gapPx;
        }
    }
}

void text_render::MinBallSize(int shape, int blockW, int blockH, int padPx,
                              int curW, int curH, int* outW, int* outH)
{
    if (padPx < 0) padPx = 0;
    if (blockW < 0) blockW = 0;
    if (blockH < 0) blockH = 0;

    const int needW = blockW + padPx * 2;
    const int needH = blockH + padPx * 2;

    int w = (curW > 0) ? curW : needW;
    int h = (curH > 0) ? curH : needH;

    if (shape == 0) {
        // 椭圆：文字矩形四角必须落在椭圆内。按 0.92 安全系数等比放大。
        if (w < needW) w = needW;
        if (h < needH) h = needH;
        const double m = 0.92;
        const double sum = ((double)needW * needW) / ((double)w * w) +
                           ((double)needH * needH) / ((double)h * h);
        double s = 1.0;
        if (sum > m * m) s = std::sqrt(sum) / m;
        const int nw = (int)std::ceil((double)w * s);
        const int nh = (int)std::ceil((double)h * s);
        if (outW) *outW = nw;
        if (outH) *outH = nh;
        return;
    }

    if (shape == 1) {
        if (outW) *outW = (w < needW) ? needW : w;
        if (outH) *outH = (h < needH) ? needH : h;
        return;
    }

    // 胶囊：两端半圆的水平内缩 = r - sqrt(r² - (bh/2)²)
    if (h < needH) h = needH;
    const double r = (double)h * 0.5;
    const double halfText = (double)needH * 0.5;
    double inset = 0.0;
    if (halfText > 0.0 && halfText < r) {
        inset = r - std::sqrt(r * r - halfText * halfText);
    }
    const int minW = (int)std::ceil((double)needW + inset * 2.0);
    if (outW) *outW = (w < minW) ? minW : w;
    if (outH) *outH = h;
}
