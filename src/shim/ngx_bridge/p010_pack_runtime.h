/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Internal (non-ABI) launch entry points for the remaster kernels that live
 * in the same DLL.  bridge_exports.cpp uses these to run the preprocess and
 * pack v2 kernels on the interop pack stream; they are not exported.
 * Phase 12 (Wave 12.2-B1): zero named launch-parameter wrapper structs;
 * callers pass std::tuple unpacked via structured bindings.
 */

#ifndef RTX_NGX_BRIDGE_P010_PACK_RUNTIME_H
#define RTX_NGX_BRIDGE_P010_PACK_RUNTIME_H

#include <cstdint>
#include <tuple>

#include "ngx_bridge_abi.h"

namespace rtx_ngx {

/* launchPreprocess tuple layout:
 *   (y_plane, uv_plane, output_surface, width, height,
 *    y_pitch_bytes, uv_pitch_bytes, format, full_range, matrix) */
RtxResult launchPreprocess(
    const std::tuple<uint64_t, uint64_t, uint64_t, int32_t, int32_t,
                     int32_t, int32_t, int32_t, int32_t, int32_t>& launch,
    void* stream);

/* launchPreprocessBuffer tuple layout:
 *   (y_plane, uv_plane, output_ptr, output_pitch_bytes, width, height,
 *    y_pitch_bytes, uv_pitch_bytes, format, full_range, matrix) */
RtxResult launchPreprocessBuffer(
    const std::tuple<uint64_t, uint64_t, uint64_t, int32_t, int32_t, int32_t,
                     int32_t, int32_t, int32_t, int32_t, int32_t>& launch,
    void* stream);

/* launchPackV2 tuple layout:
 *   (input_surface, y_plane, uv_plane, y_pitch_samples, uv_pitch_samples,
 *    width, height, input_is_pq, input_is_scrgb, linear_scale_nits,
 *    lut_tex, lut_dim, stats_devptr, preview_ptr, preview_width,
 *    preview_height, preview_pitch_px, split_x_norm, src_surface) */
RtxResult launchPackV2(
    const std::tuple<uint64_t, uint64_t, uint64_t, uint32_t, uint32_t,
                     int32_t, int32_t, int32_t, int32_t, float,
                     uint64_t, int32_t, uint64_t, uint64_t, int32_t,
                     int32_t, int32_t, float, uint64_t>& launch,
    void* stream);

/* launchFusedRemaster tuple layout:
 *   (src_y, src_uv, src_y_pitch, src_uv_pitch, src_format, src_full_range,
 *    src_matrix, dst_y, dst_uv, dst_y_pitch_samples, dst_uv_pitch_samples,
 *    src_width, src_height, dst_width, dst_height, lut_tex, lut_dim,
 *    vsr_quality, enable_itm, max_luminance, y_norm_prev, stats_devptr) */
RtxResult launchFusedRemaster(
    const std::tuple<uint64_t, uint64_t, uint32_t, uint32_t, int32_t, int32_t,
                     int32_t, uint64_t, uint64_t, uint32_t, uint32_t,
                     int32_t, int32_t, int32_t, int32_t, uint64_t, int32_t,
                     int32_t, int32_t, float, float, uint64_t>& launch,
    void* stream);

RtxResult launchInterpolateNv12(uint64_t prev_nv12, uint64_t curr_nv12,
                                uint32_t pitch, uint32_t width, uint32_t height,
                                float alpha, uint64_t out_nv12, void* stream);
RtxResult launchNvofWarpNv12(uint64_t prev_nv12, uint64_t curr_nv12,
                             uint64_t fwd_flow, uint64_t bwd_flow, uint64_t cost_buffer,
                             uint32_t pitch, uint32_t width, uint32_t height,
                             uint32_t grid_width, uint32_t grid_height,
                             float alpha, uint64_t out_nv12, void* stream,
                             uint32_t flow_stride_bytes = 0,
                             uint32_t cost_stride_bytes = 0);

const char* packLastError();

}  // namespace rtx_ngx

#endif /* RTX_NGX_BRIDGE_P010_PACK_RUNTIME_H */
