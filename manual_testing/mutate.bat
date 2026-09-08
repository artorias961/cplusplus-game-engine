@echo off
rem ---------------------------------------------------------------------------
rem mutate.bat — break the code on purpose and check a test notices.
rem
rem This is the single most useful technique in this project, and the one that
rem keeps finding tests which pass for the wrong reason:
rem
rem   * a range test that stood a unit at 1600 and aimed at where it STOOD. The
rem     unit walked eighty pixels during the shell's flight, so it left the
rem     blast whether or not the cannon could reach. Setting the range ten
rem     times too far changed nothing it looked at.
rem   * a font test that probed the first lit column of a glyph - column zero,
rem     where col * scale and col are both nought. A renderer that squashed
rem     every string to a third of its width passed cleanly.
rem   * a per-field fallback checked only for "range", so breaking the fallback
rem     on "cost" left the whole suite green.
rem   * a determinism test that ran the same case six times and checked the
rem     answers agreed. They agreed with or without the rule it was testing.
rem
rem Every one was written in good faith, ran the code it meant to run, and
rem would not have noticed the code being wrong. There is no way to tell the
rem difference by reading. You have to break it and look.
rem
rem USAGE
rem     mutate.bat <file> <find> <replace> [ctest-name]
rem
rem     mutate.bat include\engine\View.h "worldX - camera.x" "worldX + camera.x" render_tests
rem
rem The text is matched LITERALLY, so C++ punctuation needs no escaping.
rem
rem WHAT THE RESULT MEANS
rem     CAUGHT    - good. The tests would notice this bug.
rem     SURVIVED  - bad. Either the change is harmless, or you have found a
rem                 test that does not test what it claims to.
rem
rem The file is always restored.
rem
rem A NOTE ON LINE ENDINGS, because this file was broken by them and the
rem symptom pointed nowhere near the cause.
rem
rem This script must be stored with CRLF endings. cmd.exe does not read a batch
rem file line by line - it seeks by byte offset after each command, and a file
rem ending LF makes it land mid-line and run the tail of one as a command. The
rem symptom was a wall of "'m' is not recognized as an internal or external
rem command" followed by "< was unexpected at this time", on the example in the
rem USAGE block above, with no argument reaching the script at all.
rem
rem Nothing here says "line endings". verify.bat, in the same folder and just
rem as LF, runs perfectly - the mis-seek only lands badly in some files - so
rem the obvious comparison pointed the wrong way too. `.gitattributes` now
rem pins *.bat to CRLF so this cannot come back.
rem ---------------------------------------------------------------------------

setlocal

cd /d "%~dp0.."

if "%~3"=="" (
    echo Usage: mutate.bat ^<file^> ^<find^> ^<replace^> [ctest-name]
    echo.
    echo Example:
    echo   mutate.bat include\engine\View.h "worldX - camera.x" "worldX + camera.x" render_tests
    echo.
    pause
    exit /b 1
)

set "FILE=%~1"
set "FIND=%~2"
set "REPLACE=%~3"
set "TARGET=%~4"

if not exist "%FILE%" (
    echo No such file: %FILE%
    pause
    exit /b 1
)

set "BACKUP=%TEMP%\mutate_backup_%RANDOM%.bak"
copy /y "%FILE%" "%BACKUP%" >nul

set "TOOLCHAIN="
if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" set "TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined TOOLCHAIN if exist "C:\vcpkg\scripts\buildsystems\vcpkg.cmake" set "TOOLCHAIN=C:\vcpkg\scripts\buildsystems\vcpkg.cmake"

echo Mutating %FILE%
echo    from: %FIND%
echo      to: %REPLACE%

rem A literal replacement through .NET, so punctuation in the pattern is not
rem treated as a regular expression. Doing this with a text-substitution tool
rem and its own delimiter is how half the hand-run mutations in this project
rem silently failed to apply.
rem
rem On ONE line rather than wrapped with a `^` continuation. That is a small
rem robustness point and was NOT what broke this script: see the note about
rem line endings at the top of the file, which is the real story.
powershell -NoProfile -Command "$p=$env:FILE; $c=[IO.File]::ReadAllText($p); $c=$c.Replace($env:FIND,$env:REPLACE); [IO.File]::WriteAllText($p,$c)"

rem Verified rather than assumed: a mutation that did not apply produces a
rem green run that looks exactly like a surviving mutant.
findstr /c:"%REPLACE%" "%FILE%" >nul
if errorlevel 1 (
    echo.
    echo   THE MUTATION DID NOT APPLY.
    echo   The text was not found. Copy the line exactly as it appears in the
    echo   file, including indentation inside the quotes if you need it.
    goto restore_and_exit
)

echo Building...
if not exist "build\CMakeCache.txt" (
    if defined TOOLCHAIN (
        cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" -A x64 >nul
    ) else (
        cmake -S . -B build -A x64 >nul
    )
)
cmake --build build --config Release >nul 2>&1
if errorlevel 1 (
    echo.
    echo   The mutated code does not compile.
    echo   That is not a result: pick a change that builds, so the tests get a
    echo   chance to have an opinion.
    goto restore_and_exit
)

echo Testing...
echo.
if "%TARGET%"=="" (
    ctest --test-dir build -C Release --output-on-failure
) else (
    ctest --test-dir build -C Release --output-on-failure -R "^%TARGET%$"
)

if errorlevel 1 (
    set "VERDICT=CAUGHT"
) else (
    set "VERDICT=SURVIVED"
)

echo.
echo ==============================================================
if "%VERDICT%"=="CAUGHT" (
    echo   CAUGHT - the tests noticed. This bug could not ship silently.
) else (
    echo   SURVIVED - every test passed with the code broken.
    echo.
    echo   Either this change genuinely does not matter, or a test that looks
    echo   like it covers this does not. Both are worth knowing; the second is
    echo   worth fixing before you trust that test again.
)
echo ==============================================================

:restore_and_exit
copy /y "%BACKUP%" "%FILE%" >nul
del "%BACKUP%" >nul 2>&1

rem The timestamp is bumped deliberately, and this is not fussiness.
rem
rem `copy` can hand the restored file the BACKUP's modification time, which is
rem older than the object files built from the mutated source. MSBuild then
rem decides there is nothing to do, and you are left with correct source and
rem mutated binaries — a state that looks fine, tests broken, and takes a long
rem time to understand. Found by this very script leaving the suite red.
powershell -NoProfile -Command "(Get-Item $env:FILE).LastWriteTime = Get-Date"

echo.
echo (%FILE% restored)
echo Rebuilding the unmutated code...
cmake --build build --config Release >nul 2>&1

rem Proved, not assumed. Restoring a file is worth nothing if the build did
rem not follow it, so the tree is checked back to green before you are told
rem it is safe.
echo Confirming the tree is back to green...
if "%TARGET%"=="" (
    ctest --test-dir build -C Release --quiet >nul 2>&1
) else (
    ctest --test-dir build -C Release --quiet -R "^%TARGET%$" >nul 2>&1
)
if errorlevel 1 (
    echo.
    echo   WARNING: tests are still failing after the restore.
    echo   The source is back, so this is a stale build. Run:
    echo       cmake --build build --config Release --clean-first
) else (
    echo   Clean.
)
echo.
pause
exit /b 0
