# run_ac17.ps1 -- AC-17 DPI 175 %: awareness + no clipping at size 48 / 160.
# Step 1 reuses verifier-1's check_ac17_dpi.ps1 verbatim (copied to
# check_ac17_t7b.ps1, only the evidence paths were redirected) because its DPI
# handling (PMv2 thread context + GetDpiForMonitor) is correct and must not be
# re-derived.  Step 2 adds my own glyph-containment capture at both sizes.
. "$PSScriptRoot\nb.ps1"
Write-Output "=========== T7b AC-17 : DPI / scaling ==========="

$anchor = Initialize-Measurement -WaitMs 10000

Write-Output "---------- step 1: verifier-1 DPI script (copy) ----------"
$dpiScript = Join-Path $Here 'check_ac17_t7b.ps1'
$out = & $dpiScript -ExePath $ExeCopy -ProcessName 'NetBall_t7b' -ExpectedScale 175 -SizeSweep 2>&1
foreach ($l in $out) { Write-Output ("  " + $l) }
$rcDpi = $LASTEXITCODE
Write-Output ("  exit={0}" -f $rcDpi)
# The DPI numbers this script prints are authoritative; its -ExpectText margin is
# NOT: the script never masks the shape, so wallpaper brighter than the fill is
# counted as glyph ink and the box grows to the whole frame (margin < 0).  Judge
# the DPI fields here and leave overflow to the masked test in step 2.
$metricLine = (@($out | Where-Object { $_ -match 'METRIC: system_dpi' }) -join ' ')
if ($metricLine -match 'system_dpi=(\d+).*window_dpi=(\d+).*scale_pct=(\d+).*awareness=(\d+)') {
    $sdpi = [int]$matches[1]; $wdpi = [int]$matches[2]; $pct = [int]$matches[3]; $aware = [int]$matches[4]
    Add-Check 'ac17.system-dpi' ($sdpi -eq 168) ("GetDpiForSystem={0} (168 = 175%)" -f $sdpi)
    Add-Check 'ac17.window-dpi' ($wdpi -eq 168) ("GetDpiForWindow={0}" -f $wdpi)
    Add-Check 'ac17.scale' ($pct -eq 175) ("effective scale={0}%" -f $pct)
    Add-Check 'ac17.awareness' ($aware -ge 2) ("DPI awareness context={0} (2=PER_MONITOR_AWARE)" -f $aware)
} else {
    Add-Check 'ac17.dpi-fields' $false "could not parse the METRIC line from check_ac17_t7b.ps1"
}
if ($rcDpi -ne 0) {
    Write-Output "  NOTE: check_ac17_t7b.ps1 exit=$rcDpi is driven by its unmasked -ExpectText margin"
    Write-Output "        (bright wallpaper behind the ball counts as glyph ink). Advisory only;"
    Write-Output "        glyph containment is decided by the masked analysis in step 2."
}

Write-Output ""
Write-Output "---------- step 2: glyph containment at size 48 / 160 ----------"
$manifest = Join-Path $TmpDir 'ac17-manifest.txt'
$rows = @()
$captured = 0
foreach ($sz in 48, 160) {
    Write-Cfg -shape 0 -size $sz -opacity 85 -bg '#181C24' -posx 700 -posy 320 -textAuto 1
    $p = Start-Ball -SettleMs 1500
    if ($p.Hwnd -eq [IntPtr]::Zero) {
        Add-Check ("size{0}.window" -f $sz) $false "ball window never appeared"
        Stop-BallPid $p.Pid
        continue
    }
    $rc = Get-BallRect $p.Hwnd
    $png = Join-Path $MediaDir ("ac17-size{0}.png" -f $sz)
    $reg = Get-ClampedRegion -x ($rc.L - 8) -y ($rc.T - 8) -w ($rc.W + 16) -h ($rc.H + 16)
    $len = Capture-Region -x $reg.X -y $reg.Y -w $reg.W -h $reg.H -path $png
    $expect = [int]($sz * 1.75)
    Write-Output ("  size={0,3} DIP -> rect {1}x{2} (expect {3}px) png={4} bytes={5}" -f $sz, $rc.W, $rc.H, $expect, (Split-Path -Leaf $png), $len)
    Add-Check ("size{0}.physical" -f $sz) ([math]::Abs($rc.W - $expect) -le 4) ("W={0} expect {1} = {2} DIP x 175%" -f $rc.W, $expect, $sz)
    Add-Check ("size{0}.png" -f $sz) ($len -gt 200) ("screenshot bytes={0}" -f $len)
    $rows += ("size{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|" -f $sz, $png, $reg.X, $reg.Y, $rc.L, $rc.T, $rc.R, $rc.B)
    $captured++
    Stop-BallPid $p.Pid
}
Write-Manifest -path $manifest -rows $rows -header 'key|png|x0|y0|L|T|R|B|extra'
Write-Output ("  manifest: {0} ({1} rows)" -f $manifest, $rows.Count)

if ($captured -gt 0) {
    $py = Join-Path $Here 't7b_img.py'
    & python $py glyph --manifest $manifest --max-spill 20
    $rcGlyph = $LASTEXITCODE
    Write-Output ("PYEXIT-glyph: {0}" -f $rcGlyph)
    Add-Check 'ac17.glyph-containment' ($rcGlyph -eq 0) ("t7b_img.py glyph exit={0}" -f $rcGlyph)
} else {
    Add-Check 'ac17.glyph-containment' $false "no screenshot"
}

# leave the shared config in the documented default state
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
Write-Output "  config restored to defaults"

$null = Confirm-AnchorUnchanged $anchor
$fails = Get-FailCount
Write-Output ""
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
