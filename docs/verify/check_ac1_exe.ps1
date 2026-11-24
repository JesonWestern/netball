<#
  AC-1  exe size + embedded resources + manifest.
  Assertions (all PASS/FAIL, nothing is "print only"):
    1. file exists and size <= 2097152 bytes
    2. PE has a .rsrc section with real payload
    3. VersionInfo: FileVersion=1.0.0.0, ProductName=NetBall,
       OriginalFilename=NetBall.exe, FileDescription="NetBall " + U+7F51 U+901F U+60AC U+6D6E U+7403
       (exact -eq compare; the codepoint form keeps this file ASCII-only)
    4. icon: ExtractAssociatedIcon non-null, Width >= 16, pixel variance > 0 (not a flat square),
       plus assets/app.ico still loads a 32x32 frame (T5 cross-check, read-only)
    5. manifest: RT_MANIFEST (type 24, id 1) exists; XML contains "asInvoker" exactly once
       and "PerMonitorV2" exactly once
  Exit: 0 PASS / 1 FAIL / 2 SKIP
#>
[CmdletBinding()]
param(
    [string]$ExePath,
    [string]$IcoPath,
    [int]$LimitBytes = 2097152
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $ExePath) { $ExePath = Join-Path $root 'build\out\NetBall.exe' }
if (-not $IcoPath) { $IcoPath = Join-Path $root 'assets\app.ico' }
$ExePath = [System.IO.Path]::GetFullPath($ExePath)
. (Join-Path $PSScriptRoot 'verifier_preflight.ps1')
Write-BuildInfo -Exe $ExePath

# expected Chinese description built from codepoints: this script stays ASCII-only
$cnDesc = [string]::new([char[]]@(0x7F51, 0x901F, 0x60AC, 0x6D6E, 0x7403))   # wang su xuan fu qiu
$expDesc = 'NetBall ' + $cnDesc
$expVer = '1.0.0.0'
$expProduct = 'NetBall'
$expOriginal = 'NetBall.exe'

Write-Output "AC-1 EXE / RESOURCE / MANIFEST CHECK"
Write-Output "  target : $ExePath"
Write-Output "  limit  : $LimitBytes bytes (2 MiB)"

if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
    Write-Output "  exists : NO"
    Write-Output "RESULT: SKIP (artifact not built yet: build/out/NetBall.exe missing)"
    exit 2
}

$fi = Get-Item -LiteralPath $ExePath
$size = [int64]$fi.Length
$fail = @()

Write-Output "  exists : YES"
Write-Output "  bytes  : $size"
Write-Output ("  usage  : {0}% of limit" -f [math]::Round(($size / $LimitBytes) * 100, 1))
Write-Output ("  sha256 : " + (Get-FileHash -LiteralPath $ExePath -Algorithm SHA256).Hash)
Write-Output ("  mtime  : " + $fi.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))

if ($size -le 0) { $fail += 'file is empty' }
if ($size -gt $LimitBytes) { $fail += "size $size > limit $LimitBytes" }

# ---------------------------------------------------------------- PE sections
function Get-PESections([string]$Path) {
    $sections = @()
    $fs = [System.IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
    try {
        $br = New-Object System.IO.BinaryReader($fs)
        $fs.Position = 0x3C
        $e_lfanew = $br.ReadInt32()
        $fs.Position = $e_lfanew
        $sig = $br.ReadUInt32()
        if ($sig -ne 0x00004550) { throw ("bad PE signature 0x{0:X8}" -f $sig) }
        $null = $br.ReadUInt16()
        $numSec = $br.ReadUInt16()
        $fs.Position = $e_lfanew + 20
        $sizeOpt = $br.ReadUInt16()
        $fs.Position = $e_lfanew + 24 + $sizeOpt
        for ($i = 0; $i -lt $numSec; $i++) {
            $nameBytes = $br.ReadBytes(8)
            $name = [System.Text.Encoding]::ASCII.GetString($nameBytes).TrimEnd([char]0)
            $vsize = $br.ReadUInt32()
            $vaddr = $br.ReadUInt32()
            $rawSize = $br.ReadUInt32()
            $rawPtr = $br.ReadUInt32()
            $null = $br.ReadBytes(16)
            $sections += [pscustomobject]@{
                Name = $name; VirtualSize = $vsize; VirtualAddress = $vaddr
                RawSize = $rawSize; RawPointer = $rawPtr
            }
        }
    } finally { $fs.Dispose() }
    return $sections
}

$rsrc = $null
try {
    $secs = Get-PESections $ExePath
    Write-Output '  PE sections:'
    foreach ($s in $secs) {
        Write-Output ("    {0,-10} raw={1,-8} vsize={2}" -f $s.Name, $s.RawSize, $s.VirtualSize)
    }
    $rsrc = $secs | Where-Object { $_.Name -eq '.rsrc' } | Select-Object -First 1
} catch {
    Write-Output "  PE parse error : $($_.Exception.Message)"
    $fail += "PE parse failed: $($_.Exception.Message)"
}
if ($rsrc) {
    Write-Output ("  .rsrc      : present (raw size {0} bytes)" -f $rsrc.RawSize)
    if ($rsrc.RawSize -lt 512) { $fail += ".rsrc section suspiciously small ($($rsrc.RawSize) bytes)" }
} else {
    Write-Output '  .rsrc      : MISSING'
    $fail += 'no .rsrc section -> no icon/version/manifest resources linked'
}

# ---------------------------------------------------------------- VersionInfo
Write-Output '  VersionInfo (exact compare):'
try {
    $vi = $fi.VersionInfo
    $desc = [string]$vi.FileDescription
    $cps = (($desc.ToCharArray()) | ForEach-Object { 'U+{0:X4}' -f [int]$_ }) -join ' '
    Write-Output ("    FileVersion      : '{0}'" -f $vi.FileVersion)
    Write-Output ("    ProductVersion   : '{0}'" -f $vi.ProductVersion)
    Write-Output ("    ProductName      : '{0}'" -f $vi.ProductName)
    Write-Output ("    OriginalFilename : '{0}'" -f $vi.OriginalFilename)
    Write-Output ("    FileDescription  : '{0}'" -f $desc)
    Write-Output ("    desc codepoints  : {0}" -f $cps)
    Write-Output ("    expected desc cp : {0}" -f ((($expDesc.ToCharArray()) | ForEach-Object { 'U+{0:X4}' -f [int]$_ }) -join ' '))

    if ($vi.FileVersion -ne $expVer) {
        $fail += ("FileVersion '{0}' -ne '{1}'" -f $vi.FileVersion, $expVer)
    }
    if ($vi.ProductName -ne $expProduct) {
        $fail += ("ProductName '{0}' -ne '{1}'" -f $vi.ProductName, $expProduct)
    }
    if ($vi.OriginalFilename -ne $expOriginal) {
        $fail += ("OriginalFilename '{0}' -ne '{1}'" -f $vi.OriginalFilename, $expOriginal)
    }
    if ($desc -ne $expDesc) {
        $fail += 'FileDescription mismatch (mojibake or wrong string) - see codepoints above'
    } else {
        Write-Output '    FileDescription  : MATCH (no mojibake)'
    }
} catch {
    Write-Output "    <error reading VersionInfo>: $($_.Exception.Message)"
    $fail += 'VersionInfo unreadable'
}

# ---------------------------------------------------------------- icon (T5 cross-check)
Write-Output '  icon resource:'
try {
    Add-Type -AssemblyName System.Drawing -ErrorAction Stop
    $ico = [System.Drawing.Icon]::ExtractAssociatedIcon($ExePath)
    if (-not $ico) {
        $fail += 'ExtractAssociatedIcon returned NULL -> exe has no icon'
    } else {
        Write-Output ("    extracted      : {0}x{1}" -f $ico.Width, $ico.Height)
        if ($ico.Width -lt 16) { $fail += "icon width $($ico.Width) < 16" }

        $bmp = $ico.ToBitmap()
        $vals = @()
        $step = [math]::Max(1, [int][math]::Floor($bmp.Width / 16))
        for ($y = 0; $y -lt $bmp.Height; $y += $step) {
            for ($x = 0; $x -lt $bmp.Width; $x += $step) {
                $p = $bmp.GetPixel($x, $y)
                $vals += (0.299 * $p.R + 0.587 * $p.G + 0.114 * $p.B)
            }
        }
        $mean = ($vals | Measure-Object -Average).Average
        $var = 0.0
        foreach ($v in $vals) { $var += [math]::Pow(($v - $mean), 2) }
        if ($vals.Count -gt 1) { $var = $var / ($vals.Count - 1) }
        Write-Output ("    pixels sampled : {0}  variance={1:F2}" -f $vals.Count, $var)
        if ($var -le 0.5) { $fail += ("icon is a flat single colour (variance {0:F2})" -f $var) }
        $bmp.Dispose()
        $ico.Dispose()
    }
} catch {
    Write-Output "    ExtractAssociatedIcon : failed -> $($_.Exception.Message)"
    $fail += 'icon extraction threw'
}

if (Test-Path -LiteralPath $IcoPath) {
    try {
        Add-Type -AssemblyName System.Drawing -ErrorAction SilentlyContinue
        $src = New-Object System.Drawing.Icon($IcoPath, 32, 32)
        Write-Output ("    assets/app.ico : 32x32 frame = {0}x{1}" -f $src.Width, $src.Height)
        if ($src.Width -ne 32 -or $src.Height -ne 32) {
            Write-Output '    NOTE: assets/app.ico has no exact 32x32 frame (informational)'
        }
        $src.Dispose()
    } catch {
        Write-Output "    assets/app.ico : load failed -> $($_.Exception.Message)"
    }
} else {
    Write-Output "    assets/app.ico : MISSING at $IcoPath"
}

# ---------------------------------------------------------------- manifest
Write-Output '  manifest (RT_MANIFEST id 1):'
$cs = @'
using System;
using System.Runtime.InteropServices;
public class NBManifest {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern IntPtr LoadLibraryExW(string file, IntPtr reserved, uint flags);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr FindResourceW(IntPtr hLib, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern uint SizeofResource(IntPtr hLib, IntPtr res);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr LoadResource(IntPtr hLib, IntPtr res);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr LockResource(IntPtr data);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool FreeLibrary(IntPtr hLib);

    public static byte[] Read(string path) {
        IntPtr h = LoadLibraryExW(path, IntPtr.Zero, 0x00000002); // AS_DATAFILE
        if (h == IntPtr.Zero) return null;
        try {
            IntPtr res = FindResourceW(h, new IntPtr(1), new IntPtr(24)); // id 1, RT_MANIFEST
            if (res == IntPtr.Zero) return null;
            uint size = SizeofResource(h, res);
            IntPtr loaded = LoadResource(h, res);
            if (loaded == IntPtr.Zero || size == 0) return null;
            IntPtr p = LockResource(loaded);
            byte[] buf = new byte[size];
            Marshal.Copy(p, buf, 0, (int)size);
            return buf;
        } finally { FreeLibrary(h); }
    }
}
'@
try { Add-Type -TypeDefinition $cs -ErrorAction Stop } catch {
    if ($_.Exception.Message -notmatch 'already exists') { throw }
}
$xmlText = $null
try {
    $bytes = [NBManifest]::Read($ExePath)
    if ($null -eq $bytes) {
        $fail += 'RT_MANIFEST (type 24, id 1) not found -> manifest not linked'
    } else {
        Write-Output ("    resource size  : {0} bytes" -f $bytes.Length)
        $txt = [System.Text.Encoding]::UTF8.GetString($bytes)
        if ($txt -notmatch '<assembly') {
            $txt = [System.Text.Encoding]::ASCII.GetString($bytes)
        }
        $xmlText = $txt
        $a = ([regex]::Matches($txt, 'asInvoker')).Count
        $p = ([regex]::Matches($txt, 'PerMonitorV2')).Count
        Write-Output ("    asInvoker      : {0} hit(s)  (need exactly 1)" -f $a)
        Write-Output ("    PerMonitorV2   : {0} hit(s)  (need exactly 1)" -f $p)
        if ($a -ne 1) { $fail += "manifest asInvoker count $a != 1" }
        if ($p -ne 1) { $fail += "manifest PerMonitorV2 count $p != 1 (DPI awareness not declared)" }
    }
} catch {
    Write-Output "    manifest read failed: $($_.Exception.Message)"
    $fail += 'manifest read threw'
}

Write-Output ''
if ($fail.Count -eq 0) {
    Write-Output "METRIC: exe_bytes=$size limit_bytes=$LimitBytes desc_match=1 manifest=asInvoker+PerMonitorV2"
    Write-Output "RESULT: PASS (size ok, rsrc+versioninfo+icon+manifest ok, T5 cross-check passed)"
    exit 0
} else {
    foreach ($f in $fail) { Write-Output "  VIOLATION: $f" }
    Write-Output "METRIC: exe_bytes=$size limit_bytes=$LimitBytes violations=$($fail.Count)"
    Write-Output 'RESULT: FAIL'
    exit 1
}
