// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <cstdint>
#include <cstring>
#include <string>

#include <utility>

#include "rtx/raii_owners.h"
#include "rtx/geometry.h"
#include "rtx/json_scanner.h"
#include "rtx/status.h"
#include "rtx/types.h"

using namespace rtx;

RTX_TEST(types_format_metadata) {
    RTX_CHECK_EQ(std::string(pixelFormatName(PixelFormat::NV12)), std::string("NV12"));
    RTX_CHECK_EQ(std::string(pixelFormatName(PixelFormat::P010)), std::string("P010"));
    RTX_CHECK_EQ(std::string(pixelFormatName(PixelFormat::RGBA16F)), std::string("RGBA16F"));
    RTX_CHECK_EQ(std::string(pixelFormatName(PixelFormat::Unknown)), std::string("UNKNOWN"));

    RTX_CHECK_EQ(bytesPerPixel(PixelFormat::NV12), static_cast<size_t>(1));
    RTX_CHECK_EQ(bytesPerPixel(PixelFormat::P010), static_cast<size_t>(2));
    RTX_CHECK_EQ(bytesPerPixel(PixelFormat::RGBA16F), static_cast<size_t>(8));
    RTX_CHECK_EQ(bytesPerPixel(PixelFormat::Unknown), static_cast<size_t>(0));
    RTX_CHECK_EQ(planeCount(PixelFormat::NV12), 2);
    RTX_CHECK_EQ(planeCount(PixelFormat::YUV420P10), 3);
    RTX_CHECK_EQ(planeCount(PixelFormat::RGB32), 1);
    RTX_CHECK(!isTenBit(PixelFormat::NV12));
    RTX_CHECK(isTenBit(PixelFormat::P010));
    RTX_CHECK(isTenBit(PixelFormat::YUV420P10));
    RTX_CHECK(isTenBit(PixelFormat::R10G10B10A2));
}
RTX_TEST(types_alignment_and_sizes) {
    RTX_CHECK_EQ(alignUp(0, 256), static_cast<size_t>(0));
    RTX_CHECK_EQ(alignUp(1, 256), static_cast<size_t>(256));
    RTX_CHECK_EQ(alignUp(256, 256), static_cast<size_t>(256));
    RTX_CHECK_EQ(alignUp(257, 256), static_cast<size_t>(512));
    RTX_CHECK_EQ(alignUp(257, 0), static_cast<size_t>(257));

    int pitches[3] = {0, 0, 0};
    int chroma_width = 0;
    int chroma_height = 0;
    const size_t nv12 = frameByteSize(1920, 1080, PixelFormat::NV12, pitches, &chroma_width,
                                      &chroma_height);
    RTX_CHECK_EQ(pitches[0], 2048);
    RTX_CHECK_EQ(pitches[1], 2048);
    RTX_CHECK_EQ(chroma_width, 960);
    RTX_CHECK_EQ(chroma_height, 540);
    RTX_CHECK_EQ(nv12, static_cast<size_t>(2048 * 1080 + 2048 * 540));

    const size_t p010 =
        frameByteSize(3840, 2160, PixelFormat::P010, pitches, &chroma_width, &chroma_height);
    RTX_CHECK_EQ(pitches[0], 7680);
    RTX_CHECK_EQ(chroma_width, 1920);
    RTX_CHECK_EQ(chroma_height, 1080);
    RTX_CHECK_EQ(p010, static_cast<size_t>(7680 * 2160 + 7680 * 1080));
    RTX_CHECK_EQ(p010, static_cast<size_t>(3 * 3840 * 2160));

    RTX_CHECK_EQ(frameByteSize(-1, 10, PixelFormat::NV12), static_cast<size_t>(0));
    RTX_CHECK_EQ(frameByteSize(10, 10, PixelFormat::Unknown), static_cast<size_t>(0));
    RTX_CHECK_EQ(planeByteSize(PixelFormat::NV12, 10, 10, 0, 256), static_cast<size_t>(2560));
    RTX_CHECK_EQ(planeByteSize(PixelFormat::NV12, 10, 10, 1, 256), static_cast<size_t>(1280));
    RTX_CHECK_EQ(planeByteSize(PixelFormat::NV12, 10, 10, 2, 256), static_cast<size_t>(0));
}
RTX_TEST(types_frame_buffer_alignment) {
    FrameBuffer buffer{{}, 256u, 0u, 0u};
    RTX_CHECK_EQ(frameBufferSize(buffer), static_cast<size_t>(0));
    RTX_CHECK(!allocateFrameBuffer(buffer, 0).has_value());
    RTX_CHECK(allocateFrameBuffer(buffer, 100, 128).has_value());
    RTX_CHECK_EQ(frameBufferSize(buffer), static_cast<size_t>(100));
    RTX_CHECK_EQ(reinterpret_cast<uintptr_t>(frameBufferData(buffer)) % 128, static_cast<uintptr_t>(0));
    const auto& [raw_bytes, align_v, sz_v, off_v] = buffer;
    (void)align_v; (void)sz_v; (void)off_v;
    RTX_CHECK(raw_bytes.size() >= 100 + 127);
    frameBufferData(buffer)[0] = 0xAB;
    frameBufferData(buffer)[99] = 0xCD;
    RTX_CHECK_EQ(frameBufferData(buffer)[0], static_cast<uint8_t>(0xAB));
    RTX_CHECK_EQ(frameBufferData(buffer)[99], static_cast<uint8_t>(0xCD));

    RTX_CHECK(allocateFrameBuffer(buffer, 17, 64).has_value());
    RTX_CHECK_EQ(frameBufferSize(buffer), static_cast<size_t>(17));
    RTX_CHECK_EQ(reinterpret_cast<uintptr_t>(frameBufferData(buffer)) % 64, static_cast<uintptr_t>(0));
}
RTX_TEST(types_metadata_copy_and_clone) {
    Frame source;
    source.width = 32;
    source.height = 32;
    source.format = PixelFormat::NV12;
    source.pts = 1234;
    source.duration = 2;
    source.time_base = Rational{1, 24};
    source.keyframe = true;
    source.fence_value = 99;
    source.color = ColorDescription{ColorPrimaries::BT2020, TransferFunction::SMPTE2084,
                                    MatrixCoefficients::BT2020NC, ColorRange::Limited};
    source.hdr.enabled = true;
    source.hdr.target_nits = 1000;
    source.device_planes[0] = 0x12345678;

    const Frame clone = source.cloneMetadata();
    RTX_CHECK_EQ(clone.width, 32);
    RTX_CHECK_EQ(clone.height, 32);
    RTX_CHECK_EQ(clone.pts, 1234);
    RTX_CHECK_EQ(clone.time_base, source.time_base);
    RTX_CHECK_EQ(clone.fence_value, static_cast<uint64_t>(99));
    RTX_CHECK(clone.keyframe);
    RTX_CHECK(clone.hdr.enabled);
    RTX_CHECK_EQ(clone.device_planes[0], static_cast<DevicePtr>(0x12345678));
    RTX_CHECK(clone.storage == nullptr);
    RTX_CHECK(clone.planes[0] == nullptr);

    Frame destination;
    uint8_t dummy_plane[16] = {};
    destination.width = 16;
    destination.height = 16;
    destination.planes[0] = dummy_plane;
    uint8_t* original_plane = destination.planes[0];
    destination.copyMetadataFrom(clone);
    RTX_CHECK_EQ(destination.width, 32);
    RTX_CHECK(destination.planes[0] == original_plane);  // storage is untouched
    RTX_CHECK_EQ(destination.device_planes[0], static_cast<DevicePtr>(0x12345678));
}
RTX_TEST(types_device_residency) {
    Frame frame;
    RTX_CHECK(!frame.isDeviceResident());
    frame.device_planes[0] = 0x1000;
    RTX_CHECK(frame.isDeviceResident());
    frame.device_planes[0] = kInvalidDevicePtr;
    RTX_CHECK(!frame.isDeviceResident());
}
RTX_TEST(status_success_and_failure) {
    const Result<void> success;
    RTX_CHECK(success.has_value());
    RTX_CHECK(static_cast<bool>(success));

// Status and Result<T> contract (merged from test_status.cpp)
    const Result<void> failure = err(ErrorCode::Io, "disk on fire", "sector 7");
    RTX_CHECK(!failure.has_value());
    RTX_CHECK(!static_cast<bool>(failure));
    RTX_CHECK_EQ(failure.error().code, ErrorCode::Io);
    RTX_CHECK_EQ(failure.error().message, std::string("disk on fire"));
    RTX_CHECK_EQ(failure.error().detail, std::string("sector 7"));
    const std::string text = failure.error().toString();
    RTX_CHECK(text.find("IO_ERROR") != std::string::npos);
    RTX_CHECK(text.find("disk on fire") != std::string::npos);
    RTX_CHECK(text.find("sector 7") != std::string::npos);
}
RTX_TEST(status_error_names_are_stable) {
    RTX_CHECK_EQ(std::string(errorCodeName(ErrorCode::Ok)), std::string("OK"));
    RTX_CHECK_EQ(std::string(errorCodeName(ErrorCode::InvalidArgument)),
                 std::string("INVALID_ARGUMENT"));
    RTX_CHECK_EQ(std::string(errorCodeName(ErrorCode::DllMissing)), std::string("DLL_MISSING"));
    RTX_CHECK_EQ(std::string(errorCodeName(ErrorCode::Reserved11)),
                 std::string("RESERVED_11"));
    RTX_CHECK_EQ(std::string(errorCodeName(ErrorCode::Internal)), std::string("INTERNAL_ERROR"));
}
RTX_TEST(status_result_value) {
    const Result<int> good = 41;
    RTX_CHECK(good.has_value());
    RTX_CHECK_EQ(good.value(), 41);

    const Result<int> bad = err(ErrorCode::Timeout, "too slow", "2 s");
    RTX_CHECK(!bad.has_value());
    RTX_CHECK_EQ(bad.error().code, ErrorCode::Timeout);
    RTX_CHECK_EQ(bad.error().message, std::string("too slow"));
    RTX_CHECK_EQ(bad.value_or(7), 7);
}
RTX_TEST(status_result_void) {
    Result<void> success;
    RTX_CHECK(success.has_value());

    Result<void> failure = err(ErrorCode::State, "not ready");
    RTX_CHECK(!failure.has_value());
    RTX_CHECK_EQ(failure.error().code, ErrorCode::State);

    failure = err(ErrorCode::OutOfMemory, "no vram");
    RTX_CHECK_EQ(failure.error().code, ErrorCode::OutOfMemory);
}
RTX_TEST(status_error_copy_and_move) {
    Error assigned{ErrorCode::Internal, "stale message", "stale detail"};
    const Error source{ErrorCode::Io, "assigned message", "assigned detail"};
    assigned = source;
    RTX_CHECK_EQ(assigned.message, std::string("assigned message"));
    RTX_CHECK_EQ(assigned.detail, std::string("assigned detail"));

    Error moved = std::move(assigned);
    RTX_CHECK_EQ(moved.message, std::string("assigned message"));
    RTX_CHECK_EQ(moved.detail, std::string("assigned detail"));

    Error move_assigned{ErrorCode::Internal, "old message", "old detail"};
    move_assigned = std::move(moved);
    RTX_CHECK_EQ(move_assigned.message, std::string("assigned message"));
    RTX_CHECK_EQ(move_assigned.detail, std::string("assigned detail"));
}
RTX_TEST(smart_geometry_portrait_and_pillarbox) {
    const auto [a_cw, a_ch, a_sw, a_sh, a_ox, a_oy, a_pl, a_pr, a_ip] =
        calculateSmartGeometry(720, 1280, GeometryTargetMode::AutoDirect);
    (void)a_ox; (void)a_oy; (void)a_pl; (void)a_pr;
    RTX_CHECK(a_ip);
    RTX_CHECK_EQ(a_cw, 2160);
    RTX_CHECK_EQ(a_ch, 3840);
    RTX_CHECK_EQ(a_sw, 2160);
    RTX_CHECK_EQ(a_sh, 3840);

    const auto [b_cw, b_ch, b_sw, b_sh, b_ox, b_oy, b_pl, b_pr, b_ip] =
        calculateSmartGeometry(720, 1280, GeometryTargetMode::Pillarbox);
    (void)b_ox; (void)b_oy;
    RTX_CHECK(b_ip);
    RTX_CHECK_EQ(b_cw, 3840);
    RTX_CHECK_EQ(b_ch, 2160);
    RTX_CHECK_EQ(b_sh, 2160);
    RTX_CHECK_EQ(b_sw, 1214);
    RTX_CHECK(b_pl > 1300);
    RTX_CHECK(b_pr > 1300);
    RTX_CHECK_EQ(b_pl + b_sw + b_pr, 3840);

    const auto [l_cw, l_ch, l_sw, l_sh, l_ox, l_oy, l_pl, l_pr, l_ip] =
        calculateSmartGeometry(1920, 1080, GeometryTargetMode::AutoDirect);
    (void)l_sw; (void)l_sh; (void)l_ox; (void)l_oy; (void)l_pl; (void)l_pr;
    RTX_CHECK(!l_ip);
    RTX_CHECK_EQ(l_cw, 3840);
    RTX_CHECK_EQ(l_ch, 2160);

    const auto [d_cw, d_ch, d_sw, d_sh, d_ox, d_oy, d_pl, d_pr, d_ip] =
        calculateSmartGeometry(720, 1280, GeometryTargetMode::AutoDirect, 3840, 2160);
    (void)d_sw; (void)d_sh; (void)d_ox; (void)d_oy; (void)d_pl; (void)d_pr;
    RTX_CHECK(d_ip);
    RTX_CHECK_EQ(d_cw, 2160);
    RTX_CHECK_EQ(d_ch, 3840);
}
#if defined(_WIN32)
struct MockUnknown : IUnknown {
    int ref_count = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(++ref_count); }
    ULONG STDMETHODCALLTYPE Release() override {
        int r = --ref_count;
        if (r == 0) delete this;
        return static_cast<ULONG>(r);
    }
};
#endif

RTX_TEST(com_ptr_move_and_lifetime) {
#if defined(_WIN32)
    MockUnknown* mock = new MockUnknown();
    RTX_CHECK_EQ(mock->ref_count, 1);
    {
        ComPtr<MockUnknown> owner(mock);
        RTX_CHECK(static_cast<bool>(owner));
        RTX_CHECK_EQ(owner.get(), mock);
        RTX_CHECK_EQ(owner->ref_count, 1);

        ComPtr<MockUnknown> moved(std::move(owner));
        RTX_CHECK(!owner);
        RTX_CHECK(static_cast<bool>(moved));
        RTX_CHECK_EQ(moved.get(), mock);

        owner = std::move(moved);
        RTX_CHECK(static_cast<bool>(owner));
        RTX_CHECK(!moved);

        MockUnknown* released = owner.release();
        RTX_CHECK(!owner);
        RTX_CHECK_EQ(released, mock);
        RTX_CHECK_EQ(mock->ref_count, 1);
        owner.reset(released);
    }
#endif
}
RTX_TEST(json_scanner_contract) {
    const std::string text = R"({
        "name": "CineRTX",
        "version": 3,
        "pi": 3.14159,
        "enabled": true,
        "tags": ["cuda", "transcode", "hdr"],
        "nested": {"fps": 60}
    })";

    std::size_t pos = 0;
    auto obj = json::scanObject(text, pos);
    RTX_REQUIRE(obj.has_value());
    RTX_CHECK_EQ(obj->size(), 6u);

    auto name = json::memberString(*obj, "name");
    RTX_REQUIRE(name.has_value());
    RTX_CHECK_EQ(*name, "CineRTX");

    auto ver = json::memberInt(*obj, "version");
    RTX_REQUIRE(ver.has_value());
    RTX_CHECK_EQ(*ver, 3);

    auto pi = json::memberDouble(*obj, "pi");
    RTX_REQUIRE(pi.has_value());
    RTX_CHECK(std::abs(*pi - 3.14159) < 1e-4);

    auto en = json::memberBool(*obj, "enabled");
    RTX_REQUIRE(en.has_value());
    RTX_CHECK_EQ(*en, true);

    auto tags = json::memberArray(*obj, "tags");
    RTX_REQUIRE(tags.has_value());
    RTX_CHECK_EQ(tags->size(), 3u);

    auto nested = json::memberObject(*obj, "nested");
    RTX_REQUIRE(nested.has_value());
    auto nested_fps = json::memberInt(*nested, "fps");
    RTX_REQUIRE(nested_fps.has_value());
    RTX_CHECK_EQ(*nested_fps, 60);

    const std::string escaped = json::escapeJson("hello\n\"world\"");
    RTX_CHECK_EQ(escaped, "hello\\n\\\"world\\\"");
    RTX_CHECK_EQ(json::jsonQuoted("a"), "\"a\"");
}
