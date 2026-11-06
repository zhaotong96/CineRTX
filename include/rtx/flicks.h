// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#pragma once

#include <compare>
#include <cstdint>

#include "rtx/rational.h"

namespace rtx {

// ---------------------------------------------------------------------------
// Flicks: the 705,600,000 Hz integer timeline (W7-2)
//
// 705,600,000 = 2^9 x 3^2 x 5^5 x 7^2. The factorisation is chosen so that the
// constant is an exact multiple of every common video frame rate (24, 23.976,
// 25, 29.97, 30, 48, 50, 59.94, 60, 120, including the 1000/1001 families) and
// every common audio sample rate (8 kHz .. 192 kHz).
// ---------------------------------------------------------------------------
inline constexpr uint64_t kFlicksPerSecond = 705600000ULL;

// Exact 128-bit rescale of a container PTS into Flicks. No floating point.
int64_t rescaleToFlicks(int64_t stream_pts, Rational time_base);

// Rescales a stream of PTS values while carrying the integer remainder of every
// division into the next conversion, so the accumulated error never exceeds one
// Flicks tick regardless of programme length.
class FlicksDriftCompensator {
public:
    int64_t rescaleToFlicks(int64_t stream_pts, uint32_t timebase_num, uint32_t timebase_den);
    uint64_t accumulatedRemainder() const noexcept { return accumulated_remainder_; }
    void reset() noexcept { accumulated_remainder_ = 0; }

private:
    uint64_t accumulated_remainder_ = 0;
};

// Enforces a strictly increasing Flicks timeline.
class FlicksMonotonicGuard {
public:
    int64_t apply(int64_t candidate) noexcept;
    int64_t last() const noexcept { return last_; }
    bool primed() const noexcept { return primed_; }
    uint64_t repairs() const noexcept { return repairs_; }
    void reset() noexcept {
        last_ = 0;
        primed_ = false;
        repairs_ = 0;
    }

private:
    int64_t last_ = 0;
    bool primed_ = false;
    uint64_t repairs_ = 0;
};

}  // namespace rtx
