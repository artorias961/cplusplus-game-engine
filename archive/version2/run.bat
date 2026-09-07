@echo off
rem ---------------------------------------------------------------------------
rem run.bat — double-click this to build the project and play a game.
rem
rem Configures with CMake the first time, builds Release every time, then runs
rem whichever game you pick. Safe to run repeatedly: the build is incremental,
rem so after the first one it only recompiles what changed.
rem
rem From a terminal you can skip the menu:
rem     run.bat asteroids
rem     run.bat breakout
rem     run.bat tests
rem     run.bat clean       (throws the build folder away and stops)
rem ---------------------------------------------------------------------------

setlocal

rem Work from the folder this script lives in, not wherever it was launched.
cd /d "%~dp0"

set "GAME=%~1"

if /i "%GAME%"=="clean" (
    echo Removing the build folder...
    if exist "build" rmdir /s /q "build"
    echo Done.
    goto done
)

rem --- Find vcpkg -----------------------------------------------------------
rem SDL2 comes from vcpkg on Windows. VCPKG_ROOT wins if you have set it;
rem otherwise the usual C:\vcpkg is tried. Without either, CMake is still run
rem and will say clearly that it cannot find SDL2.
set "TOOLCHAIN="
if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" set "TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined TOOLCHAIN if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" set "TOOLCHAIN=C:\vcpkg\scripts\buildsystems\vcpkg.cmake"

rem --- Configure (first run only) -------------------------------------------
if not exist "build\CMakeCache.txt" (
    echo Configuring...
    if defined TOOLCHAIN (
        cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" -A x64
    ) else (
        echo   vcpkg not found - if SDL2 is missing, see README.md
        cmake -S . -B build -A x64
    )
    if errorlevel 1 goto fail
)

rem --- Build ----------------------------------------------------------------
echo Building...
cmake --build build --config Release
if errorlevel 1 goto fail

if not "%GAME%"=="" goto run

rem --- Ask which game -------------------------------------------------------
:menu
echo.
echo   [1] Asteroids
echo   [2] Breakout
echo   [3] Run the tests
echo.
set "CHOICE="
set /p "CHOICE=Choose 1-3: "
if "%CHOICE%"=="1" set "GAME=asteroids"
if "%CHOICE%"=="2" set "GAME=breakout"
if "%CHOICE%"=="3" set "GAME=tests"
if "%GAME%"=="" goto menu

rem --- Run ------------------------------------------------------------------
:run
if /i "%GAME%"=="tests" (
    ctest --test-dir build -C Release --output-on-failure
    goto done
)

if not exist "build\Release\%GAME%.exe" (
    echo.
    echo Could not find build\Release\%GAME%.exe
    echo Expected one of: asteroids, breakout, tests
    goto fail
)

echo.
echo Starting %GAME% - Escape quits.
"build\Release\%GAME%.exe"
goto done

:fail
echo.
echo Something went wrong; the messages above say what.
echo.
pause
exit /b 1

:done
echo.
pause
exit /b 0
