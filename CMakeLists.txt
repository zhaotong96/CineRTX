# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors

cmake_minimum_required(VERSION 3.25)

# Runs before project() so the Ninja generator is available even on a host
# that only has CMake.  The compiler fallback lives in the toolchain file.
include(cmake/RtxBootstrap.cmake)

project(CineRTX
    VERSION 0.2.0
    DESCRIPTION "Next-generation pure-GPU video remastering & NLE engine for NVIDIA RTX"
    LANGUAGES CXX)

if(CMAKE_HOST_WIN32 AND NOT CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_SYSTEM_PROCESSOR "AMD64" CACHE STRING "Target processor" FORCE)
endif()
if(NOT DEFINED CMAKE_INSTALL_LIBDIR)
    set(CMAKE_INSTALL_LIBDIR "lib" CACHE PATH "Object code libraries")
endif()
include(GNUInstallDirs)
include(CTest)

# The Zig toolchain identifies as Clang but its driver does not accept the
# GNU-style `ar` invocation CMake guesses.  Point the archive commands at
# `zig ar`, which understands the same `qc`/`s` operations.
if(RTX_ZIG_EXECUTABLE)
    set(CMAKE_AR "${RTX_ZIG_EXECUTABLE}" CACHE FILEPATH "Archiver" FORCE)
    set(CMAKE_CXX_ARCHIVE_CREATE
        "\"${RTX_ZIG_EXECUTABLE}\" ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_CXX_ARCHIVE_APPEND
        "\"${RTX_ZIG_EXECUTABLE}\" ar q <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_CXX_ARCHIVE_FINISH
        "\"${RTX_ZIG_EXECUTABLE}\" ar s <TARGET>")
endif()

option(RTX_BUILD_TESTS "Build the dependency-free unit and integration tests" ON)
# Windows only.  ON makes CineRTX a /SUBSYSTEM:WINDOWS image whose wWinMain runs
# the DirectComposition GUI when there are no arguments and the CLI otherwise.
# OFF keeps a plain console CineRTX (used on Linux, which has no GUI backend).
option(RTX_BUILD_GUI "Build CineRTX with the Win32 DirectComposition GUI front end" ON)
# RTX Video is not optional: every Windows build compiles the D3D12/NGX
# bridge against the RTX Video SDK and stages it under
# build/external/plugins, then mirrors it to <exe_dir>/plugins/nvidia_rtx.
# nvsdk_ngx_s.lib is proprietary, so the bridge stays a separately loaded DLL
# and is never inlined into the GPLv3 executable; the SDK-free static bridge
# exists only for non-Windows hosts (no NGX/D3D12) and for the in-process ABI
# tests.
option(RTX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(RTX_VIDEO_SDK_ROOT "" CACHE PATH
    "NVIDIA RTX Video SDK v1.1.0 root containing include/ and lib/Windows/x64/nvsdk_ngx_s.lib")

# Shared staging root for provisioned externals (Ninja/Zig kits, the RTX Video
# SDK archive) and for the evaluation NGX plugin.  Deliberately independent of
# CMAKE_BINARY_DIR so every preset finds the same cached archive and stages the
# plugin at the documented build/external/plugins/nvidia_rtx location.
set(RTX_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build/external")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Media container layer: unconditional static FFmpeg (Gate 3.0)
#
# There is no opt-in switch any more.  The supply chain module either resolves a
# surgical static archive set or fails the configure step outright; a binary
# without container I/O is never produced.
# ---------------------------------------------------------------------------
include(cmake/RtxFfmpegStatic.cmake)

# ---------------------------------------------------------------------------
# Codec ABI headers: nv-codec-headers submodule (MIT, pinned to tag n13.0.19.1)
#
# Declarations only.  No NVIDIA import library is ever linked; every entry point
# is resolved at run time with LoadLibraryExW/GetProcAddress.  The project's own
# thin loader wrapper lives in include/shim/dynlink_nvEncodeAPI.h because
# upstream n13.0.19.1 does not ship one.
# ---------------------------------------------------------------------------
set(RTX_NVCODEC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nv-codec-headers/include")
if(NOT EXISTS "${RTX_NVCODEC_INCLUDE_DIR}/ffnvcodec/nvEncodeAPI.h")
    find_package(Git QUIET)
    if(GIT_FOUND)
        message(STATUS "[CineRTX] Initializing nv-codec-headers submodule (tag n13.0.19.1)...")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive third_party/nv-codec-headers
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _nvcodec_res)
    endif()
    if(NOT EXISTS "${RTX_NVCODEC_INCLUDE_DIR}/ffnvcodec/nvEncodeAPI.h")
        message(FATAL_ERROR
            "[CineRTX] nv-codec-headers not found at ${RTX_NVCODEC_INCLUDE_DIR}.\n"
            "Run 'git submodule update --init --recursive third_party/nv-codec-headers'.")
    endif()
endif()

if(WIN32 AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/ngx_d3d12_bridge.cpp")
    message(FATAL_ERROR
        "The Windows build requires src/shim/ngx_bridge/ngx_d3d12_bridge.cpp and an "
        "installed NVIDIA RTX Video SDK (D3D12 + NGX headers). This source drop ships "
        "the validated stub bridge; restore the SDK sources before configuring.")
endif()

# ---------------------------------------------------------------------------
# Common compiler settings
# ---------------------------------------------------------------------------

add_library(rtx_project_options INTERFACE)
target_compile_features(rtx_project_options INTERFACE cxx_std_23)
target_include_directories(rtx_project_options INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(WIN32)
    target_compile_definitions(rtx_project_options INTERFACE NOMINMAX)
endif()

if(MSVC)
    target_compile_options(rtx_project_options INTERFACE /std:c++latest /W4 /permissive- /EHsc /utf-8 /arch:AVX2)
    if(RTX_WARNINGS_AS_ERRORS)
        target_compile_options(rtx_project_options INTERFACE /WX)
    endif()
else()
    target_compile_options(rtx_project_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow)
    if(RTX_WARNINGS_AS_ERRORS)
        target_compile_options(rtx_project_options INTERFACE -Werror)
    endif()
endif()

# ---------------------------------------------------------------------------
# Core engine (GPL-3.0-or-later WITH Plugin-Exception)
# ---------------------------------------------------------------------------

set(RTX_CORE_SOURCES
    src/core/color.cpp
    src/core/geometry.cpp
    src/core/hw_transcode.cpp
    src/core/i18n.cpp
    src/core/runtime_env.cpp
    src/core/ngx_remaster.cpp
    src/core/nvidia_shim.cpp
    src/core/rational.cpp
    src/core/sei_synthesizer.cpp
    src/core/status.cpp
    src/core/vram_ring_pool.cpp
    src/io/ffmpeg_io.cpp
    src/io/memory_avio.cpp
    src/io/bitstream.cpp)

add_library(rtx_core STATIC ${RTX_CORE_SOURCES})
add_library(rtx::core ALIAS rtx_core)
# FFmpeg static archives are bound here, unconditionally.  Every executable and
# test that links rtx_core therefore inherits the media container layer through
# CMake dependency propagation: no per-target opt-in, no DLL copying.
target_link_libraries(rtx_core PUBLIC rtx_project_options rtx::ffmpeg_static)
target_include_directories(rtx_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
set_target_properties(rtx_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)



if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(rtx_core PUBLIC Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# NVIDIA shim and NGX bridge (MIT OR Apache-2.0)
# ---------------------------------------------------------------------------

set(RTX_SHIM_SOURCES
    src/shim/loader/dyn_library.cpp
    src/shim/loader/nv_backend_shim.cpp
    src/shim/loader/nv_cuda_context.cpp
    src/shim/loader/nv_decoder.cpp
    src/shim/loader/nv_encoder.cpp
    src/shim/loader/nv_func_tables.cpp
    src/shim/loader/nv_runtime.cpp)

list(APPEND RTX_SHIM_SOURCES src/shim/loader/dyn_library_win32.cpp)

# ---------------------------------------------------------------------------
# NVIDIA shim (Zone B/C, MIT OR Apache-2.0) -- static, in-image
#
# Gate 3.0 single-EXE closure.  The shim is compiled once, into an archive that
# rtx_core links directly, so the deliverable contains no rtx_shim.dll:
# NvidiaShim::loadEmbedded() binds the identical decode/encode ABI table from
# inside the image and talks straight to the driver.  A sidecar DLL is still
# honoured at run time for plugin override / hot update, but it is neither built
# nor shipped by default.
# ---------------------------------------------------------------------------
add_library(rtx_shim_static STATIC
    ${RTX_SHIM_SOURCES}
    src/shim/loader/nv_shim_static_table.cpp)
add_library(rtx::shim_static ALIAS rtx_shim_static)
add_library(rtx::shim ALIAS rtx_shim_static)
target_link_libraries(rtx_shim_static PRIVATE rtx_project_options)
if(CMAKE_DL_LIBS)
    target_link_libraries(rtx_shim_static PRIVATE ${CMAKE_DL_LIBS})
endif()
target_compile_definitions(rtx_shim_static PUBLIC RTX_SHIM_STATIC=1)
target_include_directories(rtx_shim_static
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/loader"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/shim"
        "${RTX_NVCODEC_INCLUDE_DIR}")
set_target_properties(rtx_shim_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
target_link_libraries(rtx_core PUBLIC rtx_shim_static)
target_compile_definitions(rtx_core PUBLIC
    RTX_EMBEDDED_SHIM=1
    RTX_SHIM_STATIC=1)

# ---------------------------------------------------------------------------
# NGX bridge (Zone B/C, MIT OR Apache-2.0)
#
# Pure C ABI firewall: the GPL core never sees an NVIDIA structure, only
# include/shim/ngx_bridge_abi.h.  d3d12.dll, dxgi.dll and nvcuda.dll are all
# resolved with LoadLibraryExW, so no import-table entry is created.
#
# RTX Video Super Resolution and TrueHDR are mandatory product features, not
# build options.  On Windows the bridge links NVIDIA's proprietary
# nvsdk_ngx_s.lib, which the Plugin-Exception requires to stay inside a
# separately loaded Independent Module: the build produces a DLL staged outside
# the product tree and never inlines a proprietary symbol into the GPLv3
# executable.  Non-Windows hosts have no NGX/D3D12, so they compile the static
# SDK-free bridge into the image; NgxRemasterBridge then reports RTX_ERR_NGX for
# VSR/TrueHDR rather than pretending a texture exists.
# The SDK-free archive is built on every platform because the test suite
# exercises the C ABI in-process.
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# RGBA16F -> P010 pack kernel (ACT-01)
#
# Generated into ${CMAKE_BINARY_DIR}/generated/p010_pack_ptx.h from p010_pack.ptx
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(RTX_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${RTX_GENERATED_DIR}")
set(RTX_PTX_HEADER "${RTX_GENERATED_DIR}/p010_pack_ptx.h")

execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_ptx.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/p010_pack.ptx"
            "${RTX_PTX_HEADER}"
    RESULT_VARIABLE _rtx_embed_result)
if(NOT _rtx_embed_result EQUAL 0)
    message(FATAL_ERROR "p010_pack PTX embedding failed (${_rtx_embed_result})")
endif()

add_custom_command(
    OUTPUT "${RTX_PTX_HEADER}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_ptx.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/p010_pack.ptx"
            "${RTX_PTX_HEADER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/p010_pack.ptx"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_ptx.py"
    COMMENT "Generating p010_pack_ptx.h in build tree"
    VERBATIM)

set(RTX_NGX_BRIDGE_SOURCES
    src/shim/ngx_bridge/bridge_exports.cpp
    src/shim/ngx_bridge/p010_pack_runtime.cpp
    src/shim/ngx_bridge/d3d12_interop.cpp
    "${RTX_PTX_HEADER}")

if(WIN32)
    add_library(rtx_ngx_bridge SHARED
        ${RTX_NGX_BRIDGE_SOURCES}
        src/shim/ngx_bridge/ngx_d3d12_bridge.cpp)
    target_link_libraries(rtx_ngx_bridge PRIVATE rtx_project_options)
    target_compile_definitions(rtx_ngx_bridge PRIVATE RTX_SHIM_EXPORTS=1)
    target_include_directories(rtx_ngx_bridge PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/shim"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge"
        "${RTX_NVCODEC_INCLUDE_DIR}"
        "${RTX_GENERATED_DIR}")
    set_target_properties(rtx_ngx_bridge PROPERTIES
        OUTPUT_NAME rtx_ngx_bridge
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    set_target_properties(rtx_ngx_bridge PROPERTIES PREFIX "")
    if(MSVC)
        # nvsdk_ngx_s.lib is compiled by NVIDIA as a /MT static library.
        # Setting MSVC_RUNTIME_LIBRARY to MultiThreaded aligns the CRT and prevents LNK2038.
        set_property(TARGET rtx_ngx_bridge PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_link_options(rtx_ngx_bridge PRIVATE "/INCREMENTAL:NO")
        set_target_properties(rtx_ngx_bridge PROPERTIES
            PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols"
            COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols")
    endif()
    # Canonical staging outside the product tree; CineRTX.exe mirrors this
    # directory into <exe_dir>/plugins/nvidia_rtx at build time.
    set_target_properties(rtx_ngx_bridge PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${RTX_EXTERNAL_DIR}/plugins/nvidia_rtx"
        LIBRARY_OUTPUT_DIRECTORY "${RTX_EXTERNAL_DIR}/plugins/nvidia_rtx")
endif()

add_library(rtx_ngx_bridge_static STATIC ${RTX_NGX_BRIDGE_SOURCES})
target_link_libraries(rtx_ngx_bridge_static PRIVATE rtx_project_options)
target_compile_definitions(rtx_ngx_bridge_static PUBLIC RTX_SHIM_STATIC=1)
target_include_directories(rtx_ngx_bridge_static PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/shim"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge"
    "${RTX_NVCODEC_INCLUDE_DIR}"
    "${RTX_GENERATED_DIR}")
set_target_properties(rtx_ngx_bridge_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(WIN32)
    add_library(rtx::ngx_bridge ALIAS rtx_ngx_bridge)
else()
    add_library(rtx::ngx_bridge ALIAS rtx_ngx_bridge_static)
    target_link_libraries(rtx_core PUBLIC rtx_ngx_bridge_static)
    target_compile_definitions(rtx_core PUBLIC RTX_EMBEDDED_NGX_BRIDGE=1)
endif()

if(WIN32)
    set(RTX_NGX_ROOT_DIR "")

    # 1. User-supplied root parameter or environment variable
    if(RTX_VIDEO_SDK_ROOT AND EXISTS "${RTX_VIDEO_SDK_ROOT}/include/nvsdk_ngx.h")
        set(RTX_NGX_ROOT_DIR "${RTX_VIDEO_SDK_ROOT}")
    elseif(DEFINED ENV{RTX_VIDEO_SDK_ROOT} AND EXISTS "$ENV{RTX_VIDEO_SDK_ROOT}/include/nvsdk_ngx.h")
        set(RTX_NGX_ROOT_DIR "$ENV{RTX_VIDEO_SDK_ROOT}")
    endif()

    # 2. Automated supply: reuse the shared cache under build/external (the
    #    same root as the provisioned tools), then download from NVIDIA's CDN.
    #    The archive is SHA-256 pinned and curl is tried first for proxies.
    if(NOT RTX_NGX_ROOT_DIR)
        set(_rtx_sdk_cache "${RTX_EXTERNAL_DIR}")
        set(_rtx_sdk_build_dir "${_rtx_sdk_cache}/rtx_video_sdk")
        set(_rtx_sdk_zip "${_rtx_sdk_cache}/RTX_Video_SDK_v1.1.0.zip")
        set(_rtx_sdk_url "https://developer.download.nvidia.com/rtx/sdk/RTX_Video_SDK_v1.1.0.zip")

        set(_rtx_sdk_sha256 "abf4f34e2b5a618e355b0d5a0365d8ecc3db4396e756e4c850a867e1ae2ed69e")

        if(NOT EXISTS "${_rtx_sdk_build_dir}/include/nvsdk_ngx.h")
            include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RtxProvision.cmake")
            rtx_fetch_archive("${_rtx_sdk_url}" "${_rtx_sdk_sha256}" "${_rtx_sdk_zip}" _rtx_sdk_ready)
            if(NOT _rtx_sdk_ready)
                message(FATAL_ERROR
                    "Failed to obtain NVIDIA RTX Video SDK. Place a valid archive at "
                    "${_rtx_sdk_zip}, set -DRTX_VIDEO_SDK_ROOT=<path>, or set "
                    "HTTPS_PROXY/HTTP_PROXY so curl can reach ${_rtx_sdk_url}.")
            endif()

            message(STATUS "Extracting RTX Video SDK into ${_rtx_sdk_build_dir} ...")
            file(MAKE_DIRECTORY "${_rtx_sdk_build_dir}")
            file(ARCHIVE_EXTRACT
                INPUT "${_rtx_sdk_zip}"
                DESTINATION "${_rtx_sdk_build_dir}")
        endif()

        if(EXISTS "${_rtx_sdk_build_dir}/include/nvsdk_ngx.h")
            set(RTX_NGX_ROOT_DIR "${_rtx_sdk_build_dir}")
        endif()
    endif()

    set(RTX_NGX_INCLUDE_DIR "")
    set(RTX_NGX_LIBRARY "")
    if(EXISTS "${RTX_NGX_ROOT_DIR}/include/nvsdk_ngx.h")
        set(RTX_NGX_INCLUDE_DIR "${RTX_NGX_ROOT_DIR}/include")
        if(EXISTS "${RTX_NGX_ROOT_DIR}/lib/Windows/x64/nvsdk_ngx_s.lib")
            set(RTX_NGX_LIBRARY "${RTX_NGX_ROOT_DIR}/lib/Windows/x64/nvsdk_ngx_s.lib")
        elseif(EXISTS "${RTX_NGX_ROOT_DIR}/lib/x64/nvsdk_ngx_s.lib")
            set(RTX_NGX_LIBRARY "${RTX_NGX_ROOT_DIR}/lib/x64/nvsdk_ngx_s.lib")
        endif()
    endif()

    if(NOT RTX_NGX_INCLUDE_DIR OR NOT RTX_NGX_LIBRARY)
        message(FATAL_ERROR
            "NVIDIA RTX Video SDK could not be configured; VSR and TrueHDR are "
            "mandatory, so the build cannot continue. Check the network connection "
            "or set -DRTX_VIDEO_SDK_ROOT=<path>.")
    endif()

    message(STATUS
        "RTX Video SDK: ${RTX_NGX_INCLUDE_DIR} (static stub ${RTX_NGX_LIBRARY})")

    # Locate runtime DLLs (nvngx_vsr.dll, nvngx_truehdr.dll) for automatic post-build deployment
    set(RTX_NGX_VSR_DLL "")
    set(RTX_NGX_TRUEHDR_DLL "")
    foreach(_bin_sub "bin/Windows/x64/rel" "bin/Windows/x64/dev" "bin/x64/rel" "bin/x64/dev" "bin/x64" "bin")
        if(EXISTS "${RTX_NGX_ROOT_DIR}/${_bin_sub}/nvngx_vsr.dll")
            set(RTX_NGX_VSR_DLL "${RTX_NGX_ROOT_DIR}/${_bin_sub}/nvngx_vsr.dll")
            set(RTX_NGX_TRUEHDR_DLL "${RTX_NGX_ROOT_DIR}/${_bin_sub}/nvngx_truehdr.dll")
            break()
        endif()
    endforeach()
    if(NOT RTX_NGX_VSR_DLL OR NOT EXISTS "${RTX_NGX_VSR_DLL}" OR
       NOT RTX_NGX_TRUEHDR_DLL OR NOT EXISTS "${RTX_NGX_TRUEHDR_DLL}")
        message(FATAL_ERROR
            "RTX Video SDK at ${RTX_NGX_ROOT_DIR} has no runtime feature DLLs. "
            "Expected bin/Windows/x64/rel/nvngx_vsr.dll and nvngx_truehdr.dll; "
            "VSR and TrueHDR are mandatory, so the build cannot continue. "
            "Delete build/external/rtx_video_sdk and reconfigure to re-extract "
            "the pinned archive, or point RTX_VIDEO_SDK_ROOT at a complete SDK.")
    endif()

    if(NOT MSVC)
        target_sources(rtx_ngx_bridge PRIVATE
            src/shim/ngx_bridge/ngx_msvc_crt_compat.cpp)
    endif()
    target_include_directories(rtx_ngx_bridge PRIVATE "${RTX_NGX_INCLUDE_DIR}")
    target_compile_definitions(rtx_ngx_bridge PRIVATE RTX_HAVE_NGX_SDK=1)
    target_link_libraries(rtx_ngx_bridge PRIVATE "${RTX_NGX_LIBRARY}")

    # The bridge is already staged in external/plugins/nvidia_rtx, so NVIDIA's
    # own runtime DLLs land beside it rather than in the product directory.
    if(RTX_NGX_VSR_DLL AND EXISTS "${RTX_NGX_VSR_DLL}")
        add_custom_command(TARGET rtx_ngx_bridge POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RTX_NGX_VSR_DLL}"
                "$<TARGET_FILE_DIR:rtx_ngx_bridge>/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RTX_NGX_TRUEHDR_DLL}"
                "$<TARGET_FILE_DIR:rtx_ngx_bridge>/"
            VERBATIM)
    endif()

    if(NOT MSVC)
        # NVIDIA's static stub is a /MT MSVC archive whose .drectve sections
        # name LIBCMT, OLDNAMES, libcpmt and uuid.  The Zig/MinGW linker has no
        # such CRT archives, so empty shadow archives satisfy the DEFAULTLIB
        # directives.  The handful of actual MSVC CRT symbols the COFF objects
        # reference are implemented in ngx_msvc_crt_compat.cpp; the vendor
        # library itself is never patched.
        set(_rtx_ngx_shadow_dir "${CMAKE_CURRENT_BINARY_DIR}/ngx_msvc_shadow_libs")
        file(MAKE_DIRECTORY "${_rtx_ngx_shadow_dir}")
        set(_rtx_ngx_shadow_archives "")
        foreach(_name LIBCMT OLDNAMES libcpmt uuid)
            set(_archive "${_rtx_ngx_shadow_dir}/lib${_name}.a")
            if(RTX_ZIG_EXECUTABLE)
                add_custom_command(OUTPUT "${_archive}"
                    COMMAND "${RTX_ZIG_EXECUTABLE}" ar qc "${_archive}"
                    VERBATIM)
            else()
                add_custom_command(OUTPUT "${_archive}"
                    COMMAND "${CMAKE_AR}" qc "${_archive}"
                    VERBATIM)
            endif()
            list(APPEND _rtx_ngx_shadow_archives "${_archive}")
        endforeach()
        add_custom_target(rtx_ngx_shadow_archives DEPENDS ${_rtx_ngx_shadow_archives})
        add_dependencies(rtx_ngx_bridge rtx_ngx_shadow_archives)
        target_link_directories(rtx_ngx_bridge PRIVATE "${_rtx_ngx_shadow_dir}")
    endif()
endif()

# ---------------------------------------------------------------------------
# CineRTX -- the single self-contained deliverable.
#
# One image, two personalities.  With no arguments it is the Win32
# DirectComposition GUI; with arguments it attaches to the parent console and
# runs the full CLI, so CTest keeps its captured stdio because
# attachParentConsole() detects a redirected pipe and skips AttachConsole().
#
# There is deliberately no rtx_copy_shim_runtime step here: rtx_shim is a
# static archive that rtx_core already links.  The proprietary NGX bridge is a
# plugin DLL: canonical staging lives at build/external/plugins/nvidia_rtx and
# is mirrored to <exe_dir>/plugins/nvidia_rtx so CineRTX.exe finds it without
# environment variables; it is never linked into the executable.  On
# non-Windows hosts the SDK-free bridge is inlined instead.
# ---------------------------------------------------------------------------
if(WIN32 AND RTX_BUILD_GUI)
    set(RTX_CINERTX_SOURCES
        src/gui/rtx_gui.cpp
        src/cli/main.cpp
        src/cli/shim_probe.cpp)
else()
    set(RTX_CINERTX_SOURCES
        src/cli/main.cpp
        src/cli/main_entry.cpp
        src/cli/shim_probe.cpp)
endif()

add_executable(CineRTX ${RTX_CINERTX_SOURCES})
add_executable(rtx::cinertx ALIAS CineRTX)
target_link_libraries(CineRTX PRIVATE rtx_core)
if(WIN32)
    # CommandLineToArgvW: wWinMain rebuilds argv as UTF-8 so non-ASCII paths work.
    target_link_libraries(CineRTX PRIVATE shell32)
endif()
if(WIN32 AND RTX_BUILD_GUI)
    target_link_libraries(CineRTX PRIVATE dwmapi gdi32 user32 comdlg32 d3d12 dxgi)
endif()
if(CMAKE_DL_LIBS)
    target_link_libraries(CineRTX PRIVATE ${CMAKE_DL_LIBS})
endif()
target_include_directories(CineRTX PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/shim"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gui")
target_compile_definitions(CineRTX PRIVATE
    RTX_VERSION_STRING="${PROJECT_VERSION}")
if(WIN32 AND RTX_BUILD_GUI)
    if(MSVC)
        target_compile_options(CineRTX PRIVATE /O1 /Gy /Gs-)
        target_link_options(CineRTX PRIVATE
            /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /OPT:REF /OPT:ICF /INCREMENTAL:NO)
    else()
        target_compile_options(CineRTX PRIVATE
            -Os -ffunction-sections -fdata-sections)
        target_link_options(CineRTX PRIVATE
            -municode
            -Wl,--subsystem,windows
            -static -static-libgcc -static-libstdc++
            -Wl,--gc-sections -Wl,--strip-all)
    endif()
endif()
set_target_properties(CineRTX PROPERTIES
    OUTPUT_NAME "CineRTX"
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols")

if(WIN32)
    # The bridge must stay a separately loaded Independent Module, but the
    # product has to find it without environment variables.  Mirror the
    # canonical staging into <exe_dir>/plugins/nvidia_rtx, which is exactly the
    # install layout and the loader's first relative candidate.
    add_dependencies(CineRTX rtx_ngx_bridge)
    set(_rtx_ngx_stage_files "$<TARGET_FILE:rtx_ngx_bridge>")
    if(RTX_NGX_VSR_DLL AND EXISTS "${RTX_NGX_VSR_DLL}")
        list(APPEND _rtx_ngx_stage_files "${RTX_NGX_VSR_DLL}" "${RTX_NGX_TRUEHDR_DLL}")
    endif()
    add_custom_command(TARGET CineRTX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:CineRTX>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_rtx_ngx_stage_files}
            "$<TARGET_FILE_DIR:CineRTX>/"
        VERBATIM)
    # POST_BUILD only fires when CineRTX relinks, so a copied or incremental
    # build tree with an up-to-date exe can keep an empty plugin directory and
    # report the plugin as unavailable forever.  Re-stage on every build; the
    # copies are no-ops when the layout is already correct, and the check turns
    # a silently broken deployment into a build failure.  The destination is
    # computed here rather than with TARGET_FILE_DIR:CineRTX: that expression
    # creates an implicit dependency on CineRTX, which would form a cycle with
    # CineRTX's dependency on this target.
    get_target_property(_rtx_ngx_runtime_dir CineRTX RUNTIME_OUTPUT_DIRECTORY)
    if(NOT _rtx_ngx_runtime_dir)
        if(CMAKE_CONFIGURATION_TYPES)
            set(_rtx_ngx_runtime_dir "${CMAKE_BINARY_DIR}/$<CONFIG>")
        else()
            set(_rtx_ngx_runtime_dir "${CMAKE_BINARY_DIR}")
        endif()
    endif()
    set(_rtx_ngx_product_plugin_dir "${_rtx_ngx_runtime_dir}")
    add_custom_target(rtx_stage_ngx_plugin ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_rtx_ngx_product_plugin_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_rtx_ngx_stage_files}
            "${_rtx_ngx_product_plugin_dir}/"
        COMMAND ${CMAKE_COMMAND}
            "-DPLUGIN_DIR=${_rtx_ngx_product_plugin_dir}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyNgxStaging.cmake"
        DEPENDS rtx_ngx_bridge
        COMMENT "Staging the RTX NGX plugin beside CineRTX.exe"
        VERBATIM)
    # A phony target is always dirty, so every CineRTX build re-runs the stage
    # step even when the executable itself is up to date.
    add_dependencies(CineRTX rtx_stage_ngx_plugin)

    # -----------------------------------------------------------------------
    # NVIDIA SDK runtimes (Optical Flow + TensorRT)
    #
    # These are *not* plugins: the shim opens NvOFFRUC.dll and nvinfer_10.dll by
    # explicit path in the executable directory and both have system_fallback
    # disabled, so they belong beside CineRTX.exe.  cudart64_110.dll is a hard
    # dependency of the FRUC DLL (loading it alone fails with error 126), and
    # nvonnxparser_10.dll is what trt_dynamic.cpp loads for the ONNX parser, so
    # all of them ship together.  The destination is resolved from the CineRTX
    # property instead of TARGET_FILE_DIR:CineRTX for the same reason as above:
    # a generator expression would add a dependency edge back to CineRTX and
    # form a cycle with the dependency below.
    # -----------------------------------------------------------------------
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(RTX_BUILD_TESTS)
    set(RTX_TEST_SOURCES
        tests/test_color.cpp
        tests/test_concurrency.cpp
        tests/test_flicks_timebase.cpp
        tests/test_hdr.cpp
        tests/test_i18n.cpp
        tests/test_io.cpp
        tests/unit/test_audio_stream_copy.cpp
        tests/test_main.cpp
        tests/test_rational.cpp
        tests/test_shim.cpp
        tests/test_status.cpp
        tests/test_types.cpp
        tests/test_vram.cpp)

    add_executable(rtx_tests ${RTX_TEST_SOURCES}
        src/cli/shim_probe.cpp)
    target_link_libraries(rtx_tests PRIVATE rtx_core rtx_project_options)
    if(WIN32)
        target_link_libraries(rtx_tests PRIVATE rtx_ngx_bridge_static)
    endif()
    if(CMAKE_DL_LIBS)
        target_link_libraries(rtx_tests PRIVATE ${CMAKE_DL_LIBS})
    endif()
    target_include_directories(rtx_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/core"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/io"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/loader"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/shim")
    if(MSVC)
        target_link_options(rtx_tests PRIVATE "/INCREMENTAL:NO")
    endif()
    set_target_properties(rtx_tests PROPERTIES
        CXX_STANDARD 23
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols"
        COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols")

    # Per-test timeouts: a hung spin loop or blocked dynamic load must fail with
    # the test name in the log instead of consuming the whole CI job (CTest's
    # default is 1500 s per test).
    add_test(NAME rtx_unit_tests COMMAND rtx_tests)
    set_tests_properties(rtx_unit_tests PROPERTIES TIMEOUT 300)

    # Gate 6 (W9): DirectComposition viewport throttle / occlusion / self-heal.
    add_test(NAME cli_gui_viewport_smoke_test COMMAND CineRTX --viewport-self-test)
    set_tests_properties(cli_gui_viewport_smoke_test PROPERTIES
        PASS_REGULAR_EXPRESSION "viewport self-test PASS"
        TIMEOUT 60)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(TARGETS CineRTX RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(WIN32)
    install(TARGETS rtx_ngx_bridge RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
if(RTX_NGX_VSR_DLL AND EXISTS "${RTX_NGX_VSR_DLL}")
    install(FILES "${RTX_NGX_VSR_DLL}" "${RTX_NGX_TRUEHDR_DLL}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/plugins/nvidia_rtx")
endif()
install(DIRECTORY include/rtx DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h")
install(DIRECTORY include/shim DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h")
install(FILES LICENSE LICENSE-MIT LICENSE-APACHE COPYING.EXCEPTION
    DESTINATION "${CMAKE_INSTALL_DATADIR}/cinertx")
