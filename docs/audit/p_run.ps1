# auditor / T10 - runtime probe (read-only w.r.t. deliverables; touches only %APPDATA%\NetBall and build\_tmp\audit)
param(
    [int]$Shape = 0,
    [int]$Size = 88,
    [int]$Opacity = 85,
    [string]$Bg = '#181C24',
    [int]$PosX = -1,
    [int]$PosY = -1,
    [int]$Hold = 1500,
    [string]$Shot = '',
    [switch]$SecondInstance,
    [switch]$GracefulClose,
    [switch]$SkipConfigWrite,
    [string]$CfgFile = '',
    [string]$Tag = 'run'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$lines = New-Object System.Collections.ArrayList

function Add-Line($s) { [void]$lines.Add($s); Write-Output $s }

Add-Type -AssemblyName System.Drawing

if (-not ('AudWin' -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public struct AUD_RECT { public int Left, Top, Right, Bottom; }
public class AudWin {
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern int GetWindowLongW(IntPtr h, int i);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out AUD_RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int max);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr m);
    [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr hProcess, uint uiFlags);
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr lp);
    public static IntPtr FindByPid(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr lp) {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@
}

[void][AudWin]::SetThreadDpiAwarenessContext([IntPtr](-4))

$exe = Join-Path $root 'build\out\NetBall.exe'
$tmpDir = Join-Path $root 'build\_tmp\audit'
if (-not (Test-Path $tmpDir)) { New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null }
$copy = Join-Path $tmpDir 'NetBall_audit.exe'
$shaSrc = (Get-FileHash $exe -Algorithm SHA256).Hash
if (-not (Test-Path $copy) -or ((Get-FileHash $copy -Algorithm SHA256).Hash -ne $shaSrc)) {
    Copy-Item $exe $copy -Force
}
Add-Line ("TAG={0}" -f $Tag)
Add-Line ("PRE_SHA_SRC={0}" -f $shaSrc)
Add-Line ("PRE_SHA_COPY={0}" -f (Get-FileHash $copy -Algorithm SHA256).Hash)

$cfg = Join-Path $env:APPDATA 'NetBall\config.ini'
$evDir = Join-Path $PSScriptRoot 'evidence'
if (-not (Test-Path $evDir)) { New-Item -ItemType Directory -Path $evDir -Force | Out-Null }

if (-not $SkipConfigWrite -and $CfgFile -ne '') {
    $srcCfg = Join-Path $PSScriptRoot $CfgFile
    Copy-Item $srcCfg $cfg -Force
    Add-Line ("CFG_COPIED_FROM={0} sha={1}" -f $CfgFile, (Get-FileHash $cfg -Algorithm SHA256).Hash)
} elseif (-not $SkipConfigWrite) {
    $cfgDir = Split-Path $cfg -Parent
    if (-not (Test-Path $cfgDir)) { New-Item -ItemType Directory -Path $cfgDir -Force | Out-Null }
    $ini = @(
        '[NetBall]',
        ('shape=' + $Shape),
        ('size=' + $Size),
        ('opacity=' + $Opacity),
        'textauto=1',
        'textcolor=#FFFFFF',
        'shownet=1',
        'showmem=1',
        ('bgcolor=' + $Bg),
        'refreshms=1000',
        'alwaysontop=1',
        'autostart=0',
        ('posx=' + $PosX),
        ('posy=' + $PosY),
        'savedw=5760',
        'savedh=2160'
    )
    [System.IO.File]::WriteAllText($cfg, ($ini -join "`r`n") + "`r`n", (New-Object System.Text.ASCIIEncoding))
    Add-Line ("CFG_WRITTEN shape={0} size={1} opacity={2} bg={3} pos={4},{5}" -f $Shape, $Size, $Opacity, $Bg, $PosX, $PosY)
}

$procs = @(Get-Process -Name 'NetBall*' -ErrorAction SilentlyContinue)
Add-Line ("PRE_PROC_COUNT={0} names={1}" -f $procs.Count, (($procs | ForEach-Object { $_.ProcessName + ':' + $_.Id }) -join ','))

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $copy -PassThru
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 100; $i++) {
    Start-Sleep -Milliseconds 50
    if ($p.HasExited) { break }
    $hwnd = [AudWin]::FindByPid([uint32]$p.Id)
    if ($hwnd -ne [IntPtr]::Zero) { break }
}
Add-Line ("LAUNCH_MS={0}" -f $sw.ElapsedMilliseconds)
Add-Line ("PID={0}" -f $p.Id)
if ($p.HasExited) {
    Add-Line ("EXITED_EARLY exitcode={0}" -f $p.ExitCode)
} else {
    Start-Sleep -Milliseconds $Hold
    if ($hwnd -eq [IntPtr]::Zero) { $hwnd = [AudWin]::FindByPid([uint32]$p.Id) }
    Add-Line ("HWND=0x{0:X}" -f $hwnd.ToInt64())
    if ($hwnd -ne [IntPtr]::Zero) {
        $cls = New-Object System.Text.StringBuilder 128
        [void][AudWin]::GetClassNameW($hwnd, $cls, 128)
        $r = New-Object AUD_RECT
        [void][AudWin]::GetWindowRect($hwnd, [ref]$r)
        $exs = [AudWin]::GetWindowLongW($hwnd, -20)
        $sty = [AudWin]::GetWindowLongW($hwnd, -16)
        $dpi = [AudWin]::GetDpiForWindow($hwnd)
        Add-Line ("CLASS={0}" -f $cls.ToString())
        Add-Line ("STYLE=0x{0:X8} EXSTYLE=0x{1:X8}" -f $sty, $exs)
        Add-Line ("EX_LAYERED={0} EX_TOOLWINDOW={1} EX_TOPMOST={2} EX_APPWINDOW={3}" -f `
            (($exs -band 0x80000) -ne 0), (($exs -band 0x80) -ne 0), (($exs -band 0x8) -ne 0), (($exs -band 0x40000) -ne 0))
        Add-Line ("RECT={0},{1} {2}x{3}" -f $r.Left, $r.Top, ($r.Right - $r.Left), ($r.Bottom - $r.Top))
        Add-Line ("VISIBLE={0} DPI={1}" -f [AudWin]::IsWindowVisible($hwnd), $dpi)
        $p.Refresh()
        $gdi = 0; $usr = 0
        try { $gdi = [AudWin]::GetGuiResources($p.Handle, 0); $usr = [AudWin]::GetGuiResources($p.Handle, 1) } catch { }
        Add-Line ("WS_MB={0:N2} PRIVATE_MB={1:N2} PEAK_MB={2:N2} HANDLES={3} GDI={4} USER={5} THREADS={6} CPU_TOTAL_S={7:N2}" -f ($p.WorkingSet64 / 1MB), ($p.PrivateMemorySize64 / 1MB), ($p.PeakWorkingSet64 / 1MB), $p.HandleCount, $gdi, $usr, $p.Threads.Count, $p.TotalProcessorTime.TotalSeconds)
        if ($Shot -ne '') {
            $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
            $bmp = New-Object System.Drawing.Bitmap $w, $h
            $g = [System.Drawing.Graphics]::FromImage($bmp)
            $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $h))
            $g.Dispose()
            $shotPath = Join-Path $PSScriptRoot $Shot
            $bmp.Save($shotPath, [System.Drawing.Imaging.ImageFormat]::Png)
            $bmp.Dispose()
            Add-Line ("SHOT={0} {1}x{2}" -f $Shot, $w, $h)
        }
    }
}

if ($SecondInstance) {
    $before = @(Get-Process -Name 'NetBall_audit' -ErrorAction SilentlyContinue).Count
    $p2 = Start-Process -FilePath $copy -PassThru
    Start-Sleep -Milliseconds 1500
    $p2.Refresh()
    Add-Line ("SECOND_EXITED={0} EXITCODE={1}" -f $p2.HasExited, $(if ($p2.HasExited) { $p2.ExitCode } else { 'RUNNING' }))
    Add-Line ("COUNT_BEFORE={0} AFTER={1}" -f $before, @(Get-Process -Name 'NetBall_audit' -ErrorAction SilentlyContinue).Count)
    if (-not $p2.HasExited) { Stop-Process -Id $p2.Id -Force -ErrorAction SilentlyContinue }
}

if ($GracefulClose) {
    if ($hwnd -ne [IntPtr]::Zero) { [void][AudWin]::PostMessageW($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep -Milliseconds 50
        $p.Refresh()
        if ($p.HasExited) { break }
    }
    $p.Refresh()
    Add-Line ("GRACEFUL_EXITED={0}" -f $p.HasExited)
}

if (-not $p.HasExited) {
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 200
}
Add-Line ("POST_PROC_COUNT={0}" -f @(Get-Process -Name 'NetBall*' -ErrorAction SilentlyContinue).Count)
Add-Line ("POST_SHA_SRC={0}" -f (Get-FileHash $exe -Algorithm SHA256).Hash)
Add-Line ("POST_CFG_SHA={0}" -f $(if (Test-Path $cfg) { (Get-FileHash $cfg -Algorithm SHA256).Hash } else { 'ABSENT' }))
if (Test-Path $cfg) { Add-Line ("CFG_CONTENT<<<" + ((Get-Content $cfg -Raw) -replace "`r`n", ' | ') + ">>>") }

$lines | Out-File -Encoding utf8 -Append (Join-Path $PSScriptRoot 'ev_runtime.txt')
