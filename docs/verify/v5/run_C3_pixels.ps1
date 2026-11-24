# run_C3_pixels.ps1 -- re-analyse the AC-6/AC-17 screenshots captured by run_C1.
# Why: run_C1 sampled the "desktop colour" 40 px away from the ball; on this
# wallpaper that patch is not the same colour as the desk right behind the ball,
# so the mask was wrong (circle reported inset 0).  Here the reference is the
# median of the four CORNER patches of the captured rect itself -- those corners
# are transparent pixels of the layered window, i.e. the desk behind the ball.
# No program is launched here; this only re-reads PNG evidence.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$VerifyD = Split-Path -Parent $PSScriptRoot      # ...\docs\verify
$MediaDir = Join-Path $VerifyD 'media-v5'

function Get-PatchMedian($bmp, [int]$x0, [int]$y0, [int]$n) {
    $rs = @(); $gs = @(); $bs = @()
    for ($y = $y0; $y -lt ($y0 + $n); $y++) {
        for ($x = $x0; $x -lt ($x0 + $n); $x++) {
            $c = $bmp.GetPixel($x, $y)
            $rs += $c.R; $gs += $c.G; $bs += $c.B
        }
    }
    $rs = $rs | Sort-Object; $gs = $gs | Sort-Object; $bs = $bs | Sort-Object
    $m = [int](($n * $n) / 2)
    return @($rs[$m], $gs[$m], $bs[$m])
}

function Analyze([string]$path, [int]$step = 1) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    $bmp = New-Object System.Drawing.Bitmap($path)
    $W = $bmp.Width; $H = $bmp.Height
    $n = 6
    $p1 = Get-PatchMedian $bmp 2 2 $n
    $p2 = Get-PatchMedian $bmp ($W - $n - 2) 2 $n
    $p3 = Get-PatchMedian $bmp 2 ($H - $n - 2) $n
    $p4 = Get-PatchMedian $bmp ($W - $n - 2) ($H - $n - 2) $n
    $br = [int](($p1[0] + $p2[0] + $p3[0] + $p4[0]) / 4)
    $bg = [int](($p1[1] + $p2[1] + $p3[1] + $p4[1]) / 4)
    $bb = [int](($p1[2] + $p2[2] + $p3[2] + $p4[2]) / 4)

    $inside = 0; $total = 0
    $minX = -1; $maxX = -1; $minY = -1; $maxY = -1
    $rows = @{}
    for ($y = 0; $y -lt $H; $y += $step) {
        $rowMin = -1; $rowMax = -1
        for ($x = 0; $x -lt $W; $x += $step) {
            $c = $bmp.GetPixel($x, $y)
            $d = [math]::Max([math]::Abs($c.R - $br), [math]::Max([math]::Abs($c.G - $bg), [math]::Abs($c.B - $bb)))
            $total++
            if ($d -gt 45) {
                $inside++
                if (($rowMin -lt 0) -or ($x -lt $rowMin)) { $rowMin = $x }
                if ($x -gt $rowMax) { $rowMax = $x }
                if (($minX -lt 0) -or ($x -lt $minX)) { $minX = $x }
                if ($x -gt $maxX) { $maxX = $x }
                if (($minY -lt 0) -or ($y -lt $minY)) { $minY = $y }
                if ($y -gt $maxY) { $maxY = $y }
            }
        }
        $rows[$y] = @($rowMin, $rowMax)
    }
    $bmp.Dispose()
    $cov = 0.0
    if ($total -gt 0) { $cov = [math]::Round($inside / $total, 3) }

    $probeRows = @()
    foreach ($f in @(0.10, 0.25, 0.50)) {
        $y = [int][math]::Round($H * $f)
        $hit = $null
        foreach ($k in $rows.Keys) { if ([math]::Abs($k - $y) -le $step) { $hit = $k; break } }
        if ($null -ne $hit) {
            $rr = $rows[$hit]
            $cw = 0
            if ($rr[0] -ge 0) { $cw = $rr[1] - $rr[0] + 1 }
            $probeRows += [pscustomobject]@{ Frac = $f; Y = $hit; Left = $rr[0]; Right = $rr[1]; Width = $cw; Inset = $(if ($rr[0] -ge 0) { [math]::Round($rr[0] / $W, 3) } else { -1 }) }
        }
    }
    return [pscustomobject]@{
        Path = $path; W = $W; H = $H; Bg = "$br,$bg,$bb"; Coverage = $cov
        Bbox = "$minX,$minY..$maxX,$maxY"; Rows = $probeRows
        BboxW = ($maxX - $minX + 1); BboxH = ($maxY - $minY + 1)
    }
}

$script:Checks = New-Object System.Collections.ArrayList
function Add-Check([string]$name, [bool]$ok, [string]$detail) {
    [void]$script:Checks.Add([pscustomobject]@{ Name = $name; Ok = $ok })
    $tag = 'FAIL'; if ($ok) { $tag = 'PASS' }
    Write-Output ("CHECK: {0} {1}  ({2})" -f $tag, $name, $detail)
}

Write-Output "=========== T14 AC-6 / AC-17 pixel re-analysis (corner-reference mask) ==========="
Write-Output ""

$shapes = @(
    @{ File = 'c1-shape-0.png'; Tag = 'circle'; Cov = 0.785; Inset10 = 0.200 },
    @{ File = 'c1-shape-1.png'; Tag = 'rounded-square'; Cov = 0.954; Inset10 = 0.050 },
    @{ File = 'c1-shape-2.png'; Tag = 'capsule'; Cov = 0.857; Inset10 = 0.133 }
)
foreach ($s in $shapes) {
    $a = Analyze (Join-Path $MediaDir $s.File) 1
    if ($null -eq $a) { Add-Check ("PIx." + $s.Tag) $false ("{0} missing" -f $s.File); continue }
    Write-Output ("  {0}: {1}x{2} bg=({3}) coverage={4} bbox={5}" -f $s.Tag, $a.W, $a.H, $a.Bg, $a.Coverage, $a.Bbox)
    foreach ($r in $a.Rows) { Write-Output ("    y={0} ({1}%) left={2} right={3} width={4} inset={5}" -f $r.Y, [math]::Round($r.Frac * 100), $r.Left, $r.Right, $r.Width, $r.Inset) }
    $i10 = -1.0
    foreach ($r in $a.Rows) { if ($r.Frac -eq 0.10) { $i10 = $r.Inset } }
    Add-Check ("PIX." + $s.Tag + ".coverage") ([math]::Abs($a.Coverage - $s.Cov) -le 0.04) ("coverage {0} vs analytic {1} (tol 0.04)" -f $a.Coverage, $s.Cov)
    Add-Check ("PIX." + $s.Tag + ".inset10") ([math]::Abs($i10 - $s.Inset10) -le 0.04) ("10%-height inset {0} vs analytic {1} (tol 0.04)" -f $i10, $s.Inset10)
    Add-Check ("PIX." + $s.Tag + ".bbox-full") (($a.BboxW -eq $a.W) -and ($a.BboxH -eq $a.H)) ("ink bbox {0}x{1} fills the window {2}x{3}" -f $a.BboxW, $a.BboxH, $a.W, $a.H)
}

Write-Output ""
foreach ($f in @('c1-size48.png','c1-size160.png','c1-capsule160.png')) {
    $a = Analyze (Join-Path $MediaDir $f) 2
    if ($null -eq $a) { Add-Check ("PIX." + $f) $false "missing"; continue }
    $i50 = -1.0
    foreach ($r in $a.Rows) { if ($r.Frac -eq 0.50) { $i50 = $r.Inset } }
    Write-Output ("  {0}: {1}x{2} bg=({3}) coverage={4} bbox={5} mid-inset={6}" -f $f, $a.W, $a.H, $a.Bg, $a.Coverage, $a.Bbox, $i50)
    Add-Check ("PIX." + $f + ".ink") ($a.Coverage -gt 0.30) ("ink coverage {0} > 0.30" -f $a.Coverage)
}

$fails = @($script:Checks | Where-Object { -not $_.Ok }).Count
Write-Output ""
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
