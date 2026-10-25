// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "rtx/hdr.h"
#include "rtx/io.h"
#include "rtx/types.h"
#include "test_util.h"

using namespace rtx;

namespace {

std::vector<uint8_t> makeNv12Bytes(Frame& frame, uint8_t luma, uint8_t u, uint8_t v) {
    test::fillNV12(frame, luma, u, v);
    std::vector<uint8_t> bytes;
    bytes.reserve(static_cast<size_t>(frame.width) * frame.height * 3 / 2);
    for (int y = 0; y < frame.height; ++y) {
        const uint8_t* row = frame.planes[0] + static_cast<size_t>(y) * frame.pitches[0];
        bytes.insert(bytes.end(), row, row + frame.width);
    }
    for (int y = 0; y < (frame.height + 1) / 2; ++y) {
        const uint8_t* row = frame.planes[1] + static_cast<size_t>(y) * frame.pitches[1];
        bytes.insert(bytes.end(), row, row + frame.width);
    }
    return bytes;
}

}  // namespace

RTX_TEST(io_synthetic_source) {
    SyntheticNv12Source::Config config;
    config.width = 32;
    config.height = 16;
    config.frame_count = 3;
    config.frame_rate = kFps24;
    config.scene_cut_frame = 1;
    SyntheticNv12Source source(config);
    RTX_CHECK(!source.done());
    int64_t expected_pts = 0;
    while (!source.done()) {
        const Result<Frame> frame = source.next();
        RTX_REQUIRE(frame.ok());
        RTX_CHECK_EQ(frame.value().width, 32);
        RTX_CHECK_EQ(frame.value().height, 16);
        RTX_CHECK(frame.value().format == PixelFormat::NV12);
        RTX_CHECK_EQ(frame.value().pts, expected_pts);
        RTX_CHECK((frame.value().time_base == Rational{1, 24}));
        RTX_CHECK(frame.value().planes[0] != nullptr);
        RTX_CHECK(frame.value().planes[1] != nullptr);
        ++expected_pts;
    }
    RTX_CHECK_EQ(source.index(), static_cast<int64_t>(3));
    RTX_CHECK_EQ(source.next().code(), ErrorCode::NotFound);
    source.reset();
    RTX_CHECK(!source.done());
    RTX_CHECK_EQ(source.index(), static_cast<int64_t>(0));
    RTX_REQUIRE(source.next().ok());

    SyntheticNv12Source::Config invalid;
    invalid.width = 0;
    invalid.height = -4;
    invalid.frame_count = -1;
    SyntheticNv12Source empty(invalid);
    RTX_CHECK_EQ(empty.config().width, 1920);
    RTX_CHECK_EQ(empty.config().height, 1080);
    RTX_CHECK_EQ(empty.config().frame_count, 0);
    RTX_CHECK(empty.done());
    RTX_CHECK_EQ(empty.next().code(), ErrorCode::NotFound);
}

RTX_TEST(io_synthetic_scene_cut_changes_pixels) {
    SyntheticNv12Source::Config config;
    config.width = 64;
    config.height = 32;
    config.frame_count = 3;
    config.scene_cut_frame = 2;
    SyntheticNv12Source source(config);
    RTX_REQUIRE(source.next().ok());
    // The cut starts at frame 2, so compare the last pre-cut frame with the
    // first post-cut frame.
    const Frame before = std::move(source.next().value());
    const Frame after = std::move(source.next().value());
    int changed = 0;
    for (int y = 0; y < config.height; ++y) {
        for (int x = 0; x < config.width; ++x) {
            const size_t offset =
                static_cast<size_t>(y) * before.pitches[0] + static_cast<size_t>(x);
            if (before.planes[0][offset] != after.planes[0][offset]) {
                ++changed;
            }
        }
    }
    RTX_CHECK(changed > (config.width * config.height) / 2);
}

RTX_TEST(io_raw_nv12_reader) {
    test::TempDir directory;
    const std::string path = directory.file("input.nv12");
    Frame first;
    Frame second;
    RTX_REQUIRE(first.allocateHost(8, 4, PixelFormat::NV12).ok());
    RTX_REQUIRE(second.allocateHost(8, 4, PixelFormat::NV12).ok());
    std::vector<uint8_t> bytes = makeNv12Bytes(first, 16, 100, 150);
    const std::vector<uint8_t> second_bytes = makeNv12Bytes(second, 200, 120, 60);
    bytes.insert(bytes.end(), second_bytes.begin(), second_bytes.end());
    RTX_REQUIRE(test::writeAll(path, bytes));

    RawNv12Reader reader;
    RTX_CHECK_EQ(reader.read(0, kTimeBase90k).code(), ErrorCode::State);
    RTX_CHECK_EQ(reader.open(path, 0, 4).code(), ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(reader.open(directory.file("missing.nv12"), 8, 4).code(), ErrorCode::Io);
    RTX_REQUIRE(reader.open(path, 8, 4).ok());
    RTX_CHECK_EQ(reader.frameCount(), static_cast<int64_t>(0));

    Result<Frame> frame = reader.read(5, kTimeBase90k);
    RTX_REQUIRE(frame.ok());
    RTX_CHECK_EQ(frame.value().width, 8);
    RTX_CHECK_EQ(frame.value().height, 4);
    RTX_CHECK_EQ(frame.value().pts, static_cast<int64_t>(5));
    RTX_CHECK((frame.value().time_base == kTimeBase90k));
    for (int y = 0; y < 4; ++y) {
        RTX_CHECK_EQ(frame.value().planes[0][static_cast<size_t>(y) * frame.value().pitches[0]],
                     static_cast<uint8_t>(16));
    }
    RTX_CHECK_EQ(reader.frameCount(), static_cast<int64_t>(1));

    frame = reader.read(6, kTimeBase90k);
    RTX_REQUIRE(frame.ok());
    RTX_CHECK_EQ(frame.value().planes[0][0], static_cast<uint8_t>(200));
    RTX_CHECK_EQ(reader.frameCount(), static_cast<int64_t>(2));
    RTX_CHECK(!reader.eof());

    RTX_CHECK_EQ(reader.read(7, kTimeBase90k).code(), ErrorCode::NotFound);
    RTX_CHECK(reader.eof());
    RTX_CHECK_EQ(reader.read(8, kTimeBase90k).code(), ErrorCode::NotFound);
    reader.close();
    RTX_CHECK_EQ(reader.read(9, kTimeBase90k).code(), ErrorCode::State);

    // A truncated frame is an I/O error, not silent EOF.
    const std::string truncated = directory.file("truncated.nv12");
    RTX_REQUIRE(test::writeAll(truncated, std::vector<uint8_t>(bytes.begin(), bytes.begin() + 20)));
    RawNv12Reader partial;
    RTX_REQUIRE(partial.open(truncated, 8, 4).ok());
    RTX_CHECK_EQ(partial.read(0, kTimeBase90k).code(), ErrorCode::Io);
    RTX_CHECK(partial.eof());
}

RTX_TEST(io_raw_p010_writer) {
    test::TempDir directory;
    const std::string path = directory.file("output.p010");
    RawP010Writer writer;
    Frame frame;
    RTX_REQUIRE(frame.allocateHost(8, 4, PixelFormat::P010).ok());
    test::fillP010(frame, 512, 300, 700);
    RTX_CHECK_EQ(writer.write(frame).code(), ErrorCode::State);
    RTX_CHECK_EQ(writer.open(path, 0, 4).code(), ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(writer.open(directory.file("missing/out.p010"), 8, 4).code(), ErrorCode::Io);
    RTX_REQUIRE(writer.open(path, 8, 4).ok());
    RTX_CHECK(writer.finalize().ok());

    RTX_REQUIRE(writer.open(path, 8, 4).ok());
    Frame wrong_size;
    RTX_REQUIRE(wrong_size.allocateHost(4, 4, PixelFormat::P010).ok());
    RTX_CHECK_EQ(writer.write(wrong_size).code(), ErrorCode::InvalidArgument);
    Frame wrong_format;
    RTX_REQUIRE(wrong_format.allocateHost(8, 4, PixelFormat::NV12).ok());
    RTX_CHECK_EQ(writer.write(wrong_format).code(), ErrorCode::InvalidArgument);
    RTX_REQUIRE(writer.write(frame).ok());
    RTX_CHECK_EQ(writer.bytesWritten(), static_cast<uint64_t>(8 * 4 * 3));
    RTX_CHECK(writer.finalize().ok());
    RTX_CHECK(writer.finalize().ok());

    const std::vector<uint8_t> bytes = test::readAll(path);
    RTX_CHECK_EQ(bytes.size(), static_cast<size_t>(8 * 4 * 3));
    RTX_CHECK_EQ(bytes[0], static_cast<uint8_t>(frame.planes[0][0]));
    RTX_CHECK_EQ(bytes[1], static_cast<uint8_t>(frame.planes[0][1]));
}

RTX_TEST(io_hdr_sidecar_round_trip) {
    test::TempDir directory;
    TrueHdrParameters parameters;
    parameters.max_luminance = 1600;
    parameters.max_fall = 420;
    const Result<Hdr10Metadata> metadata = SeiSynthesizer::makeMetadata(parameters);
    RTX_REQUIRE(metadata.ok());
    const std::string path = directory.file("hdr.json");
    RTX_CHECK(writeHdrSidecar(path, metadata.value()).ok());
    const Result<Hdr10Metadata> restored = readHdrSidecar(path);
    RTX_REQUIRE(restored.ok());
    RTX_CHECK_EQ(restored.value().target_nits, static_cast<uint32_t>(1600));
    RTX_CHECK_EQ(restored.value().cll.max_fall, static_cast<uint16_t>(420));
    RTX_CHECK_EQ(restored.value().mastering.max_luminance,
                 metadata.value().mastering.max_luminance);
    RTX_CHECK(restored.value().enabled);

    RTX_CHECK_EQ(readHdrSidecar(directory.file("absent.json")).code(), ErrorCode::Io);
    RTX_CHECK_EQ(writeHdrSidecar(directory.file("missing/dir/hdr.json"), metadata.value()).code(),
                 ErrorCode::Io);

    const std::string bad = directory.file("bad.json");
    RTX_CHECK(test::writeAll(bad, std::vector<uint8_t>{'{', '}'}));
    RTX_CHECK_EQ(readHdrSidecar(bad).code(), ErrorCode::InvalidArgument);
}

RTX_TEST(io_ffmpeg_diagnostics) {
    std::unique_ptr<IDemuxer> demuxer = makeFfmpegDemuxer();
    RTX_REQUIRE(demuxer != nullptr);
    std::unique_ptr<IMuxer> muxer = makeFfmpegMuxer();
    RTX_REQUIRE(muxer != nullptr);

    if (ffmpegAvailable()) {
        test::TempDir directory;
        const Result<MediaInfo> opened = demuxer->open(directory.file("missing.mkv"));
        RTX_CHECK(!opened.ok());
        RTX_CHECK_EQ(opened.code(), ErrorCode::Io);
        RTX_CHECK_EQ(demuxer->readPacket().code(), ErrorCode::State);
        RTX_CHECK(demuxer->warnings().empty());

        RTX_CHECK_EQ(muxer->writePacket(Packet{}).code(), ErrorCode::State);
        RTX_CHECK_EQ(muxer->open(directory.file("out.mkv"), MediaInfo{}, Hdr10Metadata{}).code(),
                     ErrorCode::InvalidArgument);
        RTX_CHECK(muxer->finalize().ok());
        RTX_CHECK(muxer->finalize().ok());
    } else {
        const Result<MediaInfo> opened = demuxer->open("video.mkv");
        RTX_CHECK(!opened.ok());
        RTX_CHECK_EQ(opened.code(), ErrorCode::FfmpegMissing);
        RTX_CHECK(opened.error().detail.find("RTX_FFMPEG_ROOT") != std::string::npos);
        RTX_CHECK_EQ(demuxer->readPacket().code(), ErrorCode::FfmpegMissing);
        RTX_CHECK(demuxer->warnings().empty());

        RTX_CHECK_EQ(muxer->open("out.mkv", MediaInfo{}, Hdr10Metadata{}).code(),
                     ErrorCode::FfmpegMissing);
        RTX_CHECK_EQ(muxer->writePacket(Packet{}).code(), ErrorCode::FfmpegMissing);
        RTX_CHECK(muxer->finalize().ok());
    }
}
