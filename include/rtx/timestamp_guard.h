// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 8 / Wave 12.2-C: VFR / damaged-timestamp monotonicity sanitizer.

#pragma once

#include <cstdint>
#include <limits>
#include <tuple>

namespace rtx {

// (last_dts, min_step, repairs)
using StreamTimestampGuard = std::tuple<int64_t, int64_t, int64_t>;

[[nodiscard]] constexpr StreamTimestampGuard makeTimestampGuard(int64_t min_step = 1) noexcept {
    return {std::numeric_limits<int64_t>::min(), min_step, 0};
}

constexpr void sanitizeTimestamp(StreamTimestampGuard& guard, int64_t& pts, int64_t& dts,
                                 int64_t fallback_step) noexcept {
    auto& [last_dts, min_step, repairs] = guard;
    if (dts == std::numeric_limits<int64_t>::min()) {
        dts = (last_dts == std::numeric_limits<int64_t>::min()) ? 0 : (last_dts + fallback_step);
    }
    if (last_dts != std::numeric_limits<int64_t>::min() && dts <= last_dts) {
        dts = last_dts + (fallback_step > min_step ? fallback_step : min_step);
        ++repairs;
    }
    if (pts == std::numeric_limits<int64_t>::min() || pts < dts) {
        pts = dts;
    }
    last_dts = dts;
}

constexpr void resetTimestampGuard(StreamTimestampGuard& guard) noexcept {
    auto& [last_dts, min_step, repairs] = guard;
    (void)min_step; (void)repairs;
    last_dts = std::numeric_limits<int64_t>::min();
}

[[nodiscard]] constexpr bool timestampGuardPrimed(const StreamTimestampGuard& guard) noexcept {
    const auto& [last_dts, min_step, repairs] = guard;
    (void)min_step; (void)repairs;
    return last_dts != std::numeric_limits<int64_t>::min();
}

}  // namespace rtx
