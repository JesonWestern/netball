# verifier-4 preflight: build identity, processes, config, inventory.
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$exe = Join-Path $root 'build\out\NetBall.exe'
Write-Output '=== BUILD IDENTITY ==='
if (Test-Path -LiteralPath $exe) {
    $i = Get-Item -LiteralPath $exe
    $h = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    Write-Output ("exe_bytes={0}" -f $i.Length)
    Write-Output ("exe_mtime={0}" -f $i.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))
    Write-Output ("exe_sha256={0}" -f $h)
} else {
    Write-Output 'exe=MISSING'
}

Write-Output ''
Write-Output '=== NEWEST SOURCES ==='
Get-ChildItem -LiteralPath (Join-Path $root 'src') -Filter '*.cpp' -File |
    Sort-Object LastWriteTime -Descending | Select-Object -First 3 |
    ForEach-Object { Write-Output ("src {0,-22} {1}" -f $_.Name, $_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')) }
Get-ChildItem -LiteralPath (Join-Path $root 'src') -Filter '*.h' -File |
    Sort-Object LastWriteTime -Descending | Select-Object -First 2 |
    ForEach-Object { Write-Output ("src {0,-22} {1}" -f $_.Name, $_.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')) }

Write-Output ''
Write-Output '=== PROCESSES (any NetBall image) ==='
$p = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like 'NetBall*' }
if ($p) {
    foreach ($x in $p) {
        Write-Output ("proc pid={0} name={1} start={2}" -f $x.Id, $x.ProcessName, $x.StartTime.ToString('HH:mm:ss'))
    }
} else {
    Write-Output 'proc none (0 NetBall instances)'
}

Write-Output ''
Write-Output '=== BUILD OUT INVENTORY (raw) ==='
$out = Join-Path $root 'build\out'
if (Test-Path -LiteralPath $out) {
    $items = Get-ChildItem -LiteralPath $out -Force
    Write-Output ("entries={0}" -f $items.Count)
    foreach ($it in $items) {
        $kind = if ($it.PSIsContainer) { 'DIR ' } else { 'FILE' }
        $len = if ($it.PSIsContainer) { 0 } else { $it.Length }
        Write-Output ("  {0} {1,-30} {2} bytes" -f $kind, $it.Name, $len)
    }
} else {
    Write-Output 'build\out MISSING'
}

Write-Output ''
Write-Output '=== CONFIG (%APPDATA%\NetBall\config.ini) ==='
$cfg = Join-Path $env:APPDATA 'NetBall\config.ini'
Write-Output ("path={0}" -f $cfg)
if (Test-Path -LiteralPath $cfg) {
    $ci = Get-Item -LiteralPath $cfg
    Write-Output ("cfg_bytes={0} cfg_mtime={1}" -f $ci.Length, $ci.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))
    Get-Content -LiteralPath $cfg -Encoding UTF8 | ForEach-Object { Write-Output ("  | " + $_) }
} else {
    Write-Output 'config.ini MISSING'
}

Write-Output ''
Write-Output '=== PYTHON BYTECODE RESIDUE under docs\verify ==='
$pc = Join-Path $root 'docs\verify\__pycache__'
if (Test-Path -LiteralPath $pc) {
    $f = Get-ChildItem -LiteralPath $pc -Force -Recurse
    Write-Output ("__pycache__ entries={0}" -f $f.Count)
    foreach ($x in $f) { Write-Output ("  {0}  {1} bytes  {2}" -f $x.Name, $x.Length, $x.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss')) }
} else {
    Write-Output '__pycache__ absent'
}

Write-Output ''
Write-Output '=== HOST ==='
Write-Output ("os={0}" -f [Environment]::OSVersion.VersionString)
Write-Output ("ps={0}" -f $PSVersionTable.PSVersion.ToString())
