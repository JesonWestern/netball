<#
  AC-17  DPI / scaling.
  This machine is 175 % (32 * 4 / 96 * ... ), so AC-17 is judged at 175 % and the
  system scaling setting is NEVER modified. If the live scale differs from
  -ExpectedScale the script FAILs and says so (measuring at the wrong scale would
  invalidate the evidence).

  Checks:
    1. effective scale == ExpectedScale (default 175)
    2. process is DPI aware (not DPI_AWARENESS_UNAWARE) and window dpi tracks system dpi
    3. at 175 %, capture the ball at min size (48) and max size (160):
       glyph pixels must exist and must not touch the window edge (margin >= 1 px)
       -> catches clipping / overflow automatically; blur stays a human judgement
  Screenshots: docs/verify/dpi-175.png, dpi-175-size48.png, dpi-175-size160.png
  Exit: 0 PASS / 1 FAIL / 2 SKIP
#>
[CmdletBinding()]
param(
    [string]$ClassName = 'NetBallBallWnd',
    [string]$ProcessName = 'NetBall',
    [string]$ExePath,
    [int]$ExpectedScale = 175,
    [int[]]$Sizes = @(48, 160),
    [switch]$SizeSweep
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $ExePath) { $ExePath = Join-Path $root 'build\out\NetBall.exe' }
$ExePath = [System.IO.Path]::GetFullPath($ExePath)
$iniPath = Join-Path $env:APPDATA 'NetBall\config.ini'

$cs = @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class NBDpi {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr hWnd, StringBuilder sb, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern uint GetDpiForSystem();
    [DllImport("user32.dll")] public static extern IntPtr GetWindowDpiAwarenessContext(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern int GetAwarenessFromDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromPoint(POINT pt, uint flags);
    [DllImport("shcore.dll")] public static extern int GetDpiForMonitor(IntPtr hmon, int dpiType, out uint dpiX, out uint dpiY);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }

    // GetDpiForSystem is DPI-context sensitive: an unaware host (powershell.exe) sees 96.
    // Force PMv2 on this thread, then ask the monitor for its effective DPI.
    public static uint MakePerMonitorV2() {
        try { return (uint)SetThreadDpiAwarenessContext(new IntPtr(-4)).ToInt64(); } catch { return 0; }
    }
    public static uint SystemDpiAware() {
        try { return GetDpiForSystem(); } catch { return 0; }
    }
    public static uint MonitorDpi() {
        try {
            IntPtr m = MonitorFromPoint(new POINT { X = 1, Y = 1 }, 2); // MONITOR_DEFAULTTOPRIMARY
            uint x = 0, y = 0;
            if (GetDpiForMonitor(m, 0, out x, out y) == 0 && x > 0) { return x; }  // MDT_EFFECTIVE_DPI
        } catch { }
        return 0;
    }
    public static IntPtr FindByClass(string cls) {
        IntPtr hit = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            var sb = new StringBuilder(512);
            if (GetClassNameW(h, sb, sb.Capacity) > 0 && string.Equals(sb.ToString(), cls, StringComparison.Ordinal) && IsWindowVisible(h)) {
                hit = h; return false;
            }
            return true;
        }, IntPtr.Zero);
        return hit;
    }
}
'@
try { Add-Type -TypeDefinition $cs -ErrorAction Stop } catch {
    if ($_.Exception.Message -notmatch 'already exists') { throw }
}

function Get-SystemScale {
    $raw = 0; $awareDpi = 0; $monDpi = 0; $logPx = 0
    try { $raw = [int][NBDpi]::SystemDpiAware() } catch { }          # before switching context
    try { [void][NBDpi]::MakePerMonitorV2() } catch { }
    try { $awareDpi = [int][NBDpi]::SystemDpiAware() } catch { }     # after switching context
    try { $monDpi = [int][NBDpi]::MonitorDpi() } catch { }
    try { $logPx = [int](Get-ItemProperty -Path 'HKCU:\Control Panel\Desktop' -Name 'LogPixels' -ErrorAction SilentlyContinue).LogPixels } catch { }

    $dpi = 0
    if ($monDpi -gt 0) { $dpi = $monDpi }
    elseif ($awareDpi -gt 0) { $dpi = $awareDpi }
    elseif ($logPx -gt 0) { $dpi = $logPx }
    else { $dpi = 96 }

    return [pscustomobject]@{
        Dpi = $dpi
        Scale = [int][math]::Round(($dpi / 96.0) * 100)
        RawSystemDpi = $raw
        AwareSystemDpi = $awareDpi
        MonitorDpi = $monDpi
        LogPixels = $logPx
    }
}

function Wait-BallWindow([int]$TimeoutMs = 6000) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        $h = [NBDpi]::FindByClass($ClassName)
        if ($h -ne [IntPtr]::Zero) { return $h }
        Start-Sleep -Milliseconds 200
    }
    return [IntPtr]::Zero
}

function Stop-Ball {
    Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | ForEach-Object {
        try { $_.Kill(); [void]$_.WaitForExit(3000) } catch { }
    }
    Start-Sleep -Milliseconds 400
}

function Set-IniSize([string]$Path, [int]$Size) {
    $t = Get-Content -LiteralPath $Path -Raw
    if ($t -match '(?m)^size=\d+') {
        $t = [regex]::Replace($t, '(?m)^size=\d+', "size=$Size")
    } else {
        $t = $t.TrimEnd() + "`r`nsize=$Size`r`n"
    }
    Set-Content -LiteralPath $Path -Value $t -Encoding ASCII -NoNewline
}

Write-Output "AC-17 DPI / SCALING CHECK (expected $ExpectedScale%)"

$sys = Get-SystemScale
Write-Output ("  LogPixels (HKCU)      : {0}" -f $(if ($sys.LogPixels -gt 0) { "$($sys.LogPixels) dpi" } else { '<not set>' }))
Write-Output ("  GetDpiForSystem(raw)  : {0} dpi   (DPI-unaware host virtualises this to 96)" -f $sys.RawSystemDpi)
Write-Output ("  GetDpiForSystem(PMv2) : {0} dpi" -f $sys.AwareSystemDpi)
Write-Output ("  GetDpiForMonitor      : {0} dpi   <- authoritative" -f $sys.MonitorDpi)
Write-Output ("  effective scale       : {0}%  ({1} dpi)" -f $sys.Scale, $sys.Dpi)

$fail = @()
if ($sys.Scale -ne $ExpectedScale) {
    $fail += "live scale is $($sys.Scale)% but AC-17 must be measured at $ExpectedScale% (do not change system scaling to fit)"
}

# ---------------------------------------------------------------- optional size sweep
if (-not (Test-Path -LiteralPath $ExePath)) {
    Write-Output "  exe               : MISSING ($ExePath)"
    Write-Output "RESULT: SKIP (artifact not built yet; scale recorded above)"
    exit 2
}
if (-not (Test-Path -LiteralPath $iniPath)) {
    Write-Output "  config.ini        : MISSING ($iniPath) - start the app once first"
    Write-Output "RESULT: SKIP (no config.ini to drive the size sweep)"
    exit 2
}

$shots = @()
$backup = Join-Path $PSScriptRoot 'config-ac17-backup.ini'
Copy-Item -LiteralPath $iniPath -Destination $backup -Force
Write-Output ("  config backup     : {0}" -f $backup)

try {
    if ($SizeSweep) {
        foreach ($sz in $Sizes) {
            Write-Output ""
            Write-Output ("  --- size={0} ---" -f $sz)
            Stop-Ball
            Set-IniSize -Path $iniPath -Size $sz
            $curSize = ([regex]::Match((Get-Content -LiteralPath $iniPath -Raw), '(?m)^size=(\d+)')).Groups[1].Value
            Write-Output ("  config size now   : {0}" -f $curSize)
            $p = Start-Process -FilePath $ExePath -WorkingDirectory (Split-Path -Parent $ExePath) -PassThru
            $h = Wait-BallWindow
            if ($h -eq [IntPtr]::Zero) {
                $fail += "size=$sz : ball window never appeared"
                continue
            }
            Start-Sleep -Milliseconds 1200
            $shot = Join-Path $PSScriptRoot ("dpi-{0}-size{1}.png" -f $sys.Scale, $sz)
            $out = & (Join-Path $PSScriptRoot 'check_screenshot.ps1') -OutFile $shot -ClassName $ClassName -ProcessName $ProcessName -ExpectText 2>&1
            foreach ($l in $out) { Write-Output ("    " + $l) }
            $ok = ($out | Select-String -Pattern '^RESULT: PASS').Count -gt 0
            $metric = ($out | Select-String -Pattern 'margin_px=(-?\d+)' | Select-Object -First 1)
            $margin = if ($metric) { [int]$metric.Matches[0].Groups[1].Value } else { -1 }
            $shots += [pscustomobject]@{ Size = $sz; Shot = $shot; Ok = $ok; Margin = $margin }
            if (-not $ok) { $fail += "size=$sz : capture/text check failed (see lines above)" }
            Stop-Ball
        }
    } else {
        Write-Output ""
        Write-Output '  (size sweep skipped; pass -SizeSweep to cover size=48 and size=160)'
    }
} finally {
    Copy-Item -LiteralPath $backup -Destination $iniPath -Force
    Write-Output ""
    Write-Output ("  config restored   : {0}" -f $iniPath)
    Stop-Ball
}

# ---------------------------------------------------------------- single window / awareness
$p2 = Start-Process -FilePath $ExePath -WorkingDirectory (Split-Path -Parent $ExePath) -PassThru
$h2 = Wait-BallWindow
if ($h2 -eq [IntPtr]::Zero) {
    Write-Output "RESULT: FAIL (ball window did not appear even though the exe exists)"
    exit 1
}
Start-Sleep -Milliseconds 1200

$winDpi = [int][NBDpi]::GetDpiForWindow($h2)
$winScale = [int][math]::Round(($winDpi / 96.0) * 100)
$aware = [NBDpi]::GetAwarenessFromDpiAwarenessContext([NBDpi]::GetWindowDpiAwarenessContext($h2))
$awareName = switch ($aware) {
    0 { 'UNAWARE (0) -> OS stretches the bitmap, text blurs' }
    1 { 'SYSTEM_AWARE (1)' }
    2 { 'PER_MONITOR_AWARE (2)' }
    default { "unknown ($aware)" }
}
Write-Output ""
Write-Output ("  window hwnd       : 0x{0:X}" -f [int64]$h2)
Write-Output ("  GetDpiForWindow   : {0} dpi -> {1}%" -f $winDpi, $winScale)
Write-Output ("  DPI awareness     : {0}" -f $awareName)

if ($aware -eq 0) { $fail += 'process is DPI-unaware -> layered window gets bitmap-stretched at 175%' }
if ([math]::Abs($winDpi - $sys.Dpi) -gt 8) {
    $fail += "window dpi $winDpi != system dpi $($sys.Dpi) (per-monitor handling mismatch)"
}

$mainShot = Join-Path $PSScriptRoot ("dpi-{0}.png" -f $sys.Scale)
$outMain = & (Join-Path $PSScriptRoot 'check_screenshot.ps1') -OutFile $mainShot -ClassName $ClassName -ProcessName $ProcessName -ExpectText 2>&1
foreach ($l in $outMain) { Write-Output ("    " + $l) }
$mainOk = ($outMain | Select-String -Pattern '^RESULT: PASS').Count -gt 0
if (-not $mainOk) { $fail += 'main window capture / text check failed' }
else {
    $mm = ($outMain | Select-String -Pattern 'margin_px=(-?\d+)' | Select-Object -First 1)
    if ($mm) { $shots += [pscustomobject]@{ Size = 'current'; Shot = $mainShot; Ok = $true; Margin = [int]$mm.Matches[0].Groups[1].Value } }
}
Stop-Ball

Write-Output ""
Write-Output '  screenshots for human read (blur judgement cannot be automated):'
foreach ($s in $shots) {
    Write-Output ("    size={0,-8} margin={1,3}px  {2}" -f $s.Size, $s.Margin, $s.Shot)
}
Write-Output '    - glyph pixels must not touch the edge (margin >= 1 px, checked automatically)'
Write-Output '    - strokes must look crisp, not doubled/smeared (human)'

$marginList = ($shots | ForEach-Object { "size$($_.Size)=$($_.Margin)" }) -join ','
if ($fail.Count -eq 0) {
    Write-Output ("METRIC: system_dpi={0} window_dpi={1} scale_pct={2} awareness={3} margins={4}" -f $sys.Dpi, $winDpi, $sys.Scale, $aware, $marginList)
    Write-Output "RESULT: PASS (DPI aware at $($sys.Scale)%, no glyph clipped at any tested size)"
    exit 0
} else {
    foreach ($f in $fail) { Write-Output "  VIOLATION: $f" }
    Write-Output ("METRIC: system_dpi={0} window_dpi={1} scale_pct={2} awareness={3} margins={4}" -f $sys.Dpi, $winDpi, $sys.Scale, $aware, $marginList)
    Write-Output 'RESULT: FAIL'
    exit 1
}
