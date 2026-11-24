# auditor / T10 - final hygiene snapshot (read-only)
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$o = @()
$o += 'PROCS=' + @(Get-Process -Name 'NetBall*','unins*','*NetBallSetup*' -ErrorAction SilentlyContinue).Count
$o += 'APPDIR=' + (Test-Path (Join-Path $env:LOCALAPPDATA 'Programs\NetBall'))
$o += 'STARTMENU=' + (Test-Path (Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\NetBall'))
$o += 'DESKTOP=' + (Test-Path (Join-Path ([Environment]::GetFolderPath('Desktop')) 'NetBall.lnk'))
$rv = (Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -ErrorAction SilentlyContinue).NetBall
$o += 'RUNVAL=' + $(if ($rv) { 'PRESENT' } else { 'ABSENT' })
$cfg = Join-Path $env:APPDATA 'NetBall\config.ini'
$o += 'CFG=' + $(if (Test-Path $cfg) { (Get-FileHash $cfg -Algorithm SHA256).Hash } else { 'ABSENT' })
$exe = Join-Path $root 'build\out\NetBall.exe'
$o += 'EXE=' + (Get-Item $exe).Length + ' ' + (Get-FileHash $exe -Algorithm SHA256).Hash
$o += 'BUILDOUT_N=' + @(Get-ChildItem (Join-Path $root 'build\out') -File).Count
$o | Out-File -Encoding ascii (Join-Path $PSScriptRoot 'ev_r3_final2.txt')
Get-Content (Join-Path $PSScriptRoot 'ev_r3_final2.txt')
