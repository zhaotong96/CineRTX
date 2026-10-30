@echo off
setlocal enabledelayedexpansion

REM SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
REM tools/build_ffmpeg_static.bat
REM Unified wrapper for building surgical static FFmpeg:
REM   - default     : MSVC path (cl.exe / lib.exe, vcvars located via vswhere)
REM   - Zig path    : Zig/MinGW-w64 cross path (zig cc -target x86_64-windows-gnu)
REM                   for zero-assumption hosts with no MSVC, or whenever the
REM                   caller exported RTX_ZIG (cmake/toolchain-zig.cmake,
REM                   build.bat).
REM An explicit RTX_ZIG wins over an ambient cl.exe: the C++ core is then built
REM for the GNU ABI, so the FFmpeg archives must come from the same toolchain
REM even inside a Visual Studio developer environment.

set "TOOLCHAIN_SH=build_ffmpeg_static_msvc.sh"
set "USE_MSVC=1"
if defined RTX_ZIG set "USE_MSVC="

if defined USE_MSVC (
    REM ---- 1. Try to establish MSVC (default path) ----
    where cl.exe >nul 2>&1
    if errorlevel 1 (
        set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
        if exist "!VSWHERE!" (
            for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -property installationPath`) do (
                set "VS_DIR=%%i"
            )
        )
        if defined VS_DIR (
            if exist "!VS_DIR!\VC\Auxiliary\Build\vcvars64.bat" (
                echo [CineRTX-FFmpeg] Initializing MSVC x64 environment from !VS_DIR!...
                call "!VS_DIR!\VC\Auxiliary\Build\vcvars64.bat" >nul
            )
        )
    )
    where cl.exe >nul 2>&1
    if errorlevel 1 set "USE_MSVC="
)

REM ---- 2. Zig/MinGW-w64 cross path when MSVC is unavailable or bypassed ----
if not defined USE_MSVC (
    set "ZIG_EXE="
    if defined RTX_ZIG set "ZIG_EXE=!RTX_ZIG!"
    if not defined ZIG_EXE (
        for /f "delims=" %%i in ('where zig.exe 2^>nul') do (
            if not defined ZIG_EXE set "ZIG_EXE=%%i"
        )
    )
    if not defined ZIG_EXE if exist "%~dp0..\build\external\tools\zig" (
        for /f "delims=" %%i in ('dir /s /b "%~dp0..\build\external\tools\zig\zig.exe" 2^>nul') do (
            if not defined ZIG_EXE set "ZIG_EXE=%%i"
        )
    )
    if defined ZIG_EXE (
        echo [CineRTX-FFmpeg] Using Zig/MinGW-w64 cross path: !ZIG_EXE!
        set "RTX_ZIG=!ZIG_EXE!"
        set "TOOLCHAIN_SH=build_ffmpeg_static_zig.sh"
        for %%D in ("!ZIG_EXE!") do set "PATH=%%~pD;!PATH!"
        REM Zig needs a writable cache dir even on a bare host (no LOCALAPPDATA);
        REM keep it inside the repo, consistent with cmake/toolchain-zig.cmake.
        if not defined ZIG_GLOBAL_CACHE_DIR set "ZIG_GLOBAL_CACHE_DIR=%~dp0..\build\external\zig-cache"
        if not defined ZIG_LOCAL_CACHE_DIR  set "ZIG_LOCAL_CACHE_DIR=%~dp0..\build\external\zig-cache"
        if not defined TEMP set "TEMP=%~dp0..\build\external\tmp"
        if not defined TMP  set "TMP=!TEMP!"
        if not exist "!TEMP!" mkdir "!TEMP!"
    ) else (
        echo [CineRTX-FFmpeg] ERROR: neither cl.exe nor zig could be found.
        echo   The CMake toolchain normally provisions Zig under
        echo   build\external\tools\ before this script runs; install a
        echo   compiler or pass -DRTX_ZIG_EXECUTABLE=... to configure.
        exit /b 1
    )
)

REM ---- 3. Find bash (Git Bash / MSYS2) ----
set "BASH_CMD="
where bash.exe >nul 2>&1
if %errorlevel% equ 0 (
    for /f "delims=" %%i in ('where bash.exe') do (
        if not defined BASH_CMD set "BASH_CMD=%%i"
    )
)
if not defined BASH_CMD (
    for %%p in (
        "C:\Program Files\Git\bin\bash.exe"
        "C:\Program Files\Git\usr\bin\bash.exe"
        "C:\Program Files (x86)\Git\bin\bash.exe"
        "%LOCALAPPDATA%\Programs\Git\bin\bash.exe"
        "%LOCALAPPDATA%\Programs\Git\usr\bin\bash.exe"
        "C:\msys64\usr\bin\bash.exe"
    ) do (
        if not defined BASH_CMD if exist %%p set "BASH_CMD=%%~p"
    )
)
if not defined BASH_CMD (
    for /d %%g in (
        "C:\Program Files\Git*"
        "C:\Program Files (x86)\Git*"
        "C:\Program Files\PortableGit*"
        "%LOCALAPPDATA%\Programs\Git*"
    ) do (
        if not defined BASH_CMD if exist "%%~fg\bin\bash.exe" set "BASH_CMD=%%~fg\bin\bash.exe"
    )
)

if not defined BASH_CMD (
    echo [CineRTX-FFmpeg] ERROR: bash.exe not found. Git for Windows or MSYS2 is required.
    exit /b 1
)

REM ---- 4. Expose bash's directory (sh.exe) to child processes ----
REM The provisioned native make.exe executes FFmpeg's POSIX makefile recipes
REM through sh.exe when it is discoverable on PATH.
for %%D in ("!BASH_CMD!") do set "PATH=%%~pD;!PATH!"

set "SCRIPT_DIR=%~dp0"
set "SH_SCRIPT=!SCRIPT_DIR!!TOOLCHAIN_SH!"

REM Convert backslashes for bash
set "SH_SCRIPT=!SH_SCRIPT:\=/!"

"!BASH_CMD!" "!SH_SCRIPT!" %*
exit /b %errorlevel%
