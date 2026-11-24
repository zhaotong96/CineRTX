// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#pragma once

#include <algorithm>
#include <bit>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "rtx/rational.h"
#include "rtx/status.h"

namespace rtx {

// Cacheline isolation (64-byte alignment or standard interference size)
#ifdef __cpp_lib_hardware_interference_size
inline constexpr std::size_t kCacheLineSize = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t kCacheLineSize = 64;
#endif

// Phase 12 (Wave 12.2-D): dead TaggedHandle template & unused aliases removed.
// Phase 8 (Task 8-B1): broadcast codecs NVDEC cannot decode; the engine
// routes them through the FFmpeg software decoder (broadcast tier).

using DevicePtr = uint64_t;
inline constexpr DevicePtr kInvalidDevicePtr = 0;

enum class PixelFormat : int32_t {
    Unknown = 0,
    NV12 = 1,
    P010 = 2,
    RGB32 = 3,       // DXGI_FORMAT_B8G8R8A8_UNORM / packed BGRA
    R10G10B10A2 = 4, // DXGI_FORMAT_R10G10B10A2_UNORM
    RGBA16F = 5,     // DXGI_FORMAT_R16G16B16A16_FLOAT
    YUV420P10 = 6,   // planar 10-bit, used by reference code only
};

enum class VideoCodec : int32_t {
    Unknown = 0,
    H264 = 1,
    HEVC = 2,
    AV1 = 3,
    ProRes = 4,
    Dnxhd = 5,
};

[[nodiscard]] constexpr const char* videoCodecName(VideoCodec codec) noexcept {
    switch (codec) {
        case VideoCodec::H264:
            return "h264";
        case VideoCodec::HEVC:
            return "hevc";
        case VideoCodec::AV1:
            return "av1";
        case VideoCodec::ProRes:
            return "prores";
        case VideoCodec::Dnxhd:
            return "dnxhd";
        case VideoCodec::Unknown:
            break;
    }
    return "unknown";
}
[[nodiscard]] inline VideoCodec videoCodecFromName(std::string_view name) noexcept {
    if (name == "h264" || name == "avc" || name == "avc1") {
        return VideoCodec::H264;
    }
    if (name == "hevc" || name == "h265" || name == "hvc1" || name == "hev1") {
        return VideoCodec::HEVC;
    }
    if (name == "av1" || name == "av01") {
        return VideoCodec::AV1;
    }
    return VideoCodec::Unknown;
}
[[nodiscard]] constexpr const char* pixelFormatName(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::NV12:
            return "NV12";
        case PixelFormat::P010:
            return "P010";
        case PixelFormat::RGB32:
            return "RGB32";
        case PixelFormat::R10G10B10A2:
            return "R10G10B10A2";
        case PixelFormat::RGBA16F:
            return "RGBA16F";
        case PixelFormat::YUV420P10:
            return "YUV420P10";
        case PixelFormat::Unknown:
            break;
    }
    return "UNKNOWN";
}
[[nodiscard]] constexpr size_t bytesPerPixel(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::NV12:
            return 1;
        case PixelFormat::P010:
        case PixelFormat::YUV420P10:
            return 2;
        case PixelFormat::RGB32:
        case PixelFormat::R10G10B10A2:
            return 4;
        case PixelFormat::RGBA16F:
            return 8;
        case PixelFormat::Unknown:
            break;
    }
    return 0;
}
[[nodiscard]] constexpr int planeCount(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::NV12:
        case PixelFormat::P010:
            return 2;
        case PixelFormat::YUV420P10:
            return 3;
        case PixelFormat::RGB32:
        case PixelFormat::R10G10B10A2:
        case PixelFormat::RGBA16F:
            return 1;
        case PixelFormat::Unknown:
            break;
    }
    return 0;
}
[[nodiscard]] constexpr bool isTenBit(PixelFormat format) noexcept {
    return format == PixelFormat::P010 || format == PixelFormat::YUV420P10 ||
           format == PixelFormat::R10G10B10A2;
}
[[nodiscard]] constexpr size_t alignUp(size_t value, size_t alignment) noexcept {
    if (alignment == 0) {
        return value;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}
[[nodiscard]] constexpr size_t planeByteSize(PixelFormat format, int width, int height, int plane, int pitch) {
    if (width <= 0 || height <= 0 || plane < 0) {
        return 0;
    }
    const int chroma_height = (height + 1) / 2;
    switch (format) {
        case PixelFormat::NV12:
        case PixelFormat::P010:
            if (plane == 0) {
                return static_cast<size_t>(pitch) * static_cast<size_t>(height);
            }
            if (plane == 1) {
                return static_cast<size_t>(pitch) * static_cast<size_t>(chroma_height);
            }
            break;
        case PixelFormat::YUV420P10:
            if (plane == 0) {
                return static_cast<size_t>(pitch) * static_cast<size_t>(height);
            }
            if (plane == 1 || plane == 2) {
                return static_cast<size_t>(pitch) * static_cast<size_t>(chroma_height);
            }
            break;
        case PixelFormat::RGB32:
        case PixelFormat::R10G10B10A2:
        case PixelFormat::RGBA16F:
            if (plane == 0) {
                return static_cast<size_t>(pitch) * static_cast<size_t>(height);
            }
            break;
        case PixelFormat::Unknown:
            break;
    }
    return 0;
}
inline size_t frameByteSize(int width, int height, PixelFormat format, int* pitches = nullptr,
                            int* chroma_width = nullptr, int* chroma_height = nullptr) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    int local_pitches[3] = {0, 0, 0};
    const int local_chroma_width = (width + 1) / 2;
    const int local_chroma_height = (height + 1) / 2;

    switch (format) {
        case PixelFormat::NV12:
            local_pitches[0] = static_cast<int>(alignUp(static_cast<size_t>(width), 256));
            local_pitches[1] = local_pitches[0];
            break;
        case PixelFormat::P010:
        case PixelFormat::YUV420P10:
            local_pitches[0] = static_cast<int>(alignUp(static_cast<size_t>(width) * 2, 256));
            local_pitches[1] = local_pitches[0];
            local_pitches[2] = local_pitches[0];
            break;
        case PixelFormat::RGB32:
        case PixelFormat::R10G10B10A2:
            local_pitches[0] = static_cast<int>(alignUp(static_cast<size_t>(width) * 4, 256));
            break;
        case PixelFormat::RGBA16F:
            local_pitches[0] = static_cast<int>(alignUp(static_cast<size_t>(width) * 8, 256));
            break;
        case PixelFormat::Unknown:
            return 0;
    }
    size_t total = 0;
    const int planes = planeCount(format);
    for (int plane = 0; plane < planes; ++plane) {
        total += planeByteSize(format, width, height, plane, local_pitches[plane]);
    }
    if (pitches != nullptr) {
        pitches[0] = local_pitches[0];
        pitches[1] = local_pitches[1];
        pitches[2] = local_pitches[2];
    }
    if (chroma_width != nullptr) {
        *chroma_width = local_chroma_width;
    }
    if (chroma_height != nullptr) {
        *chroma_height = local_chroma_height;
    }
    return total;
}
enum class ColorPrimaries : int32_t {
    BT709 = 1,
    BT2020 = 9,
};

enum class TransferFunction : int32_t {
    BT709 = 1,
    SMPTE2084 = 16,
    HLG = 18,
};

enum class MatrixCoefficients : int32_t {
    BT709 = 1,
    BT2020NC = 9,
};

enum class ColorRange : int32_t {
    Limited = 0,
    Full = 1,
};

// Phase 12 (Wave 12.2-C): tuple alias (primaries, transfer, matrix, range).
using ColorDescription = std::tuple<ColorPrimaries, TransferFunction, MatrixCoefficients, ColorRange>;

// Phase 12 (Wave 12.2-C): tuple alias (red_x, red_y, green_x, green_y, blue_x, blue_y,
//   white_x, white_y, max_luminance, min_luminance).
using MasteringDisplay = std::tuple<uint16_t, uint16_t, uint16_t, uint16_t,
                                    uint16_t, uint16_t, uint16_t, uint16_t,
                                    uint32_t, uint32_t>;

// Phase 12 (Wave 12.2-C): standard pair (max_cll, max_fall).
using ContentLightLevel = std::pair<uint16_t, uint16_t>;

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: HDR10 静态元数据领域聚合，组合主显示色度、内容亮度、色彩描述与目标峰值尼特数，贯穿 SEI/OBU 合成与编码器配置。
// Invariant: enabled 为 true 时 color/mastering/cll 与 target_nits 共同定义合规 PQ/BT.2020 信号。
struct Hdr10Metadata {
    bool enabled = false;
    MasteringDisplay mastering{};
    ContentLightLevel cll{};
    ColorDescription color{ColorPrimaries::BT2020, TransferFunction::SMPTE2084,
                           MatrixCoefficients::BT2020NC, ColorRange::Limited};
    uint32_t target_nits = 1000;
};


// Phase 12 (Wave 12.2-C): tuple (bytes, alignment, logical_size, offset) + free helpers.
using FrameBuffer = std::tuple<std::vector<uint8_t>, size_t, size_t, size_t>;

[[nodiscard]] inline Result<void> allocateFrameBuffer(FrameBuffer& buf, size_t size,
                                                      size_t align = 256) {
    if (size == 0) return err(ErrorCode::InvalidArgument, "zero-sized frame buffer");
    auto& [bytes, alignment, logical_size, offset] = buf;
    alignment = std::max<size_t>(align, 64);
    if (size > std::numeric_limits<size_t>::max() - alignment) {
        return err(ErrorCode::InvalidArgument, "frame allocation size overflow");
    }
    try {
        bytes.assign(size + alignment - 1, 0);
    } catch (const std::bad_alloc&) {
        return err(ErrorCode::OutOfMemory, "frame allocation failed", std::to_string(size));
    }
    const uintptr_t raw = reinterpret_cast<uintptr_t>(bytes.data());
    const size_t remainder = raw % alignment;
    offset = remainder == 0 ? 0 : alignment - remainder;
    logical_size = size;
    return {};
}
[[nodiscard]] inline uint8_t* frameBufferData(FrameBuffer& buf) noexcept {
    auto& [bytes, alignment, logical_size, offset] = buf;
    (void)alignment; (void)logical_size;
    return bytes.data() + offset;
}
[[nodiscard]] inline const uint8_t* frameBufferData(const FrameBuffer& buf) noexcept {
    const auto& [bytes, alignment, logical_size, offset] = buf;
    (void)alignment; (void)logical_size;
    return bytes.data() + offset;
}
[[nodiscard]] inline size_t frameBufferSize(const FrameBuffer& buf) noexcept {
    const auto& [bytes, alignment, logical_size, offset] = buf;
    (void)bytes; (void)alignment; (void)offset;
    return logical_size;
}

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 跨解码/滤镜/编码管线的视频帧描述聚合，封装像素格式、平面步长、设备/主机存储句柄、Flicks 时间戳与 HDR10 附带信息。
// Invariant: 有效帧满足 width > 0 && height > 0，plane_count 不超过 kMaxPlanes 且主平面指针非空。
struct Frame {
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::Unknown;
    int bit_depth = 8;
    int chroma_width = 0;
    int chroma_height = 0;

    std::shared_ptr<FrameBuffer> storage;
    uint8_t* planes[3] = {nullptr, nullptr, nullptr};
    size_t plane_sizes[3] = {0, 0, 0};
    int pitches[3] = {0, 0, 0};

    DevicePtr device_planes[3] = {kInvalidDevicePtr, kInvalidDevicePtr, kInvalidDevicePtr};
    uint32_t device_pitches[3] = {0, 0, 0};

    int64_t pts = 0;
    Rational time_base{1, 1};
    int64_t duration = 0;
    uint64_t fence_value = 0;
    bool keyframe = false;
    ColorDescription color{ColorPrimaries::BT709, TransferFunction::BT709, MatrixCoefficients::BT709, ColorRange::Limited};
    Hdr10Metadata hdr{};

    bool valid() const noexcept { return width > 0 && height > 0 && format != PixelFormat::Unknown; }
    bool isDeviceResident() const noexcept { return device_planes[0] != kInvalidDevicePtr; }
    size_t hostSize() const noexcept { return storage ? frameBufferSize(*storage) : 0; }

    std::span<uint8_t> planeSpan(int plane) noexcept {
        if (plane < 0 || plane >= 3 || !planes[plane]) {
            return {};
        }
        return std::span<uint8_t>(planes[plane], plane_sizes[plane]);
    }
    std::span<const uint8_t> planeSpan(int plane) const noexcept {
        if (plane < 0 || plane >= 3 || !planes[plane]) {
            return {};
        }
        return std::span<const uint8_t>(planes[plane], plane_sizes[plane]);
    }
    void copyMetadataFrom(const Frame& other) {
        width = other.width;
        height = other.height;
        format = other.format;
        bit_depth = other.bit_depth;
        chroma_width = other.chroma_width;
        chroma_height = other.chroma_height;

        for (int i = 0; i < 3; ++i) {
            pitches[i] = other.pitches[i];
            plane_sizes[i] = other.plane_sizes[i];
            device_planes[i] = other.device_planes[i];
            device_pitches[i] = other.device_pitches[i];
        }
        pts = other.pts;
        time_base = other.time_base;
        duration = other.duration;
        fence_value = other.fence_value;
        keyframe = other.keyframe;
        color = other.color;
        hdr = other.hdr;
    }
    Frame cloneMetadata() const {
        Frame frame;
        frame.copyMetadataFrom(*this);
        return frame;
    }
};


// Phase 12 (Wave 12.2-C): tuple alias (stream_index, sample_rate, channels, opaque_codecpar).
using AudioTrackInfo = std::tuple<int, int, int, std::shared_ptr<void>>;

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 解复用/封装层压缩码流包聚合，承载原始字节载荷、PTS/DTS/Duration、关键帧标志与 Flicks 重标定时间戳。
// Invariant: flicks_valid 为 true 时 pts_flicks 与 (pts, time_base) 严格对齐。
struct Packet {
    std::vector<uint8_t> data;
    int64_t pts = 0;
    int64_t dts = 0;
    int64_t duration = 0;
    bool keyframe = false;
    int stream_index = 0;
    Rational time_base{1, 1000};
    int64_t pts_flicks = 0;
    bool flicks_valid = false;
};

}  // namespace rtx
