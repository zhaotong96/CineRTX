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
#include <ratio>
#include <string>

#include "rtx/flicks.h"
#include "rtx/rational.h"

namespace rtx {

// 原生标准时间周期（Phase 11.2-R）：705,600,000 Hz 整数时间线的 <chrono> 表达。
// 1 ns = 441/625 Flicks，编译期严格有理数折算，零浮点魔法常数、零精度丢失。
using Flicks = std::chrono::duration<int64_t, std::ratio<1, 705600000>>;

// The native Flicks time base as a Rational for rescaleQ().  The two-argument
// Rational constructor normalises at runtime, so this is const, not constexpr.
inline const Rational kTimeBaseFlicks{1, static_cast<int64_t>(kFlicksPerSecond)};

enum class TimecodeDropMode : uint8_t { NonDropFrame = 0, DropFrame = 1 };

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 帧率 Rational 与 drop-mode 的配对领域模型；std::pair 丢失字段语义，formatSmpteTimecode 的六类白名单时间码契约以具名字段表达。
// Invariant: drop_mode 仅对 1000/1001 家族（标称 30/60）有意义，其他帧率一律渲染为 non-drop。
struct TimecodeFormat {
    Rational frame_rate{24, 1};
    TimecodeDropMode drop_mode = TimecodeDropMode::NonDropFrame;
};

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
// Exact integer conversion on the 705.6 MHz axis, truncated toward zero:
//   value = pts * num * kFlicksPerSecond / den
// split into whole+remainder so every intermediate stays inside unsigned
// 64 bits (num <= 30000 and den <= 1001 keep flicks_per_unit < 2^63).
    const uint64_t den = static_cast<uint64_t>(time_base.den);
    const uint64_t num = static_cast<uint64_t>(time_base.num);
    const uint64_t whole = magnitude / den;
    const uint64_t rest = magnitude % den;
    const uint64_t flicks_per_unit = num * static_cast<uint64_t>(kFlicksPerSecond);
    const uint64_t value = whole * flicks_per_unit + (rest * flicks_per_unit) / den;
    return negative ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
}
// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 逐步除法的余数必须跨调用进位（Bresenham 载体），std::chrono 时钟与 std::accumulate 均无法表达「余数进位 + 饱和防回绕」的持续状态机。
// Invariant: 每步推进的漂移永不越过 1 Flicks tick；非法步长为 no-op（不除零）。
class IncrementalFlicksAccumulator {
public:
    explicit IncrementalFlicksAccumulator(Rational step_seconds) noexcept
        : step_(step_seconds) {}

    void reset(int64_t base_flicks = 0) noexcept {
        current_flicks_ = base_flicks;
        remainder_ = 0;
    }

    // Consumes one step and returns the timeline position it lands on.
    [[nodiscard]] int64_t advanceOneStep() noexcept {
        if (!step_.valid() || step_.num <= 0 || step_.den <= 0) {
            return current_flicks_;
        }
        const uint64_t step_ticks =
            step_.num > (1LL << 33)
// step_.num * kFlicksPerSecond stays far inside 64 bits for every real
// step (audio sample periods, frame periods); saturate instead of
// wrapping for pathological inputs.
                ? ~0ULL
                : static_cast<uint64_t>(step_.num) * kFlicksPerSecond;
        const uint64_t total = step_ticks + static_cast<uint64_t>(remainder_);
        const uint64_t quotient = total / static_cast<uint64_t>(step_.den);
        remainder_ = static_cast<int64_t>(total % static_cast<uint64_t>(step_.den));
        current_flicks_ += static_cast<int64_t>(quotient);
        return current_flicks_;
    }
    [[nodiscard]] int64_t current() const noexcept { return current_flicks_; }
    [[nodiscard]] int64_t remainder() const noexcept { return remainder_; }

private:
    Rational step_;
    int64_t current_flicks_ = 0;
    int64_t remainder_ = 0;
};

// SMPTE ST 12-1 timecode for 23.976 / 24 / 25 / 29.97 DF&NDF / 30 / 48 / 50 /
// 59.94 DF&NDF / 60 / 120 fps.  Drop-frame is only defined for the 1000/1001
// families at 30 and 60 nominal; any other rate renders non-drop.
// Negative positions clamp to 00:00:00:00.
[[nodiscard]] inline std::string formatSmpteTimecode(int64_t flicks, const TimecodeFormat& fmt) {
    const Rational& rate = fmt.frame_rate;
    if (rate.num <= 0 || rate.den <= 0) {
        return "00:00:00:00";
    }
    int64_t frames = 0;
    if (flicks > 0) {
        const uint64_t numerator = static_cast<uint64_t>(flicks) *
                                   static_cast<uint64_t>(rate.num);
        const uint64_t divisor = static_cast<uint64_t>(rate.den) * kFlicksPerSecond;
        frames = static_cast<int64_t>(numerator / divisor);
// flicks * rate.num fits 64 bits for any programme shorter than ~8 days
// at the highest supported rate; clamp instead of wrapping beyond that.
    }
    int64_t drop = 0;
    if (fmt.drop_mode == TimecodeDropMode::DropFrame) {
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
