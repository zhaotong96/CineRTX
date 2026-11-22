/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Internal launch entry points for RIFE pre-processing and post-processing
 * CUDA kernels. Shipped as embedded PTX and executed via Driver API.
 */

#ifndef RTX_NGX_BRIDGE_RIFE_PACK_RUNTIME_H
#define RTX_NGX_BRIDGE_RIFE_PACK_RUNTIME_H

#include <cstdint>
#include "ngx_bridge_abi.h"

namespace rtx_ngx {

// @rtx-type-justification
// Category: C-ABI
// Why-Std-Fails: extern "C" 跨模块导出结构/函数指针表：字段序与调用约定即 shim C ABI 契约，std::function/std::tuple 无法表达 C 布局承诺。
// Invariant: 字段序、对齐与函数指针签名与 ABI 头逐字对应；append-only，禁止重排字段。
struct RifePreprocessLaunch {
    uint64_t y_plane_0 = 0;       /* CUdeviceptr */
    uint64_t uv_plane_0 = 0;      /* CUdeviceptr */
    uint64_t y_plane_1 = 0;       /* CUdeviceptr */
    uint64_t uv_plane_1 = 0;      /* CUdeviceptr */
    uint64_t output_tensor = 0;   /* CUdeviceptr [1, 7, H_pad, W_pad] */
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t y_pitch = 0;
    uint32_t uv_pitch = 0;
    uint32_t padded_width = 0;
    uint32_t padded_height = 0;
    float alpha = 0.5f;
    int32_t full_range = 0;
    uint64_t d_sad_sum = 0;       /* CUdeviceptr of uint32_t */
    int32_t reflect_padding = 1;  /* Phase 11.4 (C-06): reflect101 PTX 7.5 / sm_75 */
    uint32_t infer_width = 0;
    uint32_t infer_height = 0;
};

// @rtx-type-justification
// Category: C-ABI
// Why-Std-Fails: extern "C" 跨模块导出结构/函数指针表：字段序与调用约定即 shim C ABI 契约，std::function/std::tuple 无法表达 C 布局承诺。
// Invariant: 字段序、对齐与函数指针签名与 ABI 头逐字对应；append-only，禁止重排字段。
struct RifePreprocessSeparateLaunch {
    uint64_t y_plane_0 = 0;
    uint64_t uv_plane_0 = 0;
    uint64_t y_plane_1 = 0;
    uint64_t uv_plane_1 = 0;
    uint64_t img0_tensor = 0;     /* CUdeviceptr [1, 3, H_pad, W_pad] */
    uint64_t img1_tensor = 0;     /* CUdeviceptr [1, 3, H_pad, W_pad] */
    uint64_t timestep_tensor = 0; /* CUdeviceptr [1, 1, H_pad, W_pad] or 0 */
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t y_pitch = 0;
    uint32_t uv_pitch = 0;
    uint32_t padded_width = 0;
    uint32_t padded_height = 0;
    float alpha = 0.5f;
    int32_t full_range = 0;
    uint64_t d_sad_sum = 0;
    int32_t reflect_padding = 1;
};

// @rtx-type-justification
// Category: C-ABI
// Why-Std-Fails: extern "C" 跨模块导出结构/函数指针表：字段序与调用约定即 shim C ABI 契约，std::function/std::tuple 无法表达 C 布局承诺。
// Invariant: 字段序、对齐与函数指针签名与 ABI 头逐字对应；append-only，禁止重排字段。
struct RifePostprocessLaunch {
    uint64_t rgb_tensor = 0;      /* CUdeviceptr [1, 3, H_pad, W_pad] */
    uint64_t y_plane = 0;         /* CUdeviceptr output */
    uint64_t uv_plane = 0;        /* CUdeviceptr output */
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t y_pitch = 0;
    uint32_t uv_pitch = 0;
    uint32_t padded_width = 0;
    uint32_t padded_height = 0;
    int32_t full_range = 0;
    uint32_t infer_width = 0;
    uint32_t infer_height = 0;
};

bool isRifePackAvailable();
RtxResult launchRifePreprocess(const RifePreprocessLaunch& launch, void* stream);
RtxResult launchRifePreprocessSeparate(const RifePreprocessSeparateLaunch& launch, void* stream);
RtxResult launchRifePostprocess(const RifePostprocessLaunch& launch, void* stream);
/* Phase 9.1 (RW-P9-02): element-wise IEEE-754 binary32 <-> binary16 conversion
 * kernels backing the kHALF engine I/O chain. Grid (count+255)/256, block 256. */
RtxResult launchFp32ToFp16(uint64_t src_fp32, uint64_t dst_fp16, uint32_t count, void* stream);
RtxResult launchFp16ToFp32(uint64_t src_fp16, uint64_t dst_fp32, uint32_t count, void* stream);
RtxResult launchEvalBlockSad8x8(uint64_t prev_y, uint64_t curr_y, uint32_t pitch,
                               uint32_t width, uint32_t height,
                               uint64_t d_block_sad_64, void* stream);
const char* rifePackLastError();

}  // namespace rtx_ngx

#endif  // RTX_NGX_BRIDGE_RIFE_PACK_RUNTIME_H
