// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/backend.h"

#include <cstring>
#include <sstream>

#include "rtx/color.h"
#include "rtx/vram_pool.h"

namespace rtx {

std::string DeviceInfo::toString() const {
    std::ostringstream out;
    out << name << " [";
    if (compute_major > 0) {
        out << "sm_" << compute_major << compute_minor;
    } else {
        out << "cpu";
    }
    out << ", LUID=";
    for (int i = 0; i < 8; ++i) {
        out << std::hex << static_cast<int>(luid[i]);
    }
    out << std::dec << ", VRAM=" << (vram_bytes / (1024ull * 1024ull)) << " MiB]";
    return out.str();
}

std::string BackendCapabilities::toString() const {
    std::ostringstream out;
    const auto append = [&out](bool enabled, const char* name) {
        if (enabled) {
            if (out.tellp() > 0) {
                out << ", ";
            }
            out << name;
        }
    };
    append(cpu_reference, "cpu-reference");
    append(nvdec, "nvdec");
    append(nvenc_hevc10, "nvenc-hevc10");
    append(nvenc_av1, "nvenc-av1");
    append(vsr, "vsr");
    append(truehdr, "truehdr");
    append(nvofa, "nvofa");
    append(tensorrt, "tensorrt");
    append(d3d12_interop, "d3d12-interop");
    return out.str();
}

CpuReferenceBackend::CpuReferenceBackend() = default;
CpuReferenceBackend::~CpuReferenceBackend() = default;

Result<DeviceInfo> CpuReferenceBackend::deviceInfo() const {
    DeviceInfo info;
    info.name = "CPU reference backend";
    info.compute_major = 0;
    info.compute_minor = 0;
    info.vram_bytes = 0;
    info.nvidia = false;
    return Result<DeviceInfo>::ok(info);
}

BackendCapabilities CpuReferenceBackend::capabilities() const {
    BackendCapabilities caps;
    caps.cpu_reference = true;
    return caps;
}

Status CpuReferenceBackend::initialize(const BackendConfig& config) {
    if (config.width <= 0 || config.height <= 0 || config.target_width <= 0 ||
        config.target_height <= 0) {
        return Status::fail(ErrorCode::InvalidArgument, "invalid backend geometry");
    }
    if (config.target_width < config.width || config.target_height < config.height) {
        return Status::fail(ErrorCode::Unsupported,
                            "downscaling is not supported by the reference backend");
    }
    config_ = config;
    initialized_ = true;
    return Status::success();
}

void CpuReferenceBackend::shutdown() { initialized_ = false; }

Result<Frame> CpuReferenceBackend::makeFrame(int width, int height, PixelFormat format) {
    Frame frame;
    const Status allocation = frame.allocateHost(width, height, format);
    if (!allocation.ok()) {
        return Result<Frame>::fromStatus(allocation);
    }
    return Result<Frame>::ok(std::move(frame));
}

Result<void> CpuReferenceBackend::interpolate(const InterpRequest& request) {
    if (!initialized_) {
        return Result<void>::fail(ErrorCode::State, "backend is not initialized");
    }
    if (request.previous == nullptr || request.next == nullptr || request.output == nullptr) {
        return Result<void>::fail(ErrorCode::InvalidArgument, "interpolation request is incomplete");
    }
    const Frame& previous = *request.previous;
    const Frame& next = *request.next;
    Frame& output = *request.output;
    if (previous.format != PixelFormat::NV12 || next.format != PixelFormat::NV12 ||
        output.format != PixelFormat::NV12) {
        return Result<void>::fail(ErrorCode::Unsupported,
                                  "reference interpolation only supports NV12");
    }
    if (previous.width != next.width || previous.height != next.height ||
        output.width != previous.width || output.height != previous.height) {
        return Result<void>::fail(ErrorCode::InvalidArgument,
                                  "interpolation frame dimensions do not match");
    }
    blendNv12(previous.planes[0], next.planes[0], previous.width, previous.height,
              previous.pitches[0], previous.pitches[1], request.timestep, output.planes[0],
              output.pitches[0], output.pitches[1]);
    output.pts = previous.pts;
    output.time_base = previous.time_base;
    output.color = previous.color;
    return {};
}

Result<void> CpuReferenceBackend::enhance(const EnhanceRequest& request) {
    if (!initialized_) {
        return Result<void>::fail(ErrorCode::State, "backend is not initialized");
    }
    if (request.input == nullptr || request.output == nullptr) {
        return Result<void>::fail(ErrorCode::InvalidArgument, "enhance request is incomplete");
    }
    const Frame& input = *request.input;
    Frame& output = *request.output;
    if (input.format != PixelFormat::NV12) {
        return Result<void>::fail(ErrorCode::Unsupported,
                                  "reference enhance expects an NV12 input");
    }
    if (output.valid() && output.format != PixelFormat::RGBA16F) {
        return Result<void>::fail(ErrorCode::InvalidArgument,
                                  "reference enhance output must be RGBA16F");
    }
    if (!output.valid()) {
        const Status allocation = output.allocateHost(config_.target_width, config_.target_height,
                                                      PixelFormat::RGBA16F);
        if (!allocation.ok()) {
            return Result<void>::fromStatus(allocation);
        }
    }

    Frame upscaled;
    const Status allocation = upscaled.allocateHost(output.width, output.height, PixelFormat::NV12);
    if (!allocation.ok()) {
        return Result<void>::fromStatus(allocation);
    }
    bilinearScaleNv12(input.planes[0], input.width, input.height, input.pitches[0],
                      input.pitches[1], upscaled.planes[0], upscaled.width, upscaled.height,
                      upscaled.pitches[0], upscaled.pitches[1]);

    const uint32_t target_nits = request.hdr.enabled ? request.hdr.target_nits : 1000u;
    inverseToneMapNv12ToRgba16f(upscaled.planes[0], upscaled.width, upscaled.height,
                                upscaled.pitches[0], upscaled.pitches[1],
                                reinterpret_cast<uint16_t*>(output.planes[0]), target_nits);
    output.hdr = request.hdr;
    output.color = ColorDescription{ColorPrimaries::BT2020, TransferFunction::SMPTE2084,
                                    MatrixCoefficients::BT2020NC, ColorRange::Limited};
    output.pts = input.pts;
    output.time_base = input.time_base;
    output.duration = input.duration;
    return {};
}

Result<void> CpuReferenceBackend::packP010(const Frame& rgba16f, Frame& p010) {
    if (rgba16f.format != PixelFormat::RGBA16F) {
        return Result<void>::fail(ErrorCode::InvalidArgument,
                                  "P010 packing requires an RGBA16F source");
    }
    if (!p010.valid()) {
        const Status allocation = p010.allocateHost(rgba16f.width, rgba16f.height, PixelFormat::P010);
        if (!allocation.ok()) {
            return Result<void>::fromStatus(allocation);
        }
    }
    if (p010.format != PixelFormat::P010 || p010.width != rgba16f.width ||
        p010.height != rgba16f.height) {
        return Result<void>::fail(ErrorCode::InvalidArgument, "P010 destination does not match");
    }
    rgba16fToP010(reinterpret_cast<const uint16_t*>(rgba16f.planes[0]), rgba16f.width,
                  rgba16f.height, reinterpret_cast<uint16_t*>(p010.planes[0]),
                  reinterpret_cast<uint16_t*>(p010.planes[1]), p010.pitches[0] / 2,
                  p010.pitches[1] / 2, true);
    p010.pts = rgba16f.pts;
    p010.time_base = rgba16f.time_base;
    p010.hdr = rgba16f.hdr;
    p010.color = rgba16f.color;
    return {};
}

Result<void> CpuReferenceBackend::encode(const Frame& p010, Packet& packet) {
    if (p010.format != PixelFormat::P010) {
        return Result<void>::fail(ErrorCode::InvalidArgument,
                                  "reference encoder only accepts P010");
    }
    // Reference elementary stream: a fixed 16-byte header followed by tightly
    // packed P010.  This is deliberately not a real codec; it exists so that
    // the muxer and audit paths can be exercised without NVENC.
    const size_t chroma_height = static_cast<size_t>((p010.height + 1) / 2);
    const size_t luma_bytes = static_cast<size_t>(p010.width) * p010.height * 2;
    const size_t chroma_bytes = static_cast<size_t>(p010.width) * chroma_height * 2;
    packet.data.resize(16 + luma_bytes + chroma_bytes);
    std::memcpy(packet.data.data(), "RTXREF10", 8);
    packet.data[8] = static_cast<uint8_t>(p010.width & 0xFF);
    packet.data[9] = static_cast<uint8_t>((p010.width >> 8) & 0xFF);
    packet.data[10] = static_cast<uint8_t>(p010.height & 0xFF);
    packet.data[11] = static_cast<uint8_t>((p010.height >> 8) & 0xFF);
    packet.data[12] = 10;
    packet.data[13] = 0;
    packet.data[14] = 0;
    packet.data[15] = 0;
    uint8_t* destination = packet.data.data() + 16;
    for (int y = 0; y < p010.height; ++y) {
        std::memcpy(destination, p010.planes[0] + static_cast<size_t>(y) * p010.pitches[0],
                    static_cast<size_t>(p010.width) * 2);
        destination += static_cast<size_t>(p010.width) * 2;
    }
    for (size_t y = 0; y < chroma_height; ++y) {
        std::memcpy(destination, p010.planes[1] + y * p010.pitches[1],
                    static_cast<size_t>(p010.width) * 2);
        destination += static_cast<size_t>(p010.width) * 2;
    }
    packet.pts = p010.pts;
    packet.dts = p010.pts;
    packet.duration = p010.duration;
    packet.keyframe = true;
    return {};
}

std::unique_ptr<IEngineBackend> makeCpuBackend() {
    return std::make_unique<CpuReferenceBackend>();
}

}  // namespace rtx
