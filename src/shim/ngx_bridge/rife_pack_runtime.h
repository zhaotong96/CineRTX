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

#include <algorithm>
#include <array>
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

/* MIT-native 8x8 block-SAD shot-boundary detector shared by RIFE and NVOFA. */
inline bool evalBlockSadSceneCut(const std::array<uint32_t, 64>& block_sad,
                                 uint32_t width, uint32_t height, float& ema_motion) noexcept {
    if (width == 0 || height == 0) return false;
    uint64_t total_sad = 0;
    for (uint32_t s : block_sad) total_sad += s;
    const double total_denom = static_cast<double>(width) * static_cast<double>(height) * 255.0;
    const float mu_k = static_cast<float>(static_cast<double>(total_sad) / total_denom);
    const double block_denom = (std::max)(1.0, static_cast<double>(width) / 8.0) *
                               (std::max)(1.0, static_cast<double>(height) / 8.0) * 255.0;
    int outliers = 0;
    for (uint32_t s : block_sad) {
        if (static_cast<float>(static_cast<double>(s) / block_denom) > 0.16f) ++outliers;
    }
    const float p_outlier = static_cast<float>(outliers) / 64.0f;
    const float baseline = (std::max)(ema_motion, 0.025f);
    const bool hard_cut = (mu_k > 0.22f) ||
                          (mu_k > 0.085f && mu_k > 3.2f * baseline && p_outlier >= 0.30f) ||
                          (p_outlier >= 0.375f && mu_k > 2.4f * ema_motion);
    if (hard_cut) {
        ema_motion = 0.03f;
    } else {
        constexpr float kAlpha = 2.0f / 9.0f;
        ema_motion = (1.0f - kAlpha) * ema_motion + kAlpha * mu_k;
    }
    return hard_cut;
}

}  // namespace rtx_ngx

#endif  // RTX_NGX_BRIDGE_RIFE_PACK_RUNTIME_H
