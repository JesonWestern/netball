# auditor / T10 round3 - second install round: desktop icon state, shortcut targets, pre-written Run value
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$o = New-Object System.Collections.ArrayList
function Add-Line($s) { [void]$o.Add($s); Write-Output $s }

$setup = Join-Path $root 'dist\NetBallSetup-1.0.0.exe'
$appDir = Join-Path $env:LOCALAPPDATA 'Programs\NetBall'
$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\NetBall'
$desktopLnk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'NetBall.lnk'
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'

Add-Line ("PRE_PROC=" + @(Get-Process -Name 'NetBall*','unins*' -ErrorAction SilentlyContinue).Count)
Add-Line ("PRE_APPDIR=" + (Test-Path $appDir))

# ---- pre-write a Run value (to prove uninsdeletevalue really deletes it) ----
New-ItemProperty -Path $runKey -Name 'NetBall' -Value '"C:\Windows\System32\notepad.exe"' -PropertyType String -Force | Out-Null
Add-Line ("PRE_RUNVALUE=" + $(if ((Get-ItemProperty $runKey).NetBall) { (Get-ItemProperty $runKey).NetBall } else { 'ABSENT' }))

# ---- install WITH desktop icon ----
$p = Start-Process -FilePath $setup -ArgumentList '/CURRENTUSER','/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/TASKS="desktopicon"' -PassThru -Wait
Add-Line ("INSTALL_EXITCODE=" + $p.ExitCode)
Start-Sleep -Milliseconds 800

Add-Line ("APPDIR=" + (Test-Path $appDir))
Add-Line ("DESKTOP_LNK_EXISTS=" + (Test-Path $desktopLnk))
$smLinks = @(if (Test-Path $startMenu) { Get-ChildItem $startMenu -Filter '*.lnk' })
Add-Line ("STARTMENU_ITEMS=" + $smLinks.Count)
$appLinks = @(if (Test-Path $appDir) { Get-ChildItem $appDir -Filter '*.lnk' })
Add-Line ("APP_LNK_ITEMS=" + $appLinks.Count)

# ---- dereference every shortcut target ----
$ws = New-Object -ComObject WScript.Shell
$all = @()
foreach ($l in $appLinks) { $all += $l.FullName }
foreach ($l in $smLinks) { $all += $l.FullName }
if (Test-Path $desktopLnk) { $all += $desktopLnk }
foreach ($f in $all) {
    $s = $ws.CreateShortcut($f)
    Add-Line ("  LNK {0} -> TARGET={1} | WORKDIR={2} | ICON={3}" -f (Split-Path $f -Leaf), $s.TargetPath, $s.WorkingDirectory, $s.IconLocation)
}
Add-Line ("LNK_TOTAL=" + $all.Count)

# ---- uninstall, timed ----
$unins = Join-Path $appDir 'unins000.exe'
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$u = Start-Process -FilePath $unins -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART' -PassThru -Wait
Add-Line ("UNINSTALL_EXITCODE=" + $u.ExitCode + " MS=" + $sw.ElapsedMilliseconds)
Start-Sleep -Milliseconds 1200

Add-Line ("FINAL_APPDIR_GONE=" + (-not (Test-Path $appDir)))
Add-Line ("FINAL_STARTMENU_GONE=" + (-not (Test-Path $startMenu)))
Add-Line ("FINAL_DESKTOP_GONE=" + (-not (Test-Path $desktopLnk)))
Add-Line ("FINAL_RUNVALUE=" + $(if ((Get-ItemProperty $runKey -ErrorAction SilentlyContinue).NetBall) { 'STILL_PRESENT' } else { 'DELETED' }))
$uninsKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{8F3A1C2E-5B47-4D9A-9E62-0C7D4B15A8E3}_is1'
Add-Line ("FINAL_UNINSKEY_GONE=" + (-not (Test-Path $uninsKey)))
Add-Line ("UNINS_TMP_PROC=" + @(Get-Process -Name '_unins*','unins*' -ErrorAction SilentlyContinue).Count)
Add-Line ("UNINS_TMP_FILES=" + @(Get-ChildItem $env:TEMP -Filter '_unins*' -ErrorAction SilentlyContinue).Count)
$cfg = Join-Path $env:APPDATA 'NetBall\config.ini'
Add-Line ("CFG=" + $(if (Test-Path $cfg) { (Get-FileHash $cfg -Algorithm SHA256).Hash } else { 'ABSENT' }))
Add-Line ("FINAL_PROC=" + @(Get-Process -Name 'NetBall*' -ErrorAction SilentlyContinue).Count)

# ---- safety cleanup: never leave a Run value behind ----
if ((Get-ItemProperty $runKey -ErrorAction SilentlyContinue).NetBall) {
    Remove-ItemProperty -Path $runKey -Name 'NetBall' -Force
    Add-Line "SAFETY_CLEANED_RUNVALUE=YES"
}
$o | Out-File -Encoding utf8 (Join-Path $PSScriptRoot 'ev_r3_install2.txt')
