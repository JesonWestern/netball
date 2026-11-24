# verifier-4: independent DPI / geometry probe for the ball window.
# Starts the private copy, measures, then kills it by PID only.
$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$exe  = Join-Path $root 'build\_tmp\v4\NetBall_v4.exe'

$cs = @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class V4Dpi {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("shcore.dll")] public static extern int GetDpiForMonitor(IntPtr hm, int type, out uint x, out uint y);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromWindow(IntPtr h, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public static IntPtr Find(string cls) {
        IntPtr hit = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            var sb = new StringBuilder(256);
            if (GetClassNameW(h, sb, sb.Capacity) > 0 && sb.ToString() == cls && IsWindowVisible(h)) { hit = h; return false; }
            return true;
        }, IntPtr.Zero);
        return hit;
    }
}
'@
try { Add-Type -TypeDefinition $cs -ErrorAction Stop } catch { if ($_.Exception.Message -notmatch 'already exists') { throw } }

$procs = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like 'NetBall*' }
if ($procs) {
    Write-Output ("ABORT: another NetBall instance holds the mutex: " + (($procs | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ', '))
    exit 2
}

$p = Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -PassThru
Write-Output ("STARTED pid={0}" -f $p.Id)
Start-Sleep -Seconds 4

$h = [V4Dpi]::Find('NetBallBallWnd')
if ($h -eq [IntPtr]::Zero) {
    Write-Output 'NO BALL WINDOW (class NetBallBallWnd not visible)'
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    exit 1
}

$r = New-Object 'V4Dpi+RECT'
[void][V4Dpi]::GetWindowRect($h, [ref]$r)
$w = $r.Right - $r.Left
$hh = $r.Bottom - $r.Top
$winDpi = [V4Dpi]::GetDpiForWindow($h)
$mon = [V4Dpi]::MonitorFromWindow($h, 2)
$sx = 0; $sy = 0
$rcm = [V4Dpi]::GetDpiForMonitor($mon, 0, [ref]$sx, [ref]$sy)

Write-Output ("hwnd=0x{0:X}" -f [int64]$h)
Write-Output ("rect L={0} T={1} R={2} B={3}" -f $r.Left, $r.Top, $r.Right, $r.Bottom)
Write-Output ("window_size={0}x{1}" -f $w, $hh)
Write-Output ("GetDpiForWindow={0}" -f $winDpi)
Write-Output ("GetDpiForMonitor(effective)={0}x{1} rc={2}" -f $sx, $sy, $rcm)
$scale = if ($winDpi -gt 0) { $winDpi / 96.0 } else { 1.0 }
Write-Output ("scale={0:N2}" -f $scale)
Write-Output ("expected_px_for_size_dip: if config size=88 -> {0:N0} px at this scale" -f (88 * $scale))
Write-Output ("observed/expected = {0:N3}" -f ($w / (88 * $scale)))

# config in force
$cfg = Join-Path $env:APPDATA 'NetBall\config.ini'
if (Test-Path -LiteralPath $cfg) {
    Write-Output '--- config in force ---'
    Get-Content -LiteralPath $cfg -Encoding UTF8 | ForEach-Object { Write-Output ("  | " + $_) }
}

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Write-Output ("STOPPED pid={0} (by PID only)" -f $p.Id)
