/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Internal launch entry points for RIFE pre-processing and post-processing
 * CUDA kernels. Shipped as embedded PTX and executed via Driver API.
 * Phase 12 (Wave 12.2-B1): zero named launch-parameter wrapper structs;
 * callers pass std::tuple unpacked via structured bindings.
 */

#ifndef RTX_NGX_BRIDGE_RIFE_PACK_RUNTIME_H
#define RTX_NGX_BRIDGE_RIFE_PACK_RUNTIME_H

#include <cstdint>
#include <tuple>

#include "ngx_bridge_abi.h"

namespace rtx_ngx {

bool isRifePackAvailable();

/* launchRifePreprocess tuple layout:
 *   (y_plane_0, uv_plane_0, y_plane_1, uv_plane_1, output_tensor,
 *    width, height, y_pitch, uv_pitch, padded_width, padded_height,
 *    alpha, full_range, d_sad_sum, reflect_padding, infer_width, infer_height) */
RtxResult launchRifePreprocess(
    const std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                     uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                     float, int32_t, uint64_t, int32_t, uint32_t, uint32_t>& launch,
    void* stream);

/* launchRifePreprocessSeparate tuple layout:
 *   (y_plane_0, uv_plane_0, y_plane_1, uv_plane_1, img0_tensor, img1_tensor,
 *    timestep_tensor, width, height, y_pitch, uv_pitch, padded_width, padded_height,
 *    alpha, full_range, d_sad_sum, reflect_padding) */
RtxResult launchRifePreprocessSeparate(
    const std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                     uint64_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                     float, int32_t, uint64_t, int32_t>& launch,
    void* stream);

/* launchRifePostprocess tuple layout:
 *   (rgb_tensor, y_plane, uv_plane, width, height, y_pitch, uv_pitch,
 *    padded_width, padded_height, full_range, infer_width, infer_height) */
RtxResult launchRifePostprocess(
    const std::tuple<uint64_t, uint64_t, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t,
                     uint32_t, uint32_t, int32_t, uint32_t, uint32_t>& launch,
    void* stream);

RtxResult launchFp32ToFp16(uint64_t src_fp32, uint64_t dst_fp16, uint32_t count, void* stream);
RtxResult launchFp16ToFp32(uint64_t src_fp16, uint64_t dst_fp32, uint32_t count, void* stream);
RtxResult launchEvalBlockSad8x8(uint64_t prev_y, uint64_t curr_y, uint32_t pitch,
                                uint32_t width, uint32_t height,
                                uint64_t d_block_sad_64, void* stream);
const char* rifePackLastError();

}  // namespace rtx_ngx

#endif  // RTX_NGX_BRIDGE_RIFE_PACK_RUNTIME_H
