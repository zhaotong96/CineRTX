// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 8 (Task 8-B2): VFR / damaged-timestamp monotonicity sanitizer.
//
// Every packet handed to av_interleaved_write_frame must satisfy
// DTS_i > DTS_(i-1) on the same stream (equality only across different
// streams), otherwise the muxer's interleaver fails with EINVAL and a
// >1-hour VFR phone recording aborts mid-transcode.  The guard is a pure
// value type so the fuzzing suite (test_fuzz_resilience.cpp) can drive it
// without a container layer.

#pragma once

#include <cstdint>
#include <limits>

namespace rtx {

// Strictly monotonic DTS fixture with a PTS >= DTS clamp.  One instance per
// output stream (video, every audio passthrough stream, every subtitle
// stream).  The repair step is max(min_step, fallback_step): when the caller
// knows the stream's ideal pacing, a single-sample regression is repaired
// back onto the ideal timeline with ZERO accumulated A/V drift (the gate
// 8.2 contract); min_step alone would drag every following sample one tick
// forward.
struct StreamTimestampGuard {
    int64_t last_dts = std::numeric_limits<int64_t>::min();
    int64_t min_step = 1;
    int64_t repairs = 0;   // diagnostics: how many timestamps were rewritten

    constexpr void sanitize(int64_t& pts, int64_t& dts, int64_t fallback_step) noexcept {
        if (dts == std::numeric_limits<int64_t>::min()) {
            dts = (last_dts == std::numeric_limits<int64_t>::min()) ? 0
                                                                    : (last_dts + fallback_step);
        }
        if (last_dts != std::numeric_limits<int64_t>::min() && dts <= last_dts) {
            dts = last_dts + (fallback_step > min_step ? fallback_step : min_step);
            ++repairs;
        }
        if (pts == std::numeric_limits<int64_t>::min() || pts < dts) {
            pts = dts;  // no B-frame reordering or an inverted pair
        }
        last_dts = dts;
    }

    constexpr void reset() noexcept { last_dts = std::numeric_limits<int64_t>::min(); }

    constexpr bool primed() const noexcept {
        return last_dts != std::numeric_limits<int64_t>::min();
    }
};

}  // namespace rtx
