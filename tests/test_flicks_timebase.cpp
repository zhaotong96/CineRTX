// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Flicks timeline contract (W7-2).

#include "test_framework.h"

#include <cstdint>
#include <vector>

#include "rtx/rational.h"

using namespace rtx;

RTX_TEST(flicks_exact_division) {
    // 1 flick = 1 / 705,600,000 s.  Every frame period and every audio sample
    // period below must divide it with a zero remainder, which is the property
    // the whole drift budget rests on.
    RTX_CHECK_EQ(kFlicksPerSecond, static_cast<uint64_t>(705600000));

    const Rational video_rates[] = {kFps24,  Rational{24000, 1001}, Rational{25, 1},
                                    Rational{30000, 1001}, kFps30, Rational{48000, 1001},
                                    Rational{50, 1}, Rational{60000, 1001}, kFps60,
                                    kFps120};
    for (const Rational& rate : video_rates) {
        const uint64_t numerator = static_cast<uint64_t>(rate.num);
        const uint64_t denominator = static_cast<uint64_t>(rate.den);
        RTX_CHECK_EQ(kFlicksPerSecond * denominator % numerator, static_cast<uint64_t>(0));
        const uint64_t period = kFlicksPerSecond * denominator / numerator;
        RTX_CHECK(period > 0);
        // One frame period (den/num seconds) expressed in Flicks must be exact.
        const int64_t period_flicks =
            rescaleToFlicks(1, Rational{rate.den, rate.num});
        RTX_CHECK_EQ(period_flicks, static_cast<int64_t>(period));
    }

    const uint32_t audio_rates[] = {8000, 44100, 48000, 96000, 192000};
    for (uint32_t rate : audio_rates) {
        RTX_CHECK_EQ(kFlicksPerSecond % rate, static_cast<uint64_t>(0));
        const int64_t period_flicks = rescaleToFlicks(1, Rational{1, static_cast<int64_t>(rate)});
        RTX_CHECK_EQ(period_flicks, static_cast<int64_t>(kFlicksPerSecond / rate));
    }
}

RTX_TEST(flicks_drift_24h) {
    // 24 hours of 29.97 fps video: 2,588,860 frames.  The remainder-carrying
    // rescale must accumulate exactly zero error, so the residual stays far
    // below the 1.42 ns budget.
    constexpr int64_t kHours = 24;
    constexpr uint64_t kSecondsPerHour = 3600;
    const Rational time_base{1001, 30000};  // MKV-style 29.97 time base
    const uint64_t frames = kHours * kSecondsPerHour * 30000u / 1001u;

    FlicksDriftCompensator drift;
    int64_t last = 0;
    for (uint64_t index = 0; index < frames; ++index) {
        const int64_t pts = static_cast<int64_t>(index);
        last = drift.rescaleToFlicks(pts, static_cast<uint32_t>(time_base.num),
                                     static_cast<uint32_t>(time_base.den));
    }

    // Exact expectation in 128-bit terms: (frames-1) * 1001 / 30000 seconds.
    const uint64_t expected = (frames - 1) * 1001ull * kFlicksPerSecond / 30000ull;
    const int64_t difference = last - static_cast<int64_t>(expected);
    RTX_CHECK(difference >= -1 && difference <= 1);

    // 1 flick = 1.417 ns; a one-flick residual is the analytic floor.
    const double drift_ns = static_cast<double>(difference) * (1.0e9 / 705600000.0);
    RTX_CHECK(drift_ns <= 1.42);
}

RTX_TEST(flicks_negative_and_zero) {
    FlicksDriftCompensator drift;
    RTX_CHECK_EQ(drift.rescaleToFlicks(0, 1, 1000), static_cast<int64_t>(0));
    RTX_CHECK_EQ(drift.rescaleToFlicks(1000, 1, 1000), static_cast<int64_t>(kFlicksPerSecond));
    RTX_CHECK_EQ(drift.rescaleToFlicks(-1000, 1, 1000),
                 -static_cast<int64_t>(kFlicksPerSecond));
    // A zero denominator is rejected rather than dividing by zero.
    RTX_CHECK_EQ(drift.rescaleToFlicks(1000, 1, 0), static_cast<int64_t>(0));

    // The stateless helper must agree with the compensator on exact multiples.
    RTX_CHECK_EQ(rescaleToFlicks(1000, Rational{1, 1000}),
                 static_cast<int64_t>(kFlicksPerSecond));
    RTX_CHECK_EQ(rescaleToFlicks(-1000, Rational{1, 1000}),
                 -static_cast<int64_t>(kFlicksPerSecond));
    RTX_CHECK_EQ(rescaleToFlicks(0, Rational{1, 90000}), static_cast<int64_t>(0));
    // An invalid time base yields 0 instead of trapping.
    RTX_CHECK_EQ(rescaleToFlicks(100, Rational{1, 0}), static_cast<int64_t>(0));
}

RTX_TEST(flicks_monotonic_guard) {
    FlicksMonotonicGuard guard;
    RTX_CHECK_EQ(guard.apply(100), static_cast<int64_t>(100));
    RTX_CHECK_EQ(guard.apply(200), static_cast<int64_t>(200));
    // A regression is repaired to last + 1, one flick (1.4 ns) later.
    RTX_CHECK_EQ(guard.apply(150), static_cast<int64_t>(201));
    RTX_CHECK_EQ(guard.repairs(), static_cast<uint64_t>(1));
    // An exact duplicate is also a regression: the timeline must be strictly
    // increasing, not merely non-decreasing.
    RTX_CHECK_EQ(guard.apply(201), static_cast<int64_t>(202));
    RTX_CHECK_EQ(guard.repairs(), static_cast<uint64_t>(2));
    RTX_CHECK(guard.primed());
    RTX_CHECK_EQ(guard.last(), static_cast<int64_t>(202));

    guard.reset();
    RTX_CHECK(!guard.primed());
    RTX_CHECK_EQ(guard.apply(-5), static_cast<int64_t>(-5));
    RTX_CHECK_EQ(guard.repairs(), static_cast<uint64_t>(0));
}
