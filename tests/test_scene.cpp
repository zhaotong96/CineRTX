// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "rtx/scene_detect.h"
#include "rtx/types.h"
#include "test_util.h"

using namespace rtx;

RTX_TEST(scene_cut_detection_basics) {
    SceneCutDetector detector;
    RTX_CHECK_EQ(detector.config().sample_stride, 4);
    Frame previous;
    Frame current;
    RTX_REQUIRE(previous.allocateHost(16, 8, PixelFormat::NV12).ok());
    RTX_REQUIRE(current.allocateHost(16, 8, PixelFormat::NV12).ok());
    test::fillNV12(previous, 32, 128, 128);
    test::fillNV12(current, 32, 128, 128);

    SceneCutResult result = detector.analyze(previous.planes[0], current.planes[0], 16, 8,
                                             previous.pitches[0]);
    RTX_CHECK(!result.cut);
    RTX_CHECK_NEAR(result.normalized_sad, 0.0, 1e-12);
    RTX_CHECK_EQ(result.sampled_pixels, static_cast<uint64_t>(8));
    RTX_CHECK_EQ(result.threshold, detector.config().threshold);
    RTX_CHECK_EQ(detector.evaluateCount(), static_cast<uint64_t>(1));
    RTX_CHECK_EQ(detector.cutCount(), static_cast<uint64_t>(0));

    test::fillNV12(current, 255, 128, 128);
    result = detector.analyze(previous.planes[0], current.planes[0], 16, 8, previous.pitches[0]);
    RTX_CHECK(result.cut);
    RTX_CHECK_NEAR(result.normalized_sad, 223.0 / 255.0, 1e-9);
    RTX_CHECK_EQ(detector.evaluateCount(), static_cast<uint64_t>(2));
    RTX_CHECK_EQ(detector.cutCount(), static_cast<uint64_t>(1));

    // null and degenerate inputs are evaluated but never report a cut.
    result = detector.analyze(nullptr, current.planes[0], 16, 8, previous.pitches[0]);
    RTX_CHECK(!result.cut);
    result = detector.analyze(previous.planes[0], current.planes[0], 0, 8, previous.pitches[0]);
    RTX_CHECK(!result.cut);
    RTX_CHECK_EQ(detector.evaluateCount(), static_cast<uint64_t>(4));
}

RTX_TEST(scene_cut_threshold_and_stride) {
    SceneCutConfig config;
    config.threshold = 0.25;
    config.sample_stride = 2;
    SceneCutDetector detector(config);
    Frame previous;
    Frame current;
    RTX_REQUIRE(previous.allocateHost(8, 8, PixelFormat::NV12).ok());
    RTX_REQUIRE(current.allocateHost(8, 8, PixelFormat::NV12).ok());
    test::fillNV12(previous, 0, 128, 128);
    test::fillNV12(current, 51, 128, 128);
    const SceneCutResult below =
        detector.analyze(previous.planes[0], current.planes[0], 8, 8, previous.pitches[0]);
    RTX_CHECK(!below.cut);
    RTX_CHECK_EQ(below.sampled_pixels, static_cast<uint64_t>(16));

    test::fillNV12(current, 64, 128, 128);
    const SceneCutResult above =
        detector.analyze(previous.planes[0], current.planes[0], 8, 8, previous.pitches[0]);
    RTX_CHECK(above.cut);
    RTX_CHECK_NEAR(above.normalized_sad, 64.0 / 255.0, 1e-9);
    detector.noteCut();
    RTX_CHECK_EQ(detector.cutCount(), static_cast<uint64_t>(2));

    // The detector honours the supplied pitch instead of assuming width.
    Frame padded;
    RTX_REQUIRE(padded.allocateHost(8, 8, PixelFormat::NV12).ok());
    test::fillNV12(padded, 64, 128, 128);
    const SceneCutResult padded_result =
        detector.analyze(previous.planes[0], padded.planes[0], 8, 8, padded.pitches[0]);
    RTX_CHECK(padded_result.cut);
}

RTX_TEST(scene_cut_histogram) {
    LumaHistogram histogram;
    RTX_CHECK_EQ(histogram.samples(), static_cast<uint64_t>(0));
    RTX_CHECK_EQ(histogram.percentile(0.99), static_cast<uint32_t>(0));
    RTX_CHECK_NEAR(histogram.mean(), 0.0, 1e-12);

    Frame frame;
    RTX_REQUIRE(frame.allocateHost(8, 8, PixelFormat::NV12).ok());
    for (int y = 0; y < 8; ++y) {
        std::memset(frame.planes[0] + static_cast<size_t>(y) * frame.pitches[0], y < 4 ? 0 : 255,
                    8);
    }
    histogram.accumulateNv12(frame.planes[0], 8, 8, frame.pitches[0], 1);
    RTX_CHECK_EQ(histogram.samples(), static_cast<uint64_t>(64));
    RTX_CHECK_NEAR(histogram.mean(), 127.5, 1e-9);
    RTX_CHECK_EQ(histogram.percentile(0.0), static_cast<uint32_t>(0));
    RTX_CHECK_EQ(histogram.percentile(0.5), static_cast<uint32_t>(0));
    RTX_CHECK_EQ(histogram.percentile(0.6), static_cast<uint32_t>(255));
    RTX_CHECK_EQ(histogram.percentile(1.0), static_cast<uint32_t>(255));
    RTX_CHECK_EQ(histogram.percentile(-1.0), static_cast<uint32_t>(0));

    histogram.reset();
    RTX_CHECK_EQ(histogram.samples(), static_cast<uint64_t>(0));
    RTX_CHECK_NEAR(histogram.mean(), 0.0, 1e-12);

    // Null and invalid geometry are no-ops.
    histogram.accumulateNv12(nullptr, 8, 8, 8, 1);
    histogram.accumulateNv12(frame.planes[0], 0, 8, frame.pitches[0], 1);
    RTX_CHECK_EQ(histogram.samples(), static_cast<uint64_t>(0));

    // Non-unit stride subsamples exactly like the histogram gate.
    LumaHistogram strided;
    strided.accumulateNv12(frame.planes[0], 8, 8, frame.pitches[0], 4);
    RTX_CHECK_EQ(strided.samples(), static_cast<uint64_t>(4));
    RTX_CHECK_NEAR(strided.mean(), 127.5, 1e-9);
}

RTX_TEST(scene_resolution_guard) {
    ResolutionGuard guard;
    const Result<bool> first = guard.observe(1920, 1080);
    RTX_REQUIRE(first.ok());
    RTX_CHECK(!first.value());
    RTX_CHECK_EQ(guard.width(), 1920);
    RTX_CHECK_EQ(guard.height(), 1080);
    RTX_CHECK_EQ(guard.resets(), 0);

    const Result<bool> same = guard.observe(1920, 1080);
    RTX_REQUIRE(same.ok());
    RTX_CHECK(!same.value());
    const Result<bool> changed = guard.observe(3840, 2160);
    RTX_REQUIRE(changed.ok());
    RTX_CHECK(changed.value());
    RTX_CHECK_EQ(guard.resets(), 1);

    ResolutionGuard disabled(ResolutionGuard::Config{false, 4});
    RTX_CHECK(disabled.observe(64, 64).ok());
    const Result<bool> rejected = disabled.observe(128, 64);
    RTX_CHECK(!rejected.ok());
    RTX_CHECK_EQ(rejected.code(), ErrorCode::Unsupported);

    ResolutionGuard limited(ResolutionGuard::Config{true, 1});
    RTX_CHECK(limited.observe(64, 64).ok());
    RTX_CHECK(limited.observe(128, 128).ok());
    const Result<bool> third = limited.observe(256, 256);
    RTX_CHECK(!third.ok());
    RTX_CHECK_EQ(third.code(), ErrorCode::State);
    RTX_CHECK_EQ(limited.resets(), 1);
}

RTX_TEST(scene_luma_stats_fraction_above) {
    LumaHistogram histogram;
    RTX_CHECK_NEAR(histogram.fractionAbove(235), 0.0, 1e-12);
    FrameLumaStats empty = lumaStats(histogram);
    RTX_CHECK_NEAR(empty.y_norm, 0.0, 1e-12);
    RTX_CHECK_NEAR(empty.p_high, 0.0, 1e-12);

    Frame frame;
    RTX_REQUIRE(frame.allocateHost(8, 8, PixelFormat::NV12).ok());
    for (int y = 0; y < 8; ++y) {
        std::memset(frame.planes[0] + static_cast<size_t>(y) * frame.pitches[0],
                    y < 4 ? 100 : 250, 8);
    }
    histogram.accumulateNv12(frame.planes[0], 8, 8, frame.pitches[0], 1);
    const FrameLumaStats stats = lumaStats(histogram);
    RTX_CHECK_NEAR(stats.y_norm, 175.0 / 255.0, 1e-9);
    RTX_CHECK_NEAR(stats.p_high, 0.5, 1e-9);  // half the samples are >= 235

    histogram.reset();
    RTX_CHECK_NEAR(histogram.fractionAbove(0), 0.0, 1e-12);
}

RTX_TEST(scene_truehdr_adaptive_law) {
    const TrueHdrAdapter adapter;

    // Golden baseline: standard daylight content keeps the 100-based values.
    TrueHdrAdaptiveParams p = adapter.target(0.5, 0.0);
    RTX_CHECK_EQ(p.contrast, 125);
    RTX_CHECK_EQ(p.middle_gray, 44);
    RTX_CHECK_EQ(p.saturation, 75);

    // Dark scene: contrast retreats toward 105, middle gray lifts toward 48.
    p = adapter.target(0.09, 0.0);
    RTX_CHECK_EQ(p.contrast, 115);
    RTX_CHECK_EQ(p.middle_gray, 46);
    RTX_CHECK_EQ(p.saturation, 75);
    p = adapter.target(0.0, 0.0);
    RTX_CHECK_EQ(p.contrast, 105);
    RTX_CHECK_EQ(p.middle_gray, 48);
    RTX_CHECK_EQ(p.saturation, 75);

    // Highlight-heavy frame: contrast capped at 110, middle gray down to the
    // 36 floor, saturation converging to the 68 floor.
    p = adapter.target(0.5, 0.2);
    RTX_CHECK_EQ(p.contrast, 110);
    RTX_CHECK_EQ(p.middle_gray, 36);
    RTX_CHECK_EQ(p.saturation, 68);
    p = adapter.target(0.5, 0.10);
    RTX_CHECK_EQ(p.contrast, 110);
    RTX_CHECK_EQ(p.middle_gray, 41);
    RTX_CHECK_EQ(p.saturation, 73);

    // Dark and highlight-heavy at once: highlight protection wins.
    p = adapter.target(0.09, 0.2);
    RTX_CHECK_EQ(p.contrast, 110);
    RTX_CHECK_EQ(p.middle_gray, 36);
    RTX_CHECK_EQ(p.saturation, 68);

    // Out-of-range statistics clamp instead of extrapolating.
    p = adapter.target(2.0, -1.0);
    RTX_CHECK_EQ(p.contrast, 125);
    RTX_CHECK_EQ(p.middle_gray, 44);
    RTX_CHECK_EQ(p.saturation, 75);
}

RTX_TEST(scene_truehdr_ema_smoothing) {
    TrueHdrAdapter adapter;  // alpha 0.92
    RTX_CHECK(!adapter.initialized());

    // First frame jumps to the target (no history to smooth from).
    TrueHdrAdaptiveParams p = adapter.update(0.5, 0.0, false);
    RTX_CHECK_EQ(p.contrast, 125);
    RTX_CHECK_EQ(p.middle_gray, 44);
    RTX_CHECK_EQ(p.saturation, 75);
    RTX_CHECK(adapter.initialized());

    // One highlight frame: 8% of the way from the baseline to the target.
    p = adapter.update(0.5, 0.2, false);
    RTX_CHECK_EQ(p.contrast, 124);  // 0.92*125 + 0.08*110
    RTX_CHECK_EQ(p.middle_gray, 43);  // 0.92*44 + 0.08*36
    RTX_CHECK_EQ(p.saturation, 74);   // 0.92*75 + 0.08*68

    // Repeated highlight frames converge onto the highlight target.
    for (int i = 0; i < 200; ++i) {
        p = adapter.update(0.5, 0.2, false);
    }
    RTX_CHECK_EQ(p.contrast, 110);
    RTX_CHECK_EQ(p.middle_gray, 36);
    RTX_CHECK_EQ(p.saturation, 68);

    // A scene cut jumps straight to the new shot's steady state.
    p = adapter.update(0.09, 0.0, true);
    RTX_CHECK_EQ(p.contrast, 115);
    RTX_CHECK_EQ(p.middle_gray, 46);
    RTX_CHECK_EQ(p.saturation, 75);

    // Reset drops the history again.
    adapter.reset();
    RTX_CHECK(!adapter.initialized());
    p = adapter.update(0.5, 0.0, false);
    RTX_CHECK_EQ(p.contrast, 125);
}
