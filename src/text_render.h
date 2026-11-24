// src/text_render.h —— 文字块引擎（v1.2.0）
//
// 任务栏面板与悬浮球共用：把「排版 spec + 指标」渲染成一组按行排列的文字块。
//
// 性能约定（这是本次最重要的优化点）：
//   1. GDI+ Font 按像素字号缓存，绝不在绘制路径里反复 CreateFont / new Font
//      （旧实现 MeasureLine() 每次新建 1×1 Bitmap + Graphics + Font，是热点）。
//   2. 文本宽度按 (字号, 文本) 缓存，数值不变时布局零开销。
//   3. 调用方先比较「文本指纹」，未变化直接跳过绘制与 UpdateLayeredWindow。
//
// 线程约定：全部函数只在 UI 线程调用（GDI+ 对象不跨线程）。
#pragma once

#include "app_common.h"
#include "layout_model.h"

#include <objidl.h>
#include <gdiplus.h>

namespace text_render {

struct Block {
    int     count;
    int     rows;
    int     fontPx;
    int     lineH;
    int     gapPx;
    int     width;                            // 内容宽度（最宽行）
    int     height;                           // 行数 × 行高
    wchar_t text[NETBALL_MAX_ITEMS][96];      // 每项显示文本
    int     itemW[NETBALL_MAX_ITEMS];         // 每项像素宽
    int     rowOf[NETBALL_MAX_ITEMS];
    int     rowW[NETBALL_MAX_ITEMS];          // 每行总宽（含项目间距）
};

// 释放缓存的 GDI+ 对象（必须在 GdiplusShutdown 之前调用）
void shutdown();

// 单行高度：fontPx × linePct / 100，向上取整，最小 1
int LineHeight(int fontPx, int linePct);

// 文本像素宽度（含尾部空格，向上取整），带缓存
int MeasureText(const wchar_t* text, int fontPx);

// 按 spec 顺序格式化并测量，生成 Block
void BuildBlock(const layout_model::LayoutBlock& spec,
                const AppConfig* cfg, const Metrics* metrics,
                int fontPx, int linePct, int gapPx, Block* out);

// 在 [minPx, maxPx] 内二分查找「内容宽度 ≤ maxWidth」的最大字号。
// maxWidth <= 0 表示不限制宽度，直接返回 maxPx。
int FitFontPx(const layout_model::LayoutBlock& spec,
              const AppConfig* cfg, const Metrics* metrics,
              int linePct, int gapPx, int minPx, int maxPx, int maxWidth);

// 在 (x,y,w,h) 区域里按行居中绘制；halo=true 时加四向反色柔光（低对比度场景）
void DrawBlock(Gdiplus::Graphics& g, const Block& block,
               int x, int y, int w, int h,
               COLORREF color, bool halo);

// 悬浮球最小尺寸：保证 blockW×blockH 的文字块完整落在 shapes 内部
//   shape：0 椭圆 / 1 圆角方 / 2 胶囊；padPx 为内边距
//   curW/curH 为用户当前尺寸，返回「不小于文字占用」的最小尺寸
void MinBallSize(int shape, int blockW, int blockH, int padPx,
                 int curW, int curH, int* outW, int* outH);

} // namespace text_render
