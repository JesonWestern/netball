# T14 回归验证报告（verifier-5）

- 被测锚点：`build\out\NetBall.exe` **1,053,696 B** / sha256 `e085727cf87e4df22b551596b2cb53d407ef177ba2539b4d9e6ac0fb33ebe983` / mtime 2026-11-02 21:28:29
- 私有副本：`build\_tmp\v5\NetBall_v5.exe`（sha 与锚点逐字符相等；`Get-FileHash` 与 `certutil` 双实现一致）
- 环境：虚拟屏 x∈[-1920,3840] y∈[0,2160]（主屏左侧另有屏，负坐标合法）；主屏 168 dpi = 175%，左侧副屏 120 dpi = 125%；全部测量线程先 `SetThreadDpiAwarenessContext(-4)`
- 结论：**A/B/C/D 全组通过**。D-01（负坐标）、D-06（拖丢 + 无复位入口）在本构建上**均已修复且可独立复现**。遗留 1 条 RISK（跨 DPI 复位后 20px 悬边）与 3 条测量法说明，见末节。

## A. D-01 负坐标位置记忆（3 条路径，全 PASS）

| 路径 | 期望 | 实测 | 判定 |
|---|---|---|---|
| A1 `posx=-500 posy=300` 启动 | 落在 x≈-500 | `rect L=-500 T=300 R=-346 B=454`（154x154），`|L-(-500)|=0`，**未回落 3662** | PASS |
| A2 在该状态下开设置窗 → 点色块 → 保存(3016) | 球仍在 -500 | 设置窗出现（`NetBallSettingsWnd`），点 `swatch[0]` 后 `bgcolor #181C24 → #262A34`（证明 `ApplyAndClose` 真跑过），保存后 `rect L=-500 T=300`，ini `posx=-500 posy=300` | PASS |
| A3 `posx=99999 posy=99999`（越界） | 回落可见且与虚拟屏有交集 | `rect L=3662 T=1898 154x154`，与虚拟屏交集 **154x154**（100% 在屏内），非法值未被采信 | PASS |

> D-01 的**两个入口**都验到了：①`cfg::load` 不再丢负坐标；②`applyConfig`（保存路径）不再因 `x<0||y<0` 无条件回默认。旧构建在 A1/A2 均会跳到 3662,1898。

## B. D-06 拖动钳制与恢复入口（全 PASS）

| 项 | 期望 | 实测 | 判定 |
|---|---|---|---|
| B1 拖到右下（光标 3838,2158） | `right≤3840 且 bottom≤2160`，0% 出屏 | `L=3686 T=2006 R=3840 B=2160`，右/下溢出 **0 px**（恰贴边），ini 落盘 `3686,2006` | PASS |
| B2 拖到左上（光标 -1900,-50） | `left≥-1920 且 top≥0` | `L=-1920 T=0 R=-1810 B=110`（110x110，副屏 125%），左/上恰贴虚拟屏边界 | PASS |
| B3c 托盘「重置位置」1004 | 回默认可见位置 | `(700,300) → (3662,1898)` 154x154，完全在屏内 | PASS |

真实物理拖拽（`SetCursorPos` + `mouse_event` 左右键），因为拖动逻辑读 `GetCursorPos()`；不是发假 `WM_MOUSEMOVE`。

### 菜单完整枚举（MN_GETHMENU，`GetMenuItemCount`+`GetMenuStringW`+`GetMenuItemID`+`GetMenuState`）

球右键菜单 **count=8**：

| # | ID | 文本 | 勾选 | 分隔 | 灰 |
|---|---|---|---|---|---|
| 0 | 1001 | 设置(&S)... | 0 | 0 | 否 |
| 1 | 0 | (separator) | 0 | **1** | — |
| 2 | 1002 | 总在最前(&T) | **1** | 0 | 否 |
| 3 | 1003 | 开机自启(&A) | 0 | 0 | 否 |
| 4 | 1004 | 重置位置(&R) | 0 | 0 | 否 |
| 5 | 0 | (separator) | 0 | **1** | — |
| 6 | 1005 | 关于(&B) | 0 | 0 | 否 |
| 7 | 1006 | 退出(&X) | 0 | 0 | 否 |

托盘菜单 **count=5**：

| # | ID | 文本 | 勾选 | 分隔 | 灰 |
|---|---|---|---|---|---|
| 0 | 1001 | 设置(&S)... | 0 | 0 | 否 |
| 1 | 1010 | 显示或隐藏(&H) | 0 | 0 | 否 |
| 2 | **1004** | 重置位置(&R) | 0 | 0 | 否 |
| 3 | 0 | (separator) | 0 | **1** | — |
| 4 | 1006 | 退出(&X) | 0 | 0 | 否 |

6 命令 + 2 分隔 / 4 命令 + 1 分隔，命令项**全部非灰**，与 AC-12 新口径一致（不沿用 8/4 旧值）。托盘菜单由 `WM_APP+100`(lParam=WM_RBUTTONUP) 触发，弹窗期间截图 `media-v5\b3-ball-menu.png`、`b3-tray-menu.png`。

## C. 回归（12 项，全部在新锚点上重取）

| 项 | 判定 | 实测 |
|---|---|---|
| AC-1 | PASS | 1,053,696 B；FileDescription `NetBall 网速悬浮球` **13 码点**（U+004E…U+0020,U+7F51,U+901F,U+60AC,U+6D6E,U+7403 逐码点相等）；manifest `asInvoker`×1 + `PerMonitorV2`×1（dpiAware 共 4 处）；FileVersion 1.0.0.0 |
| AC-2 | PASS | `exstyle=0x80088`（LAYERED\|TOOLWINDOW\|TOPMOST），APPWINDOW=0；60 s 后仍为 0x80088 |
| AC-5 | PASS | 60 s / 20 采样：WS **min 7.75 / avg 7.99 / max 8.18 MB**（≤15）；Private 3.27 MB；GDI **18→18→18**；Handles 172→173→173（无增长） |
| AC-6 | PASS | 圆 **154x154** / 圆角方 **154x154** / 胶囊 **231x154**；逐行轮廓区分明确（顶行宽 22 / 101 / 99，中行 154 / 154 / 231，底行 24 / 103 / 101） |
| AC-7 | PASS（约值，见测量说明） | 20% → 隐含 alpha **0.200**（白球环带 67.00 / 背景 20.00，(67−20)/235=0.200；白黑球双色解法 0.180）；100% → **0.977**（(249.56−20)/235；双色解法 0.957） |
| AC-8 | PASS | 严格 maximin @85%：**8.88 / 15.08 / 14.84 / 5.28 / 4.68 / 6.10 / 5.28 / 5.33**，8/8 ≥4.5，最小 **4.68**（绿），与文档值逐项差 ≤0.05 |
| AC-9 | PASS | 14 键全破坏（opacity=999/size=999/shape=9/refreshms=abc/bgcolor=zzz/posx=notanumber…）→ 不崩、size 回落 88(154px)、位置回落 3662,1898、`IDM_EXIT` 优雅退出后 **config 回写为默认 5 项全对**；删 `size` 键 → 重建为 88 |
| AC-10 | PASS | 正坐标 3000,300 → `rect 3000,300` 精确；负坐标 -500,300 → `rect -500,300` 精确（旧版负坐标此项 FAIL，现修复） |
| AC-11 | PASS | 第二实例 `WaitForExit=True`、exit code **0**；进程数 **1 → 1** |
| AC-12 | PASS | 见上表：球菜单 8 项 / 托盘 5 项，ID 与勾选全部符合 |
| AC-16 | PASS | `build\out` 仅 `NetBall.exe`（1 个条目）；工程根仅 `.agent assets build dist docs installer src tools`（8 项，0 散落） |
| AC-17 | PASS | `GetDpiForSystem=168`（175%）；48→**84x84**、160→**280x280**、胶囊160→**420x280**，比值 1.75/1.75/2.625 |

## D. 遗留物与卫生

- `src\**` 关键字 `TODO|FIXME|XXX|占位|省略|placeholder|HACK`：**0 命中**
- `docs\**` 下 `*.pyc`：**0 个**（`docs\verify\__pycache__` 已清，审计 D-07 关闭）
- `build\_tmp\fix1\`：23 个文件（build/compile 日志、`.o`、`config.diff`、`nb_menu.png`、`nb_tray.log`…），确为 builder-fix-1 构建与菜单取证，**保留**
- `build\_tmp\v5\`：`anchor.txt` + `NetBall_v5.exe`（私有副本）
- config 还原：13/13 键与基线逐键机械比对一致（`shape=0 size=88 opacity=85 textauto=1 shownet=1 showmem=1 refreshms=1000 alwaysontop=1 autostart=0 posx=-1 posy=-1 bgcolor=#181C24 textcolor=#FFFFFF`），无多余键，文件 sha256 `42cc1cadda4b2efcb357e5add28fccd87316b25a846a55993fd12d92f75ba157`
- 进程：`NetBall`/`NetBall_v5` 计数 **0**；全程只按 PID 杀自己的副本，未用 `taskkill /f /im`
- 锚点：跑完后 `build\out\NetBall.exe` 仍为 1,053,696 B / `e085727c…`（期间无重建）

## RISK / 测量法说明（不掩盖、不夸大）

1. **RISK（低）：跨 DPI 复位后 20px 悬边。** 球在 125% 副屏上时触发「重置位置」，默认位置按 110px 计算得 (3706,1942)，随后跨屏到 175% 使窗口放大到 154px，右边缘 3860 比虚拟屏右界多 **20px（13%）**，仍 87% 可见可点。D-06 验收项（拖动 0% 出屏 / 托盘复位可用）**不受影响**；钳制只在拖动与捕获丢失时执行，`WM_DPICHANGED` 后未再钳制。建议 v1.1 在 `WM_DPICHANGED` 里补一次 `ClampWindowFullyInside`。
2. **AC-7 存在约 2–4% 系统性抓图偏差**：100% 三次测量落在 0.957~0.977（t7b 同期测得 0.988）。20% 与 100% 相差 0.78 个 alpha 单位，足以证明不透明度链路生效；偏差源于 `CopyFromScreen` 合成/取整，不是产品缺陷。**未按「约」字放宽成 1.000 写死**。
3. **形状覆盖率不可用于判 PASS**：本机壁纸有抖动噪点，第一次用「矩形外 40px 底色」做参考、第二次用「四角底色」做参考均被污染；改用「同区域有球/无球两帧相减」后，160px 两组的 off 帧四角与 on 帧差异达 40–157（两帧之间桌面被别的窗口改变），该组数字作废。故 AC-6 以**窗口几何 + 逐行轮廓**判定（几何精确、轮廓三类可辨），不引用覆盖率。作废数据保留在 `run_C3_pixels.log`、`run_C4_shapes.log`、`run_C5_inspect.log`。
4. **共享控制台会被外部 Ctrl+C 打断**：首轮 A3 与 B1 各被切断一次（日志尾部 `^C`）。改为 `run_v5.cmd` + `start /min` 脱离控制台后全部跑完；后续成员建议直接用该方式。被打断那轮留下的 `NetBall_v5` 进程已按 PID 清掉。

## 证据清单

- 脚本/日志（`docs\verify\v5\`）：`run_0_anchor.ps1/.log`、`run_A.ps1/.log`、`run_A3.ps1/.log`、`run_B.ps1/.log`、`run_B3c.ps1/.log`、`run_C1.ps1/.log`、`run_C2.ps1/.log`、`run_C3_pixels.ps1/.log`、`run_C4_shapes.ps1/.log`、`run_C5_inspect.ps1/.log`、`run_C6_shape_profile.ps1/.log`、`run_C7_fix.ps1/.log`、`run_C8_alpha.ps1/.log`、`run_Z_hygiene.ps1/.log`、`run_v5.cmd`
- 截图（`docs\verify\media-v5\`，41 张）：`a1-neg500.png`、`a2-settings.png`、`a3-illegal.png`、`b1-bottomright.png`、`b2-topleft.png`、`b3c-after-reset.png`、`b3-ball-menu.png`、`b3-tray-menu.png`、`c1-shape-*.png`、`c1-size*.png`、`c2-ac9-corrupt.png`、`c4-*-on/off.png`（含作废组）、`c8-op20/100-{,w}white/black.png`
