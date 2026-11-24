# anchor_check.ps1 -- read-only anchor preflight (starts nothing, touches nothing).
# Confirms that Assert-Anchor accepts the corrected sha256 and that the private
# copy / config state are what the measurement window will start from.
. "$PSScriptRoot\nb.ps1"
Write-Output "=========== T7b ANCHOR PREFLIGHT (read only, no NetBall started) ==========="
Write-Output ("  anchor.txt: " + ((Get-Content -LiteralPath (Join-Path $TmpDir 'anchor.txt') -Raw).Trim()))
$anchor = Assert-Anchor
Write-Output ("  ANCHOR-OK sha={0} bytes={1} mtime={2}" -f $anchor.Sha, $anchor.Bytes, $anchor.Mtime)
Write-Output ("  config.ini bgcolor now = {0}  (default expects #181C24)" -f (Get-IniValue 'bgcolor'))
Write-Output ("  config.ini posx now    = {0}" -f (Get-IniValue 'posx'))
$proc = @(Get-ProcList)
Write-Output ("  netball processes now  = {0}" -f $proc.Count)
$copy = Join-Path $TmpDir 'NetBall_t7b.exe'
if (Test-Path -LiteralPath $copy) {
    $ci = Get-ExeInfo $copy
    Write-Output ("  private copy (stale until the run): {0} {1} B mtime={2}" -f $ci.Sha, $ci.Bytes, $ci.Mtime)
}
Write-Output "RESULT: PASS (anchor accepted; nothing was started)"
