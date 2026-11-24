# ui1_resume_capture.ps1 -- capture-only continuation against the ALREADY running product
# instance (the launcher script was killed by a console Ctrl+C right after Start-Process).
# It never starts a new process; it only drives the running one and then closes it.
# ASCII only. Same gates as the main script (DPI / anchor already verified in the run log).
param(
    [string]$OutDir = '',
    [int]$ExpectedPid = 0
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class W2 {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [DllImport("user32.dll")] public static extern bool SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string c, string t);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
}
'@

try { [void][W2]::SetThreadDpiAwarenessContext([IntPtr](-4)) } catch { try { [void][W2]::SetProcessDPIAware() } catch { } }

$script:lines = New-Object System.Collections.ArrayList
function Log([string]$m) { $l = (Get-Date -Format 'HH:mm:ss.fff') + '  ' + $m; [void]$script:lines.Add($l); Write-Output $l }
function Flush() { if ($script:logPath) { Set-Content -Path $script:logPath -Value $script:lines -Encoding UTF8 } }
function Bail([string]$tag, [string]$m) {
    if ($tag -notlike 'BLOCKED-*') { $tag = 'BLOCKED-' + $tag }
    Log ($tag + ': ' + $m); Flush; exit 9
}

# ASCII-only root discovery (team rule 2: no non-ASCII literals in .ps1, they get read as GBK).
$root = $null
$probe = Split-Path -Parent $PSCommandPath
for ($i = 0; $i -lt 8 -and $probe; $i++) {
    if (Test-Path (Join-Path $probe 'src\app_common.h')) { $root = $probe; break }
    $probe = Split-Path -Parent $probe
}
if (-not $root) { throw 'cannot locate project root (no src\app_common.h above this script)' }
if ([string]::IsNullOrEmpty($OutDir)) { $OutDir = Join-Path $root 'docs\verify\ui1' }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }
$script:logPath = Join-Path $OutDir 'ui1_settings_shot_product_resume.log'

Log 'EVIDENCE_CLASS = product (subject under test: build\out\NetBall.exe, resumed capture in the same granted window)'
Log 'mode = product-resume'

$exe = Join-Path $root 'build\out\NetBall.exe'
$fi = Get-Item $exe
$sha = (Get-FileHash $exe -Algorithm SHA256).Hash
Log ("exe    = " + $exe)
Log ("mtime  = " + $fi.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss.fff') + '   size = ' + $fi.Length)
Log ("sha256(before) = " + $sha)

$procs = @(Get-Process -Name 'NetBall*' -ErrorAction SilentlyContinue)
if ($procs.Count -eq 0) { Bail 'NOWND' 'no NetBall process is running; nothing to resume' }
if ($procs.Count -gt 1) { Bail 'BUSY' ('multiple NetBall processes: ' + (($procs | ForEach-Object { $_.Id }) -join ',')) }
$p = $procs[0]
# BUG FIX: this line used to print the EXE mtime under the label "start", which made a
# reader conclude the process had been started at the exe's build time. Process start and
# exe mtime are different things and are now printed with distinct labels.
$pStart = '<unknown>'
try { $pStart = $p.StartTime.ToString('yyyy-MM-dd HH:mm:ss.fff') } catch { }
Log ("attached to pid=" + $p.Id + ' processStart=' + $pStart + ' exeMtime=' + $fi.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss.fff'))
if ($ExpectedPid -ne 0 -and $p.Id -ne $ExpectedPid) { Bail 'BUSY' ('running pid ' + $p.Id + ' != expected ' + $ExpectedPid) }

$ball = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    $ball = [W2]::FindWindowW('NetBallBallWnd', $null)
    if ($ball -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 200
}
if ($ball -eq [IntPtr]::Zero) { Bail 'NOWND' 'ball window not found in the running instance' }
Log ('ball hwnd = 0x' + $ball.ToString('X'))
[void][W2]::PostMessage($ball, 0x0111, [IntPtr]1001, [IntPtr]::Zero)
Log 'posted WM_COMMAND(1001) IDM_SETTINGS'

$dlg = [IntPtr]::Zero
for ($i = 0; $i -lt 50; $i++) {
    $dlg = [W2]::FindWindowW('NetBallSettingsWnd', $null)
    if ($dlg -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 200
}
if ($dlg -eq [IntPtr]::Zero) { Bail 'NODLG' 'settings window did not appear within 10s' }
Log ('settings hwnd = 0x' + $dlg.ToString('X'))
Start-Sleep -Milliseconds 600

$r = New-Object 'W2+RECT'; $c = New-Object 'W2+RECT'
[void][W2]::GetWindowRect($dlg, [ref]$r)
[void][W2]::GetClientRect($dlg, [ref]$c)
$dpi = [W2]::GetDpiForWindow($dlg)
$cw = $c.R - $c.L; $ch = $c.B - $c.T
Log ('window rect = (' + $r.L + ',' + $r.T + ')-(' + $r.R + ',' + $r.B + ') size=' + ($r.R - $r.L) + 'x' + ($r.B - $r.T))
Log ('client rect = ' + $cw + 'x' + $ch + '  dpi=' + $dpi)
if ($dpi -ne 168) { Bail 'DPI' ('dpi=' + $dpi + ' expected 168') }
if ($cw -ne 672 -or $ch -ne 798) { Bail 'DPI' ('client ' + $cw + 'x' + $ch + ' != 672x798') }
Log 'geometry assertion OK: 672x798 @ dpi 168'

function Capture([string]$path) {
    [void][W2]::SetForegroundWindow($dlg)
    Start-Sleep -Milliseconds 450
    $w = $r.R - $r.L; $h = $r.B - $r.T
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Log ('saved ' + $path + ' (' + (Get-Item $path).Length + ' bytes)')
}

function VerifyShot([string]$path) {
    if (-not (Test-Path $path)) { Bail 'SHOT' ('missing ' + $path) }
    $bytes = (Get-Item $path).Length
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    $bg = $bmp.GetPixel(2, 2)
    $n = 0; $d = 0
    for ($y = 0; $y -lt $bmp.Height; $y += 8) {
        for ($x = 0; $x -lt $bmp.Width; $x += 8) {
            $n++
            $q = $bmp.GetPixel($x, $y)
            if ($q.R -ne $bg.R -or $q.G -ne $bg.G -or $q.B -ne $bg.B) { $d++ }
        }
    }
    $w = $bmp.Width; $h = $bmp.Height
    $bmp.Dispose()
    $pct = 0.0
    if ($n -gt 0) { $pct = [math]::Round(100.0 * $d / $n, 1) }
    Log ('RESULT: ' + [System.IO.Path]::GetFileName($path) + '  ' + $w + 'x' + $h + '  bytes=' + $bytes + '  non-bg=' + $pct + '%')
    if ($bytes -lt 20000) { Bail 'SHOT' ('png too small: ' + $bytes) }
    if ($pct -lt 5.0) { Bail 'SHOT' ('looks blank: ' + $pct + '% non-bg') }
    Log ('RESULT: ' + [System.IO.Path]::GetFileName($path) + ' OK')
}

$shotA = Join-Path $OutDir 'settings-8swatches.png'
Capture $shotA
VerifyShot $shotA

$sx = [int][math]::Round((88 + 3 * 27 + 11) * $dpi / 96.0)
$sy = [int][math]::Round(265 * $dpi / 96.0)
$lp = [IntPtr](($sy -shl 16) -bor ($sx -band 0xFFFF))
Log ('click blue swatch at client (' + $sx + ',' + $sy + ')')
[void][W2]::PostMessage($dlg, 0x0201, [IntPtr]1, $lp)
[void][W2]::PostMessage($dlg, 0x0202, [IntPtr]0, $lp)
Start-Sleep -Milliseconds 700

$shotB = Join-Path $OutDir 'settings-preview-blue.png'
Capture $shotB
VerifyShot $shotB

Log ('sha256(after) = ' + (Get-FileHash $exe -Algorithm SHA256).Hash)
[void][W2]::PostMessage($dlg, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Milliseconds 600
if ([W2]::IsWindow($dlg)) { Log 'WARN settings window still alive after WM_CLOSE' } else { Log 'settings window closed' }

[void][W2]::PostMessage($ball, 0x0111, [IntPtr]1006, [IntPtr]::Zero)
$gone = $false
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    if ($p.HasExited) { $gone = $true; break }
}
if ($gone) { Log ('process exited, code = ' + $p.ExitCode) } else { Log ('still alive after 10s, killing pid ' + $p.Id); try { Stop-Process -Id $p.Id -Force } catch { } }
$left = @(Get-Process -Name 'NetBall*' -ErrorAction SilentlyContinue)
Log ('remaining NetBall-family instances = ' + $left.Count)

$cfg = Join-Path (Join-Path $env:APPDATA 'NetBall') 'config.ini'
$snap = Join-Path $OutDir 'config-before-product.ini'
if ((Test-Path $cfg) -and (Test-Path $snap)) {
    $now = (Get-FileHash $cfg -Algorithm SHA256).Hash
    $want = (Get-FileHash $snap -Algorithm SHA256).Hash
    Log ('config.ini now  = ' + $now)
    Log ('config.ini want = ' + $want)
    if ($now -ceq $want) { Log 'config.ini compare: EQUAL (-ceq) - nothing to restore' }
    else { Copy-Item $snap $cfg -Force; Log ('config.ini restored, sha now = ' + (Get-FileHash $cfg -Algorithm SHA256).Hash) }
} else {
    Log 'config.ini snapshot unavailable; left as is'
}

Log 'DONE'
Flush
exit 0
