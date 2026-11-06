// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
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

// Cacheline isolation (64-byte alignment)
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

    // Const implicit conversion to underlying integer
    constexpr operator ValueType() const noexcept { return v; }

    static constexpr ValueType visitor(const auto& other) noexcept {
        using OtherT = std::decay_t<decltype(other)>;
        if constexpr (std::is_same_v<OtherT, TaggedHandle>) {
            return other.v;
        } else {
            return static_cast<ValueType>(other);
        }
    }

    // Consistent three-way comparison
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

const char* videoCodecName(VideoCodec codec) noexcept;
VideoCodec videoCodecFromName(const std::string& name) noexcept;

const char* pixelFormatName(PixelFormat format) noexcept;
size_t bytesPerPixel(PixelFormat format) noexcept;
int planeCount(PixelFormat format) noexcept;
bool isTenBit(PixelFormat format) noexcept;

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
    // Chromaticities in units of 0.00002 (SMPTE ST 2086 / HEVC SEI).
    uint16_t red_x = 0;
    uint16_t red_y = 0;
    uint16_t green_x = 0;
    uint16_t green_y = 0;
    uint16_t blue_x = 0;
    uint16_t blue_y = 0;
    uint16_t white_x = 0;
    uint16_t white_y = 0;
    // Luminance in units of 0.0001 cd/m^2.
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
    std::string toString() const;
};

struct FrameBuffer {
    std::vector<uint8_t> bytes;
    size_t alignment = 256;

    Result<void> allocate(size_t size, size_t align = 256);
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

    // Device-side view.  In CPU reference mode these are either zero or
    // simulated allocation addresses; in the NVIDIA path they are CUdeviceptr
    // values that were never mapped into host virtual memory.
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



    // Copy metadata only; the storage and plane pointers are left untouched.
    void copyMetadataFrom(const Frame& other);
    Frame cloneMetadata() const;
};

struct PlaneRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

size_t alignUp(size_t value, size_t alignment) noexcept;
size_t planeByteSize(PixelFormat format, int width, int height, int plane, int pitch);
size_t frameByteSize(int width, int height, PixelFormat format, int* pitches = nullptr,
                     int* chroma_width = nullptr, int* chroma_height = nullptr);

struct Packet {
    std::vector<uint8_t> data;
    int64_t pts = 0;
    int64_t dts = 0;
    int64_t duration = 0;
    bool keyframe = false;
    int stream_index = 0;
    Rational time_base{1, 1000};

    // Projection of `pts` onto the Flicks integer timeline (705,600,000 Hz).
    // `flicks_valid` is false when the container supplies no presentation
    // timestamp at all; `pts_flicks` is then meaningless.
    int64_t pts_flicks = 0;
    bool flicks_valid = false;
};

}  // namespace rtx
