# NetBall 网速悬浮球

NetBall 是一款 Windows 桌面常驻信息挂件：一个始终置顶的悬浮球，加上一个可选的、嵌在任务栏通知区域里的信息面板。形态上参考手机上的“灵动岛”，把常驻但不必盯着的读数收进一个可拖动的小块里。当前版本 v1.4.4，支持网速、内存、CPU 与 GPU 的负载、温度，以及 CPU 与 GPU 功率共 18 项指标，可在两个表面自由组合排版。程序是单文件可执行文件，运行时不需要管理员权限，也不安装任何内核驱动。

## 特性
### 显示
- 18 项可显示指标：5 个基础项、1 个 CPU 功率项、4 张显卡 × 3 项（负载、温度、功率），采样频率 500 到 3000 ms 可调，默认 1000 ms
- 两个显示表面各自独立开关：桌面悬浮球、任务栏内嵌面板（位于通知区域时钟左侧，不占用任务按钮空间）
- 托盘悬停提示为多行文本：网速、内存与 CPU、CPU 温度与功率、每张显卡各占一行

### 外观
- 5 种材质：纯色、亚克力、玻璃、液态玻璃、无底板
- 3 种形状：圆、圆角方、胶囊
- 每个表面独立可调：浓度、模糊（0 到 40 DIP）、圆角、描边、高光、文字颜色；自动文字色按真实合成画面的 WCAG 对比度采样，在黑与白之间择优

### 交互
- 悬浮球左键按住拖动，松开后位置自动记住
- 悬浮球右键、托盘单击 / 双击 / 右键，都能呼出菜单
- 任务栏面板完全点击穿透，也不抢焦点
- 设置窗口分三页，任何改动即时预览，点“取消”或按 ESC 回滚
- 单实例运行（互斥体 `NetBallSingleInstance`），第二次启动直接退出；窗口不抢焦点

### 性能
- 常驻内存约 6 到 8 MB，CPU 占用约 0%（官方验收实测 0.01% 到 0.26%）
- 冷启动到首帧约 50 ms
- 主程序约 1.14 MB（1,195,520 字节），静态链接，除系统 DLL 外无第三方运行时依赖

## 界面与交互
### 桌面悬浮球
左键按住拖动即可移动，松开后位置自动记忆（记录虚拟屏坐标，支持带负坐标的多显示器布局），拖动过程中球体始终完整限制在虚拟屏内。右键弹出菜单，条目按顺序如下：

- 设置…
- （分隔线）
- 总在最前
- 开机自启
- 在任务栏显示
- 重置位置
- （分隔线）
- 关于…
- 退出

### 任务栏面板
- 完全点击穿透（`WS_EX_TRANSPARENT` + `WM_NCHITTEST` 返回 `HTTRANSPARENT`），鼠标事件直接落到下层窗口
- 不抢焦点，固定置顶；面板位置由系统任务栏决定，不单独记忆，只跟随主任务栏，任务栏自动隐藏或前台全屏时自动隐藏
- 因为点击穿透，面板上的文字区域不能右键，所有开关都放在悬浮球菜单、托盘菜单与设置窗口里

### 托盘图标
单击、双击、右键都会弹出同一个托盘菜单：

- 设置…
- 显示悬浮球
- 在任务栏显示
- 重置位置
- （分隔线）
- 退出

### 设置窗口
- 三页：任务栏 / 悬浮球 / 数据
- 任务栏页与悬浮球页：显示开关、排版编辑器、字号与行距与项目间距、形状、颜色与浓度、材质、模糊与圆角、文字颜色、描边与高光、系统实时亚克力（实验性），悬浮球页另加宽高设置
- 数据页：刷新频率、温度数据源、显卡列表与重新探测
- 任何改动即时预览，合并到 16 ms 的刷新节拍；点“保存”写入配置文件，点“取消”或按 ESC 恢复到打开设置窗口之前的状态

## 可显示的数据项
共 18 项，可在两个表面各自自由组合：

| 排版 token | 含义 | 显示示例 |
|---|---|---|
| `down` | 下行速度 | `↓ 12.3 MB/s` |
| `up` | 上行速度 | `↑ 1.05 MB/s` |
| `mem` | 内存占用 | `内存 42%` |
| `cpuload` | CPU 负载 | `CPU 35%` |
| `cputemp` | CPU 温度 | `CPU 48°C` |
| `gpu0load` | GPU0 负载 | `GPU0 35%` |
| `gpu1load` | GPU1 负载 | `GPU1 35%` |
| `gpu2load` | GPU2 负载 | `GPU2 35%` |
| `gpu3load` | GPU3 负载 | `GPU3 35%` |
| `gpu0temp` | GPU0 温度 | `GPU0 48°C` |
| `gpu1temp` | GPU1 温度 | `GPU1 48°C` |
| `gpu2temp` | GPU2 温度 | `GPU2 48°C` |
| `gpu3temp` | GPU3 温度 | `GPU3 48°C` |
| `cpupwr` | CPU 功率 | `CPU 16.5W` |
| `gpu0pwr` | GPU0 功率 | `GPU0 120.5W` |
| `gpu1pwr` | GPU1 功率 | `GPU1 120.5W` |
| `gpu2pwr` | GPU2 功率 | `GPU2 120.5W` |
| `gpu3pwr` | GPU3 功率 | `GPU3 120.5W` |

命名与精度规则：
- 只有 1 张显卡时前缀为 GPU（不带序号），多张时为 GPU0 / GPU1 / GPU2 / GPU3
- 速度单位为 B/s、KB/s、MB/s、GB/s，按 1024 进制换算；数值 ≥ 100 取整，≥ 10 保留 1 位小数，其余保留 2 位小数，并去掉多余的 0 与孤立的小数点
- 无效或不可用的数据显示 `--`，不会伪造数值；托盘悬停提示里的速度格式只到 MB/s

排版用一段字符串描述：`/` 分行、`,` 分项，例如 `down,up/cpuload/gpu0temp`。任务栏面板最多 2 行，悬浮球最多 18 项。

## 数据来源
| 指标 | 采样方式 |
|---|---|
| 网速 | IP Helper `GetIfTable2` 的 `MIB_IF_ROW2` 收发字节计数差分，除以实际耗时（QPC 计时）；只统计 `OperStatus=Up`，并排除回环（24）、隧道（131）与 `FilterInterface` 镜像行 |
| CPU 负载 | `GetSystemTimes` 差分 |
| 内存 | `GlobalMemoryStatusEx` |
| 显卡负载 | PDH 计数器 `\GPU Engine(*)\Utilization Percentage`，按 LUID 汇总 |
| CPU 温度 | PDH `\Thermal Zone Information(*)\High Precision Temperature`，失败时依次回退 ACPI WMI、HWiNFO 共享内存、LibreHardwareMonitor WMI |
| CPU 功率 | PDH `\Energy Meter(*)\Power`（RAPL，Windows 11 才有该计数器） |
| 显卡温度 | NVML（NVIDIA）/ ADL2（AMD） |
| 显卡功率 | NVML（仅 NVIDIA） |

全部采集都不需要管理员权限，也不安装任何内核驱动。传感器在后台线程以低于普通优先级运行，并按需加载：只有排版里出现温度或显卡项目时，才会真正读取传感器。

## 外观与材质
### 材质
| 材质 | 效果 | 备注 |
|---|---|---|
| 纯色 | 纯色填充，浓度即不透明度 | 最省资源 |
| 亚克力 | 抓屏后重模糊 + 磨砂颗粒 + 柔和内阴影 | 浓度建议 30% 到 55% |
| 玻璃 | 中等模糊 + 镜面高光 + 边缘折射 | 浓度建议 20% 到 40% |
| 液态玻璃 | 更强边缘折射 + 移动高光带 + 轻微色散 | 流光是独立的 30 fps 动画 |
| 无底板 | 只显示文字与描边 | 没有背景像素 |

### 形状与尺寸
- 悬浮球支持圆、圆角方、胶囊，任务栏面板支持圆角方与胶囊；胶囊在宽高相等时自动把宽度补成高度的 3:2，保证是横向胶囊
- 悬浮球宽高固定为设置值（40 到 400 DIP），数字变化不改变球体尺寸，也不自动缩放字号
- 字号按当前排版的最坏情况（例如 1024 MB/s、100%、100°C）一次性求解，换数字不会跳动；字号上限受球体尺寸约束，达到上限时数值标签显示“· 上限”
- 拖动字号或宽高滑块时，悬浮球实时跟随变化，便于预览

### 文字颜色与背景刷新
- 自动文字色在绘制前采样已经合成好的表面像素（材质、模糊、浓度、壁纸全部计入），按 WCAG 对比度在黑与白之间择优；无底板时用背景均色判断明暗，也可以手动指定颜色
- 自绘材质每 500 ms 刷新一次背景，采用“可见状态抓屏 + 形状掩码补全”，刷新时不隐藏窗口、不打断拖动；背后正在播放的视频不会逐帧跟随
- “系统实时亚克力”是实验开关，只对亚克力材质有效，需要系统开启“透明效果”，失败时自动回退到自绘材质

## 配置文件
| 项目 | 值 |
|---|---|
| 路径 | `%APPDATA%\NetBall\config.ini` |
| 格式 | Win32 INI，段名 `[NetBall]` |
| 自启注册表 | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 下的 `NetBall` 值（注册表是自启的权威来源） |

配置键与规则：

- 通用键：`shape`、`size`、`opacity`、`bgcolor`、`textauto`、`textcolor`、`refreshms`（采样频率 500 到 3000 ms，默认 1000 ms）、`alwaysontop`、`autostart`、`posx`、`posy`、`savedw`、`savedh`
- 每个表面一套排版与外观键：`tblayout`、`balllayout`、`tbmatv2`、`ballmatv2`、`tbfont`、`ballfont`、`tbtint`、`balltint` 等；数据源键为 `tempsrc`
- 排版用一段字符串描述，`/` 分行、`,` 分项，例如 `down,up/cpuload/gpu0temp`
- 恢复全部默认：退出程序后删除整个 `%APPDATA%\NetBall` 目录
- 覆盖升级：直接运行新安装包，不需要先卸载；配置原样保留，新增键自动补默认值；非法值回落到默认值，而不是被截断

## 环境要求
| 项目 | 要求 |
|---|---|
| 操作系统 | Windows 10（1809，build 17763 及以上）或 Windows 11 |
| 架构 | 仅 64 位 x64 |
| 编译器 | MinGW-w64 g++ 13.1.0（C++17），资源编译用同套工具链的 windres |
| 打包工具 | Inno Setup 6.7.3（ISCC） |
| 运行时依赖 | 无第三方运行时依赖，除 gdiplus、dxgi、comctl32 等系统 DLL 外全部静态链接 |
| 运行权限 | 清单声明 `asInvoker`，运行 NetBall.exe 不需要管理员权限；安装包声明 `PrivilegesRequired=admin`，安装时需要 UAC |

## 编译
一键构建脚本依次完成 windres 编译资源、可选探针、链接 NetBall.exe、打印产物信息，产物路径为 `build\out\NetBall.exe`：

```bat
build\build.bat
```

`build\build.bat` 把 MinGW 路径硬编码为 `C:\application\qt\Tools\mingw1310_64\bin`，换机器必须先改这一行，否则脚本会直接报 “g++ not found”。完整编译命令与脚本中一致：

```bat
windres --input src\app.rc --output build\_tmp\app.res --output-format=coff

g++ -std=c++17 -O2 -s -municode -mwindows -DUNICODE -D_UNICODE -Wall -Wextra ^
    src\main.cpp src\ball_window.cpp src\taskbar_text.cpp src\render.cpp src\text_render.cpp src\layout_model.cpp src\sensors.cpp src\sensors_hwinfo.cpp src\glass.cpp src\surface_style.cpp src\metrics.cpp src\config.cpp src\settings_dialog.cpp src\tray.cpp ^
    build\_tmp\app.res -o build\out\NetBall.exe ^
    -lgdiplus -lgdi32 -luser32 -liphlpapi -lshell32 -lshlwapi -lcomctl32 -luxtheme -lole32 -loleaut32 -lcomdlg32 -ladvapi32 ^
    -ldwmapi -ldxgi -lpdh -lwbemuuid ^
    -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp
```

几点说明：

- 资源编译依赖 `assets\app.ico`；`-Wl,--no-insert-timestamp` 让同源码产出同一 sha256，便于比对产物；图标与安装向导图可用 `python tools\make_icon.py`、`python tools\make_wizard.py` 重新生成，需要 Python 3 与 Pillow
- 可选探针：`set BUILD_SPIKE=1` 后运行 build.bat，产物 `build\_tmp\spike.exe`，不进入正式发布
- build.bat 必须保持纯 ASCII（cmd 代码页问题）；`installer\NetBall.iss` 与 `assets\installer\README.txt` 必须是 UTF-8 带 BOM；仓库用 `.gitattributes` 的 `* -text` 关闭换行转换
- 版本号分散在 6 处，改版本要同步：`src\app_common.h`、`src\app.rc`、`src\app.manifest`、`installer\NetBall.iss`、`build\package.bat`、`assets\installer\README.txt`

## 打包与安装
```bat
build\package.bat
```

手工打包（Inno Setup 6 的 ISCC）：

```bat
"%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" installer\NetBall.iss
```

安装包输出路径为 `dist\NetBallSetup-1.4.4.exe`。安装与卸载：

- 默认安装到 `%ProgramFiles%\NetBall`，`PrivilegesRequired=admin`，双击安装会触发 UAC 提权
- `PrivilegesRequiredOverridesAllowed=commandline`，允许命令行覆盖为当前用户安装，此时 `{autopf}` 解析为 `%LOCALAPPDATA%\Programs`，全程不需要 UAC
- 静默安装参数：`NetBallSetup-1.4.4.exe /CURRENTUSER /VERYSILENT`
- 覆盖升级直接运行新版安装包即可，AppId 固定，会沿用旧安装目录原地覆盖，不会并存两套
- 卸载入口：安装目录下的 `unins000.exe`、开始菜单 NetBall 程序组、Windows“设置 → 应用”；卸载时会询问是否保留个人配置，选“是”保留 `%APPDATA%\NetBall`，选“否”一并删除，静默卸载（`/SILENT`、`/VERYSILENT`）不弹窗，直接保留配置
- `build\package.bat` 内置尺寸与 sha256 闸门（`EXPECT_SIZE`、`EXPECT_SHA`），每次重新编译 `build\out\NetBall.exe` 之后必须同步更新这两个值，否则打包会被直接拒绝

## 目录结构
```text
网速悬浮球/
├─ src/                 全部源码：14 个 .cpp + 头文件 + 资源 + 清单
│  ├─ main.cpp / ball_window.cpp / taskbar_text.cpp  入口、悬浮球、任务栏面板
│  ├─ render.cpp / text_render.cpp / layout_model.cpp 绘制、字体缓存、排版模型
│  ├─ sensors.cpp / sensors_hwinfo.cpp               温度与功率采集、HWiNFO
│  ├─ glass.cpp / surface_style.cpp / metrics.cpp     材质渲染、样式、基础采样
│  ├─ config.cpp / settings_dialog.cpp / tray.cpp    配置、设置窗口、托盘
│  ├─ app_common.h / app.rc / app.manifest           共享契约、资源、清单
│  └─ spike/spike_main.cpp                           可选探针，不进入正式发布
├─ build/               build.bat（一键编译）、package.bat（一键打包）
├─ installer/           NetBall.iss（安装脚本）、ChineseSimplified.isl（语言包）
├─ tools/               make_icon.py、make_wizard.py（生成图标与向导图）
├─ assets/              app.ico、installer/README.txt、wizard-large.bmp、wizard-small.bmp
└─ docs/                版本说明与交付文档
```

`build\out` 与 `build\_tmp` 是构建产物目录，已被 `.gitignore` 忽略。

## 已知限制
**数据**

- CPU 温度来自主板 / EC 定义的热区（ACPI thermal zone），不是 CPU 核心结温（Tctl / Tdie）；需要结温请把温度数据源选成 HWiNFO 或仅 LHM，程序本身不安装任何驱动
- CPU 功率依赖 Windows 11 的 Energy Meter（RAPL），Windows 10 上显示 `--`；读到的是封装功率，包含 CPU 核心、核显与 SoC
- 显卡功率只支持 NVIDIA（NVML），AMD 独立显卡功率显示 `--`；Intel 核显没有公开的温度接口，只能读负载
- 网速是整机所有符合条件网卡的合计吞吐，不是单个连接或进程的速度，与浏览器下载工具的读数口径不同
- 多显卡按枚举顺序映射（DXGI 只暴露 LUID），不同型号驱动的枚举顺序不一致时温度可能对调；虚拟显示器驱动（例如部分远控或模拟器的 IDD）可能被列为额外 GPU，它们的温度与功率显示 `--`

**外观**

- 亚克力材质需要系统“透明效果”开启，否则回退为自绘磨砂；“系统实时亚克力”是实验功能，非激活窗口在 Windows 11 上可能退化为纯色
- 自绘材质每 500 ms 刷新一次背景，背后播放的视频不会逐帧跟随
- 任务栏面板只跟随主任务栏，副屏任务栏不显示；第三方任务栏美化工具可能导致自动文字色与实际背景不符

**部署**

- 安装包与主程序都没有做代码签名，首次运行可能触发 SmartScreen 的“未知发布者”提示
- 官方验收环境无法应答 UAC，因此 `%ProgramFiles%` 的全用户安装分支没有实测，只实测了 `/CURRENTUSER` 分支
- 非管理员安装或卸载时，卸载只清理当前用户的 Run 自启值，其他用户的残留值不会删除
- 覆盖升级依赖固定的 AppId，改动 AppId 会被识别成另一个产品，出现两套并存

**界面**

- 任务栏面板完全点击穿透，因此它的文字区域不能右键，所有开关都在悬浮球菜单、托盘菜单与设置窗口里
- 设置窗口不支持滚动，极低分辨率或极高缩放时中间控件可能被裁剪（标题栏与底部按钮始终可见）
- 托盘悬停提示受系统 128 字符上限约束，超长会被截断；网速基于网卡累计计数差分，计数器异常回绕时该采样区间会被丢弃

## 文档
`docs/` 下的文档（随包分发的用户使用说明另见 `assets/installer/README.txt`）：

| 文件 | 说明 |
|---|---|
| `交付说明.md` | v1.0.0 交付与验收档案：构建、打包、验收表、资源占用、已知限制 |
| `v1.1.0-变更与升级说明.md` | 任务栏透明文字面板与安装包覆盖升级 |
| `v1.2.0-变更与升级说明.md` | 统一排版模型、CPU 与 GPU 负载与温度、四种材质 |
| `v1.3.0-变更与升级说明.md` | 设置即时预览与取消回滚、材质与温度源卡片选择、固定尺寸悬浮球 |
| `v1.3.1-界面重做说明.md` | 设置窗口重做：可调整大小、控件分页修复、显卡列表可滚动 |
| `v1.4.2-变更与升级说明.md` | CPU 温度改走 PDH 热区、新增 CPU 与 GPU 功率 |
| `v1.5.0-升级规划.md` | 下一版本升级规划：目标、数据源决策、WBS、验收标准、风险 |

## 许可证
本项目采用 Creative Commons Attribution-NonCommercial 4.0 International（CC BY-NC 4.0）：允许在署名后共享与改编，禁止商业性使用。许可证全文见仓库根目录的 `LICENSE`，官方说明见 https://creativecommons.org/licenses/by-nc/4.0/
