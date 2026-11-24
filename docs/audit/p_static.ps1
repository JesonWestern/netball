# auditor / T10 - static checks on delivered artifacts (read-only)
$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$exe  = Join-Path $root 'build\out\NetBall.exe'
$setup= Join-Path $root 'dist\NetBallSetup-1.0.0.exe'
$out  = @()

$out += "ROOT=$root"
foreach ($f in @($exe, $setup)) {
    if (Test-Path $f) {
        $fi = Get-Item $f
        $h  = (Get-FileHash $f -Algorithm SHA256).Hash
        $out += ("FILE {0} | bytes={1} | mtime={2:yyyy-MM-dd HH:mm:ss.fff} | sha256={3}" -f $fi.Name, $fi.Length, $fi.LastWriteTime, $h)
    } else {
        $out += "MISSING $f"
    }
}

# ---- AC-1: version resource ----
$vi = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($exe)
$out += ("AC1 FileDescription='{0}'" -f $vi.FileDescription)
$out += ("AC1 cp=" + (($vi.FileDescription.ToCharArray() | ForEach-Object { 'U+{0:X4}' -f [int]$_ }) -join ' '))
$out += ("AC1 FileVersion={0} ProductName={1} ProductVersion={2} OriginalFilename={3} CompanyName={4} Language={5}" -f $vi.FileVersion, $vi.ProductName, $vi.ProductVersion, $vi.OriginalFilename, $vi.CompanyName, $vi.Language)
$out += ("AC1 want_cp=" + (('NetBall ' + [char]0x7F51 + [char]0x901F + [char]0x60AC + [char]0x6D6E + [char]0x7403).ToCharArray() | ForEach-Object { 'U+{0:X4}' -f [int]$_ }) -join ' ')
$out += ("AC1 desc_equal={0}" -f ($vi.FileDescription -ceq ('NetBall ' + [char]0x7F51 + [char]0x901F + [char]0x60AC + [char]0x6D6E + [char]0x7403)))

# ---- AC-1: icon present (extract, measure variance) ----
Add-Type -AssemblyName System.Drawing
try {
    $ico = [System.Drawing.Icon]::ExtractAssociatedIcon($exe)
    $bmp = $ico.ToBitmap()
    $vals = New-Object System.Collections.ArrayList
    for ($y = 0; $y -lt $bmp.Height; $y += 1) {
        for ($x = 0; $x -lt $bmp.Width; $x += 1) {
            $c = $bmp.GetPixel($x, $y)
            [void]$vals.Add([double]($c.R + $c.G + $c.B))
        }
    }
    $mean = ($vals | Measure-Object -Average).Average
    $var = 0.0
    foreach ($v in $vals) { $var += [math]::Pow($v - $mean, 2) }
    $var = $var / $vals.Count
    $out += ("AC1 icon={0}x{1} variance={2:N2}" -f $bmp.Width, $bmp.Height, $var)
    $bmp.Dispose(); $ico.Dispose()
} catch { $out += "AC1 icon_ERROR $_" }

# ---- AC-1: manifest strings inside exe ----
$bytes = [System.IO.File]::ReadAllBytes($exe)
$txt = [System.Text.Encoding]::ASCII.GetString($bytes)
foreach ($needle in @('PerMonitorV2', 'asInvoker', 'Microsoft.Windows.Common-Controls', 'requestedExecutionLevel')) {
    $n = ([regex]::Matches($txt, [regex]::Escape($needle))).Count
    $out += ("AC1 manifest_str '{0}' count={1}" -f $needle, $n)
}
# arch
$peOff = [BitConverter]::ToInt32($bytes, 0x3C)
$machine = [BitConverter]::ToUInt16($bytes, $peOff + 4)
$out += ("AC1 PE machine=0x{0:X4} (0x8664=x64)" -f $machine)

# ---- installer: encoding + payload smoke ----
$sb = [System.IO.File]::ReadAllBytes($setup)
$out += ("SETUP head={0:X2}{1:X2}{2:X2}{3:X2}" -f $sb[0], $sb[1], $sb[2], $sb[3])
$setupTxt = [System.Text.Encoding]::ASCII.GetString($sb)
$out += ("SETUP contains 'NetBall.exe'={0} 'unins000'={1} 'Inno Setup'={2}" -f $setupTxt.Contains('NetBall.exe'), $setupTxt.Contains('unins000'), $setupTxt.Contains('Inno Setup'))
$out += ("SETUP VersionInfo ProductName='{0}' FileVersion='{1}'" -f ([System.Diagnostics.FileVersionInfo]::GetVersionInfo($setup)).ProductName, ([System.Diagnostics.FileVersionInfo]::GetVersionInfo($setup)).FileVersion)

# ---- iss encoding ----
$iss = Join-Path $root 'installer\NetBall.iss'
$ib = [System.IO.File]::ReadAllBytes($iss)
$out += ("ISS bytes={0} head={1:X2}{2:X2}{3:X2}" -f $ib.Length, $ib[0], $ib[1], $ib[2])
$out += ("ISS utf8_bom={0}" -f ($ib[0] -eq 0xEF -and $ib[1] -eq 0xBB -and $ib[2] -eq 0xBF))
$issiTxt = [System.IO.File]::ReadAllText($iss, [System.Text.Encoding]::UTF8)

# ---- 10 hard requirements (mechanical string checks, ASCII-only matchers) ----
$issLines = [System.IO.File]::ReadAllLines($iss, [System.Text.Encoding]::UTF8)
$groupLines = @($issLines | Where-Object { $_ -match '^Name: "\{group\}\\' })
$appLines   = @($issLines | Where-Object { $_ -match '^Name: "\{app\}\\' })
$stretch    = @($issLines | Where-Object { $_ -match '^\s*WizardImageStretch\s*=' })
$checks = [ordered]@{
  'R1_group_two_icons'      = ($groupLines.Count -eq 2)
  'R2_app_uninstall_lnk'    = ($appLines.Count -eq 1) -and ($appLines[0] -match 'Filename: "\{uninstallexe\}"')
  'R3_ask_keep_config'      = ($issiTxt.Contains((0x662F,0x5426,0x4FDD,0x7559,0x4E2A,0x4EBA,0x914D,0x7F6E,0x4E0E,0x8BBE,0x7F6E | ForEach-Object { [char]$_ }) -join '')) -and ($issiTxt.Contains('DelTree(ConfigDir'))
  'R4_displayname_icon'     = ($issiTxt.Contains('UninstallDisplayName={#MyAppName}')) -and ($issiTxt.Contains('UninstallDisplayIcon={app}\{#MyAppExeName}')) -and ($issiTxt.Contains('CloseApplications=yes'))
  'R5_wizard_bmps'          = ($issiTxt.Contains('WizardImageFile=..\assets\installer\wizard-large.bmp')) -and ($issiTxt.Contains('WizardSmallImageFile=..\assets\installer\wizard-small.bmp')) -and ($stretch.Count -eq 0)
  'R6_infobefore_readme'    = ($issiTxt.Contains('InfoBeforeFile=..\assets\installer\README.txt')) -and ($issiTxt.Contains('Source: "..\assets\installer\README.txt"; DestDir: "{app}"'))
  'R7_desktop_optional'     = ($issiTxt.Contains('Name: "desktopicon"')) -and ($issiTxt.Contains('Tasks: desktopicon')) -and ($issiTxt.Contains('checkedonce'))
  'R8_appid_fixed'          = $issiTxt.Contains('AppId={#MyAppId}')
  'R9_minversion'           = ($issiTxt.Contains('MinVersion=10.0.17763')) -and ($issiTxt.Contains('ArchitecturesAllowed=x64compatible'))
  'R10_uninstalldelete'     = ($issiTxt.Contains('[UninstallDelete]')) -and ($issiTxt.Contains('Type: filesandordirs; Name: "{app}"'))
}
foreach ($k in $checks.Keys) { $out += ("ISS_{0}={1}" -f $k, $checks[$k]) }

# ---- build.bat / package.bat ASCII-ness ----
foreach ($b in @('build\build.bat','build\package.bat')) {
    $bp = Join-Path $root $b
    if (Test-Path $bp) {
        $bb = [System.IO.File]::ReadAllBytes($bp)
        $nonAscii = ($bb | Where-Object { $_ -ge 0x80 }).Count
        $out += ("ASCII {0} bytes={1} non_ascii={2}" -f $b, $bb.Length, $nonAscii)
    } else { $out += "ASCII $b MISSING" }
}

# ---- README encoding ----
$rm = Join-Path $root 'assets\installer\README.txt'
$rb = [System.IO.File]::ReadAllBytes($rm)
$out += ("README bytes={0} head={1:X2}{2:X2}{3:X2} bom={4}" -f $rb.Length, $rb[0], $rb[1], $rb[2], ($rb[0] -eq 0xEF -and $rb[1] -eq 0xBB -and $rb[2] -eq 0xBF))
$crlf = ([regex]::Matches([System.Text.Encoding]::UTF8.GetString($rb), "`r`n")).Count
$lf   = ([regex]::Matches([System.Text.Encoding]::UTF8.GetString($rb), "(?<!`r)`n")).Count
$out += ("README crlf={0} bare_lf={1}" -f $crlf, $lf)

# ---- bmp dims ----
foreach ($bmpName in @('wizard-large.bmp','wizard-small.bmp')) {
    $bp = Join-Path $root ('assets\installer\' + $bmpName)
    $x = [System.Drawing.Image]::FromFile($bp)
    $out += ("BMP {0} {1}x{2} pixfmt={3} bytes={4}" -f $bmpName, $x.Width, $x.Height, $x.PixelFormat, (Get-Item $bp).Length)
    $x.Dispose()
}

$out | Out-File -Encoding utf8 (Join-Path $PSScriptRoot 'ev_static.txt')
$out
