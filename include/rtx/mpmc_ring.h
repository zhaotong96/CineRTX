// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Lock-free bounded multi-producer / multi-consumer ring queue based on
// Dmitry Vyukov's sequence counter algorithm.
// Zero dynamic allocations on the hot path; cacheline-aligned cell structures
// and atomic head/tail counters to eliminate false sharing.

#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "rtx/types.h"

namespace rtx {

// ---------------------------------------------------------------------------
// Lock-free Bounded Multi-Producer Multi-Consumer Queue
// Constraint: T must be trivially copyable for safe zero-allocation register movement
// ---------------------------------------------------------------------------
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // Intentional cache-line padding in the queue and its cells.
#endif
template <typename T>
    requires std::is_trivially_copyable_v<T>
class MpmcBoundedQueue {
public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    static_assert(std::is_trivially_copyable_v<T>,
                  "MpmcBoundedQueue requires a trivially copyable type T");

    explicit MpmcBoundedQueue(size_t capacity) {
        capacity_ = nextPowerOfTwo(std::max<size_t>(2, capacity));
        buffer_mask_ = capacity_ - 1;
        buffer_.reset(new (std::nothrow) Cell[capacity_]);
        if (buffer_) {
            for (size_t i = 0; i < capacity_; ++i) {
                buffer_[i].sequence.store(i, std::memory_order_relaxed);
            }
        }
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_relaxed);
    }

    ~MpmcBoundedQueue() = default;

    MpmcBoundedQueue(const MpmcBoundedQueue&) = delete;
    MpmcBoundedQueue& operator=(const MpmcBoundedQueue&) = delete;
    MpmcBoundedQueue(MpmcBoundedQueue&&) = delete;
    MpmcBoundedQueue& operator=(MpmcBoundedQueue&&) = delete;

    [[nodiscard]] bool valid() const noexcept { return buffer_ != nullptr; }
    [[nodiscard]] size_t capacity() const noexcept { return capacity_; }

    [[nodiscard]] size_t size() const noexcept {
        const size_t tail = dequeue_pos_.load(std::memory_order_relaxed);
        const size_t head = enqueue_pos_.load(std::memory_order_relaxed);
        return head >= tail ? std::min(capacity_, head - tail) : 0;
    }

    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    void reset() noexcept {
        if (!buffer_) {
            return;
        }
        enqueue_pos_.store(0, std::memory_order_relaxed);
        dequeue_pos_.store(0, std::memory_order_relaxed);
        for (size_t i = 0; i < capacity_; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] bool tryPush(T item) noexcept {
        if (!buffer_) {
            return false;
        }
        Cell* cell = nullptr;
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &buffer_[pos & buffer_mask_];
            const size_t seq = cell->sequence.load(std::memory_order_acquire);
            const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0) {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
        cell->data = item;
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool tryPop(T& out_item) noexcept {
        if (!buffer_) {
            return false;
        }
        Cell* cell = nullptr;
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        for (;;) {
            cell = &buffer_[pos & buffer_mask_];
            const size_t seq = cell->sequence.load(std::memory_order_acquire);
            const intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (diff == 0) {
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
        out_item = cell->data;
        cell->sequence.store(pos + buffer_mask_ + 1, std::memory_order_release);
        return true;
    }

private:
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // 显式设计的 64 字节缓存行伪共享隔离填充
#endif
    struct alignas(kCacheLineSize) Cell {
        std::atomic<size_t> sequence{0};
        T data{};
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    static constexpr size_t nextPowerOfTwo(size_t value) noexcept {
        if (value <= 2) {
            return 2;
        }
        constexpr size_t kMaxPower = static_cast<size_t>(1) << (sizeof(size_t) * 8 - 1);
        if (value >= kMaxPower) {
            return kMaxPower;
        }
        return std::bit_ceil(value);
    }

    alignas(kCacheLineSize) std::atomic<size_t> enqueue_pos_{0};
    alignas(kCacheLineSize) std::atomic<size_t> dequeue_pos_{0};
    size_t buffer_mask_{0};
    size_t capacity_{0};
    alignas(kCacheLineSize) std::unique_ptr<Cell[]> buffer_;
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

}  // namespace rtx
