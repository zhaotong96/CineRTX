// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Track 11C: lock-free playback telemetry.
//
// One worker thread publishes; the GUI thread pulls at 30 Hz from a SetTimer.
// Nothing here allocates, blocks, or posts a Win32 message, so a 120 fps decode
// loop can never flood the UI message queue.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace rtx {

// @rtx-type-justification
// Category: Hardware-Layout
// Why-Std-Fails: 硬件/内核参数布局结构：字段序即 PTX 内核参数 ABI 或缓存行对齐布局，std 聚合无法表达布局承诺。
// Invariant: 内存布局与对齐由硬件消费方逐字读取；禁止增删改字段顺序。
struct alignas(64) TelemetrySnapshot {
    uint64_t frames_decoded = 0;
    uint64_t frames_rendered = 0;
    uint64_t frames_dropped = 0;
    uint64_t vram_used_bytes = 0;
    uint64_t vram_budget_bytes = 0;
    uint64_t sequence_epoch = 0;
    int64_t av_drift_us = 0;
    int64_t playhead_flicks = 0;
    uint32_t current_fps_x100 = 0;
    uint32_t reserved = 0;
};

static_assert(std::is_trivially_copyable_v<TelemetrySnapshot>);
static_assert(sizeof(TelemetrySnapshot) % sizeof(uint64_t) == 0);

// Seqlock: an odd sequence marks a write in progress, an even one a stable slot.
// The payload words are atomic so the concurrent copy is not a data race.
// @rtx-type-justification
// Category: Concurrency-Invariant
// Why-Std-Fails: 无锁并发数据结构：std::mutex 化会引入回调/呈现路径的长锁等待，std 原语无等价的seqlock/世代标志语义。
// Invariant: 写者单一、读者多份；序号奇偶与 acquire/release 配对保证绝不交付撕裂快照。
class AtomicTelemetryBus {
public:
// Single writer. Never blocks, never allocates.
    void publish(const TelemetrySnapshot& value) noexcept {
        uint64_t words[kWordCount];
        std::memcpy(words, &value, sizeof value);
        const uint32_t sequence = sequence_.load(std::memory_order_relaxed);
        sequence_.store(sequence + 1, std::memory_order_release);
        for (std::size_t i = 0; i < kWordCount; ++i) {
            words_[i].store(words[i], std::memory_order_relaxed);
        }
        sequence_.store(sequence + 2, std::memory_order_release);
    }
    [[nodiscard]] TelemetrySnapshot read() const noexcept {
// Reader. Retries only while a publish overlaps the copy.
        uint64_t words[kWordCount];
        for (;;) {
            const uint32_t sequence = sequence_.load(std::memory_order_acquire);
            if ((sequence & 1u) != 0u) {
                continue;
            }
            for (std::size_t i = 0; i < kWordCount; ++i) {
                words[i] = words_[i].load(std::memory_order_relaxed);
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            if (sequence_.load(std::memory_order_acquire) == sequence) {
                break;
            }
        }
        TelemetrySnapshot out;
        std::memcpy(&out, words, sizeof out);
        return out;
    }
private:
    static constexpr std::size_t kWordCount = sizeof(TelemetrySnapshot) / sizeof(uint64_t);

    alignas(64) std::atomic<uint32_t> sequence_{0};
    alignas(64) std::atomic<uint64_t> words_[kWordCount]{};
};

}  // namespace rtx
