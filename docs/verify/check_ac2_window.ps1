<#
  AC-2  Layered / topmost / tool-window, no taskbar button, print window rect.
  Finds the top-level window whose class name equals -ClassName.
  Exit: 0 PASS / 1 FAIL / 2 SKIP
#>
[CmdletBinding()]
param(
    [string]$ClassName = 'NetBallBallWnd',
    [string]$ProcessName = 'NetBall',
    [string]$ExePath
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

if (-not $ExePath) {
    $root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $ExePath = Join-Path $root 'build\out\NetBall.exe'
}
. (Join-Path $PSScriptRoot 'verifier_preflight.ps1')
Write-BuildInfo -Exe $ExePath

$WS_EX_LAYERED    = 0x00080000
$WS_EX_TOPMOST    = 0x00000008
$WS_EX_TOOLWINDOW = 0x00000080
$WS_EX_APPWINDOW  = 0x00040000
$GWL_EXSTYLE      = -20
$GW_OWNER         = 4

$cs = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public class NBWin {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);

    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern int GetClassNameW(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll", SetLastError=true)]
    public static extern IntPtr GetWindowLongPtrW(IntPtr hWnd, int nIndex);

    [DllImport("user32.dll", SetLastError=true)]
    public static extern IntPtr GetWindow(IntPtr hWnd, uint uCmd);

    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError=true)]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }

    public static List<IntPtr> FindByClass(string cls) {
        var found = new List<IntPtr>();
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            var sb = new StringBuilder(512);
            int n = GetClassNameW(h, sb, sb.Capacity);
            if (n > 0 && string.Equals(sb.ToString(), cls, StringComparison.Ordinal)) found.Add(h);
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

try { Add-Type -TypeDefinition $cs -ErrorAction Stop } catch {
    if ($_.Exception.Message -notmatch 'already exists') { throw }
}

Write-Output "AC-2 WINDOW STYLE CHECK"
Write-Output "  expect class : $ClassName"
Write-Output "  process name : $ProcessName"

$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue

if (-not $proc) {
    if (Test-Path -LiteralPath $ExePath) {
        Write-Output "  process      : NOT RUNNING (exe exists but not started)"
        Write-Output "RESULT: SKIP (start build/out/NetBall.exe first, then re-run)"
    } else {
        Write-Output "  process      : NOT RUNNING (exe not built yet)"
        Write-Output "RESULT: SKIP (artifact build/out/NetBall.exe missing)"
    }
    exit 2
}

Write-Output ("  process      : PID {0} ({1} instance(s))" -f (($proc | ForEach-Object { $_.Id }) -join ','), $proc.Count)

$handles = [NBWin]::FindByClass($ClassName)
if ($handles.Count -eq 0) {
    Write-Output "  window       : NOT FOUND for class '$ClassName'"
    $fail = @("window with class '$ClassName' not found (process is running)")
} else {
    $fail = @()
}

$idx = 0
foreach ($h in $handles) {
    $idx++
    $ex = [int64][NBWin]::GetWindowLongPtrW($h, $GWL_EXSTYLE)
    $owner = [NBWin]::GetWindow($h, [uint32]$GW_OWNER)
    $vis = [NBWin]::IsWindowVisible($h)
    $r = $null
    $r = New-Object 'NBWin+RECT'
    $null = [NBWin]::GetWindowRect($h, [ref]$r)
    $w = $r.Right - $r.Left
    $hh = $r.Bottom - $r.Top

    Write-Output ""
    Write-Output "  --- window #$idx ---"
    Write-Output ("  hwnd         : 0x{0:X}" -f [int64]$h)
    Write-Output ("  class        : {0}" -f $ClassName)
    Write-Output ("  exstyle      : 0x{0:X8}" -f $ex)
    Write-Output ("  visible      : {0}" -f $vis)
    Write-Output ("  rect         : L={0} T={1} R={2} B={3}  ({4}x{5})" -f $r.Left, $r.Top, $r.Right, $r.Bottom, $w, $hh)
    Write-Output ("  owner        : 0x{0:X}  ({1})" -f [int64]$owner, $(if ($owner -eq [IntPtr]::Zero) { 'none -> independent top-level' } else { 'owned window' }))

    $layered = ($ex -band $WS_EX_LAYERED) -ne 0
    $topmost = ($ex -band $WS_EX_TOPMOST) -ne 0
    $tool    = ($ex -band $WS_EX_TOOLWINDOW) -ne 0
    $app     = ($ex -band $WS_EX_APPWINDOW) -ne 0

    Write-Output ("  WS_EX_LAYERED    (0x80000) : {0}" -f $layered)
    Write-Output ("  WS_EX_TOPMOST    (0x8)     : {0}" -f $topmost)
    Write-Output ("  WS_EX_TOOLWINDOW (0x80)    : {0}" -f $tool)
    Write-Output ("  WS_EX_APPWINDOW  (0x40000) : {0}" -f $app)
    Write-Output ("  taskbar button   : {0}" -f $(if ((-not $app) -and $tool) { 'NO (APPWINDOW clear + TOOLWINDOW set)' } else { 'POSSIBLY YES' }))

    if (-not $layered)    { $fail += "[#$idx] WS_EX_LAYERED missing" }
    if (-not $topmost)    { $fail += "[#$idx] WS_EX_TOPMOST missing" }
    if (-not $tool)       { $fail += "[#$idx] WS_EX_TOOLWINDOW missing" }
    if ($app)             { $fail += "[#$idx] WS_EX_APPWINDOW set -> taskbar button may appear" }
    if ($w -le 0 -or $hh -le 0) { $fail += "[#$idx] empty window rect ${w}x${hh}" }
    if (-not $vis)        { $fail += "[#$idx] window not visible" }
}

Write-Output ""
if ($fail.Count -eq 0) {
    Write-Output "RESULT: PASS (layered + topmost + toolwindow, no APPWINDOW, $($handles.Count) window(s))"
    exit 0
} else {
    foreach ($f in $fail) { Write-Output "  VIOLATION: $f" }
    Write-Output "RESULT: FAIL"
    exit 1
}

