# run_Z_hygiene.ps1 -- T14 closing checks: config restored to baseline with a
# key-by-key diff, zero residual processes, anchor unchanged, evidence inventory.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\t7b\nb.ps1"

$MediaDir = Join-Path $VerifyD 'media-v5'
$TmpDir   = Join-Path $Root 'build\_tmp\v5'
$ExeCopy  = Join-Path $TmpDir 'NetBall_v5.exe'
$ProcName = 'NetBall_v5'

Write-Output "=========== T14 closing : config baseline / processes / anchor / inventory ==========="

# ---------------------------------------------------------------- baseline
$baseline = [ordered]@{
    shape = '0'; size = '88'; opacity = '85'; textauto = '1'; shownet = '1'; showmem = '1'
    refreshms = '1000'; alwaysontop = '1'; autostart = '0'; posx = '-1'; posy = '-1'
    bgcolor = '#181C24'; textcolor = '#FFFFFF'
}
$lines = @('[NetBall]')
foreach ($k in $baseline.Keys) { $lines += ("{0}={1}" -f $k, $baseline[$k]) }
[System.IO.File]::WriteAllText($IniPath, (($lines -join "`r`n") + "`r`n"), [System.Text.Encoding]::ASCII)
$sha = (Get-FileHash -LiteralPath $IniPath -Algorithm SHA256).Hash.ToLower()
Write-Output ("  baseline written: {0} bytes sha256={1}" -f (Get-Item -LiteralPath $IniPath).Length, $sha)

$now = Read-Cfg
$bad = @()
foreach ($k in $baseline.Keys) {
    $got = ''
    if ($now.ContainsKey($k)) { $got = $now[$k] }
    $mark = 'ok'
    if ($got -ne $baseline[$k]) { $mark = 'DIFF'; $bad += $k }
    Write-Output ("  DIFF key {0,-12} expected={1,-10} actual={2,-10} {3}" -f $k, $baseline[$k], $got, $mark)
}
Add-Check 'CFG.restored' ($bad.Count -eq 0) ("{0}/{1} keys identical to the baseline" -f ($baseline.Count - $bad.Count), $baseline.Count)
$extra = @($now.Keys | Where-Object { -not $baseline.Contains($_) })
Write-Output ("  extra keys present = {0}{1}" -f $extra.Count, $(if ($extra.Count) { ': ' + ($extra -join ',') } else { '' }))

# ---------------------------------------------------------------- processes
$procs = @(Get-Process -Name 'NetBall', 'NetBall_v5' -ErrorAction SilentlyContinue)
Write-Output ("  NetBall/NetBall_v5 processes = {0}" -f $procs.Count)
foreach ($p in $procs) { Write-Output ("    pid={0} name={1}" -f $p.Id, $p.ProcessName) }
Add-Check 'PROC.zero' ($procs.Count -eq 0) ("no residual NetBall process ({0})" -f $procs.Count)

# ---------------------------------------------------------------- anchor
$i = Get-Item -LiteralPath $ExeSrc
$h = (Get-FileHash -LiteralPath $ExeSrc -Algorithm SHA256).Hash.ToLower()
Write-Output ("  anchor after the whole run: {0} B sha256={1}" -f $i.Length, $h)
Add-Check 'ANCHOR.unchanged' (($i.Length -eq 1053696) -and ($h -eq 'e085727cf87e4df22b551596b2cb53d407ef177ba2539b4d9e6ac0fb33ebe983')) "build\out\NetBall.exe is still the broadcast binary (no rebuild during the run)"

# ---------------------------------------------------------------- inventory
Write-Output ""
Write-Output "  --- docs\verify\v5 ---"
$v5 = @(Get-ChildItem -LiteralPath (Join-Path $VerifyD 'v5') -File | Sort-Object Name)
foreach ($f in $v5) { Write-Output ("    {0,-30} {1,8} B" -f $f.Name, $f.Length) }
Write-Output ("    total files = {0}" -f $v5.Count)
Write-Output "  --- docs\verify\media-v5 ---"
$mv = @(Get-ChildItem -LiteralPath $MediaDir -File | Sort-Object Name)
foreach ($f in $mv) { Write-Output ("    {0,-34} {1,8} B" -f $f.Name, $f.Length) }
Write-Output ("    total files = {0}" -f $mv.Count)
Write-Output "  --- build\_tmp\v5 ---"
$tv = @(Get-ChildItem -LiteralPath $TmpDir -File | Sort-Object Name)
foreach ($f in $tv) { Write-Output ("    {0,-34} {1,8} B" -f $f.Name, $f.Length) }
Write-Output ("    total files = {0}" -f $tv.Count)

Write-Output ""
$fails = Get-FailCount
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0) { Write-Output "RESULT: PASS"; exit 0 }
Write-Output "RESULT: FAIL"
exit 1
