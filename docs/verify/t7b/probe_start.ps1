# probe_start.ps1 -- why does the copy not show a ball window with an AC-8 config?
. "$PSScriptRoot\nb.ps1"
Write-Output "PROBE start diagnostics"
Write-Cfg -shape 0 -size 160 -opacity 85 -bg '#262A34' -posx 700 -posy 320 -textAuto 1
Write-Output ("ini written: " + ((Read-Cfg | Out-String).Trim()))
$p = Start-Process -FilePath $ExeCopy -WorkingDirectory (Split-Path -Parent $ExeCopy) -PassThru
Write-Output ("started pid={0}" -f $p.Id)
Start-Sleep -Milliseconds 2500
$alive = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
Write-Output ("alive after 2.5s = {0}" -f ($null -ne $alive))
if ($alive) {
    $p.Refresh()
    Write-Output ("main window handle=0x{0:X} title='{1}'" -f [int64]$p.MainWindowHandle, $p.MainWindowTitle)
}
$h = [NBT7b]::FindByClass($ClsBall, [uint32]$p.Id)
Write-Output ("FindByClass(pid) = 0x{0:X}" -f [int64]$h)
$h2 = [NBT7b]::FindByClass($ClsBall, 0)
Write-Output ("FindByClass(any) = 0x{0:X}" -f [int64]$h2)
if ($h2 -ne [IntPtr]::Zero) { Write-Output ("  info: " + [NBT7b]::WindowInfo($h2)) }
$all = @(Get-Process | Where-Object { $_.ProcessName -like 'NetBall*' })
foreach ($q in $all) { Write-Output ("proc {0} pid={1}" -f $q.ProcessName, $q.Id) }
Stop-BallPid $p.Id
