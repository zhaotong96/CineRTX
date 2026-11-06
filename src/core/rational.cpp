// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/rational.h"

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>

extern "C" {
#include <libavutil/mathematics.h>
}

#include "rtx/flicks.h"

namespace rtx {
namespace detail {

struct Uint128 {
    uint64_t high = 0;
    uint64_t low = 0;

    constexpr auto operator<=>(const Uint128& other) const noexcept = default;
};

// Portable constexpr 64x64 -> 128 unsigned multiplication
constexpr inline Uint128 mul64(uint64_t a, uint64_t b) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    if (!std::is_constant_evaluated()) {
        unsigned __int64 high = 0;
        unsigned __int64 low = _umul128(a, b, &high);
        return Uint128{high, low};
    }
#endif
#if defined(__SIZEOF_INT128__)
    const unsigned __int128 p = static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(b);
    return Uint128{static_cast<uint64_t>(p >> 64), static_cast<uint64_t>(p)};
#else
    const uint64_t a_lo = a & 0xFFFFFFFFULL;
    const uint64_t a_hi = a >> 32;
    const uint64_t b_lo = b & 0xFFFFFFFFULL;
    const uint64_t b_hi = b >> 32;

    const uint64_t p0 = a_lo * b_lo;
    const uint64_t p1 = a_lo * b_hi;
    const uint64_t p2 = a_hi * b_lo;
    const uint64_t p3 = a_hi * b_hi;

    const uint64_t mid1 = p1 + (p0 >> 32);
    const uint64_t mid2 = p2 + (mid1 & 0xFFFFFFFFULL);

    const uint64_t low = (mid2 << 32) | (p0 & 0xFFFFFFFFULL);
    const uint64_t high = p3 + (mid1 >> 32) + (mid2 >> 32);

    return Uint128{high, low};
#endif
}

inline std::strong_ordering compareExact(int64_t n1, int64_t d1,
                                         int64_t n2, int64_t d2) noexcept {
    if (n1 == n2 && d1 == d2) {
        return std::strong_ordering::equal;
    }

    if (d1 < 0) {
        n1 = -n1;
        d1 = -d1;
    }
    if (d2 < 0) {
        n2 = -n2;
        d2 = -d2;
    }

    if (d1 == d2 && d1 > 0) {
        return n1 <=> n2;
    }

    if (d1 == 0 || d2 == 0) {
        if (d1 == 0 && d2 == 0) return n1 <=> n2;
        if (d1 == 0) return n1 > 0 ? std::strong_ordering::greater : std::strong_ordering::less;
        return n2 > 0 ? std::strong_ordering::less : std::strong_ordering::greater;
    }

    if (n1 == 0 && n2 == 0) return std::strong_ordering::equal;
    if (n1 <= 0 && n2 > 0) return std::strong_ordering::less;
    if (n1 >= 0 && n2 < 0) return std::strong_ordering::greater;
    if (n1 < 0 && n2 >= 0) return std::strong_ordering::less;
    if (n1 > 0 && n2 <= 0) return std::strong_ordering::greater;

    if (n1 > 0 && n2 > 0) {
        const Uint128 p1 = mul64(static_cast<uint64_t>(n1), static_cast<uint64_t>(d2));
        const Uint128 p2 = mul64(static_cast<uint64_t>(n2), static_cast<uint64_t>(d1));
        return p1 <=> p2;
    }

    const uint64_t u1 = 0ULL - static_cast<uint64_t>(n1);
    const uint64_t u2 = 0ULL - static_cast<uint64_t>(n2);
    const Uint128 p1 = mul64(u1, static_cast<uint64_t>(d2));
    const Uint128 p2 = mul64(u2, static_cast<uint64_t>(d1));
    return p2 <=> p1;
}

struct Div128 {
    uint64_t quotient = 0;
    uint64_t remainder = 0;
};

inline Div128 divide128(uint64_t high, uint64_t low, uint64_t divisor) noexcept {
    Div128 result;
    if (divisor == 0) {
        return result;
    }
#if defined(_MSC_VER) && defined(_M_X64)
    if (high >= divisor) {
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

}  // namespace detail

Rational::Rational(int64_t numerator, int64_t denominator) : num(numerator), den(denominator) {
    normalize();
}

void Rational::normalize() {
    if (den == 0) {
        return;
    }
    if (den < 0) {
        num = -num;
        den = -den;
    }
    const int64_t divisor = std::gcd(num, den);
    if (divisor > 0) {
        num /= divisor;
        den /= divisor;
    }
}

Rational Rational::fromDouble(double value, int64_t max_denominator) {
    if (!std::isfinite(value) || max_denominator <= 0) {
        return {};
    }
    const bool negative = value < 0;
    double x = negative ? -value : value;

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
        return err(ErrorCode::InvalidArgument, "empty rational");
    }
    const size_t slash = text.find('/');
    if (slash != std::string_view::npos) {
        int64_t n = 0;
        const auto res_num = std::from_chars(text.data(), text.data() + slash, n);
        if (res_num.ec != std::errc{} || res_num.ptr != text.data() + slash) {
            return err(ErrorCode::InvalidArgument, "malformed rational", std::string(text));
        }
        int64_t d = 0;
        const auto res_den = std::from_chars(text.data() + slash + 1, text.data() + text.size(), d);
        if (res_den.ec != std::errc{} || res_den.ptr != text.data() + text.size()) {
            return err(ErrorCode::InvalidArgument, "malformed rational", std::string(text));
        }
        if (d == 0) {
            return err(ErrorCode::InvalidArgument, "rational denominator is zero");
        }
        return Rational{n, d};
    }

    int64_t int_val = 0;
    const auto res_int = std::from_chars(text.data(), text.data() + text.size(), int_val);
    if (res_int.ec == std::errc{} && res_int.ptr == text.data() + text.size()) {
        if (int_val <= 0) {
            return err(ErrorCode::InvalidArgument, "rational must be positive");
        }
        return Rational{int_val, 1};
    }

    double dbl_val = 0.0;
    const auto res_dbl = std::from_chars(text.data(), text.data() + text.size(), dbl_val);
    if (res_dbl.ec != std::errc{} || res_dbl.ptr != text.data() + text.size()) {
        return err(ErrorCode::InvalidArgument, "malformed rational", std::string(text));
    }
    if (dbl_val <= 0.0) {
        return err(ErrorCode::InvalidArgument, "rational must be positive");
    }
    return Rational::fromDouble(dbl_val);
}

std::string Rational::toString() const {
    if (den == 1) {
        return std::format("{}", num);
    }
    return std::format("{}/{}", num, den);
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

std::strong_ordering Rational::operator<=>(const Rational& other) const noexcept {
    return detail::compareExact(num, den, other.num, other.den);
}

bool Rational::operator==(const Rational& other) const noexcept {
    if (num == other.num && den == other.den) {
        return true;
    }
    return (*this <=> other) == 0;
}

int64_t rescaleQ(int64_t timestamp, Rational source_time_base, Rational destination_time_base,
                 Rounding rounding) {
    if (!source_time_base.valid() || !destination_time_base.valid()) {
        return 0;
    }
    AVRounding rnd = AV_ROUND_NEAR_INF;
    switch (rounding) {
        case Rounding::Nearest: rnd = AV_ROUND_NEAR_INF; break;
        case Rounding::Down:    rnd = AV_ROUND_DOWN; break;
        case Rounding::Up:      rnd = AV_ROUND_UP; break;
    }
    const AVRational src{static_cast<int>(source_time_base.num), static_cast<int>(source_time_base.den)};
    const AVRational dst{static_cast<int>(destination_time_base.num), static_cast<int>(destination_time_base.den)};
    return av_rescale_q_rnd(timestamp, src, dst, rnd);
}

int64_t rescaleToFlicks(int64_t stream_pts, Rational time_base) {
    if (time_base.num <= 0 || time_base.den <= 0) {
        return 0;
    }
    const uint64_t numerator = static_cast<uint64_t>(time_base.num);
    const uint64_t denominator = static_cast<uint64_t>(time_base.den);
    const bool negative = stream_pts < 0;
    const uint64_t magnitude = negative ? (0ULL - static_cast<uint64_t>(stream_pts))
                                        : static_cast<uint64_t>(stream_pts);
    const detail::Uint128 scaled = detail::mul64(magnitude, numerator * kFlicksPerSecond);
    const detail::Div128 divided = detail::divide128(scaled.high, scaled.low, denominator);
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

    const uint64_t factor = static_cast<uint64_t>(timebase_num) * kFlicksPerSecond;
    detail::Uint128 scaled = detail::mul64(magnitude, factor);
    const uint64_t low_before = scaled.low;
    scaled.low += accumulated_remainder_;
    if (scaled.low < low_before) {
        ++scaled.high;
    }
    const detail::Div128 divided = detail::divide128(scaled.high, scaled.low, timebase_den);
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
        ++repairs_;
        candidate = last_ + 1;
    }
    last_ = candidate;
    return last_;
}

}  // namespace rtx
