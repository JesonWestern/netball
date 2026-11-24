# AC-1 (binary metadata) + AC-2 (window style, READ-ONLY)  verifier-4
# ASCII only. Never starts NetBall: AC-1 reads the file, AC-2 inspects whatever
# window already exists (this round that window belongs to verifier-3).
$ErrorActionPreference = 'Continue'
# The project path contains CJK characters and this file has no BOM, so PowerShell
# 5.1 would decode a literal here as ANSI and mangle it. Derive it instead.
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$exe = Join-Path $root 'build\out\NetBall.exe'

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class V4W {
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern IntPtr GetWindowLongPtrW(IntPtr h, int idx);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public static IntPtr FindByClass(string cls) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            StringBuilder sb = new StringBuilder(256);
            GetClassNameW(h, sb, 256);
            if (sb.ToString() == cls) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

# DPI context must be set BEFORE any GetWindowRect
[void][V4W]::SetThreadDpiAwarenessContext([IntPtr](-4))

'=== AC-1 BINARY METADATA ==='
$fi = Get-Item $exe
$sha = (Get-FileHash $exe -Algorithm SHA256).Hash
'AC1 path        = ' + $exe
'AC1 bytes       = ' + $fi.Length
'AC1 mtime       = ' + $fi.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss.fff')
'AC1 sha256      = ' + $sha
'AC1 size_ok     = ' + ($fi.Length -le 2097152)

$vi = $fi.VersionInfo
$desc = [string]$vi.FileDescription
$want = @(0x004E,0x0065,0x0074,0x0042,0x0061,0x006C,0x006C,0x0020,0x7F51,0x901F,0x60AC,0x6D6E,0x7403)
$got = @()
if ($desc) { foreach ($ch in $desc.ToCharArray()) { $got += [int][char]$ch } }
$ok = ($got.Count -eq $want.Count)
if ($ok) {
    for ($i = 0; $i -lt $want.Count; $i++) { if ($got[$i] -ne $want[$i]) { $ok = $false; break } }
}
'AC1 desc_raw    = [' + $desc + ']'
'AC1 desc_len    = ' + $got.Count + ' (want ' + $want.Count + ')'
'AC1 desc_cp     = ' + (($got | ForEach-Object { 'U+{0:X4}' -f $_ }) -join ' ')
'AC1 want_cp     = ' + (($want | ForEach-Object { 'U+{0:X4}' -f $_ }) -join ' ')
'AC1 desc_ok     = ' + $ok
'AC1 FileVersion = ' + $vi.FileVersion
'AC1 ProductName = [' + $vi.ProductName + ']'
'AC1 OriginalFn  = [' + $vi.OriginalFilename + ']'

try {
    Add-Type -AssemblyName System.Drawing
    $ico = [System.Drawing.Icon]::ExtractAssociatedIcon($exe)
    if ($ico) {
        $bmp = $ico.ToBitmap()
        $n = 0; $sum = 0.0; $sum2 = 0.0
        for ($y = 0; $y -lt $bmp.Height; $y += 2) {
            for ($x = 0; $x -lt $bmp.Width; $x += 2) {
                $c = $bmp.GetPixel($x, $y)
                $l = 0.299 * $c.R + 0.587 * $c.G + 0.114 * $c.B
                $sum += $l; $sum2 += $l * $l; $n++
            }
        }
        $mean = $sum / $n
        $var = ($sum2 / $n) - ($mean * $mean)
        'AC1 icon        = ' + $bmp.Width + 'x' + $bmp.Height + ' sampled=' + $n + ' luma_mean=' + [math]::Round($mean,2) + ' variance=' + [math]::Round($var,2)
        'AC1 icon_ok     = ' + ($var -gt 0)
        $bmp.Dispose(); $ico.Dispose()
    } else { 'AC1 icon        = NULL'; 'AC1 icon_ok     = False' }
} catch { 'AC1 icon        = ERROR ' + $_.Exception.Message; 'AC1 icon_ok     = False' }

$bytes = [System.IO.File]::ReadAllBytes($exe)
$txt = [System.Text.Encoding]::UTF8.GetString($bytes)
$m1 = $txt.Contains('asInvoker')
$m2 = $txt.Contains('PerMonitorV2')
'AC1 manifest    = asInvoker=' + $m1 + ' PerMonitorV2=' + $m2
'AC1 manifest_ok = ' + ($m1 -and $m2)

''
'=== AC-2 WINDOW STYLE (read-only, does not start anything) ==='
# verifier-3 owns the running instance in this window; poll read-only for its ball
# window instead of starting one of my own (the mutex and config.ini are global).
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 45; $i++) {
    $h = [V4W]::FindByClass('NetBallBallWnd')
    if ($h -ne [IntPtr]::Zero) { 'AC2 waited      = ' + $i + 's for a ball window'; break }
    Start-Sleep -Seconds 1
}
if ($h -eq [IntPtr]::Zero) {
    'AC2 window      = NOT FOUND (no NetBall window right now)'
    'AC2 verdict     = BLOCKED-NOWND'
} else {
    $ex = [int64][V4W]::GetWindowLongPtrW($h, -20)
    $hex = '0x' + $ex.ToString('X')
    $layered = ($ex -band 0x80000) -ne 0
    $topmost = ($ex -band 0x8) -ne 0
    $tool = ($ex -band 0x80) -ne 0
    $appwin = ($ex -band 0x40000) -ne 0
    'AC2 hwnd        = 0x' + $h.ToString('X')
    'AC2 exstyle     = ' + $hex + ' (' + $ex + ')'
    'AC2 LAYERED     = ' + $layered
    'AC2 TOPMOST     = ' + $topmost
    'AC2 TOOLWINDOW  = ' + $tool
    'AC2 APPWINDOW   = ' + $appwin + '  (must be False)'
    'AC2 visible     = ' + [V4W]::IsWindowVisible($h)
    $r = New-Object V4W+RECT
    $ok2 = [V4W]::GetWindowRect($h, [ref]$r)
    $w = $r.R - $r.L; $hh = $r.B - $r.T
    'AC2 rect        = ' + $r.L + ',' + $r.T + ' ' + $w + 'x' + $hh
    'AC2 rect_ok     = ' + ($ok2 -and $w -gt 0 -and $hh -gt 0)
    $shape = 0
    $ini = Join-Path $env:APPDATA 'NetBall\config.ini'
    if (Test-Path $ini) {
        foreach ($line in (Get-Content $ini -Encoding Default)) {
            if ($line -match '^\s*size\s*=\s*(\d+)') { $shape = [int]$Matches[1] }
        }
    }
    'AC2 size_key    = ' + $shape + ' (from config at read time, may be owned by verifier-3)'
    $dx = 0
    if ($shape -gt 0) { $dx = [math]::Round($shape * 1.75) }
    'AC2 dpi_gate    = size=' + $shape + ' expect ' + $dx + ' got ' + $w + ' -> ' + ($w -eq $dx)
    $style_ok = $layered -and $topmost -and $tool -and (-not $appwin)
    'AC2 style_ok    = ' + $style_ok
}
''
'=== SHA AFTER ==='
$sha2 = (Get-FileHash $exe -Algorithm SHA256).Hash
'AC1 sha_after   = ' + $sha2
'AC1 sha_same    = ' + ($sha2 -eq $sha)
'DONE'
