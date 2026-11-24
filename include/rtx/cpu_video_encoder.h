// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
// Phase 12 (Wave 12.2-B2): eliminated CpuEncoderConfig and CpuVideoEncoder::Impl;
// reclassified CpuVideoEncoder to NonTrivial-StateMachine.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "rtx/status.h"
#include "rtx/types.h"

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace rtx {

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: Manages FFmpeg AVCodecContext, AVFrame, and AVPacket encoder lifecycle with deterministic av_packet_free -> av_frame_free -> avcodec_free_context teardown order.
// Invariant: ctx_ != nullptr after create(); P010 input frames are converted to YUV420P10LE and drained on the owning thread.
class CpuVideoEncoder final {
public:
    CpuVideoEncoder() = default;
    ~CpuVideoEncoder() noexcept;

    CpuVideoEncoder(const CpuVideoEncoder&) = delete;
    CpuVideoEncoder& operator=(const CpuVideoEncoder&) = delete;
    CpuVideoEncoder(CpuVideoEncoder&& other) noexcept;
    CpuVideoEncoder& operator=(CpuVideoEncoder&& other) noexcept;

    [[nodiscard]] static Result<CpuVideoEncoder> create(
        VideoCodec codec, int width, int height, Rational frame_rate,
        int crf = 22, int preset = 5, int threads = 0, const Hdr10Metadata& hdr = {});

    [[nodiscard]] Result<std::vector<Packet>> encodeP010Frame(
        const uint16_t* y_plane, size_t y_pitch_bytes,
        const uint16_t* uv_plane, size_t uv_pitch_bytes,
        int64_t pts_90k, bool force_keyframe,
        std::span<const uint8_t> hdr10plus_payload = {});

    [[nodiscard]] Result<std::vector<Packet>> flush();
    [[nodiscard]] std::span<const uint8_t> extradata() const noexcept { return extradata_buf_; }

private:
    Result<std::vector<Packet>> drainPackets();
    void release() noexcept;

    AVCodecContext* ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* pkt_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    Hdr10Metadata hdr_{};
    std::vector<uint8_t> extradata_buf_;
};

}  // namespace rtx
