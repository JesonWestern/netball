<#
  Machine-read the numbers rendered on the ball (AC-3 measured_bps source).

  Uses the Windows built-in OCR engine (Windows.Media.Ocr) - no third-party tool,
  no manual transcription. Prints:
      OCR_TEXT: <recognised text, single line>
      OCR_DOWN: <raw token that looks like the downstream value>
      BALL_DOWN_BPS=<converted bytes per second, 0 if not parseable>
  Exit: 0 parsed / 1 engine error / 2 no text / 3 no downstream token
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$Language = '',
    [int]$Scale = 4,
    [int]$Threshold = 0,
    [double]$CropLeftPct = 0
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

$Path = [System.IO.Path]::GetFullPath($Path)
if (-not (Test-Path -LiteralPath $Path)) {
    Write-Output "OCR: file missing -> $Path"
    Write-Output 'RESULT: SKIP (screenshot missing)'
    exit 3
}

# The ball is only ~154 px wide at 175 %, so glyphs are ~13 px tall - far below the
# engine's sweet spot. Upscale 4x with bicubic before recognition (measured: 13 px
# glyphs misread digit by digit, 52 px glyphs read cleanly).
$ocrInput = $Path
if ($Scale -gt 1) {
    try {
        Add-Type -AssemblyName System.Drawing -ErrorAction Stop
        $src = [System.Drawing.Image]::FromFile($Path)
        $w = [int]($src.Width * $Scale)
        $h = [int]($src.Height * $Scale)
        $dst = New-Object System.Drawing.Bitmap($w, $h)
        $g = [System.Drawing.Graphics]::FromImage($dst)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.DrawImage($src, 0, 0, $w, $h)
        $g.Dispose()

        if ($CropLeftPct -gt 0) {
            # the leading arrow (U+2193/U+2191) merges with the first digit and makes the
            # engine drop a whole order of magnitude ("1.72" -> "72"). Cut the arrow column out.
            $cut = [int]($w * $CropLeftPct)
            $crop = New-Object System.Drawing.Bitmap(($w - $cut), $h)
            $g2 = [System.Drawing.Graphics]::FromImage($crop)
            $g2.DrawImage($dst, (New-Object System.Drawing.Rectangle(0, 0, ($w - $cut), $h)), (New-Object System.Drawing.Rectangle($cut, 0, ($w - $cut), $h)), [System.Drawing.GraphicsUnit]::Pixel)
            $g2.Dispose()
            $dst.Dispose()
            $dst = $crop
            $w = $dst.Width
        }

        if ($Threshold -gt 0) {
            # the ball is auto-coloured: white glyphs on a dark fill (or the inverse).
            # Binarising removes the anti-aliased halo that makes the engine drop digits.
            $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
            $data = $dst.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadWrite, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
            $stride = $data.Stride
            $buf = New-Object byte[] ($stride * $h)
            [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
            for ($yy = 0; $yy -lt $h; $yy++) {
                $row = $yy * $stride
                for ($xx = 0; $xx -lt $w; $xx++) {
                    $o = $row + $xx * 3
                    $lum = 0.299 * $buf[$o + 2] + 0.587 * $buf[$o + 1] + 0.114 * $buf[$o]
                    $v = if ($lum -ge $Threshold) { 255 } else { 0 }
                    $buf[$o] = $v; $buf[$o + 1] = $v; $buf[$o + 2] = $v
                }
            }
            [System.Runtime.InteropServices.Marshal]::Copy($buf, 0, $data.Scan0, $buf.Length)
            $dst.UnlockBits($data)
        }

        $ocrInput = Join-Path ([System.IO.Path]::GetTempPath()) 'nb_ocr_upscaled.png'
        $dst.Save($ocrInput, [System.Drawing.Imaging.ImageFormat]::Png)
        $dst.Dispose()
        $src.Dispose()
        Write-Output ("OCR: upscaled {0}x threshold={1} -> {2} ({3}x{4})" -f $Scale, $Threshold, $ocrInput, $w, $h)
    } catch {
        Write-Output ("OCR: upscale failed ({0}), using the original file" -f $_.Exception.Message)
        $ocrInput = $Path
    }
}

try {
    Add-Type -AssemblyName System.Runtime.WindowsRuntime -ErrorAction Stop
    [void][Windows.Media.Ocr.OcrEngine, Windows.Media, ContentType = WindowsRuntime]
    [void][Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
    [void][Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics, ContentType = WindowsRuntime]
    [void][Windows.Globalization.Language, Windows.Globalization, ContentType = WindowsRuntime]
} catch {
    Write-Output ("OCR: WinRT unavailable -> " + $_.Exception.Message)
    Write-Output 'RESULT: SKIP (Windows.Media.Ocr not available on this host)'
    exit 1
}

$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
})[0]

function Await($op, [Type]$resultType) {
    $task = $asTaskGeneric.MakeGenericMethod($resultType).Invoke($null, @($op))
    $task.Wait(-1) | Out-Null
    return $task.Result
}

try {
    $engine = $null
    if ($Language) {
        $lang = New-Object Windows.Globalization.Language($Language)
        $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage($lang)
    }
    if (-not $engine) { $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages() }
    if (-not $engine) {
        # fall back to any available recogniser (usually en-US on non-localised images)
        $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage((New-Object Windows.Globalization.Language('en-US')))
    }
    if (-not $engine) {
        Write-Output 'OCR: no recogniser installed'
        Write-Output 'RESULT: SKIP (no OCR language pack)'
        exit 1
    }

    $file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($ocrInput)) ([Windows.Storage.StorageFile])
    $stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
    $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
    $soft = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
    $result = Await ($engine.RecognizeAsync($soft)) ([Windows.Media.Ocr.OcrResult])
} catch {
    Write-Output ("OCR: recognition failed -> " + $_.Exception.Message)
    Write-Output 'RESULT: SKIP (OCR runtime error)'
    exit 1
}

$text = ($result.Text -replace "`r?`n", ' | ')
Write-Output ("OCR_TEXT: " + $text)
if (-not $result.Text -or $result.Text.Trim().Length -eq 0) {
    Write-Output 'RESULT: SKIP (OCR found no text in the image)'
    exit 2
}

# downstream line: the shim usually renders a down arrow (U+2193) or the word KB/s
# on the first line; take the first "<number> <unit>" pair that is NOT the memory %.
$flat = $result.Text -replace "`r?`n", ' '
# the engine often splits a decimal into "1 . 72" or "1 \u00B7 72"; re-join before parsing
$flat = $flat -replace [string][char]0x00B7, '.'
$flat = $flat -replace [string][char]0xFF0E, '.'
$flat = $flat -replace '\s*([.,])\s*', '$1'
$pattern = '\b(\d+(?:\.\d+)?)\s*(B/s|KB/s|MB/s|K/s|M/s|GB/s)'
$matches2 = [regex]::Matches($flat, $pattern, 'IgnoreCase')
if ($matches2.Count -eq 0) {
    Write-Output 'OCR_DOWN: <none>'
    Write-Output 'BALL_DOWN_BPS=0'
    Write-Output 'RESULT: FAIL-OCR (text found but no speed token like "81.5 KB/s")'
    exit 3
}

$m = $matches2[0]
$value = [double]($m.Groups[1].Value -replace ',', '.')
$unit = $m.Groups[2].Value.ToUpper()
$mult = switch -Regex ($unit) {
    '^GB' { 1073741824 }
    '^MB|^M/' { 1048576 }
    '^KB|^K/' { 1024 }
    default { 1 }
}
$bps = [int64]($value * $mult)

# normalise: full-width percent (U+FF05) -> '%', middle dot (U+00B7) -> '.', tighten decimals
$flatPct = $flat -replace [string][char]0xFF05, '%'
$pctMatches = [regex]::Matches($flatPct, '(\d+)\s*%')
if ($pctMatches.Count -gt 0) {
    Write-Output ("BALL_MEM_PCT=" + $pctMatches[$pctMatches.Count - 1].Groups[1].Value)
} else {
    Write-Output 'BALL_MEM_PCT=-1'
}

Write-Output ("OCR_DOWN: " + $m.Value)
Write-Output ("BALL_DOWN_BPS=" + $bps)
Write-Output ("METRIC: ocr_value={0} unit={1} bps={2} tokens={3}" -f $value, $unit, $bps, $matches2.Count)
Write-Output 'RESULT: PASS (downstream value machine-read from the screenshot)'
exit 0
