---
name: NetBall v1.3.0 材质温度尺寸改造
overview: 把设置窗口改为实时预览（改动即刻作用于桌面悬浮球/任务栏，取消可回滚）；材质与温度来源改为平铺选项、选中即生效；新增 HWiNFO 共享内存温度源并保留内置/LHM 全部可选；悬浮球改为固定尺寸、字号与球体双向制约并可视化；高光与内阴影覆盖全部材质且可开关。
design:
  styleKeywords:
    - Glassmorphism
    - Dark
    - Card-based
    - Precision Instrument
    - Live Preview
  fontSystem:
    fontFamily: Microsoft YaHei UI
    heading:
      size: 15px
      weight: 600
    subheading:
      size: 13px
      weight: 500
    body:
      size: 13px
      weight: 400
  colorSystem:
    primary:
      - "#4C7DFF"
      - "#3B66E0"
    background:
      - "#1E2028"
      - "#262A33"
      - "#2E323C"
    text:
      - "#E6E9F0"
      - "#9AA3B2"
    functional:
      - "#3ECF8E"
      - "#F5A524"
      - "#E5484D"
todos:
  - id: preview-hooks
    content: 扩展 settings_ui 接口并在 main.cpp 实现轻量预览与取消回滚
    status: completed
  - id: material-temp-cards
    content: 把材质与温度来源改为平铺选择卡，选中即通过预览通道生效
    status: completed
    dependencies:
      - preview-hooks
  - id: hwinfo-source
    content: 新增 sensors_hwinfo 共享内存读取并接入 sensors 的温度合并与状态发布
    status: completed
  - id: fixed-ball-constraints
    content: 悬浮球固定尺寸，实现字号与球体双向制约及拖动时的动态提示
    status: completed
    dependencies:
      - preview-hooks
  - id: surface-overlay
    content: 新增 surface_style 并把高光/内阴影覆盖到纯色、亚克力、无底板
    status: completed
  - id: version-build-package
    content: 升版本 1.3.0、零警告构建、更新哈希、打包并覆盖安装系统旧版
    status: completed
    dependencies:
      - material-temp-cards
      - hwinfo-source
      - fixed-ball-constraints
      - surface-overlay
  - id: verify-regression
    content: 用 [subagent:code-explorer] 复核改动并跑预览/回滚/材质/尺寸/温度/退出回归
    status: completed
    dependencies:
      - version-build-package
---

## 产品概述
重做 NetBall 设置窗口，解决文字重叠、说明被裁切、GPU 列表显示不全且无法滚动的问题，同时保留四页结构与全部现有功能。

## 核心功能
- 窗口由 460×546 放大到 520×680，保持「常规 / 任务栏 / 悬浮球 / 材质与数据」四页签。
- 允许拖动窗口边缘缩放，最小尺寸 520×680；放大时列表、预览区、GPU 文本框跟随扩展，底栏始终贴底。
- 所有长说明文字自动换行、完整显示，任何页面不得出现文字压住控件或被裁切。
- GPU 区域改为只读多行文本框，带垂直滚动条，可上下滚动、可选中复制显卡名称。
- 行高、控件间距、值标签宽度统一重排，消除滑块与「· 上限 / · 下限」提示的挤压。
- 保持实时预览、取消回滚、材质/温度卡片选中态、排版编辑器、恢复默认等行为不变。

## 视觉结果
深色玻璃拟态设置面板，四页签固定在顶部，内容区留白均匀；材质与温度仍是横排卡片；GPU 区域是一个带细边框、可滚动的深色文本块；底部「恢复默认 / 取消 / 保存」固定在窗口底部。


## 技术栈
- C++17、Win32 API、GDI/GDI+、Common Controls（ListBox/TrackBar/Edit）
- MinGW-w64 g++ 13.1.0，`build\build.bat`（已含 `-Wall -Wextra`，纯 ASCII）
- 打包：`build\package.bat` + Inno Setup 6；版本 v1.3.1
- 不新增源文件、不引入第三方库，全部改动集中在 `src/settings_dialog.cpp`，外加版本号与文档

## 实现方案

### 1. 布局引擎：520×680 基准 + 可缩放
- 把 `ComputeGeo(int dpi)` 改为 `ComputeGeo(int dpi, int clientW, int clientH)`，设计基准常量 `kDesignW=520`、`kDesignH=680`。
- 所有控件仍用 `Rc(dpi,l,t,r,b)` 表达设计稿坐标；额外空间按下述规则分配，避免整体等比缩放导致字体与控件比例失调：
  - 页签、显示开关、材质/温度卡片、底部按钮保持固定高度；
  - 底栏锚定客户区底部：`btnReset.top = clientH - SD(46)`，其上方 14 DIP 画分隔线；
  - 常规页：`previewTop` 固定，`previewBottom` 向下拉伸到内容区底部；
  - 任务栏页：`tbList` 向下拉伸，字号/行距/间距/材质整体锚定在列表下方；
  - 悬浮球页：`ballList` 向下拉伸，五组滑块与材质卡片整体锚定在列表下方、材质行之上；
  - 材质页：GPU 文本框向下拉伸到「重新探测」按钮上方，按钮与说明锚定底部；
  - 横向：`ListBox`、预览面板、滑块、GPU 文本框、说明区的右边界跟随 `clientW` 扩展；值标签固定在右边缘。
- `ApplyPage()` 改为使用当前 `g.geo`（由当前客户区尺寸算出），切页与缩放都走同一条重排路径。

### 2. 可缩放窗口
- 窗口 style 增加 `WS_THICKFRAME | WS_MAXIMIZEBOX`，保留 `WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN`。
- `WM_GETMINMAXINFO`：最小跟踪尺寸 = 520×680 DIP 经 `AdjustWindowRectEx` 换算后的像素值，防止缩到布局崩坏。
- `WM_SIZE`：非最小化时更新 `g.clientW/g.clientH`，重算 Geo，调用 `ApplyPage()`；用 `g.inLayout` 标志防止 `SetWindowPos` 递归触发。
- `WM_DPICHANGED`：按新 DPI 重建字体、重算 Geo、按系统建议矩形设置窗口，再重排。
- 重排性能：`ApplyPage()` 内用 `BeginDeferWindowPos/DeferWindowPos/EndDeferWindowPos` 批量移动，减少拖动缩放时的闪烁；只在尺寸/DPI/切页时重排，不在 `WM_PAINT` 中做布局计算。

### 3. 文字裁切 / 重叠修复
- 删除 `tbHint`、`ballHint`、`matHint` 三个单行 STATIC，改由 `PaintAll()` 用 `DrawTextW(..., DT_LEFT | DT_WORDBREAK | DT_NOPREFIX)` 在 Geo 预留的说明区绘制；行高按 `18 DIP` 计算，说明区高度按 3–4 行预留。
- 说明区位置：任务栏页在材质行下方、悬浮球页在材质行下方、材质页在「重新探测」下方并锚定底栏上方。
- 短标签（形状、字号、行距、宽、高、色调、强度等）继续用 STATIC，但统一 22 DIP 高、垂直居中；值标签宽度由 122 DIP 扩到 150 DIP，保证「34 px · 上限」不被裁切。
- 行距统一：内容行 pitch 36 DIP，组间额外 8 DIP；材质/温度卡片保持 30 DIP 高、6 DIP 间隔。

### 4. GPU 只读文本框
- `IDC_GPUINFO` 由 STATIC 改为 `EDIT`：`WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL`。
- 创建后 `EM_SETLIMITTEXT` 设为 8192，`EM_SETMARGINS` 设 8 DIP 内边距；`FillGpuInfo()` 继续用 `SetWindowTextW` 写入 `\r\n` 文本。
- 增加 `WM_CTLCOLOREDIT`：文字 `kText`、背景 `kPanelBg`，返回常驻 `kPanelBg` 画刷。
- 因为去掉了系统边框，在 `PaintAll()` 里沿 EDIT 矩形画 1px `kSwatchBd` 圆角细框，保证与整体深色卡片一致；EDIT 本身底色相同，不遮挡父窗口绘制。
- 注册到 `Reg(g.gpuInfo, 3, IDC_GPUINFO)`，缩放时由 `ApplyPage()` 调整高度；滚动条由控件自身处理，鼠标滚轮可用。

### 5. PaintAll 适配
- 背景、8 个色块、两个预览面板、底部分隔线全部改用新 Geo 计算；预览面板宽度跟随 `clientW`，`previewBottom` 高度跟随内容区。
- 新增 `PaintHints()`：按当前页绘制对应说明；切页时 `InvalidateRect` 重绘。
- GPU 文本框的细边框在 `PaintAll()` 中绘制；`WM_PAINT` 仍走双缓冲，避免缩放闪烁。

### 6. 版本与交付
- 版本升到 v1.3.1，同步 `app_common.h`、`app.rc`、`app.manifest`、`NetBall.iss`、`package.bat`、`README.txt`。
- 新增 `docs/v1.3.1-变更与升级说明.md`，记录窗口尺寸、缩放行为、GPU 文本框与文字修复。
- 构建零警告后更新 `EXPECT_SIZE/EXPECT_SHA`，打包 `dist\NetBallSetup-1.3.1.exe`，覆盖安装到 `C:\Program Files\NetBall`。

## 实现注意事项
- `WM_SIZE` 在 `WM_CREATE` 完成前可能到达：`ApplyPage()` 必须判断 `g.ctrlCount > 0` 且窗口已创建，避免对空句柄 `SetWindowPos`。
- 缩放期间不要重复创建字体/画刷；字体只在 `WM_DPICHANGED` 重建，画刷保持静态。
- `ComputeGeo` 只在尺寸/DPI/切页时调用一次，避免 `WM_PAINT` 里重复计算造成拖动卡顿。
- 说明文字从 STATIC 改为自绘后，不再参与 Tab 焦点；现有 Tab 顺序不受影响。
- GPU EDIT 只读但允许选中复制，保留 `ES_NOHIDESEL`，避免失去焦点时选区消失。
- 所有控件改动仍必须调用 `SchedulePreview()`，实时预览/取消回滚链路不得回归。

## 目录结构
```
d:/agentproject/网速悬浮球/
├── src/
│   └── settings_dialog.cpp        # [MODIFY] 520×680 布局、可缩放重排、说明自绘、GPU EDIT、PaintAll 适配
├── src/app_common.h               # [MODIFY] 版本号 1.3.1
├── src/app.rc                     # [MODIFY] FileVersion/ProductVersion/IDS_VERSION 1.3.1
├── src/app.manifest               # [MODIFY] version 1.3.1.0
├── installer/NetBall.iss          # [MODIFY] 1.3.1 版本与输出名
├── build/package.bat              # [MODIFY] 文件名与 EXPECT_SIZE/EXPECT_SHA
├── assets/installer/README.txt    # [MODIFY] 1.3.1 说明（保持 BOM+CRLF）
└── docs/v1.3.1-变更与升级说明.md   # [NEW] 本次 UI 重做记录与产物锚点
```


## 设计风格
延续现有深色玻璃拟态与「精密仪表」气质，强化留白与层级：深灰蓝底、细描边、低饱和高光、单一强调色。窗口放大到 520×680 后，四页签固定在顶部，内容区按 8/12 DIP 网格排布，底栏固定贴底。

## 页面规划
- **常规页**：顶部页签栏；形状与透明度；背景色与文字色；刷新频率与双表面预览。预览面板占满剩余高度。
- **任务栏页**：页签栏；显示开关；排版列表与 6 个操作按钮；字号/行距/间距三组滑块；四张材质卡片；底部说明与保存栏。
- **悬浮球页**：页签栏；显示开关；排版列表；字号/行距/间距/宽/高五组滑块；四张材质卡片；底部说明与保存栏。
- **材质与数据页**：页签栏；两表面色调与强度；模糊半径与圆角；高光/内阴影开关；五张温度来源卡片；可滚动 GPU 文本框；重新探测与底部说明。

## 视觉细节
- 页签 104×34 DIP，选中项强调色底 + 顶部 2 DIP 高光；未选中仅文字提亮。
- 材质/温度卡片 30 DIP 高，选中态强调色 1px 描边 + 顶部 2 DIP 高光条。
- 分组之间用 8 DIP 空白与 1px `kLine` 细分隔线区分；说明文字统一 `kTextDim`。
- GPU 文本框使用 `kPanelBg` 底、1px `kSwatchBd` 细框、8 DIP 内边距，右侧滚动条不抢视觉。
- 缩放时底栏不动、内容区拉伸，预览面板与 GPU 文本框优先获得多余高度。

## 交互
- 拖动窗口边缘实时重排，不闪烁；最小 520×680，最大化时内容铺满。
- 所有设置改动仍即时预览；取消回滚、保存写盘行为不变。

## Agent Extensions
### SubAgent
- **code-explorer**
  - Purpose: 在布局与控件改造完成后做只读复核，确认 `WM_SIZE` 重排无递归、说明区不压控件、GPU EDIT 注册与绘制正确、实时预览链路未回归。
  - Expected outcome: 输出「已修复 / 仍存在 / 新引入」三类结论，并给出各页控件矩形是否越界或重叠的静态证据。
