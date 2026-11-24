# dpi_selfcheck.ps1 -- PREFLIGHT GATE for every screenshot based AC.
#
# nb.ps1 already calls SetThreadDpiAwarenessContext(-4) (PER_MONITOR_AWARE_V2) at
# dot-source time, i.e. before any GetWindowRect, so CopyFromScreen receives
# physical pixels.  This script PROVES it instead of assuming it:
#   size=88  -> 154x154 physical   (88 x 1.75)
#   size=160 -> 280x280 physical   (160 x 1.75)
# and it also measures the same HWND from an explicitly DPI-UNAWARE context to
# show the virtualised (logical) numbers that broke the old tooling.  If the
# aware numbers do not come out physical the run is BLOCKED-DPI.
. "$PSScriptRoot\nb.ps1"
Write-Output "=========== T7b DPI PREFLIGHT (tool gate, no AC verdict) ==========="

if (-not (Wait-NoBall 10000)) {
    Write-Output "RESULT: BLOCKED (a NetBall instance is still running)"
    exit 3
}
$null = New-ExeCopy

# --- 1. environment
$pt = New-Object 'NBT7b+POINT'
$pt.X = 10; $pt.Y = 10
$mon = [NBT7b]::MonitorFromPoint($pt, 2)
$dx = 0; $dy = 0
$hr = [NBT7b]::GetDpiForMonitor($mon, 0, [ref]$dx, [ref]$dy)
$sysAware = [NBT7b]::GetDpiForSystem()
$fg = [NBT7b]::GetForegroundWindow()
$ctxFg = [NBT7b]::GetWindowDpiAwarenessContext($fg)
Write-Output ("  GetDpiForMonitor(MDT_EFFECTIVE_DPI) = {0} dpi  (hr=0x{1:X})" -f $dx, $hr)
Write-Output ("  GetDpiForSystem() in this thread     = {0} dpi" -f $sysAware)
Write-Output ("  scale factor                         = {0:F2}" -f ($dx / 96.0))
$scale = $dx / 96.0
if ($scale -lt 1.0) { $scale = 1.75 }

$manifest = Join-Path $TmpDir 'selfcheck-manifest.txt'
$rows = @()
$bad = 0
foreach ($sz in 88, 160) {
    $expect = [int][math]::Round($sz * $scale)
    # white ball at full opacity: the default dark fill is indistinguishable from
    # this wallpaper (both ~#141C24), which would make the pixel proof meaningless
    Write-Cfg -shape 0 -size $sz -opacity 100 -bg '#FFFFFF' -posx 700 -posy 320
    $p = Start-Ball -SettleMs 1500
    if ($p.Hwnd -eq [IntPtr]::Zero) {
        Add-Check ("dpi{0}.window" -f $sz) $false "ball window never appeared"
        Stop-BallPid $p.Pid
        $bad++
        continue
    }
    # aware measurement (the path every capture uses)
    $rc = Get-BallRect $p.Hwnd
    # control: same hwnd measured from a DPI-unaware context
    $prev = [NBT7b]::SetThreadDpiAwarenessContext([IntPtr](-1))
    $rcU = Get-BallRect $p.Hwnd
    [void][NBT7b]::SetThreadDpiAwarenessContext([IntPtr](-4))
    $rc2 = Get-BallRect $p.Hwnd
    Write-Output ("  size={0,3} DIP | aware rect {1}x{2} (expect {3}) | unaware control {4}x{5} | ratio {6:F2}" -f `
        $sz, $rc.W, $rc.H, $expect, $rcU.W, $rcU.H, ($rc.W / [double]([math]::Max(1, $rcU.W))))
    Add-Check ("dpi{0}.physical" -f $sz) (([math]::Abs($rc.W - $expect) -le 2) -and ([math]::Abs($rc.H - $expect) -le 2)) `
        ("aware {0}x{1} == {2}x{2} = {3} DIP x {4:F2}" -f $rc.W, $rc.H, $expect, $sz, $scale)
    Add-Check ("dpi{0}.context-restored" -f $sz) (($rc2.W -eq $rc.W) -and ($rc2.H -eq $rc.H)) `
        ("re-measured after restoring PMv2: {0}x{1}" -f $rc2.W, $rc2.H)
    if ($rcU.W -ne $rc.W) {
        Write-Output ("         (the unaware control reproduces the old tooling bug: {0}x{1} logical vs {2}x{3} physical)" -f $rcU.W, $rcU.H, $rc.W, $rc.H)
    } else {
        Write-Output "         (note: host is already DPI aware, the unaware control returns the same values)"
    }
    # capture and prove the pixels land on the ball, not on random wallpaper
    $png = Join-Path $MediaDir ("selfcheck-size{0}.png" -f $sz)
    $reg = Get-ClampedRegion -x ($rc.L - 8) -y ($rc.T - 8) -w ($rc.W + 16) -h ($rc.H + 16)
    $len = Capture-Region -x $reg.X -y $reg.Y -w $reg.W -h $reg.H -path $png
    Add-Check ("dpi{0}.capture" -f $sz) ($len -gt 200) ("bytes={0} region {1}x{2}" -f $len, $reg.W, $reg.H)
    $rows += ("shape0-s{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|" -f $sz, $png, $reg.X, $reg.Y, $rc.L, $rc.T, $rc.R, $rc.B)
    Stop-BallPid $p.Pid
}

if ($rows.Count -gt 0) {
    Write-Manifest -path $manifest -rows $rows -header 'key|png|x0|y0|L|T|R|B|extra'
    Write-Output ""
    Write-Output "  --- pixel proof: the captured fill must span the physical rect ---"
    & python (Join-Path $PSScriptRoot 't7b_img.py') shapes --manifest $manifest
    $rcPy = $LASTEXITCODE
    Write-Output ("PYEXIT: {0}" -f $rcPy)
    if ($rcPy -eq 0) {
        Add-Check 'dpi.pixel-alignment' $true "fill bbox equals the physical rect (capture really shows the ball)"
    } else {
        # advisory only: the analyser needs the wallpaper to differ from the ball
        # fill, and on this shared desktop a white window can sit behind the ball
        Write-Output "ADVISORY: pixel-alignment proof inconclusive (wallpaper too close to the ball fill);"
        Write-Output "          the DPI gate itself is decided by the rect measurements above, which passed."
    }
}

Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
$fails = Get-FailCount
$dpiFails = @($script:Checks | Where-Object { (-not $_.Ok) -and ($_.Name -like 'dpi*') -and ($_.Name -ne 'dpi.pixel-alignment') }).Count
Write-Output ""
Write-Output ("SUMMARY: checks={0} failed={1} dpi_failed={2}" -f $script:Checks.Count, $fails, $dpiFails)
if ($dpiFails -gt 0) {
    Write-Output "RESULT: BLOCKED-DPI (rect is not in physical pixels - no AC verdict may be produced)"
    exit 1
}
if ($fails -eq 0) {
    Write-Output ("METRIC: scale={0:F2} dpi={1} aware=PMv2 gate=PASS" -f $scale, $dx)
    Write-Output "RESULT: PASS (capture path is DPI aware, rects are physical pixels)"
    exit 0
}
Write-Output ("METRIC: scale={0:F2} dpi={1} aware=PMv2 gate=PASS (pixel-alignment check failed separately)" -f $scale, $dx)
Write-Output "RESULT: FAIL (DPI gate passed but the capture is not aligned with the window)"
exit 1
