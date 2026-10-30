# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Toolchain selector behind the `zig-windows` / `zig-linux` presets.
#
# Priority, per docs/phase3/cinertx_unified_zero_assumption_and_single_exe_construction_plan.md:
#   1. An explicitly requested Zig: the RTX_ZIG_EXECUTABLE cache variable or
#      the RTX_ZIG environment variable. CI takes this path, so a Zig build is
#      always bit-for-bit reproducible.
#   2. A host-native compiler (MSVC cl, Clang, GCC) already configured on the
#      machine: it is used as-is and Zig plays no part in the build. Visual
#      Studio's CMake integration supplies the MSVC environment (cl.exe,
#      INCLUDE, LIB, VCToolsVersion), so opening the repository in Visual
#      Studio builds natively without installing anything.
#   3. A portable Zig kit already under build/external/tools/zig, or zig on PATH.
#   4. No compiler at all: the pinned Zig 0.13.0 archive is downloaded (curl
#      first, SHA-256 verified) and unpacked under build/external.  This is the
#      only network access a configure can trigger, and only on a bare host.
#   5. Nothing usable: hard error.
#
# Compiler selection is this file's whole job.  The Ninja generator is
# provisioned by cmake/RtxBootstrap.cmake before project(); build.bat only
# supplies CMake itself.  When Zig is selected, the `c++` subcommand is added
# through CMAKE_CXX_COMPILER_ARG1 so CMake can still identify the underlying
# Clang compiler.

get_filename_component(_rtx_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_rtx_zig_bootstrap_dir "${_rtx_repo_root}/build/external/tools/zig")
set(_rtx_zig_version "0.13.0")
set(_rtx_zig_url
    "https://ziglang.org/download/0.13.0/zig-windows-x86_64-0.13.0.zip")
set(_rtx_zig_sha256
    "d859994725ef9402381e557c60bb57497215682e355204d754ee3df75ee3c158")

include("${_rtx_repo_root}/cmake/RtxProvision.cmake")

# CMake includes the toolchain file again for each language/system detection
# pass; report the decision only the first time.
if(_rtx_toolchain_reported)
    return()
endif()
set(_rtx_toolchain_reported TRUE)

# Locates zig.exe in the portable layout the bootstrap produces.  Avoid a full
# recursive glob: the extracted Zig tree is large and the known layouts cover
# both current provisioning and older runs.
function(_rtx_zig_find_bootstrap out_var)
    foreach(_candidate
            "${_rtx_zig_bootstrap_dir}/zig.exe"
            "${_rtx_zig_bootstrap_dir}/zig-windows-x86_64-${_rtx_zig_version}/zig.exe")
        if(EXISTS "${_candidate}")
            set(${out_var} "${_candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    if(EXISTS "${_rtx_zig_bootstrap_dir}")
        file(GLOB _zig_executables "${_rtx_zig_bootstrap_dir}/*/zig.exe")
        if(_zig_executables)
            list(GET _zig_executables 0 _zig_executable)
            set(${out_var} "${_zig_executable}" PARENT_SCOPE)
        endif()
    endif()
endfunction()

# 1. Explicit Zig request.  A cached path that has since disappeared (Zig
#    moved or uninstalled) falls through instead of failing the configure.
if(RTX_ZIG_EXECUTABLE AND NOT EXISTS "${RTX_ZIG_EXECUTABLE}")
    message(STATUS
        "[CineRTX-Toolchain] Ignoring RTX_ZIG_EXECUTABLE='${RTX_ZIG_EXECUTABLE}' "
        "(the path no longer exists).")
    unset(RTX_ZIG_EXECUTABLE)
    unset(RTX_ZIG_EXECUTABLE CACHE)
endif()
if(NOT RTX_ZIG_EXECUTABLE AND DEFINED ENV{RTX_ZIG})
    if(EXISTS "$ENV{RTX_ZIG}")
        set(RTX_ZIG_EXECUTABLE "$ENV{RTX_ZIG}")
    else()
        message(WARNING
            "RTX_ZIG is set to '$ENV{RTX_ZIG}', which does not exist; ignoring it.")
    endif()
endif()

# 2. Host-native compiler.  CMake's default detection is left in charge; this
#    probe only decides whether the portable Zig kit is needed at all.
if(NOT RTX_ZIG_EXECUTABLE)
    find_program(_rtx_native_compiler NAMES cl clang++ clang g++ gcc)
    if(NOT _rtx_native_compiler AND DEFINED ENV{VCToolsInstallDir})
        # Visual Studio's CMake integration always defines VCToolsInstallDir
        # and has already applied the developer environment, so cl.exe is
        # usable even when PATH itself was trimmed.
        file(TO_CMAKE_PATH "$ENV{VCToolsInstallDir}" _rtx_vc_tools)
        file(GLOB _rtx_cl_candidates "${_rtx_vc_tools}/bin/Host*/x64/cl.exe")
        if(_rtx_cl_candidates)
            list(GET _rtx_cl_candidates 0 _rtx_native_compiler)
            set(CMAKE_CXX_COMPILER "${_rtx_native_compiler}")
        endif()
    endif()
    if(_rtx_native_compiler)
        message(STATUS
            "[CineRTX-Toolchain] Host compiler '${_rtx_native_compiler}' found; "
            "building with the native toolchain (portable Zig not needed).")
        return()
    endif()
endif()

# 3. Portable Zig: the kit from an earlier run first, then PATH.
if(NOT RTX_ZIG_EXECUTABLE)
    _rtx_zig_find_bootstrap(_rtx_bootstrapped_zig)
    if(_rtx_bootstrapped_zig)
        set(RTX_ZIG_EXECUTABLE "${_rtx_bootstrapped_zig}")
    endif()
endif()

if(NOT RTX_ZIG_EXECUTABLE)
    find_program(RTX_ZIG_EXECUTABLE NAMES zig)
endif()

# 4. Bare host: provision the pinned kit under build/external.
if(NOT RTX_ZIG_EXECUTABLE AND CMAKE_HOST_WIN32)
    message(STATUS
        "[CineRTX-Toolchain] No C++ compiler found; downloading portable Zig "
        "${_rtx_zig_version} (one-time, SHA-256 pinned) ...")
    rtx_provision_tool(zig
        "${_rtx_zig_url}"
        "${_rtx_zig_sha256}"
        zig.exe
        "${_rtx_repo_root}/build/external"
        _rtx_provisioned_zig)
    if(_rtx_provisioned_zig)
        set(RTX_ZIG_EXECUTABLE "${_rtx_provisioned_zig}")
    endif()
endif()

# 5. Nothing usable on this host.
if(NOT RTX_ZIG_EXECUTABLE OR NOT EXISTS "${RTX_ZIG_EXECUTABLE}")
    message(FATAL_ERROR
        "No C++ compiler was found and the portable Zig fallback is unavailable.\n"
        "  - Install MSVC, Clang or GCC and configure again, or\n"
        "  - pass -DRTX_ZIG_EXECUTABLE=/path/to/zig or set the RTX_ZIG "
        "environment variable, or\n"
        "  - set HTTPS_PROXY/HTTP_PROXY and configure again so curl can fetch "
        "${_rtx_zig_url}")
endif()

get_filename_component(RTX_ZIG_EXECUTABLE "${RTX_ZIG_EXECUTABLE}" ABSOLUTE)
file(TO_CMAKE_PATH "${RTX_ZIG_EXECUTABLE}" RTX_ZIG_EXECUTABLE)
set(RTX_ZIG_EXECUTABLE "${RTX_ZIG_EXECUTABLE}"
    CACHE FILEPATH "Zig executable" FORCE)

# Zig needs writable cache and temp directories even on a host without
# LOCALAPPDATA/TEMP; keep them inside the repository.
if(NOT DEFINED ENV{ZIG_GLOBAL_CACHE_DIR})
    file(MAKE_DIRECTORY "${_rtx_repo_root}/build/external/zig-cache")
    set(ENV{ZIG_GLOBAL_CACHE_DIR} "${_rtx_repo_root}/build/external/zig-cache")
endif()
if(NOT DEFINED ENV{ZIG_LOCAL_CACHE_DIR})
    set(ENV{ZIG_LOCAL_CACHE_DIR} "$ENV{ZIG_GLOBAL_CACHE_DIR}")
endif()
if(CMAKE_HOST_WIN32 AND NOT DEFINED ENV{TEMP})
    file(MAKE_DIRECTORY "${_rtx_repo_root}/build/external/tmp")
    set(ENV{TEMP} "${_rtx_repo_root}/build/external/tmp")
endif()
if(CMAKE_HOST_WIN32 AND NOT DEFINED ENV{TMP})
    set(ENV{TMP} "$ENV{TEMP}")
endif()

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
