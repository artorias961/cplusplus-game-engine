@echo off
rem ---------------------------------------------------------------------------
rem verify.bat — the checks this project actually uses, in one place.
rem
rem run.bat plays the games. This runs the things you do before believing a
rem change is finished. Every option here has caught a real bug in this
rem repository at least once; none of it is ceremony.
rem
rem     verify.bat            ask which check
rem     verify.bat quick      build, then every test once
rem     verify.bat flake      the same tests twenty times over
rem     verify.bat smoke      do the shipped games actually start?
rem     verify.bat render     the pixel tests
rem     verify.bat bench      how much do the naive parts cost?
rem     verify.bat clean      throw the build away and rebuild from nothing
rem     verify.bat archive    do the frozen versions still build on their own?
rem     verify.bat linux      the GitHub Linux job, in Docker, before you push
rem     verify.bat all        everything. Run this before you commit.
rem
rem See mutate.bat for the other half: these check the code passes its tests,
rem that one checks the tests would notice if it didn't.
rem ---------------------------------------------------------------------------

setlocal enabledelayedexpansion

rem Work from the project root, which is the parent of this script's folder.
cd /d "%~dp0.."

set "CHECK=%~1"
set "FAILED=0"

rem --- Find vcpkg -----------------------------------------------------------
set "TOOLCHAIN="
if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" set "TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined TOOLCHAIN if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" set "TOOLCHAIN=C:\vcpkg\scripts\buildsystems\vcpkg.cmake"

if not "%CHECK%"=="" goto dispatch

echo.
echo   [1] quick    every test, once
echo   [2] flake    every test, twenty times
echo   [3] smoke    do the shipped games start?
echo   [4] render   the renderer, checked against pixels
echo   [5] bench    what the naive parts cost
echo   [6] clean    rebuild from nothing, then test
echo   [7] archive  do the frozen versions still build?
echo   [8] all      everything (run this before committing)
echo   [9] linux    the GitHub Linux build, in Docker
echo.
:menu
set "PICK="
set /p "PICK=Choose 1-9: "
if "%PICK%"=="1" set "CHECK=quick"
if "%PICK%"=="2" set "CHECK=flake"
if "%PICK%"=="3" set "CHECK=smoke"
if "%PICK%"=="4" set "CHECK=render"
if "%PICK%"=="5" set "CHECK=bench"
if "%PICK%"=="6" set "CHECK=clean"
if "%PICK%"=="7" set "CHECK=archive"
if "%PICK%"=="8" set "CHECK=all"
if "%PICK%"=="9" set "CHECK=linux"
if "%CHECK%"=="" goto menu

:dispatch
if /i "%CHECK%"=="quick"   goto quick
if /i "%CHECK%"=="flake"   goto flake
if /i "%CHECK%"=="smoke"   goto smoke
if /i "%CHECK%"=="render"  goto render
if /i "%CHECK%"=="bench"   goto bench
if /i "%CHECK%"=="clean"   goto clean
if /i "%CHECK%"=="archive" goto archive
if /i "%CHECK%"=="linux"   goto linux
if /i "%CHECK%"=="all"     goto all

echo Unknown check: %CHECK%
echo Expected one of: quick flake smoke render bench clean archive linux all
goto done

rem --- Building -------------------------------------------------------------
:build
if not exist "build\CMakeCache.txt" (
    echo Configuring...
    if defined TOOLCHAIN (
        cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" -A x64
    ) else (
        echo   vcpkg not found - if SDL2 is missing, see README.md
        cmake -S . -B build -A x64
    )
    if errorlevel 1 set "FAILED=1" & exit /b 1
)
echo Building...
cmake --build build --config Release
if errorlevel 1 set "FAILED=1" & exit /b 1
exit /b 0

rem --- The checks -----------------------------------------------------------
:quick
echo.
echo ==============================================================
echo   Every test, once
echo ==============================================================
call :build || goto done
ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 set "FAILED=1"
goto done

rem Twenty runs, not one. The first real bug this repository's CI ever caught
rem failed eleven times in two hundred: a test that killed the player's ship
rem with a random rock and then dereferenced it. One green run would have
rem shipped that.
:flake
echo.
echo ==============================================================
echo   Every test, twenty times (hunting for flakes)
echo ==============================================================
call :build || goto done
ctest --test-dir build -C Release --output-on-failure --repeat until-fail:20 -LE smoke
if errorlevel 1 set "FAILED=1"
goto done

rem The shallowest test here, covering the deepest untested seam. Everything
rem else drives the games through tests\Harness.h, which reimplements the loop.
rem Nothing else executes main.cpp, the real Engine::run, window creation or
rem audio startup.
:smoke
echo.
echo ==============================================================
echo   Do the shipped games start?
echo ==============================================================
call :build || goto done
ctest --test-dir build -C Release --output-on-failure -L smoke
if errorlevel 1 set "FAILED=1"
goto done

:render
echo.
echo ==============================================================
echo   The renderer, checked against real pixels
echo ==============================================================
call :build || goto done
set "SDL_VIDEODRIVER=dummy"
"build\Release\render_tests.exe"
if errorlevel 1 set "FAILED=1"
set "SDL_VIDEODRIVER="
goto done

rem Not pass/fail. Numbers, so "optimise it later" can be decided rather than
rem argued about.
:bench
echo.
echo ==============================================================
echo   What the naive parts cost
echo ==============================================================
call :build || goto done
"build\Release\engine_bench.exe"
goto done

rem Catches what an incremental build hides: a header nobody includes any more,
rem a stale object file, a CMake change that only works because your build
rem folder already had the answer.
:clean
echo.
echo ==============================================================
echo   Rebuild from nothing
echo ==============================================================
if exist "build" rmdir /s /q "build"
call :build || goto done
ctest --test-dir build -C Release --output-on-failure
if errorlevel 1 set "FAILED=1"
goto done

rem The README promises each archived version still builds on its own. Nothing
rem else checks that promise, and the archives are never updated - so the only
rem way it can break is a change outside them, which is exactly the kind of
rem breakage nobody would look for.
:archive
echo.
echo ==============================================================
echo   Do the frozen versions still build?
echo ==============================================================
for /d %%V in (archive\*) do (
    if exist "%%V\CMakeLists.txt" (
        echo.
        echo --- %%~nxV
        if defined TOOLCHAIN (
            cmake -S "%%V" -B "%%V\_verify" -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" -A x64 >nul 2>&1
        ) else (
            cmake -S "%%V" -B "%%V\_verify" -A x64 >nul 2>&1
        )
        cmake --build "%%V\_verify" --config Release >nul 2>&1
        if errorlevel 1 (
            echo   FAILED: %%~nxV does not build
            set "FAILED=1"
        ) else (
            echo   OK: %%~nxV builds
        )
    )
)
goto done

rem The GitHub Linux job, run in Docker on this machine before you push.
rem
rem Everything above builds with MSVC. CI builds with GCC, and the two disagree
rem about more than you would think: the Linux job was red for three commits
rem because of one missing `#include <cstring>`, which MSVC and macOS both let
rem slide. A build failure skips every later step, so for those three commits
rem Linux ran no tests at all - and the only way to read why was to log in to
rem GitHub. This runs the same Ubuntu, the same packages and the same steps.
rem
rem It tests what `git add -A` would commit: tracked files plus any new ones that
rem are not ignored, and it lists the new ones so you can see a file the build
rem needs that you have not added yet.
:linux
echo.
echo ==============================================================
echo   The GitHub Linux build (GCC, in Docker)
echo ==============================================================
docker info >nul 2>&1
if errorlevel 1 (
    echo.
    echo   SKIPPED - Docker is not running.
    echo.
    echo   This is the only check here that uses GCC, and GCC is what the GitHub
    echo   Linux job builds with. Start Docker Desktop, then run:
    echo       manual_testing\verify.bat linux
    set "FAILED=2"
    goto done
)
echo Preparing the Linux image - cached after the first time...
docker build -q -t tiny-engine-linux-ci -f manual_testing\linux-ci.Dockerfile manual_testing >nul
if errorlevel 1 (
    echo   FAILED: could not build the Linux image. Run the docker build by hand
    echo   to see why: docker build -f manual_testing\linux-ci.Dockerfile manual_testing
    set "FAILED=1"
    goto done
)

set "FILELIST=%TEMP%\tiny-engine-linux-ci-files.txt"
git -c core.quotepath=off ls-files -co --exclude-standard > "%FILELIST%"
set "UNTRACKED=0"
for /f %%N in ('git ls-files -o --exclude-standard ^| find /c /v ""') do set "UNTRACKED=%%N"
if not "!UNTRACKED!"=="0" (
    echo.
    echo   Testing the tree as "git add -A" would commit it. Git is not yet
    echo   tracking !UNTRACKED! of these files:
    for /f "delims=" %%F in ('git -c core.quotepath^=off ls-files -o --exclude-standard') do echo     %%F
)

docker run --rm -v "%CD%:/repo:ro" -v "%FILELIST%:/files.txt:ro" tiny-engine-linux-ci bash /repo/manual_testing/linux-ci.sh
if errorlevel 1 (
    set "FAILED=1"
    echo.
    echo   FAILED - and it will fail on GitHub the same way. Fix it before pushing.
) else (
    echo.
    echo   OK: the Linux job passes.
)
goto done

:all
call "%~f0" clean
if errorlevel 1 set "FAILED=1"
call "%~f0" flake
if errorlevel 1 set "FAILED=1"
call "%~f0" smoke
if errorlevel 1 set "FAILED=1"
call "%~f0" render
if errorlevel 1 set "FAILED=1"
rem Exit code 2 is "skipped because Docker is not running": said out loud in the
rem verdict rather than folded into a pass, and not a failure either, because
rem refusing to finish without Docker would just teach people to stop running
rem this.
set "LINUX_SKIPPED="
call "%~f0" linux
if errorlevel 2 (
    set "LINUX_SKIPPED=1"
) else if errorlevel 1 (
    set "FAILED=1"
)
call "%~f0" archive
if errorlevel 1 set "FAILED=1"
call "%~f0" bench
echo.
echo ==============================================================
echo   Verdict
echo ==============================================================
if not "%FAILED%"=="0" (
    echo   Something failed. Scroll up; the failures say OK or FAILED.
) else if defined LINUX_SKIPPED (
    echo   Everything that ran passed - but the Linux build was NOT checked,
    echo   because Docker is not running. GitHub builds with GCC and this did
    echo   not, so CI can still fail. Start Docker Desktop and run the linux check.
) else (
    echo   Everything passed, including the Linux build.
)
goto done

:done
echo.
if not "%~1"=="" exit /b %FAILED%
pause
exit /b %FAILED%
