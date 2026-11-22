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

/* Phase 11.4 (C-06): reflect101 symmetric mirror indexing (BORDER_REFLECT_101).
 * Out-of-bounds padded pixels reflect cleanly across the border without black edges. */
__device__ __forceinline__ int reflect101i(int idx, int limit) {
    if (idx < limit) return idx;
    const int mirrored = 2 * (limit - 1) - idx;
    return (mirrored < 0) ? 0 : mirrored;
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
    unsigned long long* __restrict__ sad_accum,
    unsigned int infer_width,                /* Phase 11.4-F (PKG-G1): half-res width for scale=0.5 */
    unsigned int infer_height) {             /* Phase 11.4-F (PKG-G1): half-res height for scale=0.5 */

    const int px = blockIdx.x * blockDim.x + threadIdx.x;
    const int py = blockIdx.y * blockDim.y + threadIdx.y;

    if (px >= static_cast<int>(padded_width) || py >= static_cast<int>(padded_height)) return;

    const int iw = (infer_width > 0u) ? static_cast<int>(infer_width) : static_cast<int>(width);
    const int ih = (infer_height > 0u) ? static_cast<int>(infer_height) : static_cast<int>(height);
    const int sx_s = reflect101i(px, iw);
    const int sy_s = reflect101i(py, ih);

    uint8_t y0, u0, v0, y1, u1, v1;
    if (iw < static_cast<int>(width) || ih < static_cast<int>(height)) {
        /* Phase 11.4-F (PKG-G1): 2x2 box-average luma downsample + direct 1:1 4:2:0 chroma read. */
        const int x0 = reflect101i(sx_s * 2, static_cast<int>(width));
        const int x1 = reflect101i(sx_s * 2 + 1, static_cast<int>(width));
        const int ry0 = reflect101i(sy_s * 2, static_cast<int>(height));
        const int ry1 = reflect101i(sy_s * 2 + 1, static_cast<int>(height));
        const int ipitch = static_cast<int>(pitch);
        const float y0_avg = 0.25f * (static_cast<float>(prev_nv12[ry0 * ipitch + x0]) +
                                      static_cast<float>(prev_nv12[ry0 * ipitch + x1]) +
                                      static_cast<float>(prev_nv12[ry1 * ipitch + x0]) +
                                      static_cast<float>(prev_nv12[ry1 * ipitch + x1]));
        const float y1_avg = 0.25f * (static_cast<float>(curr_nv12[ry0 * ipitch + x0]) +
                                      static_cast<float>(curr_nv12[ry0 * ipitch + x1]) +
                                      static_cast<float>(curr_nv12[ry1 * ipitch + x0]) +
                                      static_cast<float>(curr_nv12[ry1 * ipitch + x1]));
        y0 = static_cast<uint8_t>(y0_avg + 0.5f);
        y1 = static_cast<uint8_t>(y1_avg + 0.5f);
        const int uv_y = (sy_s < static_cast<int>(height) / 2) ? sy_s : (static_cast<int>(height) / 2 - 1);
        const int uv_x = (sx_s * 2 < (static_cast<int>(width) & ~1)) ? (sx_s * 2) : ((static_cast<int>(width) & ~1) - 2);
        const int uv_offset = (static_cast<int>(height) + uv_y) * ipitch + uv_x;
        u0 = prev_nv12[uv_offset];
        v0 = prev_nv12[uv_offset + 1];
        u1 = curr_nv12[uv_offset];
        v1 = curr_nv12[uv_offset + 1];
    } else {
        const int y_offset = sy_s * static_cast<int>(pitch) + sx_s;
        const int uv_offset = (static_cast<int>(height) + sy_s / 2) * static_cast<int>(pitch) + (sx_s & ~1);
        y0 = prev_nv12[y_offset];
        u0 = prev_nv12[uv_offset];
        v0 = prev_nv12[uv_offset + 1];
        y1 = curr_nv12[y_offset];
        u1 = curr_nv12[uv_offset];
        v1 = curr_nv12[uv_offset + 1];
    }

    int diff = 0;
    if (px < iw && py < ih) {
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
    unsigned int full_range,
    unsigned int infer_width,                /* Phase 11.4-F (PKG-G1): half-res width for scale=0.5 */
    unsigned int infer_height) {             /* Phase 11.4-F (PKG-G1): half-res height for scale=0.5 */

    const int px = blockIdx.x * blockDim.x + threadIdx.x;
    const int py = blockIdx.y * blockDim.y + threadIdx.y;

    if (px >= static_cast<int>(width) || py >= static_cast<int>(height)) return;

    const int plane_size = static_cast<int>(padded_width * padded_height);
    const int iw = (infer_width > 0u) ? static_cast<int>(infer_width) : static_cast<int>(width);
    const int ih = (infer_height > 0u) ? static_cast<int>(infer_height) : static_cast<int>(height);

    float r, g, b;
    if (iw < static_cast<int>(width) || ih < static_cast<int>(height)) {
        /* Phase 11.4-F (PKG-G1): Half-pixel center-aligned 2D bilinear upsampling,
         * strictly clamped to [0, infer_width-1] x [0, infer_height-1] (never reads padding). */
        float fx = (static_cast<float>(px) + 0.5f) * (static_cast<float>(iw) / static_cast<float>(width)) - 0.5f;
        float fy = (static_cast<float>(py) + 0.5f) * (static_cast<float>(ih) / static_cast<float>(height)) - 0.5f;
        fx = fminf(fmaxf(fx, 0.0f), static_cast<float>(iw - 1));
        fy = fminf(fmaxf(fy, 0.0f), static_cast<float>(ih - 1));
        const int x0 = static_cast<int>(floorf(fx));
        const int y0 = static_cast<int>(floorf(fy));
        const int x1 = (x0 + 1 < iw) ? (x0 + 1) : (iw - 1);
        const int y1 = (y0 + 1 < ih) ? (y0 + 1) : (ih - 1);
        const float wx = fx - static_cast<float>(x0);
        const float wy = fy - static_cast<float>(y0);
        const float w00 = (1.0f - wx) * (1.0f - wy);
        const float w10 = wx * (1.0f - wy);
        const float w01 = (1.0f - wx) * wy;
        const float w11 = wx * wy;
        const int row0 = y0 * static_cast<int>(padded_width);
        const int row1 = y1 * static_cast<int>(padded_width);
        auto sample_ch = [&](int ch) -> float {
            const float* p = src_tensor + ch * plane_size;
            return clamp01(w00 * p[row0 + x0] + w10 * p[row0 + x1] +
                           w01 * p[row1 + x0] + w11 * p[row1 + x1]);
        };
        r = sample_ch(0);
        g = sample_ch(1);
        b = sample_ch(2);
    } else {
        const int pixel_idx = py * static_cast<int>(padded_width) + px;
        r = clamp01(src_tensor[0 * plane_size + pixel_idx]);
        g = clamp01(src_tensor[1 * plane_size + pixel_idx]);
        b = clamp01(src_tensor[2 * plane_size + pixel_idx]);
    }

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

// ---------------------------------------------------------------------------
// Phase 10 (Track 10C): 8x8 Grid Block-SAD & Guided 4K P010 Synthesis
// ---------------------------------------------------------------------------

extern "C" __global__ void rtx_eval_block_sad_8x8(
    const unsigned char* __restrict__ prev_y,
    const unsigned char* __restrict__ curr_y,
    int pitch, int width, int height,
    unsigned int* __restrict__ block_sad_64) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    unsigned int diff = 0;
    if (x < width && y < height) {
        const int idx = y * pitch + x;
        const int d = static_cast<int>(curr_y[idx]) - static_cast<int>(prev_y[idx]);
        diff = static_cast<unsigned int>(d < 0 ? -d : d);
        const int bx = min(7, (x * 8) / width);
        const int by = min(7, (y * 8) / height);
        atomicAdd(&block_sad_64[by * 8 + bx], diff);
    }
}

__device__ __forceinline__ float sampleP010Y(const unsigned short* plane, int pitch_samples, int w, int h, int x, int y) {
    const int cx = max(0, min(w - 1, x));
    const int cy = max(0, min(h - 1, y));
    return static_cast<float>(plane[cy * pitch_samples + cx] >> 6);
}

extern "C" __global__ void rtx_p010_guided_interp(
    const unsigned short* __restrict__ prev_p010_y,
    const unsigned short* __restrict__ prev_p010_uv,
    const unsigned short* __restrict__ curr_p010_y,
    const unsigned short* __restrict__ curr_p010_uv,
    const float* __restrict__ rife_mid_rgb_nchw,
    int p010_y_pitch_samples, int p010_uv_pitch_samples,
    int p010_w, int p010_h,
    int guide_w, int guide_h,
    float alpha,
    unsigned short* __restrict__ out_p010_y,
    unsigned short* __restrict__ out_p010_uv) {
    const int x = blockIdx.x * blockDim.x + threadIdx.x;
    const int y = blockIdx.y * blockDim.y + threadIdx.y;
    if (x >= p010_w || y >= p010_h) return;

    const float p0 = sampleP010Y(prev_p010_y, p010_y_pitch_samples, p010_w, p010_h, x, y);
    const float p1 = sampleP010Y(curr_p010_y, p010_y_pitch_samples, p010_w, p010_h, x, y);

    // Local 3x3 low-pass on 4K anchors to extract VSR high-frequency detail band
    const float lp0 = 0.25f * (
        sampleP010Y(prev_p010_y, p010_y_pitch_samples, p010_w, p010_h, x - 1, y) +
        sampleP010Y(prev_p010_y, p010_y_pitch_samples, p010_w, p010_h, x + 1, y) +
        sampleP010Y(prev_p010_y, p010_y_pitch_samples, p010_w, p010_h, x, y - 1) +
        sampleP010Y(prev_p010_y, p010_y_pitch_samples, p010_w, p010_h, x, y + 1));
    const float lp1 = 0.25f * (
        sampleP010Y(curr_p010_y, p010_y_pitch_samples, p010_w, p010_h, x - 1, y) +
        sampleP010Y(curr_p010_y, p010_y_pitch_samples, p010_w, p010_h, x + 1, y) +
        sampleP010Y(curr_p010_y, p010_y_pitch_samples, p010_w, p010_h, x, y - 1) +
        sampleP010Y(curr_p010_y, p010_y_pitch_samples, p010_w, p010_h, x, y + 1));

    const float hi0 = p0 - lp0;
    const float hi1 = p1 - lp1;
    const float motion = fabsf(p1 - p0);
    const float detail_weight = fmaxf(0.0f, 1.0f - motion * (1.0f / 128.0f));

    float base_y = (1.0f - alpha) * p0 + alpha * p1;
    if (rife_mid_rgb_nchw != nullptr && guide_w > 0 && guide_h > 0) {
        const float gx = (static_cast<float>(x) + 0.5f) * static_cast<float>(guide_w) / static_cast<float>(p010_w) - 0.5f;
        const float gy = (static_cast<float>(y) + 0.5f) * static_cast<float>(guide_h) / static_cast<float>(p010_h) - 0.5f;
        const int ix = max(0, min(guide_w - 1, static_cast<int>(gx + 0.5f)));
        const int iy = max(0, min(guide_h - 1, static_cast<int>(gy + 0.5f)));
        const int plane = guide_w * guide_h;
        const int gidx = iy * guide_w + ix;
        const float r = rife_mid_rgb_nchw[gidx];
        const float g = rife_mid_rgb_nchw[plane + gidx];
        const float b = rife_mid_rgb_nchw[2 * plane + gidx];
        const float guide_y = (0.2627f * r + 0.6780f * g + 0.0593f * b) * 1023.0f;
        base_y = 0.5f * base_y + 0.5f * guide_y;
    }

    const float final_y = base_y + detail_weight * ((1.0f - alpha) * hi0 + alpha * hi1);
    const unsigned int qy = min(1023u, static_cast<unsigned int>(max(0.0f, final_y + 0.5f)));
    out_p010_y[y * p010_y_pitch_samples + x] = static_cast<unsigned short>(qy << 6);

    if ((x & 1) == 0 && (y & 1) == 0) {
        const int cx = x >> 1;
        const int cy = y >> 1;
        const int cidx = cy * p010_uv_pitch_samples + cx * 2;
        const float u0 = static_cast<float>(prev_p010_uv[cidx] >> 6);
        const float v0 = static_cast<float>(prev_p010_uv[cidx + 1] >> 6);
        const float u1 = static_cast<float>(curr_p010_uv[cidx] >> 6);
        const float v1 = static_cast<float>(curr_p010_uv[cidx + 1] >> 6);
        const unsigned int qu = min(1023u, static_cast<unsigned int>(max(0.0f, (1.0f - alpha) * u0 + alpha * u1 + 0.5f)));
        const unsigned int qv = min(1023u, static_cast<unsigned int>(max(0.0f, (1.0f - alpha) * v0 + alpha * v1 + 0.5f)));
        out_p010_uv[cidx]     = static_cast<unsigned short>(qu << 6);
        out_p010_uv[cidx + 1] = static_cast<unsigned short>(qv << 6);
    }
}
