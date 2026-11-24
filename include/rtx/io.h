// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Concrete FFmpeg container I/O (Gate 3.0 + Phase 12 Wave 12.2-B2):
// IDemuxer/IMuxer virtual shells and dead DecodedVideoFrame removed;
// callers bind directly to concrete FfmpegDemuxer / FfmpegMuxer.

#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "rtx/flicks.h"
#include "rtx/status.h"
#include "rtx/timestamp_guard.h"
#include "rtx/types.h"

struct AVBSFContext;
struct AVFormatContext;
struct AVPacket;
struct AVStream;

namespace rtx {

// Phase 12 (Wave 12.2-C): tuple alias (stream_index, codec_type, codec_id, codec_name, time_base, opaque_codecpar).
using PassthroughStreamInfo = std::tuple<int, int, int, std::string, Rational, std::shared_ptr<void>>;

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: Media probe schema aggregating container metadata, video/audio tracks, passthrough streams, color description, HDR10 metadata, and display matrix.
// Invariant: width >= 0 and height >= 0 after open(); extradata matches codec record when non-empty.
struct MediaInfo {
    std::string container;
    std::string video_codec;
    VideoCodec codec = VideoCodec::Unknown;
    int width = 0;
    int height = 0;
    int bit_depth = 8;
    int bitrate = 0;
    Rational frame_rate{0, 1};
    Rational time_base{1, 1000};
    int64_t duration_ms = 0;
    int audio_streams = 0;
    int subtitle_streams = 0;
    std::vector<AudioTrackInfo> audio_tracks;
    int primary_audio_index = -1;
    std::vector<PassthroughStreamInfo> passthrough_streams;
    bool hdr_source = false;
    std::string format_name;
    std::vector<uint8_t> extradata;
    bool annex_b = false;
    ColorDescription color{ColorPrimaries::BT709, TransferFunction::BT709, MatrixCoefficients::BT709, ColorRange::Limited};
    Hdr10Metadata hdr{};
    std::array<int32_t, 9> display_matrix{};
    bool has_display_matrix{false};
};

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: Manages libavformat AVFormatContext, bitstream filter state, optional MemoryAvioContext lifetime, and monotonic 705.6 MHz Flicks rescaling across packet reads.
// Invariant: format_ != nullptr after open(); memory_avio_ is detached before avformat_close_input() so custom I/O never double-frees.
class FfmpegDemuxer final {
public:
    explicit FfmpegDemuxer(std::string container_hint = {});
    FfmpegDemuxer(std::vector<uint8_t> bytes, std::string container_hint);
    ~FfmpegDemuxer() noexcept;

    FfmpegDemuxer(const FfmpegDemuxer&) = delete;
    FfmpegDemuxer& operator=(const FfmpegDemuxer&) = delete;

    [[nodiscard]] Result<MediaInfo> open(const std::filesystem::path& path);
    [[nodiscard]] Result<Packet> readPacket();
    [[nodiscard]] std::vector<std::string> warnings() const { return warnings_; }
    [[nodiscard]] int audioStreamIndex() const noexcept { return audio_stream_; }
    [[nodiscard]] Result<void> seekToKeyframeBefore(int64_t video_stream_pts);

private:
    bool isPassthroughIndex(int stream_index) const noexcept;
    Result<MediaInfo> finishOpen(const std::filesystem::path& path);
    void configureBitstreamFilter(const char* name);
    void drainBitstreamFilter();
    int64_t projectWithTimebase(const AVPacket& source, Rational time_base,
                                FlicksDriftCompensator& drift,
                                FlicksMonotonicGuard& guard, bool* valid) noexcept;
    int64_t projectVideoPtsToFlicks(int64_t pts, Rational time_base,
                                    FlicksDriftCompensator& drift, bool* valid) noexcept;
    Packet convertPacket(const AVPacket& source);
    void close() noexcept;

    std::vector<uint8_t> memory_bytes_;
    std::string container_hint_;
    bool memory_source_ = false;
    std::shared_ptr<void> memory_avio_;
    AVFormatContext* format_ = nullptr;
    AVBSFContext* bitstream_filter_ = nullptr;
    AVPacket* packet_ = nullptr;
    std::deque<Packet> pending_;
    std::vector<std::string> warnings_;
    MediaInfo info_;
    FlicksDriftCompensator video_drift_;
    FlicksDriftCompensator audio_drift_;
    FlicksMonotonicGuard video_guard_;
    FlicksMonotonicGuard audio_guard_;
    int64_t last_audio_flicks_ = 0;
    bool last_audio_flicks_valid_ = false;
    int video_stream_ = -1;
    int audio_stream_ = -1;
    std::vector<int> passthrough_indices_;
    bool drained_ = false;
    bool bitstream_filter_flushed_ = false;
};

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: Coordinates libavformat output context, interleaved packet mutex, HDR side-data injection, per-stream timestamp guards, and idempotent trailer finalization.
// Invariant: writePacket() is valid only after open(); finalize() writes the container trailer once and releases AVIO/format resources.
class FfmpegMuxer final {
public:
    FfmpegMuxer() = default;
    explicit FfmpegMuxer(std::vector<uint8_t>* memory_target, std::string format);
    ~FfmpegMuxer() noexcept;

    FfmpegMuxer(const FfmpegMuxer&) = delete;
    FfmpegMuxer& operator=(const FfmpegMuxer&) = delete;

    [[nodiscard]] Result<void> open(const std::filesystem::path& path, const MediaInfo& info,
                                    const Hdr10Metadata& hdr);
    [[nodiscard]] Result<void> writePacket(const Packet& packet);
    [[nodiscard]] Result<void> writeAudioPacket(const Packet& packet) { return writePacket(packet); }
    [[nodiscard]] Result<void> finalize();

private:
    Result<void> applyHdrSideData(const Hdr10Metadata& hdr);
    void close() noexcept;

    std::mutex muxer_mutex_;
    AVStream* audio_stream_ = nullptr;
    int audio_stream_index_ = -1;
    int src_audio_stream_index_ = -1;
    Rational in_audio_time_base_{1, 48000};
    Rational out_audio_time_base_{1, 48000};
    // (source_stream_index, output_av_stream, monotonic_timestamp_guard)
    std::vector<std::tuple<int, AVStream*, StreamTimestampGuard>> passthrough_streams_;
    StreamTimestampGuard video_guard_ = makeTimestampGuard();
    std::vector<std::string> warnings_;
    std::vector<uint8_t>* memory_target_ = nullptr;
    std::string memory_format_;
    std::shared_ptr<void> memory_avio_;
    AVFormatContext* format_ = nullptr;
    AVStream* stream_ = nullptr;
    AVPacket* packet_ = nullptr;
    std::string container_;
    Hdr10Metadata hdr_{};
    bool annex_b_ = false;
    bool opened_ = false;
};

std::unique_ptr<FfmpegDemuxer> makeFfmpegDemuxer();
std::unique_ptr<FfmpegMuxer> makeFfmpegMuxer();

constexpr bool ffmpegAvailable() noexcept { return true; }

std::unique_ptr<FfmpegDemuxer> makeMemoryDemuxer(std::vector<uint8_t> bytes);
std::unique_ptr<FfmpegMuxer> makeMemoryMuxer(std::vector<uint8_t>* output,
                                             std::string container_format = "matroska");

}  // namespace rtx
