// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "rtx/spsc_queue.h"
#include "rtx/vram_pool.h"

using namespace rtx;

RTX_TEST(spsc_queue_capacity_and_fifo) {
    SpscBoundedQueue<int> queue(4);
    RTX_REQUIRE(queue.valid());
    RTX_CHECK_EQ(queue.capacity(), static_cast<size_t>(4));
    RTX_CHECK(queue.empty());
    RTX_CHECK_EQ(queue.size(), static_cast<size_t>(0));

    // One slot is reserved to distinguish full from empty.
    RTX_CHECK(queue.tryPush(1));
    RTX_CHECK(queue.tryPush(2));
    RTX_CHECK(queue.tryPush(3));
    RTX_CHECK(!queue.tryPush(4));
    RTX_CHECK_EQ(queue.size(), static_cast<size_t>(3));

    int value = 0;
    RTX_CHECK(queue.tryPop(value));
    RTX_CHECK_EQ(value, 1);
    RTX_CHECK(queue.tryPop(value));
    RTX_CHECK_EQ(value, 2);
    RTX_CHECK(queue.tryPush(5));
    RTX_CHECK(queue.tryPop(value));
    RTX_CHECK_EQ(value, 3);
    RTX_CHECK(queue.tryPop(value));
    RTX_CHECK_EQ(value, 5);
    RTX_CHECK(!queue.tryPop(value));
    RTX_CHECK(queue.empty());
}

RTX_TEST(spsc_queue_power_of_two_rounding) {
    SpscBoundedQueue<int> queue(5);
    RTX_REQUIRE(queue.valid());
    RTX_CHECK_EQ(queue.capacity(), static_cast<size_t>(8));
    for (int i = 0; i < 7; ++i) {
        RTX_CHECK(queue.tryPush(i));
    }
    RTX_CHECK(!queue.tryPush(99));
    for (int i = 0; i < 7; ++i) {
        int value = -1;
        RTX_CHECK(queue.tryPop(value));
        RTX_CHECK_EQ(value, i);
    }
}

RTX_TEST(spsc_queue_wraparound) {
    SpscBoundedQueue<int> queue(4);
    RTX_REQUIRE(queue.valid());
    for (int cycle = 0; cycle < 8; ++cycle) {
        for (int i = 0; i < 3; ++i) {
            RTX_CHECK(queue.tryPush(cycle * 10 + i));
        }
        for (int i = 0; i < 3; ++i) {
            int value = -1;
            RTX_CHECK(queue.tryPop(value));
            RTX_CHECK_EQ(value, cycle * 10 + i);
        }
    }
    queue.reset();
    RTX_CHECK(queue.empty());
}

RTX_TEST(spsc_queue_concurrent_producer_consumer) {
    SpscBoundedQueue<uint32_t> queue(64);
    RTX_REQUIRE(queue.valid());
    constexpr uint32_t kItems = 200000;
    std::atomic<bool> producer_done{false};
    std::atomic<uint32_t> produced{0};
    std::atomic<uint32_t> consumed{0};
    std::atomic<bool> order_ok{true};

    std::thread producer([&]() {
        for (uint32_t i = 0; i < kItems; ++i) {
            while (!queue.tryPush(i)) {
                std::this_thread::yield();
            }
        }
        produced.store(kItems, std::memory_order_release);
        producer_done.store(true, std::memory_order_release);
    });

    std::thread consumer([&]() {
        uint32_t expected = 0;
        uint32_t value = 0;
        while (consumed.load(std::memory_order_acquire) < kItems) {
            if (!queue.tryPop(value)) {
                std::this_thread::yield();
                continue;
            }
            if (value != expected) {
                order_ok.store(false, std::memory_order_relaxed);
            }
            ++expected;
            consumed.store(expected, std::memory_order_release);
        }
    });

    producer.join();
    consumer.join();
    RTX_CHECK_EQ(produced.load(), kItems);
    RTX_CHECK_EQ(consumed.load(), kItems);
    RTX_CHECK(order_ok.load());
    RTX_CHECK(queue.empty());
    RTX_CHECK(producer_done.load());
}

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
