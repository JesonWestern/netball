<#
  AC-5  runtime resources.
  Metrics recorded every second:
    WorkingSet64, PrivateMemorySize64, PeakWorkingSet64, HandleCount,
    GDI objects (GetGuiResources GR_GDIOBJECTS=0), USER objects (GR_USEROBJECTS=1),
    instant CPU% = delta(TotalProcessorTime) / delta(wall) / logical_cores
  Verdict rules:
    - WorkingSet is judged on the STEADY-STATE window (after -SteadySeconds warmup),
      because GDI+ init inflates the first seconds. Early value is still recorded.
    - average CPU over the sampling window <= 0.5 %
    - cold start <= 500 ms (delegated to measure_startup.ps1)
  Evidence: docs/verify/resources.txt
  Exit: 0 PASS / 1 FAIL / 2 SKIP
#>
[CmdletBinding()]
param(
    [string]$ProcessName = 'NetBall',
    [string]$ExePath,
    [int]$Seconds = 60,
    [int]$SteadySeconds = 60,
    [double]$CpuLimitPct = 0.5,
    [double]$MemLimitMB = 15,
    [int]$StartupLimitMs = 500,
    [int]$StartupRuns = 3,
    [switch]$Launch
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

if (-not $ExePath) {
    $root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $ExePath = Join-Path $root 'build\out\NetBall.exe'
}
$ExePath = [System.IO.Path]::GetFullPath($ExePath)
$reportPath = Join-Path $PSScriptRoot 'resources.txt'
. (Join-Path $PSScriptRoot 'verifier_preflight.ps1')
Write-BuildInfo -Exe $ExePath

$cs = @'
using System;
using System.Runtime.InteropServices;
public class NBRes {
    [DllImport("user32.dll", SetLastError=true)]
    public static extern uint GetGuiResources(IntPtr hProcess, uint uiFlags);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr h);
    public static uint Gdi(int pid) { return Query(pid, 0); }   // GR_GDIOBJECTS
    public static uint Usr(int pid) { return Query(pid, 1); }   // GR_USEROBJECTS
    static uint Query(int pid, uint flag) {
        IntPtr h = OpenProcess(0x0400 | 0x1000, false, pid);    // QUERY_INFORMATION | QUERY_LIMITED
        if (h == IntPtr.Zero) return 0xFFFFFFFF;
        try { return GetGuiResources(h, flag); } finally { CloseHandle(h); }
    }
}
'@
try { Add-Type -TypeDefinition $cs -ErrorAction Stop } catch {
    if ($_.Exception.Message -notmatch 'already exists') { throw }
}

Write-Output "AC-5 RESOURCE / CPU / STARTUP CHECK"
Write-Output "  process      : $ProcessName"
Write-Output "  warmup       : $SteadySeconds s before the steady-state window"
Write-Output "  sample window: $Seconds s (1 Hz)"
Write-Output "  limits       : WS(steady) <= $MemLimitMB MB, CPU avg <= $CpuLimitPct %, startup <= $StartupLimitMs ms"

$cores = (Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
if (-not $cores -or $cores -le 0) { $cores = [Environment]::ProcessorCount }
Write-Output "  logical cores: $cores"

$proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
$launched = $false

if (-not $proc -and $Launch) {
    if (-not (Test-Path -LiteralPath $ExePath)) {
        Write-Output "RESULT: SKIP (artifact build/out/NetBall.exe missing, cannot launch)"
        exit 2
    }
    Write-Output "  launching exe for sampling ..."
    $proc = Start-Process -FilePath $ExePath -WorkingDirectory (Split-Path -Parent $ExePath) -PassThru
    $launched = $true
    Start-Sleep -Seconds 1
}

if (-not $proc) {
    if (Test-Path -LiteralPath $ExePath) {
        Write-Output "RESULT: SKIP (NetBall.exe not running; re-run with -Launch)"
    } else {
        Write-Output "RESULT: SKIP (artifact build/out/NetBall.exe missing)"
    }
    exit 2
}

$proc.Refresh()
$pid_ = $proc.Id
Write-Output ("  pid          : {0}  startedAt {1}" -f $pid_, $proc.StartTime)
Write-Output ("  early values : WS={0:F2} MB  Private={1:F2} MB  PeakWS={2:F2} MB  Handles={3}  GDI={4}  USER={5}" -f `
    ($proc.WorkingSet64 / 1MB), ($proc.PrivateMemorySize64 / 1MB), ($proc.PeakWorkingSet64 / 1MB), `
    $proc.HandleCount, [NBRes]::Gdi($pid_), [NBRes]::Usr($pid_))

$earlyWs = [int64]$proc.WorkingSet64
$earlyPeak = [int64]$proc.PeakWorkingSet64
$earlyGdi = [int][NBRes]::Gdi($pid_)
$earlyUsr = [int][NBRes]::Usr($pid_)

$fail = @()
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("# NetBall resource evidence  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')")
$lines.Add("# limits: WS(steady) <= $MemLimitMB MB, CPU avg <= $CpuLimitPct pct, startup <= $StartupLimitMs ms, cores=$cores")
$lines.Add("# exe: $ExePath")
$lines.Add("# early (t=0, includes GDI+ init): ws_bytes=$earlyWs peak_ws_bytes=$earlyPeak gdi=$earlyGdi user=$earlyUsr")
$lines.Add("# t_s\tws_bytes\tws_MB\tprivate_bytes\tpeak_ws_bytes\thandles\tgdi\tuser\tcpu_pct_instant")

if ($SteadySeconds -gt 0) {
    Write-Output ""
    Write-Output "  warmup: $SteadySeconds s (waiting for steady state before judging WS) ..."
    $w = [System.Diagnostics.Stopwatch]::StartNew()
    while ($w.Elapsed.TotalSeconds -lt $SteadySeconds) {
        Start-Sleep -Milliseconds 1000
        try { $proc.Refresh() } catch { break }
        if ($proc.HasExited) { break }
        Write-Output ("    t={0,3:F0}s  WS={1,6:F2} MB  GDI={2}  USER={3}" -f $w.Elapsed.TotalSeconds, ($proc.WorkingSet64 / 1MB), [NBRes]::Gdi($pid_), [NBRes]::Usr($pid_))
    }
    $w.Stop()
    Write-Output ("  steady-state starts at t={0:F0}s  WS={1:F2} MB" -f $w.Elapsed.TotalSeconds, ($proc.WorkingSet64 / 1MB))
}

Write-Output ""
Write-Output "  t_s   WS(MB)  Priv(MB)  PeakWS(MB)  Hand  GDI  USER   CPU%"
$prevCpu = $proc.TotalProcessorTime
$prevTime = [DateTime]::UtcNow
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$wsSamples = New-Object System.Collections.Generic.List[int64]
$privSamples = New-Object System.Collections.Generic.List[int64]
$peakSamples = New-Object System.Collections.Generic.List[int64]
$handleSamples = New-Object System.Collections.Generic.List[int]
$gdiSamples = New-Object System.Collections.Generic.List[int]
$usrSamples = New-Object System.Collections.Generic.List[int]
$cpuSamples = New-Object System.Collections.Generic.List[double]

for ($i = 0; $i -lt $Seconds; $i++) {
    Start-Sleep -Milliseconds 1000
    try { $proc.Refresh() } catch { Write-Output "  process vanished at t=$i s"; break }
    if ($proc.HasExited) { Write-Output "  process exited at t=$i s"; break }

    $now = [DateTime]::UtcNow
    $dt = ($now - $prevTime).TotalSeconds
    $dcpu = ($proc.TotalProcessorTime - $prevCpu).TotalSeconds
    $instantPct = if ($dt -gt 0) { ($dcpu / $dt / $cores) * 100 } else { 0 }
    $prevCpu = $proc.TotalProcessorTime
    $prevTime = $now

    $ws = [int64]$proc.WorkingSet64
    $priv = [int64]$proc.PrivateMemorySize64
    $peak = [int64]$proc.PeakWorkingSet64
    $hand = [int]$proc.HandleCount
    $gdi = [int][NBRes]::Gdi($pid_)
    $usr = [int][NBRes]::Usr($pid_)

    $wsSamples.Add($ws); $privSamples.Add($priv); $peakSamples.Add($peak)
    $handleSamples.Add($hand); $gdiSamples.Add($gdi); $usrSamples.Add($usr)
    $cpuSamples.Add($instantPct)

    $lines.Add(("{0}`t{1}`t{2:F2}`t{3}`t{4}`t{5}`t{6}`t{7}`t{8:F4}" -f $i, $ws, ($ws / 1MB), $priv, $peak, $hand, $gdi, $usr, $instantPct))
    Write-Output ("  {0,4}  {1,6:F2}  {2,7:F2}  {3,9:F2}  {4,4}  {5,3}  {6,4}  {7,7:F4}" -f `
        $i, ($ws / 1MB), ($priv / 1MB), ($peak / 1MB), $hand, $gdi, $usr, $instantPct)
}
$sw.Stop()

$elapsed = $sw.Elapsed.TotalSeconds
if ($wsSamples.Count -eq 0) {
    Write-Output 'RESULT: FAIL (no samples collected)'
    exit 1
}

$cpuAvg = ($cpuSamples | Measure-Object -Average).Average
$cpuPeak = ($cpuSamples | Measure-Object -Maximum).Maximum
$wsMin = ($wsSamples | Measure-Object -Minimum).Minimum
$wsMax = ($wsSamples | Measure-Object -Maximum).Maximum
$wsAvg = ($wsSamples | Measure-Object -Average).Average
$privMax = ($privSamples | Measure-Object -Maximum).Maximum
$peakMax = ($peakSamples | Measure-Object -Maximum).Maximum
$handMax = ($handleSamples | Measure-Object -Maximum).Maximum
$gdiMax = ($gdiSamples | Measure-Object -Maximum).Maximum
$gdiMin = ($gdiSamples | Measure-Object -Minimum).Minimum
$usrMax = ($usrSamples | Measure-Object -Maximum).Maximum

Write-Output ""
Write-Output ("  sampled        : {0} points over {1:F1} s" -f $wsSamples.Count, $elapsed)
Write-Output ("  WS(steady)     : min {0:F2} / avg {1:F2} / max {2:F2} MB (limit {3} MB)" -f ($wsMin / 1MB), ($wsAvg / 1MB), ($wsMax / 1MB), $MemLimitMB)
Write-Output ("  WS(early t=0)  : {0:F2} MB" -f ($earlyWs / 1MB))
Write-Output ("  Private  max   : {0:F2} MB" -f ($privMax / 1MB))
Write-Output ("  PeakWS   max   : {0:F2} MB" -f ($peakMax / 1MB))
Write-Output ("  Handles  max   : {0}" -f $handMax)
Write-Output ("  GDI obj  min/max: {0} / {1}" -f $gdiMin, $gdiMax)
Write-Output ("  USER obj max   : {0}" -f $usrMax)
Write-Output ("  CPU avg/peak   : {0:F4} % / {1:F4} %" -f $cpuAvg, $cpuPeak)

if (($wsMax / 1MB) -gt $MemLimitMB) { $fail += ("steady working set max {0:F2} MB > {1} MB" -f ($wsMax / 1MB), $MemLimitMB) }
if ($cpuAvg -gt $CpuLimitPct) { $fail += ("average CPU {0:F4} % > {1} %" -f $cpuAvg, $CpuLimitPct) }
if ($gdiMax -gt $gdiMin -and ($gdiMax - $gdiMin) -gt 5) {
    Write-Output ("  NOTE: GDI objects grew by {0} (possible per-frame leak, informational)" -f ($gdiMax - $gdiMin))
}

# ---------------------------------------------------------------- cold start
Write-Output ""
Write-Output "  measuring cold start ($StartupRuns runs) ..."
$startupMs = -1
try {
    $startupOut = & (Join-Path $PSScriptRoot 'measure_startup.ps1') -ExePath $ExePath -ProcessName $ProcessName -Runs $StartupRuns -LimitMs $StartupLimitMs 2>&1
    foreach ($l in $startupOut) { Write-Output ("    " + $l) }
    $startupMetric = ($startupOut | Select-String -Pattern '^METRIC: startup_ms=(\d+)' | Select-Object -First 1)
    if ($startupMetric) { $startupMs = [int]$startupMetric.Matches[0].Groups[1].Value }
} catch {
    Write-Output ("    measure_startup failed: " + $_.Exception.Message)
}

$lines.Add("")
$lines.Add("# steady-state summary")
$lines.Add("ws_min_bytes=$wsMin")
$lines.Add("ws_avg_bytes=$([int]$wsAvg)")
$lines.Add("ws_max_bytes=$wsMax")
$lines.Add("ws_max_mb=$([math]::Round($wsMax / 1MB, 2))")
$lines.Add("ws_early_mb=$([math]::Round($earlyWs / 1MB, 2))")
$lines.Add("private_max_mb=$([math]::Round($privMax / 1MB, 2))")
$lines.Add("peak_ws_mb=$([math]::Round($peakMax / 1MB, 2))")
$lines.Add("handles_max=$handMax")
$lines.Add("gdi_min=$gdiMin")
$lines.Add("gdi_max=$gdiMax")
$lines.Add("user_max=$usrMax")
$lines.Add("cpu_avg_pct=$([math]::Round($cpuAvg, 4))")
$lines.Add("cpu_peak_pct=$([math]::Round($cpuPeak, 4))")
$lines.Add("steady_seconds=$SteadySeconds")
$lines.Add("sample_seconds=$([math]::Round($elapsed, 1))")
$lines.Add("logical_cores=$cores")
$lines.Add("startup_ms=$startupMs")
$lines.Add("limits=ws<=${MemLimitMB}MB cpu<=${CpuLimitPct}pct startup<=${StartupLimitMs}ms")

if ($startupMs -lt 0) {
    $fail += 'cold start could not be measured'
} elseif ($startupMs -gt $StartupLimitMs) {
    $fail += "cold start $startupMs ms > $StartupLimitMs ms"
}

if ($launched -and $proc) {
    try { if (-not $proc.HasExited) { $proc.Kill() } } catch {}
}

$verdict = if ($fail.Count -eq 0) { 'PASS' } else { 'FAIL' }
$lines.Add("verdict=$verdict")
$lines.Add("")
$lines.Add("# machine generated by check_ac5_resources.ps1")
Set-Content -LiteralPath $reportPath -Value $lines -Encoding UTF8
Write-Output ""
Write-Output "  evidence written: $reportPath"

if ($fail.Count -eq 0) {
    Write-Output ("RESULT: PASS (WS steady max {0:F2} MB, CPU avg {1:F4} %, startup {2} ms)" -f ($wsMax / 1MB), $cpuAvg, $startupMs)
    exit 0
} else {
    foreach ($f in $fail) { Write-Output "  VIOLATION: $f" }
    Write-Output 'RESULT: FAIL'
    exit 1
}
