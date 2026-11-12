// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/nvidia_shim.h"

#include <string>
#include "rtx/embedded_shim.h"

namespace rtx {

NvidiaShim::~NvidiaShim() { close(); }

bool NvidiaShim::open(std::string* error) {
    if (loaded()) {
        return true;
    }
    const EmbeddedShimApi* api = embeddedShimApi();
    if (api == nullptr || api->abi_version == nullptr) {
        error_ = "embedded rtx_shim API unavailable";
        if (error != nullptr) *error = error_;
        return false;
    }
    if (api->abi_version() != RTX_SHIM_ABI_VERSION) {
        error_ = "embedded rtx_shim ABI mismatch";
        if (error != nullptr) *error = error_;
        return false;
    }
    probe_ = api->probe;
    decoder_create_ = api->decoder_create;
    decoder_destroy_ = api->decoder_destroy;
    decoder_submit_ = api->decoder_submit;
    decoder_receive_ = api->decoder_receive;
    decoder_flush_ = api->decoder_flush;
    decoder_release_ = api->decoder_release_frame;
    decoder_stats_ = api->decoder_get_stats;
    decoder_last_error_ = api->decoder_last_error;
    encoder_create_ = api->encoder_create;
    encoder_destroy_ = api->encoder_destroy;
    encoder_encode_ = api->encoder_encode;
    encoder_flush_ = api->encoder_flush;
    encoder_sequence_header_ = api->encoder_get_sequence_header;
    encoder_stats_ = api->encoder_get_stats;
    encoder_last_error_ = api->encoder_last_error;
    embedded_ = true;
    path_ = "embedded:rtx_shim";
    error_.clear();
    if (error != nullptr) error->clear();
    return true;
}

void NvidiaShim::close() {
    embedded_ = false;
    path_.clear();
    error_.clear();
    probe_ = nullptr;
    decoder_create_ = nullptr;
    decoder_destroy_ = nullptr;
    decoder_submit_ = nullptr;
    decoder_receive_ = nullptr;
    decoder_flush_ = nullptr;
    decoder_release_ = nullptr;
    decoder_stats_ = nullptr;
    decoder_last_error_ = nullptr;
    encoder_create_ = nullptr;
    encoder_destroy_ = nullptr;
    encoder_encode_ = nullptr;
    encoder_flush_ = nullptr;
    encoder_sequence_header_ = nullptr;
    encoder_stats_ = nullptr;
    encoder_last_error_ = nullptr;
}

RtxResult NvidiaShim::probe(RtxDiagnostics* d) const { return probe_ ? probe_(d) : RTX_ERR_DLL_MISSING; }
RtxResult NvidiaShim::decoderCreate(const RtxDecoderConfig* c, RtxDecoder** d) const { return decoder_create_ ? decoder_create_(c, d) : RTX_ERR_DLL_MISSING; }
void NvidiaShim::decoderDestroy(RtxDecoder* d) const { if (decoder_destroy_) decoder_destroy_(d); }
RtxResult NvidiaShim::decoderSubmit(RtxDecoder* d, const uint8_t* data, size_t s, int64_t pts, int64_t dts, int32_t f) const {
    return decoder_submit_ ? decoder_submit_(d, data, s, pts, dts, f) : RTX_ERR_DLL_MISSING;
}
RtxResult NvidiaShim::decoderReceive(RtxDecoder* d, RtxFrameDesc* f) const { return decoder_receive_ ? decoder_receive_(d, f) : RTX_ERR_DLL_MISSING; }
RtxResult NvidiaShim::decoderFlush(RtxDecoder* d) const { return decoder_flush_ ? decoder_flush_(d) : RTX_ERR_DLL_MISSING; }
RtxResult NvidiaShim::decoderReleaseFrame(RtxDecoder* d, uint64_t slot) const { return decoder_release_ ? decoder_release_(d, slot) : RTX_ERR_DLL_MISSING; }
RtxResult NvidiaShim::decoderStats(RtxDecoder* d, RtxHwStats* s) const { return decoder_stats_ ? decoder_stats_(d, s) : RTX_ERR_DLL_MISSING; }
const char* NvidiaShim::decoderLastError(RtxDecoder* d) const { return decoder_last_error_ ? decoder_last_error_(d) : "not loaded"; }
RtxResult NvidiaShim::encoderCreate(const RtxEncoderConfig* c, RtxEncoder** e) const { return encoder_create_ ? encoder_create_(c, e) : RTX_ERR_DLL_MISSING; }
void NvidiaShim::encoderDestroy(RtxEncoder* e) const { if (encoder_destroy_) encoder_destroy_(e); }
RtxResult NvidiaShim::encoderEncode(RtxEncoder* e, const RtxFrameDesc* f, RtxPacketDesc* p) const { return encoder_encode_ ? encoder_encode_(e, f, p) : RTX_ERR_DLL_MISSING; }
RtxResult NvidiaShim::encoderFlush(RtxEncoder* e, RtxPacketDesc* p) const { return encoder_flush_ ? encoder_flush_(e, p) : RTX_ERR_DLL_MISSING; }
RtxResult NvidiaShim::encoderSequenceHeader(RtxEncoder* e, uint8_t* b, size_t c, size_t* s) const { return encoder_sequence_header_ ? encoder_sequence_header_(e, b, c, s) : RTX_ERR_DLL_MISSING; }
RtxResult NvidiaShim::encoderStats(RtxEncoder* e, RtxHwStats* s) const { return encoder_stats_ ? encoder_stats_(e, s) : RTX_ERR_DLL_MISSING; }
const char* NvidiaShim::encoderLastError(RtxEncoder* e) const { return encoder_last_error_ ? encoder_last_error_(e) : "not loaded"; }

}  // namespace rtx
