// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <chrono>
#include <atomic>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "rtx/types.h"
#include "rtx/vram_pool.h"

using namespace rtx;

namespace {

PoolSpec smallSpec(int slots = 4) {
    PoolSpec spec;
    spec.name = "test-pool";
    spec.width = 64;
    spec.height = 32;
    spec.format = PixelFormat::NV12;
    spec.slots = slots;
    spec.alignment = 256;
    return spec;
}

}  // namespace

RTX_TEST(vram_pool_spec_and_budget) {
    const PoolSpec spec = smallSpec();
    RTX_CHECK_EQ(spec.frameBytes(), frameByteSize(64, 32, PixelFormat::NV12));
    RTX_CHECK_EQ(spec.frameBytes(), static_cast<size_t>(256 * 32 + 256 * 16));

    const PoolBudget budget = computeBudget({spec, spec});
    RTX_CHECK_EQ(budget.entries.size(), static_cast<size_t>(2));
    RTX_CHECK_EQ(budget.entries[0].slots, 4);
    RTX_CHECK_EQ(budget.entries[0].pool_bytes, spec.frameBytes() * 4);
    RTX_CHECK_EQ(budget.total_bytes, spec.frameBytes() * 8);
    RTX_CHECK(budget.find("test-pool") != nullptr);
    RTX_CHECK(budget.find("missing") == nullptr);
    const std::string text = budget.toString();
    RTX_CHECK(text.find("test-pool") != std::string::npos);
    RTX_CHECK(text.find("MiB") != std::string::npos);
}

RTX_TEST(vram_pool_default_plan_fits_8gb) {
    const std::vector<PoolSpec> specs = defaultPoolSpecs();
    RTX_REQUIRE(specs.size() == static_cast<size_t>(5));
    const PoolBudget budget = computeBudget(specs);
    RTX_CHECK(budget.total_bytes > 0);
    RTX_CHECK(budget.total_bytes < 8ull * 1024ull * 1024ull * 1024ull);
    RTX_CHECK(validateBudget(budget, 8ull * 1024ull * 1024ull * 1024ull).ok());
    RTX_CHECK(budget.find("Pool_Encode_4K_P010") != nullptr);
    RTX_CHECK(budget.find("Pool_Interp_1080p") != nullptr);
}

RTX_TEST(vram_pool_encode_depth_covers_b_frames) {
    RTX_CHECK_EQ(encodePoolSlots(0), 12);
    RTX_CHECK(encodePoolSlots(7) >= 4 + 7);
    RTX_CHECK_EQ(encodePoolSlots(15), 19);

    const std::vector<PoolSpec> specs = defaultPoolSpecs(15);
    const PoolSpec* encode = nullptr;
    for (const PoolSpec& spec : specs) {
        if (spec.name == "Pool_Encode_4K_P010") {
            encode = &spec;
        }
    }
    RTX_REQUIRE(encode != nullptr);
    RTX_CHECK_EQ(encode->slots, 19);
    RTX_CHECK(validateBudget(computeBudget(specs), 8ull * 1024ull * 1024ull * 1024ull).ok());
}

RTX_TEST(vram_budget_validation) {
    PoolSpec spec = smallSpec(2);
    const PoolBudget budget = computeBudget({spec});
    RTX_CHECK_EQ(validateBudget(budget, 0).code(), ErrorCode::InvalidArgument);
    RTX_CHECK_EQ(validateBudget(budget, budget.total_bytes).code(), ErrorCode::OutOfMemory);
    RTX_CHECK_EQ(validateBudget(budget, budget.total_bytes + 100, 384ull * 1024 * 1024).code(),
                 ErrorCode::OutOfMemory);
    RTX_CHECK(validateBudget(budget, budget.total_bytes + 384ull * 1024 * 1024).ok());
    RTX_CHECK(validateBudget(budget, budget.total_bytes + 1024, 1024).ok());
    RTX_CHECK(validateBudget(budget, budget.total_bytes + 1023, 1024).ok() == false);
}

RTX_TEST(vram_surface_pool_lifecycle) {
    SurfacePool pool;
    RTX_CHECK_EQ(pool.initialize(PoolSpec{}).code(), ErrorCode::InvalidArgument);
    const PoolSpec spec = smallSpec(4);
    RTX_REQUIRE(pool.initialize(spec).ok());
    RTX_CHECK_EQ(pool.capacity(), static_cast<size_t>(4));
    RTX_CHECK_EQ(pool.available(), static_cast<size_t>(4));
    RTX_CHECK_EQ(pool.used(), static_cast<size_t>(0));
    RTX_CHECK_EQ(pool.occupancy(), 0.0);

    std::set<uint32_t> ids;
    std::vector<SurfaceLease> leases;
    std::atomic<int> callbacks{0};
    pool.setReleaseCallback([&callbacks]() { callbacks.fetch_add(1); });
    for (int i = 0; i < 4; ++i) {
        SurfaceLease lease = pool.acquire();
        RTX_REQUIRE(static_cast<bool>(lease));
        ids.insert(lease.slotId());
        const SurfaceSlot* slot = lease.slot();
        RTX_REQUIRE(slot != nullptr);
        RTX_CHECK(slot->device_ptr != kInvalidDevicePtr);
        const Frame view = lease.view();
        RTX_CHECK_EQ(view.width, 64);
        RTX_CHECK_EQ(view.height, 32);
        RTX_CHECK(view.format == PixelFormat::NV12);
        RTX_CHECK_EQ(view.bit_depth, 8);
        RTX_CHECK(view.planes[0] != nullptr);
        RTX_CHECK(view.planes[1] != nullptr);
        RTX_CHECK_EQ(reinterpret_cast<uintptr_t>(view.planes[0]) % 256, static_cast<uintptr_t>(0));
        RTX_CHECK_EQ(view.planes[1] - view.planes[0], static_cast<ptrdiff_t>(256 * 32));
        leases.push_back(std::move(lease));
    }
    RTX_CHECK_EQ(ids.size(), static_cast<size_t>(4));
    RTX_CHECK_EQ(pool.used(), static_cast<size_t>(4));
    RTX_CHECK_EQ(pool.available(), static_cast<size_t>(0));
    RTX_CHECK(pool.highWatermark());
    RTX_CHECK(pool.criticalWatermark());
    RTX_CHECK_EQ(pool.acquireCount(), static_cast<uint64_t>(4));

    const SurfaceLease exhausted = pool.acquire();
    RTX_CHECK(!exhausted);
    RTX_CHECK_EQ(pool.emptyCount(), static_cast<uint64_t>(1));

    leases.pop_back();
    RTX_CHECK_EQ(pool.used(), static_cast<size_t>(3));
    RTX_CHECK_EQ(callbacks.load(), 1);
    RTX_CHECK_EQ(pool.releaseCount(), static_cast<uint64_t>(1));

    leases.clear();
    RTX_CHECK_EQ(pool.used(), static_cast<size_t>(0));
    RTX_CHECK_EQ(pool.available(), static_cast<size_t>(4));
    RTX_CHECK_EQ(callbacks.load(), 4);

    // A stale slot release is ignored instead of corrupting the free list.
    pool.release(0);
    pool.release(999);
    RTX_CHECK_EQ(pool.used(), static_cast<size_t>(0));
    SurfaceLease again = pool.acquire();
    RTX_CHECK(static_cast<bool>(again));

    pool.shutdown();
    RTX_CHECK_EQ(pool.capacity(), static_cast<size_t>(0));
    RTX_CHECK(!pool.acquire());
    RTX_CHECK_EQ(pool.initialize(PoolSpec{}).code(), ErrorCode::InvalidArgument);
}

RTX_TEST(vram_surface_lease_move_semantics) {
    SurfacePool pool;
    RTX_REQUIRE(pool.initialize(smallSpec(2)).ok());
    SurfaceLease first = pool.acquire();
    RTX_REQUIRE(static_cast<bool>(first));
    const uint32_t id = first.slotId();
    SurfaceLease second = std::move(first);
    RTX_CHECK(!first);
    RTX_CHECK(static_cast<bool>(second));
    RTX_CHECK_EQ(second.slotId(), id);
    first = std::move(second);
    RTX_CHECK(static_cast<bool>(first));
    RTX_CHECK(!second);
    first.reset();
    RTX_CHECK(!first);
    RTX_CHECK_EQ(pool.used(), static_cast<size_t>(0));
}

RTX_TEST(vram_backpressure_hysteresis) {
    BackpressureController controller;
    RTX_CHECK(!controller.paused());
    controller.update(5, 10);
    RTX_CHECK(!controller.paused());
    RTX_CHECK_NEAR(controller.occupancy(), 0.5, 1e-9);
    controller.update(8, 10);
    RTX_CHECK(controller.paused());
    RTX_CHECK_EQ(controller.pauseCount(), static_cast<uint64_t>(1));
    controller.update(5, 10);
    RTX_CHECK(controller.paused());
    controller.update(2, 10);
    RTX_CHECK(!controller.paused());
    RTX_CHECK_EQ(controller.resumeCount(), static_cast<uint64_t>(1));
    controller.update(9, 10);
    RTX_CHECK(controller.critical());
    RTX_CHECK_EQ(controller.criticalCount(), static_cast<uint64_t>(1));
    controller.update(10, 10);
    RTX_CHECK_EQ(controller.criticalCount(), static_cast<uint64_t>(1));
    controller.update(0, 0);
    RTX_CHECK_NEAR(controller.occupancy(), 0.0, 1e-9);
    controller.reset();
    RTX_CHECK(!controller.paused());
    RTX_CHECK(!controller.critical());
    RTX_CHECK_EQ(controller.pauseCount(), static_cast<uint64_t>(0));
    RTX_CHECK_EQ(controller.criticalCount(), static_cast<uint64_t>(0));
}

RTX_TEST(vram_tdr_guard) {
    TdrGuard guard;
    RTX_CHECK_EQ(guard.config().max_in_flight, static_cast<size_t>(2));
    RTX_CHECK_EQ(guard.config().max_queue_depth, static_cast<size_t>(2));
    RTX_CHECK(guard.canSubmit());
    guard.submitted();
    guard.submitted();
    RTX_CHECK(!guard.canSubmit());
    RTX_CHECK_EQ(guard.inFlight(), static_cast<size_t>(2));
    RTX_CHECK_EQ(guard.queueDepth(), static_cast<size_t>(2));
    RTX_CHECK_EQ(guard.submittedTotal(), static_cast<uint64_t>(2));
    guard.completed(std::chrono::microseconds(1000));
    RTX_CHECK(guard.canSubmit());
    RTX_CHECK_EQ(guard.inFlight(), static_cast<size_t>(1));
    RTX_CHECK_EQ(guard.queueDepth(), static_cast<size_t>(1));
    RTX_CHECK_EQ(guard.completedTotal(), static_cast<uint64_t>(1));
    RTX_CHECK_EQ(guard.budgetViolations(), static_cast<uint64_t>(0));
    guard.completed(std::chrono::microseconds(600000));
    RTX_CHECK_EQ(guard.budgetViolations(), static_cast<uint64_t>(1));
    RTX_CHECK_EQ(guard.maxFrameTime(), std::chrono::microseconds(600000));
    RTX_CHECK(guard.shouldFlushThisFrame());
    guard.completed();
    RTX_CHECK_EQ(guard.inFlight(), static_cast<size_t>(0));
    RTX_CHECK_EQ(guard.queueDepth(), static_cast<size_t>(0));
    guard.completed();
    RTX_CHECK_EQ(guard.inFlight(), static_cast<size_t>(0));
    RTX_CHECK_EQ(guard.queueDepth(), static_cast<size_t>(0));
    guard.reset();
    RTX_CHECK_EQ(guard.budgetViolations(), static_cast<uint64_t>(0));
    RTX_CHECK_EQ(guard.maxFrameTime(), std::chrono::microseconds(0));
    RTX_CHECK_EQ(guard.submittedTotal(), static_cast<uint64_t>(0));

    TdrGuard custom(TdrGuard::Config{1, 2, false, std::chrono::milliseconds(10)});
    RTX_CHECK(!custom.shouldFlushThisFrame());
    custom.submitted();
    RTX_CHECK(!custom.canSubmit());
    custom.completed(std::chrono::microseconds(11000));
    RTX_CHECK_EQ(custom.budgetViolations(), static_cast<uint64_t>(1));
}

RTX_TEST(vram_tdr_queue_depth_cap) {
    // Anti-TDR mechanism 2: the submission ceiling is the queue depth, not just
    // the frame count.  A scheduler that keeps frames in flight must still stop
    // at two unretired command-list submissions.
    TdrGuard guard(TdrGuard::Config{8, 2, true, std::chrono::milliseconds(35)});
    guard.submitted();
    RTX_CHECK(guard.canSubmit());
    guard.submitted();
    RTX_CHECK(!guard.canSubmit());
    RTX_CHECK_EQ(guard.inFlight(), static_cast<size_t>(2));
    RTX_CHECK_EQ(guard.queueDepth(), static_cast<size_t>(2));
    guard.completed(std::chrono::microseconds(500));
    RTX_CHECK(guard.canSubmit());
    RTX_CHECK_EQ(guard.queueDepth(), static_cast<size_t>(1));
}

RTX_TEST(vram_surface_pool_device_backed) {
    struct AllocatorState {
        size_t allocations = 0;
        size_t frees = 0;
        size_t bytes = 0;
        size_t requested_alignment = 0;
        DevicePtr next = 0x100000000ull;
    } state;
    DeviceAllocator allocator;
    allocator.context = &state;
    allocator.allocate = [](void* context, size_t bytes, size_t alignment) -> DevicePtr {
        auto* allocator_state = static_cast<AllocatorState*>(context);
        ++allocator_state->allocations;
        allocator_state->bytes += bytes;
        allocator_state->requested_alignment = alignment;
        const DevicePtr result = allocator_state->next;
        allocator_state->next += (bytes + alignment - 1) / alignment * alignment;
        return result;
    };
    allocator.free = [](void* context, DevicePtr pointer) {
        auto* allocator_state = static_cast<AllocatorState*>(context);
        ++allocator_state->frees;
        (void)pointer;
    };

    const PoolSpec spec = smallSpec(4);
    SurfacePool pool;
    RTX_CHECK_EQ(pool.initializeDevice(spec, DeviceAllocator{}).code(),
                 ErrorCode::InvalidArgument);
    RTX_REQUIRE(pool.initializeDevice(spec, allocator).ok());
    RTX_CHECK(pool.deviceBacked());
    RTX_CHECK_EQ(pool.capacity(), static_cast<size_t>(4));
    RTX_CHECK_EQ(state.allocations, static_cast<size_t>(4));
    RTX_CHECK_EQ(state.requested_alignment, static_cast<size_t>(256));
    RTX_CHECK_EQ(pool.deviceBytes(), spec.frameBytes() * 4);
    RTX_CHECK_EQ(pool.deviceAllocationCount(), static_cast<uint64_t>(4));

    std::vector<SurfaceLease> leases;
    for (int i = 0; i < 4; ++i) {
        SurfaceLease lease = pool.acquire();
        RTX_REQUIRE(static_cast<bool>(lease));
        const SurfaceSlot* slot = lease.slot();
        RTX_REQUIRE(slot != nullptr);
        RTX_CHECK(slot->device_resident);
        RTX_CHECK(!slot->host);
        const Frame view = lease.view();
        // Device-only slot: the host view stays empty, the device view carries
        // the two NV12 plane pointers into the single physical allocation.
        RTX_CHECK(view.planes[0] == nullptr);
        RTX_CHECK(view.device_planes[0] != kInvalidDevicePtr);
        RTX_CHECK_EQ(view.device_planes[1] - view.device_planes[0],
                     static_cast<DevicePtr>(256 * 32));
        RTX_CHECK_EQ(view.device_pitches[0], static_cast<uint32_t>(256));
        RTX_CHECK_EQ(view.device_pitches[1], static_cast<uint32_t>(256));
        leases.push_back(std::move(lease));
    }
    RTX_CHECK(pool.highWatermark());
    RTX_CHECK(pool.criticalWatermark());
    RTX_CHECK(!pool.acquire());

    leases.clear();
    pool.shutdown();
    RTX_CHECK_EQ(state.frees, static_cast<size_t>(4));
    RTX_CHECK_EQ(pool.deviceBytes(), static_cast<size_t>(0));
    RTX_CHECK_EQ(pool.deviceFreeCount(), static_cast<uint64_t>(4));
    RTX_CHECK(!pool.deviceBacked());
}

RTX_TEST(vram_watermarks_65_percent) {
    // 12-slot encode pool: pause at 8/12 (66.7%), critical at 11/12 (91.7%).
    const PoolSpec spec = smallSpec(12);
    SurfacePool pool;
    RTX_REQUIRE(pool.initialize(spec).ok());
    std::vector<SurfaceLease> leases;
    for (int i = 0; i < 7; ++i) {
        leases.push_back(pool.acquire());
        RTX_REQUIRE(static_cast<bool>(leases.back()));
    }
    RTX_CHECK(!pool.highWatermark());
    leases.push_back(pool.acquire());
    RTX_REQUIRE(static_cast<bool>(leases.back()));
    RTX_CHECK(pool.highWatermark());
    RTX_CHECK(!pool.criticalWatermark());
    leases.push_back(pool.acquire());
    leases.push_back(pool.acquire());
    leases.push_back(pool.acquire());
    RTX_CHECK_EQ(pool.used(), static_cast<size_t>(11));
    RTX_CHECK(pool.criticalWatermark());
}

