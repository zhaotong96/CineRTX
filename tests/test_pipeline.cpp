// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "rtx/io.h"
#include "rtx/pipeline.h"
#include "rtx/types.h"
#include "test_util.h"

using namespace rtx;

namespace {

PipelineRunConfig smallConfig() {
    PipelineRunConfig config;
    config.width = 64;
    config.height = 32;
    config.target_width = 128;
    config.target_height = 64;
    config.input_frames = 12;
    config.input_fps = kFps24;
    config.output_fps = kFps60;
    return config;
}

std::vector<uint8_t> makeRawNv12(int width, int height, int frames) {
    std::vector<uint8_t> bytes;
    for (int frame = 0; frame < frames; ++frame) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                bytes.push_back(static_cast<uint8_t>((x + y + frame * 7) & 0xFF));
            }
        }
        for (int y = 0; y < (height + 1) / 2; ++y) {
            for (int x = 0; x < width; ++x) {
                bytes.push_back(static_cast<uint8_t>(128 + ((x + frame) & 0x1F)));
            }
        }
    }
    return bytes;
}

}  // namespace

RTX_TEST(pipeline_synthetic_24_to_60) {
    const PipelineRunConfig config = smallConfig();
    const Result<PipelineReport> result = runSyntheticPipeline(config);
    RTX_REQUIRE(result.ok());
    const PipelineReport& report = result.value();
    RTX_CHECK_EQ(report.backend, std::string("cpu-reference"));
    RTX_CHECK_EQ(report.input_width, 64);
    RTX_CHECK_EQ(report.input_height, 32);
    RTX_CHECK_EQ(report.output_width, 128);
    RTX_CHECK_EQ(report.output_height, 64);
    RTX_CHECK_EQ(report.input_frames, static_cast<int64_t>(12));
    RTX_CHECK_EQ(report.output_frames, static_cast<int64_t>(30));
    // Counts per interval are 2,3,2,3,2,3,2,3,2,3,2,3.  Every interval except
    // the last has a successor and contributes (count - 1) interpolated
    // frames; the final interval duplicates its two extra frames.
    RTX_CHECK_EQ(report.interpolated_frames, static_cast<int64_t>(16));
    RTX_CHECK_EQ(report.duplicated_frames, static_cast<int64_t>(2));
    RTX_CHECK_EQ(report.interpolated_frames + report.duplicated_frames,
                 report.output_frames - report.input_frames);
    RTX_CHECK_EQ(report.scene_cuts, static_cast<int64_t>(0));
    RTX_CHECK(report.packet_bytes > 0);
    RTX_CHECK_EQ(report.p010_bytes, static_cast<uint64_t>(0));
    RTX_CHECK_EQ(report.zero_copy, std::string("not-applicable (CPU reference path)"));
    RTX_CHECK(report.hdr.enabled);
    RTX_CHECK_EQ(report.metrics.frames, static_cast<uint64_t>(30));
    RTX_CHECK_EQ(report.metrics.flush_count, static_cast<uint64_t>(30));
    RTX_CHECK(report.metrics.elapsed_ms > 0.0);
    RTX_CHECK(report.metrics.output_fps > 0.0);
    RTX_CHECK_EQ(report.metrics.stages[static_cast<size_t>(Stage::Enhance)].submissions,
                 static_cast<uint64_t>(30));
    RTX_CHECK_EQ(report.metrics.stages[static_cast<size_t>(Stage::Pack)].submissions,
                 static_cast<uint64_t>(30));
    RTX_CHECK_EQ(report.metrics.stages[static_cast<size_t>(Stage::Encode)].submissions,
                 static_cast<uint64_t>(30));

    const std::string json = report.toJson();
    RTX_CHECK(json.find("\"frames\": 30") != std::string::npos);
    RTX_CHECK(json.find("\"input\": {\"width\": 64") != std::string::npos);
    RTX_CHECK(json.find("\"interpolated\": 16") != std::string::npos);
    RTX_CHECK(json.find("\"cpu-reference\"") != std::string::npos);
    RTX_CHECK(json.find("\"hdr\"") != std::string::npos);
}

RTX_TEST(pipeline_without_interpolation) {
    PipelineRunConfig config = smallConfig();
    config.interpolate = false;
    const Result<PipelineReport> result = runSyntheticPipeline(config);
    RTX_REQUIRE(result.ok());
    RTX_CHECK_EQ(result.value().output_frames, static_cast<int64_t>(12));
    RTX_CHECK_EQ(result.value().interpolated_frames, static_cast<int64_t>(0));
    RTX_CHECK_EQ(result.value().duplicated_frames, static_cast<int64_t>(0));
    RTX_CHECK_EQ(result.value().metrics.stages[static_cast<size_t>(Stage::Interpolate)].submissions,
                 static_cast<uint64_t>(0));
}

RTX_TEST(pipeline_scene_cut_bypasses_interpolation) {
    PipelineRunConfig config = smallConfig();
    config.scene_cut_frame = 6;
    int logged_cuts = 0;
    const auto log = [&logged_cuts](const std::string& message) {
        if (message.find("scene cut") != std::string::npos) {
            ++logged_cuts;
        }
    };
    const Result<PipelineReport> result = runSyntheticPipeline(config, log);
    RTX_REQUIRE(result.ok());
    RTX_CHECK(result.value().scene_cuts >= 1);
    RTX_CHECK_EQ(logged_cuts, static_cast<int>(result.value().scene_cuts));
    RTX_CHECK(result.value().duplicated_frames >= 3);
    RTX_CHECK_EQ(result.value().interpolated_frames + result.value().duplicated_frames,
                 result.value().output_frames - result.value().input_frames);
    RTX_CHECK_EQ(result.value().metrics.frames, static_cast<uint64_t>(30));

    config.scene_detection = false;
    const Result<PipelineReport> disabled = runSyntheticPipeline(config);
    RTX_REQUIRE(disabled.ok());
    RTX_CHECK_EQ(disabled.value().scene_cuts, static_cast<int64_t>(0));
}

RTX_TEST(pipeline_p010_sidecar_and_trace) {
    test::TempDir directory;
    PipelineRunConfig config = smallConfig();
    config.input_frames = 4;
    config.output_p010 = directory.file("out.p010");
    config.hdr_sidecar = directory.file("hdr.json");
    config.trace_path = directory.file("trace.json");
    const Result<PipelineReport> result = runSyntheticPipeline(config);
    RTX_REQUIRE(result.ok());
    RTX_CHECK_EQ(result.value().output_frames, static_cast<int64_t>(10));
    const uint64_t expected_bytes = static_cast<uint64_t>(config.target_width) *
                                    config.target_height * 3ull * 10ull;
    RTX_CHECK_EQ(result.value().p010_bytes, expected_bytes);
    const std::vector<uint8_t> p010 = test::readAll(config.output_p010);
    RTX_CHECK_EQ(p010.size(), static_cast<size_t>(expected_bytes));
    RTX_CHECK(!p010.empty());
    // P010 stores 10 bits in the high bits of little-endian 16-bit samples.
    const uint16_t first = static_cast<uint16_t>(p010[0] | (p010[1] << 8));
    RTX_CHECK((first >> 6) <= 1023);

    const Result<Hdr10Metadata> sidecar = readHdrSidecar(config.hdr_sidecar);
    RTX_REQUIRE(sidecar.ok());
    RTX_CHECK(sidecar.value().enabled);
    RTX_CHECK_EQ(sidecar.value().target_nits, static_cast<uint32_t>(1000));

    const std::vector<uint8_t> trace = test::readAll(config.trace_path);
    RTX_REQUIRE(!trace.empty());
    const std::string trace_json(trace.begin(), trace.end());
    RTX_CHECK(trace_json.find("traceEvents") != std::string::npos);
    RTX_CHECK(trace_json.find("frame.output") != std::string::npos);
    RTX_CHECK(trace_json.find("ngx.enhance") != std::string::npos);
}

RTX_TEST(pipeline_without_hdr) {
    test::TempDir directory;
    PipelineRunConfig config = smallConfig();
    config.input_frames = 2;
    config.enable_hdr = false;
    config.hdr_sidecar = directory.file("should-not-exist.json");
    const Result<PipelineReport> result = runSyntheticPipeline(config);
    RTX_REQUIRE(result.ok());
    RTX_CHECK(!result.value().hdr.enabled);
    RTX_CHECK(test::readAll(config.hdr_sidecar).empty());
    RTX_CHECK(result.value().toJson().find("\"enabled\": false") != std::string::npos);
}

RTX_TEST(pipeline_invalid_configuration) {
    PipelineRunConfig config = smallConfig();
    config.width = 0;
    RTX_CHECK_EQ(runSyntheticPipeline(config).code(), ErrorCode::InvalidArgument);
    config = smallConfig();
    config.height = 33;
    RTX_CHECK_EQ(runSyntheticPipeline(config).code(), ErrorCode::InvalidArgument);
    config = smallConfig();
    config.target_width = 32;
    RTX_CHECK_EQ(runSyntheticPipeline(config).code(), ErrorCode::Unsupported);
    config = smallConfig();
    config.input_fps = Rational{24, 0};
    RTX_CHECK_EQ(runSyntheticPipeline(config).code(), ErrorCode::InvalidArgument);
    config = smallConfig();
    config.input_frames = 0;
    RTX_CHECK_EQ(runSyntheticPipeline(config).code(), ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(runRawNv12Pipeline(smallConfig(), "definitely-missing.nv12").code(),
                 ErrorCode::Io);
}

RTX_TEST(pipeline_raw_nv12_input) {
    test::TempDir directory;
    const std::string input = directory.file("input.nv12");
    RTX_REQUIRE(test::writeAll(input, makeRawNv12(32, 16, 4)));
    PipelineRunConfig config = smallConfig();
    config.width = 32;
    config.height = 16;
    config.target_width = 64;
    config.target_height = 32;
    config.input_frames = 0;  // consume the whole file
    config.output_p010 = directory.file("out.p010");
    const Result<PipelineReport> result = runRawNv12Pipeline(config, input);
    RTX_REQUIRE(result.ok());
    RTX_CHECK_EQ(result.value().input_frames, static_cast<int64_t>(4));
    RTX_CHECK_EQ(result.value().output_frames, static_cast<int64_t>(10));
    RTX_CHECK_EQ(result.value().duplicated_frames, static_cast<int64_t>(2));
    RTX_CHECK_EQ(result.value().p010_bytes,
                 static_cast<uint64_t>(64) * 32 * 3ull * 10ull);
    RTX_CHECK_EQ(test::readAll(config.output_p010).size(),
                 static_cast<size_t>(64 * 32 * 3 * 10));

    // A frame limit truncates the reader cleanly rather than reporting EOF as
    // a pipeline failure.
    config.input_frames = 2;
    config.output_p010.clear();
    const Result<PipelineReport> limited = runRawNv12Pipeline(config, input);
    RTX_REQUIRE(limited.ok());
    RTX_CHECK_EQ(limited.value().input_frames, static_cast<int64_t>(2));
    RTX_CHECK_EQ(limited.value().output_frames, static_cast<int64_t>(5));

    // An empty input is a valid zero-frame programme.
    const std::string empty = directory.file("empty.nv12");
    RTX_REQUIRE(test::writeAll(empty, {}));
    config.input_frames = 0;
    const Result<PipelineReport> none = runRawNv12Pipeline(config, empty);
    RTX_REQUIRE(none.ok());
    RTX_CHECK_EQ(none.value().input_frames, static_cast<int64_t>(0));
    RTX_CHECK_EQ(none.value().output_frames, static_cast<int64_t>(0));
    RTX_CHECK_EQ(none.value().metrics.frames, static_cast<uint64_t>(0));
    RTX_CHECK_EQ(none.value().zero_copy, std::string("not-applicable (no frames)"));

    // A truncated frame is propagated as an I/O error.
    const std::string truncated = directory.file("truncated.nv12");
    const std::vector<uint8_t> full = makeRawNv12(32, 16, 1);
    RTX_REQUIRE(test::writeAll(truncated,
                               std::vector<uint8_t>(full.begin(), full.begin() + 100)));
    config.output_p010 = directory.file("truncated-out.p010");
    RTX_CHECK_EQ(runRawNv12Pipeline(config, truncated).code(), ErrorCode::Io);
}

