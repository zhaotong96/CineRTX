# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Generator bootstrap, evaluated from the top of CMakeLists.txt before
# project() enables any language.
#
# CMake consults CMAKE_MAKE_PROGRAM when project() creates the Ninja
# generator, so a host that has CMake but no Ninja fails at project().  This
# include repairs that before the check: an existing program wins, then a
# PATH candidate, then Ninja bundled with Visual Studio, then a pinned
# ninja-win.zip under build/external (curl first, SHA-256 verified).
#
# Only the generator lives here.  The compiler (including the portable Zig
# fallback) is chosen by cmake/toolchain-zig.cmake, and nasm/make, the
# submodules and the static FFmpeg archives are handled by the configure step
# that follows.

include("${CMAKE_CURRENT_LIST_DIR}/RtxProvision.cmake")

if(NOT CMAKE_GENERATOR MATCHES "^Ninja")
    return()
endif()

if(CMAKE_MAKE_PROGRAM AND EXISTS "${CMAKE_MAKE_PROGRAM}")
    return()
endif()

find_program(_rtx_ninja_program NAMES ninja ninja.exe)
if(NOT _rtx_ninja_program AND CMAKE_HOST_WIN32)
    # $ENV{ProgramFiles(x86)} cannot be parsed by CMake (parentheses are not
    # allowed in variable names); ProgramW6432 is the unambiguous 64-bit root.
    file(GLOB _rtx_vs_ninja
        "$ENV{ProgramFiles}/Microsoft Visual Studio/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
        "$ENV{ProgramW6432}/Microsoft Visual Studio/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe")
    if(_rtx_vs_ninja)
        list(GET _rtx_vs_ninja 0 _rtx_ninja_program)
    endif()
endif()

if(NOT _rtx_ninja_program)
    if(NOT CMAKE_HOST_WIN32)
        message(FATAL_ERROR
            "[CineRTX-Bootstrap] Ninja was not found.\n"
            "Install ninja, put it on PATH, or pass -DCMAKE_MAKE_PROGRAM=/path/to/ninja.")
    endif()
    rtx_provision_tool(ninja
        "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip"
        "f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a"
        ninja.exe
        "${CMAKE_CURRENT_LIST_DIR}/../build/external"
        _rtx_ninja_program)
endif()

if(NOT _rtx_ninja_program)
    message(FATAL_ERROR
        "[CineRTX-Bootstrap] Ninja could not be provisioned.\n"
        "  - Install ninja or pass -DCMAKE_MAKE_PROGRAM=/path/to/ninja.exe, or\n"
        "  - set HTTPS_PROXY/HTTP_PROXY and configure again so curl can download\n"
        "    https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip")
endif()

set(CMAKE_MAKE_PROGRAM "${_rtx_ninja_program}" CACHE FILEPATH "Ninja executable" FORCE)
message(STATUS "[CineRTX-Bootstrap] Ninja: ${_rtx_ninja_program}")
