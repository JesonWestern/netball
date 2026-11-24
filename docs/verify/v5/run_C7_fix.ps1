# run_C7_fix.ps1 -- corrected AC-9 (graceful exit so the config is rewritten)
# and corrected AC-7 (solve alpha against the measured backdrop, not an assumed
# pure black/white).
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\t7b\nb.ps1"

$MediaDir = Join-Path $VerifyD 'media-v5'
$TmpDir   = Join-Path $Root 'build\_tmp\v5'
$ExeCopy  = Join-Path $TmpDir 'NetBall_v5.exe'
$ProcName = 'NetBall_v5'
foreach ($d in @($MediaDir, $TmpDir)) {
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}

Write-Output "=========== T14 corrected AC-9 (graceful) + AC-7 (backdrop-solved) ==========="
foreach ($lp in @(Get-Process -Name $ProcName -ErrorAction SilentlyContinue)) {
    Write-Output ("  cleanup: killing leftover pid={0}" -f $lp.Id)
    try { $lp.Kill(); [void]$lp.WaitForExit(4000) } catch {}
}
$anchor = Initialize-Measurement -WaitMs 8000
Write-Output ("  anchor sha = {0} bytes={1}" -f $anchor.Sha, $anchor.Bytes)

# ================================================================== AC-9 (graceful exit)
Write-Output ""
Write-Output "--- AC-9: corrupted ini + GRACEFUL exit (WM_COMMAND 1006) -> config rewritten ---"
$dir = Split-Path -Parent $IniPath
if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
$bad = @('[NetBall]','shape=9','size=999','opacity=999','textauto=7','shownet=abc',
         'showmem=-5','refreshms=abc','alwaysontop=zzz','autostart=12',
         'posx=notanumber','posy=','bgcolor=zzz','textcolor=#GGGGGG')
[System.IO.File]::WriteAllText($IniPath, (($bad -join "`r`n") + "`r`n"), [System.Text.Encoding]::ASCII)
Write-Output ("  corrupted ini written ({0} bytes)" -f (Get-Item -LiteralPath $IniPath).Length)
$p = Start-Ball -SettleMs 1800
if ($p.Hwnd -eq [IntPtr]::Zero) {
    Add-Check 'AC-9.no-crash' $false "ball window never appeared"
} else {
    $r = Get-BallRect $p.Hwnd
    Write-Output ("  survived with L={0} T={1} {2}x{3}" -f $r.L, $r.T, $r.W, $r.H)
    $png = Join-Path $MediaDir 'c7-ac9-corrupt.png'
    $null = Capture-Region -x ($r.L - 16) -y ($r.T - 16) -w ($r.W + 32) -h ($r.H + 32) -path $png
    Add-Check 'AC-9.no-crash' $true "window created from a fully corrupted ini"
    Add-Check 'AC-9.default-size' ($r.W -eq 154) ("size 999 -> 88 (154 px)")
    Add-Check 'AC-9.default-pos' (($r.L -eq 3662) -and ($r.T -eq 1898)) ("posx=notanumber -> default ({0},{1})" -f $r.L, $r.T)
    Send-Cmd $p.Hwnd 1006                          # IDM_EXIT -> save then quit
    $exited = $false
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 200
        if (-not (Get-Process -Id $p.Pid -ErrorAction SilentlyContinue)) { $exited = $true; break }
    }
    Write-Output ("  graceful exit: process gone={0}" -f $exited)
    Add-Check 'AC-9.graceful-exit' $exited "IDM_EXIT terminated the process"
}
$c = Read-Cfg
Write-Output ("  ini after graceful exit: shape={0} size={1} opacity={2} refreshms={3} bgcolor={4} textcolor={5} posx={6} posy={7}" -f $c.shape, $c.size, $c.opacity, $c.refreshms, $c.bgcolor, $c.textcolor, $c.posx, $c.posy)
Add-Check 'AC-9.rewritten-sane' (($c.shape -eq '0') -and ($c.size -eq '88') -and ($c.opacity -eq '85') -and ($c.refreshms -eq '1000') -and ($c.bgcolor -eq '#181C24')) "config rewritten to the documented defaults"

# ================================================================== AC-7 (solved properly)
Write-Output ""
Write-Output "--- AC-7: implied alpha, solved against the measured backdrop ---"
function Sample-Pt([int]$x, [int]$y) {
    $bmp = New-Object System.Drawing.Bitmap(1, 1, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try { $g.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size(1, 1)), [System.Drawing.CopyPixelOperation]::SourceCopy) }
    finally { $g.Dispose() }
    $c = $bmp.GetPixel(0, 0)
    $bmp.Dispose()
    return (0.2126 * $c.R + 0.7152 * $c.G + 0.0722 * $c.B)
}
function RingMean([int]$cx, [int]$cy, [int]$radius) {
    $n = 40; $s = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        $a = 2 * [math]::PI * $i / $n
        $s += Sample-Pt ($cx + [int][math]::Round($radius * [math]::Cos($a))) ($cy + [int][math]::Round($radius * [math]::Sin($a)))
    }
    return $s / $n
}

function Measure-Alpha([int]$opacity) {
    Write-Cfg -shape 0 -size 88 -opacity $opacity -bg '#FFFFFF' -posx 900 -posy 400
    $p = Start-Ball -SettleMs 1600
    if ($p.Hwnd -eq [IntPtr]::Zero) { return $null }
    $r = Get-BallRect $p.Hwnd
    $cx = $r.L + [int]($r.W / 2); $cy = $r.T + [int]($r.H / 2)
    $ring = [int]($r.W * 0.42)
    $form = New-Object System.Windows.Forms.Form
    $form.FormBorderStyle = 'None'
    $form.StartPosition = 'Manual'
    $form.ShowInTaskbar = $false
    $form.Bounds = New-Object System.Drawing.Rectangle(($r.L - 80), ($r.T - 80), ($r.W + 160), ($r.H + 160))
    $results = @()
    foreach ($col in @('White', 'Black')) {
        $form.BackColor = [System.Drawing.Color]::FromName($col)
        if ($col -eq 'White') { $form.Show() }
        # the form is created at the bottom of the z-order; raise it above the
        # ordinary windows (the ball is WS_EX_TOPMOST, so it stays on top of it)
        $form.BringToFront()
        [void][NBT7b]::SetForegroundWindow($form.Handle)
        for ($i = 0; $i -lt 25; $i++) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 40 }
        $desk = RingMean ($r.L - 50) ($r.T - 50) 6      # inside the form, outside the ball
        $ball = RingMean $cx $cy $ring
        $png = Join-Path $MediaDir ("c7-ac7-op{0}-{1}.png" -f $opacity, $col.ToLower())
        $null = Capture-Region -x ($r.L - 20) -y ($r.T - 20) -w ($r.W + 40) -h ($r.H + 40) -path $png
        $results += [pscustomobject]@{ Col = $col; Desk = $desk; Ball = $ball }
        Write-Output ("    backdrop {0}: desk luma={1:N1} ball luma={2:N1}" -f $col, $desk, $ball)
    }
    $form.Close(); $form.Dispose()
    for ($i = 0; $i -lt 8; $i++) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 40 }
    # gate: if the two backdrops did not really differ, the whole computation is void
    $span = [math]::Abs($results[0].Desk - $results[1].Desk)
    Write-Output ("    backdrop separation = {0:N1} luma (need >= 100 for a valid solve)" -f $span)
    if ($span -lt 100) { throw ("backdrop not established (separation {0:N1}); AC-7 result VOID" -f $span) }
    # C = 255 (white ball).  a = (ball - desk) / (C - desk)
    $alphas = @()
    foreach ($x in $results) {
        $den = 255.0 - $x.Desk
        $a = 0.0
        if ([math]::Abs($den) -gt 1.0) { $a = ($x.Ball - $x.Desk) / $den }
        $alphas += $a
    }
    $alpha = ($alphas | Measure-Object -Average).Average
    Write-Output ("    implied alpha (mean of both backdrops) = {0:N3}" -f $alpha)
    Stop-BallPid $p.Pid
    $null = Wait-NoBall 5000
    # NB: the Write-Output lines above are part of this function's return value,
    # so the alpha is handed back through a script-scope variable, not the pipeline.
    $script:LastAlpha = $alpha
}

$script:LastAlpha = $null
Measure-Alpha 20
$a20 = $script:LastAlpha
if ($null -eq $a20) { Add-Check 'AC-7.opacity20' $false "ball window never appeared" }
else { Add-Check 'AC-7.opacity20' ([math]::Abs($a20 - 0.200) -le 0.02) ("implied alpha {0:N3} (expect 0.200 +-0.02)" -f $a20) }
$script:LastAlpha = $null
Measure-Alpha 100
$a100 = $script:LastAlpha
if ($null -eq $a100) { Add-Check 'AC-7.opacity100' $false "ball window never appeared" }
else { Add-Check 'AC-7.opacity100' ([math]::Abs($a100 - 1.000) -le 0.02) ("implied alpha {0:N3} (expect 1.000 +-0.02)" -f $a100) }

Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
Write-Output ""
Write-Output ("  config restored: {0} size={1} opacity={2} bg={3} pos={4},{5}" -f (Read-Cfg).shape, (Read-Cfg).size, (Read-Cfg).opacity, (Read-Cfg).bgcolor, (Read-Cfg).posx, (Read-Cfg).posy)
Write-Output ("  processes left: {0}" -f (Get-ProcList).Count)
$fails = Get-FailCount
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
