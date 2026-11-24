# run_A.ps1 -- T14 group A : D-01 negative-coordinate position memory (3 paths).
# Paths:  (1) posx=-500,posy=300 -> start -> GetWindowRect must be x ~ -500
#         (2) with the ball at -500, open settings, click a preset swatch, click Save
#             -> the ball must still be at -500 (old build jumped to the primary
#                bottom-right corner through applyConfig)
#         (3) posx=99999,posy=99999 (illegal) -> start -> must fall back to a
#             position that really intersects the virtual screen (never 99999)
# ASCII-only; all output goes to the log file.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\t7b\nb.ps1"

$MediaDir = Join-Path $VerifyD 'media-v5'
$TmpDir   = Join-Path $Root 'build\_tmp\v5'
$ExeCopy  = Join-Path $TmpDir 'NetBall_v5.exe'
$ProcName = 'NetBall_v5'
foreach ($d in @($MediaDir, $TmpDir)) {
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class NBT14 {
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int n);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder sb, int n);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
    public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    public static string Cls(IntPtr h) { var sb = new StringBuilder(256); GetClassNameW(h, sb, sb.Capacity); return sb.ToString(); }
    public static string Txt(IntPtr h) { var sb = new StringBuilder(256); GetWindowTextW(h, sb, sb.Capacity); return sb.ToString(); }
    public static IntPtr FindTopLevel(uint pid, string excludeCls) {
        IntPtr hit = IntPtr.Zero;
        EnumWindows(delegate(IntPtr h, IntPtr l) {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p != pid) return true;
            if (Cls(h) == excludeCls) return true;
            if (!IsWindowVisible(h)) return true;
            RECT r; GetWindowRect(h, out r);
            if ((r.Right - r.Left) < 200) return true;   // the tray/menu helper windows are tiny
            hit = h; return false;
        }, IntPtr.Zero);
        return hit;
    }
}
'@

$VIRT_L = -1920
$VIRT_T = 0
$VIRT_R = 3840
$VIRT_B = 2160

function Get-RectInfo([IntPtr]$h) {
    $r = New-Object 'NBT7b+RECT'
    [void][NBT7b]::GetWindowRect($h, [ref]$r)
    return [pscustomobject]@{ L = $r.Left; T = $r.Top; R = $r.Right; B = $r.Bottom
                              W = ($r.Right - $r.Left); H = ($r.Bottom - $r.Top) }
}
function Show-Rect([string]$tag, [IntPtr]$h) {
    $r = Get-RectInfo $h
    Write-Output ("  {0}: L={1} T={2} R={3} B={4} W={5} H={6}" -f $tag, $r.L, $r.T, $r.R, $r.B, $r.W, $r.H)
    return $r
}

Write-Output "=========== T14 group A : D-01 position memory ==========="
$anchor = Initialize-Measurement -WaitMs 8000
Write-Output ("  anchor sha = {0} bytes={1} mtime={2}" -f $anchor.Sha, $anchor.Bytes, $anchor.Mtime)
Write-Output ("  virtual screen x=[{0}..{1}] y=[{2}..{3}]" -f $VIRT_L, $VIRT_R, $VIRT_T, $VIRT_B)

# ------------------------------------------------------------------ path 1
Write-Output ""
Write-Output "--- A1: posx=-500 posy=300 (legal point on the monitor left of the primary) ---"
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -500 -posy 300
Write-Output ("  ini written posx=-500 posy=300 -> {0}" -f ((Read-Cfg).posx + ',' + (Read-Cfg).posy))
$p1 = Start-Ball -SettleMs 1600
if ($p1.Hwnd -eq [IntPtr]::Zero) { Write-Output "RESULT: BLOCKED (ball window never appeared, A1)"; exit 3 }
$r1 = Show-Rect 'A1 rect' $p1.Hwnd
$png1 = Join-Path $MediaDir 'a1-neg500.png'
$null = Capture-Region -x ($r1.L - 20) -y ($r1.T - 20) -w ($r1.W + 40) -h ($r1.H + 40) -path $png1
$d1 = [math]::Abs($r1.L - (-500))
$a1ok = ($d1 -le 2) -and ($r1.T -ge 297) -and ($r1.T -le 303)
Add-Check 'A1.neg500.restored' $a1ok ("rect.L={0} (want -500 +-2, |delta|={1}); rect.T={2} (want 300); fellback_to_3662={3}" -f $r1.L, $d1, $r1.T, ($r1.L -eq 3662))

# ------------------------------------------------------------------ path 2
Write-Output ""
Write-Output "--- A2: settings window -> click a preset colour swatch -> Save (IDC_SAVE=3016) ---"
$cfgBefore = Read-Cfg
Write-Output ("  cfg before save: bgcolor={0} textauto={1} posx={2} posy={3}" -f $cfgBefore.bgcolor, $cfgBefore.textauto, $cfgBefore.posx, $cfgBefore.posy)
Send-Cmd $p1.Hwnd 1001            # WM_COMMAND IDM_SETTINGS -> modal settings window
$dlg = [IntPtr]::Zero
for ($i = 0; $i -lt 40; $i++) {
    Start-Sleep -Milliseconds 150
    $dlg = [NBT14]::FindTopLevel([uint32]$p1.Pid, $ClsBall)
    if ($dlg -ne [IntPtr]::Zero) { break }
}
if ($dlg -eq [IntPtr]::Zero) {
    Add-Check 'A2.settings.opens' $false "no top-level settings window appeared for pid $($p1.Pid)"
} else {
    Add-Check 'A2.settings.opens' $true ("hwnd=0x{0:X} class={1} title={2}" -f [int64]$dlg, [NBT14]::Cls($dlg), [NBT14]::Txt($dlg))
    $dg = Show-Rect 'A2 settings rect' $dlg
    $dpi = [NBT14]::GetDpiForWindow($dlg)
    Write-Output ("  settings dpi={0} (scale {1})" -f $dpi, ([double]$dpi / 96.0))
    # swatch[0] client rect in DIP = (88,254)-(110,276); centre (99,265) scaled by dpi/96
    $sx = [int][math]::Round(99.0 * $dpi / 96.0)
    $sy = [int][math]::Round(265.0 * $dpi / 96.0)
    $pt = New-Object 'NBT14+POINT'
    $pt.X = $sx; $pt.Y = $sy
    [void][NBT14]::ClientToScreen($dlg, [ref]$pt)
    Write-Output ("  swatch[0] client=({0},{1}) -> screen=({2},{3})" -f $sx, $sy, $pt.X, $pt.Y)
    $pngDlg = Join-Path $MediaDir 'a2-settings.png'
    $null = Capture-Region -x $dg.L -y $dg.T -w $dg.W -h $dg.H -path $pngDlg
    # WM_LBUTTONDOWN with MK_LBUTTON: the dialog hit-tests the preset swatches
    $lparam = [IntPtr](($sx -band 0xFFFF) -bor (($sy -band 0xFFFF) -shl 16))
    [void][NBT14]::PostMessage($dlg, 0x0201, [IntPtr]1, $lparam)
    Start-Sleep -Milliseconds 500
    # then BN_CLICKED on the Save button (IDC_SAVE = 3016)
    [void][NBT14]::PostMessage($dlg, 0x0111, [IntPtr]3016, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 1200
    $still = [NBT14]::GetWindowRect($dlg, [ref](New-Object 'NBT14+RECT'))
    $r2 = Show-Rect 'A2 rect after save' $p1.Hwnd
    $d2 = [math]::Abs($r2.L - (-500))
    Add-Check 'A2.after-save.no-jump' ($d2 -le 2) ("rect.L={0} (want -500 +-2, |delta|={1}); jumped_to_3662={2}" -f $r2.L, $d2, ($r2.L -eq 3662))
    $cfgAfter = Read-Cfg
    Write-Output ("  cfg after save : bgcolor={0} textauto={1} posx={2} posy={3}" -f $cfgAfter.bgcolor, $cfgAfter.textauto, $cfgAfter.posx, $cfgAfter.posy)
    $colorChanged = ($cfgAfter.bgcolor -ne $cfgBefore.bgcolor)
    Add-Check 'A2.save.really-applied' $colorChanged ("bgcolor {0} -> {1} (proves ApplyAndClose ran)" -f $cfgBefore.bgcolor, $cfgAfter.bgcolor)
    Add-Check 'A2.save.kept-neg-pos-in-ini' (($cfgAfter.posx -eq '-500') -and ($cfgAfter.posy -eq '300')) ("ini posx={0} posy={1}" -f $cfgAfter.posx, $cfgAfter.posy)
}
Stop-BallPid $p1.Pid
$null = Wait-NoBall 5000
Write-Output ("  processes after A1/A2: {0}" -f (Get-ProcList).Count)

# ------------------------------------------------------------------ path 3
Write-Output ""
Write-Output "--- A3: posx=99999 posy=99999 (illegal) -> must fall back to a visible spot ---"
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx 99999 -posy 99999
$p3 = Start-Ball -SettleMs 1600
if ($p3.Hwnd -eq [IntPtr]::Zero) { Write-Output "RESULT: BLOCKED (ball window never appeared, A3)"; exit 3 }
$r3 = Show-Rect 'A3 rect' $p3.Hwnd
$png3 = Join-Path $MediaDir 'a3-illegal.png'
$null = Capture-Region -x ($r3.L - 20) -y ($r3.T - 20) -w ($r3.W + 40) -h ($r3.H + 40) -path $png3
$interX = [math]::Min($r3.R, $VIRT_R) - [math]::Max($r3.L, $VIRT_L)
$interY = [math]::Min($r3.B, $VIRT_B) - [math]::Max($r3.T, $VIRT_T)
$a3ok = ($interX -gt 0) -and ($interY -gt 0) -and ($r3.L -lt 10000)
Add-Check 'A3.illegal.coerced-visible' $a3ok ("rect.L={0} T={1} intersect=({2}x{3}) px, inside_virtual={4}" -f $r3.L, $r3.T, $interX, $interY, (($interX -ge $r3.W) -and ($interY -ge $r3.H)))
Stop-BallPid $p3.Pid
$null = Wait-NoBall 5000

# ------------------------------------------------------------------ restore
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
Write-Output ""
Write-Output ("  config restored to baseline: posx={0} posy={1} bg={2}" -f (Read-Cfg).posx, (Read-Cfg).posy, (Read-Cfg).bgcolor)
$fails = Get-FailCount
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
