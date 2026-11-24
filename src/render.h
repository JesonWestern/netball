// src/render.h —— 渲染层（属主 builder-core-1）
//
// 对外接口由 src/app_common.h（契约）中的 namespace render 声明：
//     COLORREF autoTextColor(COLORREF bg, int opacityPercent);
//     double   contrastRatio(COLORREF a, COLORREF b);
//     void     formatSpeed(double bps, wchar_t* out, size_t cch);
//     void     drawBall(HDC memDc, int w, int h, const AppConfig* c, const Metrics* m);
//
// 本头文件额外提供 v1.2.0 的 DPI 版本与尺寸求解（drawBall 只是 96dpi 兼容壳）：
//     void drawBallEx(HDC, int w, int h, int dpi, const AppConfig*, const Metrics*);
//     void computeBallSize(const AppConfig*, const Metrics*, int dpi, int* ioW, int* ioH);
//
// 实现约定：
//   1. drawBallEx / drawBallTextEx 要求 memDc 中选入的是 32bpp top-down DIB
//      （UpdateLayeredWindow 用的那张）。内部把 DIB 内存包成 GDI+ 预乘 ARGB 位图再画。
//   2. 透明背景上禁用 ClearType（ClearType 需要不透明背景），统一 AntiAliasGridFit。
//   3. 不在这里做 GdiplusStartup —— 由 main.cpp 负责进程级生命周期。
//   4. v1.4.0 起材质由 glass::paint 写进 DIB，悬浮球窗口只调用 drawBallTextEx 画文字；
//      drawBallEx 保留为「完整绘制」兼容入口（材质降级为纯色，不抓屏）。
#pragma once

#include "app_common.h"

namespace render {

// ---- 自动文字色（v1.4.3）：按真实画面采样，而不是只用「底色+不透明度」推导 ----

// 文字区域背后的实际背景均色（无底板/纯色时由 glass 抓屏给出；亚克力/玻璃/
// 液态玻璃通常直接命中材质缓存）。valid=FALSE 表示没有可用背景。
struct TextBackdrop {
    COLORREF color;
    BOOL     valid;
};

// 采样已合成表面（32bpp 预乘 BGRA，top-down）在 area 内的实际呈现色：
//     displayed = premultRGB + backdrop × (1 − alpha)
// 半透明表面必须把透出来的桌面算进去，否则会把「深色壁纸上的半透明球」
// 误判成不透明浅色。alpha 覆盖不足（无底板/系统亚克力）返回 false。
bool samplePresentedColor(const BYTE* bits, int stride, int w, int h,
                          const RECT& area, const TextBackdrop* backdrop,
                          COLORREF* out);

// 自动文字色解析：
//   1) 能采样到表面呈现色 → 直接按 WCAG 选黑/白；
//   2) 否则有背景均色（无底板）→ 按背景明暗选黑/白；
//   3) 都没有 → 回退 autoTextColor() 的最坏情况 maximin。
// outPresented/outHavePresented 回传判定依据的颜色（供 halo 判真实对比度）。
COLORREF resolveAutoTextColor(const BYTE* bits, int stride, int w, int h,
                              const RECT& area, const TextBackdrop* backdrop,
                              COLORREF tint, int opacityPercent,
                              COLORREF* outPresented, BOOL* outHavePresented);

void drawBallEx(HDC memDc, int w, int h, int dpi, const AppConfig* c, const Metrics* m);

// 只画文字（材质已由 glass::paint 写进同一张 DIB）。
// backdrop 为文字区域背后的真实背景均色，可传 nullptr（回退旧推导）。
void drawBallTextEx(HDC memDc, int w, int h, int dpi, const AppConfig* c,
                    const Metrics* m, const TextBackdrop* backdrop = nullptr);

// 悬浮球尺寸：v1.3.0 起严格由 cfg.ballW/ballH 决定（返回物理像素），不再因文字长度伸缩。
void computeBallSize(const AppConfig* c, const Metrics* m, int dpi, int* ioW, int* ioH);

// 给定字号时，球体至少要多大才能容纳当前排版（返回 DIP；用于宽高滑块的下限）。
void ballMinSizeForFont(const AppConfig* c, const Metrics* m, int dpi, int fontPx,
                        int* outW, int* outH);

// 给定球体尺寸时，内部最多能放多大字号（返回 DIP；用于字号滑块的上限）。
int maxFontForBall(const AppConfig* c, const Metrics* m, int dpi, int w, int h);

} // namespace render
