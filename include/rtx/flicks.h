// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#pragma once

#include <compare>
#include <cstdint>
#include <tuple>

#include "rtx/rational.h"

namespace rtx {

// Flicks: the 705,600,000 Hz integer timeline (W7-2)
inline constexpr uint64_t kFlicksPerSecond = 705600000ULL;

// Exact 128-bit rescale of a container PTS into Flicks. No floating point.
int64_t rescaleToFlicks(int64_t stream_pts, Rational time_base);

// Phase 12 (Wave 12.2-C): standard value/tuple aliases + free functions replace passive wrappers.
using FlicksDriftCompensator = uint64_t;
int64_t rescaleToFlicksAccum(FlicksDriftCompensator& remainder, int64_t stream_pts,
                             uint32_t timebase_num, uint32_t timebase_den) noexcept;

// (last, primed, repairs)
using FlicksMonotonicGuard = std::tuple<int64_t, bool, uint64_t>;
int64_t applyFlicksMonotonic(FlicksMonotonicGuard& guard, int64_t candidate) noexcept;
inline void resetFlicksMonotonic(FlicksMonotonicGuard& guard) noexcept { guard = {0, false, 0u}; }
[[nodiscard]] inline bool flicksMonotonicPrimed(const FlicksMonotonicGuard& guard) noexcept {
    const auto& [last, primed, repairs] = guard;
    (void)last; (void)repairs;
    return primed;
}
[[nodiscard]] inline int64_t flicksMonotonicLast(const FlicksMonotonicGuard& guard) noexcept {
    const auto& [last, primed, repairs] = guard;
    (void)primed; (void)repairs;
    return last;
}
[[nodiscard]] inline uint64_t flicksMonotonicRepairs(const FlicksMonotonicGuard& guard) noexcept {
    const auto& [last, primed, repairs] = guard;
    (void)last; (void)primed;
    return repairs;
}

}  // namespace rtx
