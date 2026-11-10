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

// Tagged strong handle: strong typing with zero-cost compile-time semantics
template <typename Tag, std::integral ValueType = std::int64_t,
          ValueType InvalidValue = static_cast<ValueType>(-1)>
struct TaggedHandle {
    using value_type = ValueType;
    using tag_type = Tag;
    static constexpr ValueType kInvalid = InvalidValue;

    ValueType v{kInvalid};

    [[nodiscard]] static constexpr TaggedHandle invalid() noexcept { return TaggedHandle{}; }

    constexpr TaggedHandle() noexcept = default;
    constexpr explicit(!std::is_same_v<ValueType, std::uint32_t>)
        TaggedHandle(ValueType val) noexcept : v(val) {}

    [[nodiscard("Handle validity check should not be ignored")]]
    constexpr bool isValid() const noexcept {
        if constexpr (std::is_signed_v<ValueType>) {
            return v >= 0 && v != kInvalid;
        } else {
            return v != kInvalid;
        }
    }

    [[nodiscard]] constexpr ValueType value() const noexcept { return v; }

    constexpr operator ValueType() const noexcept { return v; }

    static constexpr ValueType visitor(const auto& other) noexcept {
        using OtherT = std::decay_t<decltype(other)>;
        if constexpr (std::is_same_v<OtherT, TaggedHandle>) {
            return other.v;
        } else {
            return static_cast<ValueType>(other);
        }
    }

    constexpr auto operator<=>(const TaggedHandle&) const noexcept = default;
    constexpr auto operator<=>(std::integral auto other) const noexcept {
        return v <=> visitor(other);
    }
    constexpr bool operator==(std::integral auto other) const noexcept {
        return std::is_eq(v <=> visitor(other));
    }
};

// Core domain handle tags
struct SlotTag {};
struct DeviceTag {};
struct SurfaceTag {};
struct StreamTag {};
struct EventTag {};

// Strongly-typed domain handles
using SlotHandle    = TaggedHandle<SlotTag, std::uint32_t, std::numeric_limits<std::uint32_t>::max()>;
using DeviceHandle  = TaggedHandle<DeviceTag, std::int32_t, -1>;
using SurfaceHandle = TaggedHandle<SurfaceTag, std::uint64_t, 0ull>;
using StreamHandle  = TaggedHandle<StreamTag, std::uint64_t, 0ull>;
using EventHandle   = TaggedHandle<EventTag, std::uint64_t, 0ull>;

static_assert(sizeof(SlotHandle) == 4);
static_assert(std::is_trivially_copyable_v<SlotHandle>);
static_assert(std::is_standard_layout_v<SlotHandle>);

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
};

[[nodiscard]] constexpr const char* videoCodecName(VideoCodec codec) noexcept {
    switch (codec) {
        case VideoCodec::H264:
            return "h264";
        case VideoCodec::HEVC:
            return "hevc";
        case VideoCodec::AV1:
            return "av1";
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

struct ColorDescription {
    ColorPrimaries primaries = ColorPrimaries::BT709;
    TransferFunction transfer = TransferFunction::BT709;
    MatrixCoefficients matrix = MatrixCoefficients::BT709;
    ColorRange range = ColorRange::Limited;
};

struct MasteringDisplay {
    uint16_t red_x = 0;
    uint16_t red_y = 0;
    uint16_t green_x = 0;
    uint16_t green_y = 0;
    uint16_t blue_x = 0;
    uint16_t blue_y = 0;
    uint16_t white_x = 0;
    uint16_t white_y = 0;
    uint32_t max_luminance = 0;
    uint32_t min_luminance = 0;
};

struct ContentLightLevel {
    uint16_t max_cll = 0;
    uint16_t max_fall = 0;
};

struct Hdr10Metadata {
    bool enabled = false;
    MasteringDisplay mastering{};
    ContentLightLevel cll{};
    ColorDescription color{ColorPrimaries::BT2020, TransferFunction::SMPTE2084,
                           MatrixCoefficients::BT2020NC, ColorRange::Limited};
    uint32_t target_nits = 1000;
};

struct DeviceInfo {
    std::string name;
    uint8_t luid[8] = {};
    uint32_t node_mask = 0;
    int compute_major = 0;
    int compute_minor = 0;
    uint64_t vram_bytes = 0;
    uint32_t driver_version = 0;
    uint32_t cuda_version = 0;
    bool nvidia = false;

    bool turingOrNewer() const {
        return compute_major > 7 || (compute_major == 7 && compute_minor >= 5);
    }
    std::string toString() const {
        std::string sm = (compute_major > 0)
            ? std::format("sm_{}{}", compute_major, compute_minor)
            : "cpu";
        std::string luid_str;
        for (int i = 0; i < 8; ++i) {
            luid_str += std::format("{:x}", luid[i]);
        }
        return std::format("{} [{}, LUID={}, VRAM={} MiB]",
                           name, sm, luid_str, vram_bytes / (1024ull * 1024ull));
    }
};

struct FrameBuffer {
    std::vector<uint8_t> bytes;
    size_t alignment = 256;

    Result<void> allocate(size_t size, size_t align = 256) {
        if (size == 0) {
            return err(ErrorCode::InvalidArgument, "zero-sized frame buffer");
        }
        alignment = std::max<size_t>(align, 64);
        if (size > std::numeric_limits<size_t>::max() - alignment) {
            return err(ErrorCode::InvalidArgument, "frame allocation size overflow");
        }
        try {
            bytes.assign(size + alignment - 1, 0);
        } catch (const std::bad_alloc&) {
            return err(ErrorCode::OutOfMemory, "frame allocation failed",
                       std::to_string(size));
        }
        const uintptr_t raw = reinterpret_cast<uintptr_t>(bytes.data());
        const size_t remainder = raw % alignment;
        offset_ = remainder == 0 ? 0 : alignment - remainder;
        size_ = size;
        return {};
    }
    uint8_t* data() { return bytes.data() + offset_; }
    const uint8_t* data() const { return bytes.data() + offset_; }
    size_t size() const noexcept { return size_; }

    std::span<uint8_t> span() noexcept { return {data(), size_}; }
    std::span<const uint8_t> span() const noexcept { return {data(), size_}; }

private:
    size_t size_ = 0;
    size_t offset_ = 0;
};

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
    ColorDescription color{};
    Hdr10Metadata hdr{};

    bool valid() const noexcept { return width > 0 && height > 0 && format != PixelFormat::Unknown; }
    bool isDeviceResident() const noexcept { return device_planes[0] != kInvalidDevicePtr; }
    size_t hostSize() const noexcept { return storage ? storage->size() : 0; }

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

struct PlaneRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct AudioTrackInfo {
    int stream_index = -1;
    int codec_id = 0;
    std::string codec_name;
    int sample_rate = 0;
    int channels = 0;
    uint64_t channel_layout = 0;
    int bitrate = 0;
    Rational time_base{1, 48000};
    std::vector<uint8_t> extradata;
    uint32_t codec_tag = 0;
    int profile = 0;
    int frame_size = 0;
    int initial_padding = 0;
    int trailing_padding = 0;
    int bits_per_coded_sample = 0;
    int format = 0;
    std::shared_ptr<void> opaque_codecpar;
};

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
