# run_ac12.ps1 -- AC-12 context menu + tray menu.
# TrackPopupMenu blocks the host message loop, so after triggering a menu we
# NEVER wait synchronously: capture immediately, then dismiss with ESC.
# Structural evidence comes from MN_GETHMENU (item ids / texts / check marks),
# visual evidence from the screenshot taken while the menu was up.
. "$PSScriptRoot\nb.ps1"
Write-Output "=========== T7b AC-12 : right-click menu + tray menu ==========="

if (-not (Wait-NoBall 8000)) {
    Write-Output "RESULT: BLOCKED (a NetBall instance is still running)"
    exit 3
}
$null = New-ExeCopy

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

Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx 700 -posy 320
$p = Start-Ball -SettleMs 1500
if ($p.Hwnd -eq [IntPtr]::Zero) {
    Write-Output "RESULT: FAIL (ball window never appeared)"
    exit 1
}
$rc = Get-BallRect $p.Hwnd
$cx = $rc.L + [int]($rc.W / 2)
$cy = $rc.T + [int]($rc.H / 2)
Write-Output ("  ball: hwnd=0x{0:X} rect=L{1} T{2} {3}x{4} centre=({5},{6})" -f [int64]$p.Hwnd, $rc.L, $rc.T, $rc.W, $rc.H, $cx, $cy)

# what the config says the check marks should be
$autoStartReg = $null
try { $autoStartReg = (Get-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'NetBall' -ErrorAction SilentlyContinue).NetBall } catch {}
$expectAuto = 0
if ($autoStartReg) { $expectAuto = 1 }
Write-Output ("  HKCU Run\NetBall = {0} -> autostart check mark expected {1}" -f $autoStartReg, $expectAuto)

# ---------------------------------------------------------------- ball menu
[void][NBT7b]::SetCursorPos($cx, $cy)
Start-Sleep -Milliseconds 250
[void][NBT7b]::PostMessage($p.Hwnd, 0x0205, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_RBUTTONUP
Start-Sleep -Milliseconds 700                                                 # menu is up, UI thread is blocked
$menu = [NBT7b]::FindMenuWindow([uint32]$p.Pid)
Write-Output ("  ball menu window: 0x{0:X}" -f [int64]$menu)
if ($menu -eq [IntPtr]::Zero) {
    Add-Check 'ball.menu.appears' $false "SKIP-CANNOT-CAPTURE (no #32768 window for the process)"
} else {
    $mr = Get-BallRect $menu
    Write-Output ("  menu rect: L{0} T{1} {2}x{3}" -f $mr.L, $mr.T, $mr.W, $mr.H)
    Add-Check 'ball.menu.geometry' (($mr.W -gt 40) -and ($mr.H -gt 40)) ("menu size {0}x{1}" -f $mr.W, $mr.H)
    $png = Join-Path $MediaDir 'ac12-ball-menu.png'
    $reg = Get-ClampedRegion -x ($mr.L - 12) -y ($mr.T - 12) -w ($mr.W + 24) -h ($mr.H + 24)
    $len = Capture-Region -x $reg.X -y $reg.Y -w $reg.W -h $reg.H -path $png
    Add-Check 'ball.menu.screenshot' ($len -gt 200) ("captured while the popup was up: {0} bytes" -f $len)

    $dump = [NBT7b]::DumpMenu($menu)
    Write-Output "  --- MN_GETHMENU dump (ball) ---"
    foreach ($l in ($dump -split "`r?`n")) { if ($l) { Write-Output ("    " + $l) } }
    $items = Get-MenuItems $dump
    $real = @($items | Where-Object { $_.Sep -eq 0 })
    $ids = @($real | ForEach-Object { $_.Id })
    Add-Check 'ball.menu.item-count' ($real.Count -eq 6) ("{0} selectable items (expect 6)" -f $real.Count)
    Add-Check 'ball.menu.item-ids' (($ids -join ',') -eq '1001,1002,1003,1004,1005,1006') ("ids = " + ($ids -join ','))
    $sepCount = @($items | Where-Object { $_.Sep -ne 0 }).Count
    Add-Check 'ball.menu.separators' ($sepCount -eq 2) ("{0} separator(s) (expect 2)" -f $sepCount)
    $top = @($real | Where-Object { $_.Id -eq 1002 })
    $topChk = -1
    if ($top.Count -ge 1) { $topChk = $top[0].Checked }
    Add-Check 'ball.menu.topmost-checked' (($top.Count -eq 1) -and ($topChk -eq 1)) `
        ("topmost item checked={0} (config alwaysontop=1)" -f $topChk)
    $as = @($real | Where-Object { $_.Id -eq 1003 })
    $asChk = -1
    if ($as.Count -ge 1) { $asChk = $as[0].Checked }
    Add-Check 'ball.menu.autostart-checked' (($as.Count -eq 1) -and ($asChk -eq $expectAuto)) `
        ("autostart item checked={0} (registry says {1})" -f $asChk, $expectAuto)
    $allEnabled = (@($real | Where-Object { $_.Enabled -eq 1 }).Count -eq 6)
    Add-Check 'ball.menu.all-enabled' $allEnabled "all 6 items enabled"
    $closed = Close-PopupMenu $menu
    Add-Check 'ball.menu.dismissed' $closed ("ESC closed the popup={0}" -f $closed)
    if (-not $closed) { Stop-BallPid $p.Pid; Write-Output "RESULT: FAIL (popup stuck -> host loop still blocked)"; exit 1 }
}

# ---------------------------------------------------------------- tray menu
[void][NBT7b]::SetCursorPos($cx, $cy)
Start-Sleep -Milliseconds 200
[void][NBT7b]::PostMessage($p.Hwnd, 0x8064, [IntPtr]1, [IntPtr]0x0205)   # WM_APP+100, WM_RBUTTONUP
Start-Sleep -Milliseconds 700
$tmenu = [NBT7b]::FindMenuWindow([uint32]$p.Pid)
Write-Output ("  tray menu window: 0x{0:X}" -f [int64]$tmenu)
if ($tmenu -eq [IntPtr]::Zero) {
    Add-Check 'tray.menu.appears' $false "SKIP-CANNOT-CAPTURE (no #32768 window after WM_APP+100)"
} else {
    $tr = Get-BallRect $tmenu
    $png = Join-Path $MediaDir 'ac12-tray-menu.png'
    $reg = Get-ClampedRegion -x ($tr.L - 12) -y ($tr.T - 12) -w ($tr.W + 24) -h ($tr.H + 24)
    $len = Capture-Region -x $reg.X -y $reg.Y -w $reg.W -h $reg.H -path $png
    Add-Check 'tray.menu.screenshot' ($len -gt 200) ("captured while the tray popup was up: {0} bytes" -f $len)
    $dump2 = [NBT7b]::DumpMenu($tmenu)
    Write-Output "  --- MN_GETHMENU dump (tray) ---"
    foreach ($l in ($dump2 -split "`r?`n")) { if ($l) { Write-Output ("    " + $l) } }
    $items2 = Get-MenuItems $dump2
    $real2 = @($items2 | Where-Object { $_.Sep -eq 0 })
    $ids2 = @($real2 | ForEach-Object { $_.Id })
    Add-Check 'tray.menu.item-count' ($real2.Count -eq 3) ("{0} selectable items (expect 3)" -f $real2.Count)
    Add-Check 'tray.menu.item-ids' (($ids2 -join ',') -eq '1001,1010,1006') ("ids = " + ($ids2 -join ','))
    $closed2 = Close-PopupMenu $tmenu
    Add-Check 'tray.menu.dismissed' $closed2 ("ESC closed the popup={0}" -f $closed2)
    if (-not $closed2) { Stop-BallPid $p.Pid; Write-Output "RESULT: FAIL (tray popup stuck)"; exit 1 }
}

# ---------------------------------------------------------------- show / hide
$visPng = Join-Path $MediaDir 'ac12-ball-visible.png'
$hidPng = Join-Path $MediaDir 'ac12-ball-hidden.png'
$reg = Get-ClampedRegion -x ($rc.L - 8) -y ($rc.T - 8) -w ($rc.W + 16) -h ($rc.H + 16)
$lenV = Capture-Region -x $reg.X -y $reg.Y -w $reg.W -h $reg.H -path $visPng
Send-Cmd $p.Hwnd 1010
Start-Sleep -Milliseconds 600
$vis1 = [NBT7b]::IsWindowVisible($p.Hwnd)
$lenH = Capture-Region -x $reg.X -y $reg.Y -w $reg.W -h $reg.H -path $hidPng
Write-Output ("  toggle 1: IsWindowVisible={0} (visible shot {1} B, hidden shot {2} B)" -f $vis1, $lenV, $lenH)
Add-Check 'tray.showhide.hides' (-not $vis1) ("IsWindowVisible={0} after IDM_TRAY_SHOW" -f $vis1)
Add-Check 'tray.showhide.hide-evidence' (($lenV -gt 200) -and ($lenH -gt 200)) "before/after screenshots both real"

Send-Cmd $p.Hwnd 1010
Start-Sleep -Milliseconds 600
$vis2 = [NBT7b]::IsWindowVisible($p.Hwnd)
Write-Output ("  toggle 2: IsWindowVisible={0}" -f $vis2)
Add-Check 'tray.showhide.restores' $vis2 ("IsWindowVisible={0} after the second IDM_TRAY_SHOW" -f $vis2)

Stop-BallPid $p.Pid
$fails = Get-FailCount
Write-Output ""
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
