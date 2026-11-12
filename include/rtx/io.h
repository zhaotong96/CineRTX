// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// I/O abstractions.  The FFmpeg container layer is an unconditional statically
// linked core component (Gate 3.0): every build of rtx_core carries
// libavformat/libavcodec/libavutil/libswresample machine code, and there is no
// configuration switch that can remove it.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "rtx/status.h"
#include "rtx/types.h"

namespace rtx {

// Phase 7 (Task 7-B3): one source stream eligible for bitstream-level
// passthrough into the output container (every audio stream plus the
// subtitle streams the output container can carry natively).
struct PassthroughStreamInfo {
    int stream_index = -1;
    int codec_type = 0;  // 1 = audio (AVMEDIA_TYPE_AUDIO), 2 = subtitle
    int codec_id = 0;
    std::string codec_name;
    Rational time_base{1, 1000};
    // FFmpeg AVCodecParameters clone owning the real extradata/layout.
    std::shared_ptr<void> opaque_codecpar;
};

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
    // Phase 7 (Task 7-B3 / verdict §3.2): full multi-audio + subtitle
    // passthrough set (supersedes the single primary_audio_index).
    std::vector<PassthroughStreamInfo> passthrough_streams;
    bool hdr_source = false;
    std::string format_name;

    // Codec configuration record exactly as stored in the source container
    // (avcC / hvcC / AV1CodecConfigurationRecord).  Empty for Annex-B streams.
    std::vector<uint8_t> extradata;
    // True when packets produced/consumed by this side of the pipeline use
    // Annex-B start codes.  NVDEC/NVENC speak Annex-B; MP4 stores length
    // prefixed NAL units.
    bool annex_b = false;
    ColorDescription color{};
    Hdr10Metadata hdr{};
};

class IDemuxer {
public:
    virtual ~IDemuxer() = default;
    virtual Result<MediaInfo> open(const std::filesystem::path& path) = 0;
    virtual Result<Packet> readPacket() = 0;
    virtual std::vector<std::string> warnings() const { return {}; }
    // Index of the best audio stream, or -1 when the container has none.
    virtual int audioStreamIndex() const noexcept { return -1; }
    // Phase 7 scrubbing / Phase 8 checkpoint recovery: repositions the video
    // stream to the latest key frame at or before `video_stream_pts` (PTS in
    // the video stream's own time base).  Subsequent readPacket() calls
    // deliver packets from that point.
    virtual Result<void> seekToKeyframeBefore(int64_t video_stream_pts) { return err(ErrorCode::Unsupported, "this demuxer cannot seek"); }
};

class IMuxer {
public:
    virtual ~IMuxer() = default;
    virtual Result<void> open(const std::filesystem::path& path, const MediaInfo& info,
                        const Hdr10Metadata& hdr) = 0;
    virtual Result<void> writePacket(const Packet& packet) = 0;
    virtual Result<void> writeAudioPacket(const Packet& packet) { return writePacket(packet); }
    virtual Result<void> finalize() = 0;
};

std::unique_ptr<IDemuxer> makeFfmpegDemuxer();
std::unique_ptr<IMuxer> makeFfmpegMuxer();

// ---------------------------------------------------------------------------
// Phase 8 (Task 8-B1): broadcast-tier software decoder (ProRes / DNxHR).
// NVDEC has no hardware support for these codecs, so the frames are decoded
// with the statically linked FFmpeg decoders and converted to P010 planes in
// host memory; the bridge then DMA-uploads them into device memory for the
// normal VSR + TrueHDR + NVENC remaster path.
// ---------------------------------------------------------------------------
struct DecodedVideoFrame {
    std::vector<uint8_t> y_plane;   // P010 luma (uint16 LE, 10-bit << 6)
    std::vector<uint8_t> uv_plane;  // interleaved UV (uint16 LE, << 6)
    uint32_t y_pitch = 0;           // bytes per row
    uint32_t uv_pitch = 0;          // bytes per row
    int width = 0;
    int height = 0;
    int64_t pts_90k = 0;
    int64_t dts_90k = 0;
};

class IDemuxer;

// Compile-time invariant: the media container layer is always present.
constexpr bool ffmpegAvailable() noexcept { return true; }

// ---------------------------------------------------------------------------
// Zero-disk container I/O (ACT-STATIC-03)
// ---------------------------------------------------------------------------
std::unique_ptr<IDemuxer> makeMemoryDemuxer(std::vector<uint8_t> bytes);
std::unique_ptr<IMuxer> makeMemoryMuxer(std::vector<uint8_t>* output,
                                        std::string container_format = "matroska");

}  // namespace rtx
