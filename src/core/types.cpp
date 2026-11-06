// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/types.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

namespace rtx {

std::string DeviceInfo::toString() const {
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



const char* pixelFormatName(PixelFormat format) noexcept {
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

size_t bytesPerPixel(PixelFormat format) noexcept {
    switch (format) {
        case PixelFormat::NV12:
            return 1;  // luma sample, chroma is subsampled
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

int planeCount(PixelFormat format) noexcept {
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

bool isTenBit(PixelFormat format) noexcept {
    return format == PixelFormat::P010 || format == PixelFormat::YUV420P10 ||
           format == PixelFormat::R10G10B10A2;
}

size_t alignUp(size_t value, size_t alignment) noexcept {
    if (alignment == 0) {
        return value;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

Result<void> FrameBuffer::allocate(size_t size, size_t align) {
    if (size == 0) {
        return err(ErrorCode::InvalidArgument, "zero-sized frame buffer");
    }
    alignment = std::max<size_t>(align, 64);
    // Over-allocate so that data() can honour the alignment without relying on
    // the allocator's guarantees. The logical size excludes the padding.
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

size_t planeByteSize(PixelFormat format, int width, int height, int plane, int pitch) {
    if (width <= 0 || height <= 0 || plane < 0) {
        return 0;
    }
    const int chroma_height = (height + 1) / 2;
    switch (format) {
        case PixelFormat::NV12:
            if (plane == 0) {
                return static_cast<size_t>(pitch) * static_cast<size_t>(height);
            }
            if (plane == 1) {
                return static_cast<size_t>(pitch) * static_cast<size_t>(chroma_height);
            }
            break;
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

size_t frameByteSize(int width, int height, PixelFormat format, int* pitches, int* chroma_width,
                     int* chroma_height) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    int local_pitches[3] = {0, 0, 0};
    int local_chroma_width = (width + 1) / 2;
    int local_chroma_height = (height + 1) / 2;

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

void Frame::copyMetadataFrom(const Frame& other) {
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

Frame Frame::cloneMetadata() const {
    Frame frame;
    frame.copyMetadataFrom(*this);
    return frame;
}

const char* videoCodecName(VideoCodec codec) noexcept {
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

VideoCodec videoCodecFromName(const std::string& name) noexcept {
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

}  // namespace rtx
