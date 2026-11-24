---
name: NetBall v1.2.0 缺陷修复
overview: 修复源码审计中发现的高、中优先级缺陷（托盘图标残留、毛玻璃下隐藏失效、排版可删空、线程收尾与 GDI+ 清理顺序、LHM 温度被覆盖），并一并纠正两处潜伏缓冲区越界与契约注释错误；修复后重建并核对版本/哈希链路。
todos:
  - id: fix-exit-tray
    content: 修复退出链路：退出前回收托盘图标，窗口销毁后仍发送 NIM_DELETE
    status: completed
  - id: fix-glass-visibility
    content: 修复毛玻璃下 PaintNow 重新显示已隐藏悬浮球，ApplyNow 增加可见性兜底
    status: completed
  - id: fix-empty-layout
    content: 限制排版删除至少保留 1 项，空 spec 自动回落默认排版
    status: completed
  - id: fix-sensor-lifecycle
    content: 修复 join 超时关句柄、温度覆盖闪烁、FreeLibrary 与 GDI+ 收尾顺序
    status: completed
  - id: fix-buffer-bounds
    content: 修正 JoinText 剩余容量计算与 NVML 路径长度校验
    status: completed
  - id: fix-comment-build
    content: 更正材质枚举注释，构建脚本补 -Wall -Wextra
    status: completed
  - id: rebuild-package-verify
    content: 重建并更新哈希锚点、打包 1.2.0，用 [subagent:code-explorer] 复核改动
    status: completed
    dependencies:
      - fix-exit-tray
      - fix-glass-visibility
      - fix-empty-layout
      - fix-sensor-lifecycle
      - fix-buffer-bounds
      - fix-comment-build
---

## 用户需求

检查现有 NetBall v1.2.0 项目是否存在问题，并针对已确认的缺陷给出可执行修复方案。

## 产品概述

NetBall 是 Windows 桌面常驻的网速/CPU/GPU 监控工具，包含可拖动悬浮球与任务栏信息面板，支持多项目排版、纯色/亚克力/毛玻璃/无底板四种材质、四页设置窗口与系统托盘菜单。本次工作不新增功能，只修复审计中确认的缺陷，保持现有界面与配置语义不变。

## 核心修复点

- 退出程序后托盘图标必须立即消失，不留幽灵图标。
- 毛玻璃材质下关闭「显示悬浮球」后，悬浮球不得因保存设置、切换 DPI 或分辨率变化而重新出现。
- 排版编辑器不允许把项目删至 0 项；配置中出现空排版时应自动回落默认排版，而非显示空白面板。
- 退出时后台传感器线程与 GDI+ 资源应有序收尾，避免使用已关闭句柄。
- 外部 LibreHardwareMonitor 提供的 GPU 温度不应被厂商接口周期清零，导致数值与 `--` 闪烁。
- 修正潜伏的字符串缓冲区越界与契约头中的材质枚举注释错误。
- 重新构建并产出 v1.2.0 安装包，同步校验尺寸与哈希，并完成最小回归。

## 技术栈

- 语言/运行时：C++17、Win32 API、GDI+、DWM、PDH、NVML/ADL、WMI
- 编译器：MinGW-w64 g++ 13.1.0（`C:\application\qt\Tools\mingw1310_64\bin`）
- 构建/打包：`build/build.bat`、`build/package.bat`、Inno Setup 6（`installer/NetBall.iss`）
- 现有架构不变：`main.cpp` 负责消息循环与命令编排，`ball_window` / `taskbar_text` 负责表面渲染，`sensors` 后台线程发布快照，`glass` 负责材质底窗，`text_render` 负责字体缓存，`layout_model` 负责排版解析，`config` 负责 ini 读写

## 实现方案

### 1. 退出链路与托盘回收

- `src/main.cpp:198-205`：在 `ballwin::destroy()` 之前调用 `tray::remove()`，确保 NIM_DELETE 时球窗口仍有效；`Cleanup()` 中保留 `tray::remove()`（幂等，`g_added` 已为 false 时直接返回）。
- `src/tray.cpp:161-177`：去掉 `IsWindow(g_ball)` 这一硬门槛。托盘注册时保存的 HWND 即使已销毁，`Shell_NotifyIconW(NIM_DELETE, &nid)` 仍应发出；仅当从未成功 `NIM_ADD` 时跳过。
- 不改动 `Cleanup()` 中 `tray::remove() → taskbar::destroy() → ballwin::destroy()` 的既有顺序。

### 2. 毛玻璃下可见性保持

- `src/ball_window.cpp:261-325`：`PaintNow()` 入口记录 `const bool wasVisible = IsWindowVisible(g_hwnd) != FALSE;`；末尾改为 `if (hidSelf && wasVisible) ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);`。
- `src/main.cpp:122-136`：`ApplyNow()` 中若 `!g_cfg.showBall`，在 `ballwin::applyConfig()` 之后再次按配置同步可见性，作为双保险；避免 `WM_DPICHANGED`、`WM_DISPLAYCHANGE`、`setMetrics` 任一入口把球显示回来。

### 3. 排版非空约束与默认回落

- `src/settings_dialog.cpp:574-604`：`ListOp` 的删除分支（`op == 1`）增加 `if (b.count <= 1) return;`，与「每行至少 1 项」的既有约束保持一致；同步刷新「移除」按钮可用态。
- `src/layout_model.cpp:443-462`：`clampConfig` 中 `parse` 后若 `count == 0`，按表面写入默认 spec（任务栏 `down,up`；悬浮球 `down/up/mem`）再 `format`，实现不重启自愈；新增一个仅本文件可见的 `ResetLayoutSpec(AppConfig*, Surface)` 辅助函数，避免与 `resetToDefault` 的完整默认值重复。
- `src/config.cpp:370-405` 的旧键迁移逻辑保持不变。

### 4. 传感器线程与 GDI+ 收尾

- `src/sensors.cpp:975-993`：join 超时分支不再 `CloseHandle(g_thread)`、不再把 `g_thread` 置空、不再关闭 `g_wake`；只设置 `g_stop` 并保留句柄交给进程退出回收。新增 `g_stopping` 标志，`start()` 在退出中直接返回 false，防止重复启动。
- `src/main.cpp:258-283`：`Cleanup()` 在 `GdiplusShutdown` 之前依次调用 `text_render::shutdown()`、`glass::destroy()`；顺序置于 `taskbar::destroy()`、`ballwin::destroy()` 之后，避免玻璃底窗 owner 已失效。
- `src/text_render.h:36`、`src/glass.h:23-25` 的既有接口签名不变。

### 5. GPU 温度来源合并

- `src/sensors.cpp:813-835`：仅当厂商为 NVIDIA（0x10DE）或 AMD（0x1002）时才写 `gpuTempValid[g]` / `gpuTempC[g]`；读取失败时保留当前快照值（可能来自 LHM），不无条件置 FALSE。Intel 核显完全不进入覆盖分支。
- `MergeGpuTemps` 的优先级保持不变：厂商温度成功时覆盖，失败时保留 LHM 结果。
- 顺带在 `ShutdownNvml/ShutdownAdl/ShutdownPdh` 中补 `FreeLibrary`，统一模块卸载。

### 6. 潜伏越界修正

- `src/taskbar_text.cpp:450-461`：改为按剩余容量追加：
`size_t used = wcslen(out); if (used + 1 < cch) wcsncat(out, item, cch - 1 - used);`
- `src/sensors.cpp:356-362`：追加前计算 `wcslen(alt) + 38 + 1 <= MAX_PATH`，不满足则跳过 NVML 备用路径。

### 7. 注释、构建脚本与发布

- `src/app_common.h:79-80`：材质注释更正为 `0=纯色 1=亚克力 2=毛玻璃 3=无底板`，与 `settings_dialog.cpp:97`、`render.cpp:196-206`、`taskbar_text.cpp:522-528` 一致。
- `build/build.bat:81-85`：编译行补 `-Wall -Wextra`，让「零警告」成为构建脚本的常态结果，而非手工参数。
- 生成 `build/out/NetBall.exe` 后记录新的尺寸与 sha256，更新 `build/package.bat:29-30` 的 `EXPECT_SIZE` / `EXPECT_SHA`，再执行 `build\package.bat` 产出 `dist/NetBallSetup-1.2.0.exe`。
- 在 `docs/v1.2.0-变更与升级说明.md` 追加「修复记录与新产物锚点」小节；`.bat` 保持纯 ASCII，`.iss` / `README.txt` 保持 UTF-8 BOM + CRLF。

## 性能与可靠性

- 所有改动均不在采样/绘制热路径上新增分配：`PaintNow` 只多一次 `IsWindowVisible`，温度合并只多一个分支，字符串拼接不增加复杂度。
- 修复后 `setMetrics` 的脏检查、字体缓存、毛玻璃缓存策略均不变，稳态 CPU / 工作集预期与 v1.2.0 文档一致。
- 托盘删除与 `glass::destroy()` 均具备幂等性，重复调用安全。

## 架构与目录影响

不新增模块、不改变分层。仅修改既有文件：

```
d:/agentproject/网速悬浮球/
├── src/main.cpp                 # [MODIFY] 退出前先回收托盘；Cleanup 增加 text_render/glass 收尾；ApplyNow 可见性双保险
├── src/ball_window.cpp          # [MODIFY] PaintNow 记录并恢复原可见性，避免毛玻璃路径重新显示已隐藏的球
├── src/tray.cpp                 # [MODIFY] remove 去掉 IsWindow 门槛，窗口已销毁仍发 NIM_DELETE
├── src/settings_dialog.cpp      # [MODIFY] 列表删除至少保留 1 项
├── src/layout_model.cpp         # [MODIFY] 空 spec 回落默认（新增内部 ResetLayoutSpec）
├── src/sensors.cpp              # [MODIFY] join 超时不关句柄、温度仅厂商支持时覆盖、NVML 路径长度校验、FreeLibrary
├── src/taskbar_text.cpp         # [MODIFY] JoinText 按剩余容量追加
├── src/app_common.h             # [MODIFY] 材质枚举注释更正
├── build/build.bat              # [MODIFY] 增加 -Wall -Wextra
├── build/package.bat            # [MODIFY] 更新 EXPECT_SIZE / EXPECT_SHA
└── docs/v1.2.0-变更与升级说明.md # [MODIFY] 记录修复项与新产物锚点
```

## 验证要点

- 托盘退出：球菜单退出、托盘菜单退出各一次，托盘区无残留图标，进程数归零。
- 毛玻璃隐藏：`ballmat=2` + 关闭「显示悬浮球」→ 保存设置、改 DPI、改分辨率，球保持隐藏；重新打开可正常显示。
- 排版删空：删除至最后 1 项时按钮不生效；手工把 ini 的 `tballlayout` / `balllayout` 置空后启动，自动回落默认排版。
- 温度：`tempsrc=自动` 且运行 LHM 时，非 NVIDIA/AMD 显卡温度保持稳定，不再与 `--` 交替。
- 构建：`build\build.bat` 零警告，`build\package.bat` 通过尺寸+哈希双校验，产出 `dist\NetBallSetup-1.2.0.exe`。

## Agent Extensions

### SubAgent

- **code-explorer**
- Purpose: 在修复完成后对 `main.cpp`、`tray.cpp`、`ball_window.cpp`、`sensors.cpp`、`layout_model.cpp`、`taskbar_text.cpp` 做一次只读复核，确认无新增泄漏、无遗漏调用点、修复点与 `-Wall -Wextra` 构建结果一致。
- Expected outcome: 输出按严重度排序的复核结论，明确「已修复」「仍存在」「新引入」三类，作为交付前的独立闸门。