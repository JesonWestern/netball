# run_ac8.ps1 -- AC-8 auto text contrast: 9 real colour presets at the default
# opacity 85 %, each captured for real, plus the maximin arithmetic cross-check.
. "$PSScriptRoot\nb.ps1"
Write-Output "=========== T7b AC-8 : text contrast (presets x opacity 85%) ==========="

if (-not (Wait-NoBall 8000)) {
    Write-Output "RESULT: BLOCKED (a NetBall instance is still running)"
    exit 3
}
$null = New-ExeCopy

# swatch order and exact values taken from src/settings_dialog.cpp kPresets[8];
# default-bg is cfg::resetToDefault() (#181C24), i.e. what a fresh install shows.
$presets = @(
    @{ k = 'swatch1-darkgray'; hex = '#262A34' },
    @{ k = 'swatch2-black';    hex = '#000000' },
    @{ k = 'swatch3-white';    hex = '#FFFFFF' },
    @{ k = 'swatch4-blue';     hex = '#1E5ADC' },
    @{ k = 'swatch5-green';    hex = '#22A05A' },
    @{ k = 'swatch6-orange';   hex = '#EB8C1E' },
    @{ k = 'swatch7-purple';   hex = '#8246D2' },
    @{ k = 'swatch8-red';      hex = '#D23237' },
    @{ k = 'default-bg';       hex = '#181C24' }
)

$posx = 700
$posy = 320
$manifest = Join-Path $TmpDir 'ac8-manifest.txt'
$rows = @()
$captured = 0

foreach ($p0 in $presets) {
    Write-Cfg -shape 0 -size 160 -opacity 85 -bg $p0.hex -posx $posx -posy $posy -textAuto 1
    $p = Start-Ball -SettleMs 1500
    if ($p.Hwnd -eq [IntPtr]::Zero) {
        Add-Check ("{0}.window" -f $p0.k) $false "ball window never appeared"
        Stop-BallPid $p.Pid
        continue
    }
    $rc = Get-BallRect $p.Hwnd
    $png = Join-Path $MediaDir ("ac8-{0}.png" -f $p0.k)
    $reg = Get-ClampedRegion -x ($rc.L - 10) -y ($rc.T - 10) -w ($rc.W + 20) -h ($rc.H + 20)
    $len = Capture-Region -x $reg.X -y $reg.Y -w $reg.W -h $reg.H -path $png
    Write-Output ("  {0,-18} {1} rect={2}x{3} png={4} bytes={5}" -f $p0.k, $p0.hex, $rc.W, $rc.H, (Split-Path -Leaf $png), $len)
    Add-Check ("{0}.png" -f $p0.k) ($len -gt 200) ("screenshot bytes={0}" -f $len)
    Add-Check ("{0}.size" -f $p0.k) ([math]::Abs($rc.W - 280) -le 8) ("W={0} expect 280 (160 DIP x 175%)" -f $rc.W)
    $rows += ("{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}" -f $p0.k, $png, $reg.X, $reg.Y, $rc.L, $rc.T, $rc.R, $rc.B, $p0.hex)
    $captured++
    Stop-BallPid $p.Pid
}

Write-Manifest -path $manifest -rows $rows -header 'key|png|x0|y0|L|T|R|B|extra'
Add-Check "shots.captured" ($captured -eq $presets.Count) ("captured {0}/{1} preset screenshots" -f $captured, $presets.Count)

$py = Join-Path $Here 't7b_img.py'
Write-Output ""
& python $py contrast --manifest $manifest
$rcImg = $LASTEXITCODE
Write-Output ("PYEXIT-images: {0}" -f $rcImg)

Write-Output ""
Write-Output "---------- arithmetic cross-check (maximin rule) ----------"
& python $py math
$rcMath = $LASTEXITCODE
Write-Output ("PYEXIT-math: {0}" -f $rcMath)

$fails = Get-FailCount
Write-Output ""
Write-Output ("SUMMARY: checks={0} failed={1} images_exit={2} math_exit={3}" -f $script:Checks.Count, $fails, $rcImg, $rcMath)
if (($fails -eq 0) -and ($rcImg -eq 0) -and ($rcMath -eq 0)) {
    Write-Output "RESULT: PASS"
    exit 0
}
Write-Output "RESULT: FAIL"
exit 1
