// src/ball_window.h —— 悬浮球分层窗口（属主 builder-core-1）
//
// 对外接口由 src/app_common.h（契约 v1.0，冻结）中的 namespace ballwin 声明：
//     typedef void (*CommandFn)(int cmdId);
//     bool create(HINSTANCE hInst, const AppConfig* c, CommandFn onCommand);
//     void destroy();
//     void applyConfig(const AppConfig* c);
//     void setMetrics(const Metrics* m);
//     HWND hwnd();
//
// 实现约定：
//   1. 右键菜单 / 托盘菜单（tray.cpp 会把菜单结果以 WM_COMMAND 发到本窗口）统一
//      通过 create() 传入的 CommandFn 回调给 main.cpp，本文件不直接处理业务命令。
//   2. 拖动结束后窗口位置变化，用 PostThreadMessage(WM_NETBALL_POSCHANGED) 通知
//      UI 线程（main.cpp 的消息循环里捕获并 cfg::save），不占用 CommandFn 的语义。
//   3. WM_TIMER 不由本文件处理：main.cpp 在自有隐藏窗口上 SetTimer 并采样，
//      采样结果经 ballwin::setMetrics 推进来。
//   4. 尺寸按 DPI 缩放：物理像素 = cfg.size * dpi / 96（Per-Monitor V2）。
#pragma once

#include "app_common.h"

// 拖动结束 / 位置变化通知（投递到 UI 线程消息队列，msg.hwnd == NULL）
#define WM_NETBALL_POSCHANGED (WM_APP + 7)
