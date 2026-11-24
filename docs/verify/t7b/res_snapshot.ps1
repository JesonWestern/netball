# res_snapshot.ps1 -- shared-resource snapshot for the T7b measurement window.
# The team lead needs "what else was touching the machine while you measured",
# which matters more than the verdict itself when judging data contamination.
# Read only: starts nothing, changes nothing.
param([string]$Tag = 'before')
. "$PSScriptRoot\nb.ps1"

$log = Join-Path $MediaDir ("res-{0}.log" -f $Tag)
$lines = @()
$lines += ("=== T7b shared-resource snapshot: {0} ===" -f $Tag)
$lines += ("time            : " + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss.fff'))
$lines += ("anchor.txt      : " + ((Get-Content -LiteralPath (Join-Path $TmpDir 'anchor.txt') -Raw).Trim()))
$exe = Get-ExeInfo $ExeSrc
$lines += ("build\out exe   : {0} bytes={1} mtime={2}" -f $exe.Sha, $exe.Bytes, $exe.Mtime)
$copyPath = Join-Path $TmpDir 'NetBall_t7b.exe'
if (Test-Path -LiteralPath $copyPath) {
    $ci = Get-ExeInfo $copyPath
    $lines += ("t7b copy        : {0} bytes={1} mtime={2}" -f $ci.Sha, $ci.Bytes, $ci.Mtime)
}
$lines += "--- netball processes ---"
$np = @(Get-ProcList)
if ($np.Count -eq 0) {
    $lines += "none"
} else {
    foreach ($q in $np) { $lines += ("{0} pid={1} start={2}" -f $q.ProcessName, $q.Id, $q.StartTime.ToString('HH:mm:ss')) }
}
$lines += "--- other likely test drivers (informational) ---"
foreach ($n in @('python', 'powershell', 'pwsh', 'cmd', 'curl')) {
    $c = @(Get-Process -Name $n -ErrorAction SilentlyContinue).Count
    $lines += ("{0,-12} {1}" -f $n, $c)
}
$lines += "--- config.ini ---"
if (Test-Path -LiteralPath $IniPath) {
    foreach ($l in (Get-Content -LiteralPath $IniPath)) { $lines += ("  " + $l) }
    $lines += ("  ini mtime: " + (Get-Item -LiteralPath $IniPath).LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss.fff'))
} else {
    $lines += "  (missing)"
}
$lines += "--- foreground window ---"
$fg = [NBT7b]::GetForegroundWindow()
$lines += ("  " + [NBT7b]::WindowInfo($fg))
$lines += "=== end snapshot ==="

Set-Content -LiteralPath $log -Value $lines -Encoding UTF8
$lines | ForEach-Object { Write-Output $_ }
Write-Output ("snapshot written: " + $log)
