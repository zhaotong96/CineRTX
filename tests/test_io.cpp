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
    RTX_CHECK(!opened.has_value());
    RTX_CHECK_EQ(opened.error().code, ErrorCode::Io);
    RTX_CHECK_EQ(demuxer->readPacket().error().code, ErrorCode::State);
    RTX_CHECK(demuxer->warnings().empty());
    RTX_CHECK_EQ(demuxer->audioStreamIndex(), -1);

    RTX_CHECK_EQ(muxer->writePacket(Packet{}).error().code, ErrorCode::State);
    RTX_CHECK_EQ(muxer->open(directory.file("out.mkv"), MediaInfo{}, Hdr10Metadata{}).error().code,
                 ErrorCode::InvalidArgument);
    RTX_CHECK(muxer->finalize().has_value());
    RTX_CHECK(muxer->finalize().has_value());
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
    RTX_CHECK(!opened.has_value());
    RTX_CHECK_EQ(opened.error().code, ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(demuxer->readPacket().error().code, ErrorCode::State);

    // Garbage must fail at the container level, proving the memory AVIO was
    // really consumed instead of silently falling back to a file.
    std::unique_ptr<IDemuxer> garbage =
        makeMemoryDemuxer(std::vector<uint8_t>(512, 0x7F));
    RTX_REQUIRE(garbage != nullptr);
    RTX_CHECK_EQ(garbage->open("memory.bin").error().code, ErrorCode::Io);
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
    RTX_REQUIRE(muxer->open("memory.mkv", info, Hdr10Metadata{}).has_value());
    for (int index = 0; index < 2; ++index) {
        Packet packet;
        packet.data = payload;
        packet.pts = index * 40;
        packet.dts = packet.pts;
        packet.duration = 40;
        packet.keyframe = index == 0;
        packet.time_base = Rational{1, 1000};
        RTX_REQUIRE(muxer->writePacket(packet).has_value());
    }
    RTX_REQUIRE(muxer->finalize().has_value());
    RTX_CHECK(container.size() > 64);

    std::unique_ptr<IDemuxer> demuxer = makeMemoryDemuxer(container);
    RTX_REQUIRE(demuxer != nullptr);
    const Result<MediaInfo> opened = demuxer->open("memory.mkv");
    RTX_REQUIRE(opened.has_value());
    RTX_CHECK(opened.value().codec == VideoCodec::HEVC);
    RTX_CHECK_EQ(opened.value().width, 64);
    RTX_CHECK_EQ(opened.value().height, 48);
    RTX_CHECK_EQ(opened.value().extradata.size(), hvcc.size());
    RTX_CHECK(opened.value().extradata == hvcc);

    const Result<Packet> restored = demuxer->readPacket();
    RTX_REQUIRE(restored.has_value());
    RTX_CHECK(restored.value().data == payload);
    RTX_CHECK(restored.value().flicks_valid);
    RTX_CHECK_EQ(restored.value().pts_flicks, static_cast<int64_t>(0));
    RTX_CHECK(restored.value().keyframe);

    // The second packet must be projected from the container millisecond
    // timebase onto the exact 705,600,000 Hz Flicks axis.
    const Result<Packet> second = demuxer->readPacket();
    RTX_REQUIRE(second.has_value());
    RTX_CHECK_EQ(second.value().pts, static_cast<int64_t>(40));
    RTX_CHECK_EQ(second.value().pts_flicks,
                 static_cast<int64_t>(kFlicksPerSecond * 40 / 1000));
    RTX_CHECK(!second.value().keyframe);
}

RTX_TEST(io_memory_bframe_pts_reordering) {
    std::vector<uint8_t> hvcc = {
        0x01, 0x01, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x78, 0xF0, 0x00, 0xFC,
        0xFD, 0xF8, 0xF8, 0x00, 0x00, 0x0F, 0x00,
    };

    MediaInfo info;
    info.codec = VideoCodec::HEVC;
    info.video_codec = "hevc";
    info.width = 64;
    info.height = 48;
    info.bit_depth = 8;
    info.frame_rate = Rational{25, 1};
    info.time_base = Rational{1, 1000};
    info.format_name = "matroska";
    info.extradata = hvcc;
    info.annex_b = true;

    std::vector<uint8_t> payload = {0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0C, 0x01,
                                    0xAA, 0xBB, 0xCC, 0xDD};

    // Typical IBBP GOP delivered in decoding (DTS) order with positive composition delay:
    // Packet 0 (I): DTS 0, PTS 80 (PTS >= DTS)
    // Packet 1 (P): DTS 40, PTS 200 (PTS >= DTS)
    // Packet 2 (B): DTS 80, PTS 120 (PTS >= DTS, but PTS 120 < P-frame PTS 200!)
    // Packet 3 (B): DTS 120, PTS 160 (PTS >= DTS, but PTS 160 < P-frame PTS 200!)
    const int64_t pts_order[] = {80, 200, 120, 160};
    const int64_t dts_order[] = {0, 40, 80, 120};

    std::vector<uint8_t> container;
    std::unique_ptr<IMuxer> muxer = makeMemoryMuxer(&container, "matroska");
    RTX_REQUIRE(muxer != nullptr);
    RTX_REQUIRE(muxer->open("bframe.mkv", info, Hdr10Metadata{}).has_value());

    for (int i = 0; i < 4; ++i) {
        Packet packet;
        packet.data = payload;
        packet.pts = pts_order[i];
        packet.dts = dts_order[i];
        packet.duration = 40;
        packet.keyframe = (i == 0);
        packet.time_base = Rational{1, 1000};
        RTX_REQUIRE(muxer->writePacket(packet).has_value());
    }
    RTX_REQUIRE(muxer->finalize().has_value());

    std::unique_ptr<IDemuxer> demuxer = makeMemoryDemuxer(container);
    RTX_REQUIRE(demuxer != nullptr);
    RTX_REQUIRE(demuxer->open("bframe.mkv").has_value());

    std::vector<int64_t> read_pts;
    std::vector<int64_t> read_flicks;
    for (int i = 0; i < 4; ++i) {
        const Result<Packet> pkt = demuxer->readPacket();
        RTX_REQUIRE(pkt.has_value());
        read_pts.push_back(pkt.value().pts);
        read_flicks.push_back(pkt.value().pts_flicks);
    }

    // Assert that the native B-frame PTS sequence (non-monotonic) is preserved:
    RTX_CHECK_EQ(read_pts[0], 80);
    RTX_CHECK_EQ(read_pts[1], 200);
    RTX_CHECK_EQ(read_pts[2], 120);
    RTX_CHECK_EQ(read_pts[3], 160);

    // Assert Flicks timebase values reflect exact non-monotonic B-frame order
    RTX_CHECK_EQ(read_flicks[0], static_cast<int64_t>(kFlicksPerSecond * 80 / 1000));
    RTX_CHECK_EQ(read_flicks[1], static_cast<int64_t>(kFlicksPerSecond * 200 / 1000));
    RTX_CHECK_EQ(read_flicks[2], static_cast<int64_t>(kFlicksPerSecond * 120 / 1000));
    RTX_CHECK_EQ(read_flicks[3], static_cast<int64_t>(kFlicksPerSecond * 160 / 1000));

    // Specifically: Packet 2 (B-frame) PTS must precede Packet 1 (P-frame) PTS
    RTX_CHECK(read_flicks[2] < read_flicks[1]);
}
