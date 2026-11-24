# run_A3.ps1 -- T14 A3 only (illegal coordinate 99999 must be coerced to a visible spot).
# Split out of run_A because the shared console received an external Ctrl+C during A3.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\t7b\nb.ps1"

$MediaDir = Join-Path $VerifyD 'media-v5'
$TmpDir   = Join-Path $Root 'build\_tmp\v5'
$ExeCopy  = Join-Path $TmpDir 'NetBall_v5.exe'
$ProcName = 'NetBall_v5'
foreach ($d in @($MediaDir, $TmpDir)) {
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}

$VIRT_L = -1920; $VIRT_T = 0; $VIRT_R = 3840; $VIRT_B = 2160

Write-Output "=========== T14 A3 : illegal coordinate 99999 ==========="
# clean up only our own private copy (never touch a foreign NetBall.exe)
foreach ($p in @(Get-Process -Name $ProcName -ErrorAction SilentlyContinue)) {
    Write-Output ("  cleanup: killing leftover {0} pid={1}" -f $p.ProcessName, $p.Id)
    try { $p.Kill(); [void]$p.WaitForExit(4000) } catch {}
}
$anchor = Initialize-Measurement -WaitMs 8000
Write-Output ("  anchor sha = {0} bytes={1}" -f $anchor.Sha, $anchor.Bytes)

Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx 99999 -posy 99999
$c = Read-Cfg
Write-Output ("  ini written posx={0} posy={1}" -f $c.posx, $c.posy)
$p = Start-Ball -SettleMs 1600
if ($p.Hwnd -eq [IntPtr]::Zero) { Write-Output "RESULT: BLOCKED (ball window never appeared, A3)"; exit 3 }
$r = Get-BallRect $p.Hwnd
Write-Output ("  A3 rect: L={0} T={1} R={2} B={3} W={4} H={5}" -f $r.L, $r.T, $r.R, $r.B, $r.W, $r.H)
$png = Join-Path $MediaDir 'a3-illegal.png'
$null = Capture-Region -x ($r.L - 20) -y ($r.T - 20) -w ($r.W + 40) -h ($r.H + 40) -path $png

$interX = [math]::Min($r.R, $VIRT_R) - [math]::Max($r.L, $VIRT_L)
$interY = [math]::Min($r.B, $VIRT_B) - [math]::Max($r.T, $VIRT_T)
Write-Output ("  intersection with virtual screen = {0} x {1} px" -f $interX, $interY)

Add-Check 'A3.illegal.not-honoured' (($r.L -lt 10000) -and ($r.T -lt 10000)) ("rect.L={0} (99999 was NOT honoured)" -f $r.L)
Add-Check 'A3.illegal.intersects-virtual' (($interX -gt 0) -and ($interY -gt 0)) ("intersect={0}x{1}" -f $interX, $interY)
Add-Check 'A3.illegal.fully-inside' (($interX -ge $r.W) -and ($interY -ge $r.H)) ("fully inside virtual screen: intersect {0}x{1} vs window {2}x{3}" -f $interX, $interY, $r.W, $r.H)
# the pointer must be reachable: at least 32 px visible in both axes
Add-Check 'A3.illegal.usable' (($interX -ge 32) -and ($interY -ge 32)) ("visible strip {0}x{1} px (need >=32 in both axes)" -f $interX, $interY)

Stop-BallPid $p.Pid
$null = Wait-NoBall 6000
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
Write-Output ("  config restored: posx={0} posy={1}" -f (Read-Cfg).posx, (Read-Cfg).posy)
$fails = Get-FailCount
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
