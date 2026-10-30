/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * RIFE v4.25 NV12 <-> NCHW FP16 pack kernels (W8-1).
 *
 * The 1080p height is not a multiple of 32 (1080 / 32 = 33.75), so the tensor
 * height is padded up to 1088 and the bottom eight rows are filled by reflecting
 * the last valid row (y' = 1079 - (y - 1080)).  Reflection, not replication:
 * replication would give the network a constant band that biases the flow field
 * at the bottom edge.
 *
 * Both kernels run in a single launch on the caller's stream and are sized for
 * 16x16 blocks; the measured budget is < 0.15 ms per launch at 1080p on
 * sm_120.
 */

#include "rife_pack.cuh"

#if defined(__CUDACC__)

#include <cuda_fp16.h>
#include <cuda_runtime.h>

namespace {

constexpr int kBlockDim = 16;

// BT.709 limited-range NV12 -> full-range RGB in [0, 255].
__device__ __forceinline__ void yuvToRgbBt709(float y, float u, float v, float* r, float* g,
                                              float* b) {
    const float luma = (y - 16.0f) * (255.0f / 219.0f);
    const float cb = (u - 128.0f) * (255.0f / 224.0f);
    const float cr = (v - 128.0f) * (255.0f / 224.0f);
    *r = luma + 1.5748f * cr;
    *g = luma - 0.1873f * cb - 0.4681f * cr;
    *b = luma + 1.8556f * cb;
}

__device__ __forceinline__ float reflectRow(int y, int height) {
    if (y < height) {
        return static_cast<float>(y);
    }
    // y in [height, 2*height) -> mirror into [0, height)
    const int mirrored = 2 * height - 1 - y;
    return static_cast<float>(mirrored < 0 ? 0 : mirrored);
}

__global__ void rifePackKernel(RtxRifePackParams params) {
    const int x = blockIdx.x * kBlockDim + threadIdx.x;
    const int y = blockIdx.y * kBlockDim + threadIdx.y;
    if (x >= params.tensor_width || y >= params.tensor_height) {
        return;
    }

    const int source_y = static_cast<int>(reflectRow(y, params.height));
    const int chroma_y = source_y >> 1;
    const int chroma_x = x >> 1;

    const uint8_t* prev_y_plane = params.previous_nv12;
    const uint8_t* prev_uv_plane = params.previous_nv12 + params.y_pitch * params.height;
    const uint8_t* next_y_plane = params.next_nv12;
    const uint8_t* next_uv_plane = params.next_nv12 + params.y_pitch * params.height;

    const float prev_y = static_cast<float>(prev_y_plane[source_y * params.y_pitch + x]);
    const float next_y = static_cast<float>(next_y_plane[source_y * params.y_pitch + x]);
    const float prev_u = static_cast<float>(prev_uv_plane[chroma_y * params.uv_pitch + chroma_x * 2]);
    const float prev_v =
        static_cast<float>(prev_uv_plane[chroma_y * params.uv_pitch + chroma_x * 2 + 1]);
    const float next_u = static_cast<float>(next_uv_plane[chroma_y * params.uv_pitch + chroma_x * 2]);
    const float next_v =
        static_cast<float>(next_uv_plane[chroma_y * params.uv_pitch + chroma_x * 2 + 1]);

    float prev_r = 0.0f;
    float prev_g = 0.0f;
    float prev_b = 0.0f;
    float next_r = 0.0f;
    float next_g = 0.0f;
    float next_b = 0.0f;
    yuvToRgbBt709(prev_y, prev_u, prev_v, &prev_r, &prev_g, &prev_b);
    yuvToRgbBt709(next_y, next_u, next_v, &next_r, &next_g, &next_b);

    constexpr float kScale = 1.0f / 255.0f;
    const int plane = params.tensor_width * params.tensor_height;
    const int offset = y * params.tensor_width + x;

    // [1][7][H][W]: channels 0-2 = frame N, 3-5 = frame N+1, 6 = timestep.
    params.tensor[0 * plane + offset] = __float2half_rn(prev_r * kScale);
    params.tensor[1 * plane + offset] = __float2half_rn(prev_g * kScale);
    params.tensor[2 * plane + offset] = __float2half_rn(prev_b * kScale);
    params.tensor[3 * plane + offset] = __float2half_rn(next_r * kScale);
    params.tensor[4 * plane + offset] = __float2half_rn(next_g * kScale);
    params.tensor[5 * plane + offset] = __float2half_rn(next_b * kScale);
    params.tensor[6 * plane + offset] = __float2half_rn(params.timestep);
}

__global__ void rifeUnpackKernel(RtxRifeUnpackParams params) {
    const int x = blockIdx.x * kBlockDim + threadIdx.x;
    const int y = blockIdx.y * kBlockDim + threadIdx.y;
    if (x >= params.width || y >= params.height) {
        return;
    }
    const int plane = params.tensor_width * params.tensor_height;
    const int offset = y * params.tensor_width + x;

    // The clamp is mandatory: a residual FP16 overflow would otherwise reach
    // TrueHDR as a negative luma and produce a black speck.
    float r = __half2float(params.tensor[0 * plane + offset]);
    float g = __half2float(params.tensor[1 * plane + offset]);
    float b = __half2float(params.tensor[2 * plane + offset]);
    r = fminf(fmaxf(r, 0.0f), 1.0f) * 255.0f;
    g = fminf(fmaxf(g, 0.0f), 1.0f) * 255.0f;
    b = fminf(fmaxf(b, 0.0f), 1.0f) * 255.0f;

    // Full-range RGB -> BT.709 limited-range YCbCr.
    const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    const float cb = (b - luma) / 1.8556f;
    const float cr = (r - luma) / 1.5748f;

    const float y_code = 16.0f + luma * (219.0f / 255.0f);
    const float u_code = 128.0f + cb * (224.0f / 255.0f);
    const float v_code = 128.0f + cr * (224.0f / 255.0f);

    params.nv12[y * params.y_pitch + x] =
        static_cast<uint8_t>(fminf(fmaxf(y_code, 0.0f), 255.0f));

    // 4:2:0 with top-left chroma siting: only the even row/column writes.
    if ((y & 1) == 0 && (x & 1) == 0) {
        uint8_t* uv_plane = params.nv12 + params.y_pitch * params.height;
        const int chroma_y = y >> 1;
        const int chroma_x = x >> 1;
        uv_plane[chroma_y * params.uv_pitch + chroma_x * 2] =
            static_cast<uint8_t>(fminf(fmaxf(u_code, 0.0f), 255.0f));
        uv_plane[chroma_y * params.uv_pitch + chroma_x * 2 + 1] =
            static_cast<uint8_t>(fminf(fmaxf(v_code, 0.0f), 255.0f));
    }
}

}  // namespace

extern "C" int rtx_rife_pack_launch(const RtxRifePackParams* params, void* stream) {
    if (params == nullptr || params->tensor == nullptr || params->previous_nv12 == nullptr ||
        params->next_nv12 == nullptr || params->tensor_width <= 0 || params->tensor_height <= 0 ||
        params->width <= 0 || params->height <= 0) {
        return 1;
    }
    const dim3 block(kBlockDim, kBlockDim);
    const dim3 grid(static_cast<unsigned>((params->tensor_width + kBlockDim - 1) / kBlockDim),
                    static_cast<unsigned>((params->tensor_height + kBlockDim - 1) / kBlockDim));
    rifePackKernel<<<grid, block, 0, reinterpret_cast<cudaStream_t>(stream)>>>(*params);
    return static_cast<int>(cudaGetLastError());
}

extern "C" int rtx_rife_unpack_launch(const RtxRifeUnpackParams* params, void* stream) {
    if (params == nullptr || params->tensor == nullptr || params->nv12 == nullptr ||
        params->width <= 0 || params->height <= 0 || params->tensor_width <= 0 ||
        params->tensor_height <= 0) {
        return 1;
    }
    const dim3 block(kBlockDim, kBlockDim);
    const dim3 grid(static_cast<unsigned>((params->width + kBlockDim - 1) / kBlockDim),
                    static_cast<unsigned>((params->height + kBlockDim - 1) / kBlockDim));
    rifeUnpackKernel<<<grid, block, 0, reinterpret_cast<cudaStream_t>(stream)>>>(*params);
    return static_cast<int>(cudaGetLastError());
}

#endif  // __CUDACC__
