# run_t7b_final.ps1 -- the whole T7b measurement window as ONE detached process.
#
# Why one process: the single-instance mutex and config.ini are shared with every
# other member, so the gaps between individual ACs are exactly where somebody
# else can sneak in.  Running the six steps back to back leaves no gap, and the
# parent records each child's RESULT line instead of relying on exit codes.
#
# Order (approved by the team lead):
#   resource snapshot -> DPI gate -> AC-6 -> AC-7 -> AC-8 -> AC-17 -> R3 matrix
#   -> flat check -> resource snapshot -> config restore + diff
$ErrorActionPreference = 'Continue'
$here = $PSScriptRoot
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $here))
$tmpDir = Join-Path $root 'build\_tmp\t7b'
$media = Join-Path (Split-Path -Parent $here) 'media-t7b'
$log = Join-Path $media 't7b-window.log'
$ini = Join-Path $env:APPDATA 'NetBall\config.ini'
if (-not (Test-Path -LiteralPath $tmpDir)) { New-Item -ItemType Directory -Path $tmpDir -Force | Out-Null }
if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }

function Say($m) {
    $line = [string]$m
    Add-Content -LiteralPath $log -Value $line -Encoding UTF8
    Write-Output $line
}

$defaults = [ordered]@{
    shape = '0'; size = '88'; opacity = '85'; textauto = '1'; shownet = '1'; showmem = '1'
    refreshms = '1000'; alwaysontop = '1'; autostart = '0'; posx = '-1'; posy = '-1'
    savedw = '0'; savedh = '0'; bgcolor = '#181C24'; textcolor = '#FFFFFF'
}

function Get-IniValue2([string]$k) {
    if (-not (Test-Path -LiteralPath $ini)) { return '' }
    foreach ($l in (Get-Content -LiteralPath $ini)) {
        if ($l -match ('^\s*' + [regex]::Escape($k) + '\s*=\s*(.+)$')) { return $matches[1].Trim() }
    }
    return ''
}

function Write-DefaultConfig {
    $body = @('[NetBall]') + (@($defaults.Keys) | ForEach-Object { '{0}={1}' -f $_, $defaults[$_] })
    [IO.File]::WriteAllText($ini, (($body -join "`r`n") + "`r`n"), [Text.Encoding]::ASCII)
}

function Compare-Defaults {
    $bad = @()
    foreach ($k in $defaults.Keys) {
        $v = Get-IniValue2 $k
        if ($v -ne $defaults[$k]) { $bad += ("{0}={1} (expect {2})" -f $k, $v, $defaults[$k]) }
    }
    return $bad
}

Say ("================ T7b FINAL WINDOW  {0} ================" -f (Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))
Say "MEASURED BINARY comes from the anchor below; every step re-verifies it."

# ---------------------------------------------------------------- 0. snapshot
Say ""
Say "----- STEP 0 : resource snapshot (prewindow) -----"
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'res_snapshot.ps1') -Tag prewindow | ForEach-Object { Say $_ }

# ------------------------------------------------- 0b. take over the config
Say ""
Say "----- STEP 0b : config takeover check -----"
$dirty = @(Compare-Defaults)
if ($dirty.Count -eq 0) {
    Say "  config.ini was already at the documented default - nothing to fix"
} else {
    Say ("  config.ini arrived dirty from another member: " + ($dirty -join '; '))
    Write-DefaultConfig
    $still = @(Compare-Defaults)
    Say ("  rewritten to defaults, remaining mismatches: {0}" -f $still.Count)
}

# ---------------------------------------------------------------- 1..6 steps
$steps = @(
    @{ n = 'dpi-selfcheck'; f = 'dpi_selfcheck.ps1' },
    @{ n = 'ac6';           f = 'run_ac6.ps1' },
    @{ n = 'ac7';           f = 'run_ac7.ps1' },
    @{ n = 'ac8';           f = 'run_ac8.ps1' },
    @{ n = 'ac17';          f = 'run_ac17.ps1' },
    @{ n = 'r3';            f = 'run_r3matrix.ps1' }
)
$exePath = Join-Path $root 'build\out\NetBall.exe'
$anchorSha = (Get-Content -LiteralPath (Join-Path $tmpDir 'anchor.txt') -Raw).Trim()

foreach ($s in $steps) {
    Say ""
    Say ("----- STEP {0} : {1} -----" -f $s.n, $s.f)
    $outFile = Join-Path $media ($s.n + '.log')
    $errFile = $outFile + '.err'
    if (Test-Path -LiteralPath $outFile) { Remove-Item -LiteralPath $outFile -Force }
    if (Test-Path -LiteralPath $errFile) { Remove-Item -LiteralPath $errFile -Force }
    $t0 = Get-Date
    $p = Start-Process -FilePath 'powershell.exe' `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $here $s.f)) `
        -NoNewWindow -PassThru -Wait -RedirectStandardOutput $outFile -RedirectStandardError $errFile
    $dt = [int]((Get-Date) - $t0).TotalSeconds
    $txt = ''
    if (Test-Path -LiteralPath $outFile) { $txt = [string](Get-Content -LiteralPath $outFile -Raw -Encoding UTF8) }
    $resLines = @(($txt -split "`r?`n") | Where-Object { $_ -match '^RESULT' })
    $metric = @(($txt -split "`r?`n") | Where-Object { $_ -match '^METRIC' })
    Say ("  exit={0} elapsed={1}s  lines={2}" -f $p.ExitCode, $dt, (($txt -split "`r?`n").Count))
    foreach ($l in $resLines) { Say ("  >> " + $l) }
    foreach ($l in $metric) { Say ("  mm " + $l) }
    $errTxt = ''
    if (Test-Path -LiteralPath $errFile) { $errTxt = [string](Get-Content -LiteralPath $errFile -Raw -Encoding UTF8) }
    if ($errTxt -and $errTxt.Trim().Length -gt 0) { Say ("  STDERR: " + ($errTxt -replace "`r?`n", ' / ')) }
    # anchor must still hold after every step
    $now = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash.ToLower()
    if ($now -ne $anchorSha.ToLower()) {
        Say ("  !! ARTIFACT CHANGED during step {0}: {1} != {2}" -f $s.n, $now, $anchorSha)
        Say "RESULT: BLOCKED-REPLACED (artifact replaced mid-window - the data above is void)"
        Say "================ WINDOW ABORTED ================"
        exit 4
    }
    Say "  anchor still matches after the step"
}

# ------------------------------------------------------- 7. flat check + close
Say ""
Say "----- STEP 7 : evidence sanity (flat) -----"
& python (Join-Path $here 't7b_img.py') flat --dir $media | ForEach-Object { Say $_ }

Say ""
Say "----- STEP 8 : resource snapshot (postwindow) -----"
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $here 'res_snapshot.ps1') -Tag postwindow | ForEach-Object { Say $_ }

Say ""
Say "----- STEP 9 : config restore + diff -----"
$diffBefore = @(Compare-Defaults)
Say ("  state after the window: {0} mismatch(es) vs default" -f $diffBefore.Count)
if ($diffBefore.Count -gt 0) { foreach ($d in $diffBefore) { Say ("    " + $d) } }
Write-DefaultConfig
$diffAfter = @(Compare-Defaults)
foreach ($k in $defaults.Keys) { Say ("    {0,-12} expect {1,-8} actual {2}" -f $k, $defaults[$k], (Get-IniValue2 $k)) }
Say ("  RESULT: config restored, {0} mismatch(es)" -f $diffAfter.Count)

Say ""
Say ("================ T7b WINDOW END {0} ================" -f (Get-Date).ToString('HH:mm:ss'))
Say ("anchor sha256 = {0}" -f $anchorSha)
if ($diffAfter.Count -eq 0) { Say "RESULT: WINDOW-COMPLETE (config clean)"; exit 0 }
Say "RESULT: WINDOW-COMPLETE (config NOT clean - see diffs above)"
exit 1
