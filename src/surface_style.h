// src/surface_style.h —— 表面高光 / 内阴影 / 描边（v1.3.0）
//
// 纯色、亚克力、玻璃、液态玻璃、无底板共用；材质底色与颗粒/折射由
// glass::paint 在像素层完成，本文件只负责描边与顶部高光/内阴影，
// 调用方不要重复叠加，否则会出现双倍高光。
//
// 约定：
//   · 只在 UI 线程调用，g 必须已设置 AntiAlias。
//   · 所有渐变都裁剪到 path 内，不会溢出形状。
//   · border 是 1px 白色描边（alpha 可调）；sheen 是顶部高光 + 底部内阴影。
#pragma once

#include <objidl.h>
#include <gdiplus.h>

namespace surface_style {

void drawOverlay(Gdiplus::Graphics& g, Gdiplus::GraphicsPath& path,
                 int w, int h, bool border, bool sheen, int borderAlpha = 70);

} // namespace surface_style
