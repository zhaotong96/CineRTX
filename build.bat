@echo off
REM SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
REM build.bat - zero-assumption entry point for hosts without CMake.
REM
REM This script owns exactly one job: make cmake.exe available.  Everything
REM else - the Ninja generator, the compiler with its portable Zig fallback,
REM NASM, GNU Make, the git submodules and the static FFmpeg archives - is
REM provisioned by the CMake configure step, which knows what the build needs.
REM
REM A host that already has CMake never needs this script:
REM     cmake --preset zig-windows
REM     cmake --build --preset zig-windows
REM
REM Usage:  build.bat [extra cmake configure args...]
REM         (all arguments are forwarded to the cmake configure step)
REM
REM Proxying: the CMake download below uses curl, which honours the
REM HTTPS_PROXY/HTTP_PROXY environment variables.  Set them before running
REM this script if the network requires a proxy.
REM
REM Exit code is the real exit code of the configure/build steps.

setlocal EnableDelayedExpansion

set "REPO=%~dp0"
if "!REPO:~-1!"=="\" set "REPO=!REPO:~0,-1!"
set "EXTDIR=!REPO!\build\external"
set "DLDIR=!EXTDIR!\download"
set "TOOLDIR=!EXTDIR!\tools"
set "BDIR=!REPO!\build"
set "SYS32=%SystemRoot%\System32"

REM Guarantee System32 is reachable for child processes even with empty PATH.
set "PATH=!SYS32!;%SystemRoot%;!PATH!"

if not exist "!DLDIR!"   mkdir "!DLDIR!"
if not exist "!TOOLDIR!" mkdir "!TOOLDIR!"

REM ---- Pinned upstream artifact (URL / exact size / SHA256, verified 2026-10) ---
set "CMAKE_URL=https://cmake.org/files/v3.30/cmake-3.30.5-windows-x86_64.zip"
set "CMAKE_SIZE=45417243"
set "CMAKE_SHA=5ab6e1faf20256ee4f04886597e8b6c3b1bd1297b58a68a58511af013710004b"

REM ---- 1. CMake is the only thing this layer provisions ---------------------
set "CMAKE_EXE="
for /f "delims=" %%i in ('where.exe cmake.exe 2^>nul') do (
    if not defined CMAKE_EXE set "CMAKE_EXE=%%i"
)
if not defined CMAKE_EXE (
    call :ensure cmake "!CMAKE_URL!" !CMAKE_SIZE! !CMAKE_SHA! cmake.exe
    if errorlevel 1 exit /b 1
)
for %%D in ("!CMAKE_EXE!") do set "PATH=%%~pD;!PATH!"
echo [CineRTX-Bootstrap] cmake: !CMAKE_EXE!

REM ---- 2. Hand off. CMake provisions the generator, compiler and deps -------
"!CMAKE_EXE!" -S "!REPO!" -B "!BDIR!" -G Ninja ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_TOOLCHAIN_FILE="!REPO!\cmake\toolchain-zig.cmake" %*
if errorlevel 1 exit /b 1

"!CMAKE_EXE!" --build "!BDIR!" -j
set "RC=!errorlevel!"
endlocal & exit /b %RC%

REM ============================================================================
REM :ensure <name> <url> <expected_size> <expected_sha256|-> <exe_filename>
REM Downloads (resumable), hash-verifies and idempotently extracts one tool
REM into build\external\tools\<name>\.  On success defines:
REM   <name>_EXE  full path to <exe_filename>
REM ============================================================================
:ensure
set "E_DIR=!TOOLDIR!\%~1"
set "E_ARC=!DLDIR!\%~1.zip"
set "E_EXE="
if exist "!E_DIR!" (
    for /f "delims=" %%i in ('dir /s /b "!E_DIR!\%~5" 2^>nul') do (
        if not defined E_EXE set "E_EXE=%%i"
    )
)
if defined E_EXE goto :ensure_done

set "E_SIZE=0"
if exist "!E_ARC!" for %%A in ("!E_ARC!") do set "E_SIZE=%%~zA"
if "!E_SIZE!" NEQ "%~3" (
    echo [CineRTX-Bootstrap] Downloading %~1 ^(%~3 bytes^) from %~2
    "!SYS32!\curl.exe" -L --retry 3 --retry-delay 2 -C - -o "!E_ARC!" "%~2"
    if errorlevel 1 (
        REM Resume may be rejected by the server; retry from scratch.
        "!SYS32!\curl.exe" -L --retry 3 --retry-delay 2 -o "!E_ARC!" "%~2"
    )
    if errorlevel 1 (
        echo [CineRTX-Bootstrap] ERROR: download failed: %~2
        exit /b 1
    )
    set "E_SIZE=0"
    for %%A in ("!E_ARC!") do set "E_SIZE=%%~zA"
    if "!E_SIZE!" NEQ "%~3" (
        echo [CineRTX-Bootstrap] ERROR: !E_ARC! is !E_SIZE! bytes, expected %~3.
        exit /b 1
    )
)

if not "%~4"=="-" (
    set "E_HASH="
    REM NB: unquoted certutil - for /f strips the outer quotes of a leading
    REM quoted program path; System32 was prepended to PATH at startup.
    for /f "skip=1 delims=" %%h in ('certutil.exe -hashfile "!E_ARC!" SHA256') do (
        if not defined E_HASH set "E_HASH=%%h"
    )
    set "E_HASH=!E_HASH: =!"
    if /i not "!E_HASH!"=="%~4" (
        echo [CineRTX-Bootstrap] ERROR: SHA256 mismatch for !E_ARC!
        echo   expected: %~4
        echo   actual  : !E_HASH!
        echo   Delete the file and re-run to download a fresh copy.
        exit /b 1
    )
)

echo [CineRTX-Bootstrap] Extracting %~1 into !E_DIR! ...
if not exist "!E_DIR!" mkdir "!E_DIR!"
"!SYS32!\tar.exe" -xf "!E_ARC!" -C "!E_DIR!"
if errorlevel 1 (
    echo [CineRTX-Bootstrap] ERROR: tar.exe failed to extract !E_ARC!
    exit /b 1
)
for /f "delims=" %%i in ('dir /s /b "!E_DIR!\%~5" 2^>nul') do (
    if not defined E_EXE set "E_EXE=%%i"
)
if not defined E_EXE (
    echo [CineRTX-Bootstrap] ERROR: %~5 not found after extracting !E_ARC!
    exit /b 1
)

:ensure_done
"!E_EXE!" --version >nul 2>&1
if errorlevel 1 (
    echo [CineRTX-Bootstrap] ERROR: "!E_EXE!" --version failed.
    exit /b 1
)
set "%~1_EXE=!E_EXE!"
echo [CineRTX-Bootstrap] %~1 ready: !E_EXE!
exit /b 0
