// src/glass.h —— 表面材质合成器（v1.4.0 重写）
//
// 设计目标（与 v1.2 的“系统亚克力 + 自绘毛玻璃”彻底分开）：
//   1. 自绘材质是主路径：抓取表面背后的屏幕像素 → 真实高斯模糊 → 色调/透射/
//      颗粒/高光/边缘折射 → 形状遮罩 → 预乘 ARGB。形状由 SDF 遮罩保证，
//      胶囊不会因为独立底窗的矩形 region 而变成矩形。
//   2. 系统亚克力是可选实验路径：仅当材质=亚克力且 systemAcrylic=TRUE 时使用；
//      Win11 非激活窗口的 accent 可能退化为纯色，失败/不可用时调用方回退自绘。
//   3. 背景抓取与材质合成分层缓存：
//        · 背景按矩形缓存，移动/换壁纸/换主题/改 DPI 才重抓；
//        · 模糊按背景代次 + 模糊半径缓存；
//        · 材质按模糊代次 + 全部外观参数缓存。
//      因此拖动色调/浓度滑块只重合成、不重抓屏，数值刷新只 memcpy。
//   4. 只在 UI 线程调用。paint() 会在需要抓屏时临时隐藏 self，DwmFlush 等一帧
//      再 BitBlt，抓完恢复原可见性；不需要抓屏时完全不碰窗口。
//
// 形状：0=椭圆 1=圆角方 2=胶囊（与 AppConfig 的 tbShape/ballShape 一致）。
// 材质：0=纯色 1=亚克力 2=玻璃 3=液态玻璃 4=无底板。
#pragma once

#include "app_common.h"

namespace glass {

enum MaterialType {
    MAT_SOLID   = 0,
    MAT_ACRYLIC = 1,
    MAT_GLASS   = 2,
    MAT_LIQUID  = 3,
    MAT_NONE    = 4
};

struct MaterialSpec {
    int      type;          // MaterialType
    int      shape;         // 0/1/2
    int      cornerPx;      // 仅 shape==1，物理像素
    int      blurPx;        // 0..40 DIP 换算后的物理像素
    COLORREF tint;
    int      concentration; // 0..100（颜色浓度 / 遮挡强度）
    BOOL     border;        // 1px 高光描边
    BOOL     sheen;         // 顶部高光 + 内阴影
    BOOL     systemAcrylic; // 仅 MAT_ACRYLIC 有效
};

// 进程内能力探测（DWM 合成 + 透明效果 + SetWindowCompositionAttribute）
bool acrylicAvailable();

// 把材质写进调用方的 32bpp top-down DIB（BGRA，预乘 alpha）。
//   slot      0=任务栏面板 1=悬浮球（缓存按槽位分开）
//   self      需要抓屏时临时隐藏的窗口（可为 NULL）
//   screenRect 表面在屏幕上的矩形（物理像素）
//   dpi       当前 DPI，用于把 blurPx/cornerPx 从 DIP 换算成物理像素
// 返回 false 仅表示参数无效；抓屏失败会自动回退为“纯色 + 形状遮罩”，
// 仍会写满 bits 并返回 true，保证不会出现空白面板。
// v1.4.1 起不再隐藏 self：可见状态下用形状掩码补全抓屏，outHidSelf 恒为 FALSE，
// 调用方无需再 ShowWindow(SW_SHOWNOACTIVATE) 恢复（保留参数仅为接口兼容）。
bool paint(int slot, HWND self, const RECT& screenRect, int dpi,
           const MaterialSpec& spec, BYTE* bits, int stride, int w, int h,
           BOOL* outHidSelf = nullptr);

// 系统实时亚克力底窗（实验性）。返回 false 表示不可用/创建失败，调用方应回退自绘。
bool placeSystemAcrylic(int slot, HWND owner, HWND textHwnd, const RECT& screenRect,
                        int dpi, const MaterialSpec& spec);
void hideSystemAcrylic(int slot);

// v1.4.3：液态玻璃流光。paint() 只写「无流光」底图；调用方画完文字后缓存一帧，
// 每个动画帧拷贝缓存并用本函数叠加当前时刻的高光带（只依赖时间，与采样解耦）。
void addLiquidBand(BYTE* bits, int stride, int w, int h);

// 背景失效：移动/换壁纸/换主题/DPI/显示器变化时调用。只影响指定表面。
void invalidateBackdrop(int slot);
void invalidateAll();

// 表面背后（球体/面板下方）的实际背景均色，用于自动文字色判定。
//   · 亚克力/玻璃/液态玻璃：glass::paint 已抓过背景，直接命中缓存（O(1)）；
//   · 纯色/无底板：没有材质抓屏，allowCapture=TRUE 时按需抓一帧并缓存，
//     几何不变时不会重复抓屏；
//   · allowCapture=FALSE 只读缓存，几何不一致时返回 false（不抓屏）。
// 返回值 false 表示没有可用背景，调用方应回退到 render::autoTextColor 的推导。
bool backdropColor(int slot, HWND self, const RECT& screenRect, int dpi,
                   int w, int h, int shape, int cornerPx,
                   BOOL allowCapture, COLORREF* out);

// 退出时销毁所有底窗与缓存
void destroy();

} // namespace glass
