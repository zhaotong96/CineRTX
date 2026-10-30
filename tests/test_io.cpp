// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
}

#include "memory_avio.h"
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

    // The media container layer is an unconditional static component: there is
    // no build in which the diagnostic "not compiled" branch can be taken.
    RTX_CHECK(ffmpegAvailable());

    test::TempDir directory;
    const Result<MediaInfo> opened = demuxer->open(directory.file("missing.mkv"));
    RTX_CHECK(!opened.ok());
    RTX_CHECK_EQ(opened.code(), ErrorCode::Io);
    RTX_CHECK_EQ(demuxer->readPacket().code(), ErrorCode::State);
    RTX_CHECK(demuxer->warnings().empty());
    RTX_CHECK_EQ(demuxer->audioStreamIndex(), -1);

    RTX_CHECK_EQ(muxer->writePacket(Packet{}).code(), ErrorCode::State);
    RTX_CHECK_EQ(muxer->open(directory.file("out.mkv"), MediaInfo{}, Hdr10Metadata{}).code(),
                 ErrorCode::InvalidArgument);
    RTX_CHECK(muxer->finalize().ok());
    RTX_CHECK(muxer->finalize().ok());
}

// ---------------------------------------------------------------------------
// Zero-disk container I/O (ACT-STATIC-03)
// ---------------------------------------------------------------------------

RTX_TEST(io_memory_avio_read_seek) {
    std::vector<uint8_t> source(256);
    for (size_t i = 0; i < source.size(); ++i) {
        source[i] = static_cast<uint8_t>(i);
    }
    io::MemoryAvioContext context(std::span<const uint8_t>(source.data(), source.size()));
    RTX_REQUIRE(context.valid());
    RTX_CHECK(context.writable() == false);
    RTX_CHECK_EQ(context.size(), static_cast<size_t>(256));

    uint8_t buffer[16] = {};
    RTX_CHECK_EQ(avio_read(context.raw(), buffer, 16), 16);
    RTX_CHECK_EQ(buffer[0], static_cast<uint8_t>(0));
    RTX_CHECK_EQ(buffer[15], static_cast<uint8_t>(15));
    // The *logical* position advances by 16; the underlying stream position has
    // already been refilled to the end of the 32 KB page.
    RTX_CHECK_EQ(avio_tell(context.raw()), static_cast<int64_t>(16));

    // libavformat's avio_seek() itself accepts only SEEK_SET/SEEK_CUR; the
    // SEEK_END branch belongs to the callback and is probed separately below.
    RTX_CHECK_EQ(avio_seek(context.raw(), 32, SEEK_SET), static_cast<int64_t>(32));
    RTX_CHECK_EQ(avio_read(context.raw(), buffer, 4), 4);
    RTX_CHECK_EQ(buffer[0], static_cast<uint8_t>(32));
    RTX_CHECK_EQ(avio_seek(context.raw(), -4, SEEK_CUR), static_cast<int64_t>(32));
    RTX_CHECK_EQ(avio_seek(context.raw(), 248, SEEK_SET), static_cast<int64_t>(248));
    RTX_CHECK_EQ(avio_read(context.raw(), buffer, 8), 8);
    RTX_CHECK_EQ(buffer[7], static_cast<uint8_t>(255));
    RTX_CHECK_EQ(avio_size(context.raw()), static_cast<int64_t>(256));

    // Reading past the tail is a clean EOF, not an error.
    RTX_CHECK_EQ(avio_read(context.raw(), buffer, 8), AVERROR_EOF);

    // A negative absolute seek is rejected by avio_seek().
    RTX_CHECK_EQ(avio_seek(context.raw(), -1, SEEK_SET), AVERROR(EINVAL));

    // The underlying callback must still answer SEEK_END for the avio_size()
    // fallback path used by non-seekable-looking custom contexts.
    io::MemoryAvioContext tail(std::span<const uint8_t>(source.data(), source.size()));
    RTX_REQUIRE(tail.valid());
    RTX_CHECK_EQ(tail.raw()->seek(tail.raw()->opaque, -8, SEEK_END),
                 static_cast<int64_t>(248));
}

RTX_TEST(io_memory_avio_write) {
    std::vector<uint8_t> sink;
    io::MemoryAvioContext context(&sink);
    RTX_REQUIRE(context.valid());
    RTX_CHECK(context.writable());

    const uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    avio_write(context.raw(), payload, 8);
    avio_flush(context.raw());
    RTX_REQUIRE(sink.size() == 8);
    RTX_CHECK_EQ(sink[0], static_cast<uint8_t>(1));
    RTX_CHECK_EQ(sink[7], static_cast<uint8_t>(8));

    // Overwrite in place: a muxer back-patches box sizes this way.
    RTX_CHECK_EQ(avio_seek(context.raw(), 4, SEEK_SET), static_cast<int64_t>(4));
    const uint8_t patch[2] = {0xAA, 0xBB};
    avio_write(context.raw(), patch, 2);
    avio_flush(context.raw());
    RTX_CHECK_EQ(sink.size(), static_cast<size_t>(8));
    RTX_CHECK_EQ(sink[4], static_cast<uint8_t>(0xAA));
    RTX_CHECK_EQ(sink[5], static_cast<uint8_t>(0xBB));
    RTX_CHECK_EQ(sink[6], static_cast<uint8_t>(7));

    // Seeking past the tail extends the sink with zeroes (sparse write).
    RTX_CHECK_EQ(avio_seek(context.raw(), 16, SEEK_SET), static_cast<int64_t>(16));
    const uint8_t tail[1] = {0x5A};
    avio_write(context.raw(), tail, 1);
    avio_flush(context.raw());
    RTX_CHECK_EQ(sink.size(), static_cast<size_t>(17));
    RTX_CHECK_EQ(sink[15], static_cast<uint8_t>(0));
    RTX_CHECK_EQ(sink[16], static_cast<uint8_t>(0x5A));
    RTX_CHECK_EQ(avio_size(context.raw()), static_cast<int64_t>(17));
}

RTX_TEST(io_memory_demux_parse) {
    // A hand-built 16-bit PCM WAV: the demuxer must parse the container over a
    // memory AVIOContext and then reject it for having no video stream.
    std::vector<uint8_t> wav;
    const auto appendLe32 = [&wav](uint32_t value) {
        wav.push_back(static_cast<uint8_t>(value & 0xFF));
        wav.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        wav.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        wav.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    };
    const auto appendLe16 = [&wav](uint16_t value) {
        wav.push_back(static_cast<uint8_t>(value & 0xFF));
        wav.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    };
    const auto appendTag = [&wav](const char* tag) {
        for (int i = 0; i < 4; ++i) {
            wav.push_back(static_cast<uint8_t>(tag[i]));
        }
    };
    const uint32_t samples = 64;
    appendTag("RIFF");
    appendLe32(36 + samples);
    appendTag("WAVE");
    appendTag("fmt ");
    appendLe32(16);
    appendLe16(1);                 // PCM
    appendLe16(1);                 // mono
    appendLe32(8000);              // sample rate
    appendLe32(16000);             // byte rate
    appendLe16(2);                 // block align
    appendLe16(16);                // bits per sample
    appendTag("data");
    appendLe32(samples);
    for (uint32_t i = 0; i < samples; ++i) {
        appendLe16(static_cast<uint16_t>(i * 97));
    }

    std::unique_ptr<IDemuxer> demuxer = makeMemoryDemuxer(std::move(wav));
    RTX_REQUIRE(demuxer != nullptr);
    const Result<MediaInfo> opened = demuxer->open("memory.wav");
    RTX_CHECK(!opened.ok());
    RTX_CHECK_EQ(opened.code(), ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(demuxer->readPacket().code(), ErrorCode::State);

    // Garbage must fail at the container level, proving the memory AVIO was
    // really consumed instead of silently falling back to a file.
    std::unique_ptr<IDemuxer> garbage =
        makeMemoryDemuxer(std::vector<uint8_t>(512, 0x7F));
    RTX_REQUIRE(garbage != nullptr);
    RTX_CHECK_EQ(garbage->open("memory.bin").code(), ErrorCode::Io);
}

RTX_TEST(io_memory_container_round_trip) {
    // Fabricated hvcC configuration record (23 bytes).  Matroska stores it as
    // CodecPrivate verbatim, so no real HEVC decoder is required anywhere in
    // this test.
    std::vector<uint8_t> hvcc = {
        0x01,                                            // configurationVersion
        0x01,                                            // profile_space/tier/idc
        0x60, 0x00, 0x00, 0x00,                          // compatibility flags
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              // constraint flags
        0x78,                                            // level 4.0
        0xF0, 0x00,                                      // min spatial segmentation
        0xFC,                                            // parallelism type
        0xFD,                                            // chroma format 4:2:0
        0xF8,                                            // luma bit depth
        0xF8,                                            // chroma bit depth
        0x00, 0x00,                                      // avg frame rate
        0x0F,                                            // lengthSizeMinusOne = 3
        0x00,                                            // numOfArrays
    };

    MediaInfo info;
    info.codec = VideoCodec::HEVC;
    info.video_codec = "hevc";
    info.width = 64;
    info.height = 48;
    info.bit_depth = 8;
    info.frame_rate = Rational{24, 1};
    info.time_base = Rational{1, 1000};
    info.format_name = "matroska";
    info.extradata = hvcc;
    info.annex_b = true;

    // Annex-B shaped payload: the demuxer detects the start code and therefore
    // skips the mp4toannexb bitstream filter.
    std::vector<uint8_t> payload = {0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0C, 0x01,
                                    0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA};

    std::vector<uint8_t> container;
    std::unique_ptr<IMuxer> muxer = makeMemoryMuxer(&container, "matroska");
    RTX_REQUIRE(muxer != nullptr);
    RTX_REQUIRE(muxer->open("memory.mkv", info, Hdr10Metadata{}).ok());
    for (int index = 0; index < 2; ++index) {
        Packet packet;
        packet.data = payload;
        packet.pts = index * 40;
        packet.dts = packet.pts;
        packet.duration = 40;
        packet.keyframe = index == 0;
        packet.time_base = Rational{1, 1000};
        RTX_REQUIRE(muxer->writePacket(packet).ok());
    }
    RTX_REQUIRE(muxer->finalize().ok());
    RTX_CHECK(container.size() > 64);

    std::unique_ptr<IDemuxer> demuxer = makeMemoryDemuxer(container);
    RTX_REQUIRE(demuxer != nullptr);
    const Result<MediaInfo> opened = demuxer->open("memory.mkv");
    RTX_REQUIRE(opened.ok());
    RTX_CHECK(opened.value().codec == VideoCodec::HEVC);
    RTX_CHECK_EQ(opened.value().width, 64);
    RTX_CHECK_EQ(opened.value().height, 48);
    RTX_CHECK_EQ(opened.value().extradata.size(), hvcc.size());
    RTX_CHECK(opened.value().extradata == hvcc);

    const Result<Packet> restored = demuxer->readPacket();
    RTX_REQUIRE(restored.ok());
    RTX_CHECK(restored.value().data == payload);
    RTX_CHECK(restored.value().flicks_valid);
    RTX_CHECK_EQ(restored.value().pts_flicks, static_cast<int64_t>(0));
    RTX_CHECK(restored.value().keyframe);

    // The second packet must be projected from the container millisecond
    // timebase onto the exact 705,600,000 Hz Flicks axis.
    const Result<Packet> second = demuxer->readPacket();
    RTX_REQUIRE(second.ok());
    RTX_CHECK_EQ(second.value().pts, static_cast<int64_t>(40));
    RTX_CHECK_EQ(second.value().pts_flicks,
                 static_cast<int64_t>(kFlicksPerSecond * 40 / 1000));
    RTX_CHECK(!second.value().keyframe);
}
