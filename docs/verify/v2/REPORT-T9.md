# T9 安装/卸载独立验证报告（verifier-2）

- 成员：verifier-2 ｜ 角色：Verifier（只读，未修改任何交付物）
- 被测：`dist\NetBallSetup-1.0.0.exe` v1.0.0（2,413,546 B / sha256 `778E7A69…D0247F`）
- 内嵌主程序：`NetBall.exe` 1,053,184 B / sha256 `7269AFAA…4A74F0`
- 环境：Windows 10/11 单用户 `sihuo`，DPI 168（175%），虚拟屏 L=-1920 W=5760 H=2160
- 安装方式：全部 `/CURRENTUSER /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /LANG=chinesesimplified`
- 时间：2026-11-02 20:15 – 20:40
- 全部结论为独立复现，未采用 builder 的 `build\_tmp\ins1\` 结论

## 0. 总表

| 项 | 结果 | 一句话结论 |
|---|---|---|
| payload 三段哈希 | **PASS**（第 1 段为授权回退口径） | 安装包/落盘文件/版本资源三段一致，无重打包漂移 |
| A.1 内嵌 payload 解包 | **BLOCKED → 回退** | 本机无 7z；innoextract 1.9 不支持 Inno 6.7.3 loader rev 2 |
| AC-13 安装后三处在位 | **PASS** | 安装目录 exe + `unins000.exe` + `卸载 NetBall.lnk` + 开始菜单 2 项，四类快捷方式 target 全部正确 |
| AC-14 卸载无残留 | **PASS** | 目录/开始菜单/桌面/卸载键/Run 值全清，无 `_unins.tmp` 遗留；配置默认保留 |
| AC-15 桌面图标可选 | **PASS（两态）** | `/TASKS=""` → 无 lnk；`/TASKS="desktopicon"` → 有 lnk 且 target 指向 `{app}\NetBall.exe` |
| AC-18 中文向导 | **PASS** | 真交互跑通，截图 + OCR + 窗口文本码点三重取证 |
| AC-18 卸载询问保留配置 + 点「否」删配置 | **PASS** | 弹窗中文、截图在案，点「否」后 `%APPDATA%\NetBall` 递归消失（两次独立复现） |
| F 提权路径 | **NOT-RUN（按指示仅静态）** | `PrivilegesRequired=admin` 默认装 Program Files，本环境无法应答 UAC |

## A. payload 三段哈希

| 段 | 对象 | 实测 | 判定 |
|---|---|---|---|
| 0 | `dist\NetBallSetup-1.0.0.exe` | 2,413,546 B / `778e7a693ae58e3a5a6dfd66468e2a2ee30aed666fa1dfe41876488540d0247f` | 与规格一致 |
| 1 | 内嵌 payload（回退：由 Inno 自身解压落到临时安装目录的 `NetBall.exe`） | 1,053,184 B / `7269AFAAF069365DB42DFF2C5D8F8F79658248FBB6F466890AFF4C10B64A74F0` | 一致 |
| 2 | 安装后落盘 `{app}\NetBall.exe` | 同上哈希、同字节数 | 一致 |
| 3 | 安装后文件版本资源 | `FileDescription` 13 码点逐一比对 `U+004E U+0065 U+0074 U+0042 U+0061 U+006C U+006C U+0020 U+7F51 U+901F U+60AC U+6D6E U+7403` 全等；`FileVersion=1.0.0.0`；`ProductName=NetBall`；`OriginalFilename=NetBall.exe`；`Language=简体中文(中国大陆)`；图标可提取 32×32、319 色（非空白） | 一致，安装器未改写版本资源 |
| 附加 | `build\out\NetBall.exe`（`[Files]` 的 Source） | 同哈希 `7269AFAA…` | 构建产物 → 包内 → 落盘 三点闭环，无「另存/重打包」漂移 |

证据：`a0_probe_setup.log`（包哈希+innoextract 失败）、`a3_exe_meta_installed.log`、`a3_exe_meta_buildout.log`、`media-v2\icon-from-installed.png`、`media-v2\icon-from-buildout.png`

### A.1 解包工具不可用的确证（回退理由）
- `where 7z` / `C:\Program Files\7-Zip` / `C:\Program Files (x86)\7-Zip` 均不存在（`env_probe.log`）。
- 官方 innoextract 最新版即 1.9（GitHub releases API：`tag_name=1.9`），实测对本包：
  `Warning: Unexpected setup loader revision: 2` / `Setup loader checksum mismatch!` / `Could not determine setup data version!` → rc=2（`a0_probe_setup.log`）。
  其自带 README 明示支持范围为 "Inno Setup 1.2.10 to 6.0.5"，本包由 Inno 6.7.3 编译，超出支持范围。
- 因此第 1 段采用 Team Lead 授权的回退口径：**装到临时目录后取 Inno 解压出的落盘文件**。
  该口径的等价性依据：本 `.iss` 的 `[Files]` 两行均为 `Source → {app}` 直接拷贝（`ignoreversion`，无 `[Code]` 改写文件），
  Inno 不对文件内容做二次加工；且第 1 段与 `build\out` 源文件哈希相同，三点闭环。
  **残余风险**：该口径无法证明「包内压缩流」与「build\out 源文件」逐字节相同以外的内容一致性（例如若有人手工替换压缩流为不同内容，
  落盘哈希仍会变，故仍能捕获），但不覆盖「压缩流损坏而解压恰好成功」这类极端情形。

## B. AC-15 桌面图标为可选任务（两态）

| 态 | 命令 | 桌面 `NetBall.lnk` | 证据 |
|---|---|---|---|
| 无任务 | `/VERYSILENT /CURRENTUSER /SUPPRESSMSGBOXES /NORESTART /SP- /TASKS=""` | **不存在**（`DESKTOP_LNK_ABSENT`） | `a2_state_notask.log` |
| 有任务 | 同上 + `/TASKS="desktopicon"` | **存在**，1244 B，`target={app}\NetBall.exe`，`workdir={app}`，`icon=,0` | `b2_state_desktopicon.log`、`e5_lnk_targets.log`、`e5_install_links.log`（`dir /a` 原始输出） |

说明：向导里 `desktopicon` 为 `checkedonce`（首次安装默认勾选，见 `NetBall.iss` [Tasks] 与 `media-v2\e1_03_page.png`），
但命令行 `/TASKS=""` 能可靠清零 —— 两态均可复现（各 2 次以上）。

## C. AC-13 安装后三处在位

`dir /a` 原始输出（`e5_install_links.log`）：

```
C:\Users\sihuo\AppData\Local\Programs\NetBall
  1,053,184 NetBall.exe
      4,190 README.txt
      5,580 unins000.dat
  4,386,294 unins000.exe
      1,187 卸载 NetBall.lnk

%APPDATA%\...\Start Menu\Programs\NetBall
  1,258 NetBall 网速悬浮球.lnk
  1,265 卸载 NetBall.lnk        (count=2)
```

快捷方式解引用（`e5_lnk_targets.log`），四项 `match=True`：

| 快捷方式 | target |
|---|---|
| `{app}\卸载 NetBall.lnk` | `{app}\unins000.exe` |
| 开始菜单 `NetBall 网速悬浮球.lnk` | `{app}\NetBall.exe` |
| 开始菜单 `卸载 NetBall.lnk` | `{app}\unins000.exe` |
| 桌面 `NetBall.lnk` | `{app}\NetBall.exe` |

注册表（`a2_state_notask.log`）：`HKCU\...\Uninstall\{8F3A1C2E-5B47-4D9A-9E62-0C7D4B15A8E3}_is1`
→ DisplayName `NetBall 网速悬浮球`、DisplayVersion `1.0.0`、DisplayIcon/UninstallString/InstallLocation 均指向 `{app}`、Publisher `NetBall`。

补充：`unins000.msg` 未落盘属正常（仅在消息需外部文件时才生成），`unins000.dat` 存在（5,580 B @ 装 desktopicon 时 / 5,412 B @ 无任务）。

## D. AC-14 卸载无残留

静默卸载：`{app}\unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`（三轮：`a4`、`d2`、`e6`），全部 421–415 ms 返回，
Inno 日志 `Uninstallation process succeeded.` / `Need to restart Windows? No`。

| 断言 | 结果 | 证据 |
|---|---|---|
| 安装目录消失 | `APPDIR_ABSENT` | `d3_state_after_unins2.log`、`e6_final_uninstall.log` |
| 开始菜单组消失 | `STARTMENU_ABSENT` | 同上 |
| 桌面快捷方式消失 | `DESKTOP_LNK_ABSENT` | 同上 |
| Run 值 `HKCU\...\Run\NetBall` 被删 | **先人工写入**（`c1_run_set.log`，`reg query` 有值）→ 卸载后 `RUNVALUE_ABSENT` + `reg query` 返回 "unable to find the specified registry key or value" | `d1_state_before_unins2.log`、`d3_state_after_unins2.log`、`d4_run_query_after.log` |
| 卸载注册表项消失 | `UNINSTALL_KEY_ABSENT` | 同上 |
| 无 `_unins.tmp` / `unins000.exe` 遗留进程 | `NO_LEFTOVER_PROCESS`（3/3 轮） | `a4`、`d2`、`e6` |
| `%APPDATA%\NetBall` 静默路径保留 | `CFGDIR_EXISTS \config.ini 193` | `d3_state_after_unins2.log`、`e6_final_uninstall.log` |

关于 builder 修复的「静默卸载卡在不可见 MsgBox」：**修复有效**。三轮静默卸载进程均在 ~0.4 s 内退出，卸载器自身与副本进程全部消失，
无僵尸 `_unins.tmp`；对应 `[Code] CurUninstallStepChanged` 中 `if UninstallSilent then {keep config}` 分支。

## E. AC-18 中文向导 + 「点否删配置」（真交互，无静默替代）

### E1 向导为简体中文
窗口类 `TWizardForm`，标题 `安装 - NetBall 网速悬浮球`（码点 `U+5B89U+88C5…U+7403`）。

| 页面 | 截图 | 窗口文本（机器可读） | OCR 命中 |
|---|---|---|---|
| 欢迎页 | `media-v2\e1_01_welcome.png`（141,639 B，3371 色） | `欢迎使用 NetBall 网速悬浮球 安装向导`、`点击“下一步”继续，或点击“取消”退出安装程序。`、按钮 `下一步(&N)` / `取消` | `安装向导`、`取消`、`下一步` 全命中（`e2_ocr_welcome.log`） |
| 目标位置页 | `media-v2\e1_02_page.png`（71,038 B） | `选择目标位置`、`TNewPathEdit = C:\Users\sihuo\AppData\Local\Programs\NetBall`、`上一步(&B)/下一步(&N)/取消` | — |
| 附加任务页 | `media-v2\e1_03_page.png` / `e1_tasks_page.png` | `选择附加任务`、`您想要安装程序执行哪些附加任务？` | `附加任务`、`创建桌面快捷方式` 命中（`e2_ocr_tasks.log`，OCR_TEXT 含 `附加快捷方式 创建桌面快捷方式（D)`） |

取消路径：点「取消」→ 出现 `#32770` 对话框「退出安装程序」/`安装程序尚未完成。如果现在退出，将不会安装该程序。`/`是(&Y)` `否(&N)`
→ 点「是」→ 向导关闭、`APPDIR_EXISTS=False`、`NO_SETUP_PROCESS`（`e1_probe_cancel2.log`、`e1_cancel_final.log`）。
注：取消动作在 4 轮交互运行中均成功执行（`e1_probe_cancel*.log`），其中 2 轮的脚本在「取消后继续做截屏/枚举」处被宿主命令超时打断，
向导本身均被正常关闭、未产生安装目录；故取消判据以「向导窗口消失 + 无安装目录 + 无残留进程」为准，三者每轮均成立。

### E2 「是否保留个人配置」弹窗 → 点「否」→ 配置被删

非静默运行 `{app}\unins000.exe`（**不带** `/SILENT`、`/SUPPRESSMSGBOXES`），两个对话框：

1. 标题 `NetBall 网速悬浮球 卸载`，正文 `您确认要完全移除 NetBall 网速悬浮球 及其所有组件吗？`，按钮 `是(&Y)` / `否(&N)`（截图 `media-v2\e2_01_uninstall_confirm.png`，19,628 B / 255 色）
2. 标题 `卸载`，正文：
   `是否保留个人配置与设置？` / `配置目录：C:\Users\sihuo\AppData\Roaming\NetBall` /
   `选择「是」：保留配置，重新安装后沿用原有设置。` / `选择「否」：连同配置一并删除，下次安装恢复全部默认设置。`
   按钮 `是(&Y)` / `否(&N)`（截图 `media-v2\e2_02_keepconfig_prompt.png`，25,647 B / 238 色）

点击方式：`FindWindow` 类 `#32770` → 枚举子 Button → `PostMessage(BM_CLICK)`（未用 SendKeys，避免焦点依赖）。

两次独立复现：

| 轮次 | 点击「否」时刻 | 前态 | 后态 |
|---|---|---|---|
| E2 | 20:34:43 | `%APPDATA%\NetBall\config.ini` 193 B（09:xx 生成，见 baseline） | `CFGDIR_ABSENT`（`e3_state_after_click_no.log`） |
| E2b | 20:37:36 | `e2b_pre.log`：`CFGDIR_EXISTS \config.ini 193` | `e2b_post.log`：`CFGDIR_ABSENT` |

弹窗消失后卸载器显示 `已顺利从您的计算机中移除 NetBall 网速悬浮球。` + `确定`，点掉后进程全部退出。

## F. 静默/提权路径边界（仅静态 + 记录）

1. **`/CURRENTUSER` 落点**：实测安装目录 `%LOCALAPPDATA%\Programs\NetBall`，向导目标位置页 `TNewPathEdit` 同值，
   卸载键 `InstallLocation` 同值 → `{autopf}` 在 `/CURRENTUSER` 下解析为 `%LOCALAPPDATA%\Programs`，与脚本文档一致。
2. **提权路径未实跑**：`NetBall.iss` 第 81-82 行 `PrivilegesRequired=admin` + `PrivilegesRequiredOverridesAllowed=commandline`，
   即普通双击默认装 `{autopf}\NetBall` → `%ProgramFiles%\NetBall` 并请求 UAC。本环境无法应答 UAC 弹窗（无交互提权凭据），
   **未实测**，不得据此判定提权安装/卸载行为。静态可确认：全脚本无任何 HKLM 写入（第 133-145 行注释与正文一致），
   卸载信息由 Inno 按安装模式写到 HKLM 或 HKCU，二者一致。
3. **per-user 边界（builder 声明，静态复核成立）**：
   - （a）`HKCU\...\Run\NetBall` 的 `uninsdeletevalue` 只清「执行卸载者」自己的账户；多用户各自开过自启时，其它账户的 Run 值会残留（指向已删 exe）。
   - （b）`{userappdata}` 在提权卸载时解析为「执行卸载者」的 `%APPDATA%`；A 装 B 卸时，保留配置的询问针对 B 的配置目录。
     本次实测的弹窗正文显示 `C:\Users\sihuo\AppData\Roaming\NetBall`，与「当前卸载账户」一致，与（b）的静态结论吻合（同账户场景，不构成跨账户实测）。

## 已知限制 / 风险

1. **A.1 未能直接解包**（理由见上）。若 Leader 需要「真解包」硬证据，需先在有 7-Zip 或支持 Inno 6.7 的解包器（innoextract 1.9 不支持）的环境重跑。
2. **提权路径（Program Files + UAC）完全未覆盖**。
3. **环境坑（非产品缺陷，供后续验证者复用）**：
   - 本机 DPI-unaware 的 PowerShell 进程调用 `Graphics.CopyFromScreen` 会**永久阻塞**；先 `SetProcessDpiAwarenessContext(-4)` 后同一调用 ~35 ms 完成（`p_gui.ps1` 已内置）。
   - 对其它进程的**模态对话框**用 `PrintWindow(PW_RENDERFULLCONTENT)` 不返回；改用屏幕抓取 + `SetForegroundWindow` 可靠。
   - 窗口收尽（`GetWindowText`）在目标进程构建向导页时可能阻塞，需先 `SendMessageTimeout(WM_NULL)` 探活（`Wait-Responsive`）。
   - 部分日志出现中文乱码（`鍗歌浇`）纯属控制台代码页显示问题；判定用的名称由码点构造（`[char]0x5378` 等），不受影响。
4. **`%APPDATA%\NetBall` 已被 E 组实验删除过**，已按 baseline 备份原样恢复：
   恢复后 `config.ini` 193 B / sha256 `38a32e914e9769c3f3caba303b72d83fa6a91f59e7376aaa0101b90bed2a57ec`（`e4_restore_cfg.log`）。

## 环境最终状态（`e6_final_uninstall.log` / `e4_restore_cfg.log`）

```
APPDIR_ABSENT / DESKTOP_LNK_ABSENT / STARTMENU_ABSENT
RUNVALUE_ABSENT / UNINSTALL_KEY_ABSENT / NO_RELATED_PROCESS
%APPDATA%\NetBall\config.ini  193 B (已恢复)
```

## 文件清单

脚本（`build\_tmp\v2\`）：`p_state.ps1`、`p_install.ps1`、`p_uninstall.ps1`、`p_exe_meta.ps1`、`p_setrun.ps1`、
`p_gui.ps1`、`p_ocr.ps1`、`p_dlg.ps1`、`p_lnk.ps1`、`p_e1_wizard.ps1`、`p_e2_launch.ps1`、`p_close_wizard.ps1`、
`p_e1_probe.ps1`、`p_shot_test*.ps1`、`e2b_pre.bat`、`e5_cycle.bat`、`a0_probe_setup.bat`

日志（`docs\verify\v2\`）：`env_probe.log`、`env_inno.log`、`s0_baseline.log`、`a0_probe_setup.log`、`a1_install_notask.log`、
`a2_state_notask.log`、`a3_exe_meta_installed.log`、`a3_exe_meta_buildout.log`、`a4_uninstall1.log`、`a5_state_after_uninstall1.log`、
`b1_install_desktopicon.log`、`b2_state_desktopicon.log`、`c1_run_set.log`、`d1_state_before_unins2.log`、`d2_uninstall2.log`、
`d3_state_after_unins2.log`、`d4_run_query_after.log`、`e1_probe*.log`、`e1_wizard*.log`、`e1_steps.log`、`e1_cancel_final.log`、
`e2_install3.log`、`e2_launch.log`、`e2_dlg_steps.log`、`e2_dlg*_stdout.log`、`e2_ocr_welcome.log`、`e2_ocr_tasks.log`、
`e3_state_after_click_no.log`、`e2b_pre.log`、`e2b_install.log`、`e2b_dlg_steps.log`、`e2b_post.log`、`e4_restore_cfg.log`、
`e5_install_links.log`、`e5_lnk_targets.log`、`e6_final_uninstall.log`、`shot_test.log`、`media_list.log`

截图（`docs\verify\media-v2\`）：`e1_01_welcome.png`、`e1_02_page.png`、`e1_03_page.png`、`e1_tasks_page.png`、`e1_welcome.png`、
`e2_01_uninstall_confirm.png`、`e2_02_keepconfig_prompt.png`、`icon-from-installed.png`、`icon-from-buildout.png`
