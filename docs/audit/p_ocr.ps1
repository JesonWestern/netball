# auditor / T10 - minimal Windows.Media.Ocr wrapper (instrument only)
param([string]$Image, [double]$Scale = 6.0)
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
})[0]

function Await($op, $resultType) {
    $m = $asTaskGeneric.MakeGenericMethod($resultType)
    $t = $m.Invoke($null, @($op))
    [void]$t.Wait(-1)
    $t.Result
}

[void][Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
[void][Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType = WindowsRuntime]
[void][Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime]

$full = (Resolve-Path $Image).Path

# upscale via System.Drawing to help small glyph recognition
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Image]::FromFile($full)
$w = [int]($src.Width * $Scale); $h = [int]($src.Height * $Scale)
$big = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($big)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.DrawImage($src, 0, 0, $w, $h)
$g.Dispose(); $src.Dispose()
$tmp = Join-Path $env:TEMP ('ocr_' + [guid]::NewGuid().ToString('N') + '.png')
$big.Save($tmp, [System.Drawing.Imaging.ImageFormat]::Png)
$big.Dispose()

$sf = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($tmp)) ([Windows.Storage.StorageFile])
$stream = Await ($sf.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
$decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
$bmp = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])

$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
if ($null -eq $engine) { Write-Output 'OCR_ENGINE=NULL'; exit 2 }
$result = Await ($engine.RecognizeAsync($bmp)) ([Windows.Media.Ocr.OcrResult])
$txt = ($result.Lines | ForEach-Object { $_.Text }) -join ' || '
$cp = (($txt.ToCharArray() | Where-Object { [int]$_ -gt 127 } | ForEach-Object { 'U+{0:X4}' -f [int]$_ }) -join ' ')
Write-Output ('OCR_LANG=' + $engine.RecognizerLanguage.LanguageTag)
Write-Output ('OCR_TEXT=' + $txt)
Write-Output ('OCR_NONASCII_CP=' + $cp)
Remove-Item $tmp -Force -ErrorAction SilentlyContinue
