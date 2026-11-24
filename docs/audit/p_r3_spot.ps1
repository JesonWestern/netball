# auditor / T10 - post-O1/O2/O3 spot check (3 lines, no functional test)
# ASCII-only source: all CJK literals are built from code points on purpose.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$o = New-Object System.Collections.ArrayList
function Add-Line($s) { [void]$o.Add($s); Write-Output $s }
function Cps([int[]]$a) { return (($a | ForEach-Object { [char]$_ }) -join '') }

$docPath = (Get-ChildItem (Join-Path $root 'docs') -Filter '*.md' -File | Where-Object { $_.Name -notlike 'AUDIT*' } | Select-Object -First 1).FullName
$doc = [System.IO.File]::ReadAllText($docPath, [System.Text.Encoding]::UTF8)
$jlines = $doc -split "`r?`n"

Add-Line ("DOC=" + (Split-Path $docPath -Leaf) + "  BYTES=" + (Get-Item $docPath).Length)

# ================= INSTRUMENT VALIDATION (positive / negative / findstr-trap) =================
# Why: a needle check that can never match would silently "pass" every negative assertion.
# Control 1 (positive): a phrase that MUST exist must be found.
# Control 2 (negative): a fabricated phrase must NOT be found.
# Control 3 (trap)    : findstr with a CJK argument against a UTF-8 file returns 0 hits even
#                       when the phrase is present (GBK bytes vs UTF-8 bytes). Demonstrated,
#                       so nobody mistakes "findstr rc=1" for evidence.
$needleFuscreen = Cps 0x526F,0x5C4F            # secondary screen - known to exist in the doc
$needleKnown    = Cps 0x5DF2,0x77E5,0x7F3A,0x9677  # known defect - known to exist
$needleFake     = 'ZZQQXX_NOT_A_REAL_PHRASE_9F3A'
$ctrlPos = ($doc.Contains($needleFuscreen) -and $doc.Contains($needleKnown))
$ctrlNeg = (-not $doc.Contains($needleFake))
& findstr /c:$needleFuscreen $docPath | Out-Null
$findstrRc = $LASTEXITCODE
Add-Line ("CTRL_POSITIVE_DOTNET={0} (dotnet found the CJK needle)" -f $ctrlPos)
Add-Line ("CTRL_NEGATIVE_DOTNET={0} (fabricated phrase absent)" -f $ctrlNeg)
Add-Line ("CTRL_FINDSTR_RC={0} (0=hit,1=no-hit ; CJK needle vs UTF-8 file -> EXPECT 1 = the trap)" -f $findstrRc)
$instrumentOk = ($ctrlPos -and $ctrlNeg)
Add-Line ("INSTRUMENT_OK={0}" -f $instrumentOk)

# ---- O-1: three-point-hash row must cite R3, not the old T9 ----
$o1 = @($jlines | Where-Object { $_.Contains('AUDIT-REPORT-R3') })
$o1bad = @($jlines | Where-Object { $_.Contains('REPORT-T9') })
$hashRow = @($jlines | Where-Object { $_.Contains('6cdfb092') -or $_.Contains('2,414,835') })
Add-Line ("O1_CITES_R3={0} lines={1} | O1_STILL_CITES_T9={2}" -f ($o1.Count -gt 0), $o1.Count, ($o1bad.Count -gt 0))
foreach ($l in $o1) { Add-Line ("  O1_LINE: " + $l.Trim()) }

# ---- O-2: conflated sentence must be gone; new split wording must exist ----
$oldPhrase = Cps 0x7403,0x88AB,0x62D6,0x5230,0x5C4F,0x5E55,0x5916   # "ball dragged off-screen"
$neg = Cps 0x526F,0x5C4F                                             # "secondary screen"
$clamp = Cps 0x94B3,0x5236                                           # "clamp"
$o2old = $doc.Contains($oldPhrase)
$o2new = ($doc.Contains($neg) -and $doc.Contains($clamp))
Add-Line ("O2_OLD_CONFLATED_PRESENT={0} | O2_HAS_SPLIT_WORDING={1}" -f $o2old, $o2new)

# ---- O-3: unins000 volume must carry a "not an anchor" caveat ----
$cav = Cps 0x4E0D,0x4F5C,0x951A,0x70B9                                 # "not an anchor"
$unRow = @($jlines | Where-Object { $_.Contains('4,386,294') })
Add-Line ("O3_UNROW_COUNT={0} | O3_HAS_CAVEAT={1}" -f $unRow.Count, ($unRow.Count -gt 0 -and $unRow[0].Contains($cav)))
foreach ($l in $unRow) { Add-Line ("  O3_LINE: " + $l.Trim().Substring(0, [Math]::Min(220, $l.Trim().Length))) }

# ---- (2) stale strings must stay 0 ----
$files = @($docPath,
           (Join-Path $root 'assets\installer\README.txt'),
           (Join-Path $root 'installer\NetBall.iss'),
           (Join-Path $root 'build\package.bat'),
           (Join-Path $root 'build\build.bat'))
$stale = @('7269AFAA','778E7A69','1,053,184','2,413,546','2,414,810','cddc99a7','CDDC99A7','4,190','5,225','10,523','4,428')
$hits = 0
foreach ($f in $files) {
    $t = [System.IO.File]::ReadAllText($f, [System.Text.Encoding]::UTF8)
    foreach ($s in $stale) { if ($t.Contains($s)) { Add-Line ("  STALE_HIT {0} :: {1}" -f (Split-Path $f -Leaf), $s); $hits++ } }
}
Add-Line ("STALE_TOTAL_HITS={0}" -f $hits)

# ---- (3) anchors unchanged ----
$exe = Join-Path $root 'build\out\NetBall.exe'
$setup = Join-Path $root 'dist\NetBallSetup-1.0.0.exe'
$eh = (Get-FileHash $exe -Algorithm SHA256).Hash
$sh = (Get-FileHash $setup -Algorithm SHA256).Hash
$okE = ($eh -eq 'E085727CF87E4DF22B551596B2CB53D407EF177BA2539B4D9E6AC0FB33EBE983') -and ((Get-Item $exe).Length -eq 1053696)
$okS = ($sh -eq '6CDFB09245A4F5E3CF13F64CE2333783DD3FAE698D9AB19C1B5354B506777882') -and ((Get-Item $setup).Length -eq 2414835)
Add-Line ("EXE_UNCHANGED={0} ({1} / {2})" -f $okE, (Get-Item $exe).Length, $eh)
Add-Line ("SETUP_UNCHANGED={0} ({1} / {2})" -f $okS, (Get-Item $setup).Length, $sh)

$verdict = $instrumentOk -and ($o1.Count -gt 0) -and (-not ($o1bad.Count -gt 0)) -and (-not $o2old) -and $o2new -and ($unRow.Count -gt 0) -and $unRow[0].Contains($cav) -and ($hits -eq 0) -and $okE -and $okS
Add-Line ("SPOT_VERDICT=" + $(if ($verdict) { 'PASS' } else { 'FAIL' }))
$o | Out-File -Encoding utf8 (Join-Path $PSScriptRoot 'ev_r3_spot.txt')
