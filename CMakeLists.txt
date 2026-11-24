# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors

cmake_minimum_required(VERSION 3.25)

include(cmake/RtxBootstrap.cmake)

project(CineRTX
    VERSION 0.4.0
    DESCRIPTION "Next-generation pure-GPU video remastering & NLE engine for NVIDIA RTX"
    LANGUAGES CXX)

file(GLOB_RECURSE _RTX_JUNK_FILES
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.bak"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.orig"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.tmp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.bak"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.orig"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.tmp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.bak"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.bak"
    "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt.*"
)
if(_RTX_JUNK_FILES)
    message(FATAL_ERROR
        "[CineRTX Hygiene Gate] Backup/junk files detected in source tree:\n  ${_RTX_JUNK_FILES}\n"
        "Remove all .bak/.orig/.tmp files immediately before configuring.")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(CMAKE_HOST_WIN32 AND NOT CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_SYSTEM_PROCESSOR "AMD64" CACHE STRING "Target processor" FORCE)
endif()
if(NOT DEFINED CMAKE_INSTALL_LIBDIR)
    set(CMAKE_INSTALL_LIBDIR "lib" CACHE PATH "Object code libraries")
endif()
include(GNUInstallDirs)
include(CTest)

if(RTX_ZIG_EXECUTABLE)
    set(CMAKE_AR "${RTX_ZIG_EXECUTABLE}" CACHE FILEPATH "Archiver" FORCE)
    set(CMAKE_CXX_ARCHIVE_CREATE
        "\"${RTX_ZIG_EXECUTABLE}\" ar qc <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_CXX_ARCHIVE_APPEND
        "\"${RTX_ZIG_EXECUTABLE}\" ar q <TARGET> <LINK_FLAGS> <OBJECTS>")
    set(CMAKE_CXX_ARCHIVE_FINISH
        "\"${RTX_ZIG_EXECUTABLE}\" ar s <TARGET>")
endif()

set(RTX_VIDEO_SDK_ROOT "" CACHE PATH
    "NVIDIA RTX Video SDK v1.1.0 root containing include/ and lib/Windows/x64/nvsdk_ngx_s.lib")

set(RTX_EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.vs/build/external")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(cmake/RtxFfmpegStatic.cmake)

set(RTX_NVCODEC_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nv-codec-headers/include")
if(NOT EXISTS "${RTX_NVCODEC_INCLUDE_DIR}/ffnvcodec/nvEncodeAPI.h")
        message(FATAL_ERROR
            "[CineRTX] nv-codec-headers missing at ${RTX_NVCODEC_INCLUDE_DIR}. "
            "Run 'git submodule update --init --recursive' manually before configuring.")
    endif()

if(WIN32 AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/ngx_d3d12_bridge.cpp")
    message(FATAL_ERROR
        "The Windows build requires src/shim/ngx_bridge/ngx_d3d12_bridge.cpp and an "
        "installed NVIDIA RTX Video SDK (D3D12 + NGX headers). This source drop ships "
        "the validated stub bridge; restore the SDK sources before configuring.")
endif()

add_library(rtx_project_options INTERFACE)
target_compile_features(rtx_project_options INTERFACE cxx_std_23)
target_include_directories(rtx_project_options INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}")

if(WIN32)
    target_compile_definitions(rtx_project_options INTERFACE NOMINMAX)
endif()

if(MSVC)
    target_compile_options(rtx_project_options INTERFACE /std:c++latest /W4 /WX /permissive- /EHsc /utf-8 /arch:AVX2)
else()
    target_compile_options(rtx_project_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow
        -Wno-unknown-pragmas -Wno-unused-parameter -Wno-missing-field-initializers
        -Wno-unused-function -Wno-unused-const-variable -Wno-deprecated-declarations)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(rtx_project_options INTERFACE -fexperimental-library)
    endif()
endif()

# under include/ and src/ must carry a valid @rtx-type-justification block or
include(cmake/AuditTypes.cmake)

set(RTX_CORE_SOURCES
    src/core/geometry.cpp
    src/core/vram_scheduler.cpp
    src/core/hw_transcode.cpp
    src/core/hw_transcode_stages.cpp
    src/core/i18n.cpp
    src/core/json_scanner.cpp
    src/core/logger.cpp
    src/core/lut3d.cpp
    src/core/param_schema.cpp
    src/core/runtime_env.cpp
    src/core/ngx_remaster.cpp
    src/core/nvidia_shim.cpp
    src/core/rational.cpp
    src/core/sei_synthesizer.cpp
    src/core/status.cpp
    src/core/shot_segmenter.cpp
    src/core/job_queue.cpp
    src/core/gop_seek_scheduler.cpp
    src/core/nle_project.cpp
    src/core/render_graph.cpp
    src/core/audio_mixer_avx2.cpp
    src/core/wsola_stretch.cpp
    src/core/playback_session.cpp
    src/core/playback_session_video.cpp
    src/core/playback_session_audio.cpp
    src/io/ffmpeg_io.cpp
    src/io/memory_avio.cpp
    src/io/msvc_compat_mt.cpp
    src/io/bitstream.cpp
    src/io/cpu_video_encoder.cpp)

if(WIN32)
    list(APPEND RTX_CORE_SOURCES src/core/wasapi_audio_output.cpp)
endif()

add_library(rtx_core STATIC ${RTX_CORE_SOURCES})
add_library(rtx::core ALIAS rtx_core)
target_link_libraries(rtx_core PUBLIC rtx_project_options rtx::ffmpeg_static)
target_include_directories(rtx_core PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/io"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/core")
set_target_properties(rtx_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
if(NOT MSVC)
    target_compile_options(rtx_core PRIVATE -Wno-unused-function)
endif()

if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(rtx_core PUBLIC Threads::Threads)
endif()

set(RTX_SHIM_SOURCES
    src/shim/loader/dyn_library.cpp
    src/shim/loader/nv_backend_shim.cpp
    src/shim/loader/nv_cuda_context.cpp
    src/shim/loader/nv_decoder.cpp
    src/shim/loader/nv_encoder.cpp
    src/shim/loader/nv_func_tables.cpp
    src/shim/loader/nv_runtime.cpp)

list(APPEND RTX_SHIM_SOURCES src/shim/loader/dyn_library_win32.cpp)

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
if(NOT MSVC)
    target_compile_options(rtx_shim_static PRIVATE -Wno-unused-function)
endif()
target_include_directories(rtx_shim_static
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/loader"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/shim")
target_include_directories(rtx_shim_static
    SYSTEM PRIVATE
        "${RTX_NVCODEC_INCLUDE_DIR}")
set_target_properties(rtx_shim_static PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
target_link_libraries(rtx_core PUBLIC rtx_shim_static)
target_compile_definitions(rtx_core PUBLIC
    RTX_EMBEDDED_SHIM=1
    RTX_SHIM_STATIC=1)

set(RTX_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${RTX_GENERATED_DIR}")
set(RTX_PTX_HEADER "${RTX_GENERATED_DIR}/p010_pack_ptx.h")
set(RTX_RIFE_PTX_HEADER "${RTX_GENERATED_DIR}/rife_pack_ptx.h")

execute_process(
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_PTX=${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/p010_pack.ptx"
            "-DOUTPUT_HEADER=${RTX_PTX_HEADER}"
            "-DVAR_NAME=kP010PackPtx"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedPtx.cmake"
    RESULT_VARIABLE _rtx_embed_result)
if(NOT _rtx_embed_result EQUAL 0)
    message(FATAL_ERROR "p010_pack PTX embedding failed (${_rtx_embed_result})")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_PTX=${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/rife_pack.ptx"
            "-DOUTPUT_HEADER=${RTX_RIFE_PTX_HEADER}"
            "-DVAR_NAME=kRifePackPtx"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedPtx.cmake"
    RESULT_VARIABLE _rtx_rife_embed_result)
if(NOT _rtx_rife_embed_result EQUAL 0)
    message(FATAL_ERROR "rife_pack PTX embedding failed (${_rtx_rife_embed_result})")
endif()

add_custom_command(
    OUTPUT "${RTX_PTX_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_PTX=${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/p010_pack.ptx"
            "-DOUTPUT_HEADER=${RTX_PTX_HEADER}"
            "-DVAR_NAME=kP010PackPtx"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedPtx.cmake"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/p010_pack.ptx"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedPtx.cmake"
    COMMENT "Generating p010_pack_ptx.h in build tree (native CMake)"
    VERBATIM)

add_custom_command(
    OUTPUT "${RTX_RIFE_PTX_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_PTX=${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/rife_pack.ptx"
            "-DOUTPUT_HEADER=${RTX_RIFE_PTX_HEADER}"
            "-DVAR_NAME=kRifePackPtx"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedPtx.cmake"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge/rife_pack.ptx"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedPtx.cmake"
    COMMENT "Generating rife_pack_ptx.h in build tree (native CMake)"
    VERBATIM)

if(WIN32)
    find_program(FXC_EXECUTABLE fxc.exe HINTS
        "$ENV{WindowsSdkVerBinPath}/x64"
        "$ENV{WindowsSdkDir}/bin/x64"
    )
    if(NOT FXC_EXECUTABLE)
        file(GLOB WIN10_SDK_BINS "C:/Program Files (x86)/Windows Kits/10/bin/*/x64")
        list(SORT WIN10_SDK_BINS ORDER DESCENDING)
        find_program(FXC_EXECUTABLE fxc.exe HINTS ${WIN10_SDK_BINS})
    endif()
    if(NOT FXC_EXECUTABLE)
        message(FATAL_ERROR "fxc.exe not found! Please install Windows 10/11 SDK.")
    endif()
    message(STATUS "[CineRTX] fxc.exe: ${FXC_EXECUTABLE}")

    set(RTX_SHADER_HEADER "${RTX_GENERATED_DIR}/preview_tone_map_cs.h")
    add_custom_command(
        OUTPUT "${RTX_SHADER_HEADER}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${RTX_GENERATED_DIR}"
        COMMAND ${FXC_EXECUTABLE} /T cs_5_1 /E CSMain /Fh "${RTX_SHADER_HEADER}"
                /Vn g_preview_tone_map_cs
                "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/shaders/preview_tone_map_cs.hlsl"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/shaders/preview_tone_map_cs.hlsl"
        COMMENT "Compiling preview_tone_map_cs.hlsl -> ${RTX_SHADER_HEADER}"
        VERBATIM)
endif()

set(RTX_TENSORRT_ROOT "${RTX_EXTERNAL_DIR}/tensorrt/TensorRT-10.13.3.9" CACHE PATH "TensorRT SDK root directory")
if(NOT EXISTS "${RTX_TENSORRT_ROOT}/include/NvInfer.h")
    set(_trt_zip "${RTX_EXTERNAL_DIR}/TensorRT-10.13.3.9.Windows.win10.cuda-13.0.zip")
    set(_trt_url "https://developer.nvidia.com/downloads/compute/machine-learning/tensorrt/10.13.3/zip/TensorRT-10.13.3.9.Windows.win10.cuda-13.0.zip")
    set(_trt_sha256 "10ada15b14498e0ec8556cb19433129fd362a8e3debf75db06ccacd8659d9662")
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RtxProvision.cmake")
    rtx_fetch_archive("${_trt_url}" "${_trt_sha256}" "${_trt_zip}" _trt_ready)
    if(_trt_ready)
        message(STATUS "Extracting TensorRT archive: ${_trt_zip}")
        file(ARCHIVE_EXTRACT INPUT "${_trt_zip}" DESTINATION "${RTX_EXTERNAL_DIR}/tensorrt")
    else()
        message(STATUS "[CineRTX] TensorRT 10.13.3.9 archive missing/unverified; download skipped or gated by NVIDIA authentication. Falling back to cmake/cuda_headers.")
    endif()
    file(GLOB _trt_candidates "${RTX_EXTERNAL_DIR}/tensorrt/TensorRT-*")
    if(_trt_candidates)
        list(GET _trt_candidates 0 _first_trt)
        if(EXISTS "${_first_trt}/include/NvInfer.h")
            set(RTX_TENSORRT_ROOT "${_first_trt}")
        endif()
    endif()
endif()
set(RTX_TENSORRT_INCLUDE_DIR "${RTX_TENSORRT_ROOT}/include")
set(RTX_CUDA_HEADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cuda_headers")

set(RTX_EXTERNAL_MODELS_DIR "${RTX_EXTERNAL_DIR}/models")
file(MAKE_DIRECTORY "${RTX_EXTERNAL_MODELS_DIR}")
set(RTX_CACHED_RIFE_ONNX "${RTX_EXTERNAL_MODELS_DIR}/rife_v4.25.onnx")
if(WIN32)
    set(_need_rife_onnx TRUE)
    if(EXISTS "${RTX_CACHED_RIFE_ONNX}")
        file(SIZE "${RTX_CACHED_RIFE_ONNX}" _cached_onnx_sz)
        if(_cached_onnx_sz GREATER_EQUAL 5242880)
            set(_need_rife_onnx FALSE)
        endif()
    endif()
    if(_need_rife_onnx AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/rife_v4.25.onnx")
        file(SIZE "${CMAKE_CURRENT_SOURCE_DIR}/models/rife_v4.25.onnx" _src_onnx_sz)
        if(_src_onnx_sz GREATER_EQUAL 5242880)
            file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/models/rife_v4.25.onnx" "${RTX_CACHED_RIFE_ONNX}")
            set(_need_rife_onnx FALSE)
        endif()
    endif()
    if(_need_rife_onnx)
        set(_rife_onnx_url "https://huggingface.co/FuryTMP/RIFE_fp32/resolve/main/RIFE_fp32.onnx")
        message(STATUS "[CineRTX] Provisioning RIFE v4.25 ONNX weights from ${_rife_onnx_url} ...")
        find_program(_rtx_curl_onnx NAMES curl.exe curl)
        if(_rtx_curl_onnx)
            execute_process(
                COMMAND "${_rtx_curl_onnx}" -fL --retry 3 --retry-delay 2 --connect-timeout 30
                        -o "${RTX_CACHED_RIFE_ONNX}.tmp" "${_rife_onnx_url}"
                RESULT_VARIABLE _curl_onnx_rc
                OUTPUT_QUIET ERROR_QUIET)
            if(_curl_onnx_rc EQUAL 0 AND EXISTS "${RTX_CACHED_RIFE_ONNX}.tmp")
                file(SIZE "${RTX_CACHED_RIFE_ONNX}.tmp" _dl_onnx_sz)
                if(_dl_onnx_sz GREATER_EQUAL 5242880)
                    file(RENAME "${RTX_CACHED_RIFE_ONNX}.tmp" "${RTX_CACHED_RIFE_ONNX}")
                    set(_need_rife_onnx FALSE)
                else()
                    file(REMOVE "${RTX_CACHED_RIFE_ONNX}.tmp")
                endif()
            else()
                file(REMOVE "${RTX_CACHED_RIFE_ONNX}.tmp")
            endif()
        endif()
        if(_need_rife_onnx)
            file(DOWNLOAD "${_rife_onnx_url}" "${RTX_CACHED_RIFE_ONNX}.tmp"
                 TLS_VERIFY ON STATUS _dl_st)
            list(GET _dl_st 0 _dl_rc)
            if(_dl_rc EQUAL 0 AND EXISTS "${RTX_CACHED_RIFE_ONNX}.tmp")
                file(SIZE "${RTX_CACHED_RIFE_ONNX}.tmp" _dl_onnx_sz)
                if(_dl_onnx_sz GREATER_EQUAL 5242880)
                    file(RENAME "${RTX_CACHED_RIFE_ONNX}.tmp" "${RTX_CACHED_RIFE_ONNX}")
                    set(_need_rife_onnx FALSE)
                else()
                    file(REMOVE "${RTX_CACHED_RIFE_ONNX}.tmp")
                endif()
            else()
                file(REMOVE "${RTX_CACHED_RIFE_ONNX}.tmp")
            endif()
        endif()
        if(NOT _need_rife_onnx)
            message(STATUS "[CineRTX] RIFE v4.25 ONNX cached at ${RTX_CACHED_RIFE_ONNX}")
        else()
            message(STATUS "[CineRTX] Offline/CI host without RIFE ONNX mirror access; runtime will probe local models/ or synthesize when unlocked.")
        endif()
    endif()
    foreach(_eng rife_v4.25_landscape.engine rife_v4.25_portrait.engine rife_v4.25.engine)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/models/${_eng}" AND NOT EXISTS "${RTX_EXTERNAL_MODELS_DIR}/${_eng}")
            file(SIZE "${CMAKE_CURRENT_SOURCE_DIR}/models/${_eng}" _src_eng_sz)
            if(_src_eng_sz GREATER_EQUAL 5242880 AND _src_eng_sz LESS 33554432)
                file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/models/${_eng}" "${RTX_EXTERNAL_MODELS_DIR}/${_eng}")
            endif()
        endif()
    endforeach()
endif()

if(WIN32)
    set(RTX_NGX_BRIDGE_SOURCES
        src/shim/ngx_bridge/bridge_exports.cpp
        src/shim/ngx_bridge/p010_pack_runtime.cpp
        src/shim/ngx_bridge/nvof_engine.cpp
        src/shim/ngx_bridge/rife_pack_runtime.cpp
        src/shim/ngx_bridge/trt_loader.cpp
        src/shim/ngx_bridge/nv_func_tables.cpp
        src/shim/ngx_bridge/rife_engine.cpp
        src/shim/ngx_bridge/rife_builder.cpp
        src/shim/ngx_bridge/d3d12_interop.cpp
        "${RTX_PTX_HEADER}"
        "${RTX_RIFE_PTX_HEADER}")
else()
    set(RTX_NGX_BRIDGE_SOURCES
        src/shim/ngx_bridge/bridge_exports.cpp
        src/shim/ngx_bridge/p010_pack_runtime.cpp
        src/shim/ngx_bridge/nvof_engine.cpp
        src/shim/ngx_bridge/rife_pack_runtime.cpp
        src/shim/ngx_bridge/trt_loader.cpp
        src/shim/ngx_bridge/nv_func_tables.cpp
        src/shim/ngx_bridge/rife_engine.cpp
        src/shim/ngx_bridge/rife_builder.cpp
        "${RTX_PTX_HEADER}"
        "${RTX_RIFE_PTX_HEADER}")
endif()

# WO-047 任务 3（开源合规隔离网）：rtx_ngx_bridge 构建为独立 SHARED 动态库（rtx_ngx_bridge.dll），
# 专有 nvsdk_ngx_s.lib 仅允许被本 DLL 私有链接，绝不静态编入 GPL 主程序 CineRTX.exe。
add_library(rtx_ngx_bridge SHARED ${RTX_NGX_BRIDGE_SOURCES}
    src/core/shot_segmenter.cpp
    $<$<PLATFORM_ID:Windows>:src/shim/ngx_bridge/ngx_d3d12_bridge.cpp>)
add_library(rtx::ngx_bridge ALIAS rtx_ngx_bridge)
target_link_libraries(rtx_ngx_bridge PRIVATE rtx_project_options)
target_compile_definitions(rtx_ngx_bridge PRIVATE RTX_SHIM_EXPORTS=1)
if(NOT MSVC)
    target_compile_options(rtx_ngx_bridge PRIVATE
        -Wno-deprecated-declarations -Wno-unused-const-variable)
endif()
target_include_directories(rtx_ngx_bridge PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/shim"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/shim/ngx_bridge"
    "${RTX_GENERATED_DIR}")
target_include_directories(rtx_ngx_bridge SYSTEM PRIVATE
    "${RTX_NVCODEC_INCLUDE_DIR}"
    "${RTX_CUDA_HEADERS_DIR}"
    "${RTX_TENSORRT_INCLUDE_DIR}")
set_target_properties(rtx_ngx_bridge PROPERTIES
    OUTPUT_NAME "rtx_ngx_bridge"
    POSITION_INDEPENDENT_CODE ON)

if(WIN32)
    set(RTX_NGX_ROOT_DIR "")

    if(RTX_VIDEO_SDK_ROOT AND EXISTS "${RTX_VIDEO_SDK_ROOT}/include/nvsdk_ngx.h")
        set(RTX_NGX_ROOT_DIR "${RTX_VIDEO_SDK_ROOT}")
    elseif(DEFINED ENV{RTX_VIDEO_SDK_ROOT} AND EXISTS "$ENV{RTX_VIDEO_SDK_ROOT}/include/nvsdk_ngx.h")
        set(RTX_NGX_ROOT_DIR "$ENV{RTX_VIDEO_SDK_ROOT}")
    endif()

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
            if(EXISTS "${RTX_NGX_ROOT_DIR}/lib/Windows/x64/nvsdk_ngx_s_dbg.lib")
                set(RTX_NGX_LIBRARY "$<IF:$<CONFIG:Debug>,${RTX_NGX_ROOT_DIR}/lib/Windows/x64/nvsdk_ngx_s_dbg.lib,${RTX_NGX_ROOT_DIR}/lib/Windows/x64/nvsdk_ngx_s.lib>")
            else()
                set(RTX_NGX_LIBRARY "${RTX_NGX_ROOT_DIR}/lib/Windows/x64/nvsdk_ngx_s.lib")
            endif()
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
    target_include_directories(rtx_ngx_bridge SYSTEM PRIVATE "${RTX_NGX_INCLUDE_DIR}")
    target_compile_definitions(rtx_ngx_bridge PRIVATE RTX_HAVE_NGX_SDK=1)
    target_link_libraries(rtx_ngx_bridge PRIVATE "${RTX_NGX_LIBRARY}")
endif()

if(WIN32)
    set(RTX_CINERTX_SOURCES
        src/gui/rtx_gui.cpp
        src/gui/preview_viewport.cpp
        src/gui/workstation_canvas.cpp
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
if(WIN32)
    target_sources(CineRTX PRIVATE "${RTX_SHADER_HEADER}")
    target_include_directories(CineRTX PRIVATE "${RTX_GENERATED_DIR}")
endif()
target_link_libraries(CineRTX PRIVATE rtx_core)
if(WIN32)
    target_link_libraries(CineRTX PRIVATE gdi32 user32)
endif()
if(CMAKE_DL_LIBS)
    target_link_libraries(CineRTX PRIVATE ${CMAKE_DL_LIBS})
endif()
target_include_directories(CineRTX PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/shim"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gui")
target_compile_definitions(CineRTX PRIVATE
    RTX_VERSION_STRING="${PROJECT_VERSION}")
if(WIN32)
    if(MSVC)
        target_compile_options(CineRTX PRIVATE /Gy $<$<CONFIG:RelWithDebInfo>:/O1>)
        target_link_options(CineRTX PRIVATE
            /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup /OPT:REF /OPT:ICF /INCREMENTAL:NO
            "/MAP:$<TARGET_FILE_DIR:CineRTX>/CineRTX.exe.map")
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

    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols")

if(WIN32)
    set(_rtx_ngx_root_files "")
    if(RTX_NGX_VSR_DLL AND EXISTS "${RTX_NGX_VSR_DLL}")
        list(APPEND _rtx_ngx_root_files "${RTX_NGX_VSR_DLL}" "${RTX_NGX_TRUEHDR_DLL}")
    endif()
    set(_trt_lib_dir "${RTX_TENSORRT_ROOT}/lib")
    set(_rtx_trt_plugin_files "")
    foreach(_trt_dll nvinfer_10.dll nvinfer_builder_resource_10.dll nvonnxparser_10.dll)
        if(EXISTS "${_trt_lib_dir}/${_trt_dll}")
            list(APPEND _rtx_trt_plugin_files "${_trt_lib_dir}/${_trt_dll}")
        endif()
    endforeach()
    set(_rtx_stage_trt_command "")
    if(_rtx_trt_plugin_files)
        set(_rtx_stage_trt_command
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_rtx_trt_plugin_files}
                "$<TARGET_FILE_DIR:CineRTX>/plugins/nvidia_rtx/")
    endif()
    set(_rtx_model_files "")
    if(EXISTS "${RTX_EXTERNAL_MODELS_DIR}/rife_v4.25.onnx")
        list(APPEND _rtx_model_files "${RTX_EXTERNAL_MODELS_DIR}/rife_v4.25.onnx")
    endif()
    foreach(_eng rife_v4.25_landscape.engine rife_v4.25_portrait.engine rife_v4.25.engine)
        if(EXISTS "${RTX_EXTERNAL_MODELS_DIR}/${_eng}")
            list(APPEND _rtx_model_files "${RTX_EXTERNAL_MODELS_DIR}/${_eng}")
        endif()
    endforeach()
    set(_rtx_stage_models_command "")
    if(_rtx_model_files)
        set(_rtx_stage_models_command
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_rtx_model_files}
                "$<TARGET_FILE_DIR:CineRTX>/models/")
    endif()
    add_custom_command(TARGET CineRTX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:CineRTX>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_rtx_ngx_root_files}
            "$<TARGET_FILE_DIR:CineRTX>/"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:CineRTX>/plugins/nvidia_rtx"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:CineRTX>/models"
        ${_rtx_stage_trt_command}
        ${_rtx_stage_models_command}
        COMMAND ${CMAKE_COMMAND}
            -DEXE_DIR=$<TARGET_FILE_DIR:CineRTX>
            -DEXT_MODELS_DIR=${RTX_EXTERNAL_MODELS_DIR}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EnsureRifeEngine.cmake"
        VERBATIM)
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
            ${_rtx_ngx_root_files}
            "${_rtx_ngx_product_plugin_dir}/"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${_rtx_ngx_product_plugin_dir}/plugins/nvidia_rtx"
        COMMAND ${CMAKE_COMMAND}
            "-DPLUGIN_DIR=${_rtx_ngx_product_plugin_dir}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyNgxStaging.cmake"
        COMMENT "Staging the RTX NGX plugin beside CineRTX.exe"
        VERBATIM)
    add_custom_command(TARGET CineRTX POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rtx_ngx_bridge>"
            "$<TARGET_FILE_DIR:CineRTX>/"
        VERBATIM)
    add_dependencies(CineRTX rtx_ngx_bridge rtx_stage_ngx_plugin)
endif()

# Ruling 058 (Wave 12.3): Pure console headless CLI probe target for Gate 12.3-1 HEADLESS audit.
# Reuses existing CLI sources with zero GUI sources, zero user32/gdi32 linkage, and zero new types.
add_executable(rtx_headless_cli
    src/cli/main_entry.cpp
    src/cli/main.cpp
    src/cli/shim_probe.cpp)
add_executable(rtx::headless_cli ALIAS rtx_headless_cli)
target_link_libraries(rtx_headless_cli PRIVATE rtx_core rtx_project_options)
if(WIN32)
    target_link_libraries(rtx_headless_cli PRIVATE bcrypt)
endif()
if(CMAKE_DL_LIBS)
    target_link_libraries(rtx_headless_cli PRIVATE ${CMAKE_DL_LIBS})
endif()
target_include_directories(rtx_headless_cli PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/shim"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli")
target_compile_definitions(rtx_headless_cli PRIVATE
    RTX_VERSION_STRING="${PROJECT_VERSION}")
if(WIN32)
    if(MSVC)
        target_compile_options(rtx_headless_cli PRIVATE /Gy $<$<CONFIG:RelWithDebInfo>:/O1>)
        target_link_options(rtx_headless_cli PRIVATE
            /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF /INCREMENTAL:NO)
    else()
        target_compile_options(rtx_headless_cli PRIVATE
            -Os -ffunction-sections -fdata-sections)
        target_link_options(rtx_headless_cli PRIVATE
            -static -static-libgcc -static-libstdc++
            -Wl,--gc-sections -Wl,--strip-all)
    endif()
endif()
set_target_properties(rtx_headless_cli PROPERTIES
    OUTPUT_NAME "rtx_headless_cli"
    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols")

set(RTX_TEST_SOURCES
    tests/test_flicks_timebase.cpp
    tests/test_fuzz_resilience.cpp
    tests/test_gui_workstation.cpp
    tests/test_hdr.cpp
    tests/test_i18n.cpp
    tests/test_io.cpp
    tests/test_lut3d.cpp
    tests/test_main.cpp
    tests/test_rife.cpp
    tests/test_shim.cpp
    tests/test_types.cpp
    tests/test_raii_retirement.cpp
    tests/test_param_schema_reflection.cpp
    tests/test_flicks_semantics.cpp
    tests/test_playback_session.cpp
    tests/test_nle_vertical_slice.cpp
    tests/test_audio_mixer_avx2.cpp
    tests/test_geometry.cpp
    tests/test_rife_scale05.cpp
    tests/test_vram_scheduler.cpp)

add_executable(rtx_tests ${RTX_TEST_SOURCES}
    src/cli/shim_probe.cpp)
target_link_libraries(rtx_tests PRIVATE rtx_core rtx_project_options)
if(WIN32)
    target_sources(rtx_tests PRIVATE "${RTX_SHADER_HEADER}")
    target_include_directories(rtx_tests PRIVATE "${RTX_GENERATED_DIR}")
endif()
if(WIN32)
    add_dependencies(rtx_tests rtx_ngx_bridge)
    target_link_libraries(rtx_tests PRIVATE rtx_ngx_bridge)
    target_sources(rtx_tests PRIVATE
        src/shim/ngx_bridge/nv_func_tables.cpp
        src/shim/ngx_bridge/rife_pack_runtime.cpp)
    add_custom_command(TARGET rtx_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:rtx_ngx_bridge>"
            "$<TARGET_FILE_DIR:rtx_tests>/"
        VERBATIM)
    target_sources(rtx_tests PRIVATE src/gui/preview_viewport.cpp)
    target_link_libraries(rtx_tests PRIVATE user32)
endif()
add_custom_command(TARGET rtx_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:rtx_tests>/fixtures/"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/h264_25fps_gop12.mkv"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/hevc_25fps_gop12.mkv"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/av1_25fps_gop12.mkv"
        "$<TARGET_FILE_DIR:rtx_tests>/fixtures/"
    COMMENT "Deploying physical .mkv playback fixtures beside rtx_tests"
    VERBATIM)
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

    PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/symbols")

add_test(NAME rtx_unit_tests COMMAND rtx_tests)
set_tests_properties(rtx_unit_tests PROPERTIES
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:rtx_tests>"
    TIMEOUT 300)

install(TARGETS CineRTX rtx_ngx_bridge RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
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
