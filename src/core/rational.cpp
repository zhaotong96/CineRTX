// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/rational.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <sstream>

namespace rtx {
namespace {

uint64_t absU64(int64_t value) noexcept {
    return value < 0 ? 0ull - static_cast<uint64_t>(value) : static_cast<uint64_t>(value);
}

int64_t gcd64(int64_t a, int64_t b) {
    uint64_t ua = absU64(a);
    uint64_t ub = absU64(b);
    while (ub != 0) {
        const uint64_t t = ua % ub;
        ua = ub;
        ub = t;
    }
    return ua == 0 ? 1 : static_cast<int64_t>(std::min<uint64_t>(ua, static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
}

#if defined(__SIZEOF_INT128__)
using Wide = __int128;
Wide toWide(int64_t value) { return static_cast<Wide>(value); }
#else
using Wide = long double;
Wide toWide(int64_t value) { return static_cast<Wide>(value); }
#endif

}  // namespace

void Rational::normalize() {
    if (den == 0) {
        // Keep the value invalid so that callers can reject it instead of
        // silently treating a meaningless time base as 1/1.
        return;
    }
    if (den < 0) {
        num = -num;
        den = -den;
    }
    const int64_t divisor = gcd64(num, den);
    num /= divisor;
    den /= divisor;
}

Rational Rational::fromDouble(double value, int64_t max_denominator) {
    if (!std::isfinite(value) || max_denominator <= 0) {
        return {};
    }
    const bool negative = value < 0;
    double x = negative ? -value : value;

    // Continued-fraction approximation with a bounded denominator.
    int64_t h0 = 0, h1 = 1;
    int64_t k0 = 1, k1 = 0;
    double remainder = x;
    for (int iteration = 0; iteration < 64; ++iteration) {
        const double integral = std::floor(remainder);
        if (integral > static_cast<double>(std::numeric_limits<int64_t>::max() / 4)) {
            break;
        }
        const int64_t a = static_cast<int64_t>(integral);
        const int64_t h2 = a * h1 + h0;
        const int64_t k2 = a * k1 + k0;
        if (k2 > max_denominator || k2 <= 0) {
            break;
        }
        h0 = h1;
        h1 = h2;
        k0 = k1;
        k1 = k2;
        const double fraction = remainder - integral;
        if (fraction < 1e-12) {
            break;
        }
        remainder = 1.0 / fraction;
    }
    if (k1 == 0) {
        return {};
    }
    Rational result{negative ? -h1 : h1, k1};
    result.normalize();
    return result;
}

Result<Rational> Rational::parse(std::string_view text) {
    if (text.empty()) {
        return Result<Rational>::fail(ErrorCode::InvalidArgument, "empty rational");
    }
    const size_t slash = text.find('/');
    if (slash != std::string_view::npos) {
        try {
            const int64_t num = std::stoll(std::string(text.substr(0, slash)));
            const int64_t den = std::stoll(std::string(text.substr(slash + 1)));
            if (den == 0) {
                return Result<Rational>::fail(ErrorCode::InvalidArgument,
                                              "rational denominator is zero");
            }
            return Result<Rational>::ok(Rational{num, den});
        } catch (const std::exception&) {
            return Result<Rational>::fail(ErrorCode::InvalidArgument, "malformed rational",
                                          std::string(text));
        }
    }
    try {
        const double value = std::stod(std::string(text));
        if (value <= 0.0) {
            return Result<Rational>::fail(ErrorCode::InvalidArgument,
                                          "rational must be positive");
        }
        return Result<Rational>::ok(Rational::fromDouble(value));
    } catch (const std::exception&) {
        return Result<Rational>::fail(ErrorCode::InvalidArgument, "malformed rational",
                                      std::string(text));
    }
}

std::string Rational::toString() const {
    std::ostringstream stream;
    if (den == 1) {
        stream << num;
    } else {
        stream << num << '/' << den;
    }
    return stream.str();
}

Rational Rational::operator*(const Rational& other) const {
    return Rational{num * other.num, den * other.den};
}

Rational Rational::operator/(const Rational& other) const {
    return Rational{num * other.den, den * other.num};
}

Rational Rational::operator+(const Rational& other) const {
    return Rational{num * other.den + other.num * den, den * other.den};
}

Rational Rational::operator-(const Rational& other) const {
    return Rational{num * other.den - other.num * den, den * other.den};
}

int64_t rescaleQ(int64_t timestamp, Rational source_time_base, Rational destination_time_base,
                 Rounding rounding) {
    if (!source_time_base.valid() || !destination_time_base.valid()) {
        return 0;
    }
    // timestamp * (src.num/src.den) / (dst.num/dst.den)
    const Wide numerator = toWide(timestamp) * toWide(source_time_base.num) *
                           toWide(destination_time_base.den);
    const Wide denominator = toWide(source_time_base.den) * toWide(destination_time_base.num);
    if (denominator == 0) {
        return 0;
    }
#if defined(__SIZEOF_INT128__)
    Wide quotient = numerator / denominator;
    const Wide remainder = numerator % denominator;
    if (rounding == Rounding::Nearest) {
        const Wide twice = remainder < 0 ? -remainder * 2 : remainder * 2;
        if (twice >= denominator) {
            quotient += (numerator >= 0 ? 1 : -1);
        }
    } else if (rounding == Rounding::Up && remainder != 0 && numerator >= 0) {
        ++quotient;
    } else if (rounding == Rounding::Down && remainder != 0 && numerator < 0) {
        --quotient;
    }
    return static_cast<int64_t>(quotient);
#else
    long double value = numerator / denominator;
    if (rounding == Rounding::Nearest) {
        value = value >= 0.0L ? std::floor(value + 0.5L) : std::ceil(value - 0.5L);
    } else if (rounding == Rounding::Down) {
        value = std::floor(value);
    } else {
        value = std::ceil(value);
    }
    return static_cast<int64_t>(value);
#endif
}

DriftCompensator::DriftCompensator(Rational source_time_base, Rational destination_time_base)
    : source_(source_time_base), destination_(destination_time_base) {
    if (!source_.valid() || !destination_.valid()) {
        source_ = Rational{1, 1};
        destination_ = Rational{1, 1};
    }
}

void DriftCompensator::reset() {
    residual_num_ = 0;
    residual_den_ = 1;
}

int64_t DriftCompensator::map(int64_t source_timestamp) {
    // result = ts * (src.num/src.den) / (dst.num/dst.den)
    //        = ts * src.num * dst.den / (src.den * dst.num)
    const int64_t denominator = source_.den * destination_.num;
    if (denominator == 0) {
        return source_timestamp;
    }
#if defined(__SIZEOF_INT128__)
    __int128 numerator = static_cast<__int128>(source_timestamp) * source_.num *
                         destination_.den;
    if (residual_den_ == denominator) {
        numerator += residual_num_;
    }
    __int128 quotient = numerator / denominator;
    int64_t remainder = static_cast<int64_t>(numerator % denominator);
    if (remainder < 0) {
        remainder += denominator;
        --quotient;
    }
    residual_num_ = remainder;
    residual_den_ = denominator;
    return static_cast<int64_t>(quotient);
#else
    const long double exact = static_cast<long double>(source_timestamp) * source_.num *
                              destination_.den / denominator;
    const int64_t integer = static_cast<int64_t>(std::floor(exact));
    const long double fraction = exact - static_cast<long double>(integer);
    residual_num_ = static_cast<int64_t>(fraction * 1000000000.0L);
    residual_den_ = 1000000000;
    return integer;
#endif
}

FrameTimestampMapper::FrameTimestampMapper(Rational input_frame_rate,
                                           Rational output_frame_rate,
                                           Rational output_time_base)
    : input_fps_(input_frame_rate),
      output_fps_(output_frame_rate),
      time_base_(output_time_base) {
    if (!input_fps_.valid()) {
        input_fps_ = Rational{30, 1};
    }
    if (!output_fps_.valid()) {
        output_fps_ = Rational{60, 1};
    }
    if (!time_base_.valid()) {
        time_base_ = kTimeBase90k;
    }
}

int FrameTimestampMapper::outputFramesForInterval(int64_t input_index) const {
    const auto count_at = [this](int64_t index) {
        // floor(index * output_fps / input_fps)
        return rescaleQ(index, input_fps_.inverse(), output_fps_.inverse(), Rounding::Down);
    };
    const int64_t before = count_at(input_index);
    const int64_t after = count_at(input_index + 1);
    return static_cast<int>(after - before);
}

int64_t FrameTimestampMapper::outputPts(int64_t input_index, int k) const {
    const int64_t before = rescaleQ(input_index, input_fps_.inverse(), output_fps_.inverse(),
                                    Rounding::Down);
    const int64_t output_index = before + k;
    return rescaleQ(output_index, output_fps_.inverse(), time_base_);
}

int64_t FrameTimestampMapper::totalOutputFrames(int64_t input_frames) const {
    return rescaleQ(std::max<int64_t>(0, input_frames), input_fps_.inverse(),
                    output_fps_.inverse(), Rounding::Down);
}

}  // namespace rtx
