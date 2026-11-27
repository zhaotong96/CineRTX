// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#pragma once

#include <compare>
#include <cstdint>
#include <numeric>
#include <tuple>

#include "rtx/rational.h"

namespace rtx {

// Flicks: the 706,305,600,000 Hz (1001-extended) integer timeline (W7-2)
inline constexpr uint64_t kBaseFlicksPerSecond = 705600000ULL;
inline constexpr uint64_t kFlicksPerSecond = kBaseFlicksPerSecond * 1001ULL;  // 706,305,600,000 Hz (exact NTSC 1001)

// Exact 1001-extended NTSC frame durations (zero remainder on 706,305,600,000 Hz)
inline constexpr int64_t kFlicksNtsc23976 = static_cast<int64_t>((kFlicksPerSecond / 24000ULL) * 1001ULL);
inline constexpr int64_t kFlicksNtsc2997  = static_cast<int64_t>((kFlicksPerSecond / 30000ULL) * 1001ULL);
inline constexpr int64_t kFlicksNtsc5994  = static_cast<int64_t>((kFlicksPerSecond / 60000ULL) * 1001ULL);

[[nodiscard]] constexpr bool isZeroRemainderFlicks(int64_t flicks, int64_t fps_num, int64_t fps_den) noexcept {
    if (fps_num <= 0 || fps_den <= 0 || flicks < 0) return false;
    const uint64_t g = std::gcd(static_cast<uint64_t>(fps_num), static_cast<uint64_t>(fps_den));
    const uint64_t num = static_cast<uint64_t>(fps_num) / g;
    const uint64_t den = static_cast<uint64_t>(fps_den) / g;
    if ((kFlicksPerSecond % num) != 0ULL) return false;
    const uint64_t step = (kFlicksPerSecond / num) * den;
    return step != 0ULL && (static_cast<uint64_t>(flicks) % step) == 0ULL;
}

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
