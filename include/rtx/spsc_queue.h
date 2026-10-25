// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Bounded single-producer / single-consumer queue.  Used for the free lists of
// the VRAM pools and for the stage hand-off queues.  No mutex, no allocation
// on the hot path.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace rtx {

template <typename T>
class SpscBoundedQueue {
public:
    static_assert(std::is_trivially_copyable_v<T> || std::is_move_constructible_v<T>);

    explicit SpscBoundedQueue(size_t capacity) {
        // Capacity must be a power of two so that index masking is cheap.
        capacity_ = nextPowerOfTwo(capacity);
        mask_ = capacity_ - 1;
        buffer_.reset(new (std::nothrow) Slot[capacity_]);
    }

    SpscBoundedQueue(const SpscBoundedQueue&) = delete;
    SpscBoundedQueue& operator=(const SpscBoundedQueue&) = delete;

    bool valid() const noexcept { return static_cast<bool>(buffer_); }
    size_t capacity() const noexcept { return capacity_; }

    bool tryPush(const T& value) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & mask_;
        if (next == tail_cache_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (next == tail_cache_) {
                return false;
            }
        }
        buffer_[head].value = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool tryPush(T&& value) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = (head + 1) & mask_;
        if (next == tail_cache_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (next == tail_cache_) {
                return false;
            }
        }
        buffer_[head].value = std::move(value);
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool tryPop(T& out) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_cache_) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (tail == head_cache_) {
                return false;
            }
        }
        out = std::move(buffer_[tail].value);
        buffer_[tail].value = T{};
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    // Approximate size; safe to call from either side but may be stale.
    size_t size() const noexcept {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return (head + capacity_ - tail) & mask_;
    }

    bool empty() const noexcept { return size() == 0; }

    // Only safe before any concurrent access.
    void reset() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        head_cache_ = 0;
        tail_cache_ = 0;
    }

private:
    struct Slot {
        T value{};
    };

    static size_t nextPowerOfTwo(size_t value) noexcept {
        constexpr size_t kMaxPower = (static_cast<size_t>(1) << (sizeof(size_t) * 8 - 1));
        if (value >= kMaxPower) {
            return kMaxPower;
        }
        size_t result = 2;
        while (result < value) {
            result <<= 1;
        }
        return result;
    }

    alignas(64) std::unique_ptr<Slot[]> buffer_;
    size_t capacity_ = 2;
    size_t mask_ = 1;
    alignas(64) mutable std::atomic<size_t> head_{0};
    size_t tail_cache_ = 0;
    alignas(64) mutable std::atomic<size_t> tail_{0};
    size_t head_cache_ = 0;
};

}  // namespace rtx
