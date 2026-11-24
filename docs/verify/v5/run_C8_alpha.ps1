# run_C8_alpha.ps1 -- AC-7 implied alpha without any controlled backdrop.
# Two runs at the same position with the same (unknown) wallpaper behind:
#   obs(white ball) = a*255 + (1-a)*B
#   obs(black ball) = a*0   + (1-a)*B
#   => a = (obs_white - obs_black) / 255      (B cancels exactly)
# That removes the backdrop from the equation instead of trying to control it.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\t7b\nb.ps1"

$MediaDir = Join-Path $VerifyD 'media-v5'
$TmpDir   = Join-Path $Root 'build\_tmp\v5'
$ExeCopy  = Join-Path $TmpDir 'NetBall_v5.exe'
$ProcName = 'NetBall_v5'
foreach ($d in @($MediaDir, $TmpDir)) {
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}

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
    $n = 48; $s = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        $a = 2 * [math]::PI * $i / $n
        $s += Sample-Pt ($cx + [int][math]::Round($radius * [math]::Cos($a))) ($cy + [int][math]::Round($radius * [math]::Sin($a)))
    }
    return $s / $n
}
function Measure-Ball([int]$opacity, [string]$bg, [string]$tag) {
    Write-Cfg -shape 0 -size 88 -opacity $opacity -bg $bg -posx 2400 -posy 700
    $p = Start-Ball -SettleMs 1700
    if ($p.Hwnd -eq [IntPtr]::Zero) { return -1.0 }
    $r = Get-BallRect $p.Hwnd
    $cx = $r.L + [int]($r.W / 2); $cy = $r.T + [int]($r.H / 2)
    $ring = [int]($r.W * 0.42)
    $v = RingMean $cx $cy $ring
    $outer = RingMean ($r.L - 40) ($r.T - 40) 8      # wallpaper reference, outside the ball
    Write-Output ("  opacity={0,3} bg={1} ring luma={2:N2} wallpaper luma={3:N2} rect={4},{5} {6}x{7}" -f $opacity, $bg, $v, $outer, $r.L, $r.T, $r.W, $r.H)
    $png = Join-Path $MediaDir ("c8-{0}.png" -f $tag)
    $null = Capture-Region -x ($r.L - 20) -y ($r.T - 20) -w ($r.W + 40) -h ($r.H + 40) -path $png
    Stop-BallPid $p.Pid
    $null = Wait-NoBall 5000
    # Write-Output lines are part of the pipeline, so hand the value back through
    # a script-scope variable instead of "return" (an array would break the maths).
    $script:LastBall = $v
}

Write-Output "=========== T14 AC-7 : implied alpha from white-vs-black ball (backdrop cancels) ==========="
foreach ($lp in @(Get-Process -Name $ProcName -ErrorAction SilentlyContinue)) {
    Write-Output ("  cleanup: killing leftover pid={0}" -f $lp.Id)
    try { $lp.Kill(); [void]$lp.WaitForExit(4000) } catch {}
}
$anchor = Initialize-Measurement -WaitMs 8000
Write-Output ("  anchor sha = {0} bytes={1}" -f $anchor.Sha, $anchor.Bytes)

foreach ($op in @(20, 100)) {
    Write-Output ""
    Write-Output ("--- opacity {0}% ---" -f $op)
    $script:LastBall = -1.0
    Measure-Ball $op '#FFFFFF' ("op{0}-white" -f $op)
    $w = $script:LastBall
    $script:LastBall = -1.0
    Measure-Ball $op '#000000' ("op{0}-black" -f $op)
    $b = $script:LastBall
    if (($w -lt 0) -or ($b -lt 0)) { Add-Check ("AC-7.op" + $op) $false "ball window never appeared"; continue }
    $a = ($w - $b) / 255.0
    Write-Output ("  implied alpha = (white {0:N2} - black {1:N2}) / 255 = {2:N3}" -f $w, $b, $a)
    $expect = $op / 100.0
    Add-Check ("AC-7.op" + $op) ([math]::Abs($a - $expect) -le 0.02) ("implied alpha {0:N3} vs expected {1:N3} (tol 0.02)" -f $a, $expect)
    Add-Check ("AC-7.op" + $op + ".separation") (($w - $b) -gt 20) ("white-vs-black separation {0:N1} luma is decisive" -f ($w - $b))
}

Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
Write-Output ""
Write-Output ("  config restored: {0} size={1} opacity={2} bg={3} pos={4},{5}" -f (Read-Cfg).shape, (Read-Cfg).size, (Read-Cfg).opacity, (Read-Cfg).bgcolor, (Read-Cfg).posx, (Read-Cfg).posy)
Write-Output ("  processes left: {0}" -f (Get-ProcList).Count)
$fails = Get-FailCount
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
