# auditor / T10 round2 - D-01 three paths, settings-save entry, cross-DPI reset
param(
    [int]$PosX = -500,
    [int]$PosY = 100,
    [string]$Tag = 'r2pos',
    [switch]$DoSettingsSave,
    [switch]$DoResetPos,
    [switch]$DoDragToOrigin,
    [int]$DragX = -3000,
    [int]$DragY = -3000
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$lines = New-Object System.Collections.ArrayList
function Add-Line($s) { [void]$lines.Add($s); Write-Output $s }

if (-not ('R2W' -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public struct R2_RECT { public int Left, Top, Right, Bottom; }
public class R2W {
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out R2_RECT r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int max);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, IntPtr title);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
  public struct POINT { public int X, Y; }
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
  public static IntPtr ByPidAny(uint pid) {
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
[void][R2W]::SetThreadDpiAwarenessContext([IntPtr](-4))

$exe = Join-Path $root 'build\out\NetBall.exe'
$copy = Join-Path $root 'build\_tmp\audit\NetBall_audit.exe'
if (-not (Test-Path $copy) -or ((Get-FileHash $copy -Algorithm SHA256).Hash -ne (Get-FileHash $exe -Algorithm SHA256).Hash)) {
    New-Item -ItemType Directory -Force -Path (Split-Path $copy -Parent) | Out-Null
    Copy-Item $exe $copy -Force
}
Add-Line ("TAG={0}" -f $Tag)
Add-Line ("SHA_SRC={0}" -f (Get-FileHash $exe -Algorithm SHA256).Hash)
Add-Line ("SHA_COPY={0}" -f (Get-FileHash $copy -Algorithm SHA256).Hash)
Add-Line ("VSCREEN vx={0} vy={1} vw={2} vh={3} primary={4}x{5}" -f [R2W]::GetSystemMetrics(76), [R2W]::GetSystemMetrics(77), [R2W]::GetSystemMetrics(78), [R2W]::GetSystemMetrics(79), [R2W]::GetSystemMetrics(0), [R2W]::GetSystemMetrics(1))

$cfg = Join-Path $env:APPDATA 'NetBall\config.ini'
$ini = @('[NetBall]','shape=0','size=88','opacity=85','textauto=1','textcolor=#FFFFFF',
         'shownet=1','showmem=1','bgcolor=#181C24','refreshms=1000','alwaysontop=1','autostart=0',
         ('posx=' + $PosX),('posy=' + $PosY),'savedw=5760','savedh=2160')
[System.IO.File]::WriteAllText($cfg, ($ini -join "`r`n") + "`r`n", (New-Object System.Text.ASCIIEncoding))
Add-Line ("CFG posx={0} posy={1}" -f $PosX, $PosY)

$proc = @(Get-Process -Name 'NetBall*' -ErrorAction SilentlyContinue)
Add-Line ("PRE_PROC={0}" -f $proc.Count)
$p = Start-Process -FilePath $copy -PassThru
$ball = [IntPtr]::Zero
for ($i = 0; $i -lt 100; $i++) { Start-Sleep -Milliseconds 50; $ball = [R2W]::ByPidClass([uint32]$p.Id, 'NetBallBallWnd'); if ($ball -ne [IntPtr]::Zero) { break } }
Add-Line ("BALL=0x{0:X}" -f $ball.ToInt64())
Start-Sleep -Milliseconds 900

$r = New-Object R2_RECT
[void][R2W]::GetWindowRect($ball, [ref]$r)
Add-Line ("STEP1_LAUNCH rect={0},{1} {2}x{3} dpi={4} expect={5},{6}" -f $r.Left, $r.Top, ($r.Right-$r.Left), ($r.Bottom-$r.Top), [R2W]::GetDpiForWindow($ball), $PosX, $PosY)

if ($DoSettingsSave) {
    # WM_COMMAND(IDM_SETTINGS=1001) -> open settings
    [void][R2W]::PostMessageW($ball, 0x0111, [IntPtr]1001, [IntPtr]::Zero)
    $dlg = [IntPtr]::Zero
    for ($i = 0; $i -lt 60; $i++) { Start-Sleep -Milliseconds 50; $dlg = [R2W]::ByPidClass([uint32]$p.Id, 'NetBallSettingsWnd'); if ($dlg -ne [IntPtr]::Zero) { break } }
    Add-Line ("SETTINGS_HWND=0x{0:X}" -f $dlg.ToInt64())
    if ($dlg -ne [IntPtr]::Zero) {
        Start-Sleep -Milliseconds 500
        # WM_COMMAND(BN_CLICKED=0, IDC_SAVE=3016) -> ApplyAndClose
        [void][R2W]::PostMessageW($dlg, 0x0111, [IntPtr]3016, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 1200
        [void][R2W]::GetWindowRect($ball, [ref]$r)
        Add-Line ("STEP2_AFTER_SAVE rect={0},{1} {2}x{3} dpi={4}" -f $r.Left, $r.Top, ($r.Right-$r.Left), ($r.Bottom-$r.Top), [R2W]::GetDpiForWindow($ball))
        Add-Line ("STEP2_DLG_GONE={0}" -f ([R2W]::ByPidClass([uint32]$p.Id, 'NetBallSettingsWnd') -eq [IntPtr]::Zero))
    } else {
        Add-Line "STEP2_SKIP settings window not found"
    }
}

if ($DoDragToOrigin) {
    $cur = New-Object R2W+POINT
    [void][R2W]::GetCursorPos([ref]$cur)
    Add-Line ("CURSOR_BEFORE={0},{1}" -f $cur.X, $cur.Y)
    [void][R2W]::GetWindowRect($ball, [ref]$r)
    $cx = $r.Left + [int](($r.Right - $r.Left) / 2)
    $cy = $r.Top + [int](($r.Bottom - $r.Top) / 2)
    [void][R2W]::SetCursorPos($cx, $cy)
    Start-Sleep -Milliseconds 150
    [void][R2W]::PostMessageW($ball, 0x0201, [IntPtr]1, [IntPtr]::Zero)     # WM_LBUTTONDOWN
    Start-Sleep -Milliseconds 150
    [void][R2W]::SetCursorPos($DragX, $DragY)                              # far outside virtual screen
    Start-Sleep -Milliseconds 150
    [void][R2W]::PostMessageW($ball, 0x0200, [IntPtr]1, [IntPtr]::Zero)    # WM_MOUSEMOVE
    Start-Sleep -Milliseconds 250
    [void][R2W]::GetWindowRect($ball, [ref]$r)
    Add-Line ("DRAG_MID rect={0},{1} {2}x{3}" -f $r.Left, $r.Top, ($r.Right-$r.Left), ($r.Bottom-$r.Top))
    [void][R2W]::PostMessageW($ball, 0x0202, [IntPtr]0, [IntPtr]::Zero)    # WM_LBUTTONUP
    Start-Sleep -Milliseconds 500
    [void][R2W]::GetWindowRect($ball, [ref]$r)
    $vx = [R2W]::GetSystemMetrics(76); $vy = [R2W]::GetSystemMetrics(77)
    $vw = [R2W]::GetSystemMetrics(78); $vh = [R2W]::GetSystemMetrics(79)
    $inside = ($r.Left -ge $vx) -and ($r.Top -ge $vy) -and ($r.Right -le $vx + $vw) -and ($r.Bottom -le $vy + $vh)
    Add-Line ("DRAG_END rect={0},{1} {2}x{3} fully_inside={4}" -f $r.Left, $r.Top, ($r.Right-$r.Left), ($r.Bottom-$r.Top), $inside)
    [void][R2W]::SetCursorPos($cur.X, $cur.Y)
}

if ($DoResetPos) {
    [void][R2W]::GetWindowRect($ball, [ref]$r)
    Add-Line ("PRE_RESET rect={0},{1} {2}x{3} dpi={4}" -f $r.Left, $r.Top, ($r.Right-$r.Left), ($r.Bottom-$r.Top), [R2W]::GetDpiForWindow($ball))
    [void][R2W]::PostMessageW($ball, 0x0111, [IntPtr]1004, [IntPtr]::Zero)   # IDM_RESETPOS
    Start-Sleep -Milliseconds 1200
    [void][R2W]::GetWindowRect($ball, [ref]$r)
    $vw = [R2W]::GetSystemMetrics(78); $vx = [R2W]::GetSystemMetrics(76)
    Add-Line ("POST_RESET rect={0},{1} {2}x{3} dpi={4}" -f $r.Left, $r.Top, ($r.Right-$r.Left), ($r.Bottom-$r.Top), [R2W]::GetDpiForWindow($ball))
    Add-Line ("POST_RESET overhang_right={0} visible_ratio={1:N3}" -f ($r.Right - ($vx + $vw)), (([math]::Min($r.Right, $vx+$vw) - $r.Left) / [double]($r.Right - $r.Left)))
}

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 200
Add-Line ("POST_PROC={0}" -f @(Get-Process -Name 'NetBall*' -ErrorAction SilentlyContinue).Count)
Add-Line ("POST_SHA={0}" -f (Get-FileHash $exe -Algorithm SHA256).Hash)
$lines | Out-File -Encoding utf8 -Append (Join-Path $PSScriptRoot 'ev_r2_pos.txt')
