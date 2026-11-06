// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "rtx/mpmc_ring.h"
#include "rtx/vram_pool.h"

using namespace rtx;
RTX_TEST(fence_timeline_monotonic) {
    FenceTimeline timeline(5);
    RTX_CHECK_EQ(timeline.current(), static_cast<uint64_t>(5));
    RTX_CHECK_EQ(timeline.next(), static_cast<uint64_t>(6));
    RTX_CHECK_EQ(timeline.current(), static_cast<uint64_t>(6));
    timeline.signal(3);  // lower values never move the timeline backwards
    RTX_CHECK_EQ(timeline.current(), static_cast<uint64_t>(6));
    timeline.signal(10);
    RTX_CHECK_EQ(timeline.current(), static_cast<uint64_t>(10));
    RTX_CHECK_EQ(timeline.next(), static_cast<uint64_t>(11));

    FenceTimeline concurrent(0);
    std::atomic<uint64_t> max_value{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 10000; ++i) {
                const uint64_t value = concurrent.next();
                uint64_t previous = max_value.load(std::memory_order_relaxed);
                while (previous < value &&
                       !max_value.compare_exchange_weak(previous, value,
                                                        std::memory_order_relaxed)) {
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    RTX_CHECK_EQ(concurrent.current(), static_cast<uint64_t>(40000));
    RTX_CHECK_EQ(max_value.load(), static_cast<uint64_t>(40000));
}

RTX_TEST(concurrency_mpmc_queue_capacity_and_fifo) {
    MpmcBoundedQueue<int> queue(4);
    RTX_REQUIRE(queue.valid());
    RTX_CHECK_EQ(queue.capacity(), static_cast<size_t>(4));
    RTX_CHECK(queue.empty());
    RTX_CHECK_EQ(queue.size(), static_cast<size_t>(0));

    // Vyukov queue can hold up to capacity items
    RTX_CHECK(queue.tryPush(1));
    RTX_CHECK(queue.tryPush(2));
    RTX_CHECK(queue.tryPush(3));
    RTX_CHECK(queue.tryPush(4));
    RTX_CHECK(!queue.tryPush(5));
    RTX_CHECK_EQ(queue.size(), static_cast<size_t>(4));

    int value = 0;
    RTX_CHECK(queue.tryPop(value));
    RTX_CHECK_EQ(value, 1);
    RTX_CHECK(queue.tryPop(value));
    RTX_CHECK_EQ(value, 2);
    RTX_CHECK(queue.tryPush(5));
    RTX_CHECK(queue.tryPop(value));
    RTX_CHECK_EQ(value, 3);
    RTX_CHECK(queue.tryPop(value));
    RTX_CHECK_EQ(value, 4);
    RTX_CHECK(queue.tryPop(value));
    RTX_CHECK_EQ(value, 5);
    RTX_CHECK(!queue.tryPop(value));
    RTX_CHECK(queue.empty());
}

RTX_TEST(concurrency_mpmc_queue_multi_producer_multi_consumer) {
    MpmcBoundedQueue<uint32_t> queue(128);
    RTX_REQUIRE(queue.valid());
    constexpr uint32_t kProducers = 4;
    constexpr uint32_t kConsumers = 4;
    constexpr uint32_t kItemsPerProducer = 50000;
    constexpr uint32_t kTotalItems = kProducers * kItemsPerProducer;

    std::atomic<uint32_t> consumed_count{0};
    std::atomic<uint64_t> sum_consumed{0};
    std::atomic<bool> monitor_size{true};
    std::atomic<bool> sizes_bounded{true};
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::thread size_monitor([&]() {
        while (monitor_size.load(std::memory_order_relaxed)) {
            if (queue.size() > queue.capacity()) {
                sizes_bounded.store(false, std::memory_order_relaxed);
            }
        }
    });

    for (uint32_t p = 0; p < kProducers; ++p) {
        producers.emplace_back([&, p]() {
            const uint32_t base = p * kItemsPerProducer;
            for (uint32_t i = 1; i <= kItemsPerProducer; ++i) {
                while (!queue.tryPush(base + i)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (uint32_t c = 0; c < kConsumers; ++c) {
        consumers.emplace_back([&]() {
            uint32_t item = 0;
            while (consumed_count.load(std::memory_order_relaxed) < kTotalItems) {
                if (queue.tryPop(item)) {
                    sum_consumed.fetch_add(item, std::memory_order_relaxed);
                    consumed_count.fetch_add(1, std::memory_order_release);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }
    for (auto& consumer : consumers) {
        consumer.join();
    }
    monitor_size.store(false, std::memory_order_relaxed);
    size_monitor.join();

    RTX_CHECK_EQ(consumed_count.load(), kTotalItems);
    RTX_CHECK(sizes_bounded.load());
    uint64_t expected_sum = 0;
    for (uint32_t p = 0; p < kProducers; ++p) {
        const uint64_t base = p * kItemsPerProducer;
        for (uint64_t i = 1; i <= kItemsPerProducer; ++i) {
            expected_sum += (base + i);
        }
    }
    RTX_CHECK_EQ(sum_consumed.load(), expected_sum);
    RTX_CHECK(queue.empty());
}

RTX_TEST(concurrency_mpmc_queue_slot_handle) {
    MpmcBoundedQueue<SlotHandle> queue(8);
    RTX_REQUIRE(queue.valid());
    RTX_CHECK(queue.tryPush(SlotHandle(10u)));
    RTX_CHECK(queue.tryPush(SlotHandle(20u)));

    SlotHandle out{};
    RTX_CHECK(queue.tryPop(out));
    RTX_CHECK_EQ(out.v, 10u);
    RTX_CHECK(queue.tryPop(out));
    RTX_CHECK_EQ(out.v, 20u);
    RTX_CHECK(!queue.tryPop(out));
}
