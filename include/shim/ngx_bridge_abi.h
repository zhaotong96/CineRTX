/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * ABI for the isolated rtx_ngx_bridge.dll.  The bridge is the only component
 * that speaks D3D12, CUDA external-memory interop and NGX; it is licensed under
 * MIT OR Apache-2.0 and is loaded at runtime by the GPL core.
 */

#ifndef RTX_NGX_BRIDGE_ABI_H
#define RTX_NGX_BRIDGE_ABI_H

#include <stddef.h>
#include <stdint.h>

#include "rtx_shim_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTX_NGX_BRIDGE_ABI_VERSION 2u

typedef struct RtxNgxBridge RtxNgxBridge;

typedef struct RtxNgxBridgeConfig {
    uint8_t luid[8];
    uint32_t luid_node_mask;
    int32_t input_width;
    int32_t input_height;
    int32_t output_width;
    int32_t output_height;
    int32_t vsr_quality;      /* 1..4 */
    int32_t enable_truehdr;   /* 0/1 */
    int32_t contrast;         /* 100-based */
    int32_t saturation;       /* 100-based */
    int32_t middle_gray;      /* 100-based */
    int32_t max_luminance;    /* cd/m^2 */
    uint32_t project_id;
    uint8_t reserved[64];
} RtxNgxBridgeConfig;

typedef struct RtxNgxTextureDesc {
    uint32_t width;
    uint32_t height;
    int32_t format;       /* RtxPixelFormat value */
    int32_t allow_uav;
    uint64_t allocation_size;
} RtxNgxTextureDesc;

typedef struct RtxNgxBridgeCaps {
    int32_t vsr_available;
    int32_t truehdr_available;
    int32_t d3d12_device_created;
    int32_t cuda_interop_available;
    int32_t shared_input_textures;
    int32_t shared_output_textures;
    uint32_t ngx_version;
    char diagnostics[512];
} RtxNgxBridgeCaps;

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_query_caps(RtxNgxBridgeCaps* caps);
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_create(const RtxNgxBridgeConfig* config,
                                                           RtxNgxBridge** bridge);
RTX_SHIM_API void RTX_SHIM_CALL rtx_ngx_bridge_destroy(RtxNgxBridge* bridge);

/* Returns the shared textures that the CUDA side must import.  Handles are
 * Win32 HANDLE values; the caller must keep them alive for the bridge lifetime. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_get_textures(
    RtxNgxBridge* bridge, RtxNgxTextureDesc* input_desc, RtxNgxTextureDesc* output_desc,
    uint64_t* input_shared_handle, uint64_t* output_shared_handle);

/* Runs VSR followed by TrueHDR on the headless D3D12 queue.  The CUDA side has
 * already written the input texture and signalled `input_fence_value`; the
 * bridge signals `input_fence_value + 1` when the output texture is ready. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_process(RtxNgxBridge* bridge,
                                                            uint64_t input_fence_value);

/* Append-only ABI extension (v2 contract): exposes the shared ID3D12Fence as a
 * Win32 handle and the timeline value it has already consumed, so the CUDA side
 * can import it as an external semaphore and choose input_fence_value =
 * timeline + 1.  Without this getter the process() fence contract above could
 * not be honoured by a caller. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_get_fence(RtxNgxBridge* bridge,
                                                              uint64_t* fence_handle,
                                                              uint64_t* timeline_value);
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_flush(RtxNgxBridge* bridge);
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_reset(RtxNgxBridge* bridge,
                                                          int32_t width, int32_t height);
RTX_SHIM_API const char* RTX_SHIM_CALL rtx_ngx_bridge_last_error(RtxNgxBridge* bridge);

/* ------------------------------------------------------------------------- */
/* RGBA16F -> P010 pack kernel (W6)                                          */
/*                                                                           */
/* The kernel is shipped as PTX and JIT-compiled by the CUDA driver, so the   */
/* bridge never links cudart64_*.dll.  `input_surface` is a CUsurfObject      */
/* bound to the R16G16B16A16_FLOAT array that D3D12 exported; `y_plane` and   */
/* `uv_plane` are CUdeviceptr values inside the P010 encode pool.             */
/* ------------------------------------------------------------------------- */

typedef struct RtxNgxPackConfig {
    int32_t width;        /* luma width, even */
    int32_t height;       /* luma height, even */
    int32_t input_is_pq;  /* 1 when the surface already holds PQ values */
    int32_t reserved;
} RtxNgxPackConfig;

typedef struct RtxNgxPackSurfaces {
    uint64_t input_surface;   /* CUsurfObject */
    uint64_t y_plane;         /* CUdeviceptr, uint16 elements */
    uint64_t uv_plane;        /* CUdeviceptr, interleaved UV, uint16 elements */
    uint32_t y_pitch_samples;
    uint32_t uv_pitch_samples;
} RtxNgxPackSurfaces;

/* Returns RTX_OK when the embedded PTX loaded and the kernel was found. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_pack_available(void);RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_pack_launch(
    const RtxNgxPackSurfaces* surfaces, const RtxNgxPackConfig* config, uint64_t cuda_stream);
RTX_SHIM_API const char* RTX_SHIM_CALL rtx_ngx_pack_last_error(void);

/* Headless D3D12 <-> CUDA round trip.  Allocates a shared RGBA16F texture,
 * writes a gradient through D3D12, reads it through the imported CUsurfObject,
 * writes it back from CUDA and verifies the D3D12 readback.  Returns RTX_OK and
 * fills `detail` when every sample matches. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_selftest(int32_t width, int32_t height,
                                                             char* detail,
                                                             size_t detail_capacity);

/* ------------------------------------------------------------------------- */
/* Frame statistics for the content-adaptive TrueHDR law (expert B-2.2).      */
/*                                                                           */
/* Appended to the frozen ABI v2 contract under its append-only policy: the  */
/* struct and function are new, nothing existing changed.  The core feeds    */
/* per-frame luma statistics (from rtx::LumaHistogram / rtx::lumaStats) to   */
/* the bridge before rtx_ngx_bridge_process; without the proprietary SDK the */
/* call is a validated no-op.  Consumers must resolve the symbol optionally. */
/* ------------------------------------------------------------------------- */

typedef struct RtxNgxFrameStats {
    double y_norm;      /* mean luma normalised to [0, 1] */
    double p_high;      /* fraction of near-white samples (luma >= 235) */
    int32_t scene_cut;  /* 1 when scene_detect reported a cut on this frame */
    int32_t reserved[6];
} RtxNgxFrameStats;

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_set_frame_stats(RtxNgxBridge* bridge,
                                                                   const RtxNgxFrameStats* stats);

/* ------------------------------------------------------------------------- */
/* Production GPU-only remaster entry (P0-1, append-only ABI extension).     */
/*                                                                           */
/* rtx_ngx_bridge_remaster turns one decoded NV12/P010 frame that already    */
/* lives in device memory into one 4K P010 HDR10 frame, entirely on the GPU: */
/*   preprocess kernel (YUV -> R10G10B10A2 shared input texture)             */
/*   -> VSR upscale -> TrueHDR (RGBA16F linear scRGB, BT.709, 1.0 = 80 nit)  */
/*   -> pack v2 kernel (scRGB -> BT.2020 PQ limited P010)                    */
/* The P010 output buffer is owned by the bridge and reused across calls;    */
/* the caller hands it to NVENC and must not free it.  The call is           */
/* synchronous: on RTX_OK the output plane is fully written.                 */
/* ------------------------------------------------------------------------- */

typedef struct RtxNgxRemasterInput {
    uint64_t y_plane;      /* CUdeviceptr */
    uint64_t uv_plane;     /* CUdeviceptr */
    uint32_t y_pitch;      /* bytes */
    uint32_t uv_pitch;     /* bytes */
    int32_t format;        /* RTX_PIXEL_NV12 / RTX_PIXEL_P010 */
    int32_t full_range;    /* 0=limited, 1=full */
    int32_t matrix;        /* 0=BT.709, 1=BT.2020 NCL, 2=BT.601 */
    int32_t reserved[5];
} RtxNgxRemasterInput;

typedef struct RtxNgxRemasterOutput {
    uint64_t y_plane;      /* CUdeviceptr, bridge-owned, valid until the next remaster call or bridge destroy */
    uint64_t uv_plane;     /* CUdeviceptr */
    uint32_t y_pitch;      /* bytes */
    uint32_t uv_pitch;     /* bytes */
    int32_t width;
    int32_t height;
    int32_t format;        /* RTX_PIXEL_P010 */
    int32_t reserved[5];
} RtxNgxRemasterOutput;

/* Cumulative since bridge create.  Call after each remaster and diff against
 * the previous snapshot to obtain per-frame stage timings. */
typedef struct RtxNgxRemasterStats {
    double preprocess_ms;
    double ngx_ms;
    double pack_kernel_ms;
    double frame_ms;
    uint64_t frames;
    uint64_t pack_launches;
    uint64_t reserved[4];
} RtxNgxRemasterStats;

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_remaster(
    RtxNgxBridge* bridge,
    const RtxNgxRemasterInput* input,
    RtxNgxRemasterOutput* output,
    RtxNgxRemasterStats* stats);

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_get_remaster_stats(
    RtxNgxBridge* bridge, RtxNgxRemasterStats* stats);

/* Returns RTX_NGX_BRIDGE_ABI_VERSION so callers can reject a bridge older
 * than the remaster contract before resolving rtx_ngx_bridge_remaster. */
RTX_SHIM_API uint32_t RTX_SHIM_CALL rtx_ngx_bridge_abi_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RTX_NGX_BRIDGE_ABI_H */
