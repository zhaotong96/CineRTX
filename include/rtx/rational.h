// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Exact rational time bases and timestamp mapping.  All on-wire timestamps are
// integers; floating point is used only at the configuration boundary.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "rtx/status.h"

namespace rtx {

struct Rational {
    int64_t num = 0;
    int64_t den = 1;

    Rational() = default;
    Rational(int64_t numerator, int64_t denominator) : num(numerator), den(denominator) {
        normalize();
    }

    static Rational fromDouble(double value, int64_t max_denominator = 1000000);
    static Result<Rational> parse(std::string_view text);

    double toDouble() const { return static_cast<double>(num) / static_cast<double>(den); }
    bool valid() const noexcept { return den > 0; }
    std::string toString() const;
    void normalize();

    Rational inverse() const { return Rational{den, num}; }
    Rational operator*(const Rational& other) const;
    Rational operator/(const Rational& other) const;
    Rational operator+(const Rational& other) const;
    Rational operator-(const Rational& other) const;
    bool operator==(const Rational& other) const { return num == other.num && den == other.den; }
    bool operator!=(const Rational& other) const { return !(*this == other); }
};

enum class Rounding {
    Nearest,
    Down,
    Up,
};

// Equivalent to av_rescale_q_rnd for the value ranges used by the engine.
// Uses a 128-bit intermediate where the compiler supports it and a long double
// path otherwise (Windows/MSVC).  The result is always representable in int64.
int64_t rescaleQ(int64_t timestamp,
                 Rational source_time_base,
                 Rational destination_time_base,
                 Rounding rounding = Rounding::Nearest);

// Maps presentation timestamps from a source time base to a destination time
// base while compensating the fractional remainder.  This is used for audio
// packet pass-through: the accumulated error never exceeds one stream tick,
// regardless of programme duration.
class DriftCompensator {
public:
    DriftCompensator(Rational source_time_base, Rational destination_time_base);

    int64_t map(int64_t source_timestamp);
    int64_t residualNumerator() const { return residual_num_; }
    void reset();

private:
    Rational source_;
    Rational destination_;
    int64_t residual_num_ = 0;
    int64_t residual_den_ = 1;
};

// Frame-rate conversion helper.  Given an input frame index and the number of
// output sub-frames assigned to that interval, produces the exact output PTS
// in the requested destination time base.
class FrameTimestampMapper {
public:
    FrameTimestampMapper(Rational input_frame_rate, Rational output_frame_rate,
                         Rational output_time_base);

    // Number of output frames generated for the interval following input frame
    // `input_index`.  For 24 -> 60 this alternates 2/3/2/3/2.
    int outputFramesForInterval(int64_t input_index) const;

    // PTS for the k-th output frame of the given input interval.
    int64_t outputPts(int64_t input_index, int k) const;

    // Total output frames for a programme of `input_frames`.
    int64_t totalOutputFrames(int64_t input_frames) const;

    Rational outputFrameRate() const { return output_fps_; }

private:
    Rational input_fps_;
    Rational output_fps_;
    Rational time_base_;
};

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
