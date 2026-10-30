# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# NVIDIA Optical Flow SDK 5.0.7 and TensorRT 10.13.3.9 provisioning.
#
# Same contract as the RTX Video SDK block in CMakeLists.txt: an archive cached
# under build/external is verified against a pinned SHA-256 and reused offline,
# and only a *missing* archive falls back to the placeholder URL.  A cached
# archive whose digest does not match is never deleted here - the configure step
# stops and reports, because the operator owns those bytes.
#
# What the product needs at run time is only the redistributable runtime:
#   Optical Flow  NvOFFRUC.dll + cudart64_110.dll (the FRUC DLL imports the
#                 CUDA runtime; loading it alone fails with error 126)
#   TensorRT      nvinfer_10.dll, nvonnxparser_10.dll, nvinfer_plugin_10.dll and
#                 nvinfer_builder_resource_10.dll
# Both live beside CineRTX.exe: the shim loader searches RTX_NVIDIA_DIR and the
# executable directory only, with no System32 fallback for either component.
#
# TensorRT is pinned to 10.13.3.9 on purpose.  include/rtx/rife.h holds the hard
# gate (kRifeTensorRtBaseline) because 10.14+ regresses the RIFE v4.25 graph
# (NVIDIA/TensorRT issue #4721); this module provisions that exact release and
# nothing else.

include_guard(GLOBAL)

include("${CMAKE_CURRENT_LIST_DIR}/RtxProvision.cmake")

set(RTX_OF_SDK_VERSION "5.0.7")
set(RTX_OF_SDK_URL
    "https://developer.download.nvidia.cn/designworks/optical-flow-sdk/secure/5.0/Optical_Flow_SDK_5.0.7.zip")
set(RTX_OF_SDK_SHA256 "89b0923adc6f34fbe86e63cc17d5db452e34b945c03ba90d8e09bd1a0158e917")
set(RTX_OF_SDK_ARCHIVE "${RTX_EXTERNAL_DIR}/Optical_Flow_SDK_${RTX_OF_SDK_VERSION}.zip")
set(RTX_OF_SDK_DIR "${RTX_EXTERNAL_DIR}/optical_flow_sdk")

set(RTX_TENSORRT_SDK_VERSION "10.13.3.9")
set(RTX_TENSORRT_URL
    "https://developer.nvidia.com/downloads/compute/machine-learning/tensorrt/10.13.3/zip/TensorRT-10.13.3.9.Windows.win10.cuda-13.0.zip")
set(RTX_TENSORRT_SHA256 "10ada15b14498e0ec8556cb19433129fd362a8e3debf75db06ccacd8659d9662")
set(RTX_TENSORRT_ARCHIVE
    "${RTX_EXTERNAL_DIR}/TensorRT-${RTX_TENSORRT_SDK_VERSION}.Windows.win10.cuda-13.0.zip")
set(RTX_TENSORRT_DIR "${RTX_EXTERNAL_DIR}/tensorrt")

# ---------------------------------------------------------------------------
# Pinned archive handling
# ---------------------------------------------------------------------------

# rtx_verify_pinned_archive(<label> <url> <sha256> <archive> <out_var>)
#
# TRUE when <archive> exists and matches <sha256>, or when it was fetched and
# verified.  A digest mismatch on an existing archive is fatal and leaves the
# file untouched: rtx_fetch_archive() would delete it before downloading, which
# is the wrong default for an operator-supplied multi-gigabyte archive.
function(rtx_verify_pinned_archive label url expected_sha archive out_var)
    set(${out_var} FALSE PARENT_SCOPE)

    if(EXISTS "${archive}")
        file(SHA256 "${archive}" _actual_sha)
        if(NOT _actual_sha STREQUAL expected_sha)
            message(FATAL_ERROR
                "[CineRTX] ${label}: the cached archive does not match the pinned digest.\n"
                "  archive:  ${archive}\n"
                "  expected: ${expected_sha}\n"
                "  actual:   ${_actual_sha}\n"
                "The file is left untouched. Replace it with the official download or "
                "remove it yourself, then re-run configure to fetch ${url}.")
        endif()
        set(${out_var} TRUE PARENT_SCOPE)
        return()
    endif()

    message(STATUS "[CineRTX] ${label}: no cached archive; fetching ${url}")
    rtx_fetch_archive("${url}" "${expected_sha}" "${archive}" _fetched)
    if(NOT _fetched)
        message(STATUS
            "[CineRTX] ${label}: download failed or was rejected. NVIDIA gates this "
            "endpoint behind a browser session, so place the archive at "
            "${archive} (SHA-256 ${expected_sha}) and re-run configure.")
    endif()
    set(${out_var} "${_fetched}" PARENT_SCOPE)
endfunction()

# rtx_extract_pinned_archive(<archive> <destination>)
function(rtx_extract_pinned_archive archive destination)
    file(MAKE_DIRECTORY "${destination}")
    message(STATUS "[CineRTX] Extracting ${archive} into ${destination} ...")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${destination}")
endfunction()

# ---------------------------------------------------------------------------
# Optical Flow SDK
# ---------------------------------------------------------------------------

# rtx_find_optical_flow(<root> <out_dll> <out_cudart> <out_include>)
#
# The SDK ships NvOFFRUC.dll in NvOFFRUC/NvOFFRUCSample/bin/win64/ next to
# cudart64_110.dll; the win32 copy is ignored.  Headers live in
# NvOFFRUC/Interface/NvOFFRUC.h.
function(rtx_find_optical_flow root out_dll out_cudart out_include)
    set(${out_dll} "" PARENT_SCOPE)
    set(${out_cudart} "" PARENT_SCOPE)
    set(${out_include} "" PARENT_SCOPE)
    if(NOT IS_DIRECTORY "${root}")
        return()
    endif()

    file(GLOB_RECURSE _dlls "${root}/NvOFFRUC.dll")
    foreach(_candidate IN LISTS _dlls)
        get_filename_component(_directory "${_candidate}" DIRECTORY)
        get_filename_component(_architecture "${_directory}" NAME)
        if(NOT _architecture STREQUAL "win64")
            continue()
        endif()
        set(${out_dll} "${_candidate}" PARENT_SCOPE)
        if(EXISTS "${_directory}/cudart64_110.dll")
            set(${out_cudart} "${_directory}/cudart64_110.dll" PARENT_SCOPE)
        endif()
        break()
    endforeach()

    file(GLOB_RECURSE _headers "${root}/NvOFFRUC.h")
    if(_headers)
        list(GET _headers 0 _header)
        get_filename_component(_directory "${_header}" DIRECTORY)
        set(${out_include} "${_directory}" PARENT_SCOPE)
    endif()
endfunction()

set(RTX_OF_NVOFFRUC_DLL "")
set(RTX_OF_CUDART_DLL "")
set(RTX_OF_INCLUDE_DIR "")

if(RTX_OF_SDK_ROOT)
    rtx_find_optical_flow("${RTX_OF_SDK_ROOT}" RTX_OF_NVOFFRUC_DLL RTX_OF_CUDART_DLL
                          RTX_OF_INCLUDE_DIR)
    if(NOT RTX_OF_NVOFFRUC_DLL)
        message(FATAL_ERROR
            "RTX_OF_SDK_ROOT=${RTX_OF_SDK_ROOT} does not contain "
            "NvOFFRUC/NvOFFRUCSample/bin/win64/NvOFFRUC.dll. Point it at an extracted "
            "Optical Flow SDK ${RTX_OF_SDK_VERSION} tree or leave it empty to let the "
            "build provision the pinned archive.")
    endif()
elseif(WIN32)
    rtx_find_optical_flow("${RTX_OF_SDK_DIR}" RTX_OF_NVOFFRUC_DLL RTX_OF_CUDART_DLL
                          RTX_OF_INCLUDE_DIR)
    if(NOT RTX_OF_NVOFFRUC_DLL)
        rtx_verify_pinned_archive("Optical Flow SDK ${RTX_OF_SDK_VERSION}"
            "${RTX_OF_SDK_URL}" "${RTX_OF_SDK_SHA256}" "${RTX_OF_SDK_ARCHIVE}"
            _of_archive_ready)
        if(_of_archive_ready)
            rtx_extract_pinned_archive("${RTX_OF_SDK_ARCHIVE}" "${RTX_OF_SDK_DIR}")
            rtx_find_optical_flow("${RTX_OF_SDK_DIR}" RTX_OF_NVOFFRUC_DLL
                                  RTX_OF_CUDART_DLL RTX_OF_INCLUDE_DIR)
            if(NOT RTX_OF_NVOFFRUC_DLL)
                message(FATAL_ERROR
                    "Extracted ${RTX_OF_SDK_ARCHIVE} into ${RTX_OF_SDK_DIR} but found no "
                    "win64/NvOFFRUC.dll. The archive layout is not the one this build "
                    "expects; delete ${RTX_OF_SDK_DIR} and re-extract, or set "
                    "-DRTX_OF_SDK_ROOT=<path>.")
            endif()
        endif()
    endif()

    if(RTX_OF_NVOFFRUC_DLL)
        set(RTX_OF_SDK_ROOT "${RTX_OF_SDK_DIR}" CACHE PATH
            "NVIDIA Optical Flow SDK ${RTX_OF_SDK_VERSION} root (NvOFFRUC/Interface, NvOFFRUC/NvOFFRUCSample)"
            FORCE)
    endif()
endif()

if(RTX_OF_NVOFFRUC_DLL)
    message(STATUS
        "NVIDIA Optical Flow SDK ${RTX_OF_SDK_VERSION}: ${RTX_OF_NVOFFRUC_DLL}")
    if(NOT RTX_OF_CUDART_DLL)
        message(WARNING
            "NvOFFRUC.dll was found without cudart64_110.dll beside it. The FRUC DLL "
            "imports the CUDA runtime and fails to load on its own (error 126), so the "
            "optical-flow backend stays unavailable until both are deployed.")
    endif()
elseif(WIN32)
    message(WARNING
        "NVIDIA Optical Flow SDK ${RTX_OF_SDK_VERSION} is not available; the FRUC "
        "backend will report RTX_FRUC_ERR_UNAVAILABLE at run time. Place "
        "${RTX_OF_SDK_ARCHIVE} (SHA-256 ${RTX_OF_SDK_SHA256}) or set "
        "-DRTX_OF_SDK_ROOT=<path>.")
endif()

# ---------------------------------------------------------------------------
# TensorRT
# ---------------------------------------------------------------------------

# rtx_find_tensorrt(<root> <out_include> <out_nvinfer> <out_onnxparser> <out_dlls>)
#
# The 10.x Windows archive keeps the headers in include/, the import libraries
# and the runtime DLLs in lib/, and trtexec in bin/.  Older layouts put the DLLs
# in bin/, so both are searched.  The import libraries are reported for
# diagnostics only - nothing links them; trt_dynamic.cpp resolves the runtime by
# symbol so the executable's import table stays free of NVIDIA DLLs.
function(rtx_find_tensorrt root out_include out_nvinfer out_onnxparser out_dlls)
    set(${out_include} "" PARENT_SCOPE)
    set(${out_nvinfer} "" PARENT_SCOPE)
    set(${out_onnxparser} "" PARENT_SCOPE)
    set(${out_dlls} "" PARENT_SCOPE)
    if(NOT IS_DIRECTORY "${root}")
        return()
    endif()

    file(GLOB _headers "${root}/include/NvInfer.h" "${root}/*/include/NvInfer.h")
    if(NOT _headers)
        return()
    endif()
    list(GET _headers 0 _header)
    get_filename_component(_include "${_header}" DIRECTORY)
    get_filename_component(_root "${_include}" DIRECTORY)
    set(${out_include} "${_include}" PARENT_SCOPE)

    foreach(_candidate
            "${_root}/lib/nvinfer_10.lib"
            "${_root}/lib/nvinfer.lib")
        if(EXISTS "${_candidate}")
            set(${out_nvinfer} "${_candidate}" PARENT_SCOPE)
            break()
        endif()
    endforeach()
    foreach(_candidate
            "${_root}/lib/nvonnxparser_10.lib"
            "${_root}/lib/nvonnxparser.lib")
        if(EXISTS "${_candidate}")
            set(${out_onnxparser} "${_candidate}" PARENT_SCOPE)
            break()
        endif()
    endforeach()

    set(_dlls "")
    foreach(_name
            nvinfer_10.dll
            nvonnxparser_10.dll
            nvinfer_plugin_10.dll
            nvinfer_builder_resource_10.dll)
        foreach(_directory "${_root}/lib" "${_root}/bin")
            if(EXISTS "${_directory}/${_name}")
                list(APPEND _dlls "${_directory}/${_name}")
                break()
            endif()
        endforeach()
    endforeach()
    set(${out_dlls} "${_dlls}" PARENT_SCOPE)
endfunction()

set(RTX_TENSORRT_INCLUDE_DIR "")
set(RTX_TENSORRT_LIBRARY "")
set(RTX_TENSORRT_ONNX_PARSER_LIBRARY "")
set(RTX_TENSORRT_RUNTIME_DLLS "")

if(RTX_TENSORRT_ROOT)
    rtx_find_tensorrt("${RTX_TENSORRT_ROOT}" RTX_TENSORRT_INCLUDE_DIR RTX_TENSORRT_LIBRARY
                      RTX_TENSORRT_ONNX_PARSER_LIBRARY RTX_TENSORRT_RUNTIME_DLLS)
elseif(WIN32)
    rtx_find_tensorrt("${RTX_TENSORRT_DIR}" RTX_TENSORRT_INCLUDE_DIR RTX_TENSORRT_LIBRARY
                      RTX_TENSORRT_ONNX_PARSER_LIBRARY RTX_TENSORRT_RUNTIME_DLLS)
    if(NOT RTX_TENSORRT_INCLUDE_DIR)
        rtx_verify_pinned_archive("TensorRT ${RTX_TENSORRT_SDK_VERSION}"
            "${RTX_TENSORRT_URL}" "${RTX_TENSORRT_SHA256}" "${RTX_TENSORRT_ARCHIVE}"
            _trt_archive_ready)
        if(_trt_archive_ready)
            rtx_extract_pinned_archive("${RTX_TENSORRT_ARCHIVE}" "${RTX_TENSORRT_DIR}")
            rtx_find_tensorrt("${RTX_TENSORRT_DIR}" RTX_TENSORRT_INCLUDE_DIR
                              RTX_TENSORRT_LIBRARY RTX_TENSORRT_ONNX_PARSER_LIBRARY
                              RTX_TENSORRT_RUNTIME_DLLS)
            if(NOT RTX_TENSORRT_INCLUDE_DIR)
                message(FATAL_ERROR
                    "Extracted ${RTX_TENSORRT_ARCHIVE} into ${RTX_TENSORRT_DIR} but found "
                    "no include/NvInfer.h. The archive layout is not the one this build "
                    "expects; delete ${RTX_TENSORRT_DIR} and re-extract, or set "
                    "-DRTX_TENSORRT_ROOT=<path>.")
            endif()
        endif()
    endif()

    if(RTX_TENSORRT_INCLUDE_DIR)
        set(RTX_TENSORRT_ROOT "${RTX_TENSORRT_DIR}" CACHE PATH
            "TensorRT ${RTX_TENSORRT_SDK_VERSION} root containing include/NvInfer.h and lib/nvinfer_10.lib"
            FORCE)
    endif()
endif()

if(RTX_TENSORRT_INCLUDE_DIR)
    # Headers only: the runtime is resolved with GetProcAddress, so no NVIDIA
    # import library is on a link line and the PE import table stays clean.
    message(STATUS
        "TensorRT ${RTX_TENSORRT_SDK_VERSION}: ${RTX_TENSORRT_INCLUDE_DIR} "
        "(headers only; runtime resolved at run time)")
elseif(RTX_TENSORRT_ROOT)
    message(WARNING
        "RTX_TENSORRT_ROOT=${RTX_TENSORRT_ROOT} does not contain include/NvInfer.h; "
        "--build-rife falls back to the trtexec command.")
elseif(WIN32)
    message(WARNING
        "TensorRT ${RTX_TENSORRT_SDK_VERSION} is not available; RIFE engine builds fall "
        "back to the sanctioned trtexec command and the probe reports the component "
        "missing. Place ${RTX_TENSORRT_ARCHIVE} (SHA-256 ${RTX_TENSORRT_SHA256}) or set "
        "-DRTX_TENSORRT_ROOT=<path>.")
endif()

# ---------------------------------------------------------------------------
# CUDA declarations for the TensorRT headers
#
# NvInferRuntimeBase.h includes <cuda_runtime_api.h> unconditionally, but only
# uses cudaStream_t and cudaEvent_t in its declarations.  A host without a CUDA
# toolkit therefore still needs those two typedefs to compile rife_engine.cpp.
# The real toolkit wins whenever it exists; otherwise cmake/cuda_headers holds a
# declaration-only header (no function, no library, no emulation).
# ---------------------------------------------------------------------------

set(RTX_TENSORRT_CUDA_INCLUDE_DIR "")
if(RTX_TENSORRT_INCLUDE_DIR)
    if(RTX_CUDA_INCLUDE AND EXISTS "${RTX_CUDA_INCLUDE}/cuda_runtime_api.h")
        set(RTX_TENSORRT_CUDA_INCLUDE_DIR "${RTX_CUDA_INCLUDE}")
    else()
        find_package(CUDAToolkit QUIET)
        if(CUDAToolkit_FOUND AND CUDAToolkit_INCLUDE_DIRS)
            set(RTX_TENSORRT_CUDA_INCLUDE_DIR "${CUDAToolkit_INCLUDE_DIRS}")
        else()
            set(RTX_TENSORRT_CUDA_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/cuda_headers")
            message(STATUS
                "TensorRT headers: no CUDA toolkit on this host; using the "
                "declaration-only cuda_runtime_api.h shim in cmake/cuda_headers")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Runtime deployment list
#
# The shim loader opens NvOFFRUC.dll and nvinfer_10.dll by explicit path in the
# executable directory (system_fallback is false for both), and the FRUC DLL
# needs cudart64_110.dll as a sibling.  These therefore belong beside
# CineRTX.exe, not under plugins/nvidia_rtx.
# ---------------------------------------------------------------------------

set(RTX_NVIDIA_RUNTIME_DLLS "")
set(RTX_NVIDIA_REQUIRED_RUNTIME_FILES "")

if(RTX_OF_NVOFFRUC_DLL)
    list(APPEND RTX_NVIDIA_RUNTIME_DLLS "${RTX_OF_NVOFFRUC_DLL}")
    list(APPEND RTX_NVIDIA_REQUIRED_RUNTIME_FILES "NvOFFRUC.dll")
    if(RTX_OF_CUDART_DLL)
        list(APPEND RTX_NVIDIA_RUNTIME_DLLS "${RTX_OF_CUDART_DLL}")
        list(APPEND RTX_NVIDIA_REQUIRED_RUNTIME_FILES "cudart64_110.dll")
    endif()
endif()

if(RTX_TENSORRT_RUNTIME_DLLS)
    list(APPEND RTX_NVIDIA_RUNTIME_DLLS ${RTX_TENSORRT_RUNTIME_DLLS})
    foreach(_dll IN LISTS RTX_TENSORRT_RUNTIME_DLLS)
        get_filename_component(_name "${_dll}" NAME)
        if(_name STREQUAL "nvinfer_10.dll" OR _name STREQUAL "nvonnxparser_10.dll")
            list(APPEND RTX_NVIDIA_REQUIRED_RUNTIME_FILES "${_name}")
        endif()
    endforeach()
endif()

if(RTX_NVIDIA_RUNTIME_DLLS)
    list(REMOVE_DUPLICATES RTX_NVIDIA_RUNTIME_DLLS)
    list(REMOVE_DUPLICATES RTX_NVIDIA_REQUIRED_RUNTIME_FILES)
endif()
