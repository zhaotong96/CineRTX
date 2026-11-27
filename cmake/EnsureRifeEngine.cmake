# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# File: cmake/EnsureRifeEngine.cmake
# Purpose: Ensure official RIFE v4.25 orientation-dedicated TensorRT .engine files are present.
#          Compiles rife_v4.25_landscape.engine and rife_v4.25_portrait.engine automatically
#          using CineRTX --build-rife-engine if missing (Phase 11.4-F PKG-F2).

if(NOT DEFINED EXE_DIR OR NOT DEFINED EXT_MODELS_DIR)
    message(FATAL_ERROR "[EnsureRifeEngine] EXE_DIR and EXT_MODELS_DIR must be specified.")
endif()

set(_dest_onnx "${EXE_DIR}/models/rife_v4.25.onnx")
set(_cache_onnx "${EXT_MODELS_DIR}/rife_v4.25.onnx")

# 1. Locate ONNX source
if(NOT EXISTS "${_dest_onnx}")
    if(EXISTS "${_cache_onnx}")
        file(COPY_FILE "${_cache_onnx}" "${_dest_onnx}")
    endif()
endif()

if(NOT EXISTS "${_dest_onnx}")
    message(STATUS "[CineRTX] RIFE v4.25 ONNX model not found; skipping automatic .engine compilation.")
    return()
endif()

set(_cinertx_exe "${EXE_DIR}/CineRTX.exe")
if(NOT EXISTS "${_cinertx_exe}")
    return()
endif()

# 2. Macro to ensure single orientation engine exists and is valid (< 32 MB and >= 5 MB)
macro(ensure_orientation_engine _name _opt_w _opt_h)
    set(_dest_eng "${EXE_DIR}/models/${_name}")
    set(_cache_eng "${EXT_MODELS_DIR}/${_name}")
    set(_need_build TRUE)

    if(EXISTS "${_dest_eng}")
        file(SIZE "${_dest_eng}" _sz)
        if(_sz GREATER_EQUAL 5242880 AND _sz LESS 33554432)
            if(NOT EXISTS "${_cache_eng}")
                file(COPY_FILE "${_dest_eng}" "${_cache_eng}")
            endif()
            set(_need_build FALSE)
        endif()
    endif()

    if(_need_build AND EXISTS "${_cache_eng}")
        file(SIZE "${_cache_eng}" _csz)
        if(_csz GREATER_EQUAL 5242880 AND _csz LESS 33554432)
            message(STATUS "[CineRTX] Deploying cached ${_name} to ${_dest_eng} ...")
            file(COPY_FILE "${_cache_eng}" "${_dest_eng}")
            set(_need_build FALSE)
        endif()
    endif()

    if(_need_build)
        message(STATUS "================================================================================")
        message(STATUS "[CineRTX] Compiling RIFE ${_name} for this GPU...")
        message(STATUS "  Profile (Opt): ${_opt_w}x${_opt_h}")
        message(STATUS "================================================================================")
        execute_process(
            COMMAND "${_cinertx_exe}"
                    --build-rife-engine
                    --onnx "${_dest_onnx}"
                    --engine "${_dest_eng}"
                    --opt-w ${_opt_w}
                    --opt-h ${_opt_h}
            WORKING_DIRECTORY "${EXE_DIR}"
            RESULT_VARIABLE _rc
            OUTPUT_VARIABLE _out
            ERROR_VARIABLE _err
        )
        if(_rc EQUAL 0 AND EXISTS "${_dest_eng}")
            file(SIZE "${_dest_eng}" _final_sz)
            if(_final_sz GREATER_EQUAL 5242880 AND _final_sz LESS 33554432)
                message(STATUS "[CineRTX] SUCCESS: ${_name} compiled (${_final_sz} bytes).")
                file(COPY_FILE "${_dest_eng}" "${_cache_eng}")
            endif()
        else()
            message(WARNING "[CineRTX] Automated build of ${_name} returned ${_rc}: ${_err}")
        endif()
    endif()
endmacro()

# Phase 13 (Wave 2-A, [DEC-011]): 4 discrete single-profile aspect-ratio buckets
ensure_orientation_engine("rife_v4.25_ultra.engine"  2560 1088)
ensure_orientation_engine("rife_v4.25_wide.engine"   1920 1088)
ensure_orientation_engine("rife_v4.25_square.engine" 1472 1472)
ensure_orientation_engine("rife_v4.25_tall.engine"   1088 1920)

