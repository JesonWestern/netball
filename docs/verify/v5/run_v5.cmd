@echo off
rem Detached runner: launched with "start" so an external Ctrl+C on the shared
rem console cannot abort a measurement halfway (H-01 lesson from T14).
rem %~dp0 = ...\docs\verify\v5\  -> project root is three levels up.
set V5=%~dp0
cd /d "%V5%..\..\.."
set SCRIPT=%1
if "%SCRIPT%"=="" set SCRIPT=run_B
powershell -NoProfile -ExecutionPolicy Bypass -File "%V5%%SCRIPT%.ps1" > "%V5%%SCRIPT%.log" 2>&1
echo EXITCODE=%ERRORLEVEL%>> "%V5%%SCRIPT%.log"
