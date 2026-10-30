/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * rtx_fruc_bridge implementation notes.
 *
 * Two execution modes share one code path:
 *
 *   GPU mode   (cfg->cuda_context != nullptr)
 *       Device pointers are CUdeviceptr values.  The duplicate bypass is a
 *       cuMemcpyDtoDAsync on the caller's stream, so a hard cut costs one
 *       device-to-device blit and never touches host memory.
 *
 *   CPU reference mode (cfg->cuda_context == nullptr)
 *       Device pointers are ordinary host addresses.  This is what the unit
 *       tests and the hermetic CI use: the ABI contract, the scene-cut bypass
 *       and the VRAM budget are all verifiable on a machine without a GPU.
 *
 * NvOFFRUC.dll is optional.  When it is absent, create() for
 * RTX_FRUC_BACKEND_NVOFFRUC fails with RTX_FRUC_ERR_UNAVAILABLE and the caller
 * degrades to RTX_FRUC_BACKEND_DUPLICATE; the duplicate bypass itself never
 * depends on a vendor module.
 */

#ifndef RTX_FRUC_BRIDGE_OFFRUC_BRIDGE_H
#define RTX_FRUC_BRIDGE_OFFRUC_BRIDGE_H

#include <cstdint>
#include <string>

#include "fruc_bridge_abi.h"

namespace rtx_fruc {

// 1920x1080 NV12 frame in bytes, used for every budget computation.
uint64_t nv12FrameBytes(uint32_t width, uint32_t height) noexcept;

// Motion-vector grid the OFA engine needs for one frame pair.  The grid is
// 1/4 resolution in both axes with two float2 vectors per cell.
uint64_t opticalFlowGridBytes(uint32_t width, uint32_t height) noexcept;

// Total resident footprint of one bridge instance, clamped to the 120 MB
// contract ceiling.  Returns 0 for the CPU reference mode.
uint64_t estimateVramFootprint(uint32_t width, uint32_t height, bool gpu_mode) noexcept;

// Human-readable name of a backend, used by the status bar and the trace.
const char* backendName(uint32_t backend_type) noexcept;

// Maps an ABI status to a stable diagnostic string.
const char* statusName(int32_t status) noexcept;

// Copies err_buf capacity bytes at most and always NUL-terminates.
void writeError(char* err_buf, uint32_t err_cap, const std::string& message) noexcept;

}  // namespace rtx_fruc

#endif /* RTX_FRUC_BRIDGE_OFFRUC_BRIDGE_H */
