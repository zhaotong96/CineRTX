// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rtx/rational.h"

namespace rtx {

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

struct FrameBuffer {
    std::vector<uint8_t> bytes;
    size_t alignment = 256;

    Result<void> allocate(size_t size, size_t align = 256);
    uint8_t* data() { return bytes.data() + offset_; }
    const uint8_t* data() const { return bytes.data() + offset_; }
    size_t size() const noexcept { return size_; }

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

    // Allocates a tightly-packed host frame with 256-byte aligned planes and
    // pitches.  NV12/P010 are allocated as semi-planar Y + interleaved UV.
    Result<void> allocateHost(int frame_width, int frame_height, PixelFormat frame_format,
                              size_t alignment = 256);
    Result<void> allocateHostLike(const Frame& other);

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
};

}  // namespace rtx
