# run_C2.ps1 -- T14 group C (part 2): AC-9 corrupt config / AC-7 opacity alpha / AC-8 contrast.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\t7b\nb.ps1"

$MediaDir = Join-Path $VerifyD 'media-v5'
$TmpDir   = Join-Path $Root 'build\_tmp\v5'
$ExeCopy  = Join-Path $TmpDir 'NetBall_v5.exe'
$ProcName = 'NetBall_v5'
foreach ($d in @($MediaDir, $TmpDir)) {
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}

Write-Output "=========== T14 group C2 : AC-9 / AC-7 / AC-8 ==========="
foreach ($lp in @(Get-Process -Name $ProcName -ErrorAction SilentlyContinue)) {
    Write-Output ("  cleanup: killing leftover pid={0}" -f $lp.Id)
    try { $lp.Kill(); [void]$lp.WaitForExit(4000) } catch {}
}
$anchor = Initialize-Measurement -WaitMs 8000
Write-Output ("  anchor sha = {0} bytes={1}" -f $anchor.Sha, $anchor.Bytes)

# ================================================================== AC-9
Write-Output ""
Write-Output "--- AC-9: deliberately corrupted config must not crash and must fall back to defaults ---"
$dir = Split-Path -Parent $IniPath
if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
$bad = @(
    '[NetBall]',
    'shape=9',
    'size=999',
    'opacity=999',
    'textauto=7',
    'shownet=abc',
    'showmem=-5',
    'refreshms=abc',
    'alwaysontop=zzz',
    'autostart=12',
    'posx=notanumber',
    'posy=',
    'bgcolor=zzz',
    'textcolor=#GGGGGG'
)
[System.IO.File]::WriteAllText($IniPath, (($bad -join "`r`n") + "`r`n"), [System.Text.Encoding]::ASCII)
Write-Output ("  wrote corrupted ini ({0} bytes, {1} keys)" -f (Get-Item -LiteralPath $IniPath).Length, $bad.Count)
$p = Start-Ball -SettleMs 1800
if ($p.Hwnd -eq [IntPtr]::Zero) {
    $alive = @(Get-Process -Name $ProcName -ErrorAction SilentlyContinue).Count
    Add-Check 'AC-9.corrupt.no-crash' $false ("no ball window; process count={0}" -f $alive)
} else {
    $r = Get-BallRect $p.Hwnd
    Write-Output ("  ball window survived: L={0} T={1} {2}x{3}" -f $r.L, $r.T, $r.W, $r.H)
    $png = Join-Path $MediaDir 'c2-ac9-corrupt.png'
    $null = Capture-Region -x ($r.L - 16) -y ($r.T - 16) -w ($r.W + 32) -h ($r.H + 32) -path $png
    Add-Check 'AC-9.corrupt.no-crash' $true "corrupted ini accepted, window created, no crash"
    Add-Check 'AC-9.corrupt.default-size' ($r.W -eq 154) ("size fell back to 88 -> 154 px (got {0})" -f $r.W)
    Add-Check 'AC-9.corrupt.default-pos' (($r.L -eq 3662) -and ($r.T -eq 1898)) ("position fell back to default ({0},{1})" -f $r.L, $r.T)
    Stop-BallPid $p.Pid
    $null = Wait-NoBall 5000
}
$c = Read-Cfg
Write-Output ("  ini after the corrupt run: shape={0} size={1} opacity={2} refreshms={3} bgcolor={4} posx={5} posy={6}" -f $c.shape, $c.size, $c.opacity, $c.refreshms, $c.bgcolor, $c.posx, $c.posy)
Add-Check 'AC-9.corrupt.rewritten-sane' (($c.shape -eq '0') -and ($c.size -eq '88') -and ($c.opacity -eq '85') -and ($c.bgcolor -eq '#181C24') -and ($c.refreshms -eq '1000')) "config rewritten with the documented defaults"

Write-Output ""
Write-Output "--- AC-9b: deleting a key must be repaired on the next start ---"
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
$lines = @(Get-Content -LiteralPath $IniPath | Where-Object { $_ -notmatch '^size=' })
[System.IO.File]::WriteAllText($IniPath, (($lines -join "`r`n") + "`r`n"), [System.Text.Encoding]::ASCII)
Write-Output ("  removed the size key; keys now = {0}" -f $lines.Count)
$p = Start-Ball -SettleMs 1800
if ($p.Hwnd -eq [IntPtr]::Zero) {
    Add-Check 'AC-9b.missing-key' $false "ball window never appeared"
} else {
    $r = Get-BallRect $p.Hwnd
    Write-Output ("  ball: L={0} T={1} {2}x{3}" -f $r.L, $r.T, $r.W, $r.H)
    Add-Check 'AC-9b.missing-key' ($r.W -eq 154) ("missing size key -> default 88 (154 px), got {0}" -f $r.W)
    Stop-BallPid $p.Pid
    $null = Wait-NoBall 5000
}
$c = Read-Cfg
Add-Check 'AC-9b.key-restored' ($c.size -eq '88') ("size key recreated with value {0}" -f $c.size)

# ================================================================== AC-7
Write-Output ""
Write-Output "--- AC-7: implied alpha via white/black backdrop difference (white ball, ring samples) ---"
function Get-RingMean([int]$cx, [int]$cy, [int]$radius) {
    $pts = 40
    $sum = 0.0
    for ($i = 0; $i -lt $pts; $i++) {
        $a = 2 * [math]::PI * $i / $pts
        $x = $cx + [int][math]::Round($radius * [math]::Cos($a))
        $y = $cy + [int][math]::Round($radius * [math]::Sin($a))
        $bmp = New-Object System.Drawing.Bitmap(1, 1, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        try { $g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size(1, 1)), [System.Drawing.CopyPixelOperation]::SourceCopy) }
        finally { $g.Dispose() }
        $c = $bmp.GetPixel(0, 0)
        $bmp.Dispose()
        $sum += 0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B
    }
    return $sum / $pts
}

function Measure-Alpha([int]$opacity) {
    Write-Cfg -shape 0 -size 88 -opacity $opacity -bg '#FFFFFF' -posx 900 -posy 400
    $p = Start-Ball -SettleMs 1500
    if ($p.Hwnd -eq [IntPtr]::Zero) { return $null }
    $r = Get-BallRect $p.Hwnd
    $cx = $r.L + [int]($r.W / 2)
    $cy = $r.T + [int]($r.H / 2)
    $ring = [int]($r.W * 0.43)          # inside the disc, outside the text block
    # backdrop window covering the ball area (never topmost -> the ball stays on top)
    $form = New-Object System.Windows.Forms.Form
    $form.FormBorderStyle = 'None'
    $form.StartPosition = 'Manual'
    $form.ShowInTaskbar = $false
    $form.Bounds = New-Object System.Drawing.Rectangle(($r.L - 60), ($r.T - 60), ($r.W + 120), ($r.H + 120))
    $form.BackColor = [System.Drawing.Color]::White
    $form.Show()
    for ($i = 0; $i -lt 12; $i++) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 50 }
    $onWhite = Get-RingMean $cx $cy $ring
    $pngW = Join-Path $MediaDir ("c2-ac7-op{0}-white.png" -f $opacity)
    $null = Capture-Region -x ($r.L - 10) -y ($r.T - 10) -w ($r.W + 20) -h ($r.H + 20) -path $pngW
    $form.BackColor = [System.Drawing.Color]::Black
    for ($i = 0; $i -lt 12; $i++) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 50 }
    $onBlack = Get-RingMean $cx $cy $ring
    $pngB = Join-Path $MediaDir ("c2-ac7-op{0}-black.png" -f $opacity)
    $null = Capture-Region -x ($r.L - 10) -y ($r.T - 10) -w ($r.W + 20) -h ($r.H + 20) -path $pngB
    $form.Close()
    $form.Dispose()
    for ($i = 0; $i -lt 6; $i++) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 50 }
    # obs = a*C + (1-a)*D  with C = 255 (white ball): over black obs = a*255
    $alpha = $onBlack / 255.0
    Write-Output ("  opacity={0}%: ring luma on white={1:N1} on black={2:N1} -> implied alpha={3:N3}" -f $opacity, $onWhite, $onBlack, $alpha)
    Stop-BallPid $p.Pid
    $null = Wait-NoBall 5000
    return [pscustomobject]@{ Opacity = $opacity; OnWhite = $onWhite; OnBlack = $onBlack; Alpha = $alpha }
}

$a20 = Measure-Alpha 20
if ($null -eq $a20) {
    Add-Check 'AC-7.opacity20' $false "ball window never appeared"
} else {
    Add-Check 'AC-7.opacity20' ([math]::Abs($a20.Alpha - 0.200) -le 0.02) ("implied alpha {0:N3} (expect 0.200 +-0.02)" -f $a20.Alpha)
}
$a100 = Measure-Alpha 100
if ($null -eq $a100) {
    Add-Check 'AC-7.opacity100' $false "ball window never appeared"
} else {
    Add-Check 'AC-7.opacity100' ([math]::Abs($a100.Alpha - 1.000) -le 0.02) ("implied alpha {0:N3} (expect 1.000 +-0.02)" -f $a100.Alpha)
}

# ================================================================== AC-8
Write-Output ""
Write-Output "--- AC-8: strict maximin contrast, 8 presets at default opacity 85% ---"
function Lin([double]$v) {
    $s = $v / 255.0
    if ($s -le 0.03928) { return $s / 12.92 }
    return [math]::Pow((($s + 0.055) / 1.055), 2.4)
}
function RelLum($c) {
    return 0.2126 * (Lin $c[0]) + 0.7152 * (Lin $c[1]) + 0.0722 * (Lin $c[2])
}
function Contrast($c1, $c2) {
    $l1 = RelLum $c1
    $l2 = RelLum $c2
    if ($l1 -lt $l2) { $t = $l1; $l1 = $l2; $l2 = $t }
    return ($l1 + 0.05) / ($l2 + 0.05)
}
function Composite($ball, $desk, [double]$a) {
    return @(
        ($a * $ball[0] + (1 - $a) * $desk[0]),
        ($a * $ball[1] + (1 - $a) * $desk[1]),
        ($a * $ball[2] + (1 - $a) * $desk[2])
    )
}
$presets = @(
    @{ Name = 'dark-grey #262A34'; Rgb = @(38, 42, 52); Expect = 8.86 },
    @{ Name = 'black     #000000'; Rgb = @(0, 0, 0); Expect = 15.13 },
    @{ Name = 'white     #FFFFFF'; Rgb = @(255, 255, 255); Expect = 14.88 },
    @{ Name = 'blue      #144BC8'; Rgb = @(20, 75, 200); Expect = 5.28 },
    @{ Name = 'green     #22A05A'; Rgb = @(34, 160, 90); Expect = 4.68 },
    @{ Name = 'orange    #EB8C1E'; Rgb = @(235, 140, 30); Expect = 6.11 },
    @{ Name = 'purple    #6E32BE'; Rgb = @(110, 50, 190); Expect = 5.26 },
    @{ Name = 'red       #B41923'; Rgb = @(180, 25, 35); Expect = 5.32 }
)
$alpha = 0.85
$black = @(0, 0, 0); $white = @(255, 255, 255)
$minAll = 99.0
$worstName = ''
foreach ($pr in $presets) {
    $oW = Composite $pr.Rgb $white $alpha
    $oB = Composite $pr.Rgb $black $alpha
    # for each candidate text colour take the WORSE of the two desktops, then keep the better text colour
    $cBlack = [math]::Min((Contrast $oW $black), (Contrast $oB $black))
    $cWhite = [math]::Min((Contrast $oW $white), (Contrast $oB $white))
    $maximin = [math]::Max($cBlack, $cWhite)
    $pick = 'black'
    if ($cWhite -gt $cBlack) { $pick = 'white' }
    $delta = [math]::Abs($maximin - $pr.Expect)
    if ($maximin -lt $minAll) { $minAll = $maximin; $worstName = $pr.Name }
    Write-Output ("  {0}  maximin={1:N2} (text {2})  documented={3:N2}  delta={4:N2}" -f $pr.Name, $maximin, $pick, $pr.Expect, $delta)
    Add-Check ("AC-8." + ($pr.Name -replace '[ #]', '')) (($maximin -ge 4.5) -and ($delta -le 0.05)) ("maximin {0:N2} >= 4.5 and matches the documented {1:N2}" -f $maximin, $pr.Expect)
}
Write-Output ("  worst preset = {0} at {1:N2}" -f $worstName, $minAll)
Add-Check 'AC-8.worst-min' (($minAll -ge 4.5) -and ([math]::Abs($minAll - 4.678) -le 0.05)) ("global minimum {0:N2} (documented 4.68)" -f $minAll)

Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
Write-Output ""
Write-Output ("  config restored: posx={0} posy={1} bg={2}" -f (Read-Cfg).posx, (Read-Cfg).posy, (Read-Cfg).bgcolor)
Write-Output ("  processes left: {0}" -f (Get-ProcList).Count)
$fails = Get-FailCount
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
