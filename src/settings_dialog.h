// src/settings_dialog.h —— T4 设置窗口（属主 builder-ui-1）
//
// 对外接口由 src/app_common.h（契约）中的 settings_ui 命名空间声明：
//     typedef void (*ApplyFn)(const AppConfig* c, void* user);
//     typedef void (*PreviewFn)(const AppConfig* c, void* user);
//     void open(HWND owner, HINSTANCE hInst, const AppConfig* cur,
//               PreviewFn onPreview, ApplyFn onApply, void* user);
//     bool isOpen();
//
// 实现约定（本文件只做说明，不新增任何契约外符号）：
//   1. 模态：open() 时 EnableWindow(owner, FALSE)，WM_DESTROY 时恢复，避免 owner 被永久禁用；
//   2. 单实例：窗口已存在时再次 open() 只做 ShowWindow + SetForegroundWindow；
//   3. 不自建消息循环：窗口是普通 WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU 顶层窗口，
//      由宿主 main.cpp 的消息循环驱动，绝不阻塞主循环；
//   4. 保存：组装并钳制 AppConfig 后回调 onApply(cfg, user)，随后 DestroyWindow；
//      取消：直接 DestroyWindow，不回调；
//   5. DPI：用 GetDpiForWindow（动态获取，缺失时退回 GETDEVICECAPS）做全部尺寸/字体缩放，
//      并处理 WM_DPICHANGED 重新布局。
//
// 【已知坑 / 维护须知】
//   (a) 模态只靠 EnableWindow(owner, FALSE)，恢复动作写在 WM_DESTROY 里；WM_CLOSE 必须转成
//       DestroyWindow，否则 owner 永久禁用、悬浮球点不动（本项目点名的坑）。
//   (b) 这里不要 PostQuitMessage：关设置窗不能结束主消息循环。
//   (c) 全部尺寸按 DIP(96 基准) 写在 settings_dialog.cpp 的 ComputeGeo() 里，改布局只改那里。
//   (d) 预览区/色块/所有按钮都是父窗口自绘（BS_OWNERDRAW + 手绘），所以深色底一致；新增控件请沿用，
//       否则会出现系统浅色控件破坏风格。
//   (e) 175% DPI 下窗口物理高约 910px（1080p@175% 可完整显示）；更小屏只能靠工作区钳制保证按钮可见。
#pragma once

#include "app_common.h"
