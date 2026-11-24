# probe_drag.ps1 -- why does the injected drag not move the ball?
. "$PSScriptRoot\nb.ps1"
Write-Output "PROBE drag"

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class NBProbe {
    [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
    [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int n);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern int GetWindowLongW(IntPtr h, int idx);
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X; public int Y; }
    public static string Describe(IntPtr h) {
        if (h == IntPtr.Zero) return "0";
        var sb = new StringBuilder(256); GetClassNameW(h, sb, sb.Capacity);
        uint p; GetWindowThreadProcessId(h, out p);
        return string.Format("hwnd=0x{0:X} class={1} pid={2} ex=0x{3:X}", h.ToInt64(), sb.ToString(), p, GetWindowLongW(h, -20));
    }
}
'@ -ErrorAction Stop

Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx 700 -posy 320
$p = Start-Ball -SettleMs 1500
$rc = Get-BallRect $p.Hwnd
Write-Output ("ball " + [NBT7b]::WindowInfo($p.Hwnd))
$cx = $rc.L + [int]($rc.W / 2)
$cy = $rc.T + [int]($rc.H / 2)
$pt = New-Object 'NBProbe+POINT'
$pt.X = $cx; $pt.Y = $cy
$hit = [NBProbe]::WindowFromPoint($pt)
Write-Output ("cursor target at ({0},{1}) -> {2}" -f $cx, $cy, [NBProbe]::Describe($hit))
Write-Output ("foreground -> " + [NBProbe]::Describe([NBProbe]::GetForegroundWindow()))

[void][NBT7b]::SetCursorPos($cx, $cy)
Start-Sleep -Milliseconds 300
$c2 = New-Object 'NBT7b+POINT'
[void][NBT7b]::GetCursorPos([ref]$c2)
Write-Output ("cursor now ({0},{1})" -f $c2.X, $c2.Y)
[NBT7b]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 250
for ($i = 1; $i -le 6; $i++) {
    [void][NBT7b]::SetCursorPos(($cx - 30 * $i), ($cy - 20 * $i))
    Start-Sleep -Milliseconds 90
}
[NBT7b]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
Start-Sleep -Milliseconds 600
Write-Output ("after physical drag " + [NBT7b]::WindowInfo($p.Hwnd))

# ---- attempt 2: same cursor motion, but the button/move messages are posted.
# If this moves the window, the app's drag logic is fine and only the injected
# physical input is being swallowed (foreground-window mouse capture rule).
$rc = Get-BallRect $p.Hwnd
[void][NBT7b]::SetCursorPos($cx, $cy)
Start-Sleep -Milliseconds 200
$lpDown = [IntPtr](($cy - $rc.T) * 65536 + ($cx - $rc.L))
[void][NBT7b]::PostMessage($p.Hwnd, 0x0201, [IntPtr]1, $lpDown)
Start-Sleep -Milliseconds 200
$lastX = $cx
$lastY = $cy
for ($i = 1; $i -le 6; $i++) {
    $lastX = $cx - 30 * $i
    $lastY = $cy - 20 * $i
    [void][NBT7b]::SetCursorPos($lastX, $lastY)
    $lpMove = [IntPtr](($lastY - $rc.T) * 65536 + ($lastX - $rc.L))
    [void][NBT7b]::PostMessage($p.Hwnd, 0x0200, [IntPtr]1, $lpMove)
    Start-Sleep -Milliseconds 90
}
[void][NBT7b]::PostMessage($p.Hwnd, 0x0202, [IntPtr]0, $lpDown)
Start-Sleep -Milliseconds 600
Write-Output ("after posted drag " + [NBT7b]::WindowInfo($p.Hwnd))
Stop-BallPid $p.Id
Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
