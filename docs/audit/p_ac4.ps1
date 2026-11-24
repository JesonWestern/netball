# auditor / T10 - AC-4 independent check: ball text vs GlobalMemoryStatusEx
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$lines = New-Object System.Collections.ArrayList
function Add-Line($s) { [void]$lines.Add($s); Write-Output $s }
Add-Type -AssemblyName System.Drawing

if (-not ('A4' -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public struct A4_RECT { public int Left, Top, Right, Bottom; }
[StructLayout(LayoutKind.Sequential)]
public struct A4_MEM {
  public uint dwLength; public uint dwMemoryLoad;
  public ulong ullTotalPhys; public ulong ullAvailPhys;
  public ulong ullTotalPageFile; public ulong ullAvailPageFile;
  public ulong ullTotalVirtual; public ulong ullAvailVirtual;
  public ulong ullAvailExtendedVirtual;
}
public class A4 {
  [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out A4_RECT r);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lp);
  [DllImport("kernel32.dll")] public static extern bool GlobalMemoryStatusEx(ref A4_MEM m);
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
  public static uint MemLoad() {
     A4_MEM m = new A4_MEM(); m.dwLength = (uint)Marshal.SizeOf(typeof(A4_MEM));
     GlobalMemoryStatusEx(ref m); return m.dwMemoryLoad;
  }
}
"@
}
[void][A4]::SetThreadDpiAwarenessContext([IntPtr](-4))

$exe = Join-Path $root 'build\out\NetBall.exe'
$copy = Join-Path $root 'build\_tmp\audit\NetBall_audit.exe'
if (-not (Test-Path $copy)) { New-Item -ItemType Directory -Force -Path (Split-Path $copy -Parent) | Out-Null; Copy-Item $exe $copy }

$p = Start-Process -FilePath $copy -PassThru
$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 100; $i++) { Start-Sleep -Milliseconds 50; $hwnd = [A4]::FindByPid([uint32]$p.Id); if ($hwnd -ne [IntPtr]::Zero) { break } }
Start-Sleep -Milliseconds 2500
Add-Line ("PID={0} HWND=0x{1:X}" -f $p.Id, $hwnd.ToInt64())

for ($k = 1; $k -le 3; $k++) {
    $pre = [A4]::MemLoad()
    $r = New-Object A4_RECT
    [void][A4]::GetWindowRect($hwnd, [ref]$r)
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $h))
    $g.Dispose()
    $name = "evidence\ac4-$k.png"
    $bmp.Save((Join-Path $PSScriptRoot $name), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $post = [A4]::MemLoad()
    Add-Line ("SAMPLE {0} shot={1} rect={2},{3} {4}x{5} pre={6} post={7}" -f $k, $name, $r.Left, $r.Top, $w, $h, $pre, $post)
    Start-Sleep -Milliseconds 1100
}

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 200
Add-Line ("POST_PROC_COUNT={0}" -f @(Get-Process -Name 'NetBall*' -ErrorAction SilentlyContinue).Count)
Add-Line ("POST_SHA={0}" -f (Get-FileHash $exe -Algorithm SHA256).Hash)
$lines | Out-File -Encoding utf8 (Join-Path $PSScriptRoot 'ev_ac4.txt')
