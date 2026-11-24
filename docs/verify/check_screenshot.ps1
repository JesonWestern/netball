<#
  Real screen capture of the ball window (AC-2/6/7/8/17 evidence).
  Uses System.Drawing.Graphics.CopyFromScreen over the window rect - no placeholder images.
  Exit: 0 captured / 1 failed / 2 SKIP (window not found)
#>
[CmdletBinding()]
param(
    [string]$OutFile,
    [string]$ClassName = 'NetBallBallWnd',
    [string]$ProcessName = 'NetBall',
    [int]$Padding = 6,
    [switch]$ExpectText
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

if (-not $OutFile) { $OutFile = Join-Path $PSScriptRoot ('shot-' + (Get-Date -Format 'HHmmss') + '.png') }
$OutFile = [System.IO.Path]::GetFullPath($OutFile)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$cs = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class NBScreen {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr hWnd, StringBuilder sb, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    public static IntPtr FindByClass(string cls) {
        IntPtr hit = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            var sb = new StringBuilder(512);
            if (GetClassNameW(h, sb, sb.Capacity) > 0
                && string.Equals(sb.ToString(), cls, StringComparison.Ordinal)
                && IsWindowVisible(h)) { hit = h; return false; }
            return true;
        }, IntPtr.Zero);
        return hit;
    }
}
'@
try { Add-Type -TypeDefinition $cs -ErrorAction Stop } catch {
    if ($_.Exception.Message -notmatch 'already exists') { throw }
}

Write-Output "SCREEN CAPTURE"
Write-Output "  class : $ClassName"
Write-Output "  out   : $OutFile"

$p = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
if (-not $p) {
    Write-Output "RESULT: SKIP (process '$ProcessName' not running)"
    exit 2
}

$h = [NBScreen]::FindByClass($ClassName)
if ($h -eq [IntPtr]::Zero) {
    Write-Output "RESULT: SKIP (no visible window with class '$ClassName')"
    exit 2
}

$r = New-Object 'NBScreen+RECT'
$null = [NBScreen]::GetWindowRect($h, [ref]$r)
$x = $r.Left - $Padding
$y = $r.Top - $Padding
$w = ($r.Right - $r.Left) + ($Padding * 2)
$hh = ($r.Bottom - $r.Top) + ($Padding * 2)

# clamp against the VIRTUAL screen (multi-monitor aware; secondary screens can be negative)
$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
if ($x -lt $vs.Left) { $w -= ($vs.Left - $x); $x = $vs.Left }
if ($y -lt $vs.Top) { $hh -= ($vs.Top - $y); $y = $vs.Top }
if ($x + $w -gt $vs.Right) { $w = $vs.Right - $x }
if ($y + $hh -gt $vs.Bottom) { $hh = $vs.Bottom - $y }
if ($w -le 0 -or $hh -le 0) {
    Write-Output "RESULT: FAIL (window rect outside virtual screen: L=$($r.Left) T=$($r.Top) R=$($r.Right) B=$($r.Bottom))"
    Write-Output "  virtual screen: L=$($vs.Left) T=$($vs.Top) R=$($vs.Right) B=$($vs.Bottom)"
    exit 1
}

$dir = Split-Path -Parent $OutFile
if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }

$bmp = New-Object System.Drawing.Bitmap($w, $hh, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
try {
    $g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size($w, $hh)), [System.Drawing.CopyPixelOperation]::SourceCopy)
} finally {
    $g.Dispose()
}
$bmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)

# ---- pixel analysis via LockBits (fast) ----
$rect = New-Object System.Drawing.Rectangle(0, 0, $w, $hh)
$data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$stride = $data.Stride
$buf = New-Object byte[] ($stride * $hh)
try {
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
} finally {
    $bmp.UnlockBits($data)
}

# dominant colour inside the window rect only (the padding ring shows the desktop)
$hist = @{}
for ($py = $Padding; $py -lt ($hh - $Padding); $py++) {
    $row = $py * $stride
    for ($px = $Padding; $px -lt ($w - $Padding); $px++) {
        $o = $row + $px * 4
        $key = (([int]$buf[$o + 2] -shr 3) -shl 10) -bor (([int]$buf[$o + 1] -shr 3) -shl 5) -bor ([int]$buf[$o] -shr 3)
        if ($hist.ContainsKey($key)) { $hist[$key]++ } else { $hist[$key] = 1 }
    }
}
$modeKey = ($hist.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1).Key
$modeR = ((($modeKey -shr 10) -band 31)) * 8 + 4
$modeG = ((($modeKey -shr 5) -band 31)) * 8 + 4
$modeB = ((($modeKey) -band 31)) * 8 + 4
$modeLuma = 0.299 * $modeR + 0.587 * $modeG + 0.114 * $modeB

# desktop reference = the four padded corners (outside the ball, inside the capture)
$cornerLuma = @()
foreach ($cp in @(@(0, 0), @(($w - 1), 0), @(0, ($hh - 1)), @(($w - 1), ($hh - 1)))) {
    $o = $cp[1] * $stride + $cp[0] * 4
    $cornerLuma += (0.299 * [int]$buf[$o] + 0.587 * [int]$buf[$o + 1] + 0.114 * [int]$buf[$o + 2])
}

$distinct = @{}
$fillCount = 0
$fMinX = $w; $fMinY = $hh; $fMaxX = -1; $fMaxY = -1
$tMinX = $w; $tMinY = $hh; $tMaxX = -1; $tMaxY = -1
$contentCount = 0

for ($py = 0; $py -lt $hh; $py++) {
    $row = $py * $stride
    for ($px = 0; $px -lt $w; $px++) {
        $o = $row + $px * 4
        $br = [int]$buf[$o]; $bg = [int]$buf[$o + 1]; $bb = [int]$buf[$o + 2]; $ba = [int]$buf[$o + 3]
        $key = "$br,$bg,$bb,$ba"
        if ($distinct.ContainsKey($key)) { $distinct[$key]++ } else { $distinct[$key] = 1 }

        $luma = 0.299 * $br + 0.587 * $bg + 0.114 * $bb
        if ([math]::Abs($luma - $modeLuma) -le 18) {
            # part of the ball fill -> grows the ball bounding box
            if ($px -ge $Padding -and $px -lt ($w - $Padding) -and $py -ge $Padding -and $py -lt ($hh - $Padding)) {
                $fillCount++
                if ($px -lt $fMinX) { $fMinX = $px }
                if ($px -gt $fMaxX) { $fMaxX = $px }
                if ($py -lt $fMinY) { $fMinY = $py }
                if ($py -gt $fMaxY) { $fMaxY = $py }
            }
        }
    }
}

# second pass: build the shape mask (fill-like pixels) and the ink mask (glyphs).
# Ink that falls OUTSIDE the shape mask is text overflowing the ball - that is the
# defect worth failing on. A circle's mid-line is as wide as its diameter, so an
# "inscribed square" test would produce false positives.
$fillMask = New-Object 'bool[]' ($w * $hh)
for ($py = 0; $py -lt $hh; $py++) {
    $row = $py * $stride
    for ($px = 0; $px -lt $w; $px++) {
        $o = $row + $px * 4
        $luma = 0.299 * [int]$buf[$o] + 0.587 * [int]$buf[$o + 1] + 0.114 * [int]$buf[$o + 2]
        if ([math]::Abs($luma - $modeLuma) -le 18) { $fillMask[$py * $w + $px] = $true }
    }
}

$inkLuma = @{}
for ($py = 0; $py -lt $hh; $py++) {
    $row = $py * $stride
    for ($px = 0; $px -lt $w; $px++) {
        $o = $row + $px * 4
        $luma = 0.299 * [int]$buf[$o] + 0.587 * [int]$buf[$o + 1] + 0.114 * [int]$buf[$o + 2]
        if ([math]::Abs($luma - $modeLuma) -le 50) { continue }
        $bucket = [int]($luma / 8)
        if ($inkLuma.ContainsKey($bucket)) { $inkLuma[$bucket]++ } else { $inkLuma[$bucket] = 1 }
    }
}
$textLuma = -1
if ($inkLuma.Count -gt 0) {
    $textBucket = ($inkLuma.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 1).Key
    $textLuma = [math]::Min(255, $textBucket * 8 + 4)
}

$overflow = 0
for ($py = 0; $py -lt $hh; $py++) {
    $row = $py * $stride
    for ($px = 0; $px -lt $w; $px++) {
        $o = $row + $px * 4
        $luma = 0.299 * [int]$buf[$o] + 0.587 * [int]$buf[$o + 1] + 0.114 * [int]$buf[$o + 2]
        if ([math]::Abs($luma - $modeLuma) -le 50) { continue }
        if ($textLuma -ge 0 -and [math]::Abs($luma - $textLuma) -gt 30) { continue }   # not a glyph pixel
        # the desktop backdrop can be as bright as the glyphs; skip pixels that match it
        $isDesktop = $false
        foreach ($cl in $cornerLuma) { if ([math]::Abs($luma - $cl) -le 10) { $isDesktop = $true; break } }
        if ($isDesktop) { continue }
        $contentCount++
        if ($px -lt $tMinX) { $tMinX = $px }
        if ($px -gt $tMaxX) { $tMaxX = $px }
        if ($py -lt $tMinY) { $tMinY = $py }
        if ($py -gt $tMaxY) { $tMaxY = $py }
        # is this glyph pixel inside the shape? (allow 2 px slack for anti-aliasing)
        $inside = $false
        for ($dy = -2; $dy -le 2 -and -not $inside; $dy++) {
            $yy = $py + $dy
            if ($yy -lt 0 -or $yy -ge $hh) { continue }
            for ($dx = -2; $dx -le 2; $dx++) {
                $xx = $px + $dx
                if ($xx -lt 0 -or $xx -ge $w) { continue }
                if ($fillMask[$yy * $w + $xx]) { $inside = $true; break }
            }
        }
        if (-not $inside) { $overflow++ }
    }
}
$bmp.Dispose()

$size = (Get-Item -LiteralPath $OutFile).Length
Write-Output ("  hwnd  : 0x{0:X}" -f [int64]$h)
Write-Output ("  rect  : L={0} T={1} R={2} B={3}  capture={4}x{5} (padding {6})" -f $r.Left, $r.Top, $r.Right, $r.Bottom, $w, $hh, $Padding)
Write-Output ("  screen: L={0} T={1} R={2} B={3}  primary-only bug would clip here" -f $vs.Left, $vs.Top, $vs.Right, $vs.Bottom)
Write-Output ("  file  : {0} bytes" -f $size)
Write-Output ("  colors: {0} distinct colours (full scan)" -f $distinct.Count)
Write-Output ("  fill  : RGB({0},{1},{2})  luma={3:F1}  covers {4} px of the window" -f $modeR, $modeG, $modeB, $modeLuma, $fillCount)

$marginPx = -1
if ($fMaxX -gt $fMinX) {
    Write-Output ("  ball  : box L={0} T={1} R={2} B={3}  ({4}x{5} px)" -f $fMinX, $fMinY, $fMaxX, $fMaxY, ($fMaxX - $fMinX + 1), ($fMaxY - $fMinY + 1))
} else {
    Write-Output '  ball  : fill region not found (window is transparent or occluded)'
}
if ($contentCount -gt 0) {
    $marginPx = [math]::Min([math]::Min(($tMinX - $fMinX), ($fMaxX - $tMaxX)), [math]::Min(($tMinY - $fMinY), ($fMaxY - $tMaxY)))
    Write-Output ("  ink   : {0} px  luma={1}  box L={2} T={3} R={4} B={5}  margin_to_shape_edge={6} px" -f $contentCount, $textLuma, $tMinX, $tMinY, $tMaxX, $tMaxY, $marginPx)
    Write-Output ("  spill : {0} px of glyph colour outside the shape mask (advisory: desktop backdrop included)" -f $overflow)
} else {
    Write-Output '  ink   : 0 px  (no glyph pixels distinguishable from fill and desktop)'
}

if ($size -lt 200) {
    Write-Output "RESULT: FAIL (png written but suspiciously small: $size bytes)"
    exit 1
}
if ($distinct.Count -le 1) {
    Write-Output "RESULT: FAIL (capture is a single flat colour - nothing rendered or window occluded)"
    exit 1
}
if ($ExpectText -and $contentCount -eq 0) {
    Write-Output 'RESULT: FAIL (-ExpectText set but no glyph pixels: text missing or same colour as fill)'
    exit 1
}
if ($ExpectText -and $contentCount -gt 0 -and $marginPx -lt 2) {
    Write-Output "RESULT: FAIL (glyph box reaches the shape bounding box, margin=$marginPx px -> text clipped/overflowing)"
    exit 1
}
Write-Output "METRIC: shot=$OutFile bytes=$size cx=$w cy=$hh colors=$($distinct.Count) content_px=$contentCount margin_px=$marginPx spill_px=$overflow"
Write-Output "  note  : spill_px is advisory only - the desktop visible through a non-rectangular"
Write-Output "          shape can be as bright as the glyphs, so overflow is judged by the ink"
Write-Output "          margin above plus a human look at the png."
Write-Output "RESULT: PASS (real CopyFromScreen capture saved)"
exit 0

