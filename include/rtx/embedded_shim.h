// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Optional compile-time binding for the MIT-licensed rtx_shim.

#pragma once

#include <cstddef>
#include <cstdint>
#include <tuple>

#include "shim/rtx_shim_abi.h"

namespace rtx {

// Phase 12 (Wave 12.2-C): tuple of C-ABI function pointers replaces passive EmbeddedShimApi struct.
using EmbeddedShimApi = std::tuple<
    uint32_t(RTX_SHIM_CALL*)(),
    RtxResult(RTX_SHIM_CALL*)(RtxDiagnostics*),
    RtxResult(RTX_SHIM_CALL*)(const RtxDecoderConfig*, RtxDecoder**),
    void(RTX_SHIM_CALL*)(RtxDecoder*),
    RtxResult(RTX_SHIM_CALL*)(RtxDecoder*, const uint8_t*, size_t, int64_t, int64_t, int32_t),
    RtxResult(RTX_SHIM_CALL*)(RtxDecoder*, RtxFrameDesc*),
    RtxResult(RTX_SHIM_CALL*)(RtxDecoder*),
    RtxResult(RTX_SHIM_CALL*)(RtxDecoder*),
    RtxResult(RTX_SHIM_CALL*)(RtxDecoder*, uint64_t),
    RtxResult(RTX_SHIM_CALL*)(RtxDecoder*, const RtxFrameDesc*, uint8_t*, size_t),
    RtxResult(RTX_SHIM_CALL*)(RtxDecoder*, RtxHwStats*),
    const char*(RTX_SHIM_CALL*)(RtxDecoder*),
    RtxResult(RTX_SHIM_CALL*)(const RtxEncoderConfig*, RtxEncoder**),
    void(RTX_SHIM_CALL*)(RtxEncoder*),
    RtxResult(RTX_SHIM_CALL*)(RtxEncoder*, const RtxFrameDesc*, RtxPacketDesc*),
    RtxResult(RTX_SHIM_CALL*)(RtxEncoder*, const uint8_t*, uint32_t, int32_t, int64_t, RtxPacketDesc*),
    RtxResult(RTX_SHIM_CALL*)(RtxEncoder*, RtxPacketDesc*),
    RtxResult(RTX_SHIM_CALL*)(RtxEncoder*, uint8_t*, size_t, size_t*),
    RtxResult(RTX_SHIM_CALL*)(RtxEncoder*, RtxHwStats*),
    const char*(RTX_SHIM_CALL*)(RtxEncoder*)>;

const EmbeddedShimApi* embeddedShimApi();

}  // namespace rtx
