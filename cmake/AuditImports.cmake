# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Native CMake binary closure and import-table gate (replaces tools/audit_imports.ps1).

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED BUILD_DIR OR "${BUILD_DIR}" STREQUAL "")
    set(BUILD_DIR "${REPO_ROOT}/.vs/build/cmake/windows-msvc-ninja")
endif()

set(_exe "")
foreach(_cand "${BUILD_DIR}/CineRTX.exe" "${BUILD_DIR}/CineRTX" "${BINARY}")
    if( NOT "${_cand}" STREQUAL "" AND EXISTS "${_cand}" AND NOT IS_DIRECTORY "${_cand}")
        set(_exe "${_cand}")
        break()
    endif()
endforeach()

if("${_exe}" STREQUAL "")
    message(FATAL_ERROR "[AuditImports] Could not find CineRTX executable in BUILD_DIR='${BUILD_DIR}'")
endif()

get_filename_component(_exe_dir "${_exe}" DIRECTORY)
file(SIZE "${_exe}" _exe_bytes)

# Check envelope on Windows PE builds: 1.2 MB <= CineRTX.exe <= 12.0 MB
if(_exe MATCHES "\.exe$")
    if(_exe_bytes LESS 1258291 OR _exe_bytes GREATER 12582912)
        message(FATAL_ERROR
            "[AuditImports] ${_exe} size (${_exe_bytes} bytes) violates Gate 3.0 static envelope [1.2 MB, 12.0 MB]")
    endif()
endif()

# Verify no forbidden FFmpeg or NVIDIA driver sidecar DLLs leaked into root build dir
file(GLOB _leaked_media_dlls
    "${_exe_dir}/avcodec*.dll"
    "${_exe_dir}/avformat*.dll"
    "${_exe_dir}/avutil*.dll"
    "${_exe_dir}/swresample*.dll"
    "${_exe_dir}/swscale*.dll"
    "${_exe_dir}/avfilter*.dll"
    "${_exe_dir}/avdevice*.dll"
    "${_exe_dir}/postproc*.dll"
    "${_exe_dir}/nvcuda*.dll"
    "${_exe_dir}/nvcuvid*.dll"
    "${_exe_dir}/nvEncodeAPI*.dll"
    "${_exe_dir}/nvofapi*.dll"
    "${_exe_dir}/NvOFFRUC*.dll")

if(_leaked_media_dlls)
    message(FATAL_ERROR "[AuditImports] Forbidden dynamic DLLs found alongside CineRTX: ${_leaked_media_dlls}")
endif()

# On Linux ELF builds, inspect NEEDED entries via readelf if available
if(NOT _exe MATCHES "\.exe$")
    find_program(_readelf NAMES readelf)
    if(_readelf)
        execute_process(
            COMMAND "${_readelf}" -d --wide "${_exe}"
            OUTPUT_VARIABLE _elf_dyn
            ERROR_QUIET)
        if(_elf_dyn MATCHES "(libcuda|libnvcuvid|libnvidia-encode|libnvinfer|libnvonnxparser)")
            message(FATAL_ERROR "[AuditImports] ELF binary statically imports forbidden NVIDIA library: ${CMAKE_MATCH_1}")
        endif()
    endif()
endif()

message(STATUS "[AuditImports] PASS: ${_exe} (${_exe_bytes} bytes), zero leaked dynamic media/driver DLLs.")
