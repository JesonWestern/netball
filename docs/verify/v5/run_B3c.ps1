# run_B3c.ps1 -- T14 B3c (redone): tray "reset position" from a NORMAL state.
# The first attempt reset the ball while it was sitting on the 125%-DPI left
# monitor; the round trip to the 175% primary monitor resized 110->154 px after
# the position had been chosen, which is a WM_DPICHANGED edge case, not the
# reset semantics.  This run measures the documented use case.
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

Write-Output "=========== T14 B3c : tray reset position (normal case) ==========="
foreach ($lp in @(Get-Process -Name $ProcName -ErrorAction SilentlyContinue)) {
    Write-Output ("  cleanup: killing leftover pid={0}" -f $lp.Id)
    try { $lp.Kill(); [void]$lp.WaitForExit(4000) } catch {}
}
$anchor = Initialize-Measurement -WaitMs 8000
Write-Output ("  anchor sha = {0} bytes={1}" -f $anchor.Sha, $anchor.Bytes)

# start somewhere else on the primary monitor, so "reset" has to do real work
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx 700 -posy 300
$p = Start-Ball -SettleMs 1600
if ($p.Hwnd -eq [IntPtr]::Zero) { Write-Output "RESULT: BLOCKED (ball window never appeared)"; exit 3 }
$r0 = Get-BallRect $p.Hwnd
Write-Output ("  start rect: L={0} T={1} R={2} B={3} {4}x{5}" -f $r0.L, $r0.T, $r0.R, $r0.B, $r0.W, $r0.H)

# the tray menu item is invoked exactly as tray.cpp does it: WM_COMMAND(id,0)
Send-Cmd $p.Hwnd 1004
Start-Sleep -Milliseconds 1200
$r1 = Get-BallRect $p.Hwnd
Write-Output ("  after reset: L={0} T={1} R={2} B={3} {4}x{5}" -f $r1.L, $r1.T, $r1.R, $r1.B, $r1.W, $r1.H)
$png = Join-Path $MediaDir 'b3c-after-reset.png'
$null = Capture-Region -x ($r1.L - 16) -y ($r1.T - 16) -w ($r1.W + 32) -h ($r1.H + 32) -path $png

$inside = (($r1.L -ge $VIRT_L) -and ($r1.T -ge $VIRT_T) -and ($r1.R -le $VIRT_R) -and ($r1.B -le $VIRT_B))
Add-Check 'B3c.moved' ([bool](($r1.L -ne $r0.L) -or ($r1.T -ne $r0.T))) ("({0},{1}) -> ({2},{3})" -f $r0.L, $r0.T, $r1.L, $r1.T)
Add-Check 'B3c.inside-virtual' $inside ("rect fully inside the virtual screen: {0}" -f $inside)
Add-Check 'B3c.default-spot' (($r1.L -eq 3662) -and ($r1.T -eq 1898)) ("rect ({0},{1}) vs documented default (3662,1898)" -f $r1.L, $r1.T)
$c = Read-Cfg
Write-Output ("  ini after reset: posx={0} posy={1}" -f $c.posx, $c.posy)

# cross-DPI observation (documented, not a D-06 acceptance item):
# put the ball on the 125% left monitor, then reset, and record the overhang.
Write-Output ""
Write-Output "  --- cross-DPI observation: reset while the ball sits on the left monitor ---"
Stop-BallPid $p.Pid
$null = Wait-NoBall 5000
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1600 -posy 400
$p2 = Start-Ball -SettleMs 1600
if ($p2.Hwnd -ne [IntPtr]::Zero) {
    $x0 = Get-BallRect $p2.Hwnd
    Write-Output ("  on left monitor: L={0} T={1} {2}x{3} (DPI 125% -> 110px)" -f $x0.L, $x0.T, $x0.W, $x0.H)
    Send-Cmd $p2.Hwnd 1004
    Start-Sleep -Milliseconds 1200
    $x1 = Get-BallRect $p2.Hwnd
    Write-Output ("  after reset   : L={0} T={1} R={2} B={3} {4}x{5}" -f $x1.L, $x1.T, $x1.R, $x1.B, $x1.W, $x1.H)
    Write-Output ("  OBS overhang right = {0} px, bottom = {1} px ({2}% of the width)" -f ($x1.R - $VIRT_R), ($x1.B - $VIRT_B), ([math]::Round(100.0 * ($x1.R - $VIRT_R) / $x1.W, 1)))
    Stop-BallPid $p2.Pid
    $null = Wait-NoBall 5000
}

Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
Write-Output ("  config restored: posx={0} posy={1}" -f (Read-Cfg).posx, (Read-Cfg).posy)
$fails = Get-FailCount
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
