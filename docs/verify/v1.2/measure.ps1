<#
  NetBall v1.2.0 验收脚本（分层窗口截图 + 进程占用）

  为什么用 PrintWindow 而不是 CopyFromScreen：
    v1.2 的两个显示面都是 WS_EX_LAYERED（per-pixel alpha）窗口，GDI 抓屏
    （BitBlt / Graphics.CopyFromScreen）默认不包含分层窗口，会拍到一片纯色，
    从而把「正常渲染」误判成「什么都没画」。PrintWindow(PW_RENDERFULLCONTENT)
    能拿到窗口自己的分层表面。

  用法：
    powershell -NoProfile -ExecutionPolicy Bypass -File docs\verify\v1.2\measure.ps1
  输出：
    build\_tmp\v12\ball.png / taskbar.png / glass.png
    stdout 摘要（矩形、亮像素、WS/Private/句柄/线程）
#>
[CmdletBinding()]
param(
    [string]$OutDir = 'build\_tmp\v12'
)

$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..\..\..')

$out = Join-Path (Get-Location) $OutDir
if (-not (Test-Path -LiteralPath $out)) { New-Item -ItemType Directory -Path $out -Force | Out-Null }

Add-Type -AssemblyName System.Drawing
$cs = @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class NBV12 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  public static IntPtr Find(string cls) {
    IntPtr hit = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      var sb = new StringBuilder(256);
      if (GetClassNameW(h, sb, sb.Capacity) > 0 && sb.ToString() == cls) { hit = h; return false; }
      return true;
    }, IntPtr.Zero);
    return hit;
  }
}
'@
try { Add-Type -TypeDefinition $cs -ErrorAction Stop } catch {
    if ($_.Exception.Message -notmatch 'already exists') { throw }
}

# -4 = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2，必须早于任何窗口查询
[void][NBV12]::SetProcessDpiAwarenessContext([IntPtr](-4))

$proc = Get-Process -Name NetBall -ErrorAction SilentlyContinue
if (-not $proc) {
    Write-Output 'RESULT: SKIP (NetBall is not running)'
    exit 2
}
$proc = $proc[0]
Write-Output ("process : pid={0} ws={1:N2}MB private={2:N2}MB handles={3} threads={4}" -f `
    $proc.Id, ($proc.WorkingSet64 / 1MB), ($proc.PrivateMemorySize64 / 1MB), $proc.HandleCount, $proc.Threads.Count)

$targets = @(
    @{ cls = 'NetBallTaskbarWnd'; file = 'taskbar.png' },
    @{ cls = 'NetBallBallWnd';    file = 'ball.png' },
    @{ cls = 'NetBallGlassWnd';   file = 'glass.png' }
)

$fail = 0
foreach ($t in $targets) {
    $h = [NBV12]::Find($t.cls)
    if ($h -eq [IntPtr]::Zero) {
        Write-Output ("{0,-20} : NOT FOUND" -f $t.cls)
        if ($t.cls -ne 'NetBallGlassWnd') { $fail++ }   # 玻璃底窗只在亚克力可用时存在
        continue
    }

    $r = New-Object 'NBV12+RECT'
    [void][NBV12]::GetWindowRect($h, [ref]$r)
    $w = $r.R - $r.L
    $hh = $r.B - $r.T
    if ($w -le 0 -or $hh -le 0) { Write-Output ("{0,-20} : bad rect" -f $t.cls); $fail++; continue }

    $bmp = New-Object System.Drawing.Bitmap($w, $hh)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    $ok = [NBV12]::PrintWindow($h, $hdc, 2)
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    $path = Join-Path $out $t.file
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)

    # 统计不透明像素与「文字亮像素」，用于判断是不是只画了底板
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $hh)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $buf = New-Object byte[] ($data.Stride * $hh)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
    $bmp.UnlockBits($data)
    $bmp.Dispose()

    $opaque = 0; $bright = 0
    for ($i = 0; $i -lt $buf.Length; $i += 4) {
        if ($buf[$i + 3] -gt 10) { $opaque++ }
        $luma = 0.299 * $buf[$i + 2] + 0.587 * $buf[$i + 1] + 0.114 * $buf[$i]
        if ($luma -gt 150 -and $buf[$i + 3] -gt 128) { $bright++ }
    }

    $vis = [NBV12]::IsWindowVisible($h)
    Write-Output ("{0,-20} : visible={1} rect={2}x{3}@({4},{5}) printwindow={6} opaque_px={7} text_px={8} -> {9}" -f `
        $t.cls, $vis, $w, $hh, $r.L, $r.T, $ok, $opaque, $bright, $t.file)

    if ($t.cls -ne 'NetBallGlassWnd' -and $bright -lt 20) { $fail++ }
}

if ($fail -gt 0) {
    Write-Output "RESULT: FAIL ($fail check(s) failed)"
    exit 1
}
Write-Output 'RESULT: PASS'
exit 0
