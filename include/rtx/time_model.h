// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Track 11C: the orthogonal time value types of the playback plane.
//
// Two conversion semantics are kept strictly apart:
//   - absoluteTimestampToFlicks() is stateless.  Feed it any container PTS/DTS in
//     any order: B-frame reordering, seek jumps and negative PTS are all safe.
//   - IncrementalFlicksAccumulator is stateful.  Feed it only constant-step
//     increments (audio samples, synthetic fixed-rate frame stepping), where the
//     integer remainder of each division must carry into the next step.
//
// Phase 11.2: the implementations live inline in this header (the playback
// plane's only translation units are playback_session.cpp /
// wasapi_audio_output.cpp / gop_seek_scheduler.cpp; no separate time_model.cpp
// exists, so the pure policy surface stays header-only).

#pragma once

#include <chrono>
#include <cstdint>
#include <numeric>
#include <ratio>
#include <string>
#include <tuple>
#include <utility>

#include "rtx/flicks.h"
#include "rtx/rational.h"

namespace rtx {

// 原生标准时间周期（Phase 13 统一 1001 因子）：706,305,600,000 Hz 整数时间线的 <chrono> 表达。
using Flicks = std::chrono::duration<int64_t, std::ratio<1, static_cast<intmax_t>(kFlicksPerSecond)>>;
static_assert(Flicks::period::den == static_cast<intmax_t>(kFlicksPerSecond),
              "CRITICAL ERROR: Flicks timebase denominator mismatch with kFlicksPerSecond!");

// The native Flicks time base as a Rational for rescaleQ().  The two-argument
// Rational constructor normalises at runtime, so this is const, not constexpr.
inline const Rational kTimeBaseFlicks{1, static_cast<int64_t>(kFlicksPerSecond)};

enum class TimecodeDropMode : uint8_t { NonDropFrame = 0, DropFrame = 1 };

// Phase 12 (Wave 12.2-B2): standard pair alias replaces passive TimecodeFormat bag.
using TimecodeFormat = std::pair<Rational, TimecodeDropMode>;
inline const TimecodeFormat kDefaultTimecodeFormat{Rational{24, 1}, TimecodeDropMode::NonDropFrame};

// Stateless absolute conversion.  Truncates toward zero, never carries state.
// NOTE: toward-zero is deliberate (edit-list offsets and B-frame negative PTS
// keep the same magnitude as the positive domain); Rounding::Down would be
// floor semantics and would break the negative-PTS contract.
[[nodiscard]] inline int64_t absoluteTimestampToFlicks(int64_t pts, Rational time_base) noexcept {
    if (!time_base.valid() || time_base.num <= 0) {
        return 0;
    }
    const bool negative = pts < 0;
    const uint64_t magnitude = negative ? 0ULL - static_cast<uint64_t>(pts)
                                        : static_cast<uint64_t>(pts);
// Exact integer conversion on the 706,305,600,000 Hz axis, truncated toward zero:
//   value = pts * num * kFlicksPerSecond / den
// split into whole+remainder so every intermediate stays inside unsigned
// 64 bits (num <= 30000 and den <= 1001 keep flicks_per_unit < 2^63).
    const uint64_t g = std::gcd(static_cast<uint64_t>(time_base.num),
                                static_cast<uint64_t>(time_base.den));
    const uint64_t r_num = static_cast<uint64_t>(time_base.num) / g;
    const uint64_t r_den = static_cast<uint64_t>(time_base.den) / g;
    const uint64_t g2 = std::gcd(static_cast<uint64_t>(kFlicksPerSecond), r_den);
    const uint64_t flicks_per_unit = r_num * (static_cast<uint64_t>(kFlicksPerSecond) / g2);
    const uint64_t den = r_den / g2;
    const uint64_t whole = magnitude / den;
    const uint64_t rest = magnitude % den;
    const uint64_t value = whole * flicks_per_unit + (rest * flicks_per_unit) / den;
    return negative ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
}
// Phase 12 (Wave 12.2-C): tuple state (step_seconds, current_flicks, remainder) + free functions.
using IncrementalFlicksAccumulator = std::tuple<Rational, int64_t, int64_t>;

[[nodiscard]] inline IncrementalFlicksAccumulator makeFlicksAccumulator(Rational step_seconds,
                                                                        int64_t base_flicks = 0) noexcept {
    return {step_seconds, base_flicks, 0};
}

inline void resetFlicksAccumulator(IncrementalFlicksAccumulator& acc, int64_t base_flicks = 0) noexcept {
    auto& [step, current_flicks, remainder] = acc;
    (void)step;
    current_flicks = base_flicks;
    remainder = 0;
}

[[nodiscard]] inline int64_t advanceFlicksStep(IncrementalFlicksAccumulator& acc) noexcept {
    auto& [step, current_flicks, remainder] = acc;
    if (!step.valid() || step.num <= 0 || step.den <= 0) return current_flicks;
    const uint64_t step_ticks = step.num > (1LL << 33)
        ? ~0ULL
        : static_cast<uint64_t>(step.num) * kFlicksPerSecond;
    const uint64_t total = step_ticks + static_cast<uint64_t>(remainder);
    const uint64_t quotient = total / static_cast<uint64_t>(step.den);
    remainder = static_cast<int64_t>(total % static_cast<uint64_t>(step.den));
    current_flicks += static_cast<int64_t>(quotient);
    return current_flicks;
}

[[nodiscard]] inline int64_t flicksAccumulatorRemainder(const IncrementalFlicksAccumulator& acc) noexcept {
    const auto& [step, cur, rem] = acc;
    (void)step; (void)cur;
    return rem;
}

// SMPTE ST 12-1 timecode for 23.976 / 24 / 25 / 29.97 DF&NDF / 30 / 48 / 50 /
// 59.94 DF&NDF / 60 / 120 fps.  Drop-frame is only defined for the 1000/1001
// families at 30 and 60 nominal; any other rate renders non-drop.
// Negative positions clamp to 00:00:00:00.
[[nodiscard]] inline std::string formatSmpteTimecode(int64_t flicks, const TimecodeFormat& fmt) {
    const auto& [rate, drop_mode] = fmt;
    if (rate.num <= 0 || rate.den <= 0) {
        return "00:00:00:00";
    }
    int64_t frames = 0;
    if (flicks > 0) {
        // Overflow-safe exact floor(flicks * rate.num / (rate.den * kFlicksPerSecond))
        // for 1001-extended Flicks (706,305,600,000 Hz) without requiring __int128.
        const uint64_t uflicks = static_cast<uint64_t>(flicks);
        const uint64_t rnum = static_cast<uint64_t>(rate.num);
        const uint64_t rden = static_cast<uint64_t>(rate.den);
        const uint64_t q_sec = uflicks / kFlicksPerSecond;
        const uint64_t r_flk = uflicks % kFlicksPerSecond;
        const uint64_t whole_num = q_sec * rnum;
        const uint64_t frames_from_sec = whole_num / rden;
        const uint64_t rem_sec_num = whole_num % rden;
        const uint64_t divisor = rden * kFlicksPerSecond;
        const uint64_t rem_total = rem_sec_num * kFlicksPerSecond + r_flk * rnum;
        frames = static_cast<int64_t>(frames_from_sec + (rem_total / divisor));
    }
    int64_t drop = 0;
    if (drop_mode == TimecodeDropMode::DropFrame) {
        if (rate == kFps29_97) drop = 2;
        else if (rate == kFps59_94) drop = 4;
    }
    const int64_t nominal = (rate.num + rate.den - 1) / rate.den;
    int64_t label = frames;
    if (drop > 0) {
        const int64_t per_ten_minutes = nominal * 600 - drop * 9;
        const int64_t per_minute = nominal * 60 - drop;
        const int64_t tens = frames / per_ten_minutes;
        const int64_t rest = frames % per_ten_minutes;
        label += drop * 9 * tens;
        if (rest > drop) {
// SMPTE ST 12-1: skip `drop` frame labels every minute except every tenth.
            label += drop * ((rest - drop) / per_minute);
        }
    }
    const int64_t ff = label % nominal;
    const int64_t ss = (label / nominal) % 60;
    const int64_t mm = (label / (nominal * 60)) % 60;
    const int64_t hh = (label / (nominal * 3600)) % 24;
    const char separator = drop > 0 ? ';' : ':';
    std::string out(11, '0');
    out[0] = static_cast<char>('0' + (hh / 10) % 10);
    out[1] = static_cast<char>('0' + hh % 10);
    out[2] = ':';
    out[3] = static_cast<char>('0' + (mm / 10) % 10);
    out[4] = static_cast<char>('0' + mm % 10);
    out[5] = ':';
    out[6] = static_cast<char>('0' + (ss / 10) % 10);
    out[7] = static_cast<char>('0' + ss % 10);
    out[8] = separator;
    out[9] = static_cast<char>('0' + (ff / 10) % 10);
    out[10] = static_cast<char>('0' + ff % 10);
    return out;
}
}  // namespace rtx
