# run_ac6.ps1 -- AC-6 three shapes: switch, restart, real screenshot + pixel assertions
. "$PSScriptRoot\nb.ps1"
Write-Output "=========== T7b AC-6 : shape 0/1/2 ==========="

$anchor = Initialize-Measurement -WaitMs 10000

$posx = 700
$posy = 320
$manifest = Join-Path $TmpDir 'ac6-manifest.txt'
$rows = @()
$captured = 0

# Shape is independent of colour and opacity, but a pixel assertion needs the
# ball to be separable from the wallpaper.  Measure the wallpaper first, then
# use the maximally contrasting pure colour at opacity 100 % so the fill mask
# is unambiguous (the default #181C24 over a dark desktop is indistinguishable).
$wall = Get-ClampedRegion -x ($posx - 20) -y ($posy - 20) -w 320 -h 320
$wallLuma = Get-RegionLuma $wall.X $wall.Y $wall.W $wall.H
$ballHex = '#FFFFFF'
if ($wallLuma -ge 128) { $ballHex = '#000000' }
Write-Output ("  wallpaper luma at the test spot = {0:F1} -> ball colour {1} (opacity 100%%)" -f $wallLuma, $ballHex)

foreach ($shape in 0, 1, 2) {
    Write-Cfg -shape $shape -size 88 -opacity 100 -bg $ballHex -posx $posx -posy $posy
    $p = Start-Ball -SettleMs 1400
    if ($p.Hwnd -eq [IntPtr]::Zero) {
        Add-Check ("shape{0}.window" -f $shape) $false "ball window never appeared (pid $($p.Pid))"
        Stop-BallPid $p.Pid
        continue
    }
    $rc = Get-BallRect $p.Hwnd
    $png = Join-Path $MediaDir ("ac6-shape{0}.png" -f $shape)
    $reg = Get-ClampedRegion -x ($rc.L - 8) -y ($rc.T - 8) -w ($rc.W + 16) -h ($rc.H + 16)
    $len = Capture-Region -x $reg.X -y $reg.Y -w $reg.W -h $reg.H -path $png
    Write-Output ("  shape={0} rect=L{1} T{2} R{3} B{4} ({5}x{6}) png={7} bytes={8} shot={9}x{10}" -f `
        $shape, $rc.L, $rc.T, $rc.R, $rc.B, $rc.W, $rc.H, (Split-Path -Leaf $png), $len, $reg.W, $reg.H)

    Add-Check ("shape{0}.png" -f $shape) ($len -gt 200) ("screenshot bytes={0}" -f $len)
    Add-Check ("shape{0}.position" -f $shape) (([math]::Abs($rc.L - $posx) -le 2) -and ([math]::Abs($rc.T - $posy) -le 2)) `
        ("window parked at L={0} T={1} (asked {2},{3})" -f $rc.L, $rc.T, $posx, $posy)

    if ($shape -eq 2) {
        $ratio = $rc.W / [double]$rc.H
        Add-Check "shape2.aspect" (($ratio -gt 1.40) -and ($ratio -lt 1.60)) ("W/H={0:F3} expect 1.50 (88 DIP x 175%)" -f $ratio)
        Add-Check "shape2.height154" ([math]::Abs($rc.H - 154) -le 6) ("H={0} expect 154" -f $rc.H)
    } else {
        Add-Check ("shape{0}.square" -f $shape) ($rc.W -eq $rc.H) ("W={0} H={1}" -f $rc.W, $rc.H)
        Add-Check ("shape{0}.size154" -f $shape) ([math]::Abs($rc.W - 154) -le 6) ("W={0} expect 154" -f $rc.W)
    }

    $rows += ("shape{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|" -f $shape, $png, $reg.X, $reg.Y, $rc.L, $rc.T, $rc.R, $rc.B)
    $captured++
    Stop-BallPid $p.Pid
}

Write-Manifest -path $manifest -rows $rows -header 'key|png|x0|y0|L|T|R|B|extra'
Write-Output ("  manifest: {0} ({1} rows)" -f $manifest, $rows.Count)
Add-Check "shots.captured" ($captured -eq 3) ("captured {0}/3 shape screenshots" -f $captured)

Write-Output ""
$py = Join-Path $Here 't7b_img.py'
& python $py shapes --manifest $manifest
$rcPy = $LASTEXITCODE
Write-Output ("PYEXIT: {0}" -f $rcPy)

$null = Confirm-AnchorUnchanged $anchor
$fails = Get-FailCount
Write-Output ""
Write-Output ("SUMMARY: checks={0} failed={1} pyexit={2}" -f $script:Checks.Count, $fails, $rcPy)
if (($fails -eq 0) -and ($rcPy -eq 0) -and ($captured -eq 3)) {
    Write-Output "RESULT: PASS"
    exit 0
}
Write-Output "RESULT: FAIL"
exit 1
