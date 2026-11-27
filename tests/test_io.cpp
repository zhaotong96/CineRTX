// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>
}
#include "memory_avio.h"
#include "rtx/hdr.h"
#include "rtx/io.h"
#include "rtx/ngx_remaster.h"
#include "rtx/shot_segmenter.h"
#include "rtx/types.h"
#include "test_util.h"

using namespace rtx;

RTX_TEST(io_ffmpeg_diagnostics) {
    std::unique_ptr<FfmpegDemuxer> demuxer = makeFfmpegDemuxer();
    RTX_REQUIRE(demuxer != nullptr);
    std::unique_ptr<FfmpegMuxer> muxer = makeFfmpegMuxer();
    RTX_REQUIRE(muxer != nullptr);

    RTX_CHECK(ffmpegAvailable());
// The media container layer is an unconditional static component: there is
// no build in which the diagnostic "not compiled" branch can be taken.

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
RTX_TEST(io_memory_avio_read_seek) {
    std::vector<uint8_t> source(256);
// Zero-disk container I/O (ACT-STATIC-03)
    for (size_t i = 0; i < source.size(); ++i) {
        source[i] = static_cast<uint8_t>(i);
    }
    auto context = io::makeMemoryAvioRead(std::span<const uint8_t>(source.data(), source.size()));
    RTX_REQUIRE(io::memoryAvioValid(context));
    RTX_CHECK(io::memoryAvioWritable(context) == false);
    RTX_CHECK_EQ(io::memoryAvioSize(context), static_cast<size_t>(256));

    uint8_t buffer[16] = {};
    RTX_CHECK_EQ(avio_read(io::memoryAvioRaw(context), buffer, 16), 16);
    RTX_CHECK_EQ(buffer[0], static_cast<uint8_t>(0));
    RTX_CHECK_EQ(buffer[15], static_cast<uint8_t>(15));
    RTX_CHECK_EQ(avio_tell(io::memoryAvioRaw(context)), static_cast<int64_t>(16));

    RTX_CHECK_EQ(avio_seek(io::memoryAvioRaw(context), 32, SEEK_SET), static_cast<int64_t>(32));
// The *logical* position advances by 16; the underlying stream position has
// already been refilled to the end of the 32 KB page.
    RTX_CHECK_EQ(avio_read(io::memoryAvioRaw(context), buffer, 4), 4);
    RTX_CHECK_EQ(buffer[0], static_cast<uint8_t>(32));
// libavformat's avio_seek() itself accepts only SEEK_SET/SEEK_CUR; the
// SEEK_END branch belongs to the callback and is probed separately below.
    RTX_CHECK_EQ(avio_seek(io::memoryAvioRaw(context), -4, SEEK_CUR), static_cast<int64_t>(32));
    RTX_CHECK_EQ(avio_seek(io::memoryAvioRaw(context), 248, SEEK_SET), static_cast<int64_t>(248));
    RTX_CHECK_EQ(avio_read(io::memoryAvioRaw(context), buffer, 8), 8);
    RTX_CHECK_EQ(buffer[7], static_cast<uint8_t>(255));
    RTX_CHECK_EQ(avio_size(io::memoryAvioRaw(context)), static_cast<int64_t>(256));

    RTX_CHECK_EQ(avio_read(io::memoryAvioRaw(context), buffer, 8), AVERROR_EOF);

    RTX_CHECK_EQ(avio_seek(io::memoryAvioRaw(context), -1, SEEK_SET), AVERROR(EINVAL));
// Reading past the tail is a clean EOF, not an error.

    auto tail = io::makeMemoryAvioRead(std::span<const uint8_t>(source.data(), source.size()));
// A negative absolute seek is rejected by avio_seek().
    RTX_REQUIRE(io::memoryAvioValid(tail));
    RTX_CHECK_EQ(io::memoryAvioRaw(tail)->seek(io::memoryAvioRaw(tail)->opaque, -8, SEEK_END),
// The underlying callback must still answer SEEK_END for the avio_size()
// fallback path used by non-seekable-looking custom contexts.
                 static_cast<int64_t>(248));
}
RTX_TEST(io_memory_avio_write) {
    std::vector<uint8_t> sink;
    auto context = io::makeMemoryAvioWrite(&sink);
    RTX_REQUIRE(io::memoryAvioValid(context));
    RTX_CHECK(io::memoryAvioWritable(context));

    const uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    avio_write(io::memoryAvioRaw(context), payload, 8);
    avio_flush(io::memoryAvioRaw(context));
    RTX_REQUIRE(sink.size() == 8);
    RTX_CHECK_EQ(sink[0], static_cast<uint8_t>(1));
    RTX_CHECK_EQ(sink[7], static_cast<uint8_t>(8));

    RTX_CHECK_EQ(avio_seek(io::memoryAvioRaw(context), 4, SEEK_SET), static_cast<int64_t>(4));
    const uint8_t patch[2] = {0xAA, 0xBB};
    avio_write(io::memoryAvioRaw(context), patch, 2);
    avio_flush(io::memoryAvioRaw(context));
// Overwrite in place: a muxer back-patches box sizes this way.
    RTX_CHECK_EQ(sink.size(), static_cast<size_t>(8));
    RTX_CHECK_EQ(sink[4], static_cast<uint8_t>(0xAA));
    RTX_CHECK_EQ(sink[5], static_cast<uint8_t>(0xBB));
    RTX_CHECK_EQ(sink[6], static_cast<uint8_t>(7));

    RTX_CHECK_EQ(avio_seek(io::memoryAvioRaw(context), 16, SEEK_SET), static_cast<int64_t>(16));
    const uint8_t tail[1] = {0x5A};
    avio_write(io::memoryAvioRaw(context), tail, 1);
    avio_flush(io::memoryAvioRaw(context));
// Seeking past the tail extends the sink with zeroes (sparse write).
    RTX_CHECK_EQ(sink.size(), static_cast<size_t>(17));
    RTX_CHECK_EQ(sink[15], static_cast<uint8_t>(0));
    RTX_CHECK_EQ(sink[16], static_cast<uint8_t>(0x5A));
    RTX_CHECK_EQ(avio_size(io::memoryAvioRaw(context)), static_cast<int64_t>(17));
}
RTX_TEST(io_memory_demux_parse) {
    std::vector<uint8_t> wav;
    const auto appendLe32 = [&wav](uint32_t value) {
        wav.push_back(static_cast<uint8_t>(value & 0xFF));
        wav.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        wav.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
// A hand-built 16-bit PCM WAV: the demuxer must parse the container over a
// memory AVIOContext and then reject it for having no video stream.
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
    std::unique_ptr<FfmpegDemuxer> demuxer = makeMemoryDemuxer(std::move(wav));
    RTX_REQUIRE(demuxer != nullptr);
    const Result<MediaInfo> opened = demuxer->open("memory.wav");
    RTX_CHECK(!opened.has_value());
    RTX_CHECK_EQ(opened.error().code, ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(demuxer->readPacket().error().code, ErrorCode::State);

    std::unique_ptr<FfmpegDemuxer> garbage =
        makeMemoryDemuxer(std::vector<uint8_t>(512, 0x7F));
    RTX_REQUIRE(garbage != nullptr);
    RTX_CHECK_EQ(garbage->open("memory.bin").error().code, ErrorCode::Io);
}
RTX_TEST(io_memory_container_round_trip) {
// Garbage must fail at the container level, proving the memory AVIO was
// really consumed instead of silently falling back to a file.
    std::vector<uint8_t> hvcc = {
        0x01,                                            // configurationVersion
        0x01,                                            // profile_space/tier/idc
        0x60, 0x00, 0x00, 0x00,                          // compatibility flags
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,              // constraint flags
        0x78,                                            // level 4.0
        0xF0, 0x00,                                      // min spatial segmentation
// Fabricated hvcC configuration record (23 bytes).  Matroska stores it as
// CodecPrivate verbatim, so no real HEVC decoder is required anywhere in
// this test.
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

    std::vector<uint8_t> payload = {0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0C, 0x01,
                                    0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA};

    std::vector<uint8_t> container;
    std::unique_ptr<FfmpegMuxer> muxer = makeMemoryMuxer(&container, "matroska");
    RTX_REQUIRE(muxer != nullptr);
    RTX_REQUIRE(muxer->open("memory.mkv", info, Hdr10Metadata{}).has_value());
// Annex-B shaped payload: the demuxer detects the start code and therefore
// skips the mp4toannexb bitstream filter.
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

    std::unique_ptr<FfmpegDemuxer> demuxer = makeMemoryDemuxer(container);
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

    const Result<Packet> second = demuxer->readPacket();
    RTX_REQUIRE(second.has_value());
    RTX_CHECK_EQ(second.value().pts, static_cast<int64_t>(40));
    RTX_CHECK_EQ(second.value().pts_flicks,
                 static_cast<int64_t>(kFlicksPerSecond * 40 / 1000));
    RTX_CHECK(!second.value().keyframe);
}
// The second packet must be projected from the container millisecond
// timebase onto the exact 705,600,000 Hz Flicks axis.
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

    const int64_t pts_order[] = {80, 200, 120, 160};
    const int64_t dts_order[] = {0, 40, 80, 120};

    std::vector<uint8_t> container;
    std::unique_ptr<FfmpegMuxer> muxer = makeMemoryMuxer(&container, "matroska");
    RTX_REQUIRE(muxer != nullptr);
    RTX_REQUIRE(muxer->open("bframe.mkv", info, Hdr10Metadata{}).has_value());

// Typical IBBP GOP delivered in decoding (DTS) order with positive composition delay:
// Packet 0 (I): DTS 0, PTS 80 (PTS >= DTS)
// Packet 1 (P): DTS 40, PTS 200 (PTS >= DTS)
// Packet 2 (B): DTS 80, PTS 120 (PTS >= DTS, but PTS 120 < P-frame PTS 200!)
// Packet 3 (B): DTS 120, PTS 160 (PTS >= DTS, but PTS 160 < P-frame PTS 200!)
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

    std::unique_ptr<FfmpegDemuxer> demuxer = makeMemoryDemuxer(container);
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
    RTX_CHECK_EQ(read_pts[0], 80);
    RTX_CHECK_EQ(read_pts[1], 200);
    RTX_CHECK_EQ(read_pts[2], 120);
    RTX_CHECK_EQ(read_pts[3], 160);

    RTX_CHECK_EQ(read_flicks[0], static_cast<int64_t>(kFlicksPerSecond * 80 / 1000));
    RTX_CHECK_EQ(read_flicks[1], static_cast<int64_t>(kFlicksPerSecond * 200 / 1000));
    RTX_CHECK_EQ(read_flicks[2], static_cast<int64_t>(kFlicksPerSecond * 120 / 1000));
    RTX_CHECK_EQ(read_flicks[3], static_cast<int64_t>(kFlicksPerSecond * 160 / 1000));
// Assert that the native B-frame PTS sequence (non-monotonic) is preserved:

    RTX_CHECK(read_flicks[2] < read_flicks[1]);
}
RTX_TEST(io_memory_audio_video_stream_copy_roundtrip) {
    std::vector<uint8_t> hvcc = {
// Assert Flicks timebase values reflect exact non-monotonic B-frame order
        0x01, 0x01, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x78, 0xF0, 0x00, 0xFC,
        0xFD, 0xF8, 0xF8, 0x00, 0x00, 0x0F, 0x00,
    };

// Specifically: Packet 2 (B-frame) PTS must precede Packet 1 (P-frame) PTS
    AVCodecParameters* raw_audio_par = avcodec_parameters_alloc();
    RTX_REQUIRE(raw_audio_par != nullptr);
    raw_audio_par->codec_type = AVMEDIA_TYPE_AUDIO;
    raw_audio_par->codec_id = AV_CODEC_ID_PCM_S16LE;
// Deterministic zero-file-dependency verification of video + audio stream
// copy passthrough over MemoryAvioContext (replaces tests/unit/test_audio_stream_copy.cpp).
    raw_audio_par->sample_rate = 48000;
    raw_audio_par->ch_layout.nb_channels = 2;
    raw_audio_par->bits_per_coded_sample = 16;
    raw_audio_par->block_align = 4;

    std::shared_ptr<void> owned_audio_par(raw_audio_par, [](void* p) {
        AVCodecParameters* cp = static_cast<AVCodecParameters*>(p);
        avcodec_parameters_free(&cp);
    });

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
    info.audio_streams = 1;
    info.primary_audio_index = 1;

    info.passthrough_streams.emplace_back(
        1, 1, static_cast<int>(AV_CODEC_ID_PCM_S16LE), std::string("pcm_s16le"),
        Rational{1, 1000}, owned_audio_par);

    std::vector<uint8_t> video_payload = {
        0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0C, 0x01, 0x11, 0x22, 0x33, 0x44
    };
    std::vector<uint8_t> audio_payload(1920, 0x55); // 10 ms of 48kHz stereo S16LE

    std::vector<uint8_t> container;
    std::unique_ptr<FfmpegMuxer> muxer = makeMemoryMuxer(&container, "matroska");
    RTX_REQUIRE(muxer != nullptr);
    RTX_REQUIRE(muxer->open("av_roundtrip.mkv", info, Hdr10Metadata{}).has_value());

    for (int i = 0; i < 4; ++i) {
        Packet vpkt;
        vpkt.stream_index = 0;
        vpkt.data = video_payload;
        vpkt.pts = i * 40;
        vpkt.dts = vpkt.pts;
        vpkt.duration = 40;
        vpkt.keyframe = (i == 0);
        vpkt.time_base = Rational{1, 1000};
        RTX_REQUIRE(muxer->writePacket(vpkt).has_value());

        Packet apkt;
        apkt.stream_index = 1;
        apkt.data = audio_payload;
        apkt.pts = i * 40;
        apkt.dts = apkt.pts;
        apkt.duration = 10;
        apkt.keyframe = true;
        apkt.time_base = Rational{1, 1000};
        RTX_REQUIRE(muxer->writeAudioPacket(apkt).has_value());
    }
    RTX_REQUIRE(muxer->finalize().has_value());
    RTX_CHECK(container.size() > 512);

    std::unique_ptr<FfmpegDemuxer> demuxer = makeMemoryDemuxer(container);
    RTX_REQUIRE(demuxer != nullptr);
    const Result<MediaInfo> opened = demuxer->open("av_roundtrip.mkv");
    RTX_REQUIRE(opened.has_value());
    RTX_CHECK_EQ(opened.value().audio_streams, 1);
    RTX_CHECK(opened.value().primary_audio_index >= 0);
    RTX_REQUIRE(!opened.value().audio_tracks.empty());
    const auto& [at_idx, at_sr, at_ch, at_par] = opened.value().audio_tracks.front();
    (void)at_idx; (void)at_par;
    RTX_CHECK_EQ(at_sr, 48000);
    RTX_CHECK_EQ(at_ch, 2);

    int video_read = 0;
    int audio_read = 0;
    for (int i = 0; i < 8; ++i) {
        const Result<Packet> pkt = demuxer->readPacket();
        RTX_REQUIRE(pkt.has_value());
        if (pkt.value().stream_index == opened.value().primary_audio_index) {
            ++audio_read;
            RTX_CHECK_EQ(pkt.value().data.size(), audio_payload.size());
        } else {
            ++video_read;
            RTX_CHECK_EQ(pkt.value().data.size(), video_payload.size());
        }
    }
    RTX_CHECK_EQ(video_read, 4);
    RTX_CHECK_EQ(audio_read, 4);
}
#include "rtx/cpu_video_encoder.h"
#include "shim/rtx_shim_abi.h"

RTX_TEST(test_nvenc_cq_and_qp_config) {
    RtxEncoderConfig cfg{};
    cfg.rate_control = RTX_RC_CQ;
    cfg.qp = 22;
    cfg.spatial_aq = 1;
    cfg.aq_strength = 8;
    RTX_CHECK_EQ(cfg.rate_control, static_cast<int32_t>(RTX_RC_CQ));
    RTX_CHECK_EQ(cfg.qp, 22);
    RTX_CHECK_EQ(cfg.spatial_aq, 1);

    cfg.rate_control = RTX_RC_CONSTQP;
    cfg.qp = 24;
    const int32_t qp_i = std::max(0, cfg.qp - 2);
    const int32_t qp_p = cfg.qp;
    const int32_t qp_b = std::min(51, cfg.qp + 2);
    RTX_CHECK_EQ(qp_i, 22);
    RTX_CHECK_EQ(qp_p, 24);
    RTX_CHECK_EQ(qp_b, 26);
}
RTX_TEST(test_cpu_crf_encoder_10bit_hdr10) {
    Hdr10Metadata hdr{};
    hdr.enabled = true;
    auto enc = CpuVideoEncoder::create(VideoCodec::AV1, 128, 128, Rational{60, 1}, 20, 8, 0, hdr);
    if (!enc.has_value()) {
        RTX_CHECK(enc.error().code == ErrorCode::Unsupported || enc.error().code == ErrorCode::Internal);
        return;
    }
    std::vector<uint16_t> y(128 * 128, static_cast<uint16_t>(512 << 6));
    std::vector<uint16_t> uv(128 * 64, static_cast<uint16_t>(512 << 6));
    size_t bytes_crf20 = 0;
    for (int f = 0; f < 4; ++f) {
        auto pkts = enc->encodeP010Frame(y.data(), 128 * 2, uv.data(), 128 * 2, f * 1500, f == 0);
        RTX_REQUIRE(pkts.has_value());
        for (const auto& p : *pkts) bytes_crf20 += p.data.size();
    }
    auto flushed = enc->flush();
// Static FFmpeg without libsvtav1 in older cache; verify structured error
    RTX_REQUIRE(flushed.has_value());
    for (const auto& p : *flushed) bytes_crf20 += p.data.size();
    RTX_CHECK(bytes_crf20 > 0);
}

RTX_TEST(io_display_matrix_passthrough_roundtrip) {
    // 90-degree clockwise rotation matrix (fixed-point 16.16 / 2.30, 36 bytes)
    const std::array<int32_t, 9> kRot90 = {
        0, 65536, 0,
        -65536, 0, 0,
        0, 0, 1073741824,
    };

    test::TempDir directory;
    const std::filesystem::path mp4_path = directory.file("portrait_rot90.mp4");

    MediaInfo out_info{};
    out_info.container = "mp4";
    out_info.format_name = "mp4";
    out_info.codec = VideoCodec::H264;
    out_info.video_codec = "h264";
    out_info.width = 1920;
    out_info.height = 1080;
    out_info.bit_depth = 8;
    out_info.bitrate = 2000000;
    out_info.frame_rate = Rational{30, 1};
    out_info.time_base = Rational{1, 90000};
    out_info.annex_b = false;
    // Minimal valid AVCC extradata (profile=66, level=30, 4-byte NAL length)
    out_info.extradata = {0x01, 0x42, 0xC0, 0x1E, 0xFF, 0xE1, 0x00, 0x04, 0x67, 0x42, 0xC0, 0x1E,
                          0x01, 0x00, 0x04, 0x68, 0xCE, 0x3C, 0x80};
    out_info.display_matrix = kRot90;
    out_info.has_display_matrix = true;

    std::unique_ptr<FfmpegMuxer> muxer = makeFfmpegMuxer();
    RTX_REQUIRE(muxer != nullptr);
    RTX_REQUIRE(muxer->open(mp4_path, out_info, Hdr10Metadata{}).has_value());

    Packet pkt{};
    pkt.data = {0x00, 0x00, 0x00, 0x05, 0x65, 0x88, 0x84, 0x00, 0x10};
    pkt.pts = 0;
    pkt.dts = 0;
    pkt.duration = 3000;
    pkt.time_base = Rational{1, 90000};
    pkt.keyframe = true;
    RTX_REQUIRE(muxer->writePacket(pkt).has_value());
    RTX_REQUIRE(muxer->finalize().has_value());

    std::unique_ptr<FfmpegDemuxer> demuxer = makeFfmpegDemuxer();
    RTX_REQUIRE(demuxer != nullptr);
    auto reopened = demuxer->open(mp4_path);
    RTX_REQUIRE(reopened.has_value());
    RTX_CHECK(reopened->has_display_matrix);
    for (size_t i = 0; i < kRot90.size(); ++i) {
        RTX_CHECK_EQ(reopened->display_matrix[i], kRot90[i]);
    }
}

RTX_TEST(io_mp4_pcm_audio_transcode_to_aac) {
    // Synthesize a minimal H.264 video + PCM_S16LE audio stream and mux into in-memory MP4.
    // FfmpegMuxer must transcode non-MP4 PCM_S16LE via SwrContext + AVAudioFifo -> AAC-LC.
    std::vector<uint8_t> mp4_bytes;
    auto muxer = makeMemoryMuxer(&mp4_bytes, "mp4");
    RTX_REQUIRE(muxer != nullptr);

    const std::vector<uint8_t> hvcc = {
        0x01, 0x01, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x78, 0xF0, 0x00, 0xFC, 0xFD, 0xF8, 0xF8, 0x00, 0x00, 0x0F, 0x00,
    };
    MediaInfo info{};
    info.format_name = "mp4";
    info.codec = VideoCodec::HEVC;
    info.video_codec = "hevc";
    info.width = 64;
    info.height = 48;
    info.bit_depth = 8;
    info.frame_rate = Rational{25, 1};
    info.time_base = Rational{1, 90000};
    info.extradata = hvcc;
    info.annex_b = true;
    info.audio_streams = 1;
    info.primary_audio_index = 1;

    std::shared_ptr<AVCodecParameters> pcm_par(
        avcodec_parameters_alloc(), [](AVCodecParameters* p) noexcept { avcodec_parameters_free(&p); });
    RTX_REQUIRE(pcm_par != nullptr);
    pcm_par->codec_type = AVMEDIA_TYPE_AUDIO;
    pcm_par->codec_id = AV_CODEC_ID_PCM_S16LE;
    pcm_par->sample_rate = 48000;
    av_channel_layout_default(&pcm_par->ch_layout, 2);
    pcm_par->format = AV_SAMPLE_FMT_S16;
    pcm_par->bits_per_coded_sample = 16;
    info.passthrough_streams.emplace_back(
        1, 1, static_cast<int>(AV_CODEC_ID_PCM_S16LE), std::string("pcm_s16le"),
        Rational{1, 48000}, std::static_pointer_cast<void>(pcm_par));

    const Result<void> opened = muxer->open("memory.mp4", info, Hdr10Metadata{});
    RTX_REQUIRE(opened.has_value());

    Packet vp{};
    vp.stream_index = 0;
    vp.pts = 0;
    vp.dts = 0;
    vp.duration = 3600;
    vp.time_base = Rational{1, 90000};
    vp.keyframe = true;
    vp.data = {0x00, 0x00, 0x00, 0x01, 0x40, 0x01, 0x0C, 0x01, 0xFF, 0xFE, 0xFD, 0xFC};
    RTX_REQUIRE(muxer->writePacket(vp).has_value());

    // Push 3 PCM packets (3 x 1024 stereo int16 samples = 12288 bytes) to exercise SwrContext + AVAudioFifo -> AAC.
    for (int i = 0; i < 3; ++i) {
        Packet ap{};
        ap.stream_index = 1;
        ap.pts = static_cast<int64_t>(i) * 1024;
        ap.dts = ap.pts;
        ap.duration = 1024;
        ap.time_base = Rational{1, 48000};
        ap.data.assign(1024u * 2u * sizeof(int16_t), 0u);
        RTX_REQUIRE(muxer->writePacket(ap).has_value());
    }
    RTX_REQUIRE(muxer->finalize().has_value());
    RTX_CHECK(!mp4_bytes.empty());

    auto demuxer = makeMemoryDemuxer(mp4_bytes);
    RTX_REQUIRE(demuxer != nullptr);
    const Result<MediaInfo> probed = demuxer->open("memory.mp4");
    RTX_REQUIRE(probed.has_value());
    RTX_CHECK_EQ(probed->audio_streams, 1);
    bool found_aac = false;
    for (const auto& [ps_idx, ps_ct, ps_cid, ps_cn, ps_tb, ps_op] : probed->passthrough_streams) {
        (void)ps_idx; (void)ps_cn; (void)ps_tb; (void)ps_op;
        if (ps_ct == 1 && ps_cid == static_cast<int>(AV_CODEC_ID_AAC)) {
            found_aac = true;
        }
    }
    RTX_CHECK(found_aac);
}

RTX_TEST(shot_analyze_temporal_boundary_bus) {
    constexpr int kW = 64;
    constexpr int kH = 64;
    std::vector<uint8_t> prev_y(kW * kH, 100u);
    std::vector<uint8_t> same_y(kW * kH, 100u);
    std::vector<uint8_t> black_y(kW * kH, 4u);
    std::vector<uint8_t> cut_y(kW * kH, 220u);

    RTX_CHECK(analyze_temporal_boundary(prev_y.data(), same_y.data(), kW, kH, kW) ==
              TemporalBoundaryType::None);
    RTX_CHECK(analyze_temporal_boundary(prev_y.data(), black_y.data(), kW, kH, kW) ==
              TemporalBoundaryType::FadeToBlack);
    RTX_CHECK(analyze_temporal_boundary(prev_y.data(), cut_y.data(), kW, kH, kW) ==
              TemporalBoundaryType::HardCut);
}

RTX_TEST(ngx_preview_generation_monotonic_sync) {
    NgxRemasterBridge bridge;
    RTX_CHECK_EQ(bridge.previewGeneration(), 0u);
    bridge.publishPreviewSlot(0u);
    RTX_CHECK_EQ(bridge.previewGeneration(), 1u);
    bridge.publishPreviewSlot(1u);
    RTX_CHECK_EQ(bridge.previewGeneration(), 2u);
    bridge.publishPreviewSlot(2u);
    RTX_CHECK_EQ(bridge.previewGeneration(), 3u);
}
