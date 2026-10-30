// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Deterministic VRAM pool and backpressure machinery for 8 GB, 128-bit cards.
// Allocations happen exclusively during initialisation; the hot loop only
// moves slot indices through lock-free free lists.

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "rtx/spsc_queue.h"
#include "rtx/status.h"
#include "rtx/types.h"

namespace rtx {

struct PoolSpec {
    std::string name;
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::Unknown;
    int slots = 0;
    size_t alignment = 256;
    int simulated_device_base_mb = 0;

    size_t frameBytes() const;
};

// A hierarchical-B-frame session holds N decoded reference surfaces beyond the
// 4-deep decode/encode handoff, so the encode ring must be at least 4 + N.
// 12 remains the pipeline floor for the current IPP session; the returned
// depth is the larger of the two.
int encodePoolSlots(int b_frames) noexcept;
std::vector<PoolSpec> defaultPoolSpecs(int b_frames = 0);

struct PoolBudgetEntry {
    std::string name;
    size_t frame_bytes = 0;
    int slots = 0;
    size_t pool_bytes = 0;
};

struct PoolBudget {
    std::vector<PoolBudgetEntry> entries;
    size_t total_bytes = 0;

    std::string toString() const;
    const PoolBudgetEntry* find(const std::string& name) const;
};

PoolBudget computeBudget(const std::vector<PoolSpec>& specs);

// Rejects a plan that cannot coexist with the driver's reported free memory,
// keeping a reserve for the driver, NGX scratch buffers and the D3D12 heap.
Status validateBudget(const PoolBudget& budget, size_t free_vram_bytes,
                      size_t reserve_bytes = 384ull * 1024 * 1024);

// Bridge between the pool and the CUDA driver.  The core library never links
// cudart, so the NVIDIA shim supplies cuMemAlloc/cuMemFree through this table.
// A pool initialised this way owns the physical device allocation for every
// slot: there is no second host copy and no simulated pointer.
struct DeviceAllocator {
    void* context = nullptr;
    DevicePtr (*allocate)(void* context, size_t bytes, size_t alignment) = nullptr;
    void (*free)(void* context, DevicePtr pointer) = nullptr;
};

struct SurfaceSlot {
    uint32_t id = 0;
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::Unknown;
    // Host backing exists only for the CPU reference/tests.  A production
    // device-backed slot leaves this null.
    std::unique_ptr<FrameBuffer> host;
    DevicePtr device_ptr = kInvalidDevicePtr;
    size_t allocation_bytes = 0;
    bool device_resident = false;
    uint64_t fence_value = 0;
    std::atomic<uint32_t> state{0};  // 0 = free, 1 = leased
};

class SurfacePool;

class SurfaceLease {
public:
    SurfaceLease() = default;
    SurfaceLease(SurfacePool* pool, uint32_t slot);
    SurfaceLease(const SurfaceLease&) = delete;
    SurfaceLease& operator=(const SurfaceLease&) = delete;
    SurfaceLease(SurfaceLease&& other) noexcept;
    SurfaceLease& operator=(SurfaceLease&& other) noexcept;
    ~SurfaceLease();

    explicit operator bool() const noexcept { return pool_ != nullptr; }
    uint32_t slotId() const noexcept { return slot_; }
    SurfaceSlot* slot() const noexcept;
    Frame view() const;

    void reset();

private:
    SurfacePool* pool_ = nullptr;
    uint32_t slot_ = 0;
};

class SurfacePool {
public:
    SurfacePool() = default;
    ~SurfacePool();

    SurfacePool(const SurfacePool&) = delete;
    SurfacePool& operator=(const SurfacePool&) = delete;

    // CPU/reference initialisation: host slots plus synthetic device pointers
    // when `simulate_device_memory` is true.
    Status initialize(const PoolSpec& spec, bool simulate_device_memory = true);
    // Production initialisation: every slot is one physical cuMemAlloc
    // allocation supplied by `allocator`.  No host buffer is allocated unless
    // `allocate_host_view` explicitly asks for a staging view (test-only).
    Status initializeDevice(const PoolSpec& spec, DeviceAllocator allocator,
                            bool allocate_host_view = false);
    void shutdown();

    SurfaceLease acquire();
    void release(uint32_t slot_id);

    const PoolSpec& spec() const noexcept { return spec_; }
    size_t capacity() const noexcept { return initialized_ ? static_cast<size_t>(spec_.slots) : 0; }
    size_t used() const noexcept { return used_.load(std::memory_order_relaxed); }
    size_t available() const noexcept { return capacity() - used(); }
    double occupancy() const noexcept;
    bool highWatermark() const noexcept;
    bool criticalWatermark() const noexcept;
    bool deviceBacked() const noexcept { return device_backed_; }
    size_t deviceBytes() const noexcept { return device_bytes_.load(std::memory_order_relaxed); }

    uint64_t acquireCount() const noexcept { return acquire_count_.load(std::memory_order_relaxed); }
    uint64_t releaseCount() const noexcept { return release_count_.load(std::memory_order_relaxed); }
    uint64_t emptyCount() const noexcept { return empty_count_.load(std::memory_order_relaxed); }
    uint64_t deviceAllocationCount() const noexcept {
        return device_allocations_.load(std::memory_order_relaxed);
    }
    uint64_t deviceFreeCount() const noexcept {
        return device_frees_.load(std::memory_order_relaxed);
    }

    // Called on the consumer side after a slot is released.  May be used to
    // wake a backpressured producer.
    void setReleaseCallback(std::function<void()> callback) { release_callback_ = std::move(callback); }

    SurfaceSlot* slot(uint32_t id) noexcept;
    const SurfaceSlot* slot(uint32_t id) const noexcept;

private:
    friend class SurfaceLease;

    PoolSpec spec_{};
    std::vector<std::unique_ptr<SurfaceSlot>> slots_;
    std::unique_ptr<SpscBoundedQueue<uint32_t>> free_list_;
    mutable std::mutex pool_mutex_;
    std::atomic<size_t> used_{0};
    std::atomic<uint64_t> acquire_count_{0};
    std::atomic<uint64_t> release_count_{0};
    std::atomic<uint64_t> empty_count_{0};
    size_t high_slots_ = 0;
    size_t critical_slots_ = 0;
    DevicePtr simulated_base_ = 0x7f0000000000ull;
    DeviceAllocator device_allocator_{};
    std::atomic<size_t> device_bytes_{0};
    std::atomic<uint64_t> device_allocations_{0};
    std::atomic<uint64_t> device_frees_{0};
    bool device_backed_ = false;
    bool initialized_ = false;
    std::function<void()> release_callback_;
};

struct WatermarkLevels {
    double low = 0.25;
    // Phase-2 expert verdict 2.5: the 12-slot encode pool must pause decode
    // when eight slots are occupied (65%), not at the old 75% mark, so a
    // 8 GB mobile GPU keeps a 1.7 GB driver/NGX reserve.
    double high = 0.65;
    double critical = 0.90;
};

// Three-tier VRAM watermark used by the phase-3 backpressure controller.  The
// tiers are a hysteresis state machine, not thresholds on a single reading:
// Low -> High at 75 %, High -> Critical at 88 %, and every fallback to Low
// requires 55 % or less for 300 consecutive samples (about 5 s at 60 Hz).
enum class WatermarkTier : uint8_t { Low = 0, High = 1, Critical = 2 };

const char* watermarkTierName(WatermarkTier tier) noexcept;

// Hysteresis-based backpressure controller.  Pauses decode at the high
// watermark and only resumes at the low watermark, preventing queue flap.
class BackpressureController {
public:
    explicit BackpressureController(WatermarkLevels levels = {});

    void update(size_t used_slots, size_t capacity);
    bool paused() const noexcept { return paused_; }
    bool critical() const noexcept { return critical_; }
    double occupancy() const noexcept { return occupancy_; }
    uint64_t pauseCount() const noexcept { return pause_count_; }
    uint64_t resumeCount() const noexcept { return resume_count_; }
    uint64_t criticalCount() const noexcept { return critical_count_; }
    void reset();

    // ------------------------------------------------------------------
    // Phase-3 three-tier VRAM watermark (W8-3)
    //
    // Fed with the driver's free/total VRAM at the frame boundary.  Entering
    // Critical fires the emergency fallback (unload the TensorRT RIFE engine
    // and hot-degrade to NvOFFRUC, which releases > 1.0 GB); returning to Low
    // fires the restore callback only after the level has stayed below 55 %
    // for 300 consecutive samples.
    // ------------------------------------------------------------------
    void updateVramState(uint64_t used_bytes, uint64_t total_bytes);
    WatermarkTier vramTier() const noexcept { return vram_tier_; }
    void setEmergencyFallback(std::function<void()> callback) {
        emergency_fallback_ = std::move(callback);
    }
    void setRestorePreferred(std::function<void()> callback) {
        restore_preferred_ = std::move(callback);
    }
    uint64_t emergencyFallbackCount() const noexcept { return emergency_fallbacks_; }
    uint64_t restoreCount() const noexcept { return restores_; }

private:
    WatermarkLevels levels_;
    bool paused_ = false;
    bool critical_ = false;
    double occupancy_ = 0.0;
    uint64_t pause_count_ = 0;
    uint64_t resume_count_ = 0;
    uint64_t critical_count_ = 0;

    WatermarkTier vram_tier_ = WatermarkTier::Low;
    uint32_t low_stable_counter_ = 0;
    uint64_t emergency_fallbacks_ = 0;
    uint64_t restores_ = 0;
    std::function<void()> emergency_fallback_;
    std::function<void()> restore_preferred_;
};

// Monotonic 64-bit timeline shared by the CUDA external semaphore and the
// ID3D12Fence.  The producer increments; the consumer waits by value.
class FenceTimeline {
public:
    explicit FenceTimeline(uint64_t initial = 0) : value_(initial) {}

    uint64_t current() const noexcept { return value_.load(std::memory_order_acquire); }
    uint64_t next() noexcept { return value_.fetch_add(1, std::memory_order_acq_rel) + 1; }

    // CPU-side simulation of a GPU wait.  On the NVIDIA path this is replaced
    // by cuWaitExternalSemaphoresAsync / ID3D12CommandQueue::Wait.
    void signal(uint64_t value) noexcept {
        uint64_t current_value = value_.load(std::memory_order_relaxed);
        while (current_value < value &&
               !value_.compare_exchange_weak(current_value, value, std::memory_order_release,
                                             std::memory_order_relaxed)) {
        }
    }

private:
    std::atomic<uint64_t> value_;
};

// Prevents the 2-second Windows TDR threshold from being hit by limiting the
// number of unflushed command lists and in-flight frames.
class TdrGuard {
public:
    struct Config {
        // One frame in flight is the hardware pre-emption point; two is the
        // hard ceiling for a single command queue on WDDM.
        size_t max_in_flight = 2;
        size_t max_queue_depth = 2;
        bool flush_each_frame = true;
        // A frame on the D3D12 queue must retire well before the 2 s WDDM
        // timeout; 35 ms bounds the queue backlog at ~2 frames of 60 fps work.
        std::chrono::milliseconds frame_budget{35};
    };

    TdrGuard() = default;
    explicit TdrGuard(Config config) : config_(config) {}

    bool canSubmit() const noexcept {
        return in_flight_.load(std::memory_order_acquire) < config_.max_in_flight &&
               queueDepth() < config_.max_queue_depth;
    }
    void submitted() noexcept {
        in_flight_.fetch_add(1, std::memory_order_acq_rel);
        submitted_total_.fetch_add(1, std::memory_order_acq_rel);
    }
    void completed(std::chrono::microseconds elapsed) noexcept;
    void completed() noexcept {
        size_t current = in_flight_.load(std::memory_order_relaxed);
        while (current > 0 &&
               !in_flight_.compare_exchange_weak(current, current - 1, std::memory_order_acq_rel)) {
        }
        completed_total_.fetch_add(1, std::memory_order_acq_rel);
    }

    size_t inFlight() const noexcept { return in_flight_; }
    // submitted - retired, i.e. the number of command lists whose completion
    // has not been observed yet.  The scheduler compares this against the
    // hardware queue-depth cap before every submission.
    size_t queueDepth() const noexcept {
        return submitted_total_ > completed_total_
                   ? static_cast<size_t>(submitted_total_ - completed_total_)
                   : 0;
    }
    uint64_t submittedTotal() const noexcept { return submitted_total_; }
    uint64_t completedTotal() const noexcept { return completed_total_; }
    const Config& config() const noexcept { return config_; }
    bool shouldFlushThisFrame() const noexcept { return config_.flush_each_frame; }
    uint64_t budgetViolations() const noexcept { return budget_violations_; }
    std::chrono::microseconds maxFrameTime() const noexcept { return max_frame_time_; }
    void reset() noexcept {
        in_flight_ = 0;
        submitted_total_ = 0;
        completed_total_ = 0;
        budget_violations_ = 0;
        max_frame_time_ = std::chrono::microseconds{0};
    }

private:
    Config config_;
    std::atomic<size_t> in_flight_{0};
    std::atomic<uint64_t> submitted_total_{0};
    std::atomic<uint64_t> completed_total_{0};
    std::atomic<uint64_t> budget_violations_{0};
    std::chrono::microseconds max_frame_time_{0};
};

}  // namespace rtx
