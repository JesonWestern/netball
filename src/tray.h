// src/tray.h —— T4 托盘图标与右键菜单（属主 builder-ui-1）
//
// 契约（src/app_common.h v1.0 冻结）：
//     namespace tray {
//         bool add(HWND ball, HINSTANCE hInst);
//         void remove();
//         void updateTip(const Metrics* m);
//     }
//
// 本文件补充说明实现层约定（不新增契约外必需符号，仅提供两个可选的辅助入口）：
//   1. add() 内部用 SetWindowSubclass 接管托盘回调消息（WM_APP+100）与 TaskbarCreated，
//      因此 main.cpp / ball_window.cpp **不需要**转发任何消息给 tray，也不会重复弹菜单；
//   2. 若宿主已经自带转发（老版本 main），请勿再调用 tray::onTrayMessage，否则会弹两次菜单；
//   3. remove() 会撤销子类化并 NIM_DELETE，可在退出时安全重复调用。
//
// 【已知坑 / 维护须知】
//   (a) 托盘回调消息与 TaskbarCreated 由 tray.cpp 内部的 SetWindowSubclass 自行接管；
//       若 main/ball_window 里再写一份同名处理，会出现「一次点击弹两个菜单」。
//   (b) 若将来改回 main 转发，必须先删掉 tray.cpp 的 subclass 分支，两种方式只能留一种。
//   (c) 退出路径要保证 remove() 被调用，否则 NIM_DELETE 不执行、托盘图标残留。
#pragma once

#include "app_common.h"

// 托盘回调消息（add() 会发给 register 的 ball 窗口）
#define NETBALL_TRAY_MSG (WM_APP + 100)

namespace tray {
    // 以下是可选入口：add() 已自动接管，正常无需手工调用。
    void onTrayMessage(HWND hwnd, WPARAM wParam, LPARAM lParam);
    void onTaskbarCreated();
}
