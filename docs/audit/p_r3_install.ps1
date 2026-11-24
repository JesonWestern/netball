# auditor / T10 round3 - install/uninstall the NEW dist, verify payload + README + cleanup
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$lines = New-Object System.Collections.ArrayList
function Add-Line($s) { [void]$lines.Add($s); Write-Output $s }

$setup = Join-Path $root 'dist\NetBallSetup-1.0.0.exe'
$appDir = Join-Path $env:LOCALAPPDATA 'Programs\NetBall'
$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\NetBall'
$desktopLnk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'NetBall.lnk'
$cfgDir = Join-Path $env:APPDATA 'NetBall'
$uninsKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{8F3A1C2E-5B47-4D9A-9E62-0C7D4B15A8E3}_is1'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'

Add-Line ("SETUP_SHA=" + (Get-FileHash $setup -Algorithm SHA256).Hash)
Add-Line ("SETUP_BYTES=" + (Get-Item $setup).Length)
Add-Line ("PRE_PROC=" + @(Get-Process -Name 'NetBall','unins*','*NetBallSetup*' -ErrorAction SilentlyContinue).Count)
Add-Line ("PRE_APPDIR=" + (Test-Path $appDir) + " PRE_CFG=" + (Test-Path (Join-Path $cfgDir 'config.ini')))
$cfgBefore = if (Test-Path (Join-Path $cfgDir 'config.ini')) { (Get-FileHash (Join-Path $cfgDir 'config.ini') -Algorithm SHA256).Hash } else { 'ABSENT' }
Add-Line ("PRE_CFG_SHA=" + $cfgBefore)

# ---- install (current user, no desktop icon) ----
$p = Start-Process -FilePath $setup -ArgumentList '/CURRENTUSER','/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/TASKS=""' -PassThru -Wait
Add-Line ("INSTALL_EXITCODE=" + $p.ExitCode)
Start-Sleep -Milliseconds 800

Add-Line ("POST_INSTALL_APPDIR=" + (Test-Path $appDir))
if (Test-Path $appDir) {
    Get-ChildItem $appDir -Force | ForEach-Object { Add-Line ("  FILE {0} {1} bytes" -f $_.Name, $_.Length) }
    $exe = Join-Path $appDir 'NetBall.exe'
    if (Test-Path $exe) {
        $h = (Get-FileHash $exe -Algorithm SHA256).Hash
        Add-Line ("INSTALLED_EXE_SHA=" + $h)
        Add-Line ("PAYLOAD_MATCH=" + ($h -eq 'E085727CF87E4DF22B551596B2CB53D407EF177BA2539B4D9E6AC0FB33EBE983'))
        $vi = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($exe)
        Add-Line ("INSTALLED_DESC_CP=" + (($vi.FileDescription.ToCharArray() | ForEach-Object { 'U+{0:X4}' -f [int]$_ }) -join ' '))
        Add-Line ("INSTALLED_VER=" + $vi.FileVersion + " PROD=" + $vi.ProductName)
    }
    $r = Join-Path $appDir 'README.txt'
    Add-Line ("INSTALLED_README=" + (Test-Path $r) + " bytes=" + $(if (Test-Path $r) { (Get-Item $r).Length } else { 0 }))
    Add-Line ("INSTALLED_UNINS=" + (Test-Path (Join-Path $appDir 'unins000.exe')))
    $lnk = Get-ChildItem $appDir -Filter '*.lnk' -ErrorAction SilentlyContinue
    Add-Line ("APP_LNK_COUNT=" + @($lnk).Count)
    foreach ($l in $lnk) { Add-Line ("  APP_LNK=" + $l.Name) }
}
Add-Line ("STARTMENU=" + (Test-Path $startMenu) + " items=" + $(if (Test-Path $startMenu) { @(Get-ChildItem $startMenu -Filter '*.lnk').Count } else { 0 }))
Add-Line ("DESKTOP_LNK=" + (Test-Path $desktopLnk))
Add-Line ("UNINSTALL_KEY=" + (Test-Path $uninsKey))
if (Test-Path $uninsKey) {
    $k = Get-ItemProperty $uninsKey
    Add-Line ("  DISPLAYNAME=" + $k.DisplayName + " | DISPLAYVER=" + $k.DisplayVersion + " | DISPLAYICON=" + $k.DisplayIcon)
}
Add-Line ("RUN_VALUE=" + $(if ((Get-ItemProperty $runKey -ErrorAction SilentlyContinue).NetBall) { 'PRESENT' } else { 'ABSENT' }))

# ---- uninstall silently ----
$unins = Join-Path $appDir 'unins000.exe'
if (Test-Path $unins) {
    $u = Start-Process -FilePath $unins -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -PassThru -Wait
    Add-Line ("UNINSTALL_EXITCODE=" + $u.ExitCode)
} else { Add-Line "UNINSTALL_SKIP no unins000.exe" }
Start-Sleep -Milliseconds 1500

Add-Line ("FINAL_APPDIR_GONE=" + (-not (Test-Path $appDir)))
Add-Line ("FINAL_STARTMENU_GONE=" + (-not (Test-Path $startMenu)))
Add-Line ("FINAL_DESKTOP_GONE=" + (-not (Test-Path $desktopLnk)))
Add-Line ("FINAL_UNINSTALLKEY_GONE=" + (-not (Test-Path $uninsKey)))
Add-Line ("FINAL_RUNVALUE=" + $(if ((Get-ItemProperty $runKey -ErrorAction SilentlyContinue).NetBall) { 'STILL_PRESENT' } else { 'ABSENT' }))
$cfgAfter = if (Test-Path (Join-Path $cfgDir 'config.ini')) { (Get-FileHash (Join-Path $cfgDir 'config.ini') -Algorithm SHA256).Hash } else { 'ABSENT' }
Add-Line ("FINAL_CFG=" + $cfgAfter)
Add-Line ("CFG_UNCHANGED=" + ($cfgBefore -eq $cfgAfter))
$tmp = @(Get-Process -Name '_unins*','unins*' -ErrorAction SilentlyContinue)
Add-Line ("UNINS_TMP_PROC=" + $tmp.Count)
$resid = @(Get-ChildItem $env:TEMP -Filter '_unins*' -ErrorAction SilentlyContinue)
Add-Line ("UNINS_TMP_FILES=" + $resid.Count)
Add-Line ("FINAL_PROC=" + @(Get-Process -Name 'NetBall*' -ErrorAction SilentlyContinue).Count)

$lines | Out-File -Encoding utf8 (Join-Path $PSScriptRoot 'ev_r3_install.txt')
