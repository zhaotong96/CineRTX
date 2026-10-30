/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Minimal <cuda_runtime_api.h> for hosts without a CUDA toolkit.
 *
 * TensorRT's NvInferRuntimeBase.h includes <cuda_runtime_api.h> unconditionally,
 * yet the 10.13 headers only use the two opaque handle typedefs below in their
 * declarations: cudaStream_t for enqueueV3()/read()/setProfileStream(), and
 * cudaEvent_t for setInputConsumedEvent().  No CUDA runtime function is declared
 * or linked here - the engine reaches the driver through nvcuda.dll, and the
 * RIFE pack kernels are compiled by nvcc only when a toolkit is installed.
 *
 * This directory is added to the include path only when no CUDA toolkit was
 * found; a real toolkit always wins (RTX_TENSORRT_CUDA_INCLUDE_DIR in
 * cmake/RtxNvidiaSdks.cmake).  If a future TensorRT release starts using
 * cudaError_t or an enum from the runtime API, the build fails here instead of
 * silently guessing, which is the intended outcome.
 */

#ifndef CINERTX_CUDA_RUNTIME_API_SHIM_H
#define CINERTX_CUDA_RUNTIME_API_SHIM_H

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct CUstream_st* cudaStream_t;
typedef struct CUevent_st* cudaEvent_t;

#if defined(__cplusplus)
}
#endif

#endif  // CINERTX_CUDA_RUNTIME_API_SHIM_H
