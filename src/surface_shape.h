// src/surface_shape.h —— 表面形状（椭圆 / 圆角方 / 胶囊）唯一真源（v1.4.0）
//
// 为什么单独抽出来：
//   v1.3 之前 render.cpp、glass.cpp、taskbar_text.cpp 各写一套圆角/胶囊逻辑，
//   参数一改就出现「胶囊变矩形」「圆角方半径不一致」的问题。这里同时提供
//   GDI+ 路径（给描边/高光/文字排版用）和 SDF 距离（给像素遮罩/折射用），
//   两边共用同一套半径规则，形状不会再漂移。
//
// 约定：
//   · shape：0=椭圆 1=圆角方 2=胶囊。
//   · cornerPx：仅 shape==1 有效，单位物理像素；胶囊固定用短边一半。
//   · 全部 header-only，不新增 .cpp / 不改 build.bat。
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>

namespace surface_shape {

inline int ClampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// 实际圆角半径（物理像素）。椭圆返回 0；胶囊返回短边一半。
inline int RadiusPx(int shape, int w, int h, int cornerPx)
{
    if (w <= 0 || h <= 0) return 0;
    if (shape == 0) return 0;
    const int lim = (std::min)(w, h) / 2;
    if (shape == 2) return lim;
    return ClampInt(cornerPx, 0, lim);
}

// GDI+ 路径：描边、高光、文字裁剪共用
inline void BuildPath(Gdiplus::GraphicsPath* path, int w, int h, int shape, int cornerPx)
{
    if (!path) return;
    path->Reset();

    const Gdiplus::REAL W = (Gdiplus::REAL)w;
    const Gdiplus::REAL H = (Gdiplus::REAL)h;
    if (W <= 0.0f || H <= 0.0f) return;

    if (shape == 0) {
        path->AddEllipse(0.0f, 0.0f, W, H);
        return;
    }

    Gdiplus::REAL r = (Gdiplus::REAL)RadiusPx(shape, w, h, cornerPx);
    if (r < 1.0f) {
        path->AddRectangle(Gdiplus::RectF(0.0f, 0.0f, W, H));
        return;
    }

    const Gdiplus::REAL d = r * 2.0f;
    if (d > W || d > H) {
        path->AddEllipse(0.0f, 0.0f, W, H);
        return;
    }

    path->AddArc(0.0f,   0.0f,   d, d, 180.0f, 90.0f);   // 左上
    path->AddArc(W - d,  0.0f,   d, d, 270.0f, 90.0f);   // 右上
    path->AddArc(W - d,  H - d,  d, d,   0.0f, 90.0f);   // 右下
    path->AddArc(0.0f,   H - d,  d, d,  90.0f, 90.0f);   // 左下
    path->CloseFigure();
}

// 圆角矩形 SDF：负值在内部，0 在边界，正值在外部
inline float SdRoundRect(float px, float py, float hw, float hh, float r)
{
    const float qx = std::fabs(px - hw) - (hw - r);
    const float qy = std::fabs(py - hh) - (hh - r);
    const float ax = (qx > 0.0f) ? qx : 0.0f;
    const float ay = (qy > 0.0f) ? qy : 0.0f;
    const float inner = (std::min)((std::max)(qx, qy), 0.0f);
    return std::sqrt(ax * ax + ay * ay) + inner - r;
}

// 椭圆 SDF（近似欧氏距离，足够做 1px 抗锯齿）
inline float SdEllipse(float px, float py, float hw, float hh)
{
    if (hw <= 0.0f || hh <= 0.0f) return 1.0f;
    const float nx = px / hw;
    const float ny = py / hh;
    const float k  = std::sqrt(nx * nx + ny * ny);
    return (k - 1.0f) * (hw < hh ? hw : hh);
}

inline float Distance(int shape, float px, float py, float hw, float hh, float r)
{
    if (shape == 0) return SdEllipse(px - hw, py - hh, hw, hh);
    return SdRoundRect(px, py, hw, hh, r);
}

// 像素覆盖率：SDF 到 1px 抗锯齿
inline float Coverage(int shape, float px, float py, float hw, float hh, float r)
{
    const float d = Distance(shape, px, py, hw, hh, r);
    const float c = 0.5f - d;
    return (c < 0.0f) ? 0.0f : ((c > 1.0f) ? 1.0f : c);
}

// 外法线（数值梯度）。只给玻璃/液态玻璃的边缘折射用，调用点很少。
inline void Normal(int shape, float px, float py, float hw, float hh, float r,
                   float* outNx, float* outNy)
{
    if (outNx) *outNx = 0.0f;
    if (outNy) *outNy = 0.0f;

    const float e = 0.75f;
    const float dx = Distance(shape, px + e, py, hw, hh, r) -
                     Distance(shape, px - e, py, hw, hh, r);
    const float dy = Distance(shape, px, py + e, hw, hh, r) -
                     Distance(shape, px, py - e, hw, hh, r);
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0e-6f) return;
    if (outNx) *outNx = dx / len;
    if (outNy) *outNy = dy / len;
}

} // namespace surface_shape
