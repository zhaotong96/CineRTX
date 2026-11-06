// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#pragma once

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "rtx/types.h"

namespace rtx::test {

class TempDir {
public:
    explicit TempDir(const std::string& prefix = "rtx-tests") {
        static std::atomic<uint64_t> counter{0};
        const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const uint64_t sequence = counter.fetch_add(1, std::memory_order_relaxed);
        path_ = std::filesystem::temp_directory_path() /
                (prefix + "-" + std::to_string(stamp) + "-" + std::to_string(sequence));
        std::error_code error;
        std::filesystem::create_directories(path_, error);
    }

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const noexcept { return path_; }
    std::string file(const std::string& name) const { return (path_ / name).string(); }

private:
    std::filesystem::path path_;
};

inline std::vector<uint8_t> readAll(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>());
}

inline bool writeAll(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    if (!data.empty()) {
        stream.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
    }
    return static_cast<bool>(stream);
}

inline Result<void> allocateTestFrame(Frame& frame, int frame_width, int frame_height,
                                      PixelFormat frame_format, size_t alignment = 256) {
    if (frame_width <= 0 || frame_height <= 0 || frame_format == PixelFormat::Unknown) {
        return err(ErrorCode::InvalidArgument, "invalid frame geometry");
    }
    int local_pitches[3] = {0, 0, 0};
    int local_chroma_width = 0;
    int local_chroma_height = 0;
    const size_t total = frameByteSize(frame_width, frame_height, frame_format, local_pitches,
                                       &local_chroma_width, &local_chroma_height);
    if (total == 0) {
        return err(ErrorCode::InvalidArgument, "unsupported frame format");
    }
    auto buffer = std::make_shared<FrameBuffer>();
    const Result<void> status = buffer->allocate(total, alignment);
    if (!status.has_value()) {
        return std::unexpected(status.error());
    }
    frame.width = frame_width;
    frame.height = frame_height;
    frame.format = frame_format;
    frame.bit_depth = isTenBit(frame_format) ? 10 : 8;
    frame.chroma_width = local_chroma_width;
    frame.chroma_height = local_chroma_height;
    frame.storage = std::move(buffer);

    uint8_t* base = frame.storage->data();
    frame.planes[0] = base;
    frame.planes[1] = nullptr;
    frame.planes[2] = nullptr;
    frame.pitches[0] = local_pitches[0];
    frame.pitches[1] = local_pitches[1];
    frame.pitches[2] = local_pitches[2];

    if (planeCount(frame_format) >= 2) {
        frame.planes[1] = base + planeByteSize(frame_format, frame_width, frame_height, 0, frame.pitches[0]);
    }
    if (planeCount(frame_format) >= 3) {
        frame.planes[2] = frame.planes[1] + planeByteSize(frame_format, frame_width, frame_height, 1, frame.pitches[1]);
    }
    for (int plane = 0; plane < 3; ++plane) {
        frame.plane_sizes[plane] = planeByteSize(frame_format, frame_width, frame_height, plane, frame.pitches[plane]);
    }
    return {};
}
inline void fillNV12(Frame& frame, uint8_t luma, uint8_t chroma_u, uint8_t chroma_v) {
    for (int y = 0; y < frame.height; ++y) {
        std::memset(frame.planes[0] + static_cast<size_t>(y) * frame.pitches[0], luma,
                    static_cast<size_t>(frame.width));
    }
    for (int y = 0; y < frame.chroma_height; ++y) {
        uint8_t* row = frame.planes[1] + static_cast<size_t>(y) * frame.pitches[1];
        for (int x = 0; x < frame.chroma_width; ++x) {
            row[x * 2] = chroma_u;
            row[x * 2 + 1] = chroma_v;
        }
    }
}

inline void fillP010(Frame& frame, uint16_t luma10, uint16_t chroma_u10, uint16_t chroma_v10) {
    for (int y = 0; y < frame.height; ++y) {
        uint16_t* row = reinterpret_cast<uint16_t*>(frame.planes[0] +
                                                    static_cast<size_t>(y) * frame.pitches[0]);
        for (int x = 0; x < frame.width; ++x) {
            row[x] = static_cast<uint16_t>(luma10 << 6);
        }
    }
    for (int y = 0; y < frame.chroma_height; ++y) {
        uint16_t* row = reinterpret_cast<uint16_t*>(frame.planes[1] +
                                                    static_cast<size_t>(y) * frame.pitches[1]);
        for (int x = 0; x < frame.chroma_width; ++x) {
            row[x * 2] = static_cast<uint16_t>(chroma_u10 << 6);
            row[x * 2 + 1] = static_cast<uint16_t>(chroma_v10 << 6);
        }
    }
}

}  // namespace rtx::test
