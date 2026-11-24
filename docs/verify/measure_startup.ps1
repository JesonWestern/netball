<#
  Cold-start measurement (AC-5 part 3).
  Launches build/out/NetBall.exe -Runs times, measures:
    t_exist_ms  = wall time until Process object is observable
    t_window_ms = wall time until the process owns a main window handle
                   (first usable frame); falls back to t_exist if never appears.
  Exit: 0 PASS / 1 FAIL / 2 SKIP
#>
[CmdletBinding()]
param(
    [string]$ExePath,
    [string]$ProcessName = 'NetBall',
    [int]$Runs = 3,
    [int]$LimitMs = 500,
    [switch]$KeepAlive
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

if (-not $ExePath) {
    $root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $ExePath = Join-Path $root 'build\out\NetBall.exe'
}
$ExePath = [System.IO.Path]::GetFullPath($ExePath)

Write-Output "STARTUP TIME MEASUREMENT (cold start)"
Write-Output "  exe    : $ExePath"
Write-Output "  runs   : $Runs"
Write-Output "  limit  : $LimitMs ms"

if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
    Write-Output "RESULT: SKIP (artifact build/out/NetBall.exe missing)"
    exit 2
}

# Make sure nothing pre-running so each run is a real cold start.
# SAFETY: only this process name (the caller runs a private copy, e.g. NetBall_vfy),
# and only after printing the PID so the log shows exactly what was touched.
Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Output ("  pre-clean: stopping leftover PID {0} (name {1})" -f $_.Id, $ProcessName)
    try { $_.Kill() } catch {}
}
Start-Sleep -Milliseconds 400

$existTimes  = @()
$windowTimes = @()

for ($run = 1; $run -le $Runs; $run++) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p = $null
    try {
        $p = Start-Process -FilePath $ExePath -WorkingDirectory (Split-Path -Parent $ExePath) -PassThru
    } catch {
        Write-Output ("RUN {0}: launch FAILED -> {1}" -f $run, $_.Exception.Message)
        Write-Output "RESULT: FAIL (cannot launch exe)"
        exit 1
    }

    $tExist = -1
    $tWindow = -1
    while ($sw.Elapsed.TotalMilliseconds -lt 15000) {
        if ($tExist -lt 0) {
            try { $p.Refresh() } catch {}
            if (-not $p.HasExited -and $p.Id -gt 0) { $tExist = [int]$sw.Elapsed.TotalMilliseconds }
        }
        if ($tExist -ge 0) {
            try { $p.Refresh() } catch {}
            if ($p.HasExited) { break }
            if ($p.MainWindowHandle -ne [IntPtr]::Zero) {
                $tWindow = [int]$sw.Elapsed.TotalMilliseconds
                break
            }
        }
        Start-Sleep -Milliseconds 2
    }
    $sw.Stop()

    if ($p.HasExited) {
        Write-Output ("RUN {0}: process exited early (code {1})" -f $run, $p.ExitCode)
        Write-Output "RESULT: FAIL (exe terminates during startup)"
        exit 1
    }

    $existTimes += $tExist
    if ($tWindow -lt 0) {
        Write-Output ("RUN {0}: t_exist={1} ms  t_window=TIMEOUT(>15000ms)" -f $run, $tExist)
    } else {
        $windowTimes += $tWindow
        Write-Output ("RUN {0}: t_exist={1} ms  t_window={2} ms" -f $run, $tExist, $tWindow)
    }

    if (-not $KeepAlive) {
        try { $p.Kill(); $p.WaitForExit(2000) } catch {}
    }
    Start-Sleep -Milliseconds 800   # let file cache/AV settle between runs
}

function Get-Median([int[]]$v) {
    if (-not $v -or $v.Count -eq 0) { return -1 }
    $s = $v | Sort-Object
    $n = $s.Count
    if ($n % 2 -eq 1) { return [int]$s[[int](($n - 1) / 2)] }
    return [int](($s[$n / 2 - 1] + $s[$n / 2]) / 2)
}

$medExist  = Get-Median $existTimes
$medWindow = Get-Median $windowTimes
$meas = if ($windowTimes.Count -gt 0) { $medWindow } else { $medExist }

Write-Output ""
Write-Output ("SUMMARY: runs={0}  t_exist_ms(median)={1}  t_window_ms(median)={2}" -f $Runs, $medExist, $medWindow)
if ($existTimes.Count -gt 0) {
    Write-Output ("         t_exist  min={0} max={1}" -f ($existTimes | Measure-Object -Minimum).Minimum, ($existTimes | Measure-Object -Maximum).Maximum)
}
if ($windowTimes.Count -gt 0) {
    Write-Output ("         t_window min={0} max={1}" -f ($windowTimes | Measure-Object -Minimum).Minimum, ($windowTimes | Measure-Object -Maximum).Maximum)
}
Write-Output "METRIC: startup_ms=$meas limit_ms=$LimitMs method=$(if ($windowTimes.Count -gt 0) { 'window-ready median' } else { 'process-exist median' })"

if ($medWindow -ge 0 -and $medWindow -gt $LimitMs) {
    Write-Output "RESULT: FAIL (startup $medWindow ms > $LimitMs ms)"
    exit 1
}
if ($windowTimes.Count -eq 0) {
    Write-Output "RESULT: FAIL (no window appeared within 15s; measured process-exist only)"
    exit 1
}
Write-Output "RESULT: PASS (startup $meas ms <= $LimitMs ms)"
exit 0

