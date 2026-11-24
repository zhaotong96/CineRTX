// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Track 11C / Wave 12.2-D: lock-free playback telemetry.
// One worker thread publishes; the GUI thread pulls at 30 Hz from a SetTimer.
// Nothing here allocates, blocks, or posts a Win32 message.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace rtx {

// Phase 12 (Wave 12.2-D): standard std::array<uint64_t, 16> (128 B) replaces TelemetrySnapshot wrapper.
using TelemetrySnapshot = std::array<uint64_t, 16>;
static_assert(std::is_trivially_copyable_v<TelemetrySnapshot>);
static_assert(sizeof(TelemetrySnapshot) == 128);

enum class TelemetrySlot : std::size_t {
    FramesDecoded = 0,
    FramesRendered = 1,
    FramesDropped = 2,
    VramUsedBytes = 3,
    VramBudgetBytes = 4,
    SequenceEpoch = 5,
    AvDriftUs = 6,
    PlayheadFlicks = 7,
    CurrentFpsX100 = 8,
};

[[nodiscard]] constexpr uint64_t telemetryU64(const TelemetrySnapshot& s, TelemetrySlot slot) noexcept {
    return s[static_cast<std::size_t>(slot)];
}
[[nodiscard]] constexpr int64_t telemetryI64(const TelemetrySnapshot& s, TelemetrySlot slot) noexcept {
    return static_cast<int64_t>(s[static_cast<std::size_t>(slot)]);
}
[[nodiscard]] constexpr uint32_t telemetryU32(const TelemetrySnapshot& s, TelemetrySlot slot) noexcept {
    return static_cast<uint32_t>(s[static_cast<std::size_t>(slot)]);
}
constexpr void telemetrySetU64(TelemetrySnapshot& s, TelemetrySlot slot, uint64_t v) noexcept {
    s[static_cast<std::size_t>(slot)] = v;
}
constexpr void telemetrySetI64(TelemetrySnapshot& s, TelemetrySlot slot, int64_t v) noexcept {
    s[static_cast<std::size_t>(slot)] = static_cast<uint64_t>(v);
}
constexpr void telemetrySetU32(TelemetrySnapshot& s, TelemetrySlot slot, uint32_t v) noexcept {
    s[static_cast<std::size_t>(slot)] = static_cast<uint64_t>(v);
}

// Phase 12 (Wave 12.2-D): 192-byte lock-free seqlock bus as std::array<std::atomic<uint64_t>, 24>.
// Word [0] = odd/even sequence counter (cache line 0); words [8..23] = 16 payload words (cache lines 1..2).
using AtomicTelemetryBus = std::array<std::atomic<uint64_t>, 24>;
static_assert(sizeof(AtomicTelemetryBus) == 192);

inline void telemetryPublish(AtomicTelemetryBus& bus, const TelemetrySnapshot& value) noexcept {
    const uint64_t seq = bus[0].load(std::memory_order_relaxed);
    bus[0].store(seq + 1, std::memory_order_release);
    for (std::size_t i = 0; i < value.size(); ++i) {
        bus[8 + i].store(value[i], std::memory_order_relaxed);
    }
    bus[0].store(seq + 2, std::memory_order_release);
}

[[nodiscard]] inline TelemetrySnapshot telemetryRead(const AtomicTelemetryBus& bus) noexcept {
    TelemetrySnapshot out{};
    for (;;) {
        const uint64_t seq = bus[0].load(std::memory_order_acquire);
        if ((seq & 1ull) != 0ull) {
            continue;
        }
        for (std::size_t i = 0; i < out.size(); ++i) {
            out[i] = bus[8 + i].load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        if (bus[0].load(std::memory_order_acquire) == seq) {
            break;
        }
    }
    return out;
}

}  // namespace rtx
