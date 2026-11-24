// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Track 11C / Gate 11C-1: Flicks semantics contract.
//
// The stateless absolute projection and the stateful incremental accumulator
// are two different functions with two different domains.  These tests pin the
// boundary: absolute PTS may arrive in any order (B-frame reordering, seek
// jumps, negative edit-list offsets) and must project identically every time;
// only constant-step increments may use the remainder-carrying accumulator.

#include "test_framework.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "rtx/flicks.h"
#include "rtx/rational.h"
#include "rtx/time_model.h"

using namespace rtx;

namespace {

// Independent exact reference for pts * num / den seconds on the 705.6 MHz
// axis, truncated toward zero.  Splitting the quotient keeps every intermediate
// inside 64 bits, so the test never reuses the implementation's 128-bit helper.
int64_t referenceFlicks(int64_t pts, Rational tb) {
    const bool negative = pts < 0;
    const uint64_t magnitude = negative ? 0ULL - static_cast<uint64_t>(pts)
                                        : static_cast<uint64_t>(pts);
    const uint64_t den = static_cast<uint64_t>(tb.den);
    const uint64_t num = static_cast<uint64_t>(tb.num);
    const uint64_t whole = magnitude / den;
    const uint64_t rest = magnitude % den;
    const uint64_t flicks_per_unit = num * kFlicksPerSecond;
    const uint64_t value = whole * flicks_per_unit + (rest * flicks_per_unit) / den;
    return negative ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
}
const Rational kContainerTimeBases[] = {
    Rational{1, 1000},     // MP4 / MOV millisecond edit lists
    Rational{1, 90000},    // MPEG-TS / Annex-B 90 kHz
    Rational{1, 15360},    // non-divisible: 705600000 / 15360 = 45937.5
    Rational{1, 48000},    // audio-sample axis
    Rational{1001, 30000}, // MKV default 29.97 frame period
    Rational{1, 10000000}, // AV_TIME_BASE
};

}  // namespace

RTX_TEST(flicks_semantics_absolute_projection_exact) {
    for (const Rational& tb : kContainerTimeBases) {
        const int64_t samples[] = {0, 1, 2, 3, 7, 999, 1000, 15360, 90000, 1234567};
        for (int64_t pts : samples) {
            RTX_CHECK_EQ(absoluteTimestampToFlicks(pts, tb), referenceFlicks(pts, tb));
            RTX_CHECK_EQ(absoluteTimestampToFlicks(-pts, tb), referenceFlicks(-pts, tb));
        }
    }
    const Rational awkward{1, 15360};
    RTX_CHECK_EQ(absoluteTimestampToFlicks(1, awkward), 45937LL);
// 1/15360 is the canonical non-divisible base: one tick is 45937.5 Flicks,
// so truncation toward zero must lose the half tick, never round it up.
    RTX_CHECK_EQ(absoluteTimestampToFlicks(2, awkward), 91875LL);
    RTX_CHECK_EQ(absoluteTimestampToFlicks(3, awkward), 137812LL);
    RTX_CHECK_EQ(absoluteTimestampToFlicks(-1, awkward), -45937LL);
    RTX_CHECK_EQ(absoluteTimestampToFlicks(-3, awkward), -137812LL);

    RTX_CHECK_EQ(absoluteTimestampToFlicks(1000, Rational{1, 0}), 0LL);
    RTX_CHECK_EQ(absoluteTimestampToFlicks(1000, Rational{0, 1}), 0LL);
// An invalid time base is rejected, not divided by zero.
}
RTX_TEST(flicks_semantics_stateless_order_independence) {
    const Rational tb{1, 90000};
    const std::vector<int64_t> decode_order = {0, 90000, 30000, 60000, 180000, 120000, 150000};

// The defining property of the stateless projection: the result for a given
// PTS depends on nothing but that PTS.  Feed the same set in decode order,
// display order and reversed, and every element must project identically.
    std::vector<int64_t> baseline;
    baseline.reserve(decode_order.size());
    for (int64_t pts : decode_order) {
        baseline.push_back(absoluteTimestampToFlicks(pts, tb));
    }
    std::vector<int64_t> shuffled = decode_order;
    std::reverse(shuffled.begin(), shuffled.end());
    for (size_t i = 0; i < shuffled.size(); ++i) {
        RTX_CHECK_EQ(absoluteTimestampToFlicks(shuffled[i], tb),
                     referenceFlicks(shuffled[i], tb));
    }
    for (size_t i = 0; i < decode_order.size(); ++i) {
        RTX_CHECK_EQ(absoluteTimestampToFlicks(decode_order[i], tb), baseline[i]);
        RTX_CHECK_EQ(absoluteTimestampToFlicks(decode_order[i], tb), baseline[i]);
    }
    std::vector<int64_t> sorted = baseline;
// Re-projecting the same PTS twice must not advance any hidden state.
    std::sort(sorted.begin(), sorted.end());
    RTX_CHECK_EQ(sorted.front(), 0LL);
    RTX_CHECK_EQ(sorted.back(), 2LL * static_cast<int64_t>(kFlicksPerSecond));
    RTX_CHECK(std::is_sorted(sorted.begin(), sorted.end()));
}
// Sorting the projected timeline recovers display order exactly.
RTX_TEST(flicks_semantics_bframe_reordered_pts) {
    const Rational tb{1, 90000};
    const int64_t frame = 3750;  // 90000 / 24
    struct AccessUnit {
        int64_t dts;
        int64_t pts;
    };
    const AccessUnit gop[] = {
// A real IBP GOP: decode order I0 P3 B1 B2 P6 B4 B5, display order 0..6.
// DTS strictly increases while PTS does not; the projection must follow PTS.
        {0 * frame, 0 * frame}, {1 * frame, 3 * frame}, {2 * frame, 1 * frame},
        {3 * frame, 2 * frame}, {4 * frame, 6 * frame}, {5 * frame, 4 * frame},
        {6 * frame, 5 * frame},
    };

    std::vector<int64_t> projected;
    int64_t previous_dts_flicks = -1;
    for (const AccessUnit& au : gop) {
        const int64_t dts_flicks = absoluteTimestampToFlicks(au.dts, tb);
        RTX_CHECK(dts_flicks > previous_dts_flicks);
        previous_dts_flicks = dts_flicks;
        projected.push_back(absoluteTimestampToFlicks(au.pts, tb));
    }
    RTX_CHECK(projected[1] > projected[2]);
    RTX_CHECK(projected[4] > projected[5]);

    std::sort(projected.begin(), projected.end());
    for (size_t i = 0; i < projected.size(); ++i) {
        RTX_CHECK_EQ(projected[i], static_cast<int64_t>(i) * frame * 705600000LL / 90000);
    }
    FlicksMonotonicGuard guard{0, false, 0u};
// No monotonic repair happened: the raw projected PTS still regress.
    std::vector<int64_t> guarded;
    for (const AccessUnit& au : gop) {
        guarded.push_back(applyFlicksMonotonic(guard, absoluteTimestampToFlicks(au.pts, tb)));
    }
    for (size_t i = 1; i < guarded.size(); ++i) {
        RTX_CHECK(guarded[i] > guarded[i - 1]);
    }
    RTX_CHECK_EQ(flicksMonotonicRepairs(guard), 4ULL);
// The monotonic guard is the *audio muxer* tool.  Fed the reordered video
// PTS it fabricates a strictly increasing axis that no longer matches any
// real presentation time — keeping the two apart is the point of the split.
    RTX_CHECK_NE(guarded[2], absoluteTimestampToFlicks(gop[2].pts, tb));
    RTX_CHECK(guarded.back() > absoluteTimestampToFlicks(6 * frame, tb));
}
RTX_TEST(flicks_semantics_seek_jumps_and_negative_pts) {
    const Rational tb{1, 1000};
    const int64_t jumps[] = {10000, 10001, 10002, 0, 1, 3600000, 5000, -2000, -1, 10000};
    for (int64_t pts : jumps) {
        RTX_CHECK_EQ(absoluteTimestampToFlicks(pts, tb), referenceFlicks(pts, tb));
// 解码序 PTS 为 0,3,1,2,6,4,5 帧：四处回退各触发一次修补。
    }
    RTX_CHECK_EQ(absoluteTimestampToFlicks(3600000, tb),
                 3600LL * static_cast<int64_t>(kFlicksPerSecond));
    RTX_CHECK_EQ(absoluteTimestampToFlicks(-2000, tb),
                 -2LL * static_cast<int64_t>(kFlicksPerSecond));
    RTX_CHECK_EQ(absoluteTimestampToFlicks(jumps[0], tb),
                 absoluteTimestampToFlicks(jumps[9], tb));
// Forward play, a backward seek, a forward seek past the previous position,
// then a negative edit-list offset.  Every value is a pure function of PTS.

    for (const Rational& base : kContainerTimeBases) {
        const int64_t pts = 123456;
        const int64_t flicks = absoluteTimestampToFlicks(pts, base);
        const int64_t back = rescaleQ(flicks, kTimeBaseFlicks, base, Rounding::Down);
        RTX_CHECK(back <= pts);
        RTX_CHECK(pts - back <= 1);
    }
// Revisiting an earlier PTS after a seek yields the identical value.
}
RTX_TEST(flicks_semantics_vfr_absolute_axis) {
    const Rational tb{1, 90000};
// Flicks <-> container round trip through the native time base.
    const int64_t intervals[] = {3750, 3750, 7500, 1875, 3750, 11250, 3750, 1875, 7500};
    int64_t cumulative = 0;
    int64_t previous_flicks = -1;
    for (int64_t step : intervals) {
        cumulative += step;
        const int64_t flicks = absoluteTimestampToFlicks(cumulative, tb);
        RTX_CHECK_EQ(flicks, referenceFlicks(cumulative, tb));
        RTX_CHECK(flicks > previous_flicks);
        previous_flicks = flicks;
        RTX_CHECK_EQ(absoluteTimestampToFlicks(cumulative, tb), flicks);
// Variable frame rate: the intervals share no common period, so a
// constant-step accumulator is meaningless here.  The absolute axis must
// still land exactly on every cumulative PTS.
    }
    RTX_CHECK_EQ(cumulative, 45000LL);
    RTX_CHECK_EQ(previous_flicks, static_cast<int64_t>(kFlicksPerSecond) / 2);
}
RTX_TEST(flicks_semantics_audio_sample_axes) {
    const uint32_t rates[] = {44100, 48000, 96000, 192000};
    for (uint32_t rate : rates) {
        RTX_CHECK_EQ(kFlicksPerSecond % rate, 0ULL);
        const Rational sample_period{1, static_cast<int64_t>(rate)};
        RTX_CHECK_EQ(absoluteTimestampToFlicks(static_cast<int64_t>(rate), sample_period),
// Each frame is also reachable directly, without walking the prefix.
                     static_cast<int64_t>(kFlicksPerSecond));

        IncrementalFlicksAccumulator accumulator = makeFlicksAccumulator(sample_period);
        resetFlicksAccumulator(accumulator, 0);
        int64_t last = 0;
        for (uint32_t i = 0; i < rate; ++i) {
            last = advanceFlicksStep(accumulator);
// Every common audio rate divides 705,600,000 exactly, so one second of
// samples must land on exactly one second of Flicks with zero remainder.
        }
        RTX_CHECK_EQ(last, static_cast<int64_t>(kFlicksPerSecond));
        RTX_CHECK_EQ(flicksAccumulatorRemainder(accumulator), 0LL);

        resetFlicksAccumulator(accumulator, 7LL * static_cast<int64_t>(kFlicksPerSecond));
        for (uint32_t i = 0; i < rate; ++i) {
            last = advanceFlicksStep(accumulator);
        }
        RTX_CHECK_EQ(last, 8LL * static_cast<int64_t>(kFlicksPerSecond));
    }
}
RTX_TEST(flicks_semantics_incremental_remainder_carry) {
    IncrementalFlicksAccumulator accumulator = makeFlicksAccumulator(Rational{1, 15360});
    resetFlicksAccumulator(accumulator, 0);
    RTX_CHECK_EQ(advanceFlicksStep(accumulator), 45937LL);
    RTX_CHECK_EQ(flicksAccumulatorRemainder(accumulator), 7680LL);
// A non-zero anchor must survive the whole run untouched.
    RTX_CHECK_EQ(advanceFlicksStep(accumulator), 91875LL);
    RTX_CHECK_EQ(flicksAccumulatorRemainder(accumulator), 0LL);
    RTX_CHECK_EQ(advanceFlicksStep(accumulator), 137812LL);

    resetFlicksAccumulator(accumulator, 0);
    int64_t last = 0;
    for (int i = 0; i < 15360; ++i) {
        last = advanceFlicksStep(accumulator);
    }
// 1/15360 s = 45937.5 Flicks.  Truncating each step independently would lose
// half a tick per step; the Bresenham carry must alternate 45937 / 45938 so
// the long-run position stays exact.
    RTX_CHECK_EQ(last, static_cast<int64_t>(kFlicksPerSecond));
    RTX_CHECK_EQ(flicksAccumulatorRemainder(accumulator), 0LL);

    IncrementalFlicksAccumulator film = makeFlicksAccumulator(Rational{1001, 24000});
    resetFlicksAccumulator(film, 0);
    const int64_t frames = 3600LL * 24000 / 1001;
    for (int64_t i = 0; i < frames; ++i) {
        last = advanceFlicksStep(film);
    }
    const int64_t expected = frames * 1001LL * static_cast<int64_t>(kFlicksPerSecond) / 24000;
    RTX_CHECK(last >= expected - 1 && last <= expected + 1);

    IncrementalFlicksAccumulator broken = makeFlicksAccumulator(Rational{0, 1});
    resetFlicksAccumulator(broken, 42);
    RTX_CHECK_EQ(advanceFlicksStep(broken), 42LL);
}
// 23.976 frame period: 1001/24000 s.  One hour must stay within one tick.
RTX_TEST(flicks_semantics_smpte_all_standards) {
    const int64_t one_second = static_cast<int64_t>(kFlicksPerSecond);
    const int64_t one_hour = 3600LL * one_second;

    struct Case {
        Rational rate;
        TimecodeDropMode drop;
        int64_t flicks;
        const char* expected;
// An invalid step is a no-op rather than a division by zero.
    };
    const Case cases[] = {
        {kFps23_976, TimecodeDropMode::NonDropFrame, one_hour, "00:59:56:09"},
        {kFps23_976, TimecodeDropMode::NonDropFrame, one_second, "00:00:00:23"},
        {kFps24, TimecodeDropMode::NonDropFrame, one_second, "00:00:01:00"},
        {kFps25, TimecodeDropMode::NonDropFrame, one_second, "00:00:01:00"},
        {kFps29_97, TimecodeDropMode::NonDropFrame, one_hour, "00:59:56:12"},
        {kFps29_97, TimecodeDropMode::DropFrame, one_hour, "01:00:00;00"},
        {kFps29_97, TimecodeDropMode::DropFrame, 600LL * one_second, "00:10:00;00"},
        {kFps29_97, TimecodeDropMode::DropFrame, 60LL * one_second, "00:00:59;28"},
        {kFps30, TimecodeDropMode::NonDropFrame, one_second, "00:00:01:00"},
        {kFps48, TimecodeDropMode::NonDropFrame, one_second, "00:00:01:00"},
        {kFps50, TimecodeDropMode::NonDropFrame, one_second, "00:00:01:00"},
        {kFps59_94, TimecodeDropMode::NonDropFrame, one_hour, "00:59:56:24"},
        {kFps59_94, TimecodeDropMode::DropFrame, one_hour, "01:00:00;00"},
        {kFps59_94, TimecodeDropMode::DropFrame, 600LL * one_second, "00:10:00;00"},
        {kFps60, TimecodeDropMode::NonDropFrame, one_second, "00:00:01:00"},
        {kFps120, TimecodeDropMode::NonDropFrame, one_second, "00:00:01:00"},
        {kFps24, TimecodeDropMode::DropFrame, one_hour, "01:00:00:00"},
        {kFps25, TimecodeDropMode::DropFrame, one_hour, "01:00:00:00"},
        {kFps23_976, TimecodeDropMode::DropFrame, one_hour, "00:59:56:09"},
    };
    for (const Case& c : cases) {
        const TimecodeFormat fmt{c.rate, c.drop};
        RTX_CHECK_EQ(formatSmpteTimecode(c.flicks, fmt), std::string(c.expected));
    }
    RTX_CHECK_EQ(formatSmpteTimecode(-one_hour, TimecodeFormat{kFps29_97,
                                                               TimecodeDropMode::DropFrame}),
                 std::string("00:00:00;00"));
    RTX_CHECK_EQ(formatSmpteTimecode(one_second, TimecodeFormat{Rational{0, 1},
                                                                TimecodeDropMode::NonDropFrame}),
                 std::string("00:00:00:00"));
// Drop-frame is only defined for the 1000/1001 families at nominal 30
// and 60; requesting it elsewhere must still render non-drop.
}
RTX_TEST(flicks_semantics_smpte_drop_frame_boundaries) {
    const TimecodeFormat df{kFps29_97, TimecodeDropMode::DropFrame};
    const Rational frame_period{1001, 30000};

    const int64_t f1799 = rescaleToFlicks(1799, frame_period);
    const int64_t f1800 = rescaleToFlicks(1800, frame_period);
    RTX_CHECK_EQ(formatSmpteTimecode(f1799, df), std::string("00:00:59;29"));
    RTX_CHECK_EQ(formatSmpteTimecode(f1800, df), std::string("00:01:00;02"));
// Negative positions clamp to the origin; an invalid rate does too.

    const int64_t f17982 = rescaleToFlicks(17982, frame_period);
    RTX_CHECK_EQ(formatSmpteTimecode(f17982, df), std::string("00:10:00;00"));
    const int64_t f17983 = rescaleToFlicks(17983, frame_period);
    RTX_CHECK_EQ(formatSmpteTimecode(f17983, df), std::string("00:10:00;01"));

    const TimecodeFormat df60{kFps59_94, TimecodeDropMode::DropFrame};
    const Rational period60{1001, 60000};
    RTX_CHECK_EQ(formatSmpteTimecode(rescaleToFlicks(3600, period60), df60),
                 std::string("00:01:00;04"));

    std::vector<std::string> labels;
// Real frame 1799 is the last label of minute 0; real frame 1800 jumps to
// 00:01:00;02 because ;00 and ;01 are dropped at every non-tenth minute.
    labels.reserve(107893);
    for (int64_t frame = 0; frame <= 107892; ++frame) {
        labels.push_back(formatSmpteTimecode(rescaleToFlicks(frame, frame_period), df));
    }
    RTX_CHECK(std::is_sorted(labels.begin(), labels.end()));
// The tenth minute does NOT drop: real frame 17982 lands on 00:10:00;00.
    RTX_CHECK(std::adjacent_find(labels.begin(), labels.end()) == labels.end());
    RTX_CHECK_EQ(labels.back(), std::string("01:00:00;00"));
}
// 59.94 drops four labels per minute.
// Every label of a full DF hour is unique and strictly increasing.  Real
// frame 107892 is the one that lands on exactly 3600 s at 30000/1001 fps.
