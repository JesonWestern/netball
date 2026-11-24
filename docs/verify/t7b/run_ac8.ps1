# run_ac8.ps1 -- AC-8 auto text contrast: 9 real colour presets at the default
# opacity 85 %, each captured for real, plus the maximin arithmetic cross-check.
. "$PSScriptRoot\nb.ps1"
Write-Output "=========== T7b AC-8 : text contrast (presets x opacity 85%) ==========="

if (-not (Wait-NoBall 10000)) {
    Write-Output "RESULT: BLOCKED (a NetBall instance is still running)"
    exit 3
}
# anchor first, copy second: the copy is guaranteed to be the broadcast binary
$anchor = Assert-Anchor
$null = New-ExeCopy
$copyInfo = Get-ExeInfo $ExeCopy
if ($copyInfo.Sha -ne $anchor.Sha) {
    Write-Output ("  ! private copy sha {0} != anchor {1}" -f $copyInfo.Sha, $anchor.Sha)
    Write-Output "RESULT: BLOCKED-ANCHOR (private copy is not the broadcast binary)"
    exit 4
}
Write-Output ("  private copy verified: {0} {1} B mtime={2}" -f $copyInfo.Sha, $copyInfo.Bytes, $copyInfo.Mtime)
Write-Output "  PRESETS_SOURCE=new   (blue #144BC8 / purple #6E32BE / red #B41923)"
Write-Output ("  MEASURED_EXE: sha256={0} mtime={1} bytes={2}" -f $anchor.Sha, $anchor.Mtime, $anchor.Bytes)

# swatch order and exact values taken from src/settings_dialog.cpp kPresets[8];
# default-bg is cfg::resetToDefault() (#181C24), i.e. what a fresh install shows.
$presets = @(
    @{ k = 'swatch1-darkgray'; hex = '#262A34' },
    @{ k = 'swatch2-black';    hex = '#000000' },
    @{ k = 'swatch3-white';    hex = '#FFFFFF' },
    @{ k = 'swatch4-blue';     hex = '#144BC8' },
    @{ k = 'swatch5-green';    hex = '#22A05A' },
    @{ k = 'swatch6-orange';   hex = '#EB8C1E' },
    @{ k = 'swatch7-purple';   hex = '#6E32BE' },
    @{ k = 'swatch8-red';      hex = '#B41923' },
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
    $bgNow = Get-IniValue 'bgcolor'
    Write-Output ("  {0,-18} {1} rect={2}x{3} png={4} bytes={5}" -f $p0.k, $p0.hex, $rc.W, $rc.H, (Split-Path -Leaf $png), $len)
    Add-Check ("{0}.png" -f $p0.k) ($len -gt 200) ("screenshot bytes={0}" -f $len)
    # the config is shared with every other member: if it changed under us the
    # screenshot no longer shows the preset it claims to show
    Add-Check ("{0}.config-intact" -f $p0.k) ($bgNow -eq $p0.hex) `
        ("bgcolor after capture = {0} (wrote {1})" -f $bgNow, $p0.hex)
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

$null = Confirm-AnchorUnchanged $anchor
$fails = Get-FailCount
Write-Output ""
Write-Output ("SUMMARY: checks={0} failed={1} images_exit={2} math_exit={3}" -f $script:Checks.Count, $fails, $rcImg, $rcMath)
if (($fails -eq 0) -and ($rcImg -eq 0) -and ($rcMath -eq 0)) {
    Write-Output "RESULT: PASS"
    exit 0
}
Write-Output "RESULT: FAIL"
exit 1
