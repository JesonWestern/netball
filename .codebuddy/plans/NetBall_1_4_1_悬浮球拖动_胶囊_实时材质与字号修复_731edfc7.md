---
name: NetBall 1.4.1 悬浮球拖动、胶囊、实时材质与字号修复
overview: 修复 1.4.0 测试反馈：数据刷新导致拖动断触、胶囊选择后仍是圆形、自绘材质不实时刷新、悬浮球字号随数值变化、球内留白过多；完成后构建 1.4.1 安装包。
todos:
  - id: live-material-refresh
    content: 实现可见状态无隐藏抓屏、形状掩码补全与 500ms 材质刷新，保证拖动不断触
    status: completed
  - id: capsule-aspect
    content: 选择胶囊时自动保持 3:2 宽高比，并同步滑块、标签与配置校验
    status: completed
  - id: stable-font-padding
    content: 用最坏情况指标固定字号，缩小内边距并按实际块高计算椭圆/胶囊文字区
    status: completed
    dependencies:
      - capsule-aspect
  - id: verify-package-141
    content: 验证拖动、实时材质、胶囊、字号与留白，构建并打包 1.4.1
    status: completed
    dependencies:
      - live-material-refresh
      - capsule-aspect
      - stable-font-padding
---

## 用户需求

修复 1.4.0 测试反馈的五个问题：

- 拖动悬浮球期间，采样定时器刷新数值会导致鼠标捕获丢失、拖动中断。
- 设置中选择「胶囊」后悬浮球仍是圆形，形状没有真正生效。
- 亚克力、玻璃、液态玻璃的透明背景不是实时渲染，只有拖动结束才更新一次。
- 运行期数值变化会让字号自动缩放；应保持悬浮球尺寸和字号固定。
- 悬浮球内部留白过多，多行布局时尤其明显。

## 产品概述

NetBall 是任务栏网速面板与桌面悬浮球的双表面小工具。本次修复聚焦悬浮球的拖动稳定性、形状正确性、材质实时性、字号稳定性和文字排版紧凑度，不改变既有材质风格与设置页结构。

## 核心功能

- 拖动期间数据刷新不打断拖动，松手后位置正常保存。
- 选择胶囊后球体自动变为明显的横向胶囊，字号/宽高联动后仍保持胶囊比例。
- 自绘材质在背景变化后自动刷新，刷新过程不闪烁、不丢失拖动捕获。
- 运行期球体尺寸与字号保持固定，只有用户主动调整字号/宽高/形状时才变化。
- 缩小球内边距，并让椭圆、胶囊的可用文字区随实际文字块高度动态计算，多行布局不再出现大片空白。

## 技术栈

- C++17 / Win32 / GDI+，沿用 MinGW-w64 g++ 13.1.0 与 `build/build.bat`。
- 绘制继续使用 32bpp top-down DIB 与 `UpdateLayeredWindow(ULW_ALPHA)`，像素格式 `PixelFormat32bppPARGB`。
- 材质继续复用 `src/glass.cpp`、`src/surface_shape.h`；不引入第三方库。
- 配置继续使用 `%APPDATA%\NetBall\config.ini`，版本升级到 1.4.1。

## 实现策略

### 1. 无隐藏实时抓屏，修复拖动断触

当前 `glass.cpp::EnsureBackdrop()` 在 `self` 可见时执行 `ShowWindow(self, SW_HIDE)` + `DwmFlush()`，隐藏持有鼠标捕获的窗口会触发 `WM_CAPTURECHANGED`，导致拖动中断。

改为「可见状态抓屏 + 形状掩码补全」：

- 抓取区域沿用当前 `margin`（最大模糊半径 + 16px），保证球体周围有一圈干净背景。
- 用 `surface_shape::Distance()` 在抓取缓冲中生成球体形状掩码，并向外膨胀 2-3px，覆盖抗锯齿边缘。
- 对掩码内的像素做扫描线补全：
- 每一行找掩码左右两侧最近的干净像素，做线性插值；
- 每一列找上下两侧最近的干净像素，做线性插值；
- 两个方向都可用时按到干净边缘的距离加权平均；
- 只有一侧可用时使用该侧；都不可用时使用干净环的平均色。
- 补全后再进入现有高斯模糊/材质合成流程。球体自身的旧画面、文字、高光全部被掩码替换，不会留下残影。
- 抓屏失败时复用上一帧有效背景；仍失败则回退纯色+形状遮罩，绝不调用 `ShowWindow(SW_HIDE)`。
- `glass::paint()` 的 `outHidSelf` 保持接口兼容，但可见状态下恒为 FALSE；`ball_window.cpp`、`taskbar_text.cpp` 中隐藏/恢复窗口的逻辑可以移除或保留为空操作。
- 系统实时亚克力路径不受影响；自绘材质抓屏前仍由调用方隐藏系统亚克力底窗。

### 2. 实时材质刷新

- 复用 `app_common.h` 中已定义但未使用的 `TIMER_GLASS`。
- `main.cpp::RestartTimer()` 增加 `SetTimer(g_msgWnd, TIMER_GLASS, 500, nullptr)`。
- `MsgWndProc` 的 `WM_TIMER` 分支调用 `ballwin::refreshMaterial()` 与 `taskbar::refreshMaterial()`。
- `ballwin::refreshMaterial()`：仅当悬浮球可见、材质为亚克力/玻璃/液态玻璃、且未启用系统亚克力时，调用 `glass::invalidateBackdrop(1)` 后 `PaintNow()`。
- `taskbar::refreshMaterial()`：仅当任务栏面板可见、材质为自绘材质、且未启用系统亚克力时，设置 `g_lastValid=false`、`glass::invalidateBackdrop(0)` 后 `sync()`，强制 `ApplyLayout()` 重新合成。
- 拖动中允许刷新，因为不再隐藏窗口；`setMetrics()` 在 `g_dragging` 时只更新 `g_metrics`，跳过数据驱动的 `PaintNow()`，材质刷新仍由定时器负责，松手后再完整重绘。
- 实测 CPU；若稳态超过既定的低占用目标，将间隔放宽到 800ms，或在纯色/无底板/系统亚克力/隐藏状态下直接跳过。

### 3. 胶囊形状

- 设置页 `IDC_BALL_SHAPE0..2` 处理：
- 选择胶囊时，若 `ballW < ballH * 3 / 2`，把 `ballW` 提升到 `ballH * 3 / 2`，上限 400；
- 同步宽度滑块位置与值标签；
- 调用 `ClampBallFontAndSize(false)` 保证球体不小于当前字号所需。
- `ClampBallFontAndSize()` 在字号变化时不再把球体改成最小尺寸；只把字号压到当前球体能容纳的上限。悬浮球尺寸由宽高滑块控制。
- 胶囊宽高变化时保持 3:2：宽度不足时抬宽度，高度不足时按比例降高度，避免被 400 上限挤成方形。
- `layout_model::clampConfig(SURF_BALL)` 增加一次归一化：`ballShape==2` 且 `ballW==ballH` 时把宽度调成高度的 1.5 倍，兼容旧配置和手改 `config.ini`。

### 4. 固定字号

- 在 `render.cpp` 内新增最坏情况指标 `StableMetrics()`：
- `netValid=TRUE`，上下行速度取 `1023.9 MB/s`（格式化后约 `1024 MB/s`）；
- `memPercent=100`、`cpuPercent=100`、`cpuTempC=100`、GPU 数量 4、每张卡负载/温度 100；
- 用于测量当前排版的**最宽文本**。
- 新增 `FitBallFontPx()`：以字体像素为变量做二分，构建最坏情况块，按「形状 + 实际块高」计算可用宽度，同时校验块高不超过可用高度，返回能稳定容纳最大值的字号。
- `ComputeBallFontPx()`：
- `ballFontPx==0` 时直接返回 `FitBallFontPx(StableMetrics())`；
- `ballFontPx>0` 时返回 `min(MulDiv(ballFontPx,dpi,96), FitBallFontPx(StableMetrics()))`；
- 运行期不再使用当前实时数值求字号。
- 删除 `BuildBallContent()` 中按实际文本继续缩字号的兜底循环；字号一次确定后不再变化。文字块用球体形状路径裁剪，极端情况下宁可轻微裁切也不改变字号。
- `ballMinSizeForFont()` 与 `maxFontForBall()` 统一使用最坏情况指标和块高感知面积，使设置页的字号上下限与运行期一致。

### 5. 收紧留白

- `PadPx()` 从 12 DIP 降到 6 DIP（小尺寸下不低于 2 物理像素）。
- 新增块高感知的 `ShapeTextWidth()`：
- 椭圆：`availW * sqrt(1 - (blockH/availH)^2) * 0.97`，不再固定使用 0.6505；
- 胶囊：按 `min(availH, blockH) / 2` 计算端部圆弧内缩，多行时不再按整块可用高度过度内缩；
- 圆角方：在 `availW` 基础上按圆角半径扣除少量内缩。
- `ComputeBallFontPx()`、`BuildBallContent()`、`ballMinSizeForFont()`、`maxFontForBall()` 共用同一套面积函数。
- `DrawBallText()` 增加球体形状裁剪，文字绝不会画到形状之外；文字块居中绘制，内外边距明显缩小。

## 实现注意

- `glass::EnsureBackdrop()` 增加 shape/corner 参数；首次绘制（窗口隐藏）仍走干净抓屏，可见状态走补全抓屏。
- 补全缓冲复用 `std::vector` 的容量，避免每 500ms 重新分配；抓屏区域最大约几百×几百像素，补全+模糊控制在数毫秒内。
- 任务栏面板的补全掩码使用当前 `PanelLayout.shape/cornerPx`；面板隐藏、自动隐藏、全屏、纯色/无底板、系统亚克力时直接跳过刷新。
- 悬浮球系统亚克力开启时，`refreshMaterial()` 直接返回；不要同时跑两套实时背景。
- 版本升级到 1.4.1 时同步 `app_common.h`、`app.rc`、`installer/NetBall.iss`、`build/package.bat`、`assets/installer/README.txt`；构建后重新计算 `EXPECT_SIZE/EXPECT_SHA`。
- 不改变材质颜色、浓度、圆角、描边、高光等既有参数语义；仅修复刷新、形状、字号和排版。

## 目录结构

```text
src/
├── glass.h                      # [MODIFY] paint 接口注释、outHidSelf 语义改为不再隐藏窗口
├── glass.cpp                    # [MODIFY] 可见状态抓屏、形状掩码补全、背景/模糊/材质缓存
├── ball_window.h                # [MODIFY] 增加 refreshMaterial 声明
├── ball_window.cpp              # [MODIFY] 拖动中跳过数据驱动重绘、调用无隐藏抓屏、移除 hidSelf 恢复逻辑
├── taskbar_text.h               # [MODIFY] 增加 refreshMaterial 声明
├── taskbar_text.cpp             # [MODIFY] 强制重绘、无隐藏抓屏、刷新自绘材质
├── main.cpp                     # [MODIFY] TIMER_GLASS 500ms 实时材质刷新
├── render.h                     # [MODIFY] 注释更新
├── render.cpp                   # [MODIFY] StableMetrics、FitBallFontPx、ShapeTextWidth、PadPx=6、文字裁剪
├── text_render.cpp              # [MODIFY] MinBallSize 安全系数与胶囊/椭圆文字区衔接
├── settings_dialog.cpp          # [MODIFY] 胶囊宽高联动、字号不再改球体、滑块与标签同步
├── layout_model.cpp             # [MODIFY] 胶囊 1.5:1 归一化
├── app_common.h                 # [MODIFY] NETBALL_VERSION_STR 1.4.1
└── app.rc                       # [MODIFY] FILEVERSION/PRODUCTVERSION/FileVersion/ProductVersion/IDS_VERSION 1.4.1

installer/NetBall.iss            # [MODIFY] MyAppVersion/VersionInfoVersion/OutputBaseFilename 1.4.1
build/package.bat                # [MODIFY] 输出名 1.4.1、EXPECT_SIZE/EXPECT_SHA
assets/installer/README.txt      # [MODIFY] 版本号与实时材质/字号说明
```

## 关键接口

```cpp
// glass.h：可见状态下不再隐藏 self，outHidSelf 恒为 FALSE
bool paint(int slot, HWND self, const RECT&amp; screenRect, int dpi,
           const MaterialSpec&amp; spec, BYTE* bits, int stride, int w, int h,
           BOOL* outHidSelf = nullptr);

// render.cpp 内部：按实际文字块高度计算形状可用宽度
int ShapeTextWidth(int shape, int availW, int availH, int blockH, int cornerPx);
```

## 验证方案

- 拖动：用 PowerShell `mouse_event` 按住球心，持续移动 3 秒并让采样周期为 500ms；球心必须持续跟随鼠标，松手后位置正确保存，不能出现 `WM_CAPTURECHANGED`。
- 胶囊：把 `ballShape=2、ballW=ballH=88` 写入配置后启动，配置应归一化为约 132×88；帧差法测得轮廓覆盖率约 0.857，10% 高度处内缩约 0.133。
- 实时材质：在球后放置彩色图案窗口，不拖动球，等待 1 秒后截图，材质内部应反映新背景；连续抓取 2 秒帧序列，不能出现球体消失的闪烁帧。
- 字号：让速度从短字符串变化到长字符串，连续截图，球体窗口矩形与文字行高必须保持不变。
- 留白：多行 `down/up/mem` 截图，文字块高度/球体高度占比明显提升，文字不贴边、不越出形状。
- 构建：`build\build.bat` 零警告；`build\out\NetBall.exe` 版本 1.4.1。
- 打包：更新 `build/package.bat` 的 `EXPECT_SIZE/EXPECT_SHA` 后运行，生成 `dist\NetBallSetup-1.4.1.exe`，校验 FileVersion 与 SHA256。