# launch.ps1 -- start a T7b runner DETACHED from the shared console.
# Reason: the console is shared with other agents; a stray Ctrl+C there kills a
# child that lives in the same console group and silently truncates evidence.
# Win32_Process.Create spawns the process under WMI, so it has no console to lose.
param([Parameter(Mandatory = $true)][string]$Name, [string]$Log = '', [switch]$Clean)

if ($Clean) {
    # only ever kills THIS copy by its unique image name (never /im NetBall.exe,
    # which would destroy another member's sampling run)
    $k = @(Get-Process -Name 'NetBall_t7b' -ErrorAction SilentlyContinue)
    foreach ($p in $k) { try { $p.Kill(); [void]$p.WaitForExit(3000) } catch {} }
    if ($k.Count -gt 0) { Write-Output ("CLEANED {0} leftover NetBall_t7b instance(s)" -f $k.Count) }
    Start-Sleep -Milliseconds 500
}

$here = $PSScriptRoot
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $here))
$ps1 = Join-Path $here $Name
if (-not (Test-Path -LiteralPath $ps1)) { Write-Output "ERR: missing $ps1"; exit 1 }
if ([string]::IsNullOrEmpty($Log)) { $Log = [System.IO.Path]::GetFileNameWithoutExtension($Name) + '.log' }
$media = Join-Path (Split-Path -Parent $here) 'media-t7b'
if (-not (Test-Path -LiteralPath $media)) { New-Item -ItemType Directory -Path $media -Force | Out-Null }
$logp = Join-Path $media $Log
if (Test-Path -LiteralPath $logp) { Remove-Item -LiteralPath $logp -Force }

$line = 'cmd.exe /c powershell -NoProfile -ExecutionPolicy Bypass -File ' + $ps1 + ' > ' + $logp + ' 2>&1 & echo EXIT=%errorlevel% >> ' + $logp
$res = ([wmiclass]'Win32_Process').Create($line)
$pidPath = Join-Path $root ('build\_tmp\t7b\launch-' + [System.IO.Path]::GetFileNameWithoutExtension($Name) + '.txt')
"PID={0} RETURN={1}`r`nLOG={2}`r`nCMD={3}" -f $res.ProcessId, $res.ReturnValue, $logp, $line |
    Set-Content -LiteralPath $pidPath -Encoding ASCII
Write-Output ("LAUNCHED pid={0} return={1}" -f $res.ProcessId, $res.ReturnValue)
Write-Output ("LOG: {0}" -f $logp)
