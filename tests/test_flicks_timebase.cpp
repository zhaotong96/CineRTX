// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Flicks timeline contract (W7-2).

#include "test_framework.h"
#include "rtx/time_model.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "rtx/rational.h"

using namespace rtx;

RTX_TEST(flicks_exact_division) {
// 1 flick = 1 / 705,600,000 s.  Every frame period and every audio sample
// period below must divide it with a zero remainder, which is the property
// the whole drift budget rests on.
    RTX_CHECK_EQ(kFlicksPerSecond, static_cast<uint64_t>(706305600000ULL));

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
    constexpr int64_t kHours = 24;
    constexpr uint64_t kSecondsPerHour = 3600;
// 24 hours of 29.97 fps video: 2,588,860 frames.  The remainder-carrying
// rescale must accumulate exactly zero error, so the residual stays far
// below the 1.42 ns budget.
    const Rational time_base{1001, 30000};  // MKV-style 29.97 time base
    const uint64_t frames = kHours * kSecondsPerHour * 30000u / 1001u;

    FlicksDriftCompensator drift = 0;
    int64_t last = 0;
    for (uint64_t index = 0; index < frames; ++index) {
        const int64_t pts = static_cast<int64_t>(index);
        last = rescaleToFlicksAccum(drift, pts, static_cast<uint32_t>(time_base.num),
                                     static_cast<uint32_t>(time_base.den));
    }
    const uint64_t expected = (frames - 1) * ((kFlicksPerSecond * 1001ull) / 30000ull);
    const int64_t difference = last - static_cast<int64_t>(expected);
    RTX_CHECK(difference >= -1 && difference <= 1);
// Exact expectation in 128-bit terms: (frames-1) * 1001 / 30000 seconds.

    const double drift_ns = static_cast<double>(difference) * (1.0e9 / static_cast<double>(kFlicksPerSecond));
    RTX_CHECK(drift_ns <= 1.42);
}
// 1 flick = 1.417 ns; a one-flick residual is the analytic floor.
RTX_TEST(flicks_negative_and_zero) {
    FlicksDriftCompensator drift = 0;
    RTX_CHECK_EQ(rescaleToFlicksAccum(drift, 0, 1, 1000), static_cast<int64_t>(0));
    RTX_CHECK_EQ(rescaleToFlicksAccum(drift, 1000, 1, 1000), static_cast<int64_t>(kFlicksPerSecond));
    RTX_CHECK_EQ(rescaleToFlicksAccum(drift, -1000, 1, 1000),
                 -static_cast<int64_t>(kFlicksPerSecond));
    RTX_CHECK_EQ(rescaleToFlicksAccum(drift, 1000, 1, 0), static_cast<int64_t>(0));

    RTX_CHECK_EQ(rescaleToFlicks(1000, Rational{1, 1000}),
                 static_cast<int64_t>(kFlicksPerSecond));
// A zero denominator is rejected rather than dividing by zero.
    RTX_CHECK_EQ(rescaleToFlicks(-1000, Rational{1, 1000}),
                 -static_cast<int64_t>(kFlicksPerSecond));
// The stateless helper must agree with the compensator on exact multiples.
    RTX_CHECK_EQ(rescaleToFlicks(0, Rational{1, 90000}), static_cast<int64_t>(0));
    RTX_CHECK_EQ(rescaleToFlicks(100, Rational{1, 0}), static_cast<int64_t>(0));
}
RTX_TEST(flicks_monotonic_guard) {
    FlicksMonotonicGuard guard{0, false, 0u};
// An invalid time base yields 0 instead of trapping.
    RTX_CHECK_EQ(applyFlicksMonotonic(guard, 100), static_cast<int64_t>(100));
    RTX_CHECK_EQ(applyFlicksMonotonic(guard, 200), static_cast<int64_t>(200));
    RTX_CHECK_EQ(applyFlicksMonotonic(guard, 150), static_cast<int64_t>(201));
    RTX_CHECK_EQ(flicksMonotonicRepairs(guard), static_cast<uint64_t>(1));
    RTX_CHECK_EQ(applyFlicksMonotonic(guard, 201), static_cast<int64_t>(202));
    RTX_CHECK_EQ(flicksMonotonicRepairs(guard), static_cast<uint64_t>(2));
    RTX_CHECK(flicksMonotonicPrimed(guard));
// A regression is repaired to last + 1, one flick (1.4 ns) later.
    RTX_CHECK_EQ(flicksMonotonicLast(guard), static_cast<int64_t>(202));

// An exact duplicate is also a regression: the timeline must be strictly
// increasing, not merely non-decreasing.
    resetFlicksMonotonic(guard);
    RTX_CHECK(!flicksMonotonicPrimed(guard));
    RTX_CHECK_EQ(applyFlicksMonotonic(guard, -5), static_cast<int64_t>(-5));
    RTX_CHECK_EQ(flicksMonotonicRepairs(guard), static_cast<uint64_t>(0));
}
RTX_TEST(flicks_fruc_sliding_window_math) {
    constexpr int64_t kFlicks = static_cast<int64_t>(kFlicksPerSecond);

    {
        const int64_t delta_in = kFlicks / 24;   // 29,429,400,000
        const int64_t delta_out = kFlicks / 48;  // 14,714,700,000
        RTX_CHECK_EQ(delta_in, 29429400000LL);
        RTX_CHECK_EQ(delta_out, 14714700000LL);

// 1. 24 fps -> 48 fps (2x)
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
    {
        const int64_t delta_in = kFlicks / 24;   // 29,429,400,000
        const int64_t delta_out = kFlicks / 60;  // 11,771,760,000
        RTX_CHECK_EQ(delta_in, 29429400000LL);
        RTX_CHECK_EQ(delta_out, 11771760000LL);

        int64_t next_out = 0;
// 2. 24 fps -> 60 fps (2.5x)
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
        RTX_CHECK(std::abs(alphas[0] - 0.0f) < 0.001f);
        RTX_CHECK(std::abs(alphas[1] - 0.4f) < 0.001f);
        RTX_CHECK(std::abs(alphas[2] - 0.8f) < 0.001f);
        RTX_CHECK(std::abs(alphas[3] - 0.2f) < 0.001f);
        RTX_CHECK(std::abs(alphas[4] - 0.6f) < 0.001f);
        RTX_CHECK(std::abs(alphas[5] - 0.0f) < 0.001f);
    }
// Verify periodic alpha pattern: 0.0, 0.4, 0.8, 0.2, 0.6
}
RTX_TEST(rational_normalization) {
    RTX_CHECK_EQ((Rational{2, 4}.num), 1);
    RTX_CHECK_EQ((Rational{2, 4}.den), 2);
    RTX_CHECK_EQ((Rational{-2, -4}.num), 1);
    RTX_CHECK_EQ((Rational{-2, -4}.den), 2);
    RTX_CHECK_EQ((Rational{2, -4}.num), -1);
    RTX_CHECK_EQ((Rational{2, -4}.den), 2);
    RTX_CHECK_EQ((Rational{0, 5}.num), 0);
// Rational exact arithmetic contract (merged from test_rational.cpp)
    RTX_CHECK_EQ((Rational{0, 5}.den), 1);
    RTX_CHECK((Rational{1, 1}.valid()));
    RTX_CHECK((!Rational{1, 0}.valid()));
}
RTX_TEST(rational_parse_and_format) {
    const Result<Rational> exact = Rational::parse("24000/1001");
    RTX_REQUIRE(exact.has_value());
    RTX_CHECK_EQ(exact.value().num, 24000);
    RTX_CHECK_EQ(exact.value().den, 1001);
    RTX_CHECK_EQ(exact.value().toString(), std::string("24000/1001"));

    const Result<Rational> decimal = Rational::parse("23.976");
    RTX_REQUIRE(decimal.has_value());
    RTX_CHECK_NEAR(decimal.value().toDouble(), 23.976, 1e-9);

    const Result<Rational> integral = Rational::parse("30");
    RTX_REQUIRE(integral.has_value());
    RTX_CHECK_EQ(integral.value().toString(), std::string("30"));

    RTX_CHECK(!Rational::parse("").has_value());
    RTX_CHECK(!Rational::parse("1/0").has_value());
    RTX_CHECK(!Rational::parse("hello").has_value());
    RTX_CHECK(!Rational::parse("-23.976").has_value());
    RTX_CHECK_EQ(Rational::parse("1/0").error().code, ErrorCode::InvalidArgument);
}
RTX_TEST(rational_arithmetic) {
    RTX_CHECK((Rational{1, 2} == Rational{2, 4}));
    RTX_CHECK((Rational{1, 2} + Rational{1, 3} == Rational{5, 6}));
    RTX_CHECK((Rational{1, 2} - Rational{1, 3} == Rational{1, 6}));
    RTX_CHECK((Rational{2, 3} * Rational{3, 4} == Rational{1, 2}));
    RTX_CHECK((Rational{2, 3} / Rational{4, 5} == Rational{5, 6}));
    RTX_CHECK((Rational{1, 2}.inverse() == Rational{2, 1}));
}
RTX_TEST(rational_from_double) {
    const Rational fps = Rational::fromDouble(29.97, 100000);
    RTX_CHECK(fps.valid());
    RTX_CHECK_NEAR(fps.toDouble(), 29.97, 1e-6);
    RTX_CHECK_EQ(Rational::fromDouble(-0.5, 100).num, -1);
    RTX_CHECK_EQ(Rational::fromDouble(-0.5, 100).den, 2);
    RTX_CHECK_EQ(Rational::fromDouble(std::numeric_limits<double>::infinity(), 100).den, 1);
}
RTX_TEST(rational_rescale_rounding) {
    const Rational thirds{1, 3};
    const Rational halves{1, 2};
    RTX_CHECK_EQ(rescaleQ(1, thirds, halves, Rounding::Nearest), 1);
    RTX_CHECK_EQ(rescaleQ(1, thirds, halves, Rounding::Down), 0);
    RTX_CHECK_EQ(rescaleQ(1, thirds, halves, Rounding::Up), 1);
    RTX_CHECK_EQ(rescaleQ(-1, thirds, halves, Rounding::Nearest), -1);
    RTX_CHECK_EQ(rescaleQ(-1, thirds, halves, Rounding::Down), -1);
    RTX_CHECK_EQ(rescaleQ(-1, thirds, halves, Rounding::Up), 0);

    RTX_CHECK_EQ(rescaleQ(90000, kTimeBase90k, kTimeBaseMilliseconds), 1000);
    RTX_CHECK_EQ(rescaleQ(24, Rational{24, 1}.inverse(), kTimeBase90k), 90000);
    RTX_CHECK_EQ(rescaleQ(1, kTimeBase90k, kTimeBase90k), 1);
}
RTX_TEST(rational_exact_comparison_and_equivalence) {
    Rational half1;
    half1.num = 1;
    half1.den = 2;
    Rational half2;
    half2.num = 2;
    half2.den = 4;
    Rational half3;
    half3.num = 50;
    half3.den = 100;

    RTX_CHECK(half1 == half2);
    RTX_CHECK(half2 == half3);
    RTX_CHECK(half1 == half3);
    RTX_CHECK_EQ((half1 <=> half2), std::strong_ordering::equal);
// 1. Un-normalized representation mathematical equivalence
    RTX_CHECK_EQ((half2 <=> half3), std::strong_ordering::equal);
    RTX_CHECK_EQ((half1 <=> half3), std::strong_ordering::equal);

    constexpr int64_t kM = std::numeric_limits<int64_t>::max() / 4;
    Rational r1;
    r1.num = kM;
    r1.den = kM - 1;
    Rational r2;
    r2.num = kM - 1;
    r2.den = kM - 2;

    RTX_CHECK(r1 < r2);
    RTX_CHECK(r2 > r1);
    RTX_CHECK(r1 != r2);
    RTX_CHECK_EQ((r1 <=> r2), std::strong_ordering::less);
    RTX_CHECK_EQ((r2 <=> r1), std::strong_ordering::greater);

// 2. Strict ordering without float precision loss on 64-bit boundaries
// In IEEE 754 double, both would be rounded to 1.0, failing float <=> comparison.
    Rational neg1;
    neg1.num = -1;
    neg1.den = 3;
    Rational neg2;
    neg2.num = 1;
    neg2.den = -3;
    RTX_CHECK(neg1 == neg2);
    RTX_CHECK_EQ((neg1 <=> neg2), std::strong_ordering::equal);

    Rational neg_smaller;
    neg_smaller.num = -1;
    neg_smaller.den = 2;  // -0.5 < -0.333
    RTX_CHECK(neg_smaller < neg1);
    RTX_CHECK_EQ((neg_smaller <=> neg1), std::strong_ordering::less);
// 3. Negative fractions & negative denominators

    Rational zero1{0, 1};
    Rational zero2{0, 99};
    RTX_CHECK(zero1 == zero2);
    RTX_CHECK_EQ((zero1 <=> zero2), std::strong_ordering::equal);
    RTX_CHECK(zero1 < r1);
    RTX_CHECK(zero1 > neg1);
}
// 4. Zero and identity

RTX_TEST(ntsc_1001_24h_zero_drift) {
    // Phase 13 Wave 1 (Gate W1-4): 24h continuous NTSC 23.976 accumulation has zero drift.
    constexpr int64_t kFrames24h = 2071526LL;
    int64_t timeline = 0;
    for (int64_t f = 0; f < kFrames24h; ++f) {
        timeline += rtx::kFlicksNtsc23976;
    }
    RTX_CHECK_EQ(timeline, kFrames24h * rtx::kFlicksNtsc23976);
    RTX_CHECK(rtx::isZeroRemainderFlicks(timeline, 24000, 1001));
    RTX_CHECK(rtx::isZeroRemainderFlicks(rtx::kFlicksNtsc2997, 30000, 1001));
    RTX_CHECK(rtx::isZeroRemainderFlicks(rtx::kFlicksNtsc5994, 60000, 1001));
}
