<#
  Toolchain self-check (not an AC script): every .ps1 in this folder must parse
  and must be pure ASCII (a BOM-less UTF-8 file with non-ASCII breaks under GBK).
#>
$d = $PSScriptRoot
$out = @()
Get-ChildItem -LiteralPath $d -Filter *.ps1 | ForEach-Object {
    $err = $null
    [void][System.Management.Automation.Language.Parser]::ParseFile($_.FullName, [ref]$null, [ref]$err)
    $raw = Get-Content -LiteralPath $_.FullName -Raw -Encoding UTF8
    $nonAscii = ([regex]::Matches($raw, '[^\x00-\x7F]')).Count
    if ($err -and $err.Count -gt 0) {
        $out += ("{0}  PARSE_ERROR line {1}: {2}" -f $_.Name, $err[0].Extent.StartLineNumber, $err[0].Message)
    } else {
        $out += ("{0}  OK  non_ascii_chars={1}" -f $_.Name, $nonAscii)
    }
}
$out | ForEach-Object { Write-Output $_ }
$bad = ($out | Where-Object { $_ -match 'PARSE_ERROR' }).Count
Write-Output ""
Write-Output ("SUMMARY: scripts={0} parse_errors={1}" -f $out.Count, $bad)
if ($bad -gt 0) { exit 1 } else { exit 0 }
