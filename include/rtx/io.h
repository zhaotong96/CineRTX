// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// I/O abstractions.  FFmpeg-backed demux/mux is compiled only when the build
// enables RTX_WITH_FFMPEG; raw NV12/P010 paths are always available and are
// what the hermetic CI suite exercises.

#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "rtx/status.h"
#include "rtx/types.h"

namespace rtx {

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
    virtual Result<MediaInfo> open(const std::string& path) = 0;
    virtual Result<Packet> readPacket() = 0;
    virtual std::vector<std::string> warnings() const { return {}; }
};

class IMuxer {
public:
    virtual ~IMuxer() = default;
    virtual Status open(const std::string& path, const MediaInfo& info,
                        const Hdr10Metadata& hdr) = 0;
    virtual Status writePacket(const Packet& packet) = 0;
    virtual Status finalize() = 0;
};

std::unique_ptr<IDemuxer> makeFfmpegDemuxer();
std::unique_ptr<IMuxer> makeFfmpegMuxer();
bool ffmpegAvailable();

// Deterministic synthetic source used by tests and `--simulate`.  Produces
// BT.709 limited-range NV12 with a moving high-contrast edge and an optional
// hard cut at `scene_cut_frame`.
class SyntheticNv12Source {
public:
    struct Config {
        int width = 1920;
        int height = 1080;
        int frame_count = 120;
        Rational frame_rate{30, 1};
        int scene_cut_frame = -1;
        int bar_speed = 7;
    };

    explicit SyntheticNv12Source(Config config);

    Result<Frame> next();
    bool done() const noexcept { return index_ >= config_.frame_count; }
    int64_t index() const noexcept { return index_; }
    const Config& config() const noexcept { return config_; }
    void reset() { index_ = 0; }

private:
    Config config_;
    int64_t index_ = 0;
    std::shared_ptr<FrameBuffer> previous_;
};

// Reads a headerless NV12 stream.  Frame size is derived from width/height.
class RawNv12Reader {
public:
    Status open(const std::string& path, int width, int height);
    Result<Frame> read(int64_t pts, Rational time_base);
    bool eof() const noexcept { return eof_; }
    int64_t frameCount() const noexcept { return frame_count_; }
    void close();

private:
    std::ifstream stream_;
    int width_ = 0;
    int height_ = 0;
    int64_t frame_count_ = 0;
    bool eof_ = false;
};

// Writes tightly packed 10-bit P010.  This is the verification artefact used by
// the color/HDR gate and by ffmpeg-based playback checks.
class RawP010Writer {
public:
    Status open(const std::string& path, int width, int height);
    Status write(const Frame& frame);
    Status finalize();
    uint64_t bytesWritten() const noexcept { return bytes_written_; }

private:
    std::ofstream stream_;
    int width_ = 0;
    int height_ = 0;
    uint64_t bytes_written_ = 0;
};

// Sidecar metadata for QA tooling and container muxers.
Status writeHdrSidecar(const std::string& path, const Hdr10Metadata& metadata);
Result<Hdr10Metadata> readHdrSidecar(const std::string& path);

}  // namespace rtx
