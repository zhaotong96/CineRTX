// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "rtx/rational.h"

using namespace rtx;

RTX_TEST(rational_normalization) {
    RTX_CHECK_EQ((Rational{2, 4}.num), 1);
    RTX_CHECK_EQ((Rational{2, 4}.den), 2);
    RTX_CHECK_EQ((Rational{-2, -4}.num), 1);
    RTX_CHECK_EQ((Rational{-2, -4}.den), 2);
    RTX_CHECK_EQ((Rational{2, -4}.num), -1);
    RTX_CHECK_EQ((Rational{2, -4}.den), 2);
    RTX_CHECK_EQ((Rational{0, 5}.num), 0);
    RTX_CHECK_EQ((Rational{0, 5}.den), 1);
    RTX_CHECK((Rational{1, 1}.valid()));
    RTX_CHECK((!Rational{1, 0}.valid()));
}

RTX_TEST(rational_parse_and_format) {
    const Result<Rational> exact = Rational::parse("24000/1001");
    RTX_REQUIRE(exact.ok());
    RTX_CHECK_EQ(exact.value().num, 24000);
    RTX_CHECK_EQ(exact.value().den, 1001);
    RTX_CHECK_EQ(exact.value().toString(), std::string("24000/1001"));

    const Result<Rational> decimal = Rational::parse("23.976");
    RTX_REQUIRE(decimal.ok());
    RTX_CHECK_NEAR(decimal.value().toDouble(), 23.976, 1e-9);

    const Result<Rational> integral = Rational::parse("30");
    RTX_REQUIRE(integral.ok());
    RTX_CHECK_EQ(integral.value().toString(), std::string("30"));

    RTX_CHECK(!Rational::parse("").ok());
    RTX_CHECK(!Rational::parse("1/0").ok());
    RTX_CHECK(!Rational::parse("hello").ok());
    RTX_CHECK(!Rational::parse("-23.976").ok());
    RTX_CHECK_EQ(Rational::parse("1/0").code(), ErrorCode::InvalidArgument);
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

RTX_TEST(rational_drift_compensation) {
    DriftCompensator compensator(Rational{1, 48000}, kTimeBase90k);
    int64_t last = 0;
    for (int i = 0; i <= 48000; ++i) {
        last = compensator.map(i);
    }
    RTX_CHECK_EQ(last, 90000);
    RTX_CHECK(compensator.residualNumerator() >= 0);
    compensator.reset();
    RTX_CHECK_EQ(compensator.residualNumerator(), 0);
    RTX_CHECK_EQ(compensator.map(0), 0);
}

RTX_TEST(rational_frame_mapper_24_to_60) {
    FrameTimestampMapper mapper(kFps24, kFps60, kTimeBase90k);
    const std::vector<int> expected = {2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3};
    int sum = 0;
    for (size_t i = 0; i < expected.size(); ++i) {
        const int count = mapper.outputFramesForInterval(static_cast<int64_t>(i));
        RTX_CHECK_EQ(count, expected[i]);
        sum += count;
    }
    RTX_CHECK_EQ(sum, 30);
    RTX_CHECK_EQ(mapper.totalOutputFrames(12), 30);
    RTX_CHECK_EQ(mapper.totalOutputFrames(1), 2);
    RTX_CHECK_EQ(mapper.totalOutputFrames(0), 0);
    RTX_CHECK_EQ(mapper.totalOutputFrames(-5), 0);

    RTX_CHECK_EQ(mapper.outputPts(0, 0), 0);
    RTX_CHECK_EQ(mapper.outputPts(0, 1), 1500);
    RTX_CHECK_EQ(mapper.outputPts(1, 0), 3000);
    RTX_CHECK_EQ(mapper.outputPts(1, 2), 6000);

    int64_t previous = -1;
    size_t outputs = 0;
    for (int64_t input = 0; input < 12; ++input) {
        const int count = mapper.outputFramesForInterval(input);
        for (int k = 0; k < count; ++k) {
            const int64_t pts = mapper.outputPts(input, k);
            RTX_CHECK(pts > previous);
            previous = pts;
            ++outputs;
        }
    }
    RTX_CHECK_EQ(outputs, static_cast<size_t>(30));
    RTX_CHECK_EQ(previous, 43500);
}

RTX_TEST(rational_frame_mapper_other_rates) {
    FrameTimestampMapper to_48(kFps24, kFps48, kTimeBase90k);
    RTX_CHECK_EQ(to_48.outputFramesForInterval(0), 2);
    RTX_CHECK_EQ(to_48.totalOutputFrames(12), 24);
    RTX_CHECK_EQ(to_48.outputPts(0, 1), 1875);  // 90000 / 48

    FrameTimestampMapper thirty_to_sixty(kFps30, kFps60, kTimeBase90k);
    RTX_CHECK_EQ(thirty_to_sixty.outputFramesForInterval(7), 2);
    RTX_CHECK_EQ(thirty_to_sixty.totalOutputFrames(30), 60);

    FrameTimestampMapper ntsc(kFps23_976, kFps59_94, kTimeBase90k);
    RTX_CHECK_EQ(ntsc.totalOutputFrames(24), 60);
    RTX_CHECK_EQ(ntsc.outputPts(0, 0), 0);
    RTX_CHECK(ntsc.outputPts(0, 1) > 1500);
    RTX_CHECK_EQ(ntsc.outputPts(0, 1), 1502);  // round(90000 * 1001 / 60000)
}

RTX_TEST(rational_invalid_frame_mapper_configuration) {
    // The mapper substitutes safe defaults for invalid rates so a malformed
    // probe cannot trigger a division by zero in the hot path.
    FrameTimestampMapper mapper(Rational{30, 0}, Rational{60, 0}, Rational{1, 0});
    RTX_CHECK(mapper.outputFrameRate().valid());
    RTX_CHECK_EQ(mapper.outputFramesForInterval(0), 2);
}
