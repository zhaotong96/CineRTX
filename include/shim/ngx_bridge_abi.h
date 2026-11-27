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

#define RTX_NGX_BRIDGE_ABI_VERSION 5u

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
    int32_t output_offset_x;  /* 0 for full canvas, >0 for pillarbox left offset */
    int32_t output_offset_y;  /* 0 for full canvas, >0 for letterbox top offset */
    int32_t scaled_width;     /* 0 = full output_width */
    int32_t scaled_height;    /* 0 = full output_height */
    uint8_t reserved[48];
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
    /* ABI v3 (Phase 7, Task 7-A1): the former int32_t reserved[5] tail is
     * remapped onto named fields with the identical total struct size and the
     * identical field offsets, so the wire layout is byte-compatible. */
    uint64_t preview_rgba10_ptr; /* CUdeviceptr of a 960x540 R10G10B10A2 buffer
                                  * (rtx_ngx_bridge_preview_buffer), 0 = skip */
    uint32_t preview_pitch;      /* bytes per preview row */
    float split_x_norm;          /* 0.0..1.0 A/B split (<0 disables split) */
    int32_t reset_temporal;      /* 0 = maintain history, 1 = reset temporal history (scene cut) */
} RtxNgxRemasterInput;

typedef struct RtxNgxRemasterOutput {
    uint64_t y_plane;      /* CUdeviceptr, bridge-owned, valid until the next remaster call or bridge destroy */
    uint64_t uv_plane;     /* CUdeviceptr */
    uint32_t y_pitch;      /* bytes */
    uint32_t uv_pitch;     /* bytes */
    int32_t width;
    int32_t height;
    int32_t format;        /* RTX_PIXEL_P010 */
    /* ABI v3 (Phase 7, Task 7-A1): real GPU-reduced frame luma statistics and
     * the 3-slot ring index, mapped onto the former reserved[5] tail with an
     * unchanged struct size. */
    uint32_t frame_max_nits;   /* real GPU-reduced MaxCLL of this frame */
    uint32_t frame_avg_nits;   /* real GPU-reduced frame-average luminance */
    uint32_t ring_slot_index;  /* 0..2 in the 3-slot P010 ring */
    int32_t reserved[2];
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

/* ------------------------------------------------------------------------- */
/* ABI v3 append-only extensions (Phase 7, Task 7-A1).                        */
/* ------------------------------------------------------------------------- */

/* Uploads an N x N x N RGBA16F (half4) 3D LUT table into a CUDA 3D texture
 * object.  Passing lut_half4_data == NULL or grid_size == 0 unbinds and frees
 * the active LUT.  The LUT texture lives in the bridge's retained CUDA
 * context and is consumed by the fused pack kernel on the next remaster. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_set_lut3d(
    RtxNgxBridge* bridge, const uint16_t* lut_half4_data, int32_t grid_size);

/* Returns the bridge-allocated 3-slot 960x540 R10G10B10A2 shared preview
 * buffer ring (3 x 2.07 MB = 6.22 MB of VRAM total; ABI v5 upgraded from the
 * former 2-slot plain-CUDA ring to shared D3D12 buffers).  The GUI passes one
 * slot pointer per remaster through RtxNgxRemasterInput::preview_rgba10_ptr.
 * slot is 0..2; device_ptr == 0 means the preview path is unavailable. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_preview_buffer(
    RtxNgxBridge* bridge, uint32_t slot, uint64_t* device_ptr, uint32_t* pitch);

/* Legacy host-download path (<= 15 Hz synchronous cuMemcpyDtoH).  Superseded
 * by rtx_ngx_bridge_borrow_preview in ABI v5; retained for ABI stability and
 * off-GPU diagnostics only.  Product code must not call it. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_download_preview(
    RtxNgxBridge* bridge, void* destination, size_t destination_bytes);

/* Phase 8 (Task 8-B1, append-only): uploads one host-resident P010 frame
 * (software-decoded ProRes/DNxHR) into a bridge-owned device buffer and
 * returns the CUdeviceptr pair valid until the next upload or bridge
 * destroy.  The traffic is accounted as host pixel upload by the caller. */
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_upload_frame(
    RtxNgxBridge* bridge, const void* y_plane, const void* uv_plane, uint32_t y_pitch,
    uint32_t uv_pitch, int32_t width, int32_t height, uint64_t* y_device,
    uint64_t* uv_device);

/* Phase 8 Rework (Track B): NVOFA hardware optical flow and motion-interpolated
 * NV12 frame synthesis.  Interpolates between prev_nv12 and curr_nv12 at
 * temporal phase alpha (0..1) directly into out_nv12 using the bridge's
 * retained CUDA context and Driver API. */
RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_interpolate_nv12(
    RtxNgxBridge* bridge, uint64_t prev_nv12, uint64_t curr_nv12,
    uint32_t pitch, float alpha, uint64_t out_nv12);

RTX_SHIM_API uint64_t RTX_SHIM_CALL rtx_ngx_bridge_get_interp_buffer(
    RtxNgxBridge* bridge, uint32_t width, uint32_t height);

/* Phase 8 路线 B append-only ABI 扩展：TensorRT RIFE 深度学习补帧 */

RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_init_rife(
    RtxNgxBridge* bridge,
    const char* engine_path,    /* 预编译 .engine 文件路径 */
    uint32_t width,
    uint32_t height
);
RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_interpolate_rife(
    RtxNgxBridge* bridge,
    uint64_t prev_nv12,         /* CUdeviceptr: 前一帧 NV12 */
    uint64_t curr_nv12,         /* CUdeviceptr: 当前帧 NV12 */
    uint32_t pitch,             /* NV12 行距 (bytes) */
    float alpha,                /* 时间相位 in (0, 1) */
    uint64_t out_nv12,          /* CUdeviceptr: 输出 NV12 缓冲 */
    int32_t full_range,         /* 0=Limited Range, 1=Full Range */
    int32_t* scene_cut          /* out: 1=检测到场景硬切，已旁路推理 */
);
RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_build_rife_engine(
    const char* onnx_path,      /* 输入 ONNX 模型路径 */
    const char* engine_out_path,/* 输出 .engine 文件路径 */
    uint32_t width,
    uint32_t height,
    char* err_buf,              /* 错误消息缓冲区 */
    uint32_t err_cap            /* err_buf 容量 */
);
RTX_SHIM_API void RTX_SHIM_CALL rtx_ngx_bridge_destroy_rife(
    RtxNgxBridge* bridge
);
/* Returns 1 if the last NVOFA interpolate_nv12 call detected a hard scene cut
 * (atomically resets the latched flag to 0 on read). */
RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_consume_nvof_scene_cut(
    RtxNgxBridge* bridge
);
/* ------------------------------------------------------------------------- */
/* ABI v5 append-only extensions (Phase 11.2, C-02/C-03).                     */
/*                                                                           */
/* The preview ring is upgraded from plain cuMemAllocPitch device memory to a */
/* 3-slot D3D12_RESOURCE_DIMENSION_BUFFER ring (D3D12_HEAP_FLAG_SHARED,       */
/* initial state D3D12_RESOURCE_STATE_COMMON) so the DirectComposition        */
/* viewport can borrow the real ID3D12Resource and read it as a Buffer<uint>  */
/* SRV: the transcode/production path writes mapped CUdeviceptrs and the      */
/* monitor path renders them with zero host pixel traffic in both directions. */
/*                                                                           */
/* RtxNgxBridgeConfig.reserved[0] = 1 switches rtx_ngx_bridge_create into the */
/* lightweight headless playback mode: no NGX evaluator is initialised and a  */
/* 3-slot ring sized exactly input_width x input_height is created eagerly.   */
/* This is the realtime MediaPlaybackSession zero-readback presentation path. */
/* ------------------------------------------------------------------------- */

/* Returns the D3D12 device and the shared ring resource behind `slot`.
 * slot is 0..2; out_d3d12_resource == NULL means the ring is unavailable.
 * The caller AddRefs both pointers for as long as it uses them (the viewport
 * retire path releases them on fence completion). */
RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_borrow_preview(
    RtxNgxBridge* bridge,
    uint32_t slot,
    void** out_d3d12_device,
    void** out_d3d12_resource,
    uint32_t* out_pitch);

/* Packs one decoded NV12/P010 frame (device pointers from the shared primary
 * CUDA context) into ring slot `slot` as R10G10B10A2 words through the
 * rtx_yuv_to_rgb10_buf kernel and synchronises the pack stream before
 * returning: on RTX_OK the slot buffer is fully written and safe for the
 * viewport's D3D12 queue to read.  Non-terminal: never blocks the decode
 * pipeline beyond the pack kernel itself. */
RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_playback_pack(
    RtxNgxBridge* bridge,
    uint64_t y_plane,
    uint64_t uv_plane,
    uint32_t y_pitch,
    uint32_t uv_pitch,
    uint32_t width,
    uint32_t height,
    int32_t format,        /* RTX_PIXEL_NV12 / RTX_PIXEL_P010 */
    int32_t full_range,    /* 0 = limited, 1 = full */
    int32_t matrix,        /* 0 = BT.709, 1 = BT.2020 NCL, 2 = BT.601 */
    uint32_t slot);


/* ------------------------------------------------------------------------- */
/* Phase 11.4 (C-06): RIFE scale=0.5 / A=128 / reflect101 extended init ABI  */
/* ------------------------------------------------------------------------- */
/* Phase 11.4: RtxRifeInitConfig for extended RIFE initialization */
typedef struct RtxRifeInitConfig {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    float scale;              /* 1.0f (full-res flow) or 0.5f (half-res flow for 4K) */
    uint32_t align_mod;       /* 0 = auto (128); guarantees 64-byte alignment after 2x flow downscale */
    int32_t reflect_padding;  /* 1 = reflect101 mirror padding, 0 = edge clamp */
    uint32_t reserved[6];
} RtxRifeInitConfig;

RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_init_rife_ex(
    RtxNgxBridge* bridge,
    const char* engine_path,
    const RtxRifeInitConfig* config,
    uint64_t* out_trt_peak_bytes);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* RTX_NGX_BRIDGE_ABI_H */
