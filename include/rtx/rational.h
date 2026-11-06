// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#pragma once

#include "rtx/status.h"

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace rtx {

struct Rational {
    int64_t num = 0;
    int64_t den = 1;

    constexpr Rational() noexcept = default;
    Rational(int64_t numerator, int64_t denominator);

    static Rational fromDouble(double value, int64_t max_denominator = 1000000);
    static Result<Rational> parse(std::string_view text);

    constexpr double toDouble() const noexcept {
        return den != 0 ? static_cast<double>(num) / static_cast<double>(den) : 0.0;
    }
    constexpr bool valid() const noexcept { return den > 0; }
    std::string toString() const;
    void normalize();

    Rational inverse() const { return Rational{den, num}; }
    Rational operator*(const Rational& other) const;
    Rational operator/(const Rational& other) const;
    Rational operator+(const Rational& other) const;
    Rational operator-(const Rational& other) const;

    std::strong_ordering operator<=>(const Rational& other) const noexcept;
    bool operator==(const Rational& other) const noexcept;
    bool operator!=(const Rational& other) const noexcept { return !(*this == other); }
};

enum class Rounding {
    Nearest,
    Down,
    Up,
};

int64_t rescaleQ(int64_t timestamp,
                 Rational source_time_base,
                 Rational destination_time_base,
                 Rounding rounding = Rounding::Nearest);

inline const Rational kTimeBase90k{1, 90000};
inline const Rational kTimeBaseMilliseconds{1, 1000};
inline const Rational kFps23_976{24000, 1001};
inline const Rational kFps24{24, 1};
inline const Rational kFps25{25, 1};
inline const Rational kFps29_97{30000, 1001};
inline const Rational kFps30{30, 1};
inline const Rational kFps47_952{48000, 1001};
inline const Rational kFps48{48, 1};
inline const Rational kFps50{50, 1};
inline const Rational kFps59_94{60000, 1001};
inline const Rational kFps60{60, 1};
inline const Rational kFps120{120, 1};

}  // namespace rtx

#include "rtx/flicks.h"
