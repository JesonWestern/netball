# nb.ps1 -- shared helpers for T7b GUI verification (ASCII-only on purpose).
# Dot-source from a runner:  . "$PSScriptRoot\nb.ps1"
$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

$Here     = $PSScriptRoot                      # ...\docs\verify\t7b
$VerifyD  = Split-Path -Parent $Here           # ...\docs\verify
$DocsD    = Split-Path -Parent $VerifyD        # ...\docs
$Root     = Split-Path -Parent $DocsD          # project root
$MediaDir = Join-Path $VerifyD 'media-t7b'
$TmpDir   = Join-Path $Root 'build\_tmp\t7b'
$OutDir   = Join-Path $Root 'build\out'
$ExeSrc   = Join-Path $OutDir 'NetBall.exe'
$ExeCopy  = Join-Path $TmpDir 'NetBall_t7b.exe'
$IniPath  = Join-Path $env:APPDATA 'NetBall\config.ini'
$ProcName = 'NetBall_t7b'
$ClsBall  = 'NetBallBallWnd'
$ClsSet   = 'NetBallSettingsWnd'

foreach ($d in @($MediaDir, $TmpDir)) {
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}

$cs = @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class NBT7b {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr lParam);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int n);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetDpiForSystem();
    [DllImport("user32.dll")] public static extern IntPtr GetWindowDpiAwarenessContext(IntPtr h);
    [DllImport("user32.dll")] public static extern int GetAwarenessFromDpiAwarenessContext(IntPtr ctx);
    [DllImport("user32.dll")] public static extern IntPtr MonitorFromPoint(POINT p, uint f);
    [DllImport("shcore.dll")] public static extern int GetDpiForMonitor(IntPtr m, int t, out uint x, out uint y);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int ht, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr m);
    [DllImport("user32.dll")] public static extern uint GetMenuItemID(IntPtr m, int pos);
    [DllImport("user32.dll")] public static extern uint GetMenuState(IntPtr m, uint id, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetMenuStringW(IntPtr m, uint id, StringBuilder s, int n, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

    public static IntPtr FindByClass(string cls, uint pid) {
        IntPtr hit = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            var sb = new StringBuilder(512);
            if (GetClassNameW(h, sb, sb.Capacity) > 0 && string.Equals(sb.ToString(), cls, StringComparison.Ordinal)) {
                uint p; GetWindowThreadProcessId(h, out p);
                if (pid == 0 || p == pid) { hit = h; return false; }
            }
            return true;
        }, IntPtr.Zero);
        return hit;
    }
    // popup menu windows live in class "#32768" and belong to the process that popped them
    public static IntPtr FindMenuWindow(uint pid) {
        IntPtr hit = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            var sb = new StringBuilder(64);
            if (GetClassNameW(h, sb, sb.Capacity) > 0 && sb.ToString() == "#32768") {
                uint p; GetWindowThreadProcessId(h, out p);
                if (pid == 0 || p == pid) { hit = h; return false; }
            }
            return true;
        }, IntPtr.Zero);
        if (hit == IntPtr.Zero) hit = FindWindowW("#32768", null);
        return hit;
    }
    // MN_GETHMENU = 0x01E1 : hands back the HMENU behind a menu window
    public static string DumpMenu(IntPtr hMenuWnd) {
        if (hMenuWnd == IntPtr.Zero) return "ERR:no menu window";
        IntPtr hm = SendMessage(hMenuWnd, 0x01E1, IntPtr.Zero, IntPtr.Zero);
        if (hm == IntPtr.Zero) return "ERR:MN_GETHMENU returned 0";
        int n = GetMenuItemCount(hm);
        var sb = new StringBuilder();
        sb.AppendLine("HMENU=0x" + hm.ToInt64().ToString("X") + " count=" + n);
        for (int i = 0; i < n; i++) {
            uint state = GetMenuState(hm, (uint)i, 0x400);
            uint id = GetMenuItemID(hm, i);
            var t = new StringBuilder(256);
            GetMenuStringW(hm, (uint)i, t, t.Capacity, 0x400);
            sb.AppendLine(string.Format("item {0} id={1} checked={2} sep={3} enabled={4} text={5}",
                i, id, ((state & 0x8) != 0) ? 1 : 0, ((state & 0x800) != 0) ? 1 : 0,
                ((state & 0x3) != 0) ? 0 : 1, t.ToString()));
        }
        return sb.ToString();
    }
    public static string WindowInfo(IntPtr h) {
        RECT r;
        if (!GetWindowRect(h, out r)) return "ERR:GetWindowRect";
        return string.Format("L={0} T={1} R={2} B={3} W={4} H={5} visible={6}",
            r.Left, r.Top, r.Right, r.Bottom, r.Right - r.Left, r.Bottom - r.Top,
            IsWindowVisible(h) ? 1 : 0);
    }
}
'@
try { Add-Type -TypeDefinition $cs -ErrorAction Stop } catch {
    if ($_.Exception.Message -notmatch 'already exists') { throw }
}

# make this thread PerMonitorV2 so every rect / capture is in physical pixels
try { [void][NBT7b]::SetThreadDpiAwarenessContext([IntPtr](-4)) } catch {}

$script:Checks = New-Object System.Collections.ArrayList

function Add-Check([string]$name, [bool]$ok, [string]$detail) {
    [void]$script:Checks.Add([pscustomobject]@{ Name = $name; Ok = $ok; Detail = $detail })
    $tag = 'FAIL'
    if ($ok) { $tag = 'PASS' }
    Write-Output ("CHECK: {0} {1}  ({2})" -f $tag, $name, $detail)
}
function Get-FailCount {
    # @() is mandatory: an empty pipeline yields $null, and $null.Count made the
    # SUMMARY line print "failed=" which then read as "not zero" and turned a
    # fully passing AC-17 into RESULT: FAIL.
    return @($script:Checks | Where-Object { -not $_.Ok }).Count
}

function Get-ProcList {
    $l = @()
    foreach ($n in @('NetBall', $ProcName)) {
        $l += @(Get-Process -Name $n -ErrorAction SilentlyContinue)
    }
    return @($l | Sort-Object Id -Unique)
}
function Wait-NoBall([int]$TimeoutMs = 8000) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        if ((Get-ProcList).Count -eq 0) { return $true }
        Start-Sleep -Milliseconds 250
    }
    return ((Get-ProcList).Count -eq 0)
}
function Stop-BallPid([int]$TargetPid) {
    if ($TargetPid -le 0) { return }
    $p = Get-Process -Id $TargetPid -ErrorAction SilentlyContinue
    if ($p) { try { $p.Kill(); [void]$p.WaitForExit(4000) } catch {} }
    Start-Sleep -Milliseconds 300
}
function Stop-AllT7b {
    foreach ($p in (Get-ProcList)) { try { $p.Kill(); [void]$p.WaitForExit(4000) } catch {} }
    Start-Sleep -Milliseconds 300
}
function New-ExeCopy {
    if (-not (Test-Path -LiteralPath $ExeSrc)) { throw "artifact missing: $ExeSrc" }
    Copy-Item -LiteralPath $ExeSrc -Destination $ExeCopy -Force
    return (Get-Item -LiteralPath $ExeCopy).Length
}

function Write-Cfg {
    param([int]$shape = 0, [int]$size = 88, [int]$opacity = 85, [string]$bg = '#181C24',
          [int]$posx = -1, [int]$posy = -1, [int]$textAuto = 1)
    $dir = Split-Path -Parent $IniPath
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $lines = @(
        '[NetBall]',
        "shape=$shape",
        "size=$size",
        "opacity=$opacity",
        "textauto=$textAuto",
        "shownet=1",
        "showmem=1",
        "refreshms=1000",
        "alwaysontop=1",
        "autostart=0",
        "posx=$posx",
        "posy=$posy",
        "savedw=0",
        "savedh=0",
        "bgcolor=$bg",
        "textcolor=#FFFFFF"
    )
    [System.IO.File]::WriteAllText($IniPath, (($lines -join "`r`n") + "`r`n"), [System.Text.Encoding]::ASCII)
}
function Read-Cfg {
    $h = @{}
    if (-not (Test-Path -LiteralPath $IniPath)) { return $h }
    foreach ($l in (Get-Content -LiteralPath $IniPath)) {
        if ($l -match '^([A-Za-z]+)=(.*)$') { $h[$matches[1].ToLower()] = $matches[2].Trim() }
    }
    return $h
}
function Wait-BallWindow([int]$TimeoutMs = 8000, [int]$TargetPid = 0) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
        $h = [NBT7b]::FindByClass($ClsBall, [uint32]$TargetPid)
        if ($h -ne [IntPtr]::Zero) { return $h }
        Start-Sleep -Milliseconds 200
    }
    return [IntPtr]::Zero
}
function Start-Ball([int]$SettleMs = 1200, [int]$TargetPid = 0, [int]$Retries = 3) {
    # The single-instance mutex is machine wide, so a foreign NetBall.exe started
    # by another member makes our copy exit instantly.  Wait for it, retry, and
    # report why instead of silently producing a "program crashes" conclusion.
    for ($attempt = 1; $attempt -le $Retries; $attempt++) {
        $foreign = @(Get-Process -Name 'NetBall' -ErrorAction SilentlyContinue)
        if ($foreign.Count -gt 0) {
            Write-Output ("  WAIT: {0} foreign NetBall.exe instance(s) hold the single-instance mutex (attempt {1}/{2})" -f $foreign.Count, $attempt, $Retries)
            Start-Sleep -Seconds 4
            continue
        }
        $p = Start-Process -FilePath $ExeCopy -WorkingDirectory (Split-Path -Parent $ExeCopy) -PassThru
        $h = Wait-BallWindow -TimeoutMs 6000 -TargetPid $p.Id
        if ($h -ne [IntPtr]::Zero) {
            Start-Sleep -Milliseconds $SettleMs
            $h = [NBT7b]::FindByClass($ClsBall, [uint32]$p.Id)
            return [pscustomobject]@{ Pid = $p.Id; Hwnd = $h }
        }
        $alive = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
        Write-Output ("  RETRY: no ball window on attempt {0}/{1} (process alive={2})" -f $attempt, $Retries, ($null -ne $alive))
        if ($alive) { Stop-BallPid $p.Id }
        Start-Sleep -Seconds 2
    }
    return [pscustomobject]@{ Pid = 0; Hwnd = [IntPtr]::Zero }
}
function Get-BallRect([IntPtr]$h) {
    $r = New-Object 'NBT7b+RECT'
    [void][NBT7b]::GetWindowRect($h, [ref]$r)
    return [pscustomobject]@{ L = $r.Left; T = $r.Top; R = $r.Right; B = $r.Bottom
                              W = ($r.Right - $r.Left); H = ($r.Bottom - $r.Top) }
}

function Get-ClampedRegion([int]$x, [int]$y, [int]$w, [int]$h) {
    $vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
    if ($x -lt $vs.Left) { $w -= ($vs.Left - $x); $x = $vs.Left }
    if ($y -lt $vs.Top) { $h -= ($vs.Top - $y); $y = $vs.Top }
    if ($x + $w -gt $vs.Right) { $w = $vs.Right - $x }
    if ($y + $h -gt $vs.Bottom) { $h = $vs.Bottom - $y }
    if ($w -lt 1) { $w = 1 }
    if ($h -lt 1) { $h = 1 }
    return [pscustomobject]@{ X = $x; Y = $y; W = $w; H = $h }
}
function Capture-Region([int]$x, [int]$y, [int]$w, [int]$h, [string]$path) {
    $r = Get-ClampedRegion -x $x -y $y -w $w -h $h
    $bmp = New-Object System.Drawing.Bitmap($r.W, $r.H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.CopyFromScreen($r.X, $r.Y, 0, 0, (New-Object System.Drawing.Size($r.W, $r.H)), [System.Drawing.CopyPixelOperation]::SourceCopy)
    } finally { $g.Dispose() }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    return (Get-Item -LiteralPath $path).Length
}
function Get-RegionLuma([int]$x, [int]$y, [int]$w, [int]$h) {
    # mean perceptual luma of a screen region (used to pick a ball colour that is
    # actually distinguishable from the wallpaper, so shape pixels are decidable)
    $r = Get-ClampedRegion -x $x -y $y -w $w -h $h
    $bmp = New-Object System.Drawing.Bitmap($r.W, $r.H, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.CopyFromScreen($r.X, $r.Y, 0, 0, (New-Object System.Drawing.Size($r.W, $r.H)), [System.Drawing.CopyPixelOperation]::SourceCopy)
    } finally { $g.Dispose() }
    $sum = 0.0
    $n = 0
    for ($yy = 0; $yy -lt $r.H; $yy += 7) {
        for ($xx = 0; $xx -lt $r.W; $xx += 7) {
            $c = $bmp.GetPixel($xx, $yy)
            $sum += 0.299 * $c.R + 0.587 * $c.G + 0.114 * $c.B
            $n++
        }
    }
    $bmp.Dispose()
    if ($n -eq 0) { return 128.0 }
    return $sum / $n
}

function Get-ImageDiff([string]$a, [string]$b, [int]$step = 5) {
    # mean |RGB| difference between two same-size PNGs (sampled).  Used to prove
    # the desktop behind the ball is frozen while a translucent shot is taken.
    $ia = New-Object System.Drawing.Bitmap($a)
    $ib = New-Object System.Drawing.Bitmap($b)
    $sum = 0.0
    $n = 0
    try {
        $w = [math]::Min($ia.Width, $ib.Width)
        $h = [math]::Min($ia.Height, $ib.Height)
        for ($y = 0; $y -lt $h; $y += $step) {
            for ($x = 0; $x -lt $w; $x += $step) {
                $ca = $ia.GetPixel($x, $y)
                $cb = $ib.GetPixel($x, $y)
                $sum += ([math]::Abs($ca.R - $cb.R) + [math]::Abs($ca.G - $cb.G) + [math]::Abs($ca.B - $cb.B)) / 3.0
                $n++
            }
        }
    } finally {
        $ia.Dispose()
        $ib.Dispose()
    }
    if ($n -eq 0) { return 999.0 }
    return $sum / $n
}

function Test-ScreenFrozen([int]$x, [int]$y, [int]$w, [int]$h, [int]$WaitMs = 900) {
    $a = Join-Path $TmpDir 'stab-a.png'
    $b = Join-Path $TmpDir 'stab-b.png'
    $null = Capture-Region -x $x -y $y -w $w -h $h -path $a
    Start-Sleep -Milliseconds $WaitMs
    $null = Capture-Region -x $x -y $y -w $w -h $h -path $b
    return (Get-ImageDiff $a $b)
}

function Get-IniValue([string]$Key) {
    if (-not (Test-Path -LiteralPath $IniPath)) { return '' }
    foreach ($l in (Get-Content -LiteralPath $IniPath)) {
        if ($l -match ('^\s*' + [regex]::Escape($Key) + '\s*=\s*(.+)$')) { return $matches[1].Trim() }
    }
    return ''
}

function Get-ExeInfo([string]$p) {
    $i = Get-Item -LiteralPath $p
    $h = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLower()
    return [pscustomobject]@{ Path = $p; Sha = $h; Bytes = $i.Length; Mtime = $i.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss') }
}

# Every measurement must be anchored to ONE binary.  The expected sha256 is
# broadcast by the team lead; it is read from build\_tmp\t7b\anchor.txt so no
# runner has to be edited when a new build is released.  No anchor file means no
# measurement (fail closed), never "assume it is fine".
function Assert-Anchor([string]$Expected = '') {
    if (-not $Expected) {
        $f = Join-Path $TmpDir 'anchor.txt'
        if (Test-Path -LiteralPath $f) { $Expected = (Get-Content -LiteralPath $f -Raw).Trim() }
    }
    $info = Get-ExeInfo $ExeSrc
    Write-Output ("  ANCHOR: build\out\NetBall.exe bytes={0} mtime={1}" -f $info.Bytes, $info.Mtime)
    Write-Output ("  ANCHOR: sha256={0}" -f $info.Sha)
    if (-not $Expected) {
        Write-Output "  ANCHOR: no expected sha256 recorded in build\_tmp\t7b\anchor.txt"
        Write-Output "RESULT: BLOCKED-ANCHOR (no broadcast sha256 to anchor this measurement to)"
        exit 4
    }
    if ($info.Sha -ne $Expected.ToLower()) {
        Write-Output ("  ANCHOR: expected {0}" -f $Expected.ToLower())
        Write-Output "RESULT: BLOCKED-ANCHOR (build\out\NetBall.exe is not the broadcast binary)"
        exit 4
    }
    Write-Output "  ANCHOR: matches the broadcast sha256"
    return $info
}

# One call for every runner: wait for a clean machine, verify the binary against
# the broadcast sha256, then refresh the private copy and verify the copy again.
function Initialize-Measurement([int]$WaitMs = 10000) {
    if (-not (Wait-NoBall $WaitMs)) {
        Write-Output "RESULT: BLOCKED (a NetBall instance is still running)"
        exit 3
    }
    $anchor = Assert-Anchor
    $null = New-ExeCopy
    $copy = Get-ExeInfo $ExeCopy
    Write-Output ("  private copy: {0} {1} B mtime={2}" -f $copy.Sha, $copy.Bytes, $copy.Mtime)
    if ($copy.Sha -ne $anchor.Sha) {
        Write-Output ("  ! private copy sha {0} != anchor {1}" -f $copy.Sha, $anchor.Sha)
        Write-Output "RESULT: BLOCKED-ANCHOR (private copy is not the broadcast binary)"
        exit 4
    }
    Write-Output "  private copy matches the anchor"
    return $anchor
}

function Confirm-AnchorUnchanged($Anchor) {
    $now = Get-ExeInfo $ExeSrc
    $same = ($now.Sha -eq $Anchor.Sha)
    Write-Output ("  ANCHOR-AFTER: sha256={0} bytes={1} mtime={2}" -f $now.Sha, $now.Bytes, $now.Mtime)
    if (-not $same) {
        Write-Output ("  ! the binary changed while measuring (was {0})" -f $Anchor.Sha)
        Write-Output "RESULT: BLOCKED (artifact replaced during the measurement - all data above is void)"
        exit 4
    }
    Write-Output "  ANCHOR-AFTER: unchanged during the measurement"
    return $true
}

function Capture-Ball([IntPtr]$h, [int]$Pad, [string]$path) {
    $rc = Get-BallRect $h
    $n = Capture-Region -x ($rc.L - $Pad) -y ($rc.T - $Pad) -w ($rc.W + 2 * $Pad) -h ($rc.H + 2 * $Pad) -path $path
    return [pscustomobject]@{ Len = $n; L = $rc.L; T = $rc.T; R = $rc.R; B = $rc.B; W = $rc.W; H = $rc.H; Pad = $Pad }
}

function Close-PopupMenu([IntPtr]$menuHwnd) {
    # ESC first (the menu owns the keyboard while TrackPopupMenu runs)
    [NBT7b]::keybd_event(0x1B, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [NBT7b]::keybd_event(0x1B, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    if ([NBT7b]::IsWindow($menuHwnd)) {
        [void][NBT7b]::PostMessage($menuHwnd, 0x0100, [IntPtr]0x1B, [IntPtr]::Zero)   # WM_KEYDOWN VK_ESCAPE
        Start-Sleep -Milliseconds 250
    }
    if ([NBT7b]::IsWindow($menuHwnd)) {
        [void][NBT7b]::PostMessage($menuHwnd, 0x001F, [IntPtr]::Zero, [IntPtr]::Zero) # WM_CANCELMODE
        Start-Sleep -Milliseconds 250
    }
    return -not [NBT7b]::IsWindow($menuHwnd)
}
function Send-Cmd([IntPtr]$h, [int]$id) {
    [void][NBT7b]::PostMessage($h, 0x0111, [IntPtr]$id, [IntPtr]::Zero)   # WM_COMMAND
}
function Write-Manifest([string]$path, [object[]]$rows, [string]$header = 'key|png|L|T|R|B|pad|extra') {
    $lines = @($header)
    foreach ($r in $rows) { $lines += $r }
    [System.IO.File]::WriteAllLines($path, $lines, (New-Object System.Text.UTF8Encoding($false)))
}
Write-Output ("ENV: root={0}" -f $Root)
Write-Output ("ENV: exe={0} size={1}" -f $ExeCopy, (Get-Item -LiteralPath $ExeSrc).Length)
Write-Output ("ENV: ini={0}" -f $IniPath)
