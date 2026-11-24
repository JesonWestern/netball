# run_0_anchor.ps1 -- T14 step 0: freeze the measured binary.
# Reuses the T7b helper (nb.ps1) but redirects all output dirs to v5.
# ASCII-only on purpose: Windows PowerShell 5.1 reads .ps1 as ANSI without a BOM.
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\..\t7b\nb.ps1"

$MediaDir = Join-Path $VerifyD 'media-v5'
$TmpDir   = Join-Path $Root 'build\_tmp\v5'
$ExeCopy  = Join-Path $TmpDir 'NetBall_v5.exe'
$ProcName = 'NetBall_v5'
foreach ($d in @($MediaDir, $TmpDir)) {
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
}

Write-Output "=========== T14 step 0 : anchor + environment ==========="
Write-Output ("ENV root   = {0}" -f $Root)
Write-Output ("ENV exe    = {0}" -f $ExeSrc)
Write-Output ("ENV ini    = {0}" -f $IniPath)
Write-Output ("ENV copy   = {0}" -f $ExeCopy)

# ---------------- artifact identity (two independent sources) ---------------
$expectSha  = 'e085727cf87e4df22b551596b2cb53d407ef177ba2539b4d9e6ac0fb33ebe983'
$expectSize = 1053696
$i = Get-Item -LiteralPath $ExeSrc
Write-Output ("ART size      = {0} (expect {1}) match={2}" -f $i.Length, $expectSize, ($i.Length -eq $expectSize))
Write-Output ("ART mtime     = {0}" -f $i.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss.fff'))
$h1 = (Get-FileHash -LiteralPath $ExeSrc -Algorithm SHA256).Hash.ToLower()
Write-Output ("ART sha256    = {0}" -f $h1)
Write-Output ("ART sha match = {0}" -f ($h1 -ceq $expectSha))

# certutil as a second, independent hash implementation
$cu = & certutil -hashfile $ExeSrc SHA256 2>&1
$cuHash = ''
foreach ($l in $cu) {
    $t = ($l -replace '\s', '')
    if ($t -match '^[0-9a-fA-F]{64}$') { $cuHash = $t.ToLower() }
}
Write-Output ("ART certutil  = {0}" -f $cuHash)
Write-Output ("ART certutil -ceq GetFileHash = {0}" -f ($cuHash -ceq $h1))

# ---------------- processes --------------------------------------------------
$procs = @(Get-Process -Name 'NetBall','NetBall_v5' -ErrorAction SilentlyContinue)
Write-Output ("PROC pre-existing NetBall/NetBall_v5 count = {0}" -f $procs.Count)
foreach ($p in $procs) { Write-Output ("PROC   pid={0} name={1} path={2}" -f $p.Id, $p.ProcessName, $p.Path) }

# ---------------- screen geometry + DPI (physical pixels) -------------------
$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
Write-Output ("SCREEN virtual x=[{0}..{1}] y=[{2}..{3}] w={4} h={5}" -f `
    $vs.Left, $vs.Right, $vs.Top, $vs.Bottom, $vs.Width, $vs.Height)
$aw = [NBT7b]::GetAwarenessFromDpiAwarenessContext([NBT7b]::GetWindowDpiAwarenessContext([IntPtr]::Zero))
Write-Output ("DPI thread-awareness-from-NULL-hwnd = {0} (4 = PER_MONITOR_AWARE_V2, 2 = PER_MONITOR)" -f $aw)
Write-Output ("DPI GetDpiForSystem = {0}" -f [NBT7b]::GetDpiForSystem())

# ---------------- config baseline snapshot ----------------------------------
if (Test-Path -LiteralPath $IniPath) {
    $cfgSha = (Get-FileHash -LiteralPath $IniPath -Algorithm SHA256).Hash.ToLower()
    Write-Output ("CFG current sha256 = {0} size={1}" -f $cfgSha, (Get-Item -LiteralPath $IniPath).Length)
    Write-Output "CFG current contents:"
    foreach ($l in (Get-Content -LiteralPath $IniPath)) { Write-Output ("CFG   " + $l) }
} else {
    Write-Output "CFG current = <absent>"
}

# ---------------- private copy ----------------------------------------------
Copy-Item -LiteralPath $ExeSrc -Destination $ExeCopy -Force
$c = Get-Item -LiteralPath $ExeCopy
$h2 = (Get-FileHash -LiteralPath $ExeCopy -Algorithm SHA256).Hash.ToLower()
Write-Output ("COPY size={0} sha256={1}" -f $c.Length, $h2)
Write-Output ("COPY sha -ceq anchor = {0}" -f ($h2 -ceq $expectSha))

$ok = (($i.Length -eq $expectSize) -and ($h1 -ceq $expectSha) -and ($cuHash -ceq $h1) -and ($h2 -ceq $expectSha))
if ($ok) { Write-Output "RESULT: PASS (private copy is the broadcast binary)" ; exit 0 }
Write-Output "RESULT: BLOCKED-ANCHOR (build\out\NetBall.exe is not the broadcast binary)"
exit 4
