// src/taskbar_text.h —— 任务栏信息面板覆盖窗（v1.1.0 引入，v1.2.0 重写排版与材质）
//
// 对外接口由 src/app_common.h 的 namespace taskbar 声明：
//     bool create(HINSTANCE hInst, const AppConfig* c);
//     void destroy();
//     void applyConfig(const AppConfig* c);
//     void setMetrics(const Metrics* m);
//     void sync();
//     HWND hwnd();
//
// 实现约定：
//   1. 顶层分层窗口贴在任务栏上，不 SetParent 注入 Explorer（Win11 XAML 任务栏
//      下跨进程子窗口不稳定）。窗口带 WS_EX_TRANSPARENT，鼠标点击穿透。
//   2. 生命周期的创建/销毁由 main.cpp 负责；位置、可见性、主题由 main.cpp 的
//      250ms 定时器调用 sync() 推进，窗口过程自身也响应显示设置变化。
//   3. 面板尺寸由排版内容决定（最多两行），允许向上凸出任务栏；宽度用「只增不减」
//      的高水位，避免每秒数值变化把左边缘拉来拉去。
//   4. 材质：mat=1 时另建一个亚克力玻璃底窗（DWM 真实模糊，零 CPU 开销），
//      mat=2 时把自绘毛玻璃写进自己的 DIB（缓存复用，只有几何/材质变化才抓屏）。
//   5. 只在「文本 / 颜色 / 字号 / 矩形 / 材质」变化时重绘；文本未变时 sync()
//      只有几次数值比较，CPU 可忽略。
#pragma once

#include "app_common.h"
