// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "../test_framework.h"
#include "../test_util.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "rtx/geometry.h"
#include "rtx/io.h"
#include "rtx/types.h"

using namespace rtx;

RTX_TEST(audio_stream_copy_demux_probe) {
    std::filesystem::path sample_path = ".vs/gemini_generated_video_6eaef4a7.mp4";
    if (!std::filesystem::exists(sample_path)) {
        sample_path = "gemini_generated_video_6eaef4a7.mp4";
    }
    if (!std::filesystem::exists(sample_path)) {
        // If sample video not present in local workspace, skip gracefully
        return;
    }

    auto demuxer = makeFfmpegDemuxer();
    RTX_REQUIRE(demuxer != nullptr);

    auto res = demuxer->open(sample_path);
    RTX_REQUIRE(res.has_value());
    const MediaInfo& info = res.value();

    RTX_CHECK(info.audio_streams >= 1);
    RTX_CHECK(info.primary_audio_index >= 0);
    RTX_CHECK(static_cast<int>(info.audio_tracks.size()) >= 1);

    const AudioTrackInfo& track = info.audio_tracks.front();
    RTX_CHECK_EQ(track.stream_index, info.primary_audio_index);
    RTX_CHECK(track.codec_name == "aac" || track.codec_id != 0);
    RTX_CHECK(track.sample_rate > 0);
    RTX_CHECK(track.channels > 0);
    RTX_CHECK(track.time_base.valid());
}

RTX_TEST(audio_stream_copy_packet_read) {
    std::filesystem::path sample_path = ".vs/gemini_generated_video_6eaef4a7.mp4";
    if (!std::filesystem::exists(sample_path)) {
        sample_path = "gemini_generated_video_6eaef4a7.mp4";
    }
    if (!std::filesystem::exists(sample_path)) {
        return;
    }

    auto demuxer = makeFfmpegDemuxer();
    RTX_REQUIRE(demuxer != nullptr);
    auto res = demuxer->open(sample_path);
    RTX_REQUIRE(res.has_value());
    const MediaInfo& info = res.value();

    bool saw_audio_packet = false;
    for (int i = 0; i < 50; ++i) {
        auto pkt = demuxer->readPacket();
        if (!pkt.has_value()) {
            break;
        }
        if (pkt.value().stream_index == info.primary_audio_index) {
            saw_audio_packet = true;
            RTX_CHECK(!pkt.value().data.empty());
            RTX_CHECK(pkt.value().pts != std::numeric_limits<int64_t>::min());
            break;
        }
    }
    RTX_CHECK(saw_audio_packet);
}

RTX_TEST(audio_stream_copy_muxer_roundtrip) {
    std::filesystem::path sample_path = ".vs/gemini_generated_video_6eaef4a7.mp4";
    if (!std::filesystem::exists(sample_path)) {
        sample_path = "gemini_generated_video_6eaef4a7.mp4";
    }
    if (!std::filesystem::exists(sample_path)) {
        return;
    }

    auto demuxer = makeFfmpegDemuxer();
    RTX_REQUIRE(demuxer != nullptr);
    auto res = demuxer->open(sample_path);
    RTX_REQUIRE(res.has_value());
    const MediaInfo& info = res.value();

    test::TempDir temp_dir;
    std::filesystem::path out_path = temp_dir.file("audio_test_out.mp4");

    auto muxer = makeFfmpegMuxer();
    RTX_REQUIRE(muxer != nullptr);

    Hdr10Metadata hdr;
    auto open_res = muxer->open(out_path, info, hdr);
    RTX_REQUIRE(open_res.has_value());

    // Stream 20 packets through
    int audio_written = 0;
    int video_written = 0;
    for (int i = 0; i < 100; ++i) {
        auto pkt = demuxer->readPacket();
        if (!pkt.has_value()) {
            break;
        }
        if (pkt.value().stream_index == info.primary_audio_index) {
            auto w = muxer->writeAudioPacket(pkt.value());
            RTX_CHECK(w.has_value());
            ++audio_written;
        } else {
            auto w = muxer->writePacket(pkt.value());
            RTX_CHECK(w.has_value());
            ++video_written;
        }
        if (audio_written >= 10 && video_written >= 10) {
            break;
        }
    }

    auto fin = muxer->finalize();
    RTX_REQUIRE(fin.has_value());

    // Probe output container to verify audio track parameters match
    auto probe_demuxer = makeFfmpegDemuxer();
    auto probe_res = probe_demuxer->open(out_path);
    RTX_REQUIRE(probe_res.has_value());
    const MediaInfo& out_info = probe_res.value();

    RTX_CHECK(out_info.audio_streams >= 1);
    RTX_CHECK(static_cast<int>(out_info.audio_tracks.size()) >= 1);
    const AudioTrackInfo& in_track = info.audio_tracks.front();
    const AudioTrackInfo& out_track = out_info.audio_tracks.front();
    RTX_CHECK_EQ(out_track.sample_rate, in_track.sample_rate);
    RTX_CHECK_EQ(out_track.channels, in_track.channels);
    RTX_CHECK_EQ(out_track.codec_name, in_track.codec_name);
}

RTX_TEST(smart_geometry_portrait_and_pillarbox) {
    // Mode A: Portrait 9:16 (720x1280) -> 2160x3840
    auto res_a = SmartGeometryCalculator::calculate(720, 1280, GeometryTargetMode::AutoDirect);
    RTX_CHECK(res_a.is_portrait);
    RTX_CHECK_EQ(res_a.canvas_width, 2160);
    RTX_CHECK_EQ(res_a.canvas_height, 3840);
    RTX_CHECK_EQ(res_a.scaled_width, 2160);
    RTX_CHECK_EQ(res_a.scaled_height, 3840);

    // Mode B: Portrait 9:16 (720x1280) in 16:9 canvas (3840x2160)
    auto res_b = SmartGeometryCalculator::calculate(720, 1280, GeometryTargetMode::Pillarbox);
    RTX_CHECK(res_b.is_portrait);
    RTX_CHECK_EQ(res_b.canvas_width, 3840);
    RTX_CHECK_EQ(res_b.canvas_height, 2160);
    RTX_CHECK_EQ(res_b.scaled_height, 2160);
    // 2160 * 720 / 1280 = 1215, aligned even = 1214
    RTX_CHECK_EQ(res_b.scaled_width, 1214);
    RTX_CHECK(res_b.pillarbox_left > 1300);
    RTX_CHECK(res_b.pillarbox_right > 1300);
    RTX_CHECK_EQ(res_b.pillarbox_left + res_b.scaled_width + res_b.pillarbox_right, 3840);

    // Standard landscape: 1920x1080 -> 3840x2160
    auto res_land = SmartGeometryCalculator::calculate(1920, 1080, GeometryTargetMode::AutoDirect);
    RTX_CHECK(!res_land.is_portrait);
    RTX_CHECK_EQ(res_land.canvas_width, 3840);
    RTX_CHECK_EQ(res_land.canvas_height, 2160);

    // Defense test: Portrait input with accidental default landscape 4K (3840x2160)
    auto res_def = SmartGeometryCalculator::calculate(720, 1280, GeometryTargetMode::AutoDirect, 3840, 2160);
    RTX_CHECK(res_def.is_portrait);
    RTX_CHECK_EQ(res_def.canvas_width, 2160);
    RTX_CHECK_EQ(res_def.canvas_height, 3840);
}
