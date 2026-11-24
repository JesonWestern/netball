# NetBall 网速悬浮球 — 工作流文件（持久化状态）

VERSION: plan v1.1 / workflow v1
ROOT: d:/agentproject/网速悬浮球

## GOAL
在 Windows 10/11 x64 交付可安装的「网速悬浮球」：置顶半透明浮窗实时显示上/下行网速与内存占用；形状/尺寸/透明度/颜色可调，文字按背景自动选黑白保证对比度 ≥4.5:1；常驻内存 ≤15MB、CPU ≤0.5%、exe ≤2MB；Inno Setup 安装包含桌面/开始菜单图标（安装时可勾选）、安装目录内卸载文件、一键卸载。

## 技术栈（冻结）
C++17 + Win32 API + GDI+（software rendering）+ `UpdateLayeredWindow` 32bpp DIB per-pixel alpha
编译器：MinGW-w64 g++ 13.1.0（`C:\application\qt\Tools\mingw1310_64\bin`），资源编译 windres
安装包：Inno Setup 6.7.3（`%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe`）
图标生成：Python 3.12 + Pillow 12.2
降级链：GDI+ 失败 → 纯 GDI 自绘；再失败 → .NET 自包含；Inno 失败 → NSIS

## 目录约定
```
src/            main.cpp ball_window.cpp/.h render.cpp/.h metrics.cpp/.h config.cpp/.h settings_dialog.cpp/.h tray.cpp/.h
src/            app_common.h（契约，冻结）app.rc app.manifest resource.h
src/spike/      技术验证临时目录（T1 用，M6 前保留）
assets/         app.ico + 生成脚本
tools/          make_icon.py
build/          build.bat package.bat out/（产物）
installer/      NetBall.iss
dist/           NetBallSetup-1.0.0.exe（最终交付）
docs/verify/    verifier 的验证证据
.agent/         workflow.md board.md
```

## BOARD
见 `.agent/board.md`（实时）。任务 T1–T10。

## TEAM-LIVE
| 成员 | 角色 | 任务 | 可写范围 | 健康态 | 上下文 |
|---|---|---|---|---|---|
| builder-core-1 | Builder | T1/T2/T6（done） | ~~src/main.cpp, src/ball_window.*, src/render.*, src/app_common.h, src/spike/**, build/build.bat~~ 已收回 | **RETIRED**（93%，握手 approve；交接单 H-03） | 93% |
| builder-data-1 | Builder | T3 + `build/build.bat` + `docs/交付说明.md`（全部 done） | ~~src/metrics.*, src/config.*, build/build.bat~~ 已收回 | **RETIRED**（91%，握手 approve） | 91% |
| builder-ui-1 | Builder | T4（done，含 AC-8 三色整改 `#144BC8/#6E32BE/#B41923` + 设置窗取证） | ~~src/settings_dialog.*, src/tray.*~~ 已收回 | **RETIRED**（91%，握手 approve） | 91% |
| builder-assets-1 | Builder | T5（done） | ~~assets/**, tools/**, src/app.rc, src/app.manifest, src/resource.h~~ 已收回 | **RETIRED**（停机请求超时被强制终止；T5 已交付且复核通过） | 100% 自报（后被自证为失真） |
| builder-assets-2 | Builder | T11（done） | ~~assets/installer/**, tools/make_wizard.py, build/_tmp/**~~ 已收回 | **RETIRED**（握手 approve） | unknown（未自测，未编造） |
| verifier-1 | Verifier | T7（AC-1/2/5/9/11/16a 结论已出） | ~~只读 + docs/verify/**~~ 已收回 | **RETIRED**（96%，交付 HANDOVER-V2 + OCR 机读工具链） | 96% |
| verifier-4 | Verifier | T7 剩余项（AC-16b PASS / AC-4 PASS / AC-3 PASS 按新口径） | 只读 + docs/verify/v4/** + media-v4/** + build/_tmp/v4/** | GREEN | unknown |
| builder-installer-1 | Builder | T8 | installer/**, build/package.bat, dist/**, build/_tmp/ins1/** | GREEN（M5 拉起） | - |
| verifier-2 | Verifier | T9（待创建） | 只读 + build/_tmp/ins2/** | 未创建（M5 后半） | - |
| auditor | Auditor | T10（待创建） | 只读 | 未创建（M6） | - |
| verifier-3 | Verifier | T7b（AC-6/7/8/10/12/17） | 只读 + docs/verify/t7b/** + docs/verify/media-t7b/** + build/_tmp/t7b/** | GREEN（M3 后已拉起） | - |
| builder-installer-1 | Builder | T8/T9 | installer/**, dist/**, build/package.bat | 未创建（M4 扩编） | - |
| verifier-2 | Verifier | T9 复核 | 只读 + docs/verify/install.txt | 未创建（M4 扩编） | - |
| auditor | Auditor | T10 | 只读 | 未创建（M5 加入） | - |

## OWNERSHIP（一文件一写者）
- `src/app_common.h`：builder-core-1 独占落笔；内容由 Leader 冻结，改动须 Leader 批准
- `build/build.bat`：**builder-data-1 独占**（core-1 退役后转移，含 `--no-insert-timestamp`/纯 ASCII/release 跳过 spike/占用只警告不杀 四条约定）；他人只运行不修改。改后必须跑 `python -c "print(open(r'build\build.bat','rb').read().isascii())"` 断言 True
- `build/package.bat`、`installer/**`、`dist/**`：builder-installer-1 独占
- `docs/verify/**`：verifier-1 / verifier-2 独占（各自不同文件）
- 任何跨属主修改须先经 Leader 批准并在本文件登记

## DECISIONS
1. 选 C++/MinGW 而非 .NET/Electron：体积与内存是硬指标（exe ≤2MB、WS ≤15MB）
2. 安装目录内卸载入口用 `unins000.exe`（Inno 原生）+ `卸载 NetBall.lnk`（[Icons] 指向 {uninstallexe}）
3. 不做 CPU 显示（v1.0），配置位掩码预留扩展
4. 网络统计排除 loopback 与 tunnel 类型网卡，避免 VPN 重复计数
5. 无代码签名，SmartScreen 提示在文档中说明，不伪造签名
6. T5 完成即退役 builder-assets-1（CONTEXT 100% EXHAUSTED，无未完项）；其产物转由 Leader 直管至 M6，期间只读
7. `app.rc` 必须保留 `#pragma code_page(65001)` 且保持 UTF-8 无 BOM —— windres 默认按 CP936 解析，曾把 FileDescription 编成乱码「缃戦€熸偓娴悆」
8. windres 的 rc 内相对路径基准是 **rc 文件所在目录（src\）**，故图标写 `..\assets\app.ico`；build.bat 不加 `--include-dir src`（两种写法实测均 exit 0）
9. AC-5 判定以「启动后 60 秒稳态 WS」为准；允许启动后单次 `SetProcessWorkingSetSize` 并在文档明示，禁止定时 `EmptyWorkingSet` 刷指标
10. 本机 DPI = 175%，以此实测覆盖 AC-17（比 150% 更严格）；禁止修改系统缩放设置
11. 全员命令一律重定向落盘再读（共享控制台 stdout 串台）；`.ps1` 必须 ASCII-only 或 UTF-8 with BOM
12. **真实性事故记录（R7）**：builder-assets-1 相邻两次回报 CONTEXT 由「100%」变为「约 35%」，自相矛盾，导致基于虚假健康态发出停机请求，该成员超时被强制终止。**教训：CONTEXT 必须真实估算，估不准写 unknown；Leader 不得仅凭单一自报值决策，须交叉核对进度证据（产物/日志/任务板）后再处置。** 此后对任何成员的 EXHAUSTED 判定，至少要有两项独立信号（上下文自报 + 产物停滞/回报退化）。
13. 扩编 builder-assets-2 承接 T11 的理由：T8 打包需要向导素材与安装内 README；四名在役成员均无空闲（core-1/data-1/ui-1/verifier-1 全部在跑），属 8.2「可并行任务且无人空闲」情形
14. 托盘消息归属：`tray::add` 内部用 `SetWindowSubclass` 自接管 `WM_APP+100` 与 `TaskbarCreated`，**main.cpp 不得重复转发**（否则右键弹两次菜单）
15. `IDM_EXIT(1006)` 与 `IDM_TRAY_EXIT(1011)` 必须走同一条退出路径（save→tray::remove→KillTimer→metrics::shutdown→DestroyWindow→PostQuitMessage）
16. 链接行固定集合：`-lgdiplus -lgdi32 -luser32 -liphlpapi -lshell32 -lshlwapi -lcomctl32 -luxtheme -lole32 -loleaut32 -lcomdlg32 -ladvapi32`；`config.cpp` 必进链接集合（`cfg::resetToDefault` 被设置窗引用）
17. 设置窗 onApply 必须做四件事：`g_cfg=*c` → `cfg::save` → `ballwin::applyConfig` → `refreshMs` 变化时 `KillTimer`+`SetTimer`（漏则「改刷新频率不生效」）
18. 已知限制（写入交付文档）：设置窗客户区在 175% DPI 下 844 物理像素高，1080p@175% 可完整显示；更低分辨率/更高缩放时靠工作区钳制保证标题栏与按钮可见，内容不滚动
19. **T8 硬要求（源自 T11 README 对用户的公开承诺，必须逐条兑现）**：① 开始菜单程序组 `NetBall`，内含启动项与「卸载 NetBall」；② 安装目录内 `unins000.exe` + `卸载 NetBall.lnk`（`[Icons] Name: "{app}\卸载 NetBall"; Filename: "{uninstallexe}"`）；③ 卸载用 `[Code]` 弹窗询问是否保留 `%APPDATA%\NetBall`，选不保留则递归删除；④ `UninstallDisplayName=NetBall 网速悬浮球`、`UninstallDisplayIcon={app}\NetBall.exe`、`CloseApplications=yes`；⑤ `WizardImageFile`/`WizardSmallImageFile` 引用 `assets\installer\wizard-large.bmp`(164x314) 与 `wizard-small.bmp`(55x58)，**不设 `WizardImageStretch`**；⑥ `InfoBeforeFile` 引用 `assets\installer\README.txt` 并随包安装到 `{app}`；⑦ 桌面图标为可选任务（默认勾选、可取消）以覆盖 AC-15
20. 对外系统要求统一表述：**Windows 10（1809 及以上）/ Windows 11，仅 64 位（x64）**；不声明 Win7/Win8（未测试，不得编造）
21. 设置窗按钮文案固定为「恢复默认 / 取消 / 保存」，任何文档、截图说明不得写成「确定」
22. **网速统计口径（已拍板，不得回退）**：`OperStatus==Up` ∧ `IfType ∉ {24,131}` ∧ `InterfaceAndOperStatusFlags.FilterInterface==0`。依据 `build/_ifdump.log`：同一物理网卡在 GetIfTable2 中以 4~5 行镜像出现（WFP Native MAC LightWeight Filter / QoS Packet Scheduler / WFP 802.3 Filter），增量完全相同（136455/192302），不去重会放大约 4 倍（all-up 546,264 vs hardware-only 136,529）
23. **AC-3 判定 ground truth** = `curl` 实际下载字节数 ÷ 墙钟耗时，误差 ≤20%；**不得**用「另一份自研计数器与自己一致」判 PASS（自证不算独立验证）。AC-4 用 ±2pp 绝对差；AC-9 追加「配置文件被人为破坏（opacity=999 / refreshms=abc / 删键）后回落默认且不崩溃」
24. **DPI 测量坑（环境级，全项目适用）**：DPI-unaware 宿主进程里 `GetDpiForSystem` 返回 96（被系统虚拟化），会把 175% 误判成 100%。必须 `SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2=-4)` 后再测，并用 `GetDpiForMonitor(MDT_EFFECTIVE_DPI)` 交叉验证（本机实测 168 = 175%）
25. **P1-1 结论（单色截图）已关闭**：判定为「T2 未完成时的中间构建」，非渲染缺陷。证据：18:10 版 exe（1,053,184 B）在 175% 下正常渲染 `↓81.5 KB/s / ↑5.11 KB/s / 内存 75%`，字形清晰居中无裁切（`docs/verify/dpi-175.png`）
26. **测量必须机读，禁止人工抄数**：AC-3 的 `measured_bps` 只能来自程序输出通道或屏幕 OCR；OCR 不可用判 `SKIP-无法机读`。若需程序侧支持，由 Leader 批准让 builder 增加调试输出（`--dump-metrics` 或写 `%TEMP%\NetBall_metrics.txt`），不得退化为「人读截图手抄」
27. **AC-16 遗留物判定责任**：验证者自己的取证文件（截图/日志/探针脚本）本就应落 `docs/verify/**`，迁移前不得把「自己的文件」计入遗留物缺陷；`build\out\` 终态只允许 `NetBall.exe`
28. **归属判定规则（Leader 自我纠正）**：曾凭文件名把 `build\out` 的 85 个文件错判给 verifier-1，实为 builder-core-1（其 T6 回报自述已承认，且 `preset_probe.ps1:120-122` 的 `$outDir` 指向 build\out、内容为 WM_COMMAND 1001/1002/1010/1011 的 GUI 探针）。**此后归属必须以文件内容/当事人自述/自述矛盾为据，不得凭文件名推断。**
29. **AC-8 最终口径（拍板）**：`autoTextColor` 必须为 **maximin**——对「纯白桌面 / 纯黑桌面」两个极端分别算对比度，取最坏值更高者。**否决**「按实际合成桌面择优」（程序无法预知桌面，逐帧重算会导致文字颜色闪烁、结果不可复现）。验收分两层：① 必过层：8 预设色 × 默认 opacity 85% 全部 ≥4.5；② 物理不可行层：maximin 最坏值仍 <4.5 的组合（已知 `round-60-mid` 黑 3.42、`white-20` 黑 3.14）判 `KNOWN-LIMITATION`，须列明组合+最坏值+用户建议（提高不透明度 ≥85%），不计缺陷；`KNOWN-LIMITATION` 单列，不等于 PASS，T10 须核其未被用于掩盖真缺陷
80. **AC-5 披露值最终裁决（废止 #30 的 10.68MB 主值）**：对外数字必须绑定**最终交付的二进制**（`7269AA…`），故主表取该构建实测 `稳态 8.29 / 采样最大 8.30 / Peak 16.23（启动瞬间，不计红线） / Private 3.18 / CPU 0.0097% / 冷启动 50ms`。`resources.txt` 的 `3.82~3.84MB` 作为**独立复核**附注（另一副本、另一轮采样，结论一致）。`10.68MB` 系 **18:29 旧构建**在繁忙负载下的读数，该构建已被锚点替换、**作废**，仅作历史记录保留。**原则：保守不等于准确——用旧构建的数会让读者误以为最终版更吃内存。**（本条与 #30 冲突，#30 的主值部分已被本条废止）
81. **AC-5 双数据源（已被 #80 部分废止，保留作历史记录）**：builder 90s 采样 WS 3.05/3.08/3.16MB（含启动后单次 Trim）；verifier 在 18:29:44 构建实测 WS 稳态 10.63~10.68MB、GDI 18/18 无增长、USER 22、Handles 198、CPU 0.0244%、冷启动 51ms。差异源于测量窗口与是否含 GDI+ 冷启动，两者均 <15MB → AC-5 通过，取**较保守值 10.68MB** 作为对外披露值
32. **构建冻结窗口（M4 期间生效）**：自 2026-11-02 18:36 起，除缺陷单驱动外禁止重建 `NetBall.exe`。权威构建 = 18:36 版（1,053,184 B，7 源文件 -Wall -Wextra 零告警，含 `ball_window.cpp` `-Wdangling-pointer` 修复）。理由：构建/`taskkill` 会打断并行验证（已发生 3 次：core-1 撞 Permission denied、verifier-1 被打断 2 次）。需重建须先报 Leader 协调窗口
33. **Peak WS 口径裁决**：AC-5 红线 = 稳态采样最大值 ≤15MB（实测 8.30MB / 60s 稳态 8.29MB）。`PeakWorkingSet64=16.23MB` 为生存期峰值（启动瞬间加载 GDI+/字体/COM 页，一次性），**不计红线**，但必须在交付文档如实披露「稳态 8.3MB / 峰值 16.2MB」。禁止为此推迟 GDI+ 预热
34. **config.ini 还原时点**：M4 验证期间不还原（会打断 AC-6/AC-7 用例）；**M6 打包前**由 builder-core-1 统一还原默认（shape=0 size=88 opacity=85 bg=#181C24 posx/posy=-1）并留 `--diff` 证据
35. `preset-*.png`（11 张）归属 builder-core-1（其 `preset_probe.ps1` 的 `$outDir=build\out` 生成），迁 `build\_tmp\core1\preset-evidence\` 保留，是 AC-8 `KNOWN-LIMITATION`（round-60-mid / white-20）的关键证据
37. **AC-8 口径定稿（第二次、最终）**：一律按**严格 maximin**（球体分别叠纯黑/纯白桌面 → 黑白两色各取两桌面中更差者 → 取更差者更优的颜色）。**禁止**用「本机真实桌面 #313244 合成」判 PASS（那是用产品恰好跑在某台机器壁纸上来背书，8/8 全过、最差 5.20 仅可作「典型观感参考」）。否决方案 C（文字区局部抬 alpha 到 100%，虽可数学上全程达标，但球心会出现不透明文字底板、明显改变视觉且动渲染主链路）→ **C 记为 v1.1 确定性修复方案，写入后续建议**
38. **AC-8 三个预设色 FAIL 的处置 = 换配色，不放宽标准**：蓝 #1E5ADC 4.41 / 紫 #8246D2 4.18 / 红 #D23237 3.99（85%@纯白桌面）。根因是配色过亮，非渲染缺陷。由 builder-core-1 用 `core1_ac8_table.py` 搜索同色系更深、maximin@85% ≥5.0 的替换值（产出 `build\_tmp\core1\core1_palette_fix.txt`），交 builder-ui-1 落地到 `settings_dialog.cpp`（唯一写权）。预设色是我们替用户选的，属设计缺陷；用户自定义色落入不可行区由文档说明
39. **启发式退化保留**：`maximin ≥4.5` 走 maximin，`<4.5` 退化为「典型中灰桌面启发式」（优先保真实观感，纯 maximin 会在本机真实桌面上选到更差者）。builder 用 1020 组对拍证明「可达区 1005/1005 与纯 maximin 同色」，故必过层不受影响。要求把该退化行为写进 `render.cpp` 头注释与交付文档
40. **第二次重建窗口（待开）**：配色改完需重建一次，当前仍未开窗口。顺序：core-1 出 palette_fix 表 → ui-1 改 `settings_dialog.cpp` → 两位验证者跑完 18:36 版当前轮 → Leader 开窗口 → core-1 重建 → 复测 AC-1/6/7/8/17（其余项在最终版留一次证据）
41. verifier-1 上下文 96% 时交完整结论表并退役（未因上下文压力压缩断言）；T7 剩余三项由 verifier-4 依 HANDOVER-V2 接续，脚本资产复用不重写
42. **AC-3 判据调整（降流量门槛、不降精度）**：本机出口实测约 9.7 KB/s，20s 仅 194KB，原「≥1MB」门槛在环境上不可达。改判据为：总下载 **≥300KB 且窗口 ≥20s**，同一窗口内**连续 5 次采样**每次偏差 ≤20%（比单次采样更严）；OCR 读不出判 `BLOCKED-无法机读`，禁止估算/手抄。理由：判据本质是「程序读数 vs 外部真值的比值」，与绝对流量无关
43. **P1 调查（未定性）**：verifier-1 于 18:46 抓到 size=160 + 浅底球体为 172×172 纯色无文字（`docs/verify/net-shot-1.png`），而 size=88 深底正常（ink=2091px）。三假设：①选色导致视觉不可见（白球选白字，AC-8 数学达标但视觉消失）；②尺寸相关渲染缺陷；③抓图时序竞争。**派 builder-core-1 定性 + verifier-3 做 48/88/160 × 深/白/中灰 3×3 矩阵各抓 3 次，某格 ≥2 次无字才判 P1**；若属①则需调整选色规则（白底强制黑字）而非改渲染
45. **R3 升级为确证缺陷（P1）**：verifier-1 两轮独立复现（不同配置/不同进程）均得 `ink=0px`、抓图 172×172 全图单色（`RGB(236,236,244)` / `RGB(4,4,4)`），且 **`GetWindowRect` 两次都是 160×160 物理像素**；对照 `size=88` 为 154×154（=88×1.75，DPI 正确）。→ **`size=160` 时窗口尺寸未按 175% 缩放，且球体纯色无文字**。排查方向：尺寸计算分支/clamp、`CreateDIBSection` 失败未检查、GDI+ `GetLastStatus` 未检查、48..160 钳制与 DPI 相乘后超内部缓冲。修复需开构建窗口，与配色改动 + `render.cpp` 注释**合并为一次构建**
46. **AC-3/AC-4 双重阻塞**：①带宽 9.7→31.5 KB/s（已改判据 ≥300KB/≥20s/5 次采样）；②无文字导致 OCR 无输入。修 R3 同时解开两者，故 R3 优先级高于配色
47. **配色定稿（core-1 建议，ui-1 独立复算后落盘）**：红 `#D23237`→`#B21217`(5.56) / 蓝 `#1E5ADC`→`#1245B2`(5.82) / 紫 `#8246D2`→`#5A12B8`(6.90)；色相 ΔH=0°、仅降明度、S=0.90。绿 `#22A05A`(4.68) 保持不动。整改后 8 预设 × 85% 全过、最差绿 4.68
48. **记录更正（ui-1 指出）**：`customs[16]` 只有 8 个初始化器，**其余 8 槽为零初始化黑色**（不是「Windows 默认补充色」，也不是 12 槽）；保持现状不动。Leader 先前表述有误，已更正
49. `build\out` 已于 19:0x 实测只剩 `NetBall.exe`
50. **事实更正（Leader 撤回错误结论）**：曾表述「红 `#D23237` 即使 100% 也 4.94，调高不透明度救不了红」——**后半句错误**。准确：100% 时 4.94 **≥4.5 达标但无余量**；85% 时 3.99 不达标。换配色仍成立（默认 85% 即不达标、4.94 无余量），但不得用「100% 也不达标」作论证前提
51. **最终配色（已落盘 `settings_dialog.cpp`，待构建生效）**：蓝 `#144BC8`(5.28) / 紫 `#6E32BE`(5.26) / 红 `#B41923`(5.32)，ΔE76 6.3/7.4/9.0；绿/橙/深灰/纯黑/白不变。**否决 core-1 的更暗方案**（`#1245B2`/`#5A12B8`/`#B21217`，5.82/6.90/5.56，但 ΔE 11.5~18.6、紫 L\* 29.6 发闷）——**属观感取舍非合规取舍**：判据 4.5，ui-1 方案相对 4.5 余量 ≥0.78（远大于两实现 ±0.05 噪声），故取 ΔE 更小者。core-1 数值经 ui-1 独立复算差 0.00，计算正确性无疑义
53. **R3 结案（验证工具缺陷，非产品缺陷）**：`docs/verify/check_screenshot.ps1` 自身 DPI 不感知——`GetWindowRect` 返回被 1.75 虚拟化的**逻辑坐标**，却拿去 `CopyFromScreen` 取**物理像素**，截到球外桌面。决定性对照：**用完全正常的 `size=88` 跑同一脚本同样 `ink=0`**（脚本读数 88×88 @ (2093,1085)，真实为 154×154 @ (3662,1898)，比值恰 1/1.75）；DPI 感知抓取时 `size=160` 为 **280×280 且三行文字清晰**（`diag-B1.png`，unique=328、center 白字芯），白底组自动选黑字且文字在。→ **产品无缺陷、不改码、不因此重建**
54. **连带处置**：凡使用该 unaware 路径取得的截图类结论（AC-2/6/7/8/17）**一律作废重跑**——包括那些"看起来 PASS"的，不能只撤 FAIL 不撤 PASS。修正方法：脚本顶部、任何 `GetWindowRect` 之前 `SetThreadDpiAwarenessContext(-4)`，并加尺寸自检门禁（size=88→154×154、size=160→280×280、胶囊160→420×280），不符判 `BLOCKED-DPI 上下文错误`
55. **构建窗口（已开，唯一一次）**：内容 = 三色落盘（ui-1 已改 `settings_dialog.cpp`）+ `render.cpp` 启发式退化注释；**否决** core-1 提的两项防御性改动（`UpdateLayeredWindow` 失败重试、`NETBALL_DEBUG_SIZE` 尺寸日志）→ 记为 v1.1 候选，v1.0 保持最小 diff。窗口期只有 core-1 可构建，其余成员暂停测量
57. **R3 争议未决（第三次"独立复现"存疑）**：verifier-4 报 `size=160` 时 rect 160×160、`margin=-6px`、OCR 读不出，判为真实缺陷。但 core-1 的四尺寸矩阵（感知路径 48/88/120/160→84/154/210/280，比值恒 1.75；不感知路径恒等于 DIP 值）表明这极可能是**同一工具缺陷的第三次表现**——verifier-4 与 verifier-1 共用 `check_screenshot.ps1`。→ **裁决**：verifier-4 必须先做 DPI 链路自检（`SetThreadDpiAwarenessContext(-4)` 后 size=88 应得 154×154），自检通过仍异常才算产品缺陷
58. **终版构建 + 独占窗口（S4R 重整）**：18:58:07 的 exe 比源码（18:58:17 配色回退）早 10 秒 → stale、不可测。改为一律「一次终版构建 → 源码冻结 → 单一 sha 锚定 → 串行独占窗口测量」。**否决** verifier-4 提出的「回退 build/out 到旧 sha」（越权写交付目录）
78. **AC-3 判据范畴错误（Leader 自我更正 6）**：原判据「与 curl 实际字节数比对 ≤20%」**不成立**——产品显示**整机网卡速率**，curl 仅一条连接；背景有流量时球必然高于 curl 数倍，属判据错误而非产品缺陷。**定稿口径**：ground truth = **独立实现的 NIC 聚合下行**（同口径 `Up ∧ type∉{24,131} ∧ FilterInterface=0`），误差 ≤20%；`curl` 仅作辅助且需「背景流量 < 总流量 5%」。据此 verifier-4 的 3.34% / 5.45%（vs 独立 NIC 聚合）+ 3.25%（vs curl 单点）成立 → **AC-3 PASS**。低速率（18–47 KB/s）下 +18.4% / +52.7% 属 1s 采样偏移噪声，判据在 <200KB/s 时不可采信，须如实写入报告
79. **越窗启动事件（builder-ui-1，19:15:37）**：为验证"锚点比对留痕"功能，用**正确锚点**跑了取证脚本，误以为只打印比对结果，实际门禁通过后继续执行到 `Start-Process`，产生 pid 29400、存活 40–60 秒。**影响评估**：`config.ini` 前后 sha `-ceq` 相等（未污染）；该进程能成功建窗说明互斥体当时空闲（与 verifier-4 进程未重叠）；唯一残留风险是这 40–60 秒内的抓屏可能截到多一颗球，已要求 verifier-4 就该时间窗自查。**根因**：门禁只拦「锚点/DPI/stale/抢占」，**拦不住「不在你的窗口内」**——这一层原先只存在于人的记忆里
76. **新规程：门禁通过 ≠ 允许启动**（本次事件核心产出）：凡需启动被测程序的脚本，必须同时满足**窗口许可**条件——`-WindowGrant <Leader 下发的许可值>` 必填，缺失即 `BLOCKED-NOWINDOW`。功能自测一律用 `-DryRun`（只跑到门禁与比对为止，永不启动），**禁止用"正确锚点"做功能自测**（正确锚点 = 真实许可）。违规处置：ui-1 本次保留任务但设红线，再犯即转交
77. **「一致」必须机械比对（全队规程）**：任何"一致/相同/相等/匹配"的结论，必须由 `findstr /c:` / `grep -F` / `-eq` / `-ceq` / 哈希全串比对产生，**禁止目视判断**，且日志须留下比对命令与其退出码。触发案例：builder-ui-1 曾目视扫过 sha 串就声称"逐字符一致"，实际把它收到的**错误广播串**抄进了执行清单——测量对、比对假。它与 verifier-3 的差别是「声称一致然后继续」vs「停手上报」，前者会把错误传向下游
74. **锚点串转写事故（Leader 自我更正 5）**：广播 sha 时人工转写丢一 `0`、挪一 `A`（`AFAAF0`→`AAFAAF`），若无人拦截将导致全员测量作废。**由 verifier-3 的 `BLOCKED-ANCHOR` 门禁在 `Start-Process` 之前中止并上报，未自行裁定"等价"，未被穿透**。规程：①sha 一律从工具输出**整串复制**，禁止人工分段/手打/重排版；②发现广播值≠自测值一律先停手报 Leader，不自行裁量；③锚点以「Leader 广播 + ≥2 名成员独立复算一致」为准
72. **禁用 `%ERRORLEVEL%` 判成功（全队规程）**：`cmd /c "... & echo rc=%errorlevel%"` 中 `%errorlevel%` 在整行解析期展开，永远滞后一拍 → 产生"假 rc=0"（今日多次误导）。正解：`cmd /v:on` + `!errorlevel!`，或 PowerShell `$LASTEXITCODE` / `Start-Process -PassThru | .ExitCode`。**更根本的原则：用产物判成功而非返回码**——构建看 `[4/4] artifacts` + exe 元数据，脚本看 `RESULT:` 行，截图看 `ink>0` 且非纯色
70. **最终锚点（可复现性已验证）**：`build\out\NetBall.exe` = 1,053,184 B / **sha256 `7269AFAAF069365DB42DFF2C5D8F8F79658248FBB6F466890AFF4C10B64A74F0`**（注：Leader 曾误广播为 `7269AAFAAF69365…`，丢一 `0` 挪一 `A`；由 verifier-3 的 `BLOCKED-ANCHOR` 门禁在启动程序之前拦下，磁盘/`certutil`/`Get-FileHash` 三方一致值为本行） / mtime 19:10:18.485。Leader 独立验证：19:10:07 与 19:10:18 两次构建 mtime 不同但 **sha 完全相同** → `-Wl,--no-insert-timestamp` 生效。作废历史锚点：`75d32fa2…`(19:02)、`0D970015…`(19:05) 仅作事故记录。**规则：此后"锚点不符"一律作废重跑，不再讨论"内容是否等价"**
68. **可复现构建（采纳 ui-1 方案 B）**：加 `-Wl,--no-insert-timestamp` 使 PE 头不写链接时间 → **同源码重建得相同 sha**，从机制上消灭"重建即变锚点"事故（今日已 3 次）。同时把 `build.bat` 改为**纯 ASCII**（原中文注释在 GBK 控制台下会吃掉行尾 CR、把注释尾巴当命令执行，日志出现 `'t'` / `'歴et' 不是内部或外部命令`）。验收：连续两次构建 sha 相同。执行者 builder-data-1（`build.bat` 原属已退役的 core-1，临时授权）
65. **验证门禁五道（全体验证者统一）**：`BLOCKED-STALE`（exe 早于源码）、`BLOCKED-DPI`（size=88 应 154×154）、`BLOCKED-BUSY`（已有实例）、`BLOCKED-ANCHOR`（sha ≠ Leader 广播值）、`BLOCKED-被替换`（测量前后 sha 不一致）。任一不满足即不得出结论
66. **锚点事故 #2（Leader 调度失误）**：19:03 锁定锚点 `19:02:56 / sha 75d32fa2…4633` 后，我又先发了 data-1 的构建授权、随后才撤销，撤销令到得晚 → 19:05:25 产生**第三次构建**（`sha 0D970015…92C7D9`），导致 verifier-4 正在进行的 AC-4/AC-3 整轮作废。**硬规则（即刻生效）**：①任何构建必须先广播"窗口开启"、由唯一人执行、Leader 复核 sha、再广播"窗口关闭"；②验证者每次测量必须记录「测量前 sha / 测量后 sha / 一致=YES|NO」，不一致直接判 `BLOCKED-产物被替换`；③任何人发现 sha 与锚点不符必须停手报 Leader，不得自行判断"内容等价可继续"。**教训：撤销授权必须在发出新授权之前完成，或改用「先锁后发」的单向流程。**
63. **互斥体与 config 是全局共享资源**：`NetBallSingleInstance` 与 exe 名无关，私有副本换名**不能**并行；窗口类名 `NetBallBallWnd` 亦共享（已发生他人按类名关窗致 rc=0）。→ 凡运行 NetBall 一律**串行**，收尾只按 PID
60. **AC-3 的 curl 判据修正**：`curl --max-time` 截断必然返回 rc=28，若把 `rc!=0` 判为失败则 20MB 永不下载完、用例永不 PASS。→ 接受 rc=0 与 rc=28，以 `size_download ≥300KB` 为有效采样判据（实测带宽已恢复 29.5 KB/s）
61. **测量方法学教训（Leader 自我更正 4）**：曾把两条不同测量链路的读数混在一张对比表里（`88→154` 取自 DPI 感知路径、`160→160` 取自不感知路径），据此得出「size=160 未按 DPI 缩放是缺陷」的错误定性。**同一把尺子必须量到底**：感知路径 48/88/120/160 → 84/154/210/280（比值恒 1.75）；不感知路径 → 48/88/120/160（恒等于 DIP 值）。教训：①比较两组数据前必须先确认「测量工具与上下文是否一致」；②「两名成员独立复现」不等于「两个独立工具」——verifier-1 与 verifier-3 的两次观测同错在一个工具上
58. **AC-8 方法学（供 T10 复核）**：两种反伽马阈值 0.04045（sRGB 标准）与 0.03928（WCAG 原文）**对 8bit 整数输入结果必然相同**——换点落在 `10/255=0.039216` 与 `11/255=0.043137` 之间，通道=10 走线性、通道=11 走幂次，两者一致。故 ui-1 与 core-1 两套独立实现逐位吻合，不属「口径不一致」。红的 5.0 门槛：S=0.9 时 V≤0.765（`#C31419`），更亮即不合格（verifier-1 18:41 的 AC-16b FAIL 系其读数早于 core-1 迁移所致，属时序非缺陷）；`docs\verify\__pycache__` 属遗留物，T7 收尾时清理：令其只做「跑一次 -Full + 出 AC 表 + 交 HANDOVER-V2」随即退役；若 T7 仍有零散项，新拉 `verifier-4` 依 HANDOVER-V2 接手，脚本资产不重写

## SNAPSHOT
- **S6 重规划第 1 轮后的新锚点（当前有效）**：`build\out\NetBall.exe` = **1,053,696 B / sha256 `e085727cf87e4df22b551596b2cb53d407ef177ba2539b4d9e6ac0fb33ebe983` / mtime 21:28**；`dist\NetBallSetup-1.0.0.exe` = **2,414,810 B / sha256 `cddc99a7f087b91cb0144ec015a3c8d2fe6fb65f225ffaa842651d8c791ff969`**（21:36，ISCC exit 0、零告警）。旧锚点 `7269AFAA…`(exe) / `778E7A69…`(setup) **已作废**
- **修复内容**：D-01 负坐标位置记忆（`config.cpp` load 只做语法校验 + `ball_window.cpp` 删 `x<0||y<0` 短路，语义判定只在 `IsPositionUsable` 一处）、D-06 拖动严格钳制（`ClampFullyInsideVirtualScreen`，0% 出屏）+ 托盘新增「重置位置」（IDM_RESETPOS=1004）+ `applyConfig` 第二入口修复
- **`build\package.bat` 升级为尺寸+sha 双闸门**（`EXPECT_SIZE=1053696` / `EXPECT_SHA=e085727c…`，尺寸错 exit 3、哈希错 exit 10）→ 机制上杜绝"装的是旧 payload"；**今后改 exe 必须同步改这两个值**
- 当前里程碑：**M4 基本收口**——AC-1 PASS（1,053,184B / FileDescription 13 码点全等 / 图标方差 2129.59 / manifest 双命中）、AC-2 PASS（exstyle=0x80088、APPWINDOW=0）、AC-3 PASS（vs 独立 NIC 聚合 3.34%/5.45%，vs curl 3.25%）、AC-4 PASS 3/3（Δ=0pp，`Windows.Media.Ocr` 机读）、AC-9/11/16a/16b PASS、T7b 六条全 PASS、R3 结案为工具缺陷。**M5 进行中**：T8 安装包由 builder-installer-1 制作
- **撤销**：原批准的 `NETBALL_DEBUG_METRICS` 改程序方案（verifier-4 证明 OCR 路线本机可行，真正障碍是 AC-3 判据设计而非机读能力）
- **【已更正】"僵尸进程"事件实为误判（Leader 自我更正 7）**：曾判定 `pid 18240` 是 verifier-3 遗留僵尸并写入规程。**实际该进程由 builder-ui-1 在 19:29:37 自行启动**；`19:10:18` 是 **exe 的 mtime** 而非进程启动时间——ui-1 的日志把 `$fi.LastWriteTime` 标成 `start=`，Leader 未核字段定义即采信。真实根因是 `FindWindowW` 的 null 编组 bug（见下条）。**新规程：日志字段标签不可信，采信前必须核对定义（`start` 到底是进程启动时间还是文件时间）**。原「退役前确认进程归零」仍保留，但作为常规卫生要求，非本事件起因
- **环境硬坑（verifier-2 发现，危害最大的一条）**：DPI-unaware 的 PowerShell 调 `Graphics.CopyFromScreen` 会**永久阻塞**；先 `SetProcessDpiAwarenessContext(-4)` 后同一调用 ~35ms 完成。**不是"截错"而是"静默卡死"** —— 这一条可解释今日多次"抓图卡住/时好时坏"（`check_screenshot.ps1` 无此设置）。另：对其它进程的**模态对话框**用 `PrintWindow(PW_RENDERFULLCONTENT)` 不返回；目标进程构建向导页期间 `GetWindowText` 会阻塞，须先 `SendMessageTimeout(WM_NULL)` 探活
- **T9 结论措辞约束（防止过度声称）**：payload 第 1 段为**授权回退口径**（本机无 7z；官方 innoextract 1.9 仅支持到 Inno 6.0.5，对本包 6.7.3 报 `Unexpected setup loader revision: 2`）→ 交付文档**不得**写成"已直接抽出内嵌文件"，须写"等价回退 + 三点哈希闭环 + 残余风险：不覆盖压缩流损坏却恰好解压成功的极端情形"。提权安装路径（Program Files/UAC/HKLM 卸载项）**完全未覆盖，结论不可外推**
- **Win32/PowerShell 陷阱（builder-ui-1 发现）**：`[DllImport] FindWindowW(string cls, string title)` 传 `$null` 时会被编组成**空字符串**而非 NULL → 变成"查找标题为空串的窗口"，**永远返回 0**，而窗口其实存在（实测 `mainHwnd=1117044 / class=NetBallBallWnd / visible=True` 但 `FindWindow=0x0`）。修法：签名改 `FindWindowW(string cls, IntPtr title)` 并传 `[IntPtr]::Zero`，并加 `Process.MainWindowHandle + GetClassNameW` 兜底。**危害：会把"探测失败"写成"产品窗口打不开"的假缺陷**
- **方法学补充（verifier-4）**：`.ps1` 中**不得出现中文路径字面量** —— `powershell -File` 对无 BOM 文件按 ANSI 解码，`D:\agentproject\网速悬浮球` 会解成乱码导致 `Get-Item` 直接 PathNotFound。用 `$PSScriptRoot\..\..\..` 推导路径。**这条比"码点构造中文字符串"更前置：路径先崩，码点还没机会执行**
- **AC-3 采信下限**：<200 KB/s 时 1s 重绘与采样点偏移的噪声会主导比值（实测 +18.4% / +52.7% / +75% / −46%），此时须报 `BLOCKED-速率过低`，**不得判 FAIL**
- **M5 新增验证项（verifier-4 提议，Leader 采纳，交由 verifier-2 执行）**：**安装包 payload 三段哈希比对** —— ①从 `dist/NetBallSetup-1.0.0.exe` 内嵌 payload 抽出 `NetBall.exe` 算 sha256，须等于 `7269AFAAF069365DB42DFF2C5D8F8F79658248FBB6F466890AFF4C10B64A74F0`；②装到临时目录后对**落盘文件**再算一次，须同值；③在**安装后的文件**上重跑 AC-1（bytes=1,053,184 + `FileDescription` 13 码点 + 图标存在）。目的：防「installer 从 build\out 另存/重打包时哈希漂移无人察觉」与「安装器改写版本资源」。三步不启动程序、不占互斥体，可与其他工作并行
- 早期里程碑：**M3 已达成**（`build/out/NetBall.exe` = 1,053,184 B / 1.004MB；build.bat 一次跑通 exit 0；WS min/avg/max 3.05/3.08/3.16MB；CPU avg 0.0128%；冷启动 73ms；繁忙长跑 WS 峰值 10.66MB <15MB）；**M4 进行中**：T7（verifier-1：AC-1/2/3/4/5/9/11/16）与 T7b（verifier-3：AC-6/7/8/10/12/17）并行
- 已完成：T1（Leader 已复核产物与证据为真）、T5（windres exit 0，app.res 39,984B，ico 7 尺寸）、T4（settings_dialog/tray，Leader 独立 syntax 复验 exit 0）
- Leader 只读核验（`.agent/verchk.ps1` → `.agent/verchk.txt`）：NetBall.exe SIZE=1,051,648；FileVersion=1.0.0.0；ProductName=NetBall；OriginalFilename=NetBall.exe；FileDescription 码点 004E-0065-0074-0042-0061-006C-006C-0020-7F51-901F-60AC-6D6E-7403 = 「NetBall 网速悬浮球」，**无 windres 乱码**。注：脚本首次 MATCH=False 系 Leader 自身期望串少写一个 `l`（004C 只写了一次），非产物缺陷，已修正结论。
- 进行中：T2（builder-core-1，18%）、T3（builder-data-1）、T4（builder-ui-1）、T7 阶段一（verifier-1）
- 已验证事实：spike.exe 49,664B；分层窗口 exstyle=0x80088（LAYERED|TOOLWINDOW|TOPMOST）；四角 alpha=0；预乘无违例；抗锯齿过渡像素 26,387；真实拖动 400,300→535,372；右键菜单可退出；托盘 NIM_ADD=TRUE；本机 DPI=175%
- 未决问题：
  1. spike WS=14,872KB 逼近 15MB 红线 → 已下达优化令（DIB/Graphics/Font 复用、最小 GdiplusStartupInput、跳过无变化重绘），仅允许启动后单次 SetProcessWorkingSetSize，T6 需报 6 项指标
  2. 环境坑：共用一个 cmd 控制台会导致 stdout 串台；.ps1 无 BOM UTF-8 会按 GBK 解析报错 → 已全队广播（命令重定向落盘 + 脚本 ASCII-only）
  3. build/out 目前混有 spike.exe/_t4test.exe/日志 → 已令 T6 前清入 build/_tmp/

## AUDIT-LOG
| 轮次 | 缺陷数 | 已修复 | 结论 |
|---|---|---|---|
| - | - | - | - |

## HANDOVER

### H-01（builder-assets-1 → Leader 直管）
- FROM builder-assets-1 / TO Leader（M6 后统一归档）
- TASK-ID T5（已完成，无后续）
- 已完成：`tools/make_icon.py`（幂等，sha256 6293af16…b9a9a）、`assets/app.ico`（36,293B / 7 帧）、`assets/icon-preview-256.png`（preview 附产物）、`src/resource.h`、`src/app.rc`、`src/app.manifest`
- 当前状态：全部落盘且 windres exit 0（build/app.res 39,984B）；桩程序实测 VersionInfo 中文正常、图标可抽出
- 下一步：无（T8 直接复用 `assets/app.ico`；`assets/icon-preview-256.png` 由 T6 移入 `build/_tmp/`）
- 坑与教训：① windres 默认 CP936 → 中文乱码，必须 `#pragma code_page(65001)` 且文件 UTF-8 无 BOM；② rc 内相对路径基准是 rc 所在目录（src\），故 `..\assets\app.ico`
- 验证证据：`build/app.res` 39,984B；windres exit 0；XML 解析 OK；`ExtractAssociatedIcon` 与 ico 32 帧一致

### H-02（builder-assets-2 → Leader 直管，供 T8 引用）
- FROM builder-assets-2 / TO Leader（`builder-installer-1` 只读引用）
- TASK-ID T11（已完成，无后续）
- 已完成：`tools/make_wizard.py`、`assets/installer/wizard-large.bmp`（164x314/24bpp/154,542 B）、`wizard-small.bmp`（55x58/24bpp/9,798 B）、`README.txt`（4,190 B，UTF-8 BOM + 全 CRLF，7 小节）；已删除 `assets/icon-preview-256.png`
- 当前状态：可交付；ISCC 探针实测 `Successful compile` exit 0（含 WizardImageFile/WizardSmallImageFile/InfoBeforeFile 三项读取成功）
- 下一步：T8 在 `.iss` 中引用，不设 `WizardImageStretch`，`InfoBeforeFile` 指 README
- 坑与教训：① README 里写按钮名必须先回读 `settings_dialog.cpp` 实际字符串（曾误写「确定」，实为「保存」）；② README 对用户的承诺（卸载询问保留配置、开始菜单程序组）必须由 `.iss` 兑现，否则文档与行为不符；③ 不得声明未测试的系统版本
- 验证证据：`build/_tmp/assets_t11_bmp.log`（BMP 头 9 项 PASS）、`assets_t11_readme.log`（PASS/0 失败）、`assets_t11_iscc.log`（exit 0）

### H-04（verifier-3 → T10 / 后续维护）
- FROM verifier-3 / TO auditor（T10）
- TASK-ID T7b（done）：AC-6/7/8/10/12/17 全部 PASS
- 资产：`docs/verify/t7b/`（19 个脚本）、`docs/verify/media-t7b/`（24 张真实截图 + 全部日志）
- **可复用的三层门禁**：①`Initialize-Measurement` 统一入口（轮询 0 残留 → `Assert-Anchor` → 复制私有副本 → 副本 sha 二次校验）；②10 道 `BLOCKED-*` 字面量门禁（`SETUP/BUSY/NOWND/NODLG/DPI/STALE/ANCHOR/REPLACED/SHOT/NOWINDOW`）；③每步之后父进程重算 `build\out` sha，不符即中止整个窗口
- **方法论**：正式跑之前先**干跑测试框架本身**（用只读子脚本验证 `Start-Process + 重定向 + -Wait + ExitCode + 输出采集`），提前挡掉两类静默失败（`Get-Content -Raw` 空文件返回 `$null`、重定向被按 GBK 读回）
- **三个「假成功」陷阱**：①形状探针落在文字带 → 把字形像素当形状像素；②`@(...).Count` 前用 `$null -eq 0` → 零失败被判 FAIL；③不做形状掩膜的 `-ExpectText` → 亮壁纸下桌面像素被当字形（与 R3 同源）。**共性：都不报错，只表现为"数据与另一条证据打架"，唯有交叉核对可发现**
- **DPI 感知截图标准写法**：取 rect 前 `SetThreadDpiAwarenessContext(-4)`；自检 `size=88→154×154`、`size=160→280×280`、胶囊 160→`420×280`
- 证据锚点：全部结论绑定 `sha256=7269AFAAF069365DB42DFF2C5D8F8F79658248FBB6F466890AFF4C10B64A74F0`

**H-04 附：证据文件与结论对应表（T10 按此读，勿采信其它文件）**
| AC | 结论日志（以此为准） | 截图 |
|---|---|---|
| AC-6 | `media-t7b/ac6-reanalyze.log` | `ac6-shape0/1/2.png` |
| AC-7 | `media-t7b/ac7.log` | `ac7-base/op20/op100.png` |
| AC-8 | `media-t7b/ac8.log`（图像层 + maximin 数学层两段） | `ac8-*.png`（9 张） |
| AC-10 | `media-t7b/ac10.log`（旧构建，与配色无关） | — |
| AC-12 | `media-t7b/ac12.log`（旧构建，与配色无关） | `ac12-ball-menu / tray-menu / ball-visible / ball-hidden.png` |
| AC-17 | `media-t7b/ac17-rerun.log` | `dpi-175-size48/size160.png`、`ac17-size48/size160.png` |
| R3 矩阵 | `media-t7b/r3.log` + `t7b-window.log` | `r3-s{48,88,160}-{dark,white,gray}-r1..r3.png`（27 张） |
| 资源占用 | `res-prewindow.log` / `res-postwindow.log` / `t7b-window.log` | — |

⚠️ **T10 必读**：`media-t7b/ac6.log` 与 `media-t7b/ac17.log` 中各留有一条**已作废的 `RESULT: FAIL`**（分别来自旧探针集合、`Get-FailCount` 返回 `$null` 的判据缺陷）。二者**不得作为结论引用**，正确文件见上表加粗项。verifier-3 停机前申请给这两份加 `SUPERSEDED-by` 头部，因已停机未执行——**审计阅读时请自行按本表对应**。

### H-03（builder-core-1 → 待接续者 builder-core-2 / 缺陷时启用）
- FROM builder-core-1（93% 退役）/ TO 未定，缺陷出现时按本单拉起 builder-core-2
- TASK-ID T1/T2/T6（全部 done）
- 已完成：`src/main.cpp`、`src/ball_window.cpp/.h`、`src/render.cpp/.h`、`src/app_common.h`（契约冻结）、`src/spike/`、`build/build.bat`
- **溯源更正（供 T10）**：终版 exe（mtime 19:02:56 / 1,053,184 B / sha `75d32fa2…4633`）**由 builder-core-1 在窗口内构建**，非 builder-data-1 代建。Leader 曾因 core-1 上下文 93% 导致"动手早于回报"而误判其 STUCK、改派 data-1；data-1 的构建令已撤销，产物未被二次覆盖。**教训：上下文接近上限的成员「回报延迟」≠「未动手」，处置前必须先查产物 mtime 再下结论。**（这是 Leader 第二次误判成员健康态）
- 当前状态：终版 `build/out/NetBall.exe` = 1,053,184 B / mtime 19:02:56 / sha256 `75D32FA27672A36F93144BF18623AEC73136CCCB34896A17B4303810A5BE4633`（Leader 用 certutil 独立复算一致）；7 源文件 `-Wall -Wextra` **零告警**
- 关键实现要点：`UpdateLayeredWindow` + 32bpp PARGB DIB；`ComputeSize = MulDiv(size, dpi, 96)` 无阈值分支；`WM_DPICHANGED` 内显式 `PaintNow()`；`render::autoTextColor` 为 maximin，`<4.5` 退化典型中灰启发式（设计选择，已注释）；`IDM_*` 全部在 `main.cpp` 分发；托盘由 `tray::add` 内部 `SetWindowSubclass` 自管（main **不得**重复转发）
- `build/build.bat` 用法：release 默认**跳过 spike**（需复跑设 `BUILD_SPIKE=1`，产物只落 `build/_tmp/`）；已运行中的 exe **只警告不杀**（禁止 `taskkill /f /im`）
- 坑与教训：① wc 读数/时间戳必须核对（今日 5 次"把过期状态当现状"）；② rc 的 DPI 陷阱：unaware 进程 `GetWindowRect` 返回 DIP 值（175% 下差 1.75 倍）；③ `windres` 默认 CP936，`app.rc` 必须保留 `#pragma code_page(65001)`；④ 构建前必须轮询确认无 NetBall 进程占用
- 验证证据：`build/_tmp/core1/`（四尺寸矩阵、post-size88/160 截图、8 色表、告警日志、清理清单）
