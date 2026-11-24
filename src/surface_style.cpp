// src/surface_style.cpp —— 表面高光 / 内阴影 / 描边实现（v1.3.0）
#include "surface_style.h"

using namespace Gdiplus;

void surface_style::drawOverlay(Graphics& g, GraphicsPath& path,
                                int w, int h, bool border, bool sheen, int borderAlpha)
{
    if (w <= 0 || h <= 0) return;
    if (!border && !sheen) return;

    if (sheen) {
        // 顶部高光 + 底部内阴影：裁剪到形状内，避免圆角处溢出
        const GraphicsState st = g.Save();
        g.SetClip(&path);

        const int topH = (int)(h * 0.075);
        if (topH > 0) {
            LinearGradientBrush grad(Point(0, 0), Point(0, topH),
                                     Color(56, 255, 255, 255), Color(0, 255, 255, 255));
            g.FillRectangle(&grad, 0, 0, w, topH);
        }

        const int botH = (int)(h * 0.20);
        if (botH > 0) {
            LinearGradientBrush grad(Point(0, h - botH), Point(0, h),
                                     Color(0, 0, 0, 0), Color(46, 0, 0, 0));
            g.FillRectangle(&grad, 0, h - botH, w, botH);
        }

        g.Restore(st);
    }

    if (border) {
        Pen pen(Color((BYTE)(borderAlpha < 0 ? 0 : (borderAlpha > 255 ? 255 : borderAlpha)),
                      (BYTE)255, (BYTE)255, (BYTE)255), 1.0f);
        g.DrawPath(&pen, &path);
    }
}
