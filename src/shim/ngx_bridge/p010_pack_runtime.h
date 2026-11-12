/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Internal (non-ABI) launch entry points for the remaster kernels that live
 * in the same DLL.  bridge_exports.cpp uses these to run the preprocess and
 * pack v2 kernels on the interop pack stream; they are not exported.
 */

#ifndef RTX_NGX_BRIDGE_P010_PACK_RUNTIME_H
#define RTX_NGX_BRIDGE_P010_PACK_RUNTIME_H

#include <cstdint>

#include "ngx_bridge_abi.h"

namespace rtx_ngx {

struct PreprocessLaunch {
    uint64_t y_plane = 0;        /* CUdeviceptr */
    uint64_t uv_plane = 0;       /* CUdeviceptr */
    uint64_t output_surface = 0; /* CUsurfObject of the shared R10G10B10A2 input */
    int32_t width = 0;
    int32_t height = 0;
    int32_t y_pitch_bytes = 0;
    int32_t uv_pitch_bytes = 0;
    int32_t format = 0;       /* RTX_PIXEL_NV12 / RTX_PIXEL_P010 */
    int32_t full_range = 0;   /* 0 = limited */
    int32_t matrix = 0;       /* 0 = BT.709, 1 = BT.2020 NCL, 2 = BT.601 */
};

struct PackV2Launch {
    uint64_t input_surface = 0; /* CUsurfObject of the shared RGBA16F output */
    uint64_t y_plane = 0;       /* CUdeviceptr, uint16 elements */
    uint64_t uv_plane = 0;      /* CUdeviceptr, interleaved UV, uint16 elements */
    uint32_t y_pitch_samples = 0;
    uint32_t uv_pitch_samples = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t input_is_pq = 0;
    int32_t input_is_scrgb = 0;     /* 1 = linear scRGB, BT.709 primaries */
    float linear_scale_nits = 80.0f; /* TrueHDR FP16 output scale */
    /* Phase 7 fused stages (Task 7-A2): FP16 3D LUT texture, the device
     * uint32[4] statistics slots and the 960x540 R10G10B10A2 A/B preview. */
    uint64_t lut_tex = 0;            /* CUtexObject, 0 = no LUT */
    int32_t lut_dim = 0;             /* LUT grid size (17/33/64/65), 0 = none */
    uint64_t stats_devptr = 0;       /* CUdeviceptr of uint32[4], 0 = skip */
    uint64_t preview_ptr = 0;        /* CUdeviceptr R10G10B10A2, 0 = skip */
    int32_t preview_width = 0;
    int32_t preview_height = 0;
    int32_t preview_pitch_px = 0;    /* 32-bit words per preview row */
    float split_x_norm = -1.0f;      /* <0 disables the A/B split */
    uint64_t src_surface = 0;        /* CUsurfObject of the ORIGINAL R10G10B10A2 */
};

/* Phase 9 (Task 9-A1): Linux headless single-launch remaster kernel. */
struct FusedRemasterLaunch {
    uint64_t src_y = 0;          /* CUdeviceptr of the decoded NV12/P010 luma */
    uint64_t src_uv = 0;         /* CUdeviceptr of the interleaved chroma */
    uint32_t src_y_pitch = 0;    /* bytes */
    uint32_t src_uv_pitch = 0;   /* bytes */
    int32_t src_format = 0;      /* 1 = NV12, 2 = P010 */
    int32_t src_full_range = 0;
    int32_t src_matrix = 0;      /* 0 = BT.709, 1 = BT.2020 NCL, 2 = BT.601 */
    uint64_t dst_y = 0;          /* CUdeviceptr of the 4K P010 luma plane */
    uint64_t dst_uv = 0;
    uint32_t dst_y_pitch_samples = 0;
    uint32_t dst_uv_pitch_samples = 0;
    int32_t src_width = 0;
    int32_t src_height = 0;
    int32_t dst_width = 0;
    int32_t dst_height = 0;
    uint64_t lut_tex = 0;
    int32_t lut_dim = 0;
    int32_t vsr_quality = 2;     /* 1..4, drives the CAS amplitude */
    int32_t enable_itm = 1;      /* BT.2446 Method C inverse tone mapping */
    float max_luminance = 1000.0f;
    float y_norm_prev = 0.0f;    /* previous frame adaptive statistic */
    uint64_t stats_devptr = 0;
};

/* Returns RTX_OK when the kernel launched on `stream`.  Both functions bind
 * the retained CUDA primary context before launching; callers still own
 * stream-level ordering. */
RtxResult launchPreprocess(const PreprocessLaunch& launch, void* stream);
RtxResult launchPackV2(const PackV2Launch& launch, void* stream);
RtxResult launchFusedRemaster(const FusedRemasterLaunch& launch, void* stream);
RtxResult launchInterpolateNv12(uint64_t prev_nv12, uint64_t curr_nv12,
                               uint32_t pitch, uint32_t width, uint32_t height,
                               float alpha, uint64_t out_nv12, void* stream);

/* Last kernel-launch diagnostic (thread-safe mirror of
 * rtx_ngx_pack_last_error). */
const char* packLastError();

}  // namespace rtx_ngx

#endif /* RTX_NGX_BRIDGE_P010_PACK_RUNTIME_H */
