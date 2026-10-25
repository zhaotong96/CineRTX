# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Portable Zig 0.13 toolchain for CMake.
#
# Usage:
#   cmake -S . -B build/zig -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-zig.cmake
#
# Set RTX_ZIG_EXECUTABLE (cache or environment) to the zig binary.  The
# toolchain adds the `c++` subcommand through CMAKE_CXX_COMPILER_ARG1 so CMake
# can still identify the underlying Clang compiler.

if(NOT RTX_ZIG_EXECUTABLE)
    if(DEFINED ENV{RTX_ZIG})
        set(RTX_ZIG_EXECUTABLE "$ENV{RTX_ZIG}" CACHE FILEPATH "Zig executable" FORCE)
    else()
        find_program(RTX_ZIG_EXECUTABLE NAMES zig)
    endif()
endif()

if(NOT RTX_ZIG_EXECUTABLE OR NOT EXISTS "${RTX_ZIG_EXECUTABLE}")
    message(FATAL_ERROR
        "Zig was not found. Pass -DRTX_ZIG_EXECUTABLE=/path/to/zig or set RTX_ZIG.")
endif()

get_filename_component(RTX_ZIG_EXECUTABLE "${RTX_ZIG_EXECUTABLE}" ABSOLUTE)
file(TO_CMAKE_PATH "${RTX_ZIG_EXECUTABLE}" RTX_ZIG_EXECUTABLE)

set(CMAKE_CXX_COMPILER "${RTX_ZIG_EXECUTABLE}")
set(CMAKE_CXX_COMPILER_ARG1 "c++")

# CMake otherwise looks for a GNU-style ar/ranlib next to the compiler.
# Zig ships LLVM's ar, which understands `qc` and `s`.
set(CMAKE_CXX_ARCHIVE_CREATE
    "\"${RTX_ZIG_EXECUTABLE}\" ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_APPEND
    "\"${RTX_ZIG_EXECUTABLE}\" ar q <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_FINISH
    "\"${RTX_ZIG_EXECUTABLE}\" ar s <TARGET>")

# CMake's compiler ABI probe appends `-v` to the link line, which Zig's lld
# rejects.  The probe is only diagnostic; the compiler check itself succeeds.
# Native target architecture is supplied so GNUInstallDirs can pick lib/lib64.
if(CMAKE_HOST_WIN32)
    set(CMAKE_SYSTEM_PROCESSOR "AMD64" CACHE STRING "" FORCE)
endif()
