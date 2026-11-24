# run_B.ps1 -- T14 group B : D-06 drag clamping + reset entry + full menu enumeration.
# B1 push the ball at the bottom-right corner -> rect must stay 100% inside the
#    virtual screen (right<=3840, bottom<=2160), not "32px hanging out".
# B2 push it at the top-left -> rect.left>=-1920, rect.top>=0.
# B3 tray "reset position" (id 1004) -> ball returns to the default visible spot.
# Plus: enumerate BOTH menus (item count / id / text / checked / separator / state).
# The drag handler reads GetCursorPos(), so the cursor must really move.
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

function Drag-BallTo([IntPtr]$h, [int]$tx, [int]$ty) {
    $r = Get-BallRect $h
    $cx = $r.L + [int](($r.W) / 2)
    $cy = $r.T + [int](($r.H) / 2)
    Write-Output ("  drag: from rect L={0} T={1} {2}x{3} (grab {4},{5}) -> cursor ({6},{7})" -f $r.L, $r.T, $r.W, $r.H, $cx, $cy, $tx, $ty)
    [void][NBT7b]::SetCursorPos($cx, $cy)
    Start-Sleep -Milliseconds 350
    $g = New-Object 'NBT7b+POINT'
    [void][NBT7b]::GetCursorPos([ref]$g)
    Write-Output ("  grab cursor actually at ({0},{1})" -f $g.X, $g.Y)
    [NBT7b]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)      # LEFTDOWN
    Start-Sleep -Milliseconds 250
    for ($i = 1; $i -le 10; $i++) {
        $nx = $g.X + [int](($tx - $g.X) * $i / 10)
        $ny = $g.Y + [int](($ty - $g.Y) * $i / 10)
        [void][NBT7b]::SetCursorPos($nx, $ny)
        Start-Sleep -Milliseconds 110
    }
    [void][NBT7b]::SetCursorPos($tx, $ty)
    Start-Sleep -Milliseconds 250
    $g2 = New-Object 'NBT7b+POINT'
    [void][NBT7b]::GetCursorPos([ref]$g2)
    Write-Output ("  release cursor actually at ({0},{1}) (asked {2},{3})" -f $g2.X, $g2.Y, $tx, $ty)
    [NBT7b]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)      # LEFTUP
    Start-Sleep -Milliseconds 900
    return (Get-BallRect $h)
}

function Get-MenuItems([string]$dump) {
    $items = @()
    foreach ($l in ($dump -split "`r?`n")) {
        if ($l -match '^item (\d+) id=(\d+) checked=(\d) sep=(\d) enabled=(\d) text=(.*)$') {
            $items += [pscustomobject]@{
                Idx = [int]$matches[1]; Id = [int]$matches[2]; Checked = [int]$matches[3]
                Sep = [int]$matches[4]; Enabled = [int]$matches[5]; Text = $matches[6]
            }
        }
    }
    return $items
}
function Show-MenuDump([string]$tag, [string]$dump) {
    Write-Output ("  --- {0} dump ---" -f $tag)
    foreach ($l in ($dump -split "`r?`n")) { if ($l) { Write-Output ("    " + $l) } }
}

Write-Output "=========== T14 group B : D-06 drag clamp / reset / menus ==========="
# kill any leftover of OUR private copy first (never a foreign NetBall.exe)
foreach ($lp in @(Get-Process -Name $ProcName -ErrorAction SilentlyContinue)) {
    Write-Output ("  cleanup: killing leftover {0} pid={1}" -f $lp.ProcessName, $lp.Id)
    try { $lp.Kill(); [void]$lp.WaitForExit(4000) } catch {}
}
Start-Sleep -Milliseconds 400
$anchor = Initialize-Measurement -WaitMs 8000
Write-Output ("  anchor sha = {0} bytes={1} mtime={2}" -f $anchor.Sha, $anchor.Bytes, $anchor.Mtime)
Write-Output ("  virtual screen x=[{0}..{1}] y=[{2}..{3}]" -f $VIRT_L, $VIRT_R, $VIRT_T, $VIRT_B)

Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx 700 -posy 320
$p = Start-Ball -SettleMs 1600
if ($p.Hwnd -eq [IntPtr]::Zero) { Write-Output "RESULT: BLOCKED (ball window never appeared)"; exit 3 }

# ------------------------------------------------------------------- B1
Write-Output ""
Write-Output "--- B1: drag to the bottom-right corner (target cursor 3838,2158) ---"
$r1 = Drag-BallTo $p.Hwnd 3838 2158
Write-Output ("  B1 rect: L={0} T={1} R={2} B={3} W={4} H={5}" -f $r1.L, $r1.T, $r1.R, $r1.B, $r1.W, $r1.H)
$pngB1 = Join-Path $MediaDir 'b1-bottomright.png'
$null = Capture-Region -x ($r1.L - 16) -y ($r1.T - 16) -w ($r1.W + 32) -h ($r1.H + 32) -path $pngB1
$overR = $r1.R - $VIRT_R
$overB = $r1.B - $VIRT_B
Add-Check 'B1.right-inside' ($overR -le 0) ("rect.right={0} vs virtual right {1} (overhang {2} px)" -f $r1.R, $VIRT_R, $overR)
Add-Check 'B1.bottom-inside' ($overB -le 0) ("rect.bottom={0} vs virtual bottom {1} (overhang {2} px)" -f $r1.B, $VIRT_B, $overB)
Add-Check 'B1.zero-offscreen' (($overR -le 0) -and ($overB -le 0)) "0% of the ball is outside the virtual screen (not the 32px tolerance)"
$c1 = Read-Cfg
Write-Output ("  ini after B1: posx={0} posy={1}" -f $c1.posx, $c1.posy)
Add-Check 'B1.persisted-clamped' (($c1.posx -eq [string]$r1.L) -and ($c1.posy -eq [string]$r1.T)) ("ini matches the on-screen rect ({0},{1})" -f $r1.L, $r1.T)

# ------------------------------------------------------------------- B2
Write-Output ""
Write-Output "--- B2: drag to the top-left (target cursor -1900,-50) ---"
$r2 = Drag-BallTo $p.Hwnd (-1900) (-50)
Write-Output ("  B2 rect: L={0} T={1} R={2} B={3} W={4} H={5}" -f $r2.L, $r2.T, $r2.R, $r2.B, $r2.W, $r2.H)
$pngB2 = Join-Path $MediaDir 'b2-topleft.png'
$null = Capture-Region -x ([math]::Max($r2.L, $VIRT_L)) -y ([math]::Max($r2.T, 0)) -w ([math]::Min($r2.W, $VIRT_R - [math]::Max($r2.L, $VIRT_L))) -h ([math]::Min($r2.H, $VIRT_B - [math]::Max($r2.T, 0))) -path $pngB2
Add-Check 'B2.left-inside' ($r2.L -ge $VIRT_L) ("rect.left={0} vs virtual left {1}" -f $r2.L, $VIRT_L)
Add-Check 'B2.top-inside' ($r2.T -ge $VIRT_T) ("rect.top={0} vs virtual top {1}" -f $r2.T, $VIRT_T)
$c2 = Read-Cfg
Write-Output ("  ini after B2: posx={0} posy={1}" -f $c2.posx, $c2.posy)
Add-Check 'B2.persisted-clamped' (($c2.posx -eq [string]$r2.L) -and ($c2.posy -eq [string]$r2.T)) ("ini matches the on-screen rect ({0},{1})" -f $r2.L, $r2.T)

# ------------------------------------------------------------------- menus
Write-Output ""
Write-Output "--- B3a: ball right-click menu enumeration (expect 8 items = 6 commands + 2 separators) ---"
$rNow = Get-BallRect $p.Hwnd
$cx = $rNow.L + [int]($rNow.W / 2)
$cy = $rNow.T + [int]($rNow.H / 2)
[void][NBT7b]::SetCursorPos($cx, $cy)
Start-Sleep -Milliseconds 250
[void][NBT7b]::PostMessage($p.Hwnd, 0x0205, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_RBUTTONUP
Start-Sleep -Milliseconds 800
$menu = [NBT7b]::FindMenuWindow([uint32]$p.Pid)
Write-Output ("  menu window hwnd=0x{0:X}" -f [int64]$menu)
if ($menu -eq [IntPtr]::Zero) {
    Add-Check 'menus.ball.present' $false "SKIP-CANNOT-CAPTURE (no #32768 window)"
} else {
    $mr = Get-BallRect $menu
    Write-Output ("  menu rect L={0} T={1} {2}x{3}" -f $mr.L, $mr.T, $mr.W, $mr.H)
    $pngM = Join-Path $MediaDir 'b3-ball-menu.png'
    $reg = Get-ClampedRegion -x ($mr.L - 14) -y ($mr.T - 14) -w ($mr.W + 28) -h ($mr.H + 28)
    $null = Capture-Region -x $reg.X -y $reg.Y -w $reg.W -h $reg.H -path $pngM
    $dump = [NBT7b]::DumpMenu($menu)
    Show-MenuDump 'ball' $dump
    $items = Get-MenuItems $dump
    $real = @($items | Where-Object { $_.Sep -eq 0 })
    $seps = @($items | Where-Object { $_.Sep -ne 0 })
    Add-Check 'menus.ball.count' ($items.Count -eq 8) ("GetMenuItemCount = {0} (expect 8)" -f $items.Count)
    Add-Check 'menus.ball.commands' ($real.Count -eq 6) ("{0} commands (expect 6)" -f $real.Count)
    Add-Check 'menus.ball.separators' ($seps.Count -eq 2) ("{0} separators (expect 2)" -f $seps.Count)
    $ids = @($real | ForEach-Object { $_.Id })
    Add-Check 'menus.ball.ids' (($ids -join ',') -eq '1001,1002,1003,1004,1005,1006') ("ids = " + ($ids -join ','))
    $bad = @($real | Where-Object { $_.Enabled -ne 1 })
    Add-Check 'menus.ball.all-enabled' ($bad.Count -eq 0) ("{0} of {1} command items greyed out" -f $bad.Count, $real.Count)
    $top = @($real | Where-Object { $_.Id -eq 1002 })
    Add-Check 'menus.ball.topmost-checked' (($top.Count -eq 1) -and ($top[0].Checked -eq 1)) ("1002 checked={0} (config alwaysontop=1)" -f $(if ($top.Count) { $top[0].Checked } else { -1 }))
    $exec = @($real | Where-Object { $_.Id -eq 1006 })
    $as = @($real | Where-Object { $_.Id -eq 1003 })
    Add-Check 'menus.ball.autostart-unchecked' (($as.Count -eq 1) -and ($as[0].Checked -eq 0)) ("1003 checked={0} (config autostart=0)" -f $(if ($as.Count) { $as[0].Checked } else { -1 }))
    $closed = Close-PopupMenu $menu
    Add-Check 'menus.ball.dismissed' $closed ("ESC closed the popup={0}" -f $closed)
    if (-not $closed) { Stop-BallPid $p.Pid; Write-Output "RESULT: FAIL (popup stuck, host loop blocked)"; exit 1 }
}

Write-Output ""
Write-Output "--- B3b: tray menu enumeration (expect 5 items = 4 commands + 1 separator) ---"
[void][NBT7b]::SetCursorPos($cx, $cy)
Start-Sleep -Milliseconds 200
[void][NBT7b]::PostMessage($p.Hwnd, 0x8064, [IntPtr]1, [IntPtr]0x0205)   # WM_APP+100, WM_RBUTTONUP
Start-Sleep -Milliseconds 800
$tmenu = [NBT7b]::FindMenuWindow([uint32]$p.Pid)
Write-Output ("  tray menu window hwnd=0x{0:X}" -f [int64]$tmenu)
if ($tmenu -eq [IntPtr]::Zero) {
    Add-Check 'menus.tray.present' $false "SKIP-CANNOT-CAPTURE (no #32768 after WM_APP+100)"
} else {
    $tr = Get-BallRect $tmenu
    Write-Output ("  tray menu rect L={0} T={1} {2}x{3}" -f $tr.L, $tr.T, $tr.W, $tr.H)
    $pngT = Join-Path $MediaDir 'b3-tray-menu.png'
    $reg2 = Get-ClampedRegion -x ($tr.L - 14) -y ($tr.T - 14) -w ($tr.W + 28) -h ($tr.H + 28)
    $null = Capture-Region -x $reg2.X -y $reg2.Y -w $reg2.W -h $reg2.H -path $pngT
    $dump2 = [NBT7b]::DumpMenu($tmenu)
    Show-MenuDump 'tray' $dump2
    $items2 = Get-MenuItems $dump2
    $real2 = @($items2 | Where-Object { $_.Sep -eq 0 })
    $seps2 = @($items2 | Where-Object { $_.Sep -ne 0 })
    Add-Check 'menus.tray.count' ($items2.Count -eq 5) ("GetMenuItemCount = {0} (expect 5)" -f $items2.Count)
    Add-Check 'menus.tray.commands' ($real2.Count -eq 4) ("{0} commands (expect 4)" -f $real2.Count)
    Add-Check 'menus.tray.separators' ($seps2.Count -eq 1) ("{0} separator (expect 1)" -f $seps2.Count)
    $ids2 = @($real2 | ForEach-Object { $_.Id })
    Add-Check 'menus.tray.ids' (($ids2 -join ',') -eq '1001,1010,1004,1006') ("ids = " + ($ids2 -join ','))
    $bad2 = @($real2 | Where-Object { $_.Enabled -ne 1 })
    Add-Check 'menus.tray.all-enabled' ($bad2.Count -eq 0) ("{0} of {1} command items greyed out" -f $bad2.Count, $real2.Count)
    $hasReset = (@($real2 | Where-Object { $_.Id -eq 1004 }).Count -eq 1)
    Add-Check 'menus.tray.has-resetpos' $hasReset "id 1004 (reset position) present in the tray menu"
    $closed2 = Close-PopupMenu $tmenu
    Add-Check 'menus.tray.dismissed' $closed2 ("ESC closed the popup={0}" -f $closed2)
    if (-not $closed2) { Stop-BallPid $p.Pid; Write-Output "RESULT: FAIL (tray popup stuck)"; exit 1 }
}

# ------------------------------------------------------------------- reset
Write-Output ""
Write-Output "--- B3c: tray reset (exact message tray.cpp sends: WM_COMMAND 1004) ---"
$rBefore = Get-BallRect $p.Hwnd
Write-Output ("  before reset: L={0} T={1}" -f $rBefore.L, $rBefore.T)
Send-Cmd $p.Hwnd 1004
Start-Sleep -Milliseconds 1200
$rAfter = Get-BallRect $p.Hwnd
Write-Output ("  after  reset: L={0} T={1} R={2} B={3}" -f $rAfter.L, $rAfter.T, $rAfter.R, $rAfter.B)
$pngR = Join-Path $MediaDir 'b3-after-reset.png'
$null = Capture-Region -x ($rAfter.L - 16) -y ($rAfter.T - 16) -w ($rAfter.W + 32) -h ($rAfter.H + 32) -path $pngR
$inVirt = (($rAfter.L -ge $VIRT_L) -and ($rAfter.T -ge $VIRT_T) -and ($rAfter.R -le $VIRT_R) -and ($rAfter.B -le $VIRT_B))
Add-Check 'B3c.reset.moved' ([bool](($rAfter.L -ne $rBefore.L) -or ($rAfter.T -ne $rBefore.T))) ("position changed from ({0},{1}) to ({2},{3})" -f $rBefore.L, $rBefore.T, $rAfter.L, $rAfter.T)
Add-Check 'B3c.reset.inside-virtual' $inVirt "reset position is fully inside the virtual screen"
$isDefault = (($rAfter.L -eq 3662) -and ($rAfter.T -eq 1898))
Add-Check 'B3c.reset.default-spot' $isDefault ("rect ({0},{1}) vs expected default (3662,1898)" -f $rAfter.L, $rAfter.T)
$c3 = Read-Cfg
Write-Output ("  ini after reset: posx={0} posy={1}" -f $c3.posx, $c3.posy)

Stop-BallPid $p.Pid
$null = Wait-NoBall 6000
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
Write-Output ("  config restored: posx={0} posy={1}" -f (Read-Cfg).posx, (Read-Cfg).posy)
$fails = Get-FailCount
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
