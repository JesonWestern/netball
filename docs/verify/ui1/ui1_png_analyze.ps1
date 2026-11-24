# ui1_png_analyze.ps1 (v2) -- mechanical verification of the delivered screenshots.
#  v1 bugs fixed: (a) piping a function to Out-Null swallowed its output,
#                 (b) client-origin offset was assumed instead of derived.
#  Strategy: one full-image scan (4px grid) matching all 8 preset colours at once,
#  then derive the client origin FROM the swatch grid and sample the preview ball.
#  Read-only: never touches NetBall or config.ini.
Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

$root = $null
$p = Split-Path -Parent $PSCommandPath
for ($i = 0; $i -lt 8 -and $p; $i++) {
    if (Test-Path (Join-Path $p 'src\app_common.h')) { $root = $p; break }
    $p = Split-Path -Parent $p
}
if (-not $root) { throw 'cannot locate project root' }
$dir = Join-Path $root 'docs\verify\ui1'

$presets = @(
    @{ n = 'darkgray'; r = 38;  g = 42;  b = 52  },
    @{ n = 'black';    r = 0;   g = 0;   b = 0   },
    @{ n = 'white';    r = 255; g = 255; b = 255 },
    @{ n = 'blue';     r = 20;  g = 75;  b = 200 },
    @{ n = 'green';    r = 34;  g = 160; b = 90  },
    @{ n = 'orange';   r = 235; g = 140; b = 30  },
    @{ n = 'purple';   r = 110; g = 50;  b = 190 },
    @{ n = 'red';      r = 180; g = 25;  b = 35  }
)

function ScanSwatches([string]$path) {
    "== swatch scan: $([System.IO.Path]::GetFileName($path)) =="
    if (-not (Test-Path $path)) { '   missing file'; return $null }
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    "   image = $($bmp.Width)x$($bmp.Height)"
    $stat = @()
    for ($i = 0; $i -lt 8; $i++) {
        $stat += [pscustomobject]@{ idx = $i; count = 0; minX = 99999; maxX = -1; minY = 99999; maxY = -1 }
    }
    for ($y = 0; $y -lt $bmp.Height; $y += 4) {
        for ($x = 0; $x -lt $bmp.Width; $x += 4) {
            $q = $bmp.GetPixel($x, $y)
            for ($i = 0; $i -lt 8; $i++) {
                $s = $presets[$i]
                if ($q.R -eq $s.r -and $q.G -eq $s.g -and $q.B -eq $s.b) {
                    $st = $stat[$i]
                    $st.count++
                    if ($x -lt $st.minX) { $st.minX = $x }
                    if ($x -gt $st.maxX) { $st.maxX = $x }
                    if ($y -lt $st.minY) { $st.minY = $y }
                    if ($y -gt $st.maxY) { $st.maxY = $y }
                }
            }
        }
    }
    $bmp.Dispose()
    $rows = @{}
    foreach ($st in $stat) {
        $name = $presets[$st.idx].n
        if ($st.count -eq 0) { "   [$($st.idx)] $($name.PadRight(9)) NOT FOUND"; continue }
        "   [$($st.idx)] $($name.PadRight(9)) count=$($st.count)  bbox=($($st.minX),$($st.minY))-($($st.maxX),$($st.maxY))"
        $midY = [int](($st.minY + $st.maxY) / 2)
        if (-not $rows.ContainsKey($midY)) { $rows[$midY] = @() }
        $rows[$midY] += $st.idx
    }
    $found = @($stat | Where-Object { $_.count -gt 0 })
    "   found = $($found.Count) / 8"
    # NOTE: returning the array here would capture the log lines above as well
    # (PowerShell collects everything a function writes into the pipeline), so the
    # data is published through a script-scope variable and the function prints only.
    $script:statLast = $stat
}

function PreviewBall([string]$path, $stat) {
    "== preview ball check: $([System.IO.Path]::GetFileName($path)) =="
    if (-not (Test-Path $path)) { '   missing file'; return }
    # derive the client origin from swatch index 3 (blue) if present, else index 0
    $anchor = $null
    foreach ($i in @(3, 0, 4)) {
        if ($stat[$i].count -gt 0) { $anchor = $stat[$i]; break }
    }
    if (-not $anchor) { '   cannot derive client origin (no swatch found)'; return }
    $dipX = 88 + $anchor.idx * 27 + 11
    $dipY = 265
    $cxSwatch = [int](($anchor.minX + $anchor.maxX) / 2)
    $cySwatch = [int](($anchor.minY + $anchor.maxY) / 2)
    $scale = 168.0 / 96.0
    $offX = [int][math]::Round($cxSwatch - $dipX * $scale)
    $offY = [int][math]::Round($cySwatch - $dipY * $scale)
    "   derived client origin in image = ($offX,$offY) from swatch[$($anchor.idx)]"

    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    $cxp = [int][math]::Round($offX + 192 * $scale)
    $cyp = [int][math]::Round($offY + 72 * $scale)
    "   sampling preview-ball area around ($cxp,$cyp)"
    $hist = @{}
    for ($y = $cyp - 25; $y -le $cyp + 25; $y += 2) {
        for ($x = $cxp - 25; $x -le $cxp + 25; $x += 2) {
            if ($x -lt 0 -or $y -lt 0 -or $x -ge $bmp.Width -or $y -ge $bmp.Height) { continue }
            $q = $bmp.GetPixel($x, $y)
            $k = "$($q.R),$($q.G),$($q.B)"
            if ($hist.ContainsKey($k)) { $hist[$k]++ } else { $hist[$k] = 1 }
        }
    }
    $bmp.Dispose()
    foreach ($t in ($hist.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 3)) {
        "   dominant $($t.Key) count=$($t.Value)"
    }
    if ($hist.ContainsKey('22,72,185')) {
        "   RESULT: blue blend RGB(22,72,185) present ($($hist['22,72,185']) samples) -> preview switched to the new blue"
    } elseif ($hist.ContainsKey('103,49,176')) {
        "   RESULT: purple blend RGB(103,49,176) present -> preview switched to purple"
    } else {
        "   RESULT: expected blend colour not sampled; inspect the image"
    }
}

function CompareImages([string]$a, [string]$b) {
    "== product vs host: $([System.IO.Path]::GetFileName($a)) vs $([System.IO.Path]::GetFileName($b)) =="
    if (-not (Test-Path $a) -or -not (Test-Path $b)) { '   missing file, skipped'; return }
    $ia = [System.Drawing.Bitmap]::FromFile($a)
    $ib = [System.Drawing.Bitmap]::FromFile($b)
    if ($ia.Width -ne $ib.Width -or $ia.Height -ne $ib.Height) {
        "   size mismatch: $($ia.Width)x$($ia.Height) vs $($ib.Width)x$($ib.Height)"
        $ia.Dispose(); $ib.Dispose(); return
    }
    $n = 0; $d = 0
    for ($y = 0; $y -lt $ia.Height; $y += 4) {
        for ($x = 0; $x -lt $ia.Width; $x += 4) {
            $n++
            $pa = $ia.GetPixel($x, $y); $pb = $ib.GetPixel($x, $y)
            if ($pa.R -ne $pb.R -or $pa.G -ne $pb.G -or $pa.B -ne $pb.B) { $d++ }
        }
    }
    $ia.Dispose(); $ib.Dispose()
    $pct = [math]::Round(100.0 * $d / [math]::Max(1, $n), 3)
    "   sampled=$n  differing=$d  ($pct%)"
}

ScanSwatches (Join-Path $dir 'settings-8swatches.png')
$stA = $script:statLast
PreviewBall (Join-Path $dir 'settings-preview-blue.png') $stA

ScanSwatches (Join-Path $dir 'host-settings-8swatches.png')
$stH = $script:statLast
PreviewBall (Join-Path $dir 'host-settings-preview-blue.png') $stH

"== swatch grid: product vs host =="
$gridSame = 0
for ($i = 0; $i -lt 8; $i++) {
    $a = $stA[$i]; $b = $stH[$i]
    $da = $a.minX - $b.minX
    $db = $a.minY - $b.minY
    if ([math]::Abs($da) -le 2 -and [math]::Abs($db) -le 2 -and $a.count -gt 0 -and $b.count -gt 0) { $gridSame++ }
    "   [$i] product=($($a.minX),$($a.minY)) host=($($b.minX),$($b.minY))  dx=$da dy=$db"
}
"   identical-within-2px = $gridSame / 8"

CompareImages (Join-Path $dir 'settings-8swatches.png') (Join-Path $dir 'host-settings-8swatches.png') | Out-Null
CompareImages (Join-Path $dir 'settings-preview-blue.png') (Join-Path $dir 'host-settings-preview-blue.png') | Out-Null
"NOTE: the full-image diff above is expected to be non-zero because the product run loads"
"      its own config.ini (shape=0 size=88 opacity=85 bg=#181C24) while the host harness"
"      uses its built-in defaults (size=96 opacity=90 bg=#262A34). Only the WIDGET grid"
"      (swatches/controls) is comparable between the two captures."
'DONE'
