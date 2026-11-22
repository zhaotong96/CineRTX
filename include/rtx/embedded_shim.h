// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Optional compile-time binding for the MIT-licensed rtx_shim.  Targets that
// link rtx_shim_static define RTX_EMBEDDED_SHIM; NvidiaShim::open() then falls
// back to this table when no rtx_shim.dll sits next to the executable.  The
// table keeps the GPL core free of NVIDIA headers: it only names the plain C
// ABI declared in shim/rtx_shim_abi.h.

#pragma once

#include <cstddef>
#include <cstdint>

#include "shim/rtx_shim_abi.h"

namespace rtx {

// @rtx-type-justification
// Category: C-ABI
// Why-Std-Fails: extern "C" 跨模块导出结构/函数指针表：字段序与调用约定即 shim C ABI 契约，std::function/std::tuple 无法表达 C 布局承诺。
// Invariant: 字段序、对齐与函数指针签名与 ABI 头逐字对应；append-only，禁止重排字段。
struct EmbeddedShimApi {
    uint32_t(RTX_SHIM_CALL* abi_version)();
    RtxResult(RTX_SHIM_CALL* probe)(RtxDiagnostics*);

    RtxResult(RTX_SHIM_CALL* decoder_create)(const RtxDecoderConfig*, RtxDecoder**);
    void(RTX_SHIM_CALL* decoder_destroy)(RtxDecoder*);
    RtxResult(RTX_SHIM_CALL* decoder_submit)(RtxDecoder*, const uint8_t*, size_t, int64_t,
                                             int64_t, int32_t);
    RtxResult(RTX_SHIM_CALL* decoder_receive)(RtxDecoder*, RtxFrameDesc*);
    RtxResult(RTX_SHIM_CALL* decoder_flush)(RtxDecoder*);
// Phase 11.2 (ABI v3): non-terminal sequence reset for realtime playback.
    RtxResult(RTX_SHIM_CALL* decoder_reset)(RtxDecoder*);
    RtxResult(RTX_SHIM_CALL* decoder_release_frame)(RtxDecoder*, uint64_t);
    RtxResult(RTX_SHIM_CALL* decoder_download)(RtxDecoder*, const RtxFrameDesc*, uint8_t*,
                                               size_t);
    RtxResult(RTX_SHIM_CALL* decoder_get_stats)(RtxDecoder*, RtxHwStats*);
    const char*(RTX_SHIM_CALL* decoder_last_error)(RtxDecoder*);

    RtxResult(RTX_SHIM_CALL* encoder_create)(const RtxEncoderConfig*, RtxEncoder**);
    void(RTX_SHIM_CALL* encoder_destroy)(RtxEncoder*);
    RtxResult(RTX_SHIM_CALL* encoder_encode)(RtxEncoder*, const RtxFrameDesc*,
                                             RtxPacketDesc*);
    RtxResult(RTX_SHIM_CALL* encoder_encode_host)(RtxEncoder*, const uint8_t*, uint32_t,
                                                  int32_t, int64_t, RtxPacketDesc*);
    RtxResult(RTX_SHIM_CALL* encoder_flush)(RtxEncoder*, RtxPacketDesc*);
    RtxResult(RTX_SHIM_CALL* encoder_get_sequence_header)(RtxEncoder*, uint8_t*, size_t,
                                                          size_t*);
    RtxResult(RTX_SHIM_CALL* encoder_get_stats)(RtxEncoder*, RtxHwStats*);
    const char*(RTX_SHIM_CALL* encoder_last_error)(RtxEncoder*);
};

// Returns nullptr when the shim was not linked into the current executable.
const EmbeddedShimApi* embeddedShimApi();

}  // namespace rtx
