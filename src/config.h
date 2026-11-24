// src/config.h —— 配置持久化与开机自启接口（T3）
// 实现见 config.cpp；对外契约（namespace cfg）在 app_common.h 中冻结。
//
// 存储位置：%APPDATA%\NetBall\config.ini
// 自启位置：HKCU\Software\Microsoft\Windows\CurrentVersion\Run 值 "NetBall"
#pragma once

#include "app_common.h"

// 契约（app_common.h）：
//   void         resetToDefault(AppConfig* c);
//   std::wstring configPath();
//   void         load(AppConfig* c);
//   void         save(const AppConfig* c);
//   bool         getAutoStart();
//   void         setAutoStart(bool on);
//
// 默认值集中在两处：
//   1. 通用字段（形状 0 圆 / 尺寸 88 / 不透明度 85 / 背景 #181C24 / 自动文字色 /
//      刷新 1000ms / 总在最前 / 开机不自启 / 位置未设置）；
//   2. 排版与材质字段由 layout_model::defaults() 写入：
//      任务栏 L"down,up" + 自适应字号 + 118% 行距 + 8dip 项间距 + 亚克力 + 色调 #181B22@55%
//             + 圆角方 + 16dip 模糊/12dip 圆角；
//      悬浮球 L"down/up/mem" + 自适应字号 + 118% 行距 + 6dip 项间距 + 纯色 + 88×88 圆
//             + 16dip 模糊/12dip 圆角。
//   v1.4.0 起形状、模糊、圆角、描边、高光、文字色、系统亚克力开关均按表面独立，
//   旧全局键只用于迁移与降级写回。
//
// v1.1 的 size / shape / shownet / showmem / tbshowdown / tbshowup / tbshowmem 保留为
// 迁移来源：load 时缺新键则据此推导，save 时由排版反推写回，保证可降级。
