# run_C5_inspect.ps1 -- forensic read of the c4-*-on / c4-*-off pairs.
# Prints diff values at the four corners, the centre and a horizontal slice, so
# we can tell a real silhouette from frame noise / a stale "hidden" frame.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$MediaDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'media-v5'

function D($a, $b, [int]$x, [int]$y) {
    $p = $a.GetPixel($x, $y); $q = $b.GetPixel($x, $y)
    return [math]::Max([math]::Abs($p.R - $q.R), [math]::Max([math]::Abs($p.G - $q.G), [math]::Abs($p.B - $q.B)))
}

foreach ($tag in @('circle-88','roundedsq-88','capsule-88','circle-48','circle-160','capsule-160')) {
    $pa = Join-Path $MediaDir ("c4-" + $tag + "-on.png")
    $pb = Join-Path $MediaDir ("c4-" + $tag + "-off.png")
    if (-not (Test-Path -LiteralPath $pa)) { Write-Output ("{0}: MISSING" -f $tag); continue }
    $a = New-Object System.Drawing.Bitmap($pa)
    $b = New-Object System.Drawing.Bitmap($pb)
    $W = $a.Width; $H = $a.Height
    Write-Output ""
    Write-Output ("== {0}  {1}x{2}" -f $tag, $W, $H)
    Write-Output ("  corner diffs: TL={0} TR={1} BL={2} BR={3}  centre={4}" -f `
        (D $a $b 2 2), (D $a $b ($W-3) 2), (D $a $b 2 ($H-3)), (D $a $b ($W-3) ($H-3)), (D $a $b ([int]($W/2)) ([int]($H/2))))
    $on = $a.GetPixel(2,2); $off = $b.GetPixel(2,2)
    Write-Output ("  TL on=({0},{1},{2}) off=({3},{4},{5})" -f $on.R,$on.G,$on.B,$off.R,$off.G,$off.B)
    $on2 = $a.GetPixel([int]($W/2), [int]($H/2)); $off2 = $b.GetPixel([int]($W/2), [int]($H/2))
    Write-Output ("  C  on=({0},{1},{2}) off=({3},{4},{5})" -f $on2.R,$on2.G,$on2.B,$off2.R,$off2.G,$off2.B)
    $yRow = [int][math]::Round($H * 0.10)
    $s = ""
    for ($x = 0; $x -lt [math]::Min(46, $W); $x++) {
        $d = D $a $b $x $yRow
        if ($d -gt 25) { $s += "#" } else { $s += "." }
    }
    Write-Output ("  row y={0} mask(x=0..45) = {1}" -f $yRow, $s)
    # coverage at several thresholds
    foreach ($th in @(10, 25, 60)) {
        $in = 0; $tot = 0
        $step = 1
        if ($W -gt 300) { $step = 2 }
        for ($y = 0; $y -lt $H; $y += $step) {
            for ($x = 0; $x -lt $W; $x += $step) {
                $tot++
                if ((D $a $b $x $y) -gt $th) { $in++ }
            }
        }
        Write-Output ("    coverage@thr{0} = {1}" -f $th, [math]::Round($in / $tot, 3))
    }
    $a.Dispose(); $b.Dispose()
}
Write-Output ""
Write-Output "RESULT: INFO"
