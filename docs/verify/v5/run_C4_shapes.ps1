# run_C4_shapes.ps1 -- AC-6 / AC-17 silhouette measured by frame differencing.
# Method: capture the ball rect while the ball is visible, hide it with the
# product's own "show or hide" command (WM_COMMAND 1010), capture the same rect
# again, and diff the two frames.  The pixels that changed are exactly the ball
# silhouette -- no wallpaper guesswork, no "desk colour" reference sample.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\t7b\nb.ps1"

$MediaDir = Join-Path $VerifyD 'media-v5'
$TmpDir   = Join-Path $Root 'build\_tmp\v5'
$ExeCopy  = Join-Path $TmpDir 'NetBall_v5.exe'
$ProcName = 'NetBall_v5'
foreach ($d in @($MediaDir, $TmpDir)) {
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}

Add-Type -AssemblyName System.Drawing

function Grab([int]$x, [int]$y, [int]$w, [int]$h) {
    $bmp = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try { $g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size($w, $h)), [System.Drawing.CopyPixelOperation]::SourceCopy) }
    finally { $g.Dispose() }
    return $bmp
}

function DiffStats($bmpA, $bmpB, [int]$step = 1) {
    $W = $bmpA.Width; $H = $bmpA.Height
    $inside = 0; $total = 0
    $minX = -1; $maxX = -1; $minY = -1; $maxY = -1
    $chord = @{}
    for ($y = 0; $y -lt $H; $y += $step) {
        $rowMin = -1; $rowMax = -1
        for ($x = 0; $x -lt $W; $x += $step) {
            $a = $bmpA.GetPixel($x, $y); $b = $bmpB.GetPixel($x, $y)
            $d = [math]::Max([math]::Abs($a.R - $b.R), [math]::Max([math]::Abs($a.G - $b.G), [math]::Abs($a.B - $b.B)))
            $total++
            if ($d -gt 25) {
                $inside++
                if (($rowMin -lt 0) -or ($x -lt $rowMin)) { $rowMin = $x }
                if ($x -gt $rowMax) { $rowMax = $x }
                if (($minX -lt 0) -or ($x -lt $minX)) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if (($minY -lt 0) -or ($y -lt $minY)) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
        if ($rowMin -ge 0) { $chord[$y] = @($rowMin, $rowMax) }
    }
    $cov = 0.0
    if ($total -gt 0) { $cov = [math]::Round($inside / $total, 3) }
    return [pscustomobject]@{
        Coverage = $cov; MinX = $minX; MaxX = $maxX; MinY = $minY; MaxY = $maxY
        BboxW = ($maxX - $minX + 1); BboxH = ($maxY - $minY + 1); Chord = $chord; W = $W; H = $H
    }
}

function ChordInset($st, [double]$frac) {
    $yTarget = [int][math]::Round($st.H * $frac)
    $best = $null
    foreach ($k in $st.Chord.Keys) { if ([math]::Abs($k - $yTarget) -le 1) { $best = $k; break } }
    if ($null -eq $best) { return -1.0 }
    $r = $st.Chord[$best]
    return [math]::Round($r[0] / $st.W, 3)
}

Write-Output "=========== T14 AC-6 / AC-17 : silhouette via frame differencing ==========="
foreach ($lp in @(Get-Process -Name $ProcName -ErrorAction SilentlyContinue)) {
    Write-Output ("  cleanup: killing leftover pid={0}" -f $lp.Id)
    try { $lp.Kill(); [void]$lp.WaitForExit(4000) } catch {}
}
$anchor = Initialize-Measurement -WaitMs 8000
Write-Output ("  anchor sha = {0} bytes={1}" -f $anchor.Sha, $anchor.Bytes)

$cases = @(
    @{ Tag = 'circle-88';        Shape = 0; Size = 88;  W = 154; H = 154; Cov = 0.785; Inset = 0.200; Step = 1 },
    @{ Tag = 'roundedsq-88';     Shape = 1; Size = 88;  W = 154; H = 154; Cov = 0.954; Inset = 0.050; Step = 1 },
    @{ Tag = 'capsule-88';       Shape = 2; Size = 88;  W = 231; H = 154; Cov = 0.857; Inset = 0.133; Step = 1 },
    @{ Tag = 'circle-48';        Shape = 0; Size = 48;  W = 84;  H = 84;  Cov = 0.785; Inset = 0.200; Step = 1 },
    @{ Tag = 'circle-160';       Shape = 0; Size = 160; W = 280; H = 280; Cov = 0.785; Inset = 0.200; Step = 2 },
    @{ Tag = 'capsule-160';      Shape = 2; Size = 160; W = 420; H = 280; Cov = 0.857; Inset = 0.133; Step = 2 }
)

foreach ($c in $cases) {
    Write-Output ""
    Write-Output ("--- {0}: shape={1} size={2} (expect {3}x{4}) ---" -f $c.Tag, $c.Shape, $c.Size, $c.W, $c.H)
    Write-Cfg -shape $c.Shape -size $c.Size -opacity 100 -bg '#00FF00' -posx 900 -posy 400
    $p = Start-Ball -SettleMs 1700
    if ($p.Hwnd -eq [IntPtr]::Zero) { Add-Check ("SIL." + $c.Tag) $false "ball window never appeared"; continue }
    $r = Get-BallRect $p.Hwnd
    Write-Output ("  rect L={0} T={1} {2}x{3}" -f $r.L, $r.T, $r.W, $r.H)
    Start-Sleep -Milliseconds 400
    $bmpBall = Grab $r.L $r.T $r.W $r.H
    $pngOn = Join-Path $MediaDir ("c4-" + $c.Tag + "-on.png")
    $bmpBall.Save($pngOn, [System.Drawing.Imaging.ImageFormat]::Png)
    Send-Cmd $p.Hwnd 1010                      # hide
    Start-Sleep -Milliseconds 600
    $vis = [NBT7b]::IsWindowVisible($p.Hwnd)
    $bmpDesk = Grab $r.L $r.T $r.W $r.H
    $pngOff = Join-Path $MediaDir ("c4-" + $c.Tag + "-off.png")
    $bmpDesk.Save($pngOff, [System.Drawing.Imaging.ImageFormat]::Png)
    Send-Cmd $p.Hwnd 1010                      # show again
    Start-Sleep -Milliseconds 300
    $st = DiffStats $bmpBall $bmpDesk $c.Step
    $bmpBall.Dispose(); $bmpDesk.Dispose()
    $i10 = ChordInset $st 0.10
    Write-Output ("  hidden={0} coverage={1} bbox={2},{3}..{4},{5} ({6}x{7}) inset@10%={8}" -f (-not $vis), $st.Coverage, $st.MinX, $st.MinY, $st.MaxX, $st.MaxY, $st.BboxW, $st.BboxH, $i10)
    Add-Check ("SIL." + $c.Tag + ".geometry") (($r.W -eq $c.W) -and ($r.H -eq $c.H)) ("window {0}x{1} (expect {2}x{3})" -f $r.W, $r.H, $c.W, $c.H)
    Add-Check ("SIL." + $c.Tag + ".silhouette-fills") (($st.BboxW -ge $c.W - 2) -and ($st.BboxH -ge $c.H - 2)) ("ink spans {0}x{1} of {2}x{3}" -f $st.BboxW, $st.BboxH, $c.W, $c.H)
    Add-Check ("SIL." + $c.Tag + ".coverage") ([math]::Abs($st.Coverage - $c.Cov) -le 0.04) ("coverage {0} vs analytic {1} (tol 0.04)" -f $st.Coverage, $c.Cov)
    Add-Check ("SIL." + $c.Tag + ".inset10") ([math]::Abs($i10 - $c.Inset) -le 0.04) ("10%-height inset {0} vs analytic {1} (tol 0.04)" -f $i10, $c.Inset)
    Stop-BallPid $p.Pid
    $null = Wait-NoBall 5000
}

Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
Write-Output ""
Write-Output ("  config restored: posx={0} posy={1}" -f (Read-Cfg).posx, (Read-Cfg).posy)
Write-Output ("  processes left: {0}" -f (Get-ProcList).Count)
$fails = Get-FailCount
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
