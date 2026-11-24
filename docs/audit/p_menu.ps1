# auditor / T10 - AC-12 context menu enumeration (read-only)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$lines = New-Object System.Collections.ArrayList
function Add-Line($s) { [void]$lines.Add($s); Write-Output $s }

if (-not ('AudMenu' -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public struct M_RECT { public int Left, Top, Right, Bottom; }
public class AudMenu {
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, IntPtr title);
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr h);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr res);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr m);
    [DllImport("user32.dll")] public static extern int GetMenuStringW(IntPtr m, uint item, StringBuilder sb, int max, uint flags);
    [DllImport("user32.dll")] public static extern uint GetMenuState(IntPtr m, uint item, uint flags);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out M_RECT r);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
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
[void][AudMenu]::SetThreadDpiAwarenessContext([IntPtr](-4))

$exe = Join-Path $root 'build\out\NetBall.exe'
$tmpDir = Join-Path $root 'build\_tmp\audit'
$copy = Join-Path $tmpDir 'NetBall_audit.exe'
if (-not (Test-Path $copy)) { New-Item -ItemType Directory -Force -Path $tmpDir | Out-Null; Copy-Item $exe $copy }

$p = Start-Process -FilePath $copy -PassThru
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 100; $i++) {
    Start-Sleep -Milliseconds 50
    $hwnd = [AudMenu]::FindByPid([uint32]$p.Id)
    if ($hwnd -ne [IntPtr]::Zero) { break }
}
Add-Line ("PID={0} HWND=0x{1:X}" -f $p.Id, $hwnd.ToInt64())
Start-Sleep -Milliseconds 800

# right-button up at window centre -> ShowContextMenu -> TrackPopupMenu blocks the ball thread
$wp = [IntPtr]([int64]1)          # MK_LBUTTON irrelevant, just non-zero
$lp = [IntPtr]([int64]((20 -shl 16) -bor 20))
[void][AudMenu]::PostMessageW($hwnd, 0x0205, [IntPtr]::Zero, $lp)   # WM_RBUTTONUP
Add-Line "POSTED_WM_RBUTTONUP"

$menuWnd = [IntPtr]::Zero
for ($i = 0; $i -lt 60; $i++) {
    Start-Sleep -Milliseconds 50
    $menuWnd = [AudMenu]::FindWindowW('#32768', [IntPtr]::Zero)
    if ($menuWnd -ne [IntPtr]::Zero) { break }
}
Add-Line ("MENU_WND=0x{0:X}" -f $menuWnd.ToInt64())
if ($menuWnd -ne [IntPtr]::Zero) {
    $hm = [AudMenu]::GetMenu($menuWnd)
    if ($hm -eq [IntPtr]::Zero) {
        $res = [IntPtr]::Zero
        [void][AudMenu]::SendMessageTimeoutW($menuWnd, 0x01E1, [IntPtr]::Zero, [IntPtr]::Zero, 0x2, 1000, [ref]$res)
        $hm = $res
        Add-Line ("MENU_HANDLE_SRC=MN_GETHMENU")
    } else { Add-Line ("MENU_HANDLE_SRC=GetMenu") }
    Add-Line ("MENU_HANDLE=0x{0:X}" -f $hm.ToInt64())
    if ($hm -ne [IntPtr]::Zero) {
        $cnt = [AudMenu]::GetMenuItemCount($hm)
        Add-Line ("MENU_COUNT={0}" -f $cnt)
        for ($i = 0; $i -lt $cnt; $i++) {
            $sb = New-Object System.Text.StringBuilder 256
            $n = [AudMenu]::GetMenuStringW($hm, [uint32]$i, $sb, 256, 0x400)
            $st = [AudMenu]::GetMenuState($hm, [uint32]$i, 0x400)
            $sep = (($st -band 0x800) -ne 0)
            $chk = (($st -band 0x8) -ne 0)
            $cp = (($sb.ToString().ToCharArray() | ForEach-Object { 'U+{0:X4}' -f [int]$_ }) -join ' ')
            Add-Line ("ITEM[{0}] sep={1} checked={2} text_cp=[{3}]" -f $i, $sep, $chk, $cp)
        }
    }
    [void][AudMenu]::PostMessageW($menuWnd, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero)   # ESC
    Start-Sleep -Milliseconds 300
    Add-Line ("MENU_CLOSED={0}" -f (-not [AudMenu]::IsWindowVisible([AudMenu]::FindWindowW('#32768', [IntPtr]::Zero))))
}

if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
Start-Sleep -Milliseconds 200
Add-Line ("POST_PROC_COUNT={0}" -f @(Get-Process -Name 'NetBall*' -ErrorAction SilentlyContinue).Count)
Add-Line ("POST_SHA={0}" -f (Get-FileHash $exe -Algorithm SHA256).Hash)
$lines | Out-File -Encoding utf8 (Join-Path $PSScriptRoot 'ev_menu.txt')
