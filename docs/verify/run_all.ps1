<#
  Runs every verification script once, appending raw output to coldrun.txt.
  Each script is spawned as its own child process (powershell.exe -File / python),
  so exit codes are real and one script cannot poison another.
  Output is flushed to disk after every step so an interrupted run still leaves evidence.

  Exit code: 0 if every script produced a RESULT line, 1 otherwise.
  This is a smoke runner - it does NOT decide AC verdicts.
#>
[CmdletBinding()]
param(
    [int]$Sample = 5,
    [string]$OutFile,
    [switch]$IncludeStartup,
    [switch]$Reset,
    [switch]$Full
)

$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

if (-not $OutFile) { $OutFile = Join-Path $PSScriptRoot 'coldrun.txt' }
$OutFile = [System.IO.Path]::GetFullPath($OutFile)
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exeFull = Join-Path $root 'build\out\NetBall.exe'

$steps = @(
    @{ Name = 'check_ac1_exe.ps1';            Kind = 'ps'; Args = @() },
    @{ Name = 'check_ac2_window.ps1';         Kind = 'ps'; Args = @() },
    @{ Name = 'check_ac3_net.py';             Kind = 'py'; Args = @('--seconds', "$Sample") },
    @{ Name = 'check_ac3_net.py --json';      Kind = 'py'; Args = @('--json') },
    @{ Name = 'check_ac5_resources.ps1';      Kind = 'ps'; Args = @('-Seconds', "$Sample", '-SteadySeconds', '0') },
    @{ Name = 'check_ac7_alpha.py';           Kind = 'py'; Args = @() },
    @{ Name = 'check_ac7_alpha.py --selftest'; Kind = 'py'; Args = @('--selftest') },
    @{ Name = 'check_ac8_contrast.py --math'; Kind = 'py'; Args = @('--math') },
    @{ Name = 'check_ac8_contrast.py --selftest'; Kind = 'py'; Args = @('--selftest') },
    @{ Name = 'check_ac8_contrast.py --shots'; Kind = 'py'; Args = @('--shots', (Join-Path $PSScriptRoot 'shape-*.png')) },
    @{ Name = 'check_ac9_config.py';           Kind = 'py'; Args = @() },
    @{ Name = 'check_ac9_config.py --corrupt'; Kind = 'py'; Args = @('--corrupt') },
    @{ Name = 'check_ac11_single.py';          Kind = 'py'; Args = @() },
    @{ Name = 'check_ac16_clean.ps1';          Kind = 'ps'; Args = @() },
    @{ Name = 'check_ac16_residue.ps1';        Kind = 'ps'; Args = @('-AllowExeAbsent') },
    @{ Name = 'check_ac17_dpi.ps1';            Kind = 'ps'; Args = @() },
    @{ Name = 'check_screenshot.ps1';          Kind = 'ps'; Args = @() }
)
if ($Full) {
    $steps = @(
        @{ Name = 'check_ac5_resources.ps1 (full 60s steady)'; Kind = 'ps'; Args = @('-Seconds', '60', '-SteadySeconds', '60', '-Launch') },
        @{ Name = 'check_ac17_dpi.ps1 (size sweep 48/160)';    Kind = 'ps'; Args = @('-SizeSweep') }
    ) + $steps
}
if ($IncludeStartup) {
    $steps = @(@{ Name = 'measure_startup.ps1'; Kind = 'ps'; Args = @('-Runs', '2') }) + $steps
}

$header = @()
$header += "==================================================================="
$header += "NetBall verification toolchain - cold run"
$header += "when      : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$header += "host      : $env:COMPUTERNAME / $([Environment]::OSVersion.VersionString)"
$header += "ps        : $($PSVersionTable.PSVersion)"
$header += "python    : $(& python --version 2>&1)"
$header += "root      : $root"
$header += "artifact  : $(if (Test-Path -LiteralPath $exeFull) { 'present -> ' + $exeFull } else { 'MISSING (expected during phase 1)' })"
$header += "note      : every block below is the verbatim stdout+stderr of one child process"
$header += "==================================================================="

if ($Reset -or -not (Test-Path -LiteralPath $OutFile)) {
    Set-Content -LiteralPath $OutFile -Value $header -Encoding UTF8
} else {
    Add-Content -LiteralPath $OutFile -Value "" -Encoding UTF8
    Add-Content -LiteralPath $OutFile -Value "########## new run $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ##########" -Encoding UTF8
}

$i = 0
$bad = 0
foreach ($s in $steps) {
    $i++
    $fileBase = ($s.Name -split '\s+')[0]
    $path = Join-Path $PSScriptRoot $fileBase
    $block = New-Object System.Collections.Generic.List[string]
    $block.Add("")
    $block.Add("===== [$i/$($steps.Count)] $($s.Name) =====")

    if (-not (Test-Path -LiteralPath $path)) {
        $block.Add("(script missing: $path)")
        $block.Add("----- exit=MISSING")
        Add-Content -LiteralPath $OutFile -Value $block -Encoding UTF8
        Write-Host ("[{0}/{1}] {2} MISSING" -f $i, $steps.Count, $s.Name)
        $bad++
        continue
    }

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        if ($s.Kind -eq 'ps') {
            $cmdArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $path) + $s.Args
            $out = & powershell.exe @cmdArgs 2>&1 | Out-String
        } else {
            $out = & python $path @($s.Args) 2>&1 | Out-String
        }
        $code = $LASTEXITCODE
    } catch {
        $out = "HARNESS EXCEPTION: $($_.Exception.Message)`r`n$($_.ScriptStackTrace)"
        $code = 99
    }
    $sw.Stop()

    foreach ($l in ($out -split "`r?`n")) { $block.Add($l) }
    $block.Add("----- exit=$code  elapsed=$([int]$sw.Elapsed.TotalMilliseconds)ms")

    $verdict = ($out -split "`r?`n" | Select-String -Pattern '^RESULT:\s*(\w+)' | Select-Object -Last 1)
    $v = if ($verdict) { $verdict.Matches[0].Groups[1].Value } else { 'NO-RESULT-LINE' }
    if ($v -eq 'NO-RESULT-LINE') { $bad++ }

    Add-Content -LiteralPath $OutFile -Value $block -Encoding UTF8
    Write-Host ("[{0}/{1}] {2,-34} exit={3,-3} {4,-14} {5} ms" -f $i, $steps.Count, $s.Name, $code, $v, [int]$sw.Elapsed.TotalMilliseconds)
}

$tail = @()
$tail += ""
$tail += "==================================================================="
$tail += "SUMMARY: scripts=$($steps.Count) withoutResultLine=$bad"
$tail += "==================================================================="
Add-Content -LiteralPath $OutFile -Value $tail -Encoding UTF8

Write-Host ""
Write-Host "written: $OutFile"
Write-Host "scripts without a RESULT line: $bad / $($steps.Count)"
if ($bad -gt 0) { exit 1 } else { exit 0 }

