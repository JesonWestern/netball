# verifier-4: time the shared capture tool against the LIVE peer instance and
# immediately OCR the result, so we learn (a) capture cost, (b) OCR readability.
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$shot = Join-Path $root 'docs\verify\media-v4\probe-live.png'
$cap  = Join-Path $root 'docs\verify\check_screenshot.ps1'
$ocr  = Join-Path $root 'docs\verify\verifier_ocr.ps1'

# who is running?
$procs = Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -like 'NetBall*' }
if ($procs) {
    foreach ($p in $procs) { Write-Output ("proc pid={0} name={1}" -f $p.Id, $p.ProcessName) }
} else {
    Write-Output 'proc none'
}

$name = 'NetBall'
if ($procs) { $name = $procs[0].ProcessName }

Write-Output ''
Write-Output "=== capture via check_screenshot.ps1 (ProcessName=$name) ==="
$sw = [Diagnostics.Stopwatch]::StartNew()
$out = & powershell -NoProfile -ExecutionPolicy Bypass -File $cap -OutFile $shot -ClassName 'NetBallBallWnd' -ProcessName $name -ExpectText 2>&1
$rcCap = $LASTEXITCODE
$sw.Stop()
$out | ForEach-Object { Write-Output ("  " + $_) }
Write-Output ("CAPTURE_SECONDS={0:N2} rc={1}" -f $sw.Elapsed.TotalSeconds, $rcCap)

Write-Output ''
Write-Output '=== OCR via verifier_ocr.ps1 (-Scale 6 -Threshold 110 -CropLeftPct 0.18) ==='
$sw2 = [Diagnostics.Stopwatch]::StartNew()
$out2 = & powershell -NoProfile -ExecutionPolicy Bypass -File $ocr -Path $shot -Scale 6 -Threshold 110 -CropLeftPct 0.18 2>&1
$rc2 = $LASTEXITCODE
$sw2.Stop()
$out2 | ForEach-Object { Write-Output ("  " + $_) }
Write-Output ("OCR_SECONDS={0:N2} rc={1}" -f $sw2.Elapsed.TotalSeconds, $rc2)

Write-Output ''
Write-Output '=== dwMemoryLoad (independent) ==='
$src = @'
using System;
using System.Runtime.InteropServices;
public class V4Mem {
    [StructLayout(LayoutKind.Sequential)]
    public struct MEMORYSTATUSEX {
        public uint dwLength; public uint dwMemoryLoad;
        public ulong ullTotalPhys; public ulong ullAvailPhys;
        public ulong ullTotalPageFile; public ulong ullAvailPageFile;
        public ulong ullTotalVirtual; public ulong ullAvailVirtual;
        public ulong ullAvailExtendedVirtual;
    }
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX lpBuffer);
    public static uint Load() {
        MEMORYSTATUSEX m = new MEMORYSTATUSEX();
        m.dwLength = (uint)Marshal.SizeOf(typeof(MEMORYSTATUSEX));
        if (!GlobalMemoryStatusEx(ref m)) throw new Exception("GlobalMemoryStatusEx failed");
        return m.dwMemoryLoad;
    }
}
'@
try { Add-Type -TypeDefinition $src -ErrorAction Stop } catch { if ($_.Exception.Message -notmatch 'already exists') { throw } }
$load = [V4Mem]::Load()
Write-Output ("dwMemoryLoad={0}" -f $load)
Write-Output ("PRECHECK mem_delta=|ball - system| will be computed after OCR")
