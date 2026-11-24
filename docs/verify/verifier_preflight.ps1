<#
  Shared preflight. Dot-source it, then call:
      Write-BuildInfo -Exe $ExePath
  Prints one BUILD: line per fact so every verdict can be traced back to a build
  (a stale exe once made a fixed defect look live - and vice versa).
  ASCII only on purpose.
#>
function Write-BuildInfo {
    param([string]$Exe)

    if (-not $Exe) {
        $root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
        $Exe = Join-Path $root 'build\out\NetBall.exe'
    }
    $root2 = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

    if (-not (Test-Path -LiteralPath $Exe)) {
        Write-Output "BUILD: exe=$Exe MISSING"
        return
    }
    $fi = Get-Item -LiteralPath $Exe
    Write-Output ("BUILD: exe={0} bytes={1} mtime={2}" -f $fi.Name, $fi.Length, $fi.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))

    $watch = @('src\metrics.cpp', 'src\config.cpp', 'src\ball_window.cpp', 'src\render.cpp', 'src\main.cpp')
    $newest = $null
    $newestName = 'n/a'
    foreach ($w in $watch) {
        $p = Join-Path $root2 $w
        if (Test-Path -LiteralPath $p) {
            $m = (Get-Item -LiteralPath $p).LastWriteTime
            if (-not $newest -or $m -gt $newest) { $newest = $m; $newestName = $w }
        }
    }
    if ($newest) {
        Write-Output ("BUILD: newest source={0} mtime={1}" -f ($newestName -replace '\\', '/'), $newest.ToString('yyyy-MM-dd HH:mm:ss'))
        if ($fi.LastWriteTime -lt $newest) {
            Write-Output 'BUILD: fresh=NO -> exe predates the source, rebuild before drawing conclusions'
        } else {
            Write-Output 'BUILD: fresh=yes'
        }
    } else {
        Write-Output 'BUILD: newest source=n/a (no watched sources found)'
    }
}
