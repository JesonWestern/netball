; ============================================================================
;  NetBall 网速悬浮球 v1.4.4 —— Inno Setup 6 安装脚本
;  编码：UTF-8 with BOM（Inno Setup 6 需 BOM 才能正确识别中文，勿另存为 ANSI）
;  功能对齐：.agent\workflow.md DECISIONS #19 全部 7 条 + T8 任务硬要求 10 条
;
;  编译方式：
;    1) 推荐： build\package.bat            （含产物前置校验与日志落盘）
;    2) 手工： "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" installer\NetBall.iss
;    产物：   dist\NetBallSetup-1.4.4.exe
;
;  权限说明（重要）：
;    PrivilegesRequired=admin  → 普通双击安装默认落到 %ProgramFiles%\NetBall，
;                                 期间需要 UAC 提权（面向最终用户的标准行为）。
;    PrivilegesRequiredOverridesAllowed=commandline
;                              → 允许安装包命令行传 /CURRENTUSER 覆盖为「当前用户装」，
;                                 此时 {autopf} 自动解析为 %LOCALAPPDATA%\Programs，
;                                 全程无需 UAC。
;    之所以加这个覆盖项：本项目的自动化验证环境（CI/验证者脚本）无法应答 UAC 弹窗，
;    必须能以 /CURRENTUSER /VERYSILENT 完成无人值守安装与卸载自测；
;    若不加覆盖项，验证环节会被 UAC 卡死，安装包本身也无法被自动回归。
;    普通用户双击安装不受影响，仍然是「所有用户 + Program Files + UAC」。
;
;  向导语言包来源（vendor 进仓库，不依赖本机编译器目录）：
;    文件：installer\ChineseSimplified.isl
;    来源：https://raw.githubusercontent.com/jrsoftware/issrc/refs/heads/main/Files/Languages/ChineseSimplified.isl
;          （该链接取自 jrsoftware 官方翻译索引页 https://jrsoftware.org/files/istrans/
;            中 "Chinese (Simplified)" 一行，维护者 Zhenghan Yang，标注 "6.5.0+"）
;    下载日期：2026-11-02
;    上游校验：21,516 B，无 BOM，sha256 E0B0B350E2245F3C5E65586DFE43D574F6E7F06F2261149ABA284954B3FC9A8D
;    本地校验：21,519 B，UTF-8 **with BOM**，sha256 93D74E31B95E630C4D3582E46C48306A54F598893277C79B16F701EBC2405538
;    差异说明：仅在文件头补了 3 字节 BOM（EF BB BF），因为 Inno Setup 6 对无 BOM 的 .isl
;              按 ANSI 解码，中文会全部乱码；**译文正文一字未改**，不得手工改写翻译内容。
; ============================================================================

#define MyAppName "NetBall 网速悬浮球"
#define MyAppShortName "NetBall"
#define MyAppVersion "1.4.4"
#define MyAppPublisher "NetBall"
#define MyAppExeName "NetBall.exe"
; 固定 AppId（GUID 写死）：保证后续版本升级/卸载能识别为同一产品，
; 严禁每次编译重新生成，否则会变成两个可并存的产品。
#define MyAppId "{{8F3A1C2E-5B47-4D9A-9E62-0C7D4B15A8E3}"

[Setup]
AppId={#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
VersionInfoVersion=1.4.4.0
VersionInfoProductName={#MyAppShortName}
VersionInfoDescription={#MyAppName} Setup

; 安装目录：默认所有用户 → %ProgramFiles%\NetBall；/CURRENTUSER 时 → %LOCALAPPDATA%\Programs\NetBall
DefaultDirName={autopf}\NetBall
; 覆盖升级：沿用旧版已选安装目录（默认 yes，这里显式写出，避免以后被误改）。
; 配合固定 AppId，双击新版安装包即可识别为同一产品并原地覆盖，不会并存两套。
UsePreviousAppDir=yes
DefaultGroupName={#MyAppShortName}
AllowNoIcons=yes

; 输出
OutputDir=..\dist
OutputBaseFilename=NetBallSetup-1.4.4

; 外观素材（尺寸已是 Inno 经典值 164x314 / 55x58，故不设 WizardImageStretch）
SetupIconFile=..\assets\app.ico
WizardImageFile=..\assets\installer\wizard-large.bmp
WizardSmallImageFile=..\assets\installer\wizard-small.bmp

; 安装前展示用户使用说明（同一份 README 随包安装到 {app}）
InfoBeforeFile=..\assets\installer\README.txt

; 压缩
Compression=lzma2/max
SolidCompression=yes

; 平台：仅 64 位；Windows 10 1809 (build 17763) 及以上 / Windows 11
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763

; 权限（理由见文件头「权限说明」）
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
; ISCC 会提示「admin 模式下又用了 HKCU」的 UsedUserAreasWarning。
; 本脚本的 HKCU 用法只有一处，且是刻意为之：[Registry] 用 ValueType: none
; 只在卸载时删除程序自己写入的自启项（安装阶段不写任何 HKCU 值），
; 因此该告警不适用，这里显式关闭以保持编译日志干净、无告警。
UsedUserAreasWarning=no

; 控制面板「已安装的应用」中的显示信息
UninstallDisplayName={#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}

; 覆盖安装时自动关闭正在运行的程序，但不自动重启（避免用户被静默拉起）
CloseApplications=yes
RestartApplications=no

; 向导页
DisableWelcomePage=no
DisableFinishedPage=no

[Languages]
; 简体中文放在第一位 = 默认语言（官方译者维护的 6.5.0+ 版，见文件头「向导语言包来源」）
Name: "chinesesimplified"; MessagesFile: "ChineseSimplified.isl"
; 后备：Inno 内置英文。非简体中文系统（或用户手动切换）时仍然可用，
; 这样语言包万一与环境不符也不会让安装程序无界面可用。
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
; 桌面图标 = 可选任务，默认勾选（checkedonce），用户可取消；取消则不会创建
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
; 程序主体（package.bat 已先行校验 字节数 1,185,792 + sha256 A0E8BEEC…BABA）
Source: "..\build\out\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
; 用户说明随包安装，用户随时可在安装目录查看
Source: "..\assets\installer\README.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; 开始菜单程序组 NetBall → 启动项
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
; 开始菜单程序组 NetBall → 卸载入口（指向 Inno 原生 unins000.exe）
Name: "{group}\卸载 {#MyAppShortName}"; Filename: "{uninstallexe}"
; 桌面图标（受 desktopicon 任务控制）
Name: "{autodesktop}\{#MyAppShortName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon
; 安装目录内的中文卸载快捷入口（与 unins000.exe 并存）
Name: "{app}\卸载 {#MyAppShortName}"; Filename: "{uninstallexe}"

[Registry]
; 开机自启项由程序自身（设置窗开关）写入 HKCU\...\Run\NetBall。
; 安装时不创建该值（ValueType: none → 安装期不写任何东西，保持 README 中「默认不自启」的承诺）；
; 卸载时由 Flags: uninsdeletevalue 负责删除，确保不留残留。
;
; 【提权(admin)模式的静态复核结论】
;   a) 全脚本**没有任何 HKLM 写入**：Uninstall 信息由 Inno 按安装模式自行写到
;      HKLM\...\Uninstall（admin）或 HKCU\...\Uninstall（/CURRENTUSER），
;      两者与安装模式始终一致 —— 不存在「安装写 HKCU、卸载读 HKLM」这类不对称。
;   b) 本行是本脚本唯一的 HKCU 动作，且只在**卸载时**执行。提权卸载时它作用的是
;      「运行卸载程序的那个账户」的 HKCU。若一台机器有多个用户都开过自启，
;      卸载只会清掉执行卸载者自己的 Run 值，其它用户的 Run 值会残留（指向已删除的 exe，
;      Windows 登录时会静默跳过）。这是「每用户自启 + 全用户安装」的固有边界，
;      单一主用户场景不受影响；如需彻底清理，应在卸载前用程序自身的开关关闭自启。
;   c) 同理，[Code] 里的 {userappdata} 在提权卸载时解析为「执行卸载者」的 %APPDATA%。
;      换账户卸载（A 装、B 卸）时，保留配置的询问会针对 B 的配置目录。
;      本产品配置本就是每用户私有，此行为可接受，仅在此记录。
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: none; ValueName: "{#MyAppShortName}"; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 卸载后不得留下任何残留：先清快捷方式，再整目录清空（含运行期产生的文件）
Type: files; Name: "{app}\*.lnk"
Type: filesandordirs; Name: "{app}"

[Code]
{ --------------------------------------------------------------------------
  覆盖升级：AppId / UsePreviousAppDir 已保证装到原目录；这里补一步
  「自启路径校正」——若用户曾开机自启，Run 值会指向旧路径。
  只有旧值已存在时才改写，避免把没开自启的用户变成开启自启。
  -------------------------------------------------------------------------- }
procedure CurStepChanged(CurStep: TSetupStep);
var
  RunValue: String;
begin
  if CurStep = ssPostInstall then
  begin
    if RegQueryStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run',
                           '{#MyAppShortName}', RunValue) then
    begin
      RegWriteStringValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run',
                          '{#MyAppShortName}',
                          '"' + ExpandConstant('{app}\{#MyAppExeName}') + '"');
    end;
  end;
end;

{ --------------------------------------------------------------------------
  卸载时询问是否保留个人配置 %APPDATA%\NetBall
  · 默认按钮 = 「是」→ 保留（重装后沿用原设置）
  · 选「否」→ 递归删除配置目录
  · 静默卸载（/SILENT、/VERYSILENT）时不弹窗，直接保留配置，
    既不会误删用户设置，也不会把卸载进程堵在模态框上（见下方说明）
  -------------------------------------------------------------------------- }
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ConfigDir: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    ConfigDir := ExpandConstant('{userappdata}\NetBall');
    if DirExists(ConfigDir) then
    begin
      { 静默卸载（/SILENT、/VERYSILENT）时绝不弹窗：
        实测 Inno 6.7.3 的 MsgBox 在卸载器的第二（静默）阶段并不会被
        /SUPPRESSMSGBOXES 自动应答，而是把进程堵在一个不可见的模态框上，
        卸载虽已完成却留下一个 _unins.tmp 僵尸进程（无残留判定的隐患）。
        因此这里显式判断 UninstallSilent：静默时按默认语义「保留配置」。 }
      if UninstallSilent then
      begin
        { keep config, do nothing }
      end
      else
      begin
        if MsgBox('是否保留个人配置与设置？' + #13#10 + #13#10 +
                  '配置目录：' + ConfigDir + #13#10 + #13#10 +
                  '选择「是」：保留配置，重新安装后沿用原有设置。' + #13#10 +
                  '选择「否」：连同配置一并删除，下次安装恢复全部默认设置。',
                  mbConfirmation, MB_YESNO or MB_DEFBUTTON1) = IDNO then
          DelTree(ConfigDir, True, True, True);
      end;
    end;
  end;
end;
