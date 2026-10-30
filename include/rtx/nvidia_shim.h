// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Runtime loader for the MIT-licensed rtx_shim hardware ABI.  The GPL core
// never links the shim and never resolves an NVIDIA symbol directly; this class
// is the only bridge between the engine and the versioned C ABI.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "shim/rtx_shim_abi.h"

namespace rtx {

class NvidiaShim {
public:
    NvidiaShim() = default;
    ~NvidiaShim();
    NvidiaShim(const NvidiaShim&) = delete;
    NvidiaShim& operator=(const NvidiaShim&) = delete;

    // Loads rtx_shim.dll / librtx_shim.so from RTX_SHIM_PATH or the executable
    // directory and resolves the stage-1 codec entry points.
    bool open(std::string* error = nullptr);
    void close();
    bool loaded() const noexcept { return handle_ != nullptr || embedded_; }
    const std::string& path() const noexcept { return path_; }
    const std::string& error() const noexcept { return error_; }

    RtxResult probe(RtxDiagnostics* diagnostics) const;

    RtxResult decoderCreate(const RtxDecoderConfig* config, RtxDecoder** decoder) const;
    void decoderDestroy(RtxDecoder* decoder) const;
    RtxResult decoderSubmit(RtxDecoder* decoder, const uint8_t* data, size_t size, int64_t pts,
                            int64_t dts, int32_t flags) const;
    RtxResult decoderReceive(RtxDecoder* decoder, RtxFrameDesc* frame) const;
    RtxResult decoderFlush(RtxDecoder* decoder) const;
    RtxResult decoderReleaseFrame(RtxDecoder* decoder, uint64_t slot_index) const;
    RtxResult decoderDownload(RtxDecoder* decoder, const RtxFrameDesc* frame, uint8_t* destination,
                              size_t destination_pitch) const;
    RtxResult decoderStats(RtxDecoder* decoder, RtxHwStats* stats) const;
    const char* decoderLastError(RtxDecoder* decoder) const;

    RtxResult encoderCreate(const RtxEncoderConfig* config, RtxEncoder** encoder) const;
    void encoderDestroy(RtxEncoder* encoder) const;
    RtxResult encoderEncode(RtxEncoder* encoder, const RtxFrameDesc* frame,
                            RtxPacketDesc* packet) const;
    RtxResult encoderEncodeHost(RtxEncoder* encoder, const uint8_t* data, uint32_t pitch,
                                int32_t format, int64_t pts, RtxPacketDesc* packet) const;
    RtxResult encoderFlush(RtxEncoder* encoder, RtxPacketDesc* packet) const;
    RtxResult encoderSequenceHeader(RtxEncoder* encoder, uint8_t* buffer, size_t capacity,
                                    size_t* size) const;
    RtxResult encoderStats(RtxEncoder* encoder, RtxHwStats* stats) const;
    const char* encoderLastError(RtxEncoder* encoder) const;

private:
    template <typename Fn>
    bool resolve(Fn& function, const char* name, std::string* error);

#if defined(RTX_EMBEDDED_SHIM)
    bool loadEmbedded(std::string* error);
#endif

    void* handle_ = nullptr;
    bool embedded_ = false;
    std::string path_;
    std::string error_;

    RtxResult(RTX_SHIM_CALL* probe_)(RtxDiagnostics*) = nullptr;
    RtxResult(RTX_SHIM_CALL* decoder_create_)(const RtxDecoderConfig*, RtxDecoder**) = nullptr;
    void(RTX_SHIM_CALL* decoder_destroy_)(RtxDecoder*) = nullptr;
    RtxResult(RTX_SHIM_CALL* decoder_submit_)(RtxDecoder*, const uint8_t*, size_t, int64_t,
                                              int64_t, int32_t) = nullptr;
    RtxResult(RTX_SHIM_CALL* decoder_receive_)(RtxDecoder*, RtxFrameDesc*) = nullptr;
    RtxResult(RTX_SHIM_CALL* decoder_flush_)(RtxDecoder*) = nullptr;
    RtxResult(RTX_SHIM_CALL* decoder_release_)(RtxDecoder*, uint64_t) = nullptr;
    RtxResult(RTX_SHIM_CALL* decoder_download_)(RtxDecoder*, const RtxFrameDesc*, uint8_t*,
                                                size_t) = nullptr;
    RtxResult(RTX_SHIM_CALL* decoder_stats_)(RtxDecoder*, RtxHwStats*) = nullptr;
    const char*(RTX_SHIM_CALL* decoder_last_error_)(RtxDecoder*) = nullptr;

    RtxResult(RTX_SHIM_CALL* encoder_create_)(const RtxEncoderConfig*, RtxEncoder**) = nullptr;
    void(RTX_SHIM_CALL* encoder_destroy_)(RtxEncoder*) = nullptr;
    RtxResult(RTX_SHIM_CALL* encoder_encode_)(RtxEncoder*, const RtxFrameDesc*,
                                              RtxPacketDesc*) = nullptr;
    RtxResult(RTX_SHIM_CALL* encoder_encode_host_)(RtxEncoder*, const uint8_t*, uint32_t, int32_t,
                                                   int64_t, RtxPacketDesc*) = nullptr;
    RtxResult(RTX_SHIM_CALL* encoder_flush_)(RtxEncoder*, RtxPacketDesc*) = nullptr;
    RtxResult(RTX_SHIM_CALL* encoder_sequence_header_)(RtxEncoder*, uint8_t*, size_t,
                                                       size_t*) = nullptr;
    RtxResult(RTX_SHIM_CALL* encoder_stats_)(RtxEncoder*, RtxHwStats*) = nullptr;
    const char*(RTX_SHIM_CALL* encoder_last_error_)(RtxEncoder*) = nullptr;
};

}  // namespace rtx
