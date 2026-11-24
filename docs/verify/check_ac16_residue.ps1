<#
  AC-16 (part 2)  build/out residue: after T6 claims cleanup, build/out must contain
                  NetBall.exe and nothing else. Kept as a SEPARATE verdict from the
                  marker scan so a residue problem is recorded as its own defect
                  instead of being mixed into AC-16 part 1.
  Exit: 0 PASS / 1 FAIL / 2 SKIP (build/out not created yet)
#>
[CmdletBinding()]
param(
    [string]$OutDir,
    [switch]$AllowExeAbsent,
    [switch]$ExpectClean
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $OutDir) { $OutDir = Join-Path $root 'build\out' }

Write-Output 'AC-16 RESIDUE CHECK (part 2: build/out inventory)'
Write-Output "  dir : $OutDir"

if (-not (Test-Path -LiteralPath $OutDir -PathType Container)) {
    Write-Output 'RESULT: SKIP (build/out does not exist yet)'
    exit 2
}

$items = Get-ChildItem -LiteralPath $OutDir -Force -ErrorAction SilentlyContinue
Write-Output ("  entries: {0}" -f $items.Count)
$extra = @()
$exeOk = $false
foreach ($it in $items) {
    $kind = if ($it.PSIsContainer) { 'DIR ' } else { 'FILE' }
    $len = if ($it.PSIsContainer) { 0 } else { $it.Length }
    Write-Output ("    {0} {1,-34} {2} bytes" -f $kind, $it.Name, $len)
    if ($it.PSIsContainer) { $extra += ($it.Name + '\') }
    elseif ($it.Name -ieq 'NetBall.exe') { $exeOk = $true }
    else { $extra += $it.Name }
}

Write-Output ''
if (-not $exeOk) {
    if ($AllowExeAbsent) {
        Write-Output '  NetBall.exe absent (allowed while T6 is still building)'
    } else {
        Write-Output '  VIOLATION: NetBall.exe is missing from build/out'
        Write-Output 'METRIC: entries=' + $items.Count + ' extra=' + $extra.Count + ' exe=0'
        Write-Output 'RESULT: FAIL (no deliverable in build/out)'
        exit 1
    }
}

if ($extra.Count -eq 0) {
    Write-Output '  inventory clean: only NetBall.exe'
    Write-Output ("METRIC: entries={0} extra=0" -f $items.Count)
    Write-Output 'RESULT: PASS (build/out contains only NetBall.exe)'
    exit 0
}

Write-Output ("  RESIDUE ({0}): {1}" -f $extra.Count, ($extra -join ', '))
Write-Output '  Each entry above is a leftover test binary / log / screenshot.'
Write-Output ''
if (-not $ExpectClean) {
    Write-Output '  T6 has not claimed a cleanup yet, so this is recorded as inventory only.'
    Write-Output ("METRIC: entries={0} extra={1} expect_clean=0" -f $items.Count, $extra.Count)
    Write-Output 'RESULT: SKIP (residue listed above; re-run with -ExpectClean after T6 claims cleanup)'
    exit 2
}
Write-Output '  VIOLATION: build/out is not clean -> separate AC-16 residue defect'
Write-Output ("METRIC: entries={0} extra={1} expect_clean=1" -f $items.Count, $extra.Count)
Write-Output "RESULT: FAIL (build/out not clean: $($extra.Count) extra entries)"
exit 1
