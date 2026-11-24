# auditor / T10 round3 - document chain + wording gate (ASCII-only source)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$out = New-Object System.Collections.ArrayList
function Add-Line($s) { [void]$out.Add($s); Write-Output $s }
function Cps([int[]]$a) { return (($a | ForEach-Object { [char]$_ }) -join '') }

$doc = (Get-ChildItem (Join-Path $root 'docs') -Filter '*.md' -File | Where-Object { $_.Name -notlike 'AUDIT*' } | Select-Object -First 1).FullName
$readme = Join-Path $root 'assets\installer\README.txt'
$iss  = Join-Path $root 'installer\NetBall.iss'
$pbat = Join-Path $root 'build\package.bat'
$bbat = Join-Path $root 'build\build.bat'

$texts = [ordered]@{
  'JIAOFU' = [System.IO.File]::ReadAllText($doc, [System.Text.Encoding]::UTF8)
  'README' = [System.IO.File]::ReadAllText($readme, [System.Text.Encoding]::UTF8)
  'ISS'    = [System.IO.File]::ReadAllText($iss, [System.Text.Encoding]::UTF8)
  'PKG'    = [System.IO.File]::ReadAllText($pbat, [System.Text.Encoding]::UTF8)
  'BUILD'  = [System.IO.File]::ReadAllText($bbat, [System.Text.Encoding]::UTF8)
}

Add-Line "=== A. STALE-VALUE SCAN (must be 0) ==="
$stale = @('7269AFAA','7269afaa','778E7A69','778e7a69','1,053,184','1053184','2,413,546','2413546',
           '2,414,810','2414810','cddc99a7','CDDC99A7','4,190','5,225','10,523','4,428','1.004 MB','2,413,5')
$hits = 0
foreach ($k in $texts.Keys) {
    $lines = $texts[$k] -split "`r?`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        foreach ($s in $stale) {
            if ($lines[$i].Contains($s)) { Add-Line ("HIT {0} L{1}: [{2}] {3}" -f $k, ($i+1), $s, $lines[$i].Trim()); $hits++ }
        }
    }
}
Add-Line ("STALE_TOTAL_HITS={0}" -f $hits)

Add-Line ""
Add-Line "=== B. BANNED WORDING (must be 0 in JIAOFU + README) ==="
$banned = @( (Cps 0x7269,0x7406,0x9650,0x5236),                      # wu-li xian-zhi
             (Cps 0x8BBE,0x8BA1,0x5982,0x6B64),                      # she-ji ru-ci
             (Cps 0x9884,0x671F,0x884C,0x4E3A),                      # yu-qi xing-wei
             (Cps 0x786C,0x4EF6,0x9650,0x5236),                      # ying-jian xian-zhi
             (Cps 0x53D7,0x7CFB,0x7EDF,0x9650,0x5236,0x65E0,0x6CD5,0x907F,0x514D),  # shou xi-tong ... wu-fa bi-mian
             (Cps 0x5DF2,0x4FEE,0x590D),                             # yi-xiu-fu (already fixed)
             (Cps 0x65E0,0x6CD5,0x4FEE,0x590D) )                     # wu-fa xiu-fu
$bhits = 0
foreach ($k in @('JIAOFU','README')) {
    $lines = $texts[$k] -split "`r?`n"
    for ($i = 0; $i -lt $lines.Count; $i++) {
        foreach ($s in $banned) {
            if ($lines[$i].Contains($s)) { Add-Line ("BANNED {0} L{1}: [{2}] {3}" -f $k, ($i+1), $s, $lines[$i].Trim()); $bhits++ }
        }
    }
}
Add-Line ("BANNED_TOTAL_HITS={0}" -f $bhits)

Add-Line ""
Add-Line "=== C. SECTION 4.10 PRESENCE / ELEMENTS ==="
$jlines = $texts['JIAOFU'] -split "`r?`n"
$idx = -1
for ($i = 0; $i -lt $jlines.Count; $i++) { if ($jlines[$i] -match '^###\s*4\.10') { $idx = $i; break } }
if ($idx -lt 0) { Add-Line "SEC_4_10=MISSING" } else {
    Add-Line ("SEC_4_10_LINE={0}" -f ($idx+1))
    $block = @()
    for ($i = $idx; $i -lt [Math]::Min($idx + 26, $jlines.Count); $i++) {
        if ($i -gt $idx -and $jlines[$i] -match '^###\s*4\.1[1-9]') { break }
        $block += $jlines[$i]
    }
    $blk = ($block -join "`n")
    Add-Line ("HAS_20PX={0}" -f ($blk -match '20\s*px'))
    Add-Line ("HAS_87PCT={0}" -f ($blk -match '87'))
    Add-Line ("HAS_V11={0}" -f ($blk -match 'v1\.1'))
    Add-Line ("HAS_CLAMP={0}" -f $blk.Contains('ClampWindowFullyInside'))
    Add-Line ("HAS_KNOWN_DEFECT={0}" -f $blk.Contains((Cps 0x5DF2,0x77E5,0x7F3A,0x9677)))
    Add-Line "--- BLOCK BEGIN ---"
    foreach ($l in $block) { Add-Line $l }
    Add-Line "--- BLOCK END ---"
}

Add-Line ""
Add-Line "=== D. ANCHOR VALUES IN DOCS ==="
foreach ($needle in @('1,053,696','E085727C','2,414,835','6CDFB092','5,258')) {
    $c = 0
    foreach ($k in $texts.Keys) { if ($texts[$k].Contains($needle)) { $c++ } }
    Add-Line ("DOC_CONTAINS {0} in {1} files" -f $needle, $c)
}
Add-Line ("README_1_2 = {0}" -f ($texts['README'].Contains('1.2:1')))
Add-Line ("README_1_7_LEFT = {0}" -f ($texts['README'].Contains('1.7:1')))
Add-Line ("JIAOFU_1_7_LEFT = {0}" -f ($texts['JIAOFU'].Contains('1.7:1')))
Add-Line ("PKG_EXPECT_1053696 = {0}" -f $texts['PKG'].Contains('1053696'))

$out | Out-File -Encoding utf8 (Join-Path $PSScriptRoot 'ev_r3_docs.txt')
