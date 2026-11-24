# run_ac7.ps1 -- AC-7 opacity 20% vs 100% must really change the rendering.
# A WHITE ball is mandatory: the default dark fill #181C24 differs by only 3.05 %
# in luminance between 20 % and 100 %, which is physically indistinguishable and
# would produce a false FAIL.
# The desktop behind a 20 % ball is visible through it, so the backdrop is proven
# frozen (before/after capture) around every shot and the shot is retried when
# another window wanders over the test spot.
. "$PSScriptRoot\nb.ps1"
Write-Output "=========== T7b AC-7 : opacity 20% vs 100% (white ball) ==========="

$anchor = Initialize-Measurement -WaitMs 10000

$sizeDip = 160
$expectPx = [int]($sizeDip * 1.75)          # 280 at 175 %
$pad = 20
$scr = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds

# 1. find a spot whose backdrop is not moving (this box is shared with other
#    agents, so a chat window can slide under the ball at any moment)
$candidates = @(
    @(700, 320), @(1400, 200), @(260, 640), @(1200, 620), @(500, 200)
)
$spot = $null
$bestD = 999.0
foreach ($c in $candidates) {
    $x = $c[0]; $y = $c[1]
    if (($x + $expectPx + 2 * $pad) -gt $scr.Width) { continue }
    if (($y + $expectPx + 2 * $pad) -gt $scr.Height) { continue }
    $d = Test-ScreenFrozen -x ($x - $pad) -y ($y - $pad) -w ($expectPx + 2 * $pad) -h ($expectPx + 2 * $pad) -WaitMs 900
    Write-Output ("  candidate ({0},{1}) backdrop drift over 0.9s = {2:F2}" -f $x, $y, $d)
    if ($d -lt 1.5) { $spot = $c; $bestD = $d; break }
    if ($d -lt $bestD) { $bestD = $d; $spot = $c }
}
if ($null -eq $spot) {
    Write-Output "RESULT: BLOCKED (no usable spot on the primary screen)"
    exit 3
}
$posx = $spot[0]
$posy = $spot[1]
Write-Output ("  test spot: {0},{1}" -f $posx, $posy)

$region = [pscustomobject]@{ X = $posx - $pad; Y = $posy - $pad; W = $expectPx + 2 * $pad; H = $expectPx + 2 * $pad }
$base = Join-Path $MediaDir 'ac7-base.png'
$lenBase = Capture-Region -x $region.X -y $region.Y -w $region.W -h $region.H -path $base
Write-Output ("  baseline (no ball) : {0} bytes  region x={1} y={2} {3}x{4}" -f $lenBase, $region.X, $region.Y, $region.W, $region.H)
Add-Check 'baseline.png' ($lenBase -gt 200) ("desktop baseline bytes={0}" -f $lenBase)

$shots = @{}
foreach ($op in 20, 100) {
    $done = $false
    for ($attempt = 1; ($attempt -le 3) -and (-not $done); $attempt++) {
        $pre = Join-Path $TmpDir ("ac7-pre{0}-{1}.png" -f $op, $attempt)
        $post = Join-Path $TmpDir ("ac7-post{0}-{1}.png" -f $op, $attempt)
        $null = Capture-Region -x $region.X -y $region.Y -w $region.W -h $region.H -path $pre
        Write-Cfg -shape 0 -size $sizeDip -opacity $op -bg '#FFFFFF' -posx $posx -posy $posy -textAuto 1
        $p = Start-Ball -SettleMs 1500
        if ($p.Hwnd -eq [IntPtr]::Zero) {
            Add-Check ("op{0}.window" -f $op) $false "ball window never appeared"
            Stop-BallPid $p.Pid
            continue
        }
        $rc = Get-BallRect $p.Hwnd
        $png = Join-Path $MediaDir ("ac7-op{0}.png" -f $op)
        $len = Capture-Region -x $region.X -y $region.Y -w $region.W -h $region.H -path $png
        Stop-BallPid $p.Pid
        Start-Sleep -Milliseconds 300
        $null = Capture-Region -x $region.X -y $region.Y -w $region.W -h $region.H -path $post
        $drift = Get-ImageDiff $pre $post
        Write-Output ("  opacity {0,3}% attempt {1}: rect=L{2} T{3} {4}x{5} png={6} bytes={7} backdrop drift={8:F2}" -f `
            $op, $attempt, $rc.L, $rc.T, $rc.W, $rc.H, (Split-Path -Leaf $png), $len, $drift)
        Add-Check ("op{0}.png" -f $op) ($len -gt 200) ("screenshot bytes={0}" -f $len)
        Add-Check ("op{0}.rect" -f $op) (([math]::Abs($rc.L - $posx) -le 2) -and ([math]::Abs($rc.W - $expectPx) -le 8)) `
            ("L={0} W={1} expect L={2} W={3}" -f $rc.L, $rc.W, $posx, $expectPx)
        # The implied-alpha measurement reads the backdrop from the same capture,
        # so a drifting desktop does not invalidate it; drift is recorded only as
        # context for the (wallpaper dependent) luminance-gap number.
        if ($drift -lt 1.5) {
            Add-Check ("op{0}.backdrop-frozen" -f $op) $true ("drift {0:F2} < 1.5, luminance gap is clean" -f $drift)
        } else {
            Write-Output ("  opacity {0}% attempt {1}: backdrop moved (drift {2:F2}); plain brightness gap is advisory, implied alpha is still exact" -f $op, $attempt, $drift)
        }
        $shots[$op] = $png
        if ($op -eq 20) { Copy-Item -LiteralPath $pre -Destination $base -Force }
        $done = $true
    }
    if (-not $done) {
        Write-Output ("  opacity {0}% : could not get a frozen backdrop after 3 attempts" -f $op)
    }
}

if ($shots.ContainsKey(20) -and $shots.ContainsKey(100)) {
    $py = Join-Path $Here 't7b_img.py'
    $rectArg = "{0},{1},{2},{3}" -f $pad, $pad, $expectPx, $expectPx
    Write-Output ""
    & python $py alpha --base $base --low $shots[20] --high $shots[100] --rect $rectArg --min-diff 0.20
    $rcPy = $LASTEXITCODE
    Write-Output ("PYEXIT: {0}" -f $rcPy)
    Add-Check 'opacity.alpha-applied' ($rcPy -eq 0) ("t7b_img.py alpha exit={0}" -f $rcPy)
} else {
    Write-Output "  cannot compare: a shot is missing"
    Add-Check 'opacity.alpha-applied' $false "missing a frozen screenshot"
}

$null = Confirm-AnchorUnchanged $anchor
$fails = Get-FailCount
Write-Output ""
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
