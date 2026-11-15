/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * NOTE: The deployed artifact is src/shim/ngx_bridge/rife_pack.ptx (embedded
 * into rife_pack_ptx.h and JIT-loaded by rife_pack_runtime.cpp at runtime).
 * This file is an equivalent CUDA reference mirror of that PTX for human
 * review; it is NOT compiled by the build system.
 *
 * Phase 9 ABI (Track 9A):
 *   - Both NV12 inputs/outputs use a single base pointer; the UV plane lives
 *     at byte offset height * pitch from the base (NV12 layout).
 *   - Out-of-bounds padded pixels are filled by clamping the source
 *     coordinates (edge replication), never by reflective indexing.
 *   - The SAD reduction is executed uniformly by every thread of the warp
 *     (out-of-range pixels contribute 0) so shfl.sync never runs divergent.
 *   - All BT.709 conversion constants are annotated with their IEEE-754
 *     hexadecimal form exactly as encoded in the deployed PTX.
 *
 * Phase 9.1 (RW-P9-05): both pre/post kernels take a uniform full_range
 * selector; full == 1 uses the BT.601 full-range matrix (Y = 0.299R +
 * 0.587G + 0.114B over 0..255 and the matching chroma derivations), while
 * full == 0 keeps the frozen BT.709 limited-range path. The branch is uniform
 * (kernel parameter) so the warp never diverges.
 *
 * Phase 9.1 (RW-P9-02): rife_fp32_to_fp16 / rife_fp16_to_fp32 mirror the
 * element-wise IEEE-754 binary32 <-> binary16 conversion kernels backing the
 * kHALF engine I/O chain (grid (count+255)/256, block 256).
 */

#include <cuda_runtime.h>
#include <cstdint>

namespace {

__device__ __forceinline__ int clampi(int idx, int limit) {
    return (idx < limit) ? idx : (limit - 1);
}

__device__ __forceinline__ float clamp01(float v) {
    return fminf(fmaxf(v, 0.0f), 1.0f);
}

/* IEEE-754 hex encodings (identical to the deployed PTX):
 *   16.0f        = 0f41800000      128.0f      = 0f43000000
 *   0.00456621f  = 0f3B95A106      0.00703125f = 0f3BE66666
 *  -0.00083610f  = 0fBA5B2574     -0.00208984f = 0fBB08F5C3
 *   0.00828125f  = 0f3C07AE14      1.402f      = 0f3FB374BC
 *  -0.344136f    = 0fBEB03298     -0.714136f   = 0fBF36D19E
 *   1.772f       = 0f3FE2D0E5      1/255f      = 0f3B808081
 */
__device__ __forceinline__ void to_rgb_limited(uint8_t y, uint8_t u, uint8_t v,
                                               float& r, float& g, float& b) {
    const float y_norm = (static_cast<float>(y) - 16.0f) * 0.00456621f;
    const float u_diff = static_cast<float>(u) - 128.0f;
    const float v_diff = static_cast<float>(v) - 128.0f;
    r = clamp01(y_norm + 0.00703125f * v_diff);
    g = clamp01(y_norm - 0.00083610f * u_diff - 0.00208984f * v_diff);
    b = clamp01(y_norm + 0.00828125f * u_diff);
}

__device__ __forceinline__ void to_rgb_full(uint8_t y, uint8_t u, uint8_t v,
                                            float& r, float& g, float& b) {
    const float y_full = static_cast<float>(y);
    const float u_diff = static_cast<float>(u) - 128.0f;
    const float v_diff = static_cast<float>(v) - 128.0f;
    const float r255 = y_full + 1.402f * v_diff;
    const float g255 = y_full - 0.344136f * u_diff - 0.714136f * v_diff;
    const float b255 = y_full + 1.772f * u_diff;
    r = clamp01(r255 * (1.0f / 255.0f));
    g = clamp01(g255 * (1.0f / 255.0f));
    b = clamp01(b255 * (1.0f / 255.0f));
}

}  // namespace

extern "C" __global__ void rife_nv12_to_nchw_7ch_fp32(
    const uint8_t* __restrict__ prev_nv12,   /* single NV12 base pointer */
    const uint8_t* __restrict__ curr_nv12,   /* single NV12 base pointer */
    unsigned int pitch,
    unsigned int width,
    unsigned int height,
    unsigned int padded_width,
    unsigned int padded_height,
    unsigned int full_range,
    float alpha,
    float* __restrict__ dst_tensor,          /* [1, 7, padded_height, padded_width] */
    unsigned long long* __restrict__ sad_accum) {

    const int px = blockIdx.x * blockDim.x + threadIdx.x;
    const int py = blockIdx.y * blockDim.y + threadIdx.y;

    if (px >= static_cast<int>(padded_width) || py >= static_cast<int>(padded_height)) return;

    /* Clamp to [0, width-1] / [0, height-1]: edge replication fill. */
    const int sx = clampi(px, static_cast<int>(width));
    const int sy = clampi(py, static_cast<int>(height));

    const int y_offset = sy * static_cast<int>(pitch) + sx;
    const int uv_offset = (static_cast<int>(height) + sy / 2) * static_cast<int>(pitch) + (sx & ~1);

    const uint8_t y0 = prev_nv12[y_offset];
    const uint8_t u0 = prev_nv12[uv_offset];
    const uint8_t v0 = prev_nv12[uv_offset + 1];

    const uint8_t y1 = curr_nv12[y_offset];
    const uint8_t u1 = curr_nv12[uv_offset];
    const uint8_t v1 = curr_nv12[uv_offset + 1];

    int diff = 0;
    if (px < static_cast<int>(width) && py < static_cast<int>(height)) {
        diff = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    }
    #pragma unroll
    for (int offset = 16; offset > 0; offset /= 2) {
        diff += __shfl_down_sync(0xffffffff, diff, offset);
    }
    const int lane_id = (threadIdx.y * blockDim.x + threadIdx.x) & 31;
    if (lane_id == 0 && diff > 0 && sad_accum != nullptr) {
        atomicAdd(sad_accum, static_cast<unsigned long long>(diff));
    }

    float r0, g0, b0, r1, g1, b1;
    if (full_range != 0) {
        to_rgb_full(y0, u0, v0, r0, g0, b0);
        to_rgb_full(y1, u1, v1, r1, g1, b1);
    } else {
        to_rgb_limited(y0, u0, v0, r0, g0, b0);
        to_rgb_limited(y1, u1, v1, r1, g1, b1);
    }

    const int plane_size = static_cast<int>(padded_width * padded_height);
    const int pixel_idx = py * static_cast<int>(padded_width) + px;

    dst_tensor[0 * plane_size + pixel_idx] = r0;
    dst_tensor[1 * plane_size + pixel_idx] = g0;
    dst_tensor[2 * plane_size + pixel_idx] = b0;
    dst_tensor[3 * plane_size + pixel_idx] = r1;
    dst_tensor[4 * plane_size + pixel_idx] = g1;
    dst_tensor[5 * plane_size + pixel_idx] = b1;
    dst_tensor[6 * plane_size + pixel_idx] = alpha;
}

extern "C" __global__ void rife_nchw_3ch_fp32_to_nv12(
    const float* __restrict__ src_tensor,    /* [1, 3, padded_height, padded_width] */
    uint8_t* __restrict__ dst_nv12,          /* single NV12 base pointer */
    unsigned int pitch,
    unsigned int width,
    unsigned int height,
    unsigned int padded_width,
    unsigned int padded_height,
    unsigned int full_range) {

    const int px = blockIdx.x * blockDim.x + threadIdx.x;
    const int py = blockIdx.y * blockDim.y + threadIdx.y;

    if (px >= static_cast<int>(width) || py >= static_cast<int>(height)) return;

    const int plane_size = static_cast<int>(padded_width * padded_height);
    const int pixel_idx = py * static_cast<int>(padded_width) + px;

    const float r = clamp01(src_tensor[0 * plane_size + pixel_idx]);
    const float g = clamp01(src_tensor[1 * plane_size + pixel_idx]);
    const float b = clamp01(src_tensor[2 * plane_size + pixel_idx]);

    const int uv_offset =
        (static_cast<int>(height) + py / 2) * static_cast<int>(pitch) + px;

    if (full_range != 0) {
        /* BT.601 full range over 0..255. */
        const float y_val = fminf(fmaxf(
            0.299f * r + 0.587f * g + 0.114f * b, 0.0f), 255.0f);
        dst_nv12[py * static_cast<int>(pitch) + px] = static_cast<uint8_t>(y_val);
        if ((px & 1) != 0 || (py & 1) != 0) return;
        const float u_val = fminf(fmaxf(
            128.0f - 0.168736f * r - 0.331264f * g + 0.5f * b, 0.0f), 255.0f);
        const float v_val = fminf(fmaxf(
            128.0f + 0.5f * r - 0.418688f * g - 0.081312f * b, 0.0f), 255.0f);
        dst_nv12[uv_offset] = static_cast<uint8_t>(u_val);
        dst_nv12[uv_offset + 1] = static_cast<uint8_t>(v_val);
        return;
    }

    /* BT.709 limited range over [16..235] / [16..240]. */
    const float y_val = fminf(fmaxf(
        16.5f + 46.5588f * r + 156.6288f * g + 15.8124f * b, 16.0f), 235.0f);
    dst_nv12[py * static_cast<int>(pitch) + px] = static_cast<uint8_t>(y_val);
    if ((px & 1) != 0 || (py & 1) != 0) return;
    const float u_val = fminf(fmaxf(
        128.5f - 25.6642f * r - 86.3358f * g + 112.0f * b, 16.0f), 240.0f);
    const float v_val = fminf(fmaxf(
        128.5f + 112.0f * r - 101.7303f * g - 10.2697f * b, 16.0f), 240.0f);
    dst_nv12[uv_offset] = static_cast<uint8_t>(u_val);
    dst_nv12[uv_offset + 1] = static_cast<uint8_t>(v_val);
}

extern "C" __global__ void rife_fp32_to_fp16(
    const float* __restrict__ src,
    __half* __restrict__ dst,
    unsigned int count) {
    const unsigned int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;
    dst[idx] = __float2half_rn(src[idx]);
}

extern "C" __global__ void rife_fp16_to_fp32(
    const __half* __restrict__ src,
    float* __restrict__ dst,
    unsigned int count) {
    const unsigned int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;
    dst[idx] = __half2float(src[idx]);
}
