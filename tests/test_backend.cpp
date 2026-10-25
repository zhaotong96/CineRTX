// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#include "rtx/backend.h"
#include "rtx/color.h"
#include "rtx/hdr.h"
#include "rtx/types.h"
#include "test_util.h"

using namespace rtx;

namespace {

BackendConfig backendConfig() {
    BackendConfig config;
    config.width = 8;
    config.height = 8;
    config.target_width = 16;
    config.target_height = 16;
    config.enable_truehdr = true;
    return config;
}

}  // namespace

RTX_TEST(backend_device_info_and_capabilities) {
    DeviceInfo info;
    RTX_CHECK(!info.turingOrNewer());
    info.compute_major = 7;
    info.compute_minor = 0;
    RTX_CHECK(!info.turingOrNewer());
    info.compute_minor = 5;
    RTX_CHECK(info.turingOrNewer());
    info.compute_major = 8;
    info.compute_minor = 9;
    RTX_CHECK(info.turingOrNewer());
    info.name = "NVIDIA GeForce RTX 4090";
    info.vram_bytes = 24ull * 1024 * 1024 * 1024;
    const std::string text = info.toString();
    RTX_CHECK(text.find("NVIDIA GeForce RTX 4090") != std::string::npos);
    RTX_CHECK(text.find("sm_89") != std::string::npos);
    RTX_CHECK(text.find("24576 MiB") != std::string::npos);

    DeviceInfo cpu;
    cpu.name = "CPU";
    RTX_CHECK(cpu.toString().find("cpu") != std::string::npos);

    BackendCapabilities caps;
    RTX_CHECK(caps.toString().empty());
    caps.cpu_reference = true;
    RTX_CHECK_EQ(caps.toString(), std::string("cpu-reference"));
    caps.vsr = true;
    caps.truehdr = true;
    caps.tensorrt = true;
    caps.d3d12_interop = true;
    const std::string list = caps.toString();
    RTX_CHECK(list.find("cpu-reference, vsr") != std::string::npos);
    RTX_CHECK(list.find("truehdr") != std::string::npos);
    RTX_CHECK(list.find("tensorrt") != std::string::npos);
    RTX_CHECK(list.find("d3d12-interop") != std::string::npos);
}

RTX_TEST(backend_cpu_reference_lifecycle) {
    auto backend = makeCpuBackend();
    RTX_REQUIRE(backend != nullptr);
    RTX_CHECK_EQ(std::string(backend->name()), std::string("cpu-reference"));
    const Result<DeviceInfo> device = backend->deviceInfo();
    RTX_REQUIRE(device.ok());
    RTX_CHECK(!device.value().nvidia);
    RTX_CHECK_EQ(device.value().vram_bytes, static_cast<uint64_t>(0));
    const BackendCapabilities caps = backend->capabilities();
    RTX_CHECK(caps.cpu_reference);
    RTX_CHECK(!caps.vsr);
    RTX_CHECK(!caps.truehdr);

    RTX_CHECK_EQ(backend->initialize(BackendConfig{0, 0, 0, 0}).code(),
                 ErrorCode::InvalidArgument);
    BackendConfig downscale = backendConfig();
    downscale.target_width = 4;
    RTX_CHECK_EQ(backend->initialize(downscale).code(), ErrorCode::Unsupported);
    RTX_REQUIRE(backend->initialize(backendConfig()).ok());

    const Result<Frame> frame = backend->makeFrame(8, 8, PixelFormat::NV12);
    RTX_REQUIRE(frame.ok());
    RTX_CHECK(frame.value().valid());
    RTX_CHECK_EQ(frame.value().bit_depth, 8);
    RTX_CHECK(!backend->makeFrame(0, 8, PixelFormat::NV12).ok());
    RTX_CHECK(!backend->makeFrame(8, 8, PixelFormat::Unknown).ok());

    backend->shutdown();
    Frame a;
    Frame b;
    Frame out;
    RTX_REQUIRE(a.allocateHost(8, 8, PixelFormat::NV12).ok());
    RTX_REQUIRE(b.allocateHost(8, 8, PixelFormat::NV12).ok());
    RTX_REQUIRE(out.allocateHost(8, 8, PixelFormat::NV12).ok());
    RTX_CHECK_EQ(backend->interpolate(InterpRequest{&a, &b, 0.5f, &out}).code(),
                 ErrorCode::State);
    RTX_REQUIRE(backend->initialize(backendConfig()).ok());
}

RTX_TEST(backend_cpu_interpolate) {
    auto backend = makeCpuBackend();
    RTX_REQUIRE(backend->initialize(backendConfig()).ok());
    Frame a;
    Frame b;
    Frame out;
    RTX_REQUIRE(a.allocateHost(8, 8, PixelFormat::NV12).ok());
    RTX_REQUIRE(b.allocateHost(8, 8, PixelFormat::NV12).ok());
    RTX_REQUIRE(out.allocateHost(8, 8, PixelFormat::NV12).ok());
    test::fillNV12(a, 0, 100, 100);
    test::fillNV12(b, 200, 120, 140);
    a.pts = 42;
    a.time_base = kTimeBase90k;
    a.color = ColorDescription{ColorPrimaries::BT709, TransferFunction::BT709,
                               MatrixCoefficients::BT709, ColorRange::Limited};

    RTX_REQUIRE(backend->interpolate(InterpRequest{&a, &b, 0.5f, &out}).ok());
    RTX_CHECK_EQ(out.planes[0][0], static_cast<uint8_t>(100));
    RTX_CHECK_EQ(out.planes[1][0], static_cast<uint8_t>(110));
    RTX_CHECK_EQ(out.planes[1][1], static_cast<uint8_t>(120));
    RTX_CHECK_EQ(out.pts, static_cast<int64_t>(42));
    RTX_CHECK((out.time_base == kTimeBase90k));
    RTX_CHECK(out.color.primaries == ColorPrimaries::BT709);

    Frame p010;
    RTX_REQUIRE(p010.allocateHost(8, 8, PixelFormat::P010).ok());
    RTX_CHECK_EQ(backend->interpolate(InterpRequest{&p010, &b, 0.5f, &out}).code(),
                 ErrorCode::Unsupported);
    Frame small;
    RTX_REQUIRE(small.allocateHost(4, 4, PixelFormat::NV12).ok());
    RTX_CHECK_EQ(backend->interpolate(InterpRequest{&a, &small, 0.5f, &out}).code(),
                 ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(backend->interpolate(InterpRequest{nullptr, &b, 0.5f, &out}).code(),
                 ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(backend->interpolate(InterpRequest{&a, &b, 0.5f, nullptr}).code(),
                 ErrorCode::InvalidArgument);
}

RTX_TEST(backend_cpu_enhance_pack_encode) {
    auto backend = makeCpuBackend();
    BackendConfig config = backendConfig();
    RTX_REQUIRE(backend->initialize(config).ok());
    Frame input;
    RTX_REQUIRE(input.allocateHost(8, 8, PixelFormat::NV12).ok());
    test::fillNV12(input, 235, 128, 128);
    input.pts = 7;
    input.time_base = kTimeBase90k;
    input.duration = 1;

    TrueHdrParameters parameters;
    const Result<Hdr10Metadata> metadata = SeiSynthesizer::makeMetadata(parameters);
    RTX_REQUIRE(metadata.ok());

    Frame enhanced;
    RTX_REQUIRE(backend->enhance(EnhanceRequest{&input, &enhanced, metadata.value()}).ok());
    RTX_CHECK_EQ(enhanced.width, 16);
    RTX_CHECK_EQ(enhanced.height, 16);
    RTX_CHECK(enhanced.format == PixelFormat::RGBA16F);
    RTX_CHECK_EQ(enhanced.pts, static_cast<int64_t>(7));
    RTX_CHECK(enhanced.hdr.enabled);
    RTX_CHECK(enhanced.color.transfer == TransferFunction::SMPTE2084);
    const auto* rgba = reinterpret_cast<const uint16_t*>(enhanced.planes[0]);
    RTX_CHECK(halfToFloat(rgba[3]) > 0.99f);
    RTX_CHECK(halfToFloat(rgba[0]) > 0.3f);

    Frame p010;
    RTX_REQUIRE(backend->packP010(enhanced, p010).ok());
    RTX_CHECK(p010.format == PixelFormat::P010);
    RTX_CHECK_EQ(p010.width, 16);
    RTX_CHECK_EQ(p010.height, 16);
    RTX_CHECK_EQ(p010.pts, static_cast<int64_t>(7));
    RTX_CHECK(p010.hdr.enabled);
    const auto* y = reinterpret_cast<const uint16_t*>(p010.planes[0]);
    RTX_CHECK((y[0] >> 6) > 800);

    Packet packet;
    RTX_REQUIRE(backend->encode(p010, packet).ok());
    RTX_REQUIRE(packet.data.size() > 16);
    RTX_CHECK(std::memcmp(packet.data.data(), "RTXREF10", 8) == 0);
    RTX_CHECK_EQ(packet.data[12], static_cast<uint8_t>(10));
    RTX_CHECK_EQ(packet.pts, static_cast<int64_t>(7));
    RTX_CHECK_EQ(packet.dts, static_cast<int64_t>(7));
    RTX_CHECK(packet.keyframe);
    RTX_CHECK_EQ(packet.data.size(),
                 static_cast<size_t>(16 + 16 * 16 * 2 + 16 * 8 * 2));

    Frame nv12_out;
    RTX_REQUIRE(nv12_out.allocateHost(16, 16, PixelFormat::NV12).ok());
    RTX_CHECK_EQ(backend->encode(nv12_out, packet).code(), ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(backend->packP010(input, p010).code(), ErrorCode::InvalidArgument);
    Frame wrong_size;
    RTX_REQUIRE(wrong_size.allocateHost(8, 8, PixelFormat::P010).ok());
    RTX_CHECK_EQ(backend->packP010(enhanced, wrong_size).code(), ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(backend->enhance(EnhanceRequest{nullptr, &enhanced, metadata.value()}).code(),
                 ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(backend->enhance(EnhanceRequest{&input, nullptr, metadata.value()}).code(),
                 ErrorCode::InvalidArgument);

    Frame wrong_output;
    RTX_REQUIRE(wrong_output.allocateHost(16, 16, PixelFormat::P010).ok());
    RTX_CHECK_EQ(backend->enhance(EnhanceRequest{&input, &wrong_output, metadata.value()}).code(),
                 ErrorCode::InvalidArgument);
}
