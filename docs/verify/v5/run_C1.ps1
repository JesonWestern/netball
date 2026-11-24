# run_C1.ps1 -- T14 group C (part 1): AC-1 / AC-2 / AC-5 / AC-6 / AC-10 / AC-11 / AC-16 / AC-17
# All values are re-measured on the v5 anchor; nothing is copied from earlier reports.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\t7b\nb.ps1"

$MediaDir = Join-Path $VerifyD 'media-v5'
$TmpDir   = Join-Path $Root 'build\_tmp\v5'
$ExeCopy  = Join-Path $TmpDir 'NetBall_v5.exe'
$ProcName = 'NetBall_v5'
foreach ($d in @($MediaDir, $TmpDir)) {
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}
$OutDir = Join-Path $Root 'build\out'

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class NBV5C {
    [DllImport("user32.dll")] public static extern int GetWindowLongW(IntPtr h, int idx);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr h, uint f);
    [DllImport("kernel32.dll")] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
'@

Write-Output "=========== T14 group C1 : AC-1/2/5/6/10/11/16/17 ==========="
foreach ($lp in @(Get-Process -Name $ProcName -ErrorAction SilentlyContinue)) {
    Write-Output ("  cleanup: killing leftover pid={0}" -f $lp.Id)
    try { $lp.Kill(); [void]$lp.WaitForExit(4000) } catch {}
}
$anchor = Initialize-Measurement -WaitMs 8000
Write-Output ("  anchor sha = {0} bytes={1} mtime={2}" -f $anchor.Sha, $anchor.Bytes, $anchor.Mtime)

# ================================================================== AC-1
Write-Output ""
Write-Output "--- AC-1: artifact identity / version resource / manifest ---"
$vi = (Get-Item -LiteralPath $ExeCopy).VersionInfo
$fd = $vi.FileDescription
$codes = @()
foreach ($ch in $fd.ToCharArray()) { $codes += [int][char]$ch }
Write-Output ("  FileDescription = '{0}' (length {1})" -f $fd, $fd.Length)
Write-Output ("  code points     = {0}" -f (($codes | ForEach-Object { 'U+{0:X4}' -f $_ }) -join ' '))
$expectCodes = @(78,101,116,66,97,108,108,32,0x7F51,0x901F,0x60AC,0x6D6E,0x7403)
$fdOk = (($fd.Length -eq 13) -and ((($codes) -join ',') -eq (($expectCodes) -join ',')))
Add-Check 'AC-1.filedescription' $fdOk ("13 code points, N-e-t-B-a-l-l-space + U+7F51 U+901F U+60AC U+6D6E U+7403")
Write-Output ("  FileVersion={0} ProductName='{1}' CompanyName='{2}'" -f $vi.FileVersion, $vi.ProductName, $vi.CompanyName)
$sizeOk = ((Get-Item -LiteralPath $ExeCopy).Length -eq 1053696)
Add-Check 'AC-1.size' $sizeOk ("{0} B <= 2097152" -f (Get-Item -LiteralPath $ExeCopy).Length)

$bytes = [System.IO.File]::ReadAllBytes($ExeCopy)
$latin = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes)
$asInv = ([regex]::Matches($latin, 'asInvoker')).Count
$pmv2  = ([regex]::Matches($latin, 'PerMonitorV2')).Count
$dpiA  = ([regex]::Matches($latin, 'dpiAware')).Count
Write-Output ("  manifest hits: asInvoker={0} PerMonitorV2={1} dpiAware={2}" -f $asInv, $pmv2, $dpiA)
Add-Check 'AC-1.manifest' (($asInv -ge 1) -and ($pmv2 -ge 1)) "asInvoker>=1 and PerMonitorV2>=1 (double hit)"

# ================================================================== AC-16
Write-Output ""
Write-Output "--- AC-16: no leftovers in build\out / project root ---"
$outFiles = @(Get-ChildItem -LiteralPath $OutDir -Force)
Write-Output ("  build\out entries = {0}" -f $outFiles.Count)
foreach ($f in $outFiles) { Write-Output ("    {0}  {1} B" -f $f.Name, $f.Length) }
Add-Check 'AC-16.out-only-exe' (($outFiles.Count -eq 1) -and ($outFiles[0].Name -eq 'NetBall.exe')) ("build\out holds exactly NetBall.exe")

$rootItems = @(Get-ChildItem -LiteralPath $Root -Force)
Write-Output ("  project root entries = {0}" -f $rootItems.Count)
foreach ($f in $rootItems) { Write-Output ("    {0}" -f $f.Name) }
$expectRoot = @('.agent','assets','build','dist','docs','installer','src','tools')
$extra = @($rootItems | Where-Object { $expectRoot -notcontains $_.Name })
Add-Check 'AC-16.root-clean' ($extra.Count -eq 0) ("{0} unexpected root entries{1}" -f $extra.Count, $(if ($extra.Count) { ': ' + (($extra | ForEach-Object { $_.Name }) -join ',') } else { '' }))

# ================================================================== helpers
function Get-BallPixelStats([int]$L, [int]$T, [int]$W, [int]$H, [int]$step = 1) {
    $bmp = New-Object System.Drawing.Bitmap($W, $H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $bg = $null
    try {
        $g.CopyFromScreen($L, $T, 0, 0, (New-Object System.Drawing.Size($W, $H)), [System.Drawing.CopyPixelOperation]::SourceCopy)
    } finally { $g.Dispose() }
    # background reference: a patch 40 px above-left of the ball
    $bgBmp = New-Object System.Drawing.Bitmap(12, 12)
    $g2 = [System.Drawing.Graphics]::FromImage($bgBmp)
    try {
        $g2.CopyFromScreen(($L - 40), ($T - 40), 0, 0, (New-Object System.Drawing.Size(12, 12)), [System.Drawing.CopyPixelOperation]::SourceCopy)
    } finally { $g2.Dispose() }
    $r = 0; $gg = 0; $b = 0; $n = 0
    for ($y = 0; $y -lt 12; $y += 3) { for ($x = 0; $x -lt 12; $x += 3) {
        $c = $bgBmp.GetPixel($x, $y); $r += $c.R; $gg += $c.G; $b += $c.B; $n++ } }
    $bgBmp.Dispose()
    $br = [int]($r / $n); $bgc = [int]($gg / $n); $bb = [int]($b / $n)
    $inside = 0; $total = 0
    $chordMin = -1; $chordMax = -1
    $yChord = [int][math]::Round($H * 0.1)
    for ($y = 0; $y -lt $H; $y += $step) {
        for ($x = 0; $x -lt $W; $x += $step) {
            $c = $bmp.GetPixel($x, $y)
            $d = [math]::Max([math]::Abs($c.R - $br), [math]::Max([math]::Abs($c.G - $bgc), [math]::Abs($c.B - $bb)))
            $total++
            if ($d -gt 50) {
                $inside++
                if ([math]::Abs($y - $yChord) -le $step) {
                    if (($chordMin -lt 0) -or ($x -lt $chordMin)) { $chordMin = $x }
                    if ($x -gt $chordMax) { $chordMax = $x }
                }
            }
        }
    }
    $bmp.Dispose()
    $cov = 0.0
    if ($total -gt 0) { $cov = [math]::Round($inside / $total, 3) }
    $inset = -1.0
    if ($chordMin -ge 0) { $inset = [math]::Round($chordMin / $W, 3) }
    return [pscustomobject]@{ Coverage = $cov; Inset = $inset; Bg = "$br,$bgc,$bb"; ChordY = $yChord }
}

function Measure-One([string]$tag, [int]$shape, [int]$size, [int]$opacity, [string]$bg, [int]$posx, [int]$posy, [int]$settle = 1500) {
    Write-Cfg -shape $shape -size $size -opacity $opacity -bg $bg -posx $posx -posy $posy
    $p = Start-Ball -SettleMs $settle
    if ($p.Hwnd -eq [IntPtr]::Zero) { return $null }
    $r = Get-BallRect $p.Hwnd
    $ex = [NBV5C]::GetWindowLongW($p.Hwnd, -20)
    Write-Output ("  {0}: rect L={1} T={2} R={3} B={4} {5}x{6} exstyle=0x{7:X}" -f $tag, $r.L, $r.T, $r.R, $r.B, $r.W, $r.H, $ex)
    return [pscustomobject]@{ Pid = $p.Pid; Hwnd = $p.Hwnd; R = $r; Ex = $ex }
}

# ================================================================== AC-2 + AC-10 + AC-11 + AC-5
Write-Output ""
Write-Output "--- AC-2 / AC-10 / AC-11 / AC-5 : one long-lived instance at 3000,300 ---"
$m = Measure-One 'AC-10 positive 3000,300' 0 88 85 '#181C24' 3000 300 1800
if ($null -eq $m) { Write-Output "RESULT: BLOCKED (ball never appeared)"; exit 3 }
Add-Check 'AC-10.pos-3000' (($m.R.L -eq 3000) -and ($m.R.T -eq 300)) ("rect ({0},{1}) restored from ini 3000,300" -f $m.R.L, $m.R.T)

$styleWsEx = 0x80088
$hasAppWindow = (($m.Ex -band 0x40000) -ne 0)
Write-Output ("  exstyle=0x{0:X} (expect 0x{1:X} = LAYERED|TOOLWINDOW|TOPMOST), APPWINDOW={2}" -f $m.Ex, $styleWsEx, $hasAppWindow)
Add-Check 'AC-2.exstyle' ($m.Ex -eq $styleWsEx) ("0x{0:X} == 0x80088" -f $m.Ex)
Add-Check 'AC-2.no-appwindow' (-not $hasAppWindow) "WS_EX_APPWINDOW is clear"

# --- AC-11: second instance must exit and leave exactly one process
$before = @(Get-Process -Name $ProcName -ErrorAction SilentlyContinue).Count
$p2 = Start-Process -FilePath $ExeCopy -WorkingDirectory (Split-Path -Parent $ExeCopy) -PassThru
$exited = $p2.WaitForExit(6000)
$code = -1
if ($exited) { $code = $p2.ExitCode }
Start-Sleep -Milliseconds 500
$after = @(Get-Process -Name $ProcName -ErrorAction SilentlyContinue).Count
Write-Output ("  second instance exited={0} exitcode={1}; process count {2} -> {3}" -f $exited, $code, $before, $after)
Add-Check 'AC-11.second-exits' ([bool]$exited) ("second instance terminated (exit code {0})" -f $code)
Add-Check 'AC-11.count-stays-1' (($before -eq 1) -and ($after -eq 1)) ("count {0} -> {1}" -f $before, $after)

# --- AC-5: 60 s steady-state sampling on the SAME instance
Write-Output ""
Write-Output "--- AC-5: 60 s steady-state resources (same instance) ---"
$pr = Get-Process -Id $m.Pid
$hProc = [NBV5C]::OpenProcess(0x0400, $false, [uint32]$m.Pid)
$wsList = @(); $gdi0 = -1; $gdi1 = -1; $gdi2 = -1; $h0 = 0; $h1 = 0; $h2 = 0
try {
    $gdi0 = [int][NBV5C]::GetGuiResources($hProc, 0)
    $h0 = $pr.HandleCount
    Write-Output ("  t=0    WS={0:N2} MB private={1:N2} MB handles={2} GDI={3}" -f ($pr.WorkingSet64 / 1MB), ($pr.PrivateMemorySize64 / 1MB), $h0, $gdi0)
    for ($i = 1; $i -le 20; $i++) {
        Start-Sleep -Seconds 3
        $pr.Refresh()
        $wsList += ($pr.WorkingSet64 / 1MB)
        if ($i -eq 10) {
            $gdi1 = [int][NBV5C]::GetGuiResources($hProc, 0); $h1 = $pr.HandleCount
            Write-Output ("  t=30   WS={0:N2} MB handles={1} GDI={2}" -f ($pr.WorkingSet64 / 1MB), $h1, $gdi1)
        }
    }
    $gdi2 = [int][NBV5C]::GetGuiResources($hProc, 0); $h2 = $pr.HandleCount
    Write-Output ("  t=60   WS={0:N2} MB handles={1} GDI={2}" -f ($pr.WorkingSet64 / 1MB), $h2, $gdi2)
} finally {
    if ($hProc -ne [IntPtr]::Zero) { [void][NBV5C]::CloseHandle($hProc) }
}
$wsMax = ($wsList | Measure-Object -Maximum).Maximum
$wsMin = ($wsList | Measure-Object -Minimum).Minimum
$wsAvg = ($wsList | Measure-Object -Average).Average
Write-Output ("  samples={0} WS min={1:N2} avg={2:N2} max={3:N2} MB" -f $wsList.Count, $wsMin, $wsAvg, $wsMax)
Add-Check 'AC-5.ws-steady' ($wsMax -le 15.0) ("steady-state WS max {0:N2} MB <= 15 MB (min {1:N2})" -f $wsMax, $wsMin)
Add-Check 'AC-5.gdi-no-growth' (($gdi2 -eq $gdi0) -and ($gdi0 -ge 0)) ("GDI {0} -> {1} -> {2} (no growth over 60 s)" -f $gdi0, $gdi1, $gdi2)
Add-Check 'AC-5.handle-no-growth' ($h2 -le $h0 + 2) ("handles {0} -> {1} -> {2}" -f $h0, $h1, $h2)
# AC-2 must still hold after 60 s of layered painting
$exNow = [NBV5C]::GetWindowLongW($m.Hwnd, -20)
Add-Check 'AC-2.exstyle-stable' ($exNow -eq $styleWsEx) ("exstyle still 0x{0:X} after 60 s" -f $exNow)
Stop-BallPid $m.Pid
$null = Wait-NoBall 6000

# ================================================================== AC-6 shapes
Write-Output ""
Write-Output "--- AC-6: three shapes at size 88 (opaque bright fill so pixels are decidable) ---"
$expectShapes = @(
    [pscustomobject]@{ Shape = 0; Name = 'circle'; W = 154; H = 154; Cov = 0.785; Inset = 0.200 },
    [pscustomobject]@{ Shape = 1; Name = 'rounded-square'; W = 154; H = 154; Cov = 0.954; Inset = 0.050 },
    [pscustomobject]@{ Shape = 2; Name = 'capsule'; W = 231; H = 154; Cov = 0.857; Inset = 0.133 }
)
foreach ($e in $expectShapes) {
    $r = Measure-One ("AC-6 " + $e.Name) $e.Shape 88 100 '#00FF00' 900 400 1600
    if ($null -eq $r) { Add-Check ("AC-6." + $e.Name) $false "ball window never appeared"; continue }
    $png = Join-Path $MediaDir ("c1-shape-" + $e.Shape + ".png")
    $null = Capture-Region -x $r.R.L -y $r.R.T -w $r.R.W -h $r.R.H -path $png
    $st = Get-BallPixelStats $r.R.L $r.R.T $r.R.W $r.R.H 1
    Write-Output ("    pixels: bg={0} coverage={1} inset@10%={2} (window {3}x{4})" -f $st.Bg, $st.Coverage, $st.Inset, $r.R.W, $r.R.H)
    Add-Check ("AC-6." + $e.Name + ".geometry") (($r.R.W -eq $e.W) -and ($r.R.H -eq $e.H)) ("{0}x{1} (expect {2}x{3})" -f $r.R.W, $r.R.H, $e.W, $e.H)
    Add-Check ("AC-6." + $e.Name + ".coverage") ([math]::Abs($st.Coverage - $e.Cov) -le 0.03) ("coverage {0} vs analytic {1} (tol 0.03)" -f $st.Coverage, $e.Cov)
    Add-Check ("AC-6." + $e.Name + ".chord") ([math]::Abs($st.Inset - $e.Inset) -le 0.03) ("10%-height chord inset {0} vs analytic {1} (tol 0.03)" -f $st.Inset, $e.Inset)
    Stop-BallPid $r.Pid
    $null = Wait-NoBall 5000
}

# ================================================================== AC-17 sizes
Write-Output ""
Write-Output "--- AC-17: DPI 175% -> 48/160/capsule160 physical sizes ---"
$dpi = 0
try { $dpi = [NBT7b]::GetDpiForSystem() } catch {}
Write-Output ("  GetDpiForSystem = {0} ({1}%)" -f $dpi, [math]::Round(100.0 * $dpi / 96))
Add-Check 'AC-17.dpi' ($dpi -eq 168) ("168 dpi == 175%")
$cases = @(
    [pscustomobject]@{ Shape = 0; Size = 48; W = 84; H = 84; Tag = 'size48' },
    [pscustomobject]@{ Shape = 0; Size = 160; W = 280; H = 280; Tag = 'size160' },
    [pscustomobject]@{ Shape = 2; Size = 160; W = 420; H = 280; Tag = 'capsule160' }
)
foreach ($c in $cases) {
    $r = Measure-One ("AC-17 " + $c.Tag) $c.Shape $c.Size 100 '#00FF00' 900 400 1600
    if ($null -eq $r) { Add-Check ("AC-17." + $c.Tag) $false "ball window never appeared"; continue }
    $png = Join-Path $MediaDir ("c1-" + $c.Tag + ".png")
    $null = Capture-Region -x $r.R.L -y $r.R.T -w $r.R.W -h $r.R.H -path $png
    $st = Get-BallPixelStats $r.R.L $r.R.T $r.R.W $r.R.H 2
    Write-Output ("    coverage={0} inset@10%={1}" -f $st.Coverage, $st.Inset)
    Add-Check ("AC-17." + $c.Tag) (($r.R.W -eq $c.W) -and ($r.R.H -eq $c.H)) ("{0}x{1} (expect {2}x{3}, ratio {4})" -f $r.R.W, $r.R.H, $c.W, $c.H, [math]::Round($r.R.W / $c.Size, 3))
    Add-Check ("AC-17." + $c.Tag + ".ink") ($st.Coverage -gt 0.3) ("rendered content coverage {0} > 0.3 (not a blank box)" -f $st.Coverage)
    Stop-BallPid $r.Pid
    $null = Wait-NoBall 5000
}

Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
Write-Output ""
Write-Output ("  config restored: posx={0} posy={1} shape={2} size={3}" -f (Read-Cfg).posx, (Read-Cfg).posy, (Read-Cfg).shape, (Read-Cfg).size)
Write-Output ("  processes left: {0}" -f (Get-ProcList).Count)
$fails = Get-FailCount
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
