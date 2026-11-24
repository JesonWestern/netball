<#
  AC-16 (part 1)  source hygiene: no TODO / FIXME / XXX / placeholder / leftover
                  Chinese filler markers in shipped sources.
  This file is ASCII-only on purpose (a BOM-less UTF-8 .ps1 containing non-ASCII
  gets parsed as GBK and dies with a syntax error). The Chinese markers are built
  from codepoints at runtime.
  Exit: 0 PASS (0 hits) / 1 FAIL / 2 SKIP (no sources yet)
#>
[CmdletBinding()]
param(
    [string]$Root
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = [Text.Encoding]::UTF8 } catch {}

if (-not $Root) { $Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot) }

# Chinese markers assembled from codepoints (keeps the file ASCII)
$mZhanWei = [string]::new([char[]]@(0x5360, 0x4F4D))                                    # "placeholder" (zhanwei)
$mCiChuShengLue = [string]::new([char[]]@(0x6B64, 0x5904, 0x7701, 0x7565))              # "omitted here"
$mDaiBuChong = [string]::new([char[]]@(0x5F85, 0x8865, 0x5145))                         # "to be filled"
$mDaiShiXian = [string]::new([char[]]@(0x5F85, 0x5B9E, 0x73B0))                         # "to be implemented"

$scanDirs = @('src', 'tools', 'installer') | ForEach-Object { Join-Path $Root $_ }
$exists = $scanDirs | Where-Object { Test-Path -LiteralPath $_ -PathType Container }

Write-Output 'AC-16 SOURCE HYGIENE SCAN (part 1: markers)'
foreach ($d in $scanDirs) {
    Write-Output ("  scan : {0}  [{1}]" -f $d, $(if (Test-Path -LiteralPath $d) { 'exists' } else { 'MISSING' }))
}

if (-not $exists) {
    Write-Output 'RESULT: SKIP (none of src/, tools/, installer/ exist yet)'
    exit 2
}

$exts = @('.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.rc', '.manifest', '.def',
          '.py', '.bat', '.cmd', '.ps1', '.iss', '.ini', '.md', '.txt', '.json', '.xml')

$files = Get-ChildItem -LiteralPath $exists -Recurse -File -ErrorAction SilentlyContinue |
         Where-Object { $exts -contains $_.Extension.ToLower() }
Write-Output ("  files: {0} text files under scan scope" -f $files.Count)

if ($files.Count -eq 0) {
    Write-Output 'RESULT: SKIP (no scannable source files yet)'
    exit 2
}

# marker set 1: case-insensitive latin words + the Chinese filler markers
$pat1 = 'TODO|FIXME|HACK|placeholder|' + $mZhanWei + '|' + $mCiChuShengLue + '|' + $mDaiBuChong + '|' + $mDaiShiXian
# marker set 2: XXX only as a standalone token (a hex dump of many X is not a marker)
$pat2 = '(?<![A-Za-z0-9_])XXX(?![A-Za-z0-9_])'

$hits = New-Object System.Collections.Generic.List[string]

foreach ($f in $files) {
    $text = Get-Content -LiteralPath $f.FullName -Raw -ErrorAction SilentlyContinue
    if ($null -eq $text) { continue }
    $lines = $text -split "`r?`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $ln = $lines[$i]
        $hit = $false
        $kind = ''
        if ($ln -match $pat1) { $hit = $true; $kind = $Matches[0] }
        elseif ($ln -cmatch $pat2) { $hit = $true; $kind = 'XXX' }
        if ($hit) {
            $rel = $f.FullName.Substring($Root.Length).TrimStart('\')
            $hits.Add(("{0}:{1}: [{2}] {3}" -f $rel, ($i + 1), $kind, $ln.Trim()))
        }
    }
}

Write-Output ''
if ($hits.Count -eq 0) {
    Write-Output ("  no markers found in {0} files" -f $files.Count)
    Write-Output "METRIC: files_scanned=$($files.Count) hits=0"
    Write-Output 'RESULT: PASS (0 marker hits in shipped sources)'
    exit 0
}

Write-Output ("  HITS: {0}" -f $hits.Count)
foreach ($h in $hits) { Write-Output ('    ' + $h) }
Write-Output "METRIC: files_scanned=$($files.Count) hits=$($hits.Count)"
Write-Output "RESULT: FAIL ($($hits.Count) marker hits must be resolved or rephrased)"
exit 1
