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
};

/* Returns RTX_OK when the kernel launched on `stream`.  Both functions bind
 * the retained CUDA primary context before launching; callers still own
 * stream-level ordering. */
RtxResult launchPreprocess(const PreprocessLaunch& launch, void* stream);
RtxResult launchPackV2(const PackV2Launch& launch, void* stream);

/* Last kernel-launch diagnostic (thread-safe mirror of
 * rtx_ngx_pack_last_error). */
const char* packLastError();

}  // namespace rtx_ngx

#endif /* RTX_NGX_BRIDGE_P010_PACK_RUNTIME_H */
