/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Pure C ABI firewall for the frame-rate up-conversion (FRUC) bridge.
 *
 * The GPL core links nothing from NVIDIA and includes nothing from NVIDIA.
 * It sees only this header: fixed-width scalars, opaque handles and a versioned
 * function table.  rtx_fruc_bridge.dll resolves NvOFFRUC.dll (optical flow),
 * nvinfer / nvonnxparser (TensorRT RIFE) and nvcuda.dll at run time with
 * LoadLibraryExW, so the PE import table of every Zone A binary stays clean.
 *
 * ABI rules:
 *   * every field is fixed width; no bool, no size_t, no pointer arithmetic
 *     across the boundary;
 *   * the struct layouts are append-only, and the table is replaced wholesale
 *     when RTX_FRUC_BRIDGE_ABI_VERSION changes;
 *   * the caller owns every buffer it passes in, including err_buf.
 */

#ifndef RTX_SHIM_FRUC_BRIDGE_ABI_H
#define RTX_SHIM_FRUC_BRIDGE_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTX_FRUC_BRIDGE_ABI_VERSION 2u

#if defined(_WIN32)
#if defined(RTX_FRUC_BRIDGE_EXPORTS)
#define RTX_FRUC_EXPORT __declspec(dllexport)
#elif defined(RTX_FRUC_BRIDGE_STATIC)
/* Single-EXE build: the bridge is linked into the image, so neither dllexport
 * nor dllimport applies.  Mirrors RTX_SHIM_STATIC in rtx_shim_abi.h. */
#define RTX_FRUC_EXPORT
#else
#define RTX_FRUC_EXPORT __declspec(dllimport)
#endif
#else
#define RTX_FRUC_EXPORT __attribute__((visibility("default")))
#endif

/* Status codes.  Zero is success; every failure is negative so that
 * `result < 0` is the single error test. */
typedef enum RtxFrucStatus {
    RTX_FRUC_OK = 0,
    RTX_FRUC_ERR_INVALID_ARGUMENT = -1,
    RTX_FRUC_ERR_ABI_MISMATCH = -2,
    RTX_FRUC_ERR_UNAVAILABLE = -3,   /* vendor module not installed / not loadable */
    RTX_FRUC_ERR_CUDA = -4,
    RTX_FRUC_ERR_OUT_OF_MEMORY = -5,
    RTX_FRUC_ERR_STATE = -6,
    RTX_FRUC_ERR_UNSUPPORTED = -7
} RtxFrucStatus;

typedef struct RtxFrucBridge RtxFrucBridge;

#if defined(__cplusplus)
enum RtxFrucBackendType : uint32_t {
    RTX_FRUC_BACKEND_NONE      = 0,
    RTX_FRUC_BACKEND_NVOFFRUC  = 1, /* path A: hardware optical-flow engine */
    RTX_FRUC_BACKEND_TRT_RIFE  = 2, /* path B: TensorRT 10.13+ RIFE v4.25 */
    RTX_FRUC_BACKEND_DUPLICATE = 3  /* hard-cut / static-frame duplicate bypass */
};
#else
typedef enum RtxFrucBackendType {
    RTX_FRUC_BACKEND_NONE      = 0,
    RTX_FRUC_BACKEND_NVOFFRUC  = 1,
    RTX_FRUC_BACKEND_TRT_RIFE  = 2,
    RTX_FRUC_BACKEND_DUPLICATE = 3
} RtxFrucBackendType;
#endif

typedef struct RtxFrucConfig {
    uint32_t abi_version;          /* must equal RTX_FRUC_BRIDGE_ABI_VERSION */
    uint32_t backend_type;         /* RtxFrucBackendType */
    uint32_t width;                /* input width, e.g. 1920 */
    uint32_t height;               /* input height, e.g. 1080 */
    uint32_t surface_pitch;        /* NV12 device pitch in bytes */
    uint32_t fp16_enabled;         /* 1 = Tensor Core mixed precision */
    const char* model_engine_path; /* serialised .engine path (RIFE only) */
    void* cuda_context;            /* CUcontext; NULL selects CPU reference mode */
    void* cuda_stream;             /* CUstream for the async interpolation */
} RtxFrucConfig;

typedef struct RtxFrucFrameInput {
    uint64_t prev_nv12_devptr;     /* frame N device pointer (CUdeviceptr) */
    uint64_t curr_nv12_devptr;     /* frame N+1 device pointer */
    uint64_t out_nv12_devptr;      /* reconstructed frame N+0.5 */
    uint64_t wait_prev_event;      /* CUevent signalled when frame N is ready */
    uint64_t wait_curr_event;      /* CUevent signalled when frame N+1 is ready */
    uint64_t signal_done_event;    /* CUevent to signal when interpolation ends */
    float timestep;                /* interpolation phase, default 0.5f */
    uint32_t is_scene_cut;         /* 1 = force the duplicate bypass */
    uint32_t is_static_frame;      /* 1 = fast duplicate bypass */
} RtxFrucFrameInput;

typedef struct RtxFrucBridgeApi {
    uint32_t abi_version;
    int32_t (*create)(const RtxFrucConfig* cfg, RtxFrucBridge** out_bridge, char* err_buf,
                      uint32_t err_cap);
    int32_t (*interpolate)(RtxFrucBridge* bridge, const RtxFrucFrameInput* in);
    int32_t (*build_engine)(const char* onnx_path, const char* engine_out_path, uint32_t width,
                            uint32_t height, uint32_t fp16, char* err_buf, uint32_t err_cap);
    uint64_t (*query_vram_footprint)(RtxFrucBridge* bridge);
    void (*destroy)(RtxFrucBridge* bridge);
} RtxFrucBridgeApi;

/* Returns the process-wide, immutable function table.  Never NULL: a bridge
 * that cannot load a vendor module still returns a valid table whose create()
 * reports RTX_FRUC_ERR_UNAVAILABLE, which keeps the caller free of null checks.
 * The returned pointer stays valid for the lifetime of the module. */
RTX_FRUC_EXPORT const RtxFrucBridgeApi* rtx_fruc_bridge_get_api(void);

/* Convenience: the ABI version this binary was built against.  The contract
 * test asserts it equals RTX_FRUC_BRIDGE_ABI_VERSION. */
RTX_FRUC_EXPORT uint32_t rtx_fruc_bridge_abi_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RTX_SHIM_FRUC_BRIDGE_ABI_H */
