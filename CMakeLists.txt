# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors

cmake_minimum_required(VERSION 3.25)

project(CineRTX
    VERSION 0.1.0
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
option(RTX_WITH_FFMPEG "Enable the real libavformat/libavcodec demuxer and muxer" ON)
if(WIN32)
    option(RTX_WITH_NGX_SDK "Compile the D3D12/NGX bridge against the NVIDIA SDK" ON)
else()
    option(RTX_WITH_NGX_SDK "Compile the D3D12/NGX bridge against the NVIDIA SDK" OFF)
endif()
option(RTX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(RTX_FFMPEG_ROOT "" CACHE PATH
    "FFmpeg development prefix containing include/ and lib/ (or set RTX_FFMPEG_ROOT)")
set(RTX_VIDEO_SDK_ROOT "" CACHE PATH
    "NVIDIA RTX Video SDK v1.1.0 root containing include/ and lib/Windows/x64/nvsdk_ngx_s.lib")
set(RTX_FFMPEG_DLL_DIR "" CACHE PATH
    "Directory containing the FFmpeg runtime DLLs; copied beside test/CLI binaries")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

if(RTX_WITH_NGX_SDK AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/ngx_d3d12_bridge.cpp")
    message(FATAL_ERROR
        "RTX_WITH_NGX_SDK=ON requires src/shim/ngx_bridge/ngx_d3d12_bridge.cpp and an "
        "installed NVIDIA RTX Video SDK (D3D12 + NGX headers). This source drop ships "
        "the validated stub bridge; rebuild with the SDK sources present.")
endif()

if(RTX_WITH_FFMPEG)
    set(_rtx_ffmpeg_roots "")
    if(RTX_FFMPEG_ROOT)
        list(APPEND _rtx_ffmpeg_roots "${RTX_FFMPEG_ROOT}")
    endif()
    if(DEFINED ENV{RTX_FFMPEG_ROOT} AND NOT "$ENV{RTX_FFMPEG_ROOT}" STREQUAL "")
        list(APPEND _rtx_ffmpeg_roots "$ENV{RTX_FFMPEG_ROOT}")
    endif()
    list(APPEND _rtx_ffmpeg_roots
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ffmpeg"
        "C:/ffmpeg"
        "/usr/local"
        "/usr")

    set(RTX_FFMPEG_INCLUDE_DIR "")
    set(RTX_FFMPEG_LIBRARY_DIR "")
    foreach(_root IN LISTS _rtx_ffmpeg_roots)
        if(_root AND EXISTS "${_root}/include/libavformat/avformat.h" AND
           EXISTS "${_root}/lib")
            set(RTX_FFMPEG_INCLUDE_DIR "${_root}/include")
            set(RTX_FFMPEG_LIBRARY_DIR "${_root}/lib")
            break()
        endif()
    endforeach()

    if(RTX_FFMPEG_INCLUDE_DIR)
        set(_rtx_ffmpeg_libraries "")
        set(_rtx_ffmpeg_missing "")
        foreach(_component avformat avcodec avutil swscale)
            find_library(RTX_FFMPEG_${_component}_LIBRARY
                NAMES ${_component} ${_component}-62 ${_component}-61 ${_component}-60 ${_component}-9
                PATHS "${RTX_FFMPEG_LIBRARY_DIR}"
                NO_DEFAULT_PATH)
            if(RTX_FFMPEG_${_component}_LIBRARY)
                list(APPEND _rtx_ffmpeg_libraries "${RTX_FFMPEG_${_component}_LIBRARY}")
            elseif(NOT _component STREQUAL "swscale")
                # The engine never calls swscale; older shared builds link it
                # anyway, while the trimmed static container layer omits it.
                list(APPEND _rtx_ffmpeg_missing ${_component})
            endif()
        endforeach()
    endif()

    if(RTX_FFMPEG_INCLUDE_DIR AND NOT _rtx_ffmpeg_missing)
        set(RTX_HAVE_FFMPEG ON)
        message(STATUS "FFmpeg: ${RTX_FFMPEG_INCLUDE_DIR} (${_rtx_ffmpeg_libraries})")
    else()
        set(RTX_HAVE_FFMPEG OFF)
        if(RTX_FFMPEG_INCLUDE_DIR)
            message(WARNING
                "FFmpeg headers found but libraries are missing: ${_rtx_ffmpeg_missing}. "
                "The structured FfmpegMissing path will be compiled instead.")
        else()
            message(STATUS
                "FFmpeg development files not found. The structured FfmpegMissing path "
                "will be compiled; pass -DRTX_FFMPEG_ROOT=<prefix> to enable real I/O.")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Common compiler settings
# ---------------------------------------------------------------------------

add_library(rtx_project_options INTERFACE)
target_compile_features(rtx_project_options INTERFACE cxx_std_20)
target_include_directories(rtx_project_options INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(WIN32)
    target_compile_definitions(rtx_project_options INTERFACE NOMINMAX)
endif()

if(MSVC)
    target_compile_options(rtx_project_options INTERFACE /W4 /permissive- /EHsc /utf-8)
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
    src/core/cpu_backend.cpp
    src/core/hw_transcode.cpp
    src/core/ngx_remaster.cpp
    src/core/nvidia_shim.cpp
    src/core/pipeline.cpp
    src/core/pipeline_scheduler.cpp
    src/core/rational.cpp
    src/core/scene_detect.cpp
    src/core/sei_synthesizer.cpp
    src/core/status.cpp
    src/core/types.cpp
    src/core/vram_ring_pool.cpp
    src/io/ffmpeg_io.cpp
    src/io/bitstream.cpp
    src/io/raw_io.cpp)

add_library(rtx_core STATIC ${RTX_CORE_SOURCES})
add_library(rtx::core ALIAS rtx_core)
target_link_libraries(rtx_core PUBLIC rtx_project_options)
target_include_directories(rtx_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
set_target_properties(rtx_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(rtx_core PUBLIC Threads::Threads)
endif()

if(RTX_HAVE_FFMPEG)
    target_include_directories(rtx_core PUBLIC "${RTX_FFMPEG_INCLUDE_DIR}")
    target_link_libraries(rtx_core PUBLIC ${_rtx_ffmpeg_libraries})
    target_compile_definitions(rtx_core PUBLIC RTX_HAVE_FFMPEG=1)

    # Import libraries only work when the matching DLLs are reachable.  Copy
    # them beside every executable that links rtx_core for local runs.  The
    # FFmpeg DLLs depend on each other (avcodec pulls swresample, for example),
    # so the whole runtime directory is copied rather than a fixed shortlist.
    function(rtx_copy_ffmpeg_runtime target)
        if(RTX_FFMPEG_DLL_DIR AND EXISTS "${RTX_FFMPEG_DLL_DIR}")
            file(GLOB _rtx_ffmpeg_dlls "${RTX_FFMPEG_DLL_DIR}/*.dll")
            foreach(_dll IN LISTS _rtx_ffmpeg_dlls)
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_dll}" "$<TARGET_FILE_DIR:${target}>"
                    VERBATIM)
            endforeach()
        endif()
    endfunction()
endif()

# RIFE/TensorRT configuration and tensor marshalling.  This target is
# MIT/Apache-2.0 and never includes a TensorRT header.
add_library(rtx_rife_support STATIC src/shim/backends/trt_rife.cpp)
add_library(rtx::rife_support ALIAS rtx_rife_support)
target_link_libraries(rtx_rife_support PUBLIC rtx_core)
set_target_properties(rtx_rife_support PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

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

if(WIN32)
    list(APPEND RTX_SHIM_SOURCES src/shim/loader/dyn_library_win32.cpp)
else()
    list(APPEND RTX_SHIM_SOURCES src/shim/loader/dyn_library_posix.cpp)
endif()

add_library(rtx_shim SHARED ${RTX_SHIM_SOURCES})
add_library(rtx::shim ALIAS rtx_shim)
target_link_libraries(rtx_shim PRIVATE rtx_project_options)
if(CMAKE_DL_LIBS)
    target_link_libraries(rtx_shim PRIVATE ${CMAKE_DL_LIBS})
endif()
target_compile_definitions(rtx_shim PRIVATE RTX_SHIM_EXPORTS=1)
target_include_directories(rtx_shim
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/loader"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/shim"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/open_headers")
set_target_properties(rtx_shim PROPERTIES
    OUTPUT_NAME rtx_shim
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(WIN32)
    # Windows has no "lib" prefix; POSIX keeps the default so the runtime
    # loader finds librtx_shim.so / librtx_shim.dylib.
    set_target_properties(rtx_shim PROPERTIES PREFIX "")
endif()

add_library(rtx_ngx_bridge SHARED
    src/shim/ngx_bridge/bridge_exports.cpp
    src/shim/ngx_bridge/p010_pack_runtime.cpp
    src/shim/ngx_bridge/d3d12_interop.cpp)
add_library(rtx::ngx_bridge ALIAS rtx_ngx_bridge)
target_link_libraries(rtx_ngx_bridge PRIVATE rtx_project_options)
target_compile_definitions(rtx_ngx_bridge PRIVATE RTX_SHIM_EXPORTS=1)
target_include_directories(rtx_ngx_bridge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/shim"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/open_headers")
set_target_properties(rtx_ngx_bridge PROPERTIES
    OUTPUT_NAME rtx_ngx_bridge
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(WIN32)
    set_target_properties(rtx_ngx_bridge PROPERTIES PREFIX "")
endif()
if(MSVC)
    # nvsdk_ngx_s.lib is compiled by NVIDIA as a /MT static library.
    # Setting MSVC_RUNTIME_LIBRARY to MultiThreaded aligns the CRT and prevents LNK2038.
    set_property(TARGET rtx_ngx_bridge PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(RTX_WITH_NGX_SDK)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/ngx_d3d12_bridge.cpp")
        message(FATAL_ERROR
            "RTX_WITH_NGX_SDK=ON requires src/shim/ngx_bridge/ngx_d3d12_bridge.cpp")
    endif()

    set(RTX_NGX_ROOT_DIR "")

    # 1. User-supplied root parameter or environment variable
    if(RTX_VIDEO_SDK_ROOT AND EXISTS "${RTX_VIDEO_SDK_ROOT}/include/nvsdk_ngx.h")
        set(RTX_NGX_ROOT_DIR "${RTX_VIDEO_SDK_ROOT}")
    elseif(DEFINED ENV{RTX_VIDEO_SDK_ROOT} AND EXISTS "$ENV{RTX_VIDEO_SDK_ROOT}/include/nvsdk_ngx.h")
        set(RTX_NGX_ROOT_DIR "$ENV{RTX_VIDEO_SDK_ROOT}")
    endif()

    # 2. Automated supply: download from NVIDIA official CDN strictly into the build tree
    if(NOT RTX_NGX_ROOT_DIR)
        set(_rtx_sdk_build_dir "${CMAKE_CURRENT_BINARY_DIR}/external/rtx_video_sdk")
        set(_rtx_sdk_zip "${CMAKE_CURRENT_BINARY_DIR}/external/RTX_Video_SDK_v1.1.0.zip")
        set(_rtx_sdk_url "https://developer.download.nvidia.com/rtx/sdk/RTX_Video_SDK_v1.1.0.zip")

        set(_rtx_sdk_sha256 "abf4f34e2b5a618e355b0d5a0365d8ecc3db4396e756e4c850a867e1ae2ed69e")

        # 1. Pre-validate cached archive hash: if hash mismatches, remove corrupted file
        if(EXISTS "${_rtx_sdk_zip}")
            file(SHA256 "${_rtx_sdk_zip}" _cached_hash)
            if(NOT _cached_hash STREQUAL _rtx_sdk_sha256)
                message(STATUS "Cached SDK archive hash mismatch; re-downloading...")
                file(REMOVE "${_rtx_sdk_zip}")
            endif()
        endif()

        if(NOT EXISTS "${_rtx_sdk_build_dir}/include/nvsdk_ngx.h")
            if(NOT EXISTS "${_rtx_sdk_zip}")
                message(STATUS "Downloading NVIDIA RTX Video SDK from official CDN into build tree...")
                file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/external")
                file(DOWNLOAD "${_rtx_sdk_url}" "${_rtx_sdk_zip}"
                    STATUS _download_status
                    SHOW_PROGRESS
                    TLS_VERIFY ON
                    INACTIVITY_TIMEOUT 15
                    EXPECTED_HASH SHA256=${_rtx_sdk_sha256})
                list(GET _download_status 0 _download_code)
                list(GET _download_status 1 _download_err)
                if(NOT _download_code EQUAL 0)
                    message(FATAL_ERROR
                        "Failed to download NVIDIA RTX Video SDK (${_download_err}). "
                        "Please verify internet connection or set -DRTX_VIDEO_SDK_ROOT=<path> manually.")
                endif()
            endif()

            message(STATUS "Extracting RTX Video SDK into build tree: ${_rtx_sdk_build_dir}...")
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
            "RTX_WITH_NGX_SDK=ON but NVIDIA RTX Video SDK could not be configured. "
            "Please check network connection or set -DRTX_VIDEO_SDK_ROOT=<path>.")
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

    target_sources(rtx_ngx_bridge PRIVATE
        src/shim/ngx_bridge/ngx_d3d12_bridge.cpp)
    if(NOT MSVC)
        target_sources(rtx_ngx_bridge PRIVATE
            src/shim/ngx_bridge/ngx_msvc_crt_compat.cpp)
    endif()
    target_include_directories(rtx_ngx_bridge PRIVATE "${RTX_NGX_INCLUDE_DIR}")
    target_compile_definitions(rtx_ngx_bridge PRIVATE RTX_HAVE_NGX_SDK=1)
    target_link_libraries(rtx_ngx_bridge PRIVATE "${RTX_NGX_LIBRARY}")

    if(RTX_NGX_VSR_DLL AND EXISTS "${RTX_NGX_VSR_DLL}")
        add_custom_command(TARGET rtx_ngx_bridge POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:rtx_ngx_bridge>/plugins/nvidia_rtx"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RTX_NGX_VSR_DLL}"
                "$<TARGET_FILE_DIR:rtx_ngx_bridge>/plugins/nvidia_rtx/"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RTX_NGX_TRUEHDR_DLL}"
                "$<TARGET_FILE_DIR:rtx_ngx_bridge>/plugins/nvidia_rtx/"
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
# RGBA16F -> P010 pack kernel (ACT-01)
#
# The kernel is authored in CUDA (p010_pack.cu) and shipped as PTX text so the
# runtime never links cudart64_*.dll.  The checked-in p010_pack_ptx.h is the
# default; set RTX_REGENERATE_PTX=ON together with RTX_NVRTC_DLL and
# RTX_CUDA_INCLUDE to rebuild it from the .cu source during configure.
# ---------------------------------------------------------------------------
option(RTX_REGENERATE_PTX "Regenerate p010_pack_ptx.h from p010_pack.cu" OFF)
set(RTX_NVRTC_DLL "" CACHE FILEPATH "Path to nvrtc64_*.dll for PTX regeneration")
set(RTX_CUDA_INCLUDE "" CACHE PATH "CUDA include directory for PTX regeneration")

set(RTX_PTX_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/p010_pack_ptx.h")
if(RTX_REGENERATE_PTX)
    if(NOT RTX_NVRTC_DLL OR NOT RTX_CUDA_INCLUDE)
        message(FATAL_ERROR
            "RTX_REGENERATE_PTX=ON requires RTX_NVRTC_DLL and RTX_CUDA_INCLUDE")
    endif()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_p010_ptx.py"
                --nvrtc "${RTX_NVRTC_DLL}" --cuda-include "${RTX_CUDA_INCLUDE}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE _rtx_ptx_result)
    if(NOT _rtx_ptx_result EQUAL 0)
        message(FATAL_ERROR "p010_pack PTX regeneration failed (${_rtx_ptx_result})")
    endif()
endif()
if(NOT EXISTS "${RTX_PTX_HEADER}")
    message(FATAL_ERROR
        "missing ${RTX_PTX_HEADER}; run tools/gen_p010_ptx.py or enable RTX_REGENERATE_PTX")
endif()

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

add_executable(rtx-transcode
    src/cli/main.cpp
    src/cli/main_entry.cpp
    src/cli/shim_probe.cpp)
add_executable(rtx::transcode ALIAS rtx-transcode)
target_link_libraries(rtx-transcode PRIVATE rtx_core rtx_rife_support)
if(CMAKE_DL_LIBS)
    target_link_libraries(rtx-transcode PRIVATE ${CMAKE_DL_LIBS})
endif()
if(WIN32)
    # CommandLineToArgvW: the CLI rebuilds argv as UTF-8 so non-ASCII paths work.
    target_link_libraries(rtx-transcode PRIVATE shell32)
endif()
target_include_directories(rtx-transcode PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/shim")
target_compile_definitions(rtx-transcode PRIVATE
    RTX_VERSION_STRING="${PROJECT_VERSION}")
set_target_properties(rtx-transcode PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(RTX_HAVE_FFMPEG)
    rtx_copy_ffmpeg_runtime(rtx-transcode)
endif()

if(WIN32)
    # The shim is loaded with LoadLibraryExW at runtime; it is deliberately
    # not a link dependency.  Copy the artefacts next to the CLI for local
    # runs and packaging, but keep the import table clean.
    function(rtx_copy_shim_runtime target)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:rtx_shim>
                $<TARGET_FILE_DIR:${target}>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:rtx_ngx_bridge>
                $<TARGET_FILE_DIR:${target}>
            VERBATIM)
        if(RTX_NGX_VSR_DLL AND EXISTS "${RTX_NGX_VSR_DLL}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${target}>/plugins/nvidia_rtx"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${RTX_NGX_VSR_DLL}"
                    "$<TARGET_FILE_DIR:${target}>/plugins/nvidia_rtx/"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${RTX_NGX_TRUEHDR_DLL}"
                    "$<TARGET_FILE_DIR:${target}>/plugins/nvidia_rtx/"
                VERBATIM)
        endif()
    endfunction()
else()
    function(rtx_copy_shim_runtime target)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:rtx_shim>
                $<TARGET_FILE_DIR:${target}>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:rtx_ngx_bridge>
                $<TARGET_FILE_DIR:${target}>
            VERBATIM)
    endfunction()
endif()

rtx_copy_shim_runtime(rtx-transcode)

# ---------------------------------------------------------------------------
# Single-exe Win32 GUI (工序 C).  The targeting is deliberately independent of
# the console CLI so CTest keeps its captured stdio; the GUI binary embeds the
# same CLI code and enters it through AttachConsole when arguments are present.
# ---------------------------------------------------------------------------
if(WIN32)
    option(RTX_BUILD_GUI "Build the Win32 single-exe GUI" ON)
endif()
if(WIN32 AND RTX_BUILD_GUI)
    add_executable(rtx-transcode-gui
        src/gui/rtx_gui.cpp
        src/cli/main.cpp
        src/cli/shim_probe.cpp)
    target_link_libraries(rtx-transcode-gui PRIVATE
        rtx_core rtx_rife_support dwmapi shell32 gdi32 user32)
    if(CMAKE_DL_LIBS)
        target_link_libraries(rtx-transcode-gui PRIVATE ${CMAKE_DL_LIBS})
    endif()
    target_include_directories(rtx-transcode-gui PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/shim")
    target_compile_definitions(rtx-transcode-gui PRIVATE
        RTX_VERSION_STRING="${PROJECT_VERSION}"
        RTX_GUI_BUILD=1)
    if(MSVC)
        target_compile_options(rtx-transcode-gui PRIVATE /O1 /Gy /Gs-)
        target_link_options(rtx-transcode-gui PRIVATE
            /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /OPT:REF /OPT:ICF)
    else()
        target_compile_options(rtx-transcode-gui PRIVATE
            -Os -ffunction-sections -fdata-sections)
        target_link_options(rtx-transcode-gui PRIVATE
            -municode
            -Wl,--subsystem,windows
            -static -static-libgcc -static-libstdc++
            -Wl,--gc-sections -Wl,--strip-all)
    endif()
    set_target_properties(rtx-transcode-gui PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    if(RTX_HAVE_FFMPEG)
        rtx_copy_ffmpeg_runtime(rtx-transcode-gui)
    endif()
    rtx_copy_shim_runtime(rtx-transcode-gui)
endif()

# Stage-1 bare transcode prototype.  It shares the validated CLI but defaults
# to the NVIDIA backend so that the acceptance command does not need an extra
# flag.  rtx-transcode --backend nvidia remains fully functional as well.
add_executable(rtx-transcode-base
    src/cli/main.cpp
    src/cli/main_entry.cpp
    src/cli/shim_probe.cpp)
target_link_libraries(rtx-transcode-base PRIVATE rtx_core rtx_rife_support)
if(CMAKE_DL_LIBS)
    target_link_libraries(rtx-transcode-base PRIVATE ${CMAKE_DL_LIBS})
endif()
if(WIN32)
    target_link_libraries(rtx-transcode-base PRIVATE shell32)
endif()
target_include_directories(rtx-transcode-base PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/shim")
target_compile_definitions(rtx-transcode-base PRIVATE
    RTX_VERSION_STRING="${PROJECT_VERSION}"
    RTX_BASE_EDITION=1)
set_target_properties(rtx-transcode-base PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(RTX_HAVE_FFMPEG)
    rtx_copy_ffmpeg_runtime(rtx-transcode-base)
endif()
rtx_copy_shim_runtime(rtx-transcode-base)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

if(RTX_BUILD_TESTS)
    set(RTX_TEST_SOURCES
        tests/test_backend.cpp
        tests/test_color.cpp
        tests/test_concurrency.cpp
        tests/test_hdr.cpp
        tests/test_io.cpp
        tests/test_main.cpp
        tests/test_pipeline.cpp
        tests/test_rational.cpp
        tests/test_rife.cpp
        tests/test_scene.cpp
        tests/test_scheduler.cpp
        tests/test_shim.cpp
        tests/test_status.cpp
        tests/test_types.cpp
        tests/test_vram.cpp)

    add_executable(rtx_tests ${RTX_TEST_SOURCES}
        src/cli/shim_probe.cpp
        src/shim/loader/dyn_library.cpp)
    if(WIN32)
        target_sources(rtx_tests PRIVATE src/shim/loader/dyn_library_win32.cpp)
    else()
        target_sources(rtx_tests PRIVATE src/shim/loader/dyn_library_posix.cpp)
    endif()
    target_link_libraries(rtx_tests PRIVATE rtx_core rtx_rife_support rtx_project_options)
    if(CMAKE_DL_LIBS)
        target_link_libraries(rtx_tests PRIVATE ${CMAKE_DL_LIBS})
    endif()
    target_include_directories(rtx_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cli"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/loader"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/shim")
    target_compile_definitions(rtx_tests PRIVATE
        RTX_TEST_SHIM_PATH="$<TARGET_FILE:rtx_shim>"
        RTX_TEST_NGX_PATH="$<TARGET_FILE:rtx_ngx_bridge>")
    add_dependencies(rtx_tests rtx_shim rtx_ngx_bridge)
    if(RTX_HAVE_FFMPEG)
        rtx_copy_ffmpeg_runtime(rtx_tests)
    endif()
    rtx_copy_shim_runtime(rtx_tests)
    set_target_properties(rtx_tests PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)

    # Per-test timeouts: a hung spin loop or blocked dynamic load must fail with
    # the test name in the log instead of consuming the whole CI job (CTest's
    # default is 1500 s per test).
    add_test(NAME rtx_unit_tests COMMAND rtx_tests)
    set_tests_properties(rtx_unit_tests PROPERTIES
        ENVIRONMENT "RTX_TEST_SHIM_PATH=$<TARGET_FILE:rtx_shim>;RTX_TEST_NGX_PATH=$<TARGET_FILE:rtx_ngx_bridge>"
        TIMEOUT 300)

    add_test(NAME cli_self_test COMMAND rtx-transcode --self-test)
    set_tests_properties(cli_self_test PROPERTIES TIMEOUT 60)
    add_test(NAME cli_simulate_json COMMAND rtx-transcode
        --simulate 4 --width 64 --height 64 --target-width 128 --target-height 128 --json)
    set_tests_properties(cli_simulate_json PROPERTIES
        PASS_REGULAR_EXPRESSION "\"interpolated\""
        TIMEOUT 60)
    add_test(NAME cli_rife_command COMMAND rtx-transcode
        --build-rife --onnx model.onnx --engine model.engine)
    set_tests_properties(cli_rife_command PROPERTIES
        PASS_REGULAR_EXPRESSION "10\\.13\\.3\\.9"
        TIMEOUT 60)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(TARGETS rtx-transcode rtx-transcode-base rtx_shim rtx_ngx_bridge
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
if(TARGET rtx-transcode-gui)
    install(TARGETS rtx-transcode-gui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
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
    DESTINATION "${CMAKE_INSTALL_DATADIR}/rtx-transcode")

