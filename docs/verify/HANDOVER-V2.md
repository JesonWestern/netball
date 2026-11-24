# HANDOVER-V2（简版）— T7b 视觉/交互验收

交接：verifier-1 → verifier-3 ｜ 工具在 `docs/verify/`（**只读产品代码**）｜ 证据：你写 `docs/verify/media-t7b/`，脚本复用 `docs/verify/`，**不写 `build\out\`**
环境：DPI **175%**（168 dpi）｜虚拟屏 L=-1920 T=0 W=5760 H=2160

## 1. 六个脚本：路径 / 命令 / 输出 / 阈值

| AC | 命令（在 `docs\verify` 下） | 输出 | 阈值 |
|---|---|---|---|
| 截图入口（全部视觉项必用） | `powershell -File check_screenshot.ps1 -OutFile <绝对路径>.png -ExpectText` | PNG + `rect/fill/ball/ink/spill/METRIC` | PNG≥200B；distinct>1（纯色=FAIL）；`-ExpectText` 时 ink 存在且 ink 框距球体框 ≥2px |
| AC-6 形状/尺寸 | 手写 `%APPDATA%\NetBall\config.ini` 的 `shape=0\|1\|2`、`size=48\|88\|160` → 重启 → 上面截图 | `shape-<s>-<sz>.png` | 球体框 ≈ `size×1.75`（±3px）；三形状填充覆盖率不同（圆≈78%，圆角/胶囊>90%）；文字不溢出 |
| AC-7 透明度 | `python check_ac7_alpha.py --low alpha-20.png --high alpha-100.png` | 两图 `lum` + `diff` | `|lum差| ≥ 0.20`；自检 `--selftest` |
| AC-8 对比度 | `python check_ac8_contrast.py --shots "docs\verify\shape-*.png"`；数学 `--math`；自检 `--selftest` | 表格 `bg/text/contrast/verdict` | 每图 ≥4.5:1；`--math` 72 组 ≥4.5（当前最差 4.766） |
| AC-17 DPI | `powershell -File check_ac17_dpi.ps1 -SizeSweep` | `dpi-175.png` / `-size48` / `-size160` / `config-ac17-backup.ini` | scale 必须==175；awareness≠UNAWARE；`|winDpi-sysDpi|≤8`；两档截图 `-ExpectText` 通过 |
| AC-10 位置记忆 | 无脚本：记 `GetWindowRect` → `SetWindowPos(300,200)` → `WM_CLOSE` → 重启 → 比对 | `pos-before/after.png` | 重启后 (L,T) 一致 ±2px；`posx/posy` 同步；`-1` 表示「未设置」不是「没保存」 |
| AC-12 托盘/设置窗 | 无脚本：`PostMessage` 触发 → 立刻截图 → `VK_ESCAPE`；判据见 `REPORT-TEMPLATE.md` AC-12 表 | `settings-*.png`、`menu-*.png` | 菜单三项齐全；显示/隐藏 → `IsWindowVisible=false` 再恢复；tooltip 截不到判 `SKIP-无法取证` |

## 2. 已知坑（8 条，全部踩过）

1. **`GetDpiForSystem` 在 DPI-unaware 宿主里返回 96**（被虚拟化），会把 175% 骗成 100%。→ 先 `SetThreadDpiAwarenessContext(-4)`（或进程级 `SetProcessDpiAwarenessContext`），再取 `GetDpiForMonitor(MDT_EFFECTIVE_DPI=0)`；本机权威值 168 dpi。
2. **不 aware 时 `GetSystemMetrics(SM_*VIRTUALSCREEN)` 与 `GetWindowRect` 坐标不可比**（前者 1536×864 虚拟化值，后者物理 3840×2160），会把在屏窗口误判 off-screen。
3. **设置窗是模态的**：打开期间 owner 悬浮球被 `EnableWindow(FALSE)`。AC-6/7/8 截图必须在**设置窗开着**时截（预览区才是判定对象），截完 `WM_CLOSE`。
4. **`TrackPopupMenu` 阻塞消息循环**：菜单弹出后程序在等你操作。→ 触发后**立刻** `CopyFromScreen`，再 `keybd_event(VK_ESCAPE)`；判「菜单是否真的弹出」看截图里有无非背景色弹层，别用「没截到就 FAIL」。
5. **取色取固定预览球心坐标**，不要取窗口平均色（平均色被深色面板稀释，透明度差被低估 30%+）。
6. **截图必须非纯色**：`CopyFromScreen` 截到整幅单色 = 窗口被遮挡/未渲染，`check_screenshot.ps1` 会直接 FAIL（防伪，别绕过）。
7. **单实例互斥是全局的**（`NetBallSingleInstance`）：任何时刻只能有一个实例，否则新进程 rc=0 秒退——这不是崩溃。测启动/交互前确认无人占用。
8. **`speed.cloudflare.com/__down?bytes=` 有上限**：`104857600` 只返回 1 字节（GROUND_TRUTH 变 0）。用 `bytes=20000000`，且 `size_download<1MB → BLOCKED`。

> 另外：**不要 `taskkill /f /im NetBall.exe`**，只按 PID 杀；跑自己的副本 `build\_tmp\vfy\NetBall_vfy.exe`（同 sha256，独立进程名，互不干扰）。

## 3. 如何确认被测 exe 版本（每次判定前必做）

```cmd
powershell -NoProfile -Command "Get-Item 'd:\agentproject\网速悬浮球\build\out\NetBall.exe' | Select-Object Length,LastWriteTime | Format-List"
powershell -NoProfile -Command "Get-Item 'd:\agentproject\网速悬浮球\src\*.cpp' | Sort-Object LastWriteTime -Descending | Select-Object -First 1 Name,LastWriteTime | Format-List"
```
- **exe 的 mtime 必须 ≥ 最新 `src\*.cpp` 的 mtime**，否则 `BLOCKED-被测二进制早于源码`。
- 现成实现：`verifier_preflight.ps1`（`Write-BuildInfo -Exe $p`）/ `nb_build_info.py`（`build_info()`）。

## 4. 当前真实现状（2026-11-02 18:3x）

| AC | 结果 | 证据 |
|---|---|---|
| AC-1 | **PASS** 1,053,184B；FileDescription 码点精确匹配（无乱码）、`1.0.0.0`/`NetBall`/`NetBall.exe`、图标 variance 2271、`asInvoker`×1 + `PerMonitorV2`×1 | `coldrun.txt` |
| AC-2 | **PASS** `exstyle=0x00080088`，无 APPWINDOW，rect 154×154 | `coldrun.txt` |
| AC-3 | 口径 PASS（42 行→7 行）；正式比对待机读 | `net.txt` |
| AC-5 | 稳态 **10.63~10.68MB**、GDI 18/18、USER 22、Handles 198、CPU 0.0244%、启动 51ms（另有 builder 报 3.08MB/73ms，需我复测） | `resources.txt` |
| AC-6 | 未做 | — |
| AC-7 | 工具自检 PASS；**必须用高对比球色（`#FFFFFF`）测 20%/100%** ——默认 `#181C24` 两档亮度差仅 3.05%，会误判 FAIL | `verifier_selftest.log` |
| AC-8 | 数学 72 组 PASS（最差 4.766）；11 张真实截图：9 PASS + **`round-60-mid` 3.42 / `white-20` 3.14 → Leader 口径为 KNOWN-LIMITATION（maximin 不可行区），不是 PASS** | 本文件 §5 |
| AC-9 | 12 键 PASS；破坏鲁棒性 PASS；autostart 改验注册表 | `verifier_ac9corrupt.log` |
| AC-10 | 未做 | — |
| AC-11 | PASS（二次启动秒退，PID 不变） | `coldrun.txt` |
| AC-12 | 未做 | — |
| AC-16 | 标记 PASS（20 文件 0 命中）；`build\out` 已被 core-1 清到只剩 `NetBall.exe` | `coldrun.txt` |
| AC-17 | PASS 175%（168dpi、PMv2、ink margin 16px）；48/160 两档待 `-SizeSweep` | `dpi-175.png` |

## 5. AC-8 两条硬约束（别改回去）

1. `check_ac8_contrast.py` 必须是「**填充色逐行跨度几何掩膜 + 众数取色**」。早期整幅 2-means 会把球外桌面（占 23% 像素）并进文字簇，把 `#000000` 稀释成 `#0E1718`，对比度低估 30~55%，把 PASS 判成 FAIL。已与 builder 独立实现交叉验证，11 张截图差 ≤0.06。
2. Leader 裁决的是 **maximin**（对纯白/纯黑桌面取最坏情况，选最坏值更高者），不是「按实际桌面择优」。所以 `round-60-mid` 3.42 与 `white-20` 3.14 属**不可行区 → `KNOWN-LIMITATION`**：单列、写明组合与最坏值，**不得写成 PASS，也不得洗成「按实际桌面 6.30」**。
