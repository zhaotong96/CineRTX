# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# File: cmake/EnsureRifeEngine.cmake
# Purpose: Ensure official RIFE v4.25 TensorRT .engine is present after CineRTX builds.
#          Compiles it automatically using CineRTX --build-rife-engine if missing,
#          guaranteeing a 100% painless zero-delay out-of-the-box user experience.

if(NOT DEFINED EXE_DIR OR NOT DEFINED EXT_MODELS_DIR)
    message(FATAL_ERROR "[EnsureRifeEngine] EXE_DIR and EXT_MODELS_DIR must be specified.")
endif()

set(_dest_engine "${EXE_DIR}/models/rife_v4.25.engine")
set(_cache_engine "${EXT_MODELS_DIR}/rife_v4.25.engine")
set(_dest_onnx "${EXE_DIR}/models/rife_v4.25.onnx")
set(_cache_onnx "${EXT_MODELS_DIR}/rife_v4.25.onnx")

# 1. If destination engine already exists and valid (>= 5 MB), nothing to do!
if(EXISTS "${_dest_engine}")
    file(SIZE "${_dest_engine}" _sz)
    if(_sz GREATER_EQUAL 5242880)
        # Also ensure external cache has it for future rebuilds
        if(NOT EXISTS "${_cache_engine}")
            file(COPY_FILE "${_dest_engine}" "${_cache_engine}")
        endif()
        return()
    endif()
endif()

# 2. If external cache has a valid engine, copy it into destination
if(EXISTS "${_cache_engine}")
    file(SIZE "${_cache_engine}" _csz)
    if(_csz GREATER_EQUAL 5242880)
        message(STATUS "[CineRTX] Deploying cached RIFE v4.25 TensorRT engine to ${_dest_engine} ...")
        file(COPY_FILE "${_cache_engine}" "${_dest_engine}")
        return()
    endif()
endif()

# 3. Neither exists. Locate ONNX source
if(NOT EXISTS "${_dest_onnx}")
    if(EXISTS "${_cache_onnx}")
        file(COPY_FILE "${_cache_onnx}" "${_dest_onnx}")
    endif()
endif()

if(NOT EXISTS "${_dest_onnx}")
    message(STATUS "[CineRTX] RIFE v4.25 ONNX model not found; skipping automatic .engine compilation.")
    return()
endif()

# 4. We have CineRTX.exe and rife_v4.25.onnx, but no .engine!
# Compile it now during POST_BUILD so the user NEVER faces a 3-minute freeze at runtime!
set(_cinertx_exe "${EXE_DIR}/CineRTX.exe")
if(NOT EXISTS "${_cinertx_exe}")
    return()
endif()

message(STATUS "================================================================================")
message(STATUS "[CineRTX] Compiling RIFE v4.25 TensorRT engine for this GPU (one-time build step)...")
message(STATUS "  Executable: ${_cinertx_exe}")
message(STATUS "  ONNX:       ${_dest_onnx}")
message(STATUS "  Output:     ${_dest_engine}")
message(STATUS "  Note:       TensorRT optimizer is selecting fastest kernel tactics (~1-2 mins)...")
message(STATUS "================================================================================")

execute_process(
    COMMAND "${_cinertx_exe}"
            --build-rife-engine
            --onnx "${_dest_onnx}"
            --engine "${_dest_engine}"
            --opt-w 1920
            --opt-h 1080
    WORKING_DIRECTORY "${EXE_DIR}"
    RESULT_VARIABLE _build_rc
    OUTPUT_VARIABLE _build_out
    ERROR_VARIABLE _build_err
)

if(_build_rc EQUAL 0 AND EXISTS "${_dest_engine}")
    file(SIZE "${_dest_engine}" _final_sz)
    if(_final_sz GREATER_EQUAL 5242880)
        message(STATUS "[CineRTX] SUCCESS: RIFE v4.25 TensorRT engine compiled (${_final_sz} bytes).")
        file(COPY_FILE "${_dest_engine}" "${_cache_engine}")
        return()
    endif()
endif()

message(WARNING
    "[CineRTX] Automated RIFE engine compilation returned code ${_build_rc}.\n"
    "Details: ${_build_err}\n"
    "CineRTX will continue to build normally. If running in a headless environment without an NVIDIA GPU,\n"
    "the engine will be synthesized or probed when an RTX GPU becomes active."
)
