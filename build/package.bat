@echo off
rem =====================================================================
rem  NetBall v1.4.4 - packaging script (Inno Setup 6 / ISCC)
rem
rem  IMPORTANT: keep this file PURE ASCII.
rem  Chinese comments inside .bat are eaten by cmd.exe under the GBK
rem  console code page (line-ending CR gets swallowed), which has already
rem  caused silent breakage in this project. Do not add non-ASCII here.
rem
rem  Success is decided by the ARTIFACT (dist\NetBallSetup-1.4.4.exe
rem  exists and has a sane size), not by ERRORLEVEL, because ERRORLEVEL
rem  is unreliable when the console is shared with other agents.
rem
rem  Usage:  build\package.bat
rem  Log:    build\_tmp\ins1\package.log
rem =====================================================================
setlocal EnableExtensions
cd /d "%~dp0.."

set "PROJ=%CD%"
set "TMPD=%PROJ%\build\_tmp\ins1"
set "LOG=%TMPD%\package.log"
set "SRCEXE=%PROJ%\build\out\NetBall.exe"
set "ISS=%PROJ%\installer\NetBall.iss"
set "OUTEXE=%PROJ%\dist\NetBallSetup-1.4.4.exe"
rem Source payload identity. Bump BOTH values whenever build\out\NetBall.exe
rem is rebuilt, otherwise this script refuses to package (that is the point:
rem silently shipping a stale payload is the one failure mode that looks fine).
set "EXPECT_SIZE=1195520"
set "EXPECT_SHA=608d5fdb878ea4603ed6e9f312e1a3c0aa55d3956d22f189ddcfc79413c846cd"
set "MIN_OUT=200000"
set "MAX_OUT=20000000"

if not exist "%TMPD%" mkdir "%TMPD%"
if errorlevel 1 goto no_tmpdir

echo ==================================================== > "%LOG%"
echo  NetBall packaging run >> "%LOG%"
echo  project : %PROJ% >> "%LOG%"
echo  date    : %DATE% %TIME% >> "%LOG%"
echo ==================================================== >> "%LOG%"

rem ---------- step 1: source exe must exist ----------
if not exist "%SRCEXE%" goto exe_missing
for %%F in ("%SRCEXE%") do set "SRCSIZE=%%~zF"
if not defined SRCSIZE goto exe_missing

echo [1/4] source exe: %SRCEXE% >> "%LOG%"
echo       bytes    : %SRCSIZE% >> "%LOG%"
if %SRCSIZE% LSS %EXPECT_SIZE% goto exe_size_bad
if %SRCSIZE% GTR %EXPECT_SIZE% goto exe_size_bad
echo       size check: OK (expected %EXPECT_SIZE%) >> "%LOG%"

rem ---------- step 1b: source exe sha256 must match the frozen payload ----------
rem size alone cannot tell two rebuilds apart; the hash is the real identity.
certutil -hashfile "%SRCEXE%" SHA256 2>nul | findstr /i /c:"%EXPECT_SHA%" >nul
if errorlevel 1 goto exe_sha_bad
echo       sha256 check: OK (%EXPECT_SHA%) >> "%LOG%"

rem ---------- step 2: find ISCC ----------
rem ---------- step 3: compile ----------
if not exist "%ISS%" goto iss_missing
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto iscc_found
set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if exist "%ISCC%" goto iscc_found
goto iscc_missing

:iscc_found
echo [2/4] compiler : %ISCC% >> "%LOG%"
if not exist "%PROJ%\dist" mkdir "%PROJ%\dist"
rem remove stale artifact so the post-compile existence check is meaningful
if exist "%OUTEXE%" del /q "%OUTEXE%"
if exist "%OUTEXE%" goto out_locked

echo [3/4] compiling %ISS% >> "%LOG%"
"%ISCC%" "%ISS%" >> "%LOG%" 2>&1
set "RC=%ERRORLEVEL%"
echo       ISCC exit code (informational only) = %RC% >> "%LOG%"

rem ---------- step 4: verify artifact ----------
if not exist "%OUTEXE%" goto build_failed
for %%F in ("%OUTEXE%") do set "OUTSIZE=%%~zF"
if not defined OUTSIZE goto build_failed
if %OUTSIZE% LSS %MIN_OUT% goto out_size_bad
if %OUTSIZE% GTR %MAX_OUT% goto out_size_bad

echo [4/4] artifact : %OUTEXE% >> "%LOG%"
echo       bytes    : %OUTSIZE% >> "%LOG%"
echo RESULT: OK >> "%LOG%"
echo.
echo RESULT: OK
echo artifact: %OUTEXE%
echo bytes   : %OUTSIZE%
echo log     : %LOG%
endlocal
exit /b 0

:exe_missing
echo RESULT: FAIL - source exe not found: %SRCEXE% >> "%LOG%"
echo RESULT: FAIL - source exe not found: %SRCEXE%
endlocal
exit /b 2

:exe_size_bad
echo RESULT: FAIL - source exe size %SRCSIZE% != %EXPECT_SIZE% (wrong or rebuilt binary) >> "%LOG%"
echo RESULT: FAIL - source exe size %SRCSIZE% != %EXPECT_SIZE%
endlocal
exit /b 3

:exe_sha_bad
echo RESULT: FAIL - source exe sha256 != %EXPECT_SHA% (stale or rebuilt binary) >> "%LOG%"
echo RESULT: FAIL - source exe sha256 mismatch, expected %EXPECT_SHA%
endlocal
exit /b 10

:iss_missing
echo RESULT: FAIL - script not found: %ISS% >> "%LOG%"
echo RESULT: FAIL - script not found: %ISS%
endlocal
exit /b 4

:iscc_missing
echo RESULT: FAIL - ISCC.exe not found in Inno Setup 6 folders >> "%LOG%"
echo RESULT: FAIL - ISCC.exe not found (install Inno Setup 6)
endlocal
exit /b 5

:out_locked
echo RESULT: FAIL - cannot delete stale %OUTEXE% (file locked) >> "%LOG%"
echo RESULT: FAIL - cannot delete stale artifact (file locked)
endlocal
exit /b 6

:build_failed
echo RESULT: FAIL - compiler produced no usable artifact >> "%LOG%"
echo RESULT: FAIL - compiler produced no usable artifact, see %LOG%
endlocal
exit /b 7

:out_size_bad
echo RESULT: FAIL - artifact size %OUTSIZE% outside [%MIN_OUT%, %MAX_OUT%] >> "%LOG%"
echo RESULT: FAIL - artifact size %OUTSIZE% looks wrong
endlocal
exit /b 8

:no_tmpdir
echo RESULT: FAIL - cannot create log dir
endlocal
exit /b 9
