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

inline std::vector<uint8_t> readAll(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>());
}

inline bool writeAll(const std::string& path, const std::vector<uint8_t>& data) {
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
