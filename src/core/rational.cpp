// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/rational.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <sstream>

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>  // _umul128 / _udiv128
#endif

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

// ---------------------------------------------------------------------------
// Flicks timeline
// ---------------------------------------------------------------------------

namespace {

// 128-bit by 64-bit unsigned divide with remainder.  MSVC has no __int128, so
// the x64 intrinsic pair is used; both paths guard against a quotient that
// would not fit, which would otherwise trap (_udiv128 raises #DE).
struct Div128 {
    uint64_t quotient = 0;
    uint64_t remainder = 0;
};

Div128 divide128(uint64_t high, uint64_t low, uint64_t divisor) noexcept {
    Div128 result;
    if (divisor == 0) {
        return result;
    }
#if defined(_MSC_VER) && defined(_M_X64)
    if (high >= divisor) {
        // Saturated: the true quotient exceeds 2^64.  Only reachable for
        // absurd PTS values, so clamping keeps the timeline monotonic instead
        // of faulting.
        result.quotient = UINT64_MAX;
        return result;
    }
    unsigned __int64 remainder = 0;
    result.quotient = _udiv128(high, low, divisor, &remainder);
    result.remainder = remainder;
    return result;
#elif defined(__SIZEOF_INT128__)
    const unsigned __int128 total =
        (static_cast<unsigned __int128>(high) << 64) | static_cast<unsigned __int128>(low);
    const unsigned __int128 divisor128 = divisor;
    const unsigned __int128 quotient = total / divisor128;
    result.quotient = static_cast<uint64_t>(quotient > UINT64_MAX ? UINT64_MAX : quotient);
    result.remainder = static_cast<uint64_t>(total % divisor128);
    return result;
#else
    // Portable shift-subtract long division.  Only used by compilers without a
    // 128-bit type and without the MSVC intrinsics; the hot path is never here.
    if (high >= divisor) {
        result.quotient = UINT64_MAX;
        return result;
    }
    uint64_t quotient = 0;
    uint64_t remainder = high;
    for (int bit = 63; bit >= 0; --bit) {
        const uint64_t carry = remainder >> 63;
        remainder = (remainder << 1) | ((low >> bit) & 1ULL);
        quotient <<= 1;
        if (carry != 0 || remainder >= divisor) {
            remainder -= divisor;
            quotient |= 1ULL;
        }
    }
    result.quotient = quotient;
    result.remainder = remainder;
    return result;
#endif
}

struct Mul128 {
    uint64_t high = 0;
    uint64_t low = 0;
};

Mul128 multiply128(uint64_t a, uint64_t b) noexcept {
    Mul128 result;
#if defined(_MSC_VER) && defined(_M_X64)
    unsigned __int64 high = 0;
    result.low = _umul128(a, b, &high);
    result.high = high;
#elif defined(__SIZEOF_INT128__)
    const unsigned __int128 product =
        static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(b);
    result.low = static_cast<uint64_t>(product);
    result.high = static_cast<uint64_t>(product >> 64);
#else
    const uint64_t a_low = a & 0xFFFFFFFFULL;
    const uint64_t a_high = a >> 32;
    const uint64_t b_low = b & 0xFFFFFFFFULL;
    const uint64_t b_high = b >> 32;
    const uint64_t low_low = a_low * b_low;
    const uint64_t cross = a_high * b_low + a_low * b_high;
    result.low = low_low + (cross << 32);
    result.high = a_high * b_high + (cross >> 32) +
                  (result.low < low_low ? 1ULL : 0ULL);
#endif
    return result;
}

}  // namespace

int64_t rescaleToFlicks(int64_t stream_pts, Rational time_base) {
    if (time_base.num <= 0 || time_base.den <= 0) {
        return 0;
    }
    const uint64_t numerator = static_cast<uint64_t>(time_base.num);
    const uint64_t denominator = static_cast<uint64_t>(time_base.den);
    const bool negative = stream_pts < 0;
    const uint64_t magnitude = negative ? (0ULL - static_cast<uint64_t>(stream_pts))
                                        : static_cast<uint64_t>(stream_pts);
    const Mul128 scaled = multiply128(magnitude, numerator * kFlicksPerSecond);
    const Div128 divided = divide128(scaled.high, scaled.low, denominator);
    return negative ? -static_cast<int64_t>(divided.quotient)
                    : static_cast<int64_t>(divided.quotient);
}

int64_t FlicksDriftCompensator::rescaleToFlicks(int64_t stream_pts, uint32_t timebase_num,
                                                uint32_t timebase_den) {
    if (timebase_num == 0 || timebase_den == 0) {
        return 0;
    }
    const bool negative = stream_pts < 0;
    const uint64_t magnitude = negative ? (0ULL - static_cast<uint64_t>(stream_pts))
                                        : static_cast<uint64_t>(stream_pts);

    // u_pts * (timebase_num * kFlicksPerSecond) + accumulated_remainder, in 128
    // bits, then divided by timebase_den with the remainder carried forward.
    const uint64_t factor = static_cast<uint64_t>(timebase_num) * kFlicksPerSecond;
    Mul128 scaled = multiply128(magnitude, factor);
    const uint64_t low_before = scaled.low;
    scaled.low += accumulated_remainder_;
    if (scaled.low < low_before) {
        ++scaled.high;
    }
    const Div128 divided = divide128(scaled.high, scaled.low, timebase_den);
    accumulated_remainder_ = divided.remainder;
    return negative ? -static_cast<int64_t>(divided.quotient)
                    : static_cast<int64_t>(divided.quotient);
}

int64_t FlicksMonotonicGuard::apply(int64_t candidate) noexcept {
    if (!primed_) {
        primed_ = true;
        last_ = candidate;
        return last_;
    }
    if (candidate <= last_) {
        // Force strict monotonicity.  One Flicks tick is 1.4 ns, far below the
        // 1.0 ms Gate 3.4 A/V budget, so the repair is inaudible.
        ++repairs_;
        candidate = last_ + 1;
    }
    last_ = candidate;
    return last_;
}

}  // namespace rtx
