# run_C6_shape_profile.ps1 -- row-profile of the three size-88 silhouettes.
# Uses the saved on/off pairs with a high diff threshold (60) so wallpaper
# dither cannot leak into the mask.  Prints the left/right edge per row and the
# area integrated from the rows, which is the coverage that actually matters.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$MediaDir = Join-Path (Split-Path -Parent $PSScriptRoot) 'media-v5'

function D($a, $b, [int]$x, [int]$y) {
    $p = $a.GetPixel($x, $y); $q = $b.GetPixel($x, $y)
    return [math]::Max([math]::Abs($p.R - $q.R), [math]::Max([math]::Abs($p.G - $q.G), [math]::Abs($p.B - $q.B)))
}

foreach ($tag in @('circle-88','roundedsq-88','capsule-88')) {
    $a = New-Object System.Drawing.Bitmap((Join-Path $MediaDir ("c4-" + $tag + "-on.png")))
    $b = New-Object System.Drawing.Bitmap((Join-Path $MediaDir ("c4-" + $tag + "-off.png")))
    $W = $a.Width; $H = $a.Height
    Write-Output ""
    Write-Output ("== {0} {1}x{2}" -f $tag, $W, $H)
    $area = 0
    $rows = @{}
    for ($y = 0; $y -lt $H; $y++) {
        $l = -1; $r = -1
        for ($x = 0; $x -lt $W; $x++) {
            if ((D $a $b $x $y) -gt 60) { if ($l -lt 0) { $l = $x }; $r = $x }
        }
        $rows[$y] = @($l, $r)
        if ($l -ge 0) { $area += ($r - $l + 1) }
    }
    foreach ($y in @(0, 8, 15, 30, 46, 77, 107, 123, 138, 145, 153)) {
        if ($y -lt $H) {
            $rr = $rows[$y]
            $w = 0; if ($rr[0] -ge 0) { $w = $rr[1] - $rr[0] + 1 }
            Write-Output ("  y={0,3} ({1,4}%) left={2,3} right={3,3} width={4,3}" -f $y, [math]::Round(100.0 * $y / $H, 1), $rr[0], $rr[1], $w)
        }
    }
    $cov = [math]::Round($area / ($W * $H), 3)
    Write-Output ("  integrated area = {0} px -> coverage {1}" -f $area, $cov)
    $a.Dispose(); $b.Dispose()
}
Write-Output ""
Write-Output "RESULT: INFO"
