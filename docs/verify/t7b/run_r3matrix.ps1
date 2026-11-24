# run_r3matrix.ps1 -- R3 follow-up: 3 sizes x 3 backgrounds x 3 captures.
#
# Purpose after the DPI verdict: prove that the DPI-aware capture path produces
# glyph ink in every one of the 9 combinations, 3 times in a row.  A cell with
# >=2 ink-less captures is the only thing that would still point at the product.
# "Flat / no ink" images are kept and labelled as evidence, not discarded.
#
# Also records, per cell, the measured fill + rendered text colour so a
# "white text on white ball" case can be told apart from a real render bug.
. "$PSScriptRoot\nb.ps1"
Write-Output "=========== T7b R3 MATRIX : 3 sizes x 3 backgrounds x 3 captures ==========="

$anchor = Initialize-Measurement -WaitMs 10000

$sizes = @(48, 88, 160)
$bgs = @(
    @{ tag = 'dark'; hex = '#181C24' },
    @{ tag = 'white'; hex = '#FFFFFF' },
    @{ tag = 'gray'; hex = '#808080' }
)
$manifest = Join-Path $TmpDir 'r3-manifest.txt'
$rows = @()
$noInk = @{}
$cells = @()

foreach ($sz in $sizes) {
    foreach ($bg in $bgs) {
        Write-Cfg -shape 0 -size $sz -opacity 85 -bg $bg.hex -posx 700 -posy 320
        $p = Start-Ball -SettleMs 1600
        if ($p.Hwnd -eq [IntPtr]::Zero) {
            Write-Output ("  CELL size={0} bg={1}: BALL WINDOW NEVER APPEARED" -f $sz, $bg.tag)
            Add-Check ("cell.{0}.{1}.window" -f $sz, $bg.tag) $false "ball window never appeared"
            Stop-BallPid $p.Pid
            continue
        }
        $rc = Get-BallRect $p.Hwnd
        Write-Output ("  CELL size={0,3} bg={1,-5} rect {2}x{3} at {4},{5}" -f $sz, $bg.tag, $rc.W, $rc.H, $rc.L, $rc.T)
        for ($i = 1; $i -le 3; $i++) {
            Start-Sleep -Milliseconds 1200        # let UpdateLayeredWindow settle
            $png = Join-Path $MediaDir ("r3-s{0}-{1}-r{2}.png" -f $sz, $bg.tag, $i)
            $reg = Get-ClampedRegion -x ($rc.L - 12) -y ($rc.T - 12) -w ($rc.W + 24) -h ($rc.H + 24)
            $len = Capture-Region -x $reg.X -y $reg.Y -w $reg.W -h $reg.H -path $png
            $rows += ("s{0}-{1}-r{2}|{3}|{4}|{5}|{6}|{7}|{8}|{9}|" -f $sz, $bg.tag, $i, $png, $reg.X, $reg.Y, $rc.L, $rc.T, $rc.R, $rc.B)
            Write-Output ("    capture {0}: {1} bytes -> {2}" -f $i, $len, (Split-Path -Leaf $png))
        }
        # cross-check with verifier-1's helper (inherits this thread's PMv2 context)
        $cs = Join-Path $VerifyD 'check_screenshot.ps1'
        $probe = Join-Path $MediaDir ("r3-s{0}-{1}-cs.png" -f $sz, $bg.tag)
        $out = & $cs -OutFile $probe -ProcessName $ProcName -ExpectText 2>&1
        $verdict = (@($out | Where-Object { $_ -match '^(RESULT|  RESULT)' }) -join ' ')
        $inkLine = (@($out | Where-Object { $_ -match 'ink\s*:' }) -join ' ')
        Write-Output ("    check_screenshot.ps1: {0} | {1}" -f ($verdict -replace '\s+', ' ').Trim(), ($inkLine -replace '\s+', ' ').Trim())
        $cells += [pscustomobject]@{ Size = $sz; Bg = $bg.tag; Verdict = $verdict; Ink = $inkLine }
        Stop-BallPid $p.Pid
    }
}

Write-Manifest -path $manifest -rows $rows -header 'key|png|x0|y0|L|T|R|B|extra'
Write-Output ("  captures: {0} (expect 27)" -f $rows.Count)

$py = Join-Path $PSScriptRoot 't7b_img.py'
Write-Output ""
Write-Output "---------- ink per capture (DPI-aware path) ----------"
& python $py glyph --manifest $manifest --max-spill 40
$rcGlyph = $LASTEXITCODE
Write-Output ("PYEXIT-glyph: {0}" -f $rcGlyph)

Write-Output ""
Write-Output "---------- measured fill / text colour per capture (diagnosis aid) ----------"
& python $py contrast --manifest $manifest
$rcContrast = $LASTEXITCODE
Write-Output ("PYEXIT-contrast: {0} (informational for R3, AC-8 verdict is the strict maximin one)" -f $rcContrast)

Write-Output ""
Write-Output "---------- evidence sanity ----------"
& python $py flat --dir $MediaDir
$rcFlat = $LASTEXITCODE
Write-Output ("PYEXIT-flat: {0}" -f $rcFlat)

Add-Check 'r3.captures-captured' ($rows.Count -eq 27) ("{0}/27 captures" -f $rows.Count)
Add-Check 'r3.check-screenshot-expecttext' (@($cells | Where-Object { $_.Verdict -match 'PASS' }).Count -eq 9) `
    ("{0}/9 cells reported PASS by check_screenshot.ps1 -ExpectText" -f @($cells | Where-Object { $_.Verdict -match 'PASS' }).Count)

Write-Cfg -shape 0 -size 88 -opacity 85 -bg '#181C24' -posx -1 -posy -1
$null = Confirm-AnchorUnchanged $anchor
$fails = Get-FailCount
Write-Output ""
Write-Output ("SUMMARY: checks={0} failed={1}" -f $script:Checks.Count, $fails)
if ($fails -eq 0 -and $rcGlyph -eq 0) {
    Write-Output "RESULT: PASS (9/9 cells produced glyph ink on all captures)"
    exit 0
}
Write-Output "RESULT: FAIL"
exit 1
