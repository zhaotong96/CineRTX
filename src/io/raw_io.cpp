// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/io.h"

#include <algorithm>
#include <cstring>

#include "rtx/color.h"
#include "rtx/hdr.h"

namespace rtx {

SyntheticNv12Source::SyntheticNv12Source(Config config) : config_(config) {
    if (config_.width <= 0) {
        config_.width = 1920;
    }
    if (config_.height <= 0) {
        config_.height = 1080;
    }
    if (config_.frame_count < 0) {
        config_.frame_count = 0;
    }
}

Result<Frame> SyntheticNv12Source::next() {
    if (done()) {
        return Result<Frame>::fail(ErrorCode::NotFound, "synthetic source is exhausted");
    }
    Frame frame;
    const Status allocation = frame.allocateHost(config_.width, config_.height, PixelFormat::NV12);
    if (!allocation.ok()) {
        return Result<Frame>::fromStatus(allocation);
    }

    const int64_t index = index_;
    const bool cut = config_.scene_cut_frame >= 0 && index >= config_.scene_cut_frame;
    const int bar = static_cast<int>((index * config_.bar_speed) % std::max(1, config_.width));
    const int bar_width = std::max(8, config_.width / 12);
    const uint8_t* uv = frame.planes[1];

    for (int y = 0; y < config_.height; ++y) {
        uint8_t* row = frame.planes[0] + static_cast<size_t>(y) * frame.pitches[0];
        const int gradient = (y * 180) / std::max(1, config_.height);
        for (int x = 0; x < config_.width; ++x) {
            int value = 16 + gradient + ((x * 100) / std::max(1, config_.width));
            if (x >= bar && x < bar + bar_width) {
                value = cut ? 220 : 60;
            }
            if (cut) {
                value = 235 - value / 2;
            }
            row[x] = static_cast<uint8_t>(std::clamp(value, 16, 235));
        }
    }
    for (int y = 0; y < (config_.height + 1) / 2; ++y) {
        uint8_t* row = frame.planes[1] + static_cast<size_t>(y) * frame.pitches[1];
        for (int x = 0; x < config_.width; x += 2) {
            const int phase = (x / 2 + y + static_cast<int>(index)) % 64;
            row[x] = static_cast<uint8_t>(128 + (phase - 32) / 4);
            row[x + 1] = static_cast<uint8_t>(128 + (32 - phase) / 4);
        }
    }
    (void)uv;

    frame.pts = index;
    frame.time_base = Rational{config_.frame_rate.den, config_.frame_rate.num};
    frame.duration = 1;
    frame.keyframe = index % 30 == 0;
    frame.color = ColorDescription{ColorPrimaries::BT709, TransferFunction::BT709,
                                   MatrixCoefficients::BT709, ColorRange::Limited};
    ++index_;
    return Result<Frame>::ok(std::move(frame));
}

Status RawNv12Reader::open(const std::string& path, int width, int height) {
    close();
    if (width <= 0 || height <= 0) {
        return Status::fail(ErrorCode::InvalidArgument, "invalid raw NV12 geometry");
    }
    stream_.open(path, std::ios::binary);
    if (!stream_) {
        return Status::fail(ErrorCode::Io, "failed to open raw NV12 input", path);
    }
    width_ = width;
    height_ = height;
    frame_count_ = 0;
    eof_ = false;
    return Status::success();
}

Result<Frame> RawNv12Reader::read(int64_t pts, Rational time_base) {
    if (!stream_.is_open()) {
        return Result<Frame>::fail(ErrorCode::State, "raw NV12 reader is not open");
    }
    if (eof_) {
        return Result<Frame>::fail(ErrorCode::NotFound, "raw NV12 reader reached EOF");
    }
    if (stream_.peek() == std::char_traits<char>::eof()) {
        eof_ = true;
        return Result<Frame>::fail(ErrorCode::NotFound, "raw NV12 reader reached EOF");
    }
    Frame frame;
    const Status allocation = frame.allocateHost(width_, height_, PixelFormat::NV12);
    if (!allocation.ok()) {
        return Result<Frame>::fromStatus(allocation);
    }
    const int chroma_height = (height_ + 1) / 2;
    for (int y = 0; y < height_; ++y) {
        stream_.read(reinterpret_cast<char*>(frame.planes[0] + static_cast<size_t>(y) * frame.pitches[0]),
                     width_);
        if (stream_.gcount() != width_) {
            eof_ = true;
            return Result<Frame>::fail(ErrorCode::Io, "truncated raw NV12 luma plane");
        }
    }
    for (int y = 0; y < chroma_height; ++y) {
        stream_.read(reinterpret_cast<char*>(frame.planes[1] + static_cast<size_t>(y) * frame.pitches[1]),
                     width_);
        if (stream_.gcount() != width_) {
            eof_ = true;
            return Result<Frame>::fail(ErrorCode::Io, "truncated raw NV12 chroma plane");
        }
    }
    frame.pts = pts;
    frame.time_base = time_base;
    frame.duration = 1;
    frame.color = ColorDescription{ColorPrimaries::BT709, TransferFunction::BT709,
                                   MatrixCoefficients::BT709, ColorRange::Limited};
    ++frame_count_;
    return Result<Frame>::ok(std::move(frame));
}

void RawNv12Reader::close() {
    if (stream_.is_open()) {
        stream_.close();
    }
    width_ = 0;
    height_ = 0;
    frame_count_ = 0;
    eof_ = false;
}

Status RawP010Writer::open(const std::string& path, int width, int height) {
    if (width <= 0 || height <= 0) {
        return Status::fail(ErrorCode::InvalidArgument, "invalid raw P010 geometry");
    }
    if (stream_.is_open()) {
        stream_.close();
    }
    stream_.open(path, std::ios::binary | std::ios::trunc);
    if (!stream_) {
        return Status::fail(ErrorCode::Io, "failed to open P010 output", path);
    }
    width_ = width;
    height_ = height;
    bytes_written_ = 0;
    return Status::success();
}

Status RawP010Writer::write(const Frame& frame) {
    if (!stream_.is_open()) {
        return Status::fail(ErrorCode::State, "P010 writer is not open");
    }
    if (frame.format != PixelFormat::P010 || frame.width != width_ || frame.height != height_) {
        return Status::fail(ErrorCode::InvalidArgument, "P010 frame does not match writer");
    }
    const int chroma_height = (height_ + 1) / 2;
    for (int y = 0; y < height_; ++y) {
        stream_.write(reinterpret_cast<const char*>(frame.planes[0] +
                                                    static_cast<size_t>(y) * frame.pitches[0]),
                      static_cast<std::streamsize>(width_) * 2);
    }
    for (int y = 0; y < chroma_height; ++y) {
        stream_.write(reinterpret_cast<const char*>(frame.planes[1] +
                                                    static_cast<size_t>(y) * frame.pitches[1]),
                      static_cast<std::streamsize>(width_) * 2);
    }
    if (!stream_) {
        return Status::fail(ErrorCode::Io, "failed to write P010 output");
    }
    bytes_written_ += static_cast<uint64_t>(width_) * static_cast<uint64_t>(height_) * 3ull;
    return Status::success();
}

Status RawP010Writer::finalize() {
    if (!stream_.is_open()) {
        return Status::success();
    }
    stream_.flush();
    stream_.close();
    if (!stream_) {
        return Status::fail(ErrorCode::Io, "failed to finalize P010 output");
    }
    return Status::success();
}

Status writeHdrSidecar(const std::string& path, const Hdr10Metadata& metadata) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return Status::fail(ErrorCode::Io, "failed to open HDR sidecar", path);
    }
    const std::string json = SeiSynthesizer::toJson(metadata);
    stream.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!stream) {
        return Status::fail(ErrorCode::Io, "failed to write HDR sidecar", path);
    }
    return Status::success();
}

Result<Hdr10Metadata> readHdrSidecar(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return Result<Hdr10Metadata>::fail(ErrorCode::Io, "failed to open HDR sidecar", path);
    }
    std::string json((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return SeiSynthesizer::fromJson(json);
}

}  // namespace rtx
