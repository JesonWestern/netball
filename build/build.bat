@echo off
rem ============================================================================
rem  build/build.bat ---- NetBall one-key build (MinGW-w64 g++ 13.1.0)
rem  Output: build\out\NetBall.exe  (+ build\out\spike.exe for T1 spike)
rem  Any source file not yet delivered by other members is skipped, so this
rem  script always works for the current stage.
rem ============================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0.."
set "ROOT=%CD%"
set "PATH=C:\application\qt\Tools\mingw1310_64\bin;%PATH%"
if not exist "build\out" mkdir "build\out"
if not exist "build\_tmp" mkdir "build\_tmp"

rem Warn only, never kill by image name. If link fails with "Permission denied",
rem an instance is running: end it by PID yourself (team rule: no kill by name).
tasklist /fi "IMAGENAME eq NetBall.exe" 2>nul | find /i "NetBall.exe" >nul && (
  echo [WARN] NetBall.exe is running - linking may fail with Permission denied
)

echo === NetBall build ===
echo root: %ROOT%

where g++ >nul 2>nul
if errorlevel 1 (
  echo [FAIL] g++ not found. Expected at C:\application\qt\Tools\mingw1310_64\bin
  exit /b 1
)
where windres >nul 2>nul
if errorlevel 1 set "NO_WINDRES=1"

rem ---------------------------------------------------------------- 1. resources
set "RES="
if exist "src\app.rc" (
  if defined NO_WINDRES (
    echo [1/4] FAIL: src\app.rc exists but windres not found
    exit /b 1
  )
  echo [1/4] windres src\app.rc
  windres --input src\app.rc --output build\_tmp\app.res --output-format=coff
  if errorlevel 1 (
    echo [FAIL] windres
    exit /b 1
  )
  set "RES=build\_tmp\app.res"
) else (
  echo [1/4] skip windres - src\app.rc not ready
)

rem ---------------------------------------------------------------- 2. T1 spike
rem M1 done: release build no longer produces spike (keeps build\out clean, build fast).
rem To rebuild spike: set BUILD_SPIKE=1 then run this script; output goes to build\_tmp\.
if /i "%BUILD_SPIKE%"=="1" (
  if exist "src\spike\spike_main.cpp" (
    echo [2/4] compile T1 spike optional
    g++ -std=c++17 -O2 -s -municode -mwindows -DUNICODE -D_UNICODE ^
        src\spike\spike_main.cpp -o build\_tmp\spike.exe ^
        -lgdiplus -lgdi32 -luser32 -lshell32
    if errorlevel 1 (
      echo [FAIL] spike compile
      exit /b 1
    )
  ) else (
    echo [2/4] skip spike - src\spike\spike_main.cpp missing
  )
) else (
  echo [2/4] skip spike - release build ^(set BUILD_SPIKE=1 to rebuild spike^)
)

rem ---------------------------------------------------------------- 3. NetBall.exe
set "SRCS="
for %%F in (main.cpp ball_window.cpp taskbar_text.cpp render.cpp text_render.cpp layout_model.cpp sensors.cpp sensors_hwinfo.cpp glass.cpp surface_style.cpp metrics.cpp config.cpp settings_dialog.cpp tray.cpp) do (
  if exist "src\%%F" (
    set "SRCS=!SRCS! src\%%F"
  ) else (
    echo       skip missing src\%%F
  )
)
if exist "src\main.cpp" (
  echo [3/4] link NetBall.exe
  g++ -std=c++17 -O2 -s -municode -mwindows -DUNICODE -D_UNICODE -Wall -Wextra ^
      !SRCS! !RES! -o build\out\NetBall.exe ^
      -lgdiplus -lgdi32 -luser32 -liphlpapi -lshell32 -lshlwapi -lcomctl32 -luxtheme -lole32 -loleaut32 -lcomdlg32 -ladvapi32 ^
      -ldwmapi -ldxgi -lpdh -lwbemuuid ^
      -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp
  if errorlevel 1 (
    echo [FAIL] NetBall link
    exit /b 1
  )
) else (
  echo [3/4] skip NetBall.exe - src\main.cpp not ready
)

rem ---------------------------------------------------------------- 4. summary
echo [4/4] artifacts:
for %%F in ("build\out\NetBall.exe") do (
  if exist %%F (
    for %%S in (%%F) do echo       %%~fS  %%~zS bytes
  )
)
echo === build done ===
endlocal
exit /b 0
