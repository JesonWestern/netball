# dry run for the Start-Process + redirect pattern used by run_t7b_final.ps1.
# Uses res_snapshot.ps1 as the child: read only, starts no NetBall.
$here = $PSScriptRoot
$media = Join-Path (Split-Path -Parent $here) 'media-t7b'
$child = Join-Path $here 'res_snapshot.ps1'
$outFile = Join-Path $media 'dryrun-child.log'
$errFile = $outFile + '.err'
if (Test-Path $outFile) { Remove-Item $outFile -Force }
if (Test-Path $errFile) { Remove-Item $errFile -Force }
$t0 = Get-Date
$p = Start-Process -FilePath 'powershell.exe' `
    -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $child, '-Tag', 'dryrun') `
    -NoNewWindow -PassThru -Wait -RedirectStandardOutput $outFile -RedirectStandardError $errFile
$dt = [int]((Get-Date) - $t0).TotalSeconds
$txt = ''
if (Test-Path $outFile) { $txt = [string](Get-Content $outFile -Raw -Encoding UTF8) }
$errTxt = ''
if (Test-Path $errFile) { $errTxt = [string](Get-Content $errFile -Raw -Encoding UTF8) }
$errLen = 0
if ($errTxt) { $errLen = $errTxt.Trim().Length }
Write-Output ("DRYRUN exit={0} elapsed={1}s stdoutLines={2} stderrLen={3}" -f $p.ExitCode, $dt, (@($txt -split "`r?`n").Count), $errLen)
Write-Output ("DRYRUN child RESULT line: " + ((($txt -split "`r?`n") | Where-Object { $_ -match '^RESULT' }) -join ' | '))
Write-Output ("DRYRUN child snapshot line: " + ((($txt -split "`r?`n") | Where-Object { $_ -match 'snapshot written' }) -join ' | '))
if ($errLen -gt 0) { Write-Output ("DRYRUN STDERR: " + ($errTxt -replace "`r?`n", ' / ')) }
