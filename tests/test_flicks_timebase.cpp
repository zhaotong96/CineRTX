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

RTX_TEST(flicks_fruc_sliding_window_math) {
    constexpr int64_t kFlicks = 705600000LL;

    // 1. 24 fps -> 48 fps (2x)
    {
        const int64_t delta_in = kFlicks / 24;   // 29,400,000
        const int64_t delta_out = kFlicks / 48;  // 14,700,000
        RTX_CHECK_EQ(delta_in, 29400000LL);
        RTX_CHECK_EQ(delta_out, 14700000LL);

        int64_t next_out = 0;
        int frames_emitted = 0;
        int zero_copy_count = 0;
        int interp_count = 0;

        for (int i = 1; i <= 24; ++i) {
            const int64_t prev_pts = (i - 1) * delta_in;
            const int64_t curr_pts = i * delta_in;
            while (next_out < curr_pts) {
                const float alpha = static_cast<float>(next_out - prev_pts) / static_cast<float>(curr_pts - prev_pts);
                if (alpha <= 0.0001f || alpha >= 0.9999f) {
                    ++zero_copy_count;
                } else {
                    ++interp_count;
                    RTX_CHECK(std::abs(alpha - 0.5f) < 0.001f);
                }
                ++frames_emitted;
                next_out += delta_out;
            }
        }
        RTX_CHECK_EQ(frames_emitted, 48);
        RTX_CHECK_EQ(zero_copy_count, 24);
        RTX_CHECK_EQ(interp_count, 24);
    }

    // 2. 24 fps -> 60 fps (2.5x)
    {
        const int64_t delta_in = kFlicks / 24;   // 29,400,000
        const int64_t delta_out = kFlicks / 60;  // 11,760,000
        RTX_CHECK_EQ(delta_in, 29400000LL);
        RTX_CHECK_EQ(delta_out, 11760000LL);

        int64_t next_out = 0;
        int frames_emitted = 0;
        std::vector<float> alphas;

        for (int i = 1; i <= 24; ++i) {
            const int64_t prev_pts = (i - 1) * delta_in;
            const int64_t curr_pts = i * delta_in;
            while (next_out < curr_pts) {
                const float alpha = static_cast<float>(next_out - prev_pts) / static_cast<float>(curr_pts - prev_pts);
                alphas.push_back(alpha);
                ++frames_emitted;
                next_out += delta_out;
            }
        }
        RTX_CHECK_EQ(frames_emitted, 60);
        // Verify periodic alpha pattern: 0.0, 0.4, 0.8, 0.2, 0.6
        RTX_CHECK(std::abs(alphas[0] - 0.0f) < 0.001f);
        RTX_CHECK(std::abs(alphas[1] - 0.4f) < 0.001f);
        RTX_CHECK(std::abs(alphas[2] - 0.8f) < 0.001f);
        RTX_CHECK(std::abs(alphas[3] - 0.2f) < 0.001f);
        RTX_CHECK(std::abs(alphas[4] - 0.6f) < 0.001f);
        RTX_CHECK(std::abs(alphas[5] - 0.0f) < 0.001f);
    }
}
