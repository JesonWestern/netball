# auditor / T10 round2 - AC-12 menu enumeration: ball menu (right click) + tray menu (WM_APP+100)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$lines = New-Object System.Collections.ArrayList
function Add-Line($s) { [void]$lines.Add($s); Write-Output $s }

if (-not ('M2' -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class M2 {
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, IntPtr title);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageTimeoutW(IntPtr h, uint m, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr res);
  [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr m);
  [DllImport("user32.dll")] public static extern uint GetMenuItemID(IntPtr m, int i);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetMenuStringW(IntPtr m, uint item, StringBuilder sb, int max, uint flags);
  [DllImport("user32.dll")] public static extern uint GetMenuState(IntPtr m, uint item, uint flags);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int max);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr lp);
  public static IntPtr ByPidClass(uint pid, string cls) {
     IntPtr found = IntPtr.Zero;
     EnumWindows(delegate(IntPtr h, IntPtr lp) {
        uint p; GetWindowThreadProcessId(h, out p);
        if (p != pid) return true;
        StringBuilder sb = new StringBuilder(128);
        GetClassNameW(h, sb, 128);
        if (sb.ToString() == cls) { found = h; return false; }
        return true;
     }, IntPtr.Zero);
     return found;
  }
}
"@
}
[void][M2]::SetThreadDpiAwarenessContext([IntPtr](-4))

$exe = Join-Path $root 'build\out\NetBall.exe'
$copy = Join-Path $root 'build\_tmp\audit\NetBall_audit.exe'
if (-not (Test-Path $copy) -or ((Get-FileHash $copy -Algorithm SHA256).Hash -ne (Get-FileHash $exe -Algorithm SHA256).Hash)) { Copy-Item $exe $copy -Force }
Add-Line ("SHA_COPY={0}" -f (Get-FileHash $copy -Algorithm SHA256).Hash)

$cfg = Join-Path $env:APPDATA 'NetBall\config.ini'
$ini = @('[NetBall]','shape=0','size=88','opacity=85','textauto=1','textcolor=#FFFFFF','shownet=1','showmem=1','bgcolor=#181C24','refreshms=1000','alwaysontop=1','autostart=0','posx=3000','posy=300','savedw=5760','savedh=2160')
[System.IO.File]::WriteAllText($cfg, ($ini -join "`r`n") + "`r`n", (New-Object System.Text.ASCIIEncoding))

$p = Start-Process -FilePath $copy -PassThru
$ball = [IntPtr]::Zero
for ($i = 0; $i -lt 100; $i++) { Start-Sleep -Milliseconds 50; $ball = [M2]::ByPidClass([uint32]$p.Id, 'NetBallBallWnd'); if ($ball -ne [IntPtr]::Zero) { break } }
Add-Line ("PID={0} BALL=0x{1:X}" -f $p.Id, $ball.ToInt64())
Start-Sleep -Milliseconds 900

function Dump-Menu($label) {
    $mw = [IntPtr]::Zero
    for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 50; $mw = [M2]::FindWindowW('#32768', [IntPtr]::Zero); if ($mw -ne [IntPtr]::Zero) { break } }
    if ($mw -eq [IntPtr]::Zero) { Add-Line ("{0}_MENU_WND=NONE" -f $label); return }
    $res = [IntPtr]::Zero
    [void][M2]::SendMessageTimeoutW($mw, 0x01E1, [IntPtr]::Zero, [IntPtr]::Zero, 0x2, 1500, [ref]$res)
    $hm = $res
    Add-Line ("{0}_HANDLE=0x{1:X}" -f $label, $hm.ToInt64())
    if ($hm -eq [IntPtr]::Zero) { return }
    $cnt = [M2]::GetMenuItemCount($hm)
    Add-Line ("{0}_COUNT={1}" -f $label, $cnt)
    for ($i = 0; $i -lt $cnt; $i++) {
        $sb = New-Object System.Text.StringBuilder 256
        [void][M2]::GetMenuStringW($hm, [uint32]$i, $sb, 256, 0x400)
        $st = [M2]::GetMenuState($hm, [uint32]$i, 0x400)
        $id = [M2]::GetMenuItemID($hm, $i)
        $sep = (($st -band 0x800) -ne 0)
        $chk = (($st -band 0x8) -ne 0)
        $cp = (($sb.ToString().ToCharArray() | ForEach-Object { 'U+{0:X4}' -f [int]$_ }) -join ' ')
        Add-Line ("{0}_ITEM[{1}] id={2} sep={3} checked={4} text_cp=[{5}]" -f $label, $i, $id, $sep, $chk, $cp)
    }
    [void][M2]::PostMessageW($mw, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero)   # ESC
    Start-Sleep -Milliseconds 400
}

# ball context menu: WM_RBUTTONUP
[void][M2]::PostMessageW($ball, 0x0205, [IntPtr]::Zero, [IntPtr]::Zero)
Dump-Menu 'BALL'

# tray menu: NETBALL_TRAY_MSG = WM_APP+100 = 0x8000+100 = 0x8064, lParam LOWORD = WM_RBUTTONUP(0x205)
[void][M2]::PostMessageW($ball, 0x8064, [IntPtr]1, [IntPtr]0x0205)
Dump-Menu 'TRAY'

if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
Start-Sleep -Milliseconds 200
Add-Line ("POST_PROC={0}" -f @(Get-Process -Name 'NetBall*' -ErrorAction SilentlyContinue).Count)
Add-Line ("POST_SHA={0}" -f (Get-FileHash $exe -Algorithm SHA256).Hash)
$lines | Out-File -Encoding utf8 (Join-Path $PSScriptRoot 'ev_r2_menu.txt')
