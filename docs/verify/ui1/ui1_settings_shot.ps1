# ui1_settings_shot.ps1 -- T4 evidence capture for the settings dialog (ASCII only by team rule)
#
# Mode "product": launch build\out\NetBall.exe (the real product binary), open the settings
#                 window by posting WM_COMMAND(IDM_SETTINGS=1001) to the ball window, capture
#                 it, click the blue preset swatch, capture the preview again, then close the
#                 dialog, exit the app, and restore config.ini byte-for-byte.
# Mode "host":    same capture flow against build\_tmp\ui1_t4test.exe -show (same source tree,
#                 used only as a cross-check that the harness itself introduces no visual diff).
#
# DPI: SetThreadDpiAwarenessContext(-4) is called FIRST, so GetWindowRect returns physical
#      pixels on this 175% desktop. Without it the capture region is off by 1.75x - exactly
#      the trap that produced the false "size=160 has no text" conclusions.
#      The script hard-asserts: settings client area == 672x798 physical px at dpi 168,
#      otherwise it aborts with BLOCKED-DPI and produces no conclusion.
#
# Writes only into <OutDir> (default docs\verify\ui1). Never writes under build\out.

param(
    [ValidateSet('product', 'host')]
    [string]$Mode = 'product',
    [string]$OutDir = '',
    # Broadcast anchor from the team lead. When supplied, the run aborts with
    # BLOCKED-ANCHOR unless the binary hash matches exactly.
    [string]$ExpectSha = '',
    # Serial-window grant token, handed over by the lead only when the turn is actually
    # ui-1's. Passing the identity gates is NOT enough to launch: without this token the
    # script stops with BLOCKED-NOWINDOW. Form: ui1-GO-<4..8 digits>.
    [string]$WindowGrant = '',
    # Dry run: evaluate every identity gate, print the verdict, launch nothing at all.
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class W {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    // NOTE: title must be IntPtr, not string. Passing $null for a string parameter makes
    // PowerShell marshal an EMPTY STRING instead of NULL, so FindWindowW would search for a
    // window with an empty title and always return 0 (this bug cost one window already).
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, IntPtr title);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
}
'@

try {
    [void][W]::SetThreadDpiAwarenessContext([IntPtr](-4))   # DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
} catch {
    try { [void][W]::SetProcessDPIAware() } catch { }
}

$script:LogLines = New-Object System.Collections.ArrayList
$script:LogPath = $null

function Log([string]$msg) {
    $line = (Get-Date -Format 'HH:mm:ss.fff') + '  ' + $msg
    [void]$script:LogLines.Add($line)
    Write-Output $line
}

function Flush() {
    if ($script:LogPath) {
        Set-Content -Path $script:LogPath -Value $script:LogLines -Encoding UTF8
    }
}

# All gates, listed literally so an auditor can grep this file for them:
#   BLOCKED-SETUP     exe or source missing
#   BLOCKED-BUSY      another instance already running (serial window violated)
#   BLOCKED-NOWND     ball window never appeared
#   BLOCKED-NODLG     settings window never appeared (IDM_SETTINGS not handled)
#   BLOCKED-DPI       dpi != 168 or client != 672x798 -> capture coordinates unverified
#   BLOCKED-STALE     exe older than settings_dialog.cpp
#   BLOCKED-ANCHOR    sha256 != the anchor broadcast by the lead
#   BLOCKED-REPLACED  binary changed between the start and the end of the measurement
#   BLOCKED-SHOT      screenshot missing / suspiciously small / looks blank (artifact check)
#   BLOCKED-NOWINDOW  no -WindowGrant token: the serial window does not belong to ui-1 yet
function Bail([string]$tag, [string]$msg) {
    if ($tag -notlike 'BLOCKED-*') { $tag = 'BLOCKED-' + $tag }
    Log ($tag + ": " + $msg)
    Flush
    exit 9
}

# ---------------------------------------------------------------- locate project root
$root = $null
$probe = Split-Path -Parent $PSCommandPath
for ($i = 0; $i -lt 6 -and $probe; $i++) {
    if (Test-Path (Join-Path $probe 'src\app_common.h')) { $root = $probe; break }
    $probe = Split-Path -Parent $probe
}
# ASCII-only: never put a non-ASCII path literal here (team rule 2 - .ps1 is read as GBK).
if (-not $root) { throw 'cannot locate project root (no src\app_common.h above this script)' }

if ([string]::IsNullOrEmpty($OutDir)) { $OutDir = Join-Path $root 'docs\verify\ui1' }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

$script:LogPath = Join-Path $OutDir ('ui1_settings_shot_' + $Mode + '.log')
Log ("root = " + $root)
Log ("mode = " + $Mode)
# Evidence class must be unambiguous in the log itself, so an auditor reading only the
# log cannot mistake a harness capture for a product capture.
if ($Mode -eq 'product') {
    Log "EVIDENCE_CLASS = product (subject under test: build\out\NetBall.exe, anchor asserted via -ExpectSha)"
} else {
    Log "EVIDENCE_CLASS = reference-only (not product evidence) (subject under test: build\_tmp\ui1_t4test.exe)"
}

$exe = if ($Mode -eq 'product') { Join-Path $root 'build\out\NetBall.exe' } else { Join-Path $root 'build\_tmp\ui1_t4test.exe' }
if (-not (Test-Path $exe)) { Bail 'SETUP' ("exe not found: " + $exe) }

# ---------------------------------------------------------------- binary identity
$fi = Get-Item $exe
$sha = (Get-FileHash -Path $exe -Algorithm SHA256).Hash
Log ("exe    = " + $fi.FullName)
Log ("mtime  = " + $fi.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss') + "   size = " + $fi.Length + " bytes")
Log ("sha256 = " + $sha)
Log ("mtime(precision) = " + $fi.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss.fff'))

# ---------------------------------------------------------------- anchor gate (team rule: the measured binary must be uniquely identified)
# Anchor may arrive as -ExpectSha <hex> or as env NETBALL_ANCHOR_SHA (whichever the lead sets).
if ([string]::IsNullOrEmpty($ExpectSha) -and -not [string]::IsNullOrEmpty($env:NETBALL_ANCHOR_SHA)) {
    $ExpectSha = $env:NETBALL_ANCHOR_SHA
    Log "anchor supplied via environment variable NETBALL_ANCHOR_SHA"
}
if (-not [string]::IsNullOrEmpty($ExpectSha)) {
    $want = $ExpectSha.Trim().ToUpperInvariant()
    # Mechanical comparison only: both full strings are written to the log together with
    # their lengths, so an auditor can re-run the check instead of trusting an eyeball.
    Log ("anchor compare: expected (len=" + $want.Length + ") = " + $want)
    Log ("anchor compare: actual   (len=" + $sha.Length + ") = " + $sha)
    if ($want.Length -ne 64) {
        Bail 'ANCHOR' ("anchor supplied with length " + $want.Length + " (expected 64 hex chars); copy the whole hash verbatim, never re-format it")
    }
    if ($sha -cne $want) {
        Bail 'ANCHOR' ("sha256 != broadcast anchor; refusing to measure an unanchored binary")
    }
    Log "anchor compare result: EQUAL (-cne test on 64-char upper-case hex, both sides printed above)"
} else {
    Log "WARN no -ExpectSha supplied; anchor not asserted, T10 must cross-check the sha printed above"
}

$procName = [System.IO.Path]::GetFileNameWithoutExtension($exe)

# ---------------------------------------------------------------- staleness gate (exe must be newer than source)
# Two teams already lost time to "exe built before the source was written". This is a
# hard gate, not a warning: a binary older than settings_dialog.cpp cannot contain the
# palette that was just ruled final, so any screenshot from it would be a false evidence.
$srcSettings = Join-Path $root 'src\settings_dialog.cpp'
if (-not (Test-Path $srcSettings)) { Bail 'STALE' ("cannot verify staleness, source missing: " + $srcSettings) }
$srcFi = Get-Item $srcSettings
Log ("src    = " + $srcFi.FullName)
Log ("src mtime = " + $srcFi.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss') + "   size = " + $srcFi.Length)
Log ("src sha256 = " + (Get-FileHash $srcSettings -Algorithm SHA256).Hash)
if ($fi.LastWriteTime -le $srcFi.LastWriteTime) {
    Bail 'STALE' ("exe mtime " + $fi.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss') + " <= settings_dialog.cpp mtime " + $srcFi.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss') + "; binary predates source, rebuild required")
}
Log "staleness assertion OK: exe mtime > settings_dialog.cpp mtime"

# Informational only: if some OTHER src file is newer than the exe, say so out loud
# (the lead's gate is settings_dialog.cpp; this line just prevents silent surprises).
$newest = Get-ChildItem (Join-Path $root 'src') -File -Recurse -ErrorAction SilentlyContinue |
          Where-Object { $_.Extension -eq '.cpp' -or $_.Extension -eq '.h' } |
          Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($newest) {
    Log ("newest src file = " + $newest.Name + " @ " + $newest.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))
    if ($newest.LastWriteTime -gt $fi.LastWriteTime -and $newest.Name -ne $srcFi.Name) {
        Log ("WARN: " + $newest.Name + " is newer than the exe (not my file, but the binary may not reflect it)")
    }
}

# ---------------------------------------------------------------- serial-window gate
# Deliberately placed AFTER the identity gates and BEFORE anything that touches the
# machine: "all gates passed" must never be sufficient to launch. The token only exists
# in the lead's GO broadcast, so its presence is the mechanical proof of a granted turn.
if ($DryRun) {
    Log "dry-run: window-grant gate and launch are disabled (nothing will be started)"
    Log "RESULT: DRYRUN OK - identity gates evaluated, nothing launched, config.ini untouched"
    Flush
    exit 0
}
if ([string]::IsNullOrEmpty($WindowGrant)) {
    Bail 'NOWINDOW' ("no -WindowGrant token; the serial window has not been granted to ui-1 (identity gates passing is not a launch permit)")
}
if ($WindowGrant -notmatch '^ui1-GO-[0-9]{4,8}$') {
    Bail 'NOWINDOW' ("-WindowGrant does not match the expected form ui1-GO-<4..8 digits>")
}
Log ("window grant accepted: " + $WindowGrant)

# ---------------------------------------------------------------- config.ini snapshot
$cfg = Join-Path (Join-Path $env:APPDATA 'NetBall') 'config.ini'
$cfgExisted = Test-Path $cfg
$cfgBefore = $null
if ($cfgExisted) {
    $cfgBefore = Join-Path $OutDir ('config-before-' + $Mode + '.ini')
    Copy-Item -Path $cfg -Destination $cfgBefore -Force
    Log ("config.ini before: " + (Get-FileHash $cfg -Algorithm SHA256).Hash + "  (saved to " + $cfgBefore + ")")
} else {
    Log ("config.ini before: absent (" + $cfg + ")")
}

# ---------------------------------------------------------------- serial window guard
# Any NetBall* process holds the SAME global mutex (the mutex name does not depend on the
# executable name), so a sibling build such as NetBall_v4.exe must block this run as well.
$others = @(Get-Process -Name 'NetBall*' -ErrorAction SilentlyContinue)
if ($others.Count -gt 0) {
    $ids = ($others | ForEach-Object { $_.ProcessName + ':' + $_.Id }) -join ', '
    Bail 'BUSY' ("another NetBall-family process is running [" + $ids + "]; the global mutex is shared, so this run would collide - wait for the serial window")
}

# ---------------------------------------------------------------- launch
if ($Mode -eq 'product') { Log "launching product..." ; $p = Start-Process -FilePath $exe -PassThru }
else { Log "launching host harness (-show)..." ; $p = Start-Process -FilePath $exe -ArgumentList '-show' -PassThru }
Start-Sleep -Milliseconds 1200

$ballClass = 'NetBallBallWnd'
$setClass  = 'NetBallSettingsWnd'

if ($Mode -eq 'product') {
    $ball = [IntPtr]::Zero
    for ($i = 0; $i -lt 60; $i++) {
        $ball = [W]::FindWindowW($ballClass, [IntPtr]::Zero)
        if ($ball -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 250
    }
    # Fallback: the process main window is the ball; verify its class instead of trusting it.
    if ($ball -eq [IntPtr]::Zero) {
        $mw = $p.MainWindowHandle
        if ($mw -ne [IntPtr]::Zero) {
            $sb = New-Object System.Text.StringBuilder 128
            [void][W]::GetClassNameW($mw, $sb, 128)
            if ($sb.ToString() -ceq $ballClass) { $ball = $mw; Log "ball hwnd recovered from Process.MainWindowHandle" }
        }
    }
    if ($ball -eq [IntPtr]::Zero) { Bail 'NOWND' ("ball window " + $ballClass + " not found within 15s") }
    Log ("ball hwnd = 0x" + $ball.ToString('X'))
    [void][W]::PostMessage($ball, 0x0111, [IntPtr]1001, [IntPtr]::Zero)   # WM_COMMAND / IDM_SETTINGS
    Log "posted WM_COMMAND(1001) to ball window"
}

$dlg = [IntPtr]::Zero
for ($i = 0; $i -lt 60; $i++) {
    $dlg = [W]::FindWindowW($setClass, [IntPtr]::Zero)
    if ($dlg -ne [IntPtr]::Zero) { break }
    Start-Sleep -Milliseconds 250
}
if ($dlg -eq [IntPtr]::Zero) { Bail 'NODLG' ("settings window " + $setClass + " not found within 15s") }
Log ("settings hwnd = 0x" + $dlg.ToString('X'))
Start-Sleep -Milliseconds 600

# ---------------------------------------------------------------- geometry + hard DPI assertion
$r = New-Object 'W+RECT'
$c = New-Object 'W+RECT'
[void][W]::GetWindowRect($dlg, [ref]$r)
[void][W]::GetClientRect($dlg, [ref]$c)
$dpi = [W]::GetDpiForWindow($dlg)
$cw = $c.R - $c.L
$ch = $c.B - $c.T
Log ("window rect = (" + $r.L + "," + $r.T + ")-(" + $r.R + "," + $r.B + ")  size=" + ($r.R - $r.L) + "x" + ($r.B - $r.T))
Log ("client rect = " + $cw + "x" + $ch + "   dpi=" + $dpi)
if ($dpi -ne 168) { Bail 'DPI' ("dpi=" + $dpi + ", expected 168 for this 175% desktop; refusing to capture with unverified coordinates") }
if ($cw -ne 672 -or $ch -ne 798) { Bail 'DPI' ("client " + $cw + "x" + $ch + " != 672x798 physical px (384x456 dip @175%)") }
Log "geometry assertion OK: 672x798 physical px, dpi 168"

function Capture([string]$path) {
    [void][W]::SetForegroundWindow($dlg)
    Start-Sleep -Milliseconds 500
    $w = $r.R - $r.L
    $h = $r.B - $r.T
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Log ("saved " + $path + "  (" + (Get-Item $path).Length + " bytes)")
}

# Artifact-based success check (team rule: judge by the product, not by the return code).
# A blank / all-one-colour capture is the classic false evidence, so a screenshot only
# counts if it exists, is not suspiciously small, and has enough non-background pixels.
function VerifyShot([string]$path) {
    if (-not (Test-Path $path)) { Bail 'SHOT' ("screenshot missing: " + $path) }
    $bytes = (Get-Item $path).Length
    $bmp = [System.Drawing.Bitmap]::FromFile($path)
    $bg = $bmp.GetPixel(2, 2)
    $sampled = 0
    $diff = 0
    for ($y = 0; $y -lt $bmp.Height; $y += 8) {
        for ($x = 0; $x -lt $bmp.Width; $x += 8) {
            $sampled++
            $p = $bmp.GetPixel($x, $y)
            if ($p.R -ne $bg.R -or $p.G -ne $bg.G -or $p.B -ne $bg.B) { $diff++ }
        }
    }
    $w = $bmp.Width
    $h = $bmp.Height
    $bmp.Dispose()
    $pct = 0.0
    if ($sampled -gt 0) { $pct = [math]::Round(100.0 * $diff / $sampled, 1) }
    Log ("RESULT: " + [System.IO.Path]::GetFileName($path) + "  " + $w + "x" + $h + "  bytes=" + $bytes + "  non-bg=" + $pct + "%")
    if ($bytes -lt 20000) { Bail 'SHOT' ("png suspiciously small (" + $bytes + " bytes), likely a blank capture") }
    if ($pct -lt 5.0) { Bail 'SHOT' ("png looks blank: only " + $pct + "% non-background pixels") }
    Log ("RESULT: " + [System.IO.Path]::GetFileName($path) + " OK (exists, size and ink checks passed)")
}

$name1 = if ($Mode -eq 'product') { 'settings-8swatches.png' } else { 'host-settings-8swatches.png' }
$shotA = Join-Path $OutDir $name1
Capture $shotA
VerifyShot $shotA

# ---------------------------------------------------------------- click the blue swatch (index 3)
# layout in source: swatch i center = (88 + i*27 + 11, 265) dip  ->  physical = dip * dpi / 96
$sx = [int][math]::Round((88 + 3 * 27 + 11) * $dpi / 96.0)
$sy = [int][math]::Round(265 * $dpi / 96.0)
$lp = [IntPtr](($sy -shl 16) -bor ($sx -band 0xFFFF))
Log ("click blue swatch at client (" + $sx + "," + $sy + ")")
[void][W]::PostMessage($dlg, 0x0201, [IntPtr]1, $lp)   # WM_LBUTTONDOWN
[void][W]::PostMessage($dlg, 0x0202, [IntPtr]0, $lp)   # WM_LBUTTONUP
Start-Sleep -Milliseconds 700

$name2 = if ($Mode -eq 'product') { 'settings-preview-blue.png' } else { 'host-settings-preview-blue.png' }
$shotB = Join-Path $OutDir $name2
Capture $shotB
VerifyShot $shotB

# ---------------------------------------------------------------- close dialog, exit app
[void][W]::PostMessage($dlg, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_CLOSE
Start-Sleep -Milliseconds 600
if ([W]::IsWindow($dlg)) { Log "WARN: settings window still alive after WM_CLOSE" } else { Log "settings window closed by WM_CLOSE" }

if ($Mode -eq 'product') { [void][W]::PostMessage($ball, 0x0111, [IntPtr]1006, [IntPtr]::Zero) }   # IDM_EXIT

$gone = $false
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 250
    if ($p.HasExited) { $gone = $true; break }
}
if ($gone) {
    Log ("process exited, exit code = " + $p.ExitCode)
} else {
    Log ("process still alive after 10s, killing pid " + $p.Id)
    try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch { }
    Start-Sleep -Milliseconds 500
}
$left = @(Get-Process -Name $procName -ErrorAction SilentlyContinue)
Log ("remaining " + $procName + " instances = " + $left.Count)

# ---------------------------------------------------------------- restore config.ini
if ($cfgExisted -and $cfgBefore) {
    Copy-Item -Path $cfgBefore -Destination $cfg -Force
    $want = (Get-FileHash $cfgBefore -Algorithm SHA256).Hash
    $after = if (Test-Path $cfg) { (Get-FileHash $cfg -Algorithm SHA256).Hash } else { '<absent>' }
    if ($after -eq $want) { Log ("config.ini restored, sha256 = " + $after) }
    else { Log ("WARN config.ini restore mismatch: got " + $after + " want " + $want) }
} else {
    Log "config.ini was absent before the run; nothing to restore"
}

# ---------------------------------------------------------------- post-run identity assertion
# Team rule: every measurement must record the binary identity before AND after; if it
# changed, the whole run is void (BLOCKED-REPLACED) - no conclusions may be drawn.
$shaAfter = (Get-FileHash -Path $exe -Algorithm SHA256).Hash
$fiAfter = Get-Item $exe
Log ("sha256 after  = " + $shaAfter)
Log ("mtime  after  = " + $fiAfter.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss.fff') + "   size = " + $fiAfter.Length)
if ($shaAfter -ne $sha) {
    Log "BLOCKED-REPLACED: binary changed during the measurement; these screenshots are VOID"
    Flush
    exit 9
}
Log "post-run assertion OK: identical bytes before and after the measurement"

Log "DONE"
Flush
exit 0
