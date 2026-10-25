// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <cstdint>
#include <cstring>
#include <string>

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
    FrameBuffer buffer;
    RTX_CHECK_EQ(buffer.size(), static_cast<size_t>(0));
    RTX_CHECK(!buffer.allocate(0).ok());
    RTX_CHECK(buffer.allocate(100, 128).ok());
    RTX_CHECK_EQ(buffer.size(), static_cast<size_t>(100));
    RTX_CHECK_EQ(reinterpret_cast<uintptr_t>(buffer.data()) % 128, static_cast<uintptr_t>(0));
    RTX_CHECK(buffer.bytes.size() >= 100 + 127);
    buffer.data()[0] = 0xAB;
    buffer.data()[99] = 0xCD;
    RTX_CHECK_EQ(buffer.data()[0], static_cast<uint8_t>(0xAB));
    RTX_CHECK_EQ(buffer.data()[99], static_cast<uint8_t>(0xCD));

    // A second allocation must reset the logical size, even when the vector
    // reuses heap storage.
    RTX_CHECK(buffer.allocate(17, 64).ok());
    RTX_CHECK_EQ(buffer.size(), static_cast<size_t>(17));
    RTX_CHECK_EQ(reinterpret_cast<uintptr_t>(buffer.data()) % 64, static_cast<uintptr_t>(0));
}

RTX_TEST(types_allocate_nv12) {
    Frame frame;
    RTX_REQUIRE(frame.allocateHost(100, 50, PixelFormat::NV12).ok());
    RTX_CHECK(frame.valid());
    RTX_CHECK_EQ(frame.width, 100);
    RTX_CHECK_EQ(frame.height, 50);
    RTX_CHECK_EQ(frame.bit_depth, 8);
    RTX_CHECK_EQ(frame.chroma_width, 50);
    RTX_CHECK_EQ(frame.chroma_height, 25);
    RTX_CHECK_EQ(frame.pitches[0], 256);
    RTX_CHECK_EQ(frame.pitches[1], 256);
    RTX_CHECK_EQ(frame.plane_sizes[0], static_cast<size_t>(256 * 50));
    RTX_CHECK_EQ(frame.plane_sizes[1], static_cast<size_t>(256 * 25));
    RTX_CHECK_EQ(frame.plane_sizes[2], static_cast<size_t>(0));
    RTX_CHECK(frame.planes[0] != nullptr);
    RTX_CHECK(frame.planes[1] == frame.planes[0] + frame.plane_sizes[0]);
    RTX_CHECK(frame.planes[2] == nullptr);
    RTX_CHECK_EQ(reinterpret_cast<uintptr_t>(frame.planes[0]) % 256, static_cast<uintptr_t>(0));
    RTX_CHECK_EQ(frame.hostSize(), frame.plane_sizes[0] + frame.plane_sizes[1]);
    RTX_CHECK(!frame.isDeviceResident());
}

RTX_TEST(types_allocate_planar_and_rgba) {
    Frame p010;
    RTX_REQUIRE(p010.allocateHost(64, 32, PixelFormat::P010).ok());
    RTX_CHECK_EQ(p010.bit_depth, 10);
    RTX_CHECK_EQ(p010.pitches[0], 256);
    RTX_CHECK_EQ(p010.plane_sizes[0], static_cast<size_t>(256 * 32));
    RTX_CHECK_EQ(p010.plane_sizes[1], static_cast<size_t>(256 * 16));
    RTX_CHECK_EQ(p010.chroma_width, 32);
    RTX_CHECK_EQ(p010.chroma_height, 16);

    Frame planar;
    RTX_REQUIRE(planar.allocateHost(64, 32, PixelFormat::YUV420P10).ok());
    RTX_CHECK_EQ(planar.bit_depth, 10);
    RTX_CHECK(planar.planes[1] != nullptr);
    RTX_CHECK(planar.planes[2] == planar.planes[1] + planar.plane_sizes[1]);
    RTX_CHECK_EQ(planar.plane_sizes[2], static_cast<size_t>(256 * 16));

    Frame rgba;
    RTX_REQUIRE(rgba.allocateHost(16, 16, PixelFormat::RGBA16F).ok());
    RTX_CHECK_EQ(rgba.bit_depth, 8);
    RTX_CHECK_EQ(rgba.plane_sizes[0], static_cast<size_t>(256 * 16));
    RTX_CHECK(rgba.planes[1] == nullptr);

    Frame invalid;
    RTX_CHECK(!invalid.allocateHost(0, 10, PixelFormat::NV12).ok());
    RTX_CHECK(!invalid.allocateHost(10, 10, PixelFormat::Unknown).ok());
    RTX_CHECK(!invalid.valid());
}

RTX_TEST(types_metadata_copy_and_clone) {
    Frame source;
    RTX_REQUIRE(source.allocateHost(32, 32, PixelFormat::NV12).ok());
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
    RTX_REQUIRE(destination.allocateHost(16, 16, PixelFormat::NV12).ok());
    uint8_t* original_plane = destination.planes[0];
    destination.copyMetadataFrom(clone);
    RTX_CHECK_EQ(destination.width, 32);
    RTX_CHECK(destination.planes[0] == original_plane);  // storage is untouched
    RTX_CHECK_EQ(destination.device_planes[0], static_cast<DevicePtr>(0x12345678));

    Frame like;
    RTX_REQUIRE(like.allocateHostLike(source).ok());
    RTX_CHECK_EQ(like.width, source.width);
    RTX_CHECK_EQ(like.pts, source.pts);
    RTX_CHECK_EQ(like.time_base, source.time_base);
    RTX_CHECK(like.planes[0] != nullptr);
}

RTX_TEST(types_device_residency) {
    Frame frame;
    RTX_CHECK(!frame.isDeviceResident());
    frame.device_planes[0] = 0x1000;
    RTX_CHECK(frame.isDeviceResident());
    frame.device_planes[0] = kInvalidDevicePtr;
    RTX_CHECK(!frame.isDeviceResident());
}
