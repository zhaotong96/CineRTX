/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * CUDA kernel for the final colour-space conversion:
 *   RGBA16F (linear or PQ BT.2020) -> P010 (BT.2020 NCL, PQ, 10-bit, limited)
 *
 * One thread owns a 2x2 luma block: four Y samples and one interleaved Cb/Cr
 * pair.  The input is a D3D12 R16G16B16A16_FLOAT texture imported through a
 * proprietary CUsurfObject; every pixel is therefore an 8-byte ushort4, not a
 * 16-byte float4.  This file is compiled by nvcc/NVRTC only when the pack PTX
 * is regenerated; the checked-in PTX is what ships.
 *
 * Numerical rules fixed by the phase-2 expert verdict:
 *   - IEEE round-to-nearest-even (__float2int_rn) before the 10-bit clamp;
 *   - Jimenez interleaved gradient noise (IGN) added to Y and UV before
 *     quantisation, removing 10-bit banding without temporal shimmer;
 *   - 4:2:0 chroma filtered with a separable [1,2,1]/4 x [1,2,1]/4 kernel
 *     centred on the top-left luma sample, matching the HEVC VUI
 *     chromaSampleLocationTop/Bottom = 2 (topleft) declaration;
 *   - U and V are emitted as one 32-bit coalesced store.
 */

#include <cuda_fp16.h>
#include <cuda_runtime.h>

namespace {

constexpr float kKrBt2020 = 0.2627f;
constexpr float kKgBt2020 = 0.6780f;
constexpr float kKbBt2020 = 0.0593f;
constexpr float kBuBt2020 = 1.8814f;
constexpr float kRvBt2020 = 1.4746f;

__device__ __forceinline__ float clamp01(float value) {
    return fminf(fmaxf(value, 0.0f), 1.0f);
}

// SMPTE ST 2084 encode.  Input is absolute luminance in cd/m^2.
__device__ __forceinline__ float pqEncode(float luminance_nits) {
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;
    const float y = fmaxf(luminance_nits, 0.0f) / 10000.0f;
    const float y_m1 = powf(y, m1);
    return powf((c1 + c2 * y_m1) / (1.0f + c3 * y_m1), m2);
}

// Jimenez interleaved gradient noise in [-0.5, 0.5).  Pure register math, so
// it is stable across frames and does not consume extra bandwidth.
__device__ __forceinline__ float ignDither(int x, int y) {
    const float magic = 0.06711056f * static_cast<float>(x) +
                        0.00583715f * static_cast<float>(y);
    const float frac_inner = magic - floorf(magic);
    const float product = 52.9829189f * frac_inner;
    return (product - floorf(product)) - 0.5f;
}

__device__ __forceinline__ float3 readRgb(cudaSurfaceObject_t input, int x, int y, int width,
                                          int height, int input_is_pq) {
    // Surface reads outside the array return zero, which would darken the
    // ceiled border of the 3x3 chroma kernel.  Clamp explicitly instead.
    const int cx = max(0, min(x, width - 1));
    const int cy = max(0, min(y, height - 1));
    ushort4 pixel;
    surf2Dread(&pixel, input, cx * static_cast<int>(sizeof(ushort4)), cy);
    float r = clamp01(__half2float(__ushort_as_half(pixel.x)));
    float g = clamp01(__half2float(__ushort_as_half(pixel.y)));
    float b = clamp01(__half2float(__ushort_as_half(pixel.z)));
    if (!input_is_pq) {
        // Linear BT.2020 with a 10000-nit peak -> PQ.
        r = pqEncode(r * 10000.0f);
        g = pqEncode(g * 10000.0f);
        b = pqEncode(b * 10000.0f);
    }
    return make_float3(r, g, b);
}

// BT.709 (linear light, D65) -> BT.2020 (linear light) primaries conversion.
// Rows of the canonical 3x3 derive from the xy chromaticities of both spaces;
// the constants are the ones agreed with the CPU reference in docs/report.md.
__device__ __forceinline__ float3 bt709ToBt2020(float r, float g, float b) {
    return make_float3(0.6274f * r + 0.3293f * g + 0.0433f * b,
                       0.0691f * r + 0.9195f * g + 0.0114f * b,
                       0.0164f * r + 0.0880f * g + 0.8956f * b);
}

// TrueHDR FP16 variant of readRgb: linear scRGB with BT.709 primaries where
// 1.0 corresponds to linear_scale_nits (80 cd/m^2 for the RTX Video SDK).
// Negative values clip to zero, but highlights above 1.0 are preserved: the
// whole point of inverse tone mapping is specular content at 4..12.49.
__device__ __forceinline__ float3 readScrgb(cudaSurfaceObject_t input, int x, int y, int width,
                                            int height, float linear_scale_nits) {
    const int cx = max(0, min(x, width - 1));
    const int cy = max(0, min(y, height - 1));
    ushort4 pixel;
    surf2Dread(&pixel, input, cx * static_cast<int>(sizeof(ushort4)), cy);
    float r = fmaxf(__half2float(__ushort_as_half(pixel.x)), 0.0f);
    float g = fmaxf(__half2float(__ushort_as_half(pixel.y)), 0.0f);
    float b = fmaxf(__half2float(__ushort_as_half(pixel.z)), 0.0f);
    const float3 wide = bt709ToBt2020(r, g, b);
    return make_float3(pqEncode(wide.x * linear_scale_nits),
                       pqEncode(wide.y * linear_scale_nits),
                       pqEncode(wide.z * linear_scale_nits));
}

}  // namespace

// ---------------------------------------------------------------------------
// NV12 / P010 -> R10G10B10A2 preprocess kernel (P0-1.2).
//
// One thread converts one luma pixel of the decoded NVDEC frame into the
// shared input texture NGX consumes.  Layout rules:
//   - NV12: Y and interleaved UV are 8-bit; pitches in bytes.
//   - P010: 16-bit containers with the 10-bit value in the high bits; the
//     kernel drops the low 6 bits.  Pitches are in bytes.
// Range/matrix handling follows ITU-R BT.709/2020/601 limited/full Studio
// swing exactly as specified in the phase-2 handover (§P0-1.2); the packed
// write keeps R in the low 10 bits, then G, B, alpha = 3.
// ---------------------------------------------------------------------------

extern "C" __global__ void rtx_yuv_to_rgb10(
    const unsigned short* __restrict__ y_plane, const unsigned short* __restrict__ uv_plane,
    cudaSurfaceObject_t output, int width, int height, int y_pitch_bytes, int uv_pitch_bytes,
    int format, int full_range, int matrix) {
    const int x = static_cast<int>(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = static_cast<int>(blockIdx.y * blockDim.y + threadIdx.y);
    if (x >= width || y >= height) {
        return;
    }

    float yp;
    float up;
    float vp;
    if (format == 2) {  // P010: 16-bit container, 10-bit value << 6
        const unsigned short y_raw =
            y_plane[static_cast<size_t>(y) * (y_pitch_bytes >> 1) + x];
        const size_t uv_offset =
            static_cast<size_t>(y >> 1) * (uv_pitch_bytes >> 1) + (x << 1);
        const unsigned short u_raw = uv_plane[uv_offset];
        const unsigned short v_raw = uv_plane[uv_offset + 1];
        const float y10 = static_cast<float>(y_raw >> 6);
        const float u10 = static_cast<float>(u_raw >> 6);
        const float v10 = static_cast<float>(v_raw >> 6);
        if (full_range) {
            yp = y10 / 1023.0f;
            up = (u10 - 512.0f) / 1023.0f;
            vp = (v10 - 512.0f) / 1023.0f;
        } else {
            yp = (y10 - 64.0f) / 876.0f;
            up = (u10 - 512.0f) / 896.0f;
            vp = (v10 - 512.0f) / 896.0f;
        }
    } else {  // NV12: 8-bit planes
        const unsigned char* y8 = reinterpret_cast<const unsigned char*>(y_plane);
        const unsigned char* uv8 = reinterpret_cast<const unsigned char*>(uv_plane);
        const float yv = static_cast<float>(y8[static_cast<size_t>(y) * y_pitch_bytes + x]);
        const size_t uv_offset = static_cast<size_t>(y >> 1) * uv_pitch_bytes + (x << 1);
        const float uv_u = static_cast<float>(uv8[uv_offset]);
        const float uv_v = static_cast<float>(uv8[uv_offset + 1]);
        if (full_range) {
            yp = yv / 255.0f;
            up = (uv_u - 128.0f) / 255.0f;
            vp = (uv_v - 128.0f) / 255.0f;
        } else {
            yp = (yv - 16.0f) / 219.0f;
            up = (uv_u - 128.0f) / 224.0f;
            vp = (uv_v - 128.0f) / 224.0f;
        }
    }

    // Y'CbCr -> R'G'B' for the requested matrix.  The coefficients are the
    // KR/KG/KB derivations fixed by the handover; do not renormalise them
    // here or the round trip against the CPU reference drifts.
    float r;
    float g;
    float b;
    if (matrix == 1) {        // BT.2020 NCL
        r = yp + 1.4746f * vp;
        g = yp - 0.16455f * up - 0.57135f * vp;
        b = yp + 1.8814f * up;
    } else if (matrix == 2) { // BT.601
        r = yp + 1.402f * vp;
        g = yp - 0.344136f * up - 0.714136f * vp;
        b = yp + 1.772f * up;
    } else {                  // BT.709
        r = yp + 1.5748f * vp;
        g = yp - 0.1873f * up - 0.4681f * vp;
        b = yp + 1.8556f * up;
    }

    // Round-to-nearest-even into the 10-bit channels; alpha carries the SDR
    // opaque marker (3 << 30) expected by the VSR feature.
    const unsigned int ir = min(
        1023u, static_cast<unsigned int>(__float2uint_rn(fminf(fmaxf(r, 0.0f), 1.0f) * 1023.0f)));
    const unsigned int ig = min(
        1023u, static_cast<unsigned int>(__float2uint_rn(fminf(fmaxf(g, 0.0f), 1.0f) * 1023.0f)));
    const unsigned int ib = min(
        1023u, static_cast<unsigned int>(__float2uint_rn(fminf(fmaxf(b, 0.0f), 1.0f) * 1023.0f)));
    const unsigned int packed = ir | (ig << 10) | (ib << 20) | (3u << 30);
    surf2Dwrite(packed, output, x * static_cast<int>(sizeof(unsigned int)), y);
}


extern "C" __global__ void rtx_rgba16f_to_p010_bt2020nc(
    cudaSurfaceObject_t input, unsigned short* __restrict__ y_plane,
    unsigned short* __restrict__ uv_plane, int width, int height, int y_pitch_samples,
    int uv_pitch_samples, int input_is_pq) {
    const int x0 = (blockIdx.x * blockDim.x + threadIdx.x) * 2;
    const int y0 = (blockIdx.y * blockDim.y + threadIdx.y) * 2;
    if (x0 >= width || y0 >= height) {
        return;
    }

    // The public entry point only accepts even geometry, so the whole 2x2
    // block is in bounds.  Read the four luma pixels once; the chroma filter
    // additionally reads the surrounding ring.
    const float3 p00 = readRgb(input, x0, y0, width, height, input_is_pq);
    const float3 p10 = readRgb(input, x0 + 1, y0, width, height, input_is_pq);
    const float3 p01 = readRgb(input, x0, y0 + 1, width, height, input_is_pq);
    const float3 p11 = readRgb(input, x0 + 1, y0 + 1, width, height, input_is_pq);

    int y00 = 0;
    int y10 = 0;
    int y01 = 0;
    int y11 = 0;
    int u10 = 0;
    int v10 = 0;

    const float d00 = ignDither(x0, y0);
    const float d10 = ignDither(x0 + 1, y0);
    const float d01 = ignDither(x0, y0 + 1);
    const float d11 = ignDither(x0 + 1, y0 + 1);

    // Dither is applied on the normalised signal before the limited-range
    // scale, which keeps the perturbation at half a 10-bit code value.
    const float yn00 = kKrBt2020 * p00.x + kKgBt2020 * p00.y + kKbBt2020 * p00.z;
    const float yn10 = kKrBt2020 * p10.x + kKgBt2020 * p10.y + kKbBt2020 * p10.z;
    const float yn01 = kKrBt2020 * p01.x + kKgBt2020 * p01.y + kKbBt2020 * p01.z;
    const float yn11 = kKrBt2020 * p11.x + kKgBt2020 * p11.y + kKbBt2020 * p11.z;
    y00 = max(64, min(940, __float2int_rn(yn00 * 876.0f + 64.0f + d00)));
    y10 = max(64, min(940, __float2int_rn(yn10 * 876.0f + 64.0f + d10)));
    y01 = max(64, min(940, __float2int_rn(yn01 * 876.0f + 64.0f + d01)));
    y11 = max(64, min(940, __float2int_rn(yn11 * 876.0f + 64.0f + d11)));

    y_plane[static_cast<size_t>(y0) * y_pitch_samples + x0] =
        static_cast<unsigned short>(y00 << 6);
    y_plane[static_cast<size_t>(y0) * y_pitch_samples + x0 + 1] =
        static_cast<unsigned short>(y10 << 6);
    y_plane[static_cast<size_t>(y0 + 1) * y_pitch_samples + x0] =
        static_cast<unsigned short>(y01 << 6);
    y_plane[static_cast<size_t>(y0 + 1) * y_pitch_samples + x0 + 1] =
        static_cast<unsigned short>(y11 << 6);

    // Separable [1,2,1]/4 x [1,2,1]/4 low-pass centred exactly on p00 (the
    // top-left luma sample).  The equal-weight 2x2 box filter used previously
    // sat at (0.5, 0.5), i.e. a 0.25 chroma-pixel phase error against the
    // topleft VUI signal.
    const float3 ring[9] = {
        readRgb(input, x0 - 1, y0 - 1, width, height, input_is_pq),
        readRgb(input, x0, y0 - 1, width, height, input_is_pq),
        readRgb(input, x0 + 1, y0 - 1, width, height, input_is_pq),
        readRgb(input, x0 - 1, y0, width, height, input_is_pq),
        p00,
        p10,
        readRgb(input, x0 - 1, y0 + 1, width, height, input_is_pq),
        p01,
        p11,
    };
    const float w[9] = {0.0625f, 0.125f, 0.0625f, 0.125f, 0.25f,
                        0.125f,  0.0625f, 0.125f, 0.0625f};

    float r_avg = 0.0f;
    float g_avg = 0.0f;
    float b_avg = 0.0f;
#pragma unroll
    for (int i = 0; i < 9; ++i) {
        r_avg += w[i] * ring[i].x;
        g_avg += w[i] * ring[i].y;
        b_avg += w[i] * ring[i].z;
    }
    const float y_avg = kKrBt2020 * r_avg + kKgBt2020 * g_avg + kKbBt2020 * b_avg;
    const float cb = (b_avg - y_avg) / kBuBt2020;
    const float cr = (r_avg - y_avg) / kRvBt2020;
    const float chroma_dither = 0.5f * ignDither(x0 >> 1, y0 >> 1);
    u10 = max(64, min(960, __float2int_rn(cb * 896.0f + 512.0f + chroma_dither)));
    v10 = max(64, min(960, __float2int_rn(cr * 896.0f + 512.0f + chroma_dither)));

    // One coalesced 32-bit store: low half = U, high half = V (little endian).
    const size_t uv_word = (static_cast<size_t>(y0 >> 1) * uv_pitch_samples + x0) / 2;
    reinterpret_cast<unsigned int*>(uv_plane)[uv_word] =
        static_cast<unsigned int>(u10 << 6) |
        (static_cast<unsigned int>(v10 << 6) << 16);
}

// ---------------------------------------------------------------------------
// pack v2 (P0-1.3): TrueHDR FP16 output -> P010.
//
// rtx_rgba16f_to_p010_bt2020nc_v2 adds the scRGB interpretation the TrueHDR
// feature actually produces (linear, BT.709 primaries, 1.0 = 80 cd/m^2) on
// top of the untouched v1 semantics:
//   input_is_pq    == 1  : surface holds BT.2020 PQ (passthrough, as v1)
//   input_is_scrgb == 1  : linear scRGB BT.709 -> nits via linear_scale_nits
//                          -> BT.2020 linear -> PQ
//   both 0                : legacy linear-BT.2020-with-10000-nit-peak (v1)
// Luma/chroma quantisation, IGN dither and the top-left 3x3 chroma filter are
// bit-identical to v1 so pack_verify keeps covering both kernels.
// ---------------------------------------------------------------------------

extern "C" __global__ void rtx_rgba16f_to_p010_bt2020nc_v2(
    cudaSurfaceObject_t input, unsigned short* __restrict__ y_plane,
    unsigned short* __restrict__ uv_plane, int width, int height, int y_pitch_samples,
    int uv_pitch_samples, int input_is_pq, int input_is_scrgb, float linear_scale_nits) {
    const int x0 = (blockIdx.x * blockDim.x + threadIdx.x) * 2;
    const int y0 = (blockIdx.y * blockDim.y + threadIdx.y) * 2;
    if (x0 >= width || y0 >= height) {
        return;
    }

    const bool pq = input_is_pq != 0;
    const bool scrgb = input_is_scrgb != 0;
    const float nits = scrgb ? (linear_scale_nits > 0.0f ? linear_scale_nits : 80.0f) : 0.0f;
    auto sample = [&](int x, int y) -> float3 {
        if (scrgb) {
            return readScrgb(input, x, y, width, height, nits);
        }
        return readRgb(input, x, y, width, height, pq ? 1 : 0);
    };

    const float3 p00 = sample(x0, y0);
    const float3 p10 = sample(x0 + 1, y0);
    const float3 p01 = sample(x0, y0 + 1);
    const float3 p11 = sample(x0 + 1, y0 + 1);

    const float d00 = ignDither(x0, y0);
    const float d10 = ignDither(x0 + 1, y0);
    const float d01 = ignDither(x0, y0 + 1);
    const float d11 = ignDither(x0 + 1, y0 + 1);

    const float yn00 = kKrBt2020 * p00.x + kKgBt2020 * p00.y + kKbBt2020 * p00.z;
    const float yn10 = kKrBt2020 * p10.x + kKgBt2020 * p10.y + kKbBt2020 * p10.z;
    const float yn01 = kKrBt2020 * p01.x + kKgBt2020 * p01.y + kKbBt2020 * p01.z;
    const float yn11 = kKrBt2020 * p11.x + kKgBt2020 * p11.y + kKbBt2020 * p11.z;
    const int y00 = max(64, min(940, __float2int_rn(yn00 * 876.0f + 64.0f + d00)));
    const int y10 = max(64, min(940, __float2int_rn(yn10 * 876.0f + 64.0f + d10)));
    const int y01 = max(64, min(940, __float2int_rn(yn01 * 876.0f + 64.0f + d01)));
    const int y11 = max(64, min(940, __float2int_rn(yn11 * 876.0f + 64.0f + d11)));

    y_plane[static_cast<size_t>(y0) * y_pitch_samples + x0] =
        static_cast<unsigned short>(y00 << 6);
    y_plane[static_cast<size_t>(y0) * y_pitch_samples + x0 + 1] =
        static_cast<unsigned short>(y10 << 6);
    y_plane[static_cast<size_t>(y0 + 1) * y_pitch_samples + x0] =
        static_cast<unsigned short>(y01 << 6);
    y_plane[static_cast<size_t>(y0 + 1) * y_pitch_samples + x0 + 1] =
        static_cast<unsigned short>(y11 << 6);

    const float3 ring[9] = {
        sample(x0 - 1, y0 - 1), sample(x0, y0 - 1), sample(x0 + 1, y0 - 1),
        sample(x0 - 1, y0),     p00,                sample(x0 + 1, y0),
        sample(x0 - 1, y0 + 1), p01,                sample(x0 + 1, y0 + 1),
    };
    const float w[9] = {0.0625f, 0.125f, 0.0625f, 0.125f, 0.25f,
                        0.125f,  0.0625f, 0.125f, 0.0625f};

    float r_avg = 0.0f;
    float g_avg = 0.0f;
    float b_avg = 0.0f;
#pragma unroll
    for (int i = 0; i < 9; ++i) {
        r_avg += w[i] * ring[i].x;
        g_avg += w[i] * ring[i].y;
        b_avg += w[i] * ring[i].z;
    }
    const float y_avg = kKrBt2020 * r_avg + kKgBt2020 * g_avg + kKbBt2020 * b_avg;
    const float cb = (b_avg - y_avg) / kBuBt2020;
    const float cr = (r_avg - y_avg) / kRvBt2020;
    const float chroma_dither = 0.5f * ignDither(x0 >> 1, y0 >> 1);
    const int u10 = max(64, min(960, __float2int_rn(cb * 896.0f + 512.0f + chroma_dither)));
    const int v10 = max(64, min(960, __float2int_rn(cr * 896.0f + 512.0f + chroma_dither)));

    const size_t uv_word = (static_cast<size_t>(y0 >> 1) * uv_pitch_samples + x0) / 2;
    reinterpret_cast<unsigned int*>(uv_plane)[uv_word] =
        static_cast<unsigned int>(u10 << 6) |
        (static_cast<unsigned int>(v10 << 6) << 16);
}
