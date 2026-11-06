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
    // 1. Un-normalized representation mathematical equivalence
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
    RTX_CHECK_EQ((half2 <=> half3), std::strong_ordering::equal);
    RTX_CHECK_EQ((half1 <=> half3), std::strong_ordering::equal);

    // 2. Strict ordering without float precision loss on 64-bit boundaries
    // In IEEE 754 double, both would be rounded to 1.0, failing float <=> comparison.
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

    // 3. Negative fractions & negative denominators
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

    // 4. Zero and identity
    Rational zero1{0, 1};
    Rational zero2{0, 99};
    RTX_CHECK(zero1 == zero2);
    RTX_CHECK_EQ((zero1 <=> zero2), std::strong_ordering::equal);
    RTX_CHECK(zero1 < r1);
    RTX_CHECK(zero1 > neg1);
}
