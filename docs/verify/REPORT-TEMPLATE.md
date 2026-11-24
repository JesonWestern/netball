# NetBall 验收报告（verifier-1 / T7）

- 项目：`d:/agentproject/网速悬浮球` ；产物：`build/out/NetBall.exe`
- 本表覆盖 AC-1~AC-12、AC-16、AC-17；AC-13/14/15/18（安装包相关）不在 T7 范围
- 验证环境：Windows / 逻辑核数见 `resources.txt` / **DPI 175%**（禁止改系统缩放去凑数）
- 脚本目录：`docs/verify/`；冷跑记录：`coldrun.txt`
- 报告时间：`YYYY-MM-DD HH:MM:SS` ；结论：`PASS x / FAIL y / SKIP z / BLOCKED n`

## 判定原则（不可绕过）

1. 每条 AC 必须有**真实命令输出 + 证据文件**，不抽样。
2. 脚本只打印事实（字节数/毫秒/百分比/对比度/像素），PASS 由断言阈值判定，**禁止硬编码 PASS**。
3. 任一 FAIL 必须写：现象 / 复现步骤 / 期望值 / 实测值 / 定位建议。
4. `SKIP` 不等于 PASS；`BLOCKED` 表示环境无法取证（如 curl 断网），两者都要写明缺什么。
5. **P1 缺陷不得降级为「已知限制」**；P1 缺陷一律判 FAIL。
6. 截图必须是 `CopyFromScreen` 真实截图；**禁止占位图/生成图**；截图纯色（unique colours ≤1）视为无证据，脚本直接 FAIL。
7. `.ps1` 一律 ASCII-only 或 UTF-8 with BOM（本目录脚本均为 ASCII-only，中文用码点构造），否则 GBK 解析会语法错。
8. 所有命令输出重定向到 `docs/verify/verifier_*.log` 后再读，**不信任终端直出**（共享控制台会串台）。
9. 判 PASS 前必须复跑一次确认可重复（数值类允许区间波动，但结论不能翻转）。

## 逐条结论

| AC | 验收项 | 阈值 / 判据 | 脚本 | 实测值 | 证据文件 | 结论 |
|---|---|---|---|---|---|---|
| AC-1 | exe 体积 | ≤ 2097152 B | `check_ac1_exe.ps1` | | `coldrun.txt` AC-1 段 | |
| AC-1b | 版本信息/图标/manifest | 四项精确断言（见下） | `check_ac1_exe.ps1` | | 同上 | |
| AC-2 | 置顶+分层+无任务栏按钮 | `0x80000\|0x8\|0x80`，`APPWINDOW==0` | `check_ac2_window.ps1` | | `coldrun.txt` AC-2 段 | |
| AC-3 | 网速读数正确 | 基准 vs 悬浮球读数 误差 ≤ 20% | `check_ac3_net.py --compare --measured N` | | `net.txt` | |
| AC-4 | 内存读数正确 | 与独立口径 **±2pp**（不是 ±2% 相对） | 截图读数 vs `Get-Process`/WMI | | `mem-shot-*.png` | |
| AC-5a | 常驻内存 | **稳态**（启动后 60s）WS max ≤ 15 MB | `check_ac5_resources.ps1` | | `resources.txt` | |
| AC-5b | CPU | 平均 ≤ 0.5 % | 同上 | | `resources.txt` | |
| AC-5c | 冷启动 | ≤ 500 ms | `measure_startup.ps1` | | `resources.txt` | |
| AC-6 | 形状/尺寸切换 | 各形状截图轮廓/尺寸变化 | `check_screenshot.ps1` + 人工 | | `shape-*.png` | |
| AC-7 | 透明度可调 | 20% vs 100% 亮度差 ≥ 20%（取**预览球心**像素） | `check_ac7_alpha.py` | | `alpha-20.png`/`alpha-100.png` | |
| AC-8 | 文字对比度 | ≥ 4.5:1（截图实测 + 72 组纯数学） | `check_ac8_contrast.py` | | `shape-*.png` | |
| AC-9 | 配置持久化 | 12 键齐全 + 改设置重启保留 + **恶意破坏回落** | `check_ac9_config.py` | | `config-snapshot*.json` | |
| AC-10 | 拖动+位置记忆 | 重启后回到原位置 | 人工 + `check_ac9_config.py` | | `pos-*.png` | |
| AC-11 | 单实例 | 进程数 == 1 且 PID 不变 | `check_ac11_single.py` | | `coldrun.txt` AC-11 段 | |
| AC-12 | 托盘/设置窗 | 菜单三项齐全、设置保存生效、**显示/隐藏切换** | 截图 + 人工 | | `settings-*.png` | |
| AC-16a | 源码无残留标记 | 0 命中 | `check_ac16_clean.ps1` | | `coldrun.txt` AC-16 段 | |
| AC-16b | `build/out` 洁净 | 仅 `NetBall.exe` | `check_ac16_residue.ps1` | | `coldrun.txt` AC-16b 段 | |
| AC-17 | 175% 不裁切/不糊 | scale==175，size 48 与 160 均 margin ≥ 1px | `check_ac17_dpi.ps1 -SizeSweep` | | `dpi-175*.png` | |

### AC-1 细化断言（含 T5 资源互检）

| 断言 | 期望 | 说明 |
|---|---|---|
| 文件存在 | 是 | |
| 字节数 | ≤ 2097152 | |
| PE `.rsrc` | 存在且 raw ≥ 512 B | 资源真的链进去了 |
| `FileVersion` | `-eq "1.0.0.0"` | 精确 |
| `ProductName` | `-eq "NetBall"` | 精确 |
| `OriginalFilename` | `-eq "NetBall.exe"` | 精确 |
| `FileDescription` | `-eq "NetBall 网速悬浮球"` | 逐字比对（曾出现 windres CP936 乱码 `缃戦€熸偡娴佺悆`）；脚本用码点构造期望值 |
| 图标 | 非空、`Width ≥ 16`、像素方差 > 0.5 | 排除纯色占位图标 |
| `assets/app.ico` | 可加载 32×32 帧（只读） | T5 互检 |
| RT_MANIFEST | type 24 / id 1 存在 | |
| manifest 文本 | `asInvoker` 命中 1 次、`PerMonitorV2` 命中 1 次 | 清单真生效 |

> AC-1 行同时承担 **T5 资源互检职责**：图标编入 + 版本信息中文正确。

### AC-5 记录口径

| 指标 | 来源 | 备注 |
|---|---|---|
| `WorkingSet64` | `Get-Process` | 早值(t≈0) 与**稳态值**(60s 后) 都要记，**以稳态判 PASS** |
| `PrivateMemorySize64` | `Get-Process` | |
| `PeakWorkingSet64` | `Get-Process` | |
| `HandleCount` | `Get-Process` | |
| GDI 对象数 | `GetGuiResources(GR_GDIOBJECTS=0)` | P/Invoke；GDI 对象持续增长 = 泄漏信号 |
| USER 对象数 | `GetGuiResources(GR_USEROBJECTS=1)` | 同上 |
| CPU% | `ΔTotalProcessorTime / Δwall / 逻辑核数` | 60 点 × 1 Hz 取平均 |
| 冷启动 | `measure_startup.ps1`（进程出现 + 窗口出现，取 median） | ≤ 500 ms |

### AC-3 记录口径

`docs/verify/net.txt` 必须同时出现：
```
GROUND_TRUTH_DOWN_BPS=<curl 报文字节 ÷ curl 墙钟>
NIC_AGG_DOWN_BPS=<GetIfTable2 聚合，FilterInterface=0>
measured_bps=<从悬浮球截图读到的数值，人读>
error_pct=<|measured-truth|/truth*100>
```
- 判据：`error_pct ≤ 20`。TCP/IP + 以太网帧开销会让 NIC 计数比应用层高 2~8%，属正常。
- `curl` 失败（断网/超时/rc≠0）→ 判 `BLOCKED`，**不得**改用「与另一份自研计数器一致」判 PASS。
- 截图字号太小读不清 → 判 **FAIL**（读不清本身是可读性缺陷），**禁止估算数值充数**。

### AC-9 三段验证

1. 键齐全：`shape size opacity bgcolor textauto textcolor shownet showmem refreshms alwaysontop posx posy`
2. 持久化：`--snapshot` → 改设置 → 重启 → `--diff`（必须报告 changed_keys 非空且无 lost keys）
3. 破坏鲁棒性：`--corrupt`（`opacity=999`、`refreshms=abc`、`posx/posy=999999`、删 `textauto`）→ 启动后必须
   不崩溃、不弹错误框（无 `#32770`）、窗口仍在虚拟屏幕内（位置回落到默认）。

### AC-12 交互细则（易漏）

| 步骤 | 期望 | 取证方式 |
|---|---|---|
| 右键悬浮球 → 菜单 | 含「设置 / 显示或隐藏 / 退出」三项 | 先 `PostMessage` 触发，**立刻** `CopyFromScreen`，再 `keybd_event VK_ESCAPE` 关菜单 |
| 打开设置窗 | 模态，悬浮球被 `EnableWindow(FALSE)` | 截图（设置窗 + 预览区） |
| 点「显示或隐藏」 | 悬浮球不可见（`IsWindowVisible=false` 或 rect 为 0） | 前后各截一张 |
| 再点一次 | 恢复可见 | 同上 |
| 托盘 tooltip | 悬停 3s 截图 | 截不到 → 判 `SKIP-无法取证`，**不得**按「应该正常」判 PASS |

## FAIL / BLOCKED 明细模板（每份一条）

```
### [AC-x] <一句话现象>
- 严重度：P1 / P2
- 现象：
- 复现步骤：
  1.
  2.
- 期望值：
- 实测值：
- 证据：docs/verify/<file>:<行号或数值>
- 定位建议：<文件/函数/可能原因>
```

## 附件清单

| 文件 | 内容 | 生成方式 |
|---|---|---|
| `coldrun.txt` | 全部脚本一次真实运行（逐段 exit code + RESULT） | `run_all.ps1` |
| `resources.txt` | 稳态 WS/CPU/GDI/Handle + 冷启动 | `check_ac5_resources.ps1` |
| `net.txt` | curl 基准 + NIC 聚合 + 悬浮球读数 | `check_ac3_net.py --compare` |
| `shape-*.png` / `alpha-*.png` / `dpi-175*.png` | 真实 CopyFromScreen 截图 | `check_screenshot.ps1` |
| `config-snapshot*.json` / `config-ac17-backup.ini` | 配置快照与还原点 | `check_ac9_config.py` / `check_ac17_dpi.ps1` |
| `verifier_*.log` | 每个命令的重定向输出（防串台） | 各次实测 |
