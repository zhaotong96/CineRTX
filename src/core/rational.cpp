// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/rational.h"

#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

extern "C" {
#include <libavutil/mathematics.h>
}
#include "rtx/flicks.h"

namespace rtx {
namespace detail {

// first = high 64 bits, second = low 64 bits
using U128 = std::pair<uint64_t, uint64_t>;
// first = quotient (U128), second = remainder (uint64_t)
using Div128Result = std::pair<U128, uint64_t>;

[[nodiscard]] constexpr U128 multiply128(uint64_t a, uint64_t b) noexcept {
#if defined(_MSC_VER) && defined(_M_X64) && !defined(__clang__)
    if (!std::is_constant_evaluated()) {
        unsigned __int64 hi = 0;
        const uint64_t lo = _umul128(a, b, &hi);
        return {static_cast<uint64_t>(hi), lo};
    }
#elif defined(__SIZEOF_INT128__)
    __extension__ using u128_t = unsigned __int128;
    const u128_t prod = static_cast<u128_t>(a) * static_cast<u128_t>(b);
    return {static_cast<uint64_t>(prod >> 64), static_cast<uint64_t>(prod)};
#endif
    const uint64_t a_lo = a & 0xFFFF'FFFFull;
    const uint64_t a_hi = a >> 32;
    const uint64_t b_lo = b & 0xFFFF'FFFFull;
    const uint64_t b_hi = b >> 32;

    const uint64_t p0 = a_lo * b_lo;
    const uint64_t p1 = a_lo * b_hi;
    const uint64_t p2 = a_hi * b_lo;
    const uint64_t p3 = a_hi * b_hi;

    const uint64_t mid1 = p1 + (p0 >> 32);
    const uint64_t mid2 = p2 + (mid1 & 0xFFFF'FFFFull);

    const uint64_t lo = (mid2 << 32) | (p0 & 0xFFFF'FFFFull);
    const uint64_t hi = p3 + (mid1 >> 32) + (mid2 >> 32);
    return {hi, lo};
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
        const U128 p1 = multiply128(static_cast<uint64_t>(n1), static_cast<uint64_t>(d2));
        const U128 p2 = multiply128(static_cast<uint64_t>(n2), static_cast<uint64_t>(d1));
        return p1 <=> p2;
    }
    const uint64_t u1 = 0ULL - static_cast<uint64_t>(n1);
    const uint64_t u2 = 0ULL - static_cast<uint64_t>(n2);
    const U128 p1 = multiply128(u1, static_cast<uint64_t>(d2));
    const U128 p2 = multiply128(u2, static_cast<uint64_t>(d1));
    return p2 <=> p1;
}

[[nodiscard]] constexpr Div128Result divide128(uint64_t high, uint64_t low, uint64_t divisor) noexcept {
    if (divisor == 0) {
        return {{0ull, 0ull}, 0ull};
    }
#if defined(__SIZEOF_INT128__)
    __extension__ using u128_t = unsigned __int128;
    const u128_t dividend = (static_cast<u128_t>(high) << 64) | static_cast<u128_t>(low);
    const u128_t q = dividend / divisor;
    const uint64_t r = static_cast<uint64_t>(dividend % divisor);
    return {{static_cast<uint64_t>(q >> 64), static_cast<uint64_t>(q)}, r};
#else
    uint64_t rem = high % divisor;
    const uint64_t q_hi = high / divisor;
    uint64_t q_lo = 0;
    for (int bit = 63; bit >= 0; --bit) {
        const uint64_t carry = rem >> 63;
        rem = (rem << 1) | ((low >> bit) & 1ull);
        if (carry != 0 || rem >= divisor) {
            rem -= divisor;
            q_lo |= (1ull << bit);
        }
    }
    return {{q_hi, q_lo}, rem};
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
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 200000
    const std::string dbl_text(text.data(), text.size());
    char* dbl_end = nullptr;
    errno = 0;
    dbl_val = std::strtod(dbl_text.c_str(), &dbl_end);
    if (dbl_end != dbl_text.c_str() + dbl_text.size() || dbl_end == dbl_text.c_str()) {
        return err(ErrorCode::InvalidArgument, "malformed rational", std::string(text));
    }
#else
    const auto res_dbl = std::from_chars(text.data(), text.data() + text.size(), dbl_val);
    if (res_dbl.ec != std::errc{} || res_dbl.ptr != text.data() + text.size()) {
        return err(ErrorCode::InvalidArgument, "malformed rational", std::string(text));
    }
#endif
// Zig's bundled libc++ (LLVM 18/19) has no floating-point std::from_chars
// overload.  std::strtod is the byte-identical fallback for this host
// path; MSVC keeps the locale-independent from_chars semantics.
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
    const auto [scaled_hi, scaled_lo] = detail::multiply128(magnitude, numerator * kFlicksPerSecond);
    const auto [quotient, remainder] = detail::divide128(scaled_hi, scaled_lo, denominator);
    (void)remainder;
    const auto [q_hi, q_lo] = quotient;
    const uint64_t q64 = (q_hi != 0) ? UINT64_MAX : q_lo;
    return negative ? -static_cast<int64_t>(q64)
                    : static_cast<int64_t>(q64);
}
int64_t rescaleToFlicksAccum(FlicksDriftCompensator& accumulated_remainder, int64_t stream_pts,
                             uint32_t timebase_num, uint32_t timebase_den) noexcept {
    if (timebase_num == 0 || timebase_den == 0) return 0;
    const bool negative = stream_pts < 0;
    const uint64_t magnitude = negative ? (0ULL - static_cast<uint64_t>(stream_pts))
                                        : static_cast<uint64_t>(stream_pts);
    const uint64_t factor = static_cast<uint64_t>(timebase_num) * kFlicksPerSecond;
    auto [scaled_hi, scaled_lo] = detail::multiply128(magnitude, factor);
    const uint64_t prev_lo = scaled_lo;
    scaled_lo += accumulated_remainder;
    if (scaled_lo < prev_lo) ++scaled_hi;
    const auto [quotient, next_remainder] = detail::divide128(scaled_hi, scaled_lo, timebase_den);
    const auto [q_hi, q_lo] = quotient;
    accumulated_remainder = next_remainder;
    const uint64_t q64 = (q_hi != 0) ? UINT64_MAX : q_lo;
    return negative ? -static_cast<int64_t>(q64) : static_cast<int64_t>(q64);
}
int64_t applyFlicksMonotonic(FlicksMonotonicGuard& guard, int64_t candidate) noexcept {
    auto& [last, primed, repairs] = guard;
    if (!primed) {
        primed = true;
        last = candidate;
        return last;
    }
    if (candidate <= last) {
        ++repairs;
        candidate = last + 1;
    }
    last = candidate;
    return last;
}
}  // namespace rtx
