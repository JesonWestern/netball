# run_ac10.ps1 -- AC-10 drag position memory + off-screen reset.
# Real mouse events (SetCursorPos + mouse_event), graceful exit through the app's
# own command id 1006, then the ini is read back and the app restarted.
. "$PSScriptRoot\nb.ps1"
Write-Output "=========== T7b AC-10 : drag memory + off-screen reset ==========="

if (-not (Wait-NoBall 8000)) {
    Write-Output "RESULT: BLOCKED (a NetBall instance is still running)"
    exit 3
}
$null = New-ExeCopy

$scr = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
Write-Output ("  primary screen (physical px): {0}x{1} at {2},{3}" -f $scr.Width, $scr.Height, $scr.X, $scr.Y)

function Test-OnScreen($rc) {
    return (($rc.L -ge $scr.X) -and ($rc.T -ge $scr.Y) -and
            ($rc.R -le ($scr.X + $scr.Width)) -and ($rc.B -le ($scr.Y + $scr.Height)) -and
            ($rc.W -gt 10) -and ($rc.H -gt 10))
}

# ---------------------------------------------------------------- 1. default start
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
$p = Start-Ball -SettleMs 1400
if ($p.Hwnd -eq [IntPtr]::Zero) {
    Write-Output "RESULT: FAIL (ball window never appeared)"
    exit 1
}
$rc = Get-BallRect $p.Hwnd
Write-Output ("  initial rect : L={0} T={1} {2}x{3} (on-screen={4})" -f $rc.L, $rc.T, $rc.W, $rc.H, (Test-OnScreen $rc))
Add-Check 'initial.on-screen' (Test-OnScreen $rc) ("default position L={0} T={1}" -f $rc.L, $rc.T)

# ---------------------------------------------------------------- 2. real drag
$targetX = 900
$targetY = 500
$sx = $rc.L + [int]($rc.W / 2)
$sy = $rc.T + [int]($rc.H / 2)
$ex = $sx + ($targetX - $rc.L)
$ey = $sy + ($targetY - $rc.T)
Write-Output ("  drag: cursor ({0},{1}) -> ({2},{3}) moves the window to {4},{5}" -f $sx, $sy, $ex, $ey, $targetX, $targetY)
# a background window cannot take mouse capture (only the foreground window can),
# so the cursor must stay over the ball for the whole gesture: small steps, and
# the window follows the cursor 1:1, which keeps it under the pointer.
[void][NBT7b]::SetForegroundWindow($p.Hwnd)
Start-Sleep -Milliseconds 200
[void][NBT7b]::SetCursorPos($sx, $sy)
Start-Sleep -Milliseconds 250
[NBT7b]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)      # LEFTDOWN
Start-Sleep -Milliseconds 180
$dist = [math]::Sqrt([math]::Pow($ex - $sx, 2) + [math]::Pow($ey - $sy, 2))
$steps = [int][math]::Ceiling($dist / 16.0)
if ($steps -lt 4) { $steps = 4 }
if ($steps -gt 240) { $steps = 240 }
Write-Output ("  drag steps: {0} (<=16 px each)" -f $steps)
for ($i = 1; $i -le $steps; $i++) {
    $mx = $sx + [int](($ex - $sx) * $i / [double]$steps)
    $my = $sy + [int](($ey - $sy) * $i / [double]$steps)
    [void][NBT7b]::SetCursorPos($mx, $my)
    Start-Sleep -Milliseconds 15
}
[NBT7b]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)      # LEFTUP
Start-Sleep -Milliseconds 900
$rc2 = Get-BallRect $p.Hwnd
Write-Output ("  after drag   : L={0} T={1} (asked 900,500)" -f $rc2.L, $rc2.T)
Add-Check 'drag.moved' ((($rc2.L -ne $rc.L) -or ($rc2.T -ne $rc.T))) ("window moved to L={0} T={1}" -f $rc2.L, $rc2.T)
Add-Check 'drag.lands-on-target' (([math]::Abs($rc2.L - $targetX) -le 3) -and ([math]::Abs($rc2.T - $targetY) -le 3)) `
    ("L={0} T={1} expect 900/500 (tolerance 3 px)" -f $rc2.L, $rc2.T)

# ---------------------------------------------------------------- 3. graceful exit
Send-Cmd $p.Hwnd 1006
$proc = Get-Process -Id $p.Pid -ErrorAction SilentlyContinue
$gone = $true
if ($proc) { $gone = $proc.WaitForExit(6000) }
Start-Sleep -Milliseconds 500
$still = Get-Process -Id $p.Pid -ErrorAction SilentlyContinue
Add-Check 'exit.via-command-1006' ($null -eq $still) ("process exited={0}" -f ($null -eq $still))
Stop-BallPid $p.Pid

$cfg = Read-Cfg
Write-Output ("  ini after exit: posx={0} posy={1}" -f $cfg['posx'], $cfg['posy'])
Add-Check 'ini.posx' ($cfg['posx'] -eq '900') ("posx={0} expect 900" -f $cfg['posx'])
Add-Check 'ini.posy' ($cfg['posy'] -eq '500') ("posy={0} expect 500" -f $cfg['posy'])

# ---------------------------------------------------------------- 4. restart
$p2 = Start-Ball -SettleMs 1400
$rc3 = Get-BallRect $p2.Hwnd
Write-Output ("  restart rect : L={0} T={1} {2}x{3}" -f $rc3.L, $rc3.T, $rc3.W, $rc3.H)
Add-Check 'restart.restores-position' (([math]::Abs($rc3.L - $targetX) -le 3) -and ([math]::Abs($rc3.T - $targetY) -le 3)) `
    ("L={0} T={1} expect 900/500" -f $rc3.L, $rc3.T)
Stop-BallPid $p2.Pid

# ---------------------------------------------------------------- 5. out of range
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx 99999 -posy 99999
$p3 = Start-Ball -SettleMs 1400
$rc4 = Get-BallRect $p3.Hwnd
Write-Output ("  posx=99999   : L={0} T={1} {2}x{3} on-screen={4}" -f $rc4.L, $rc4.T, $rc4.W, $rc4.H, (Test-OnScreen $rc4))
Add-Check 'oob.huge.falls-back-visible' (Test-OnScreen $rc4) ("L={0} T={1}" -f $rc4.L, $rc4.T)
Stop-BallPid $p3.Pid

# inside the parser range (-1..32767) but outside the 3840x2160 desktop
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx 5000 -posy 2000
$p4 = Start-Ball -SettleMs 1400
$rc5 = Get-BallRect $p4.Hwnd
Write-Output ("  posx=5000/2000: L={0} T={1} {2}x{3} on-screen={4}" -f $rc5.L, $rc5.T, $rc5.W, $rc5.H, (Test-OnScreen $rc5))
Add-Check 'oob.offdesk.falls-back-visible' (Test-OnScreen $rc5) ("L={0} T={1} (5000 is beyond the 3840 wide desktop)" -f $rc5.L, $rc5.T)
Stop-BallPid $p4.Pid

# negative-but-not-minus-one coordinates: parser keeps them only if >= -1
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -500 -posy 400
$p5 = Start-Ball -SettleMs 1400
$rc6 = Get-BallRect $p5.Hwnd
Write-Output ("  posx=-500    : L={0} T={1} {2}x{3} on-screen={4}" -f $rc6.L, $rc6.T, $rc6.W, $rc6.H, (Test-OnScreen $rc6))
Add-Check 'oob.negative.falls-back-visible' (Test-OnScreen $rc6) ("L={0} T={1}" -f $rc6.L, $rc6.T)
Stop-BallPid $p5.Pid

# ---------------------------------------------------------------- 6. restore defaults
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
Start-Sleep -Milliseconds 300
$cur = Read-Cfg
$expect = [ordered]@{
    shape = '0'; size = '88'; opacity = '85'; textauto = '1'; shownet = '1'; showmem = '1'
    refreshms = '1000'; alwaysontop = '1'; autostart = '0'; posx = '-1'; posy = '-1'
    savedw = '0'; savedh = '0'; bgcolor = '#181C24'; textcolor = '#FFFFFF'
}
$diff = @()
foreach ($k in $expect.Keys) {
    if ($cur[$k] -ne $expect[$k]) { $diff += ("{0}: expect {1} actual {2}" -f $k, $expect[$k], $cur[$k]) }
}
$extra = @($cur.Keys | Where-Object { -not $expect.Contains($_) })
Write-Output "  config --diff against the documented default:"
foreach ($k in $expect.Keys) {
    $mark = '  '
    if ($cur[$k] -ne $expect[$k]) { $mark = '! ' }
    Write-Output ("   {0}{1,-12} expect {2,-8} actual {3}" -f $mark, $k, $expect[$k], $cur[$k])
}
if ($extra.Count -gt 0) { Write-Output ("   extra keys: " + ($extra -join ',')) }
Add-Check 'config.restored' (($diff.Count -eq 0) -and ($extra.Count -eq 0)) `
    ("{0}/{1} keys match, extra={2}" -f ($expect.Keys.Count - $diff.Count), $expect.Keys.Count, $extra.Count)

$fails = Get-FailCount
Write-Output ""
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
