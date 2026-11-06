// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/vram_pool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <compare>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "rtx/status.h"
#include "rtx/types.h"

namespace rtx {

size_t PoolSpec::frameBytes() const {
    return frameByteSize(width, height, format);
}

std::vector<PoolSpec> defaultPoolSpecs(int b_frames) {
    return {
        {"Pool_Decode_1080p", 1920, 1080, PixelFormat::NV12, 16, 256, 0},
        {"Pool_Interp_1080p", 1920, 1088, PixelFormat::NV12, 8, 256, 64},
        {"PingPong_NGX_In", 1920, 1080, PixelFormat::R10G10B10A2, 2, 256, 128},
        {"PingPong_NGX_Out", 3840, 2160, PixelFormat::RGBA16F, 2, 256, 160},
        {"Pool_Encode_4K_P010", 3840, 2160, PixelFormat::P010, encodePoolSlots(b_frames),
         256, 384},
    };
}

int encodePoolSlots(int b_frames) noexcept {
    return std::max(12, 4 + std::max(0, b_frames));
}

std::string PoolBudget::toString() const {
    std::string out = "VRAM budget:\n";
    for (const auto& entry : entries) {
        out += std::format("  {}: {} x {} B = {} MiB\n",
                           entry.name, entry.slots, entry.frame_bytes,
                           entry.pool_bytes / (1024.0 * 1024.0));
    }
    out += std::format("  total: {} MiB\n", total_bytes / (1024.0 * 1024.0));
    return out;
}

const PoolBudgetEntry* PoolBudget::find(const std::string& name) const {
    for (const auto& entry : entries) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

PoolBudget computeBudget(const std::vector<PoolSpec>& specs) {
    PoolBudget budget;
    for (const auto& spec : specs) {
        PoolBudgetEntry entry;
        entry.name = spec.name;
        entry.frame_bytes = spec.frameBytes();
        entry.slots = spec.slots;
        entry.pool_bytes = entry.frame_bytes * static_cast<size_t>(std::max(0, spec.slots));
        budget.total_bytes += entry.pool_bytes;
        budget.entries.push_back(std::move(entry));
    }
    return budget;
}

Result<void> validateBudget(const PoolBudget& budget, size_t free_vram_bytes, size_t reserve_bytes) {
    if (std::is_eq(free_vram_bytes <=> 0)) {
        return err(ErrorCode::InvalidArgument, "free VRAM is unknown");
    }
    if (std::is_gt(budget.total_bytes <=> free_vram_bytes)) {
        return err(ErrorCode::OutOfMemory, "VRAM pool budget exceeds free device memory",
                   std::format("required={} free={}", budget.total_bytes, free_vram_bytes));
    }
    const size_t remaining = free_vram_bytes - budget.total_bytes;
    if (std::is_lt(remaining <=> reserve_bytes)) {
        return err(
            ErrorCode::OutOfMemory, "insufficient VRAM reserve after pool allocation",
            std::format("remaining={} reserve={}", remaining, reserve_bytes));
    }
    return {};
}

namespace {
constexpr size_t kDeviceAlignment = 256;
}

SurfaceLease::SurfaceLease(SurfacePool* pool, SlotHandle slot) : pool_(pool), slot_(slot) {}

SurfaceLease::SurfaceLease(SurfaceLease&& other) noexcept
    : pool_(other.pool_), slot_(other.slot_) {
    other.pool_ = nullptr;
    other.slot_ = SlotHandle::invalid();
}

SurfaceLease& SurfaceLease::operator=(SurfaceLease&& other) noexcept {
    if (this != &other) {
        reset();
        pool_ = other.pool_;
        slot_ = other.slot_;
        other.pool_ = nullptr;
        other.slot_ = SlotHandle::invalid();
    }
    return *this;
}

SurfaceLease::~SurfaceLease() {
    reset();
}

void SurfaceLease::reset() {
    if (pool_ != nullptr) {
        pool_->release(slot_);
        pool_ = nullptr;
        slot_ = SlotHandle::invalid();
    }
}

SurfaceSlot* SurfaceLease::slot() const noexcept {
    return pool_ != nullptr ? pool_->slot(slot_) : nullptr;
}

Frame SurfaceLease::view() const {
    Frame frame;
    SurfaceSlot* surface = slot();
    if (surface == nullptr || (!surface->host && !surface->device_resident)) {
        return frame;
    }
    frame.width = surface->width;
    frame.height = surface->height;
    frame.format = surface->format;
    frame.bit_depth = isTenBit(surface->format) ? 10 : 8;
    frame.chroma_width = (surface->width + 1) / 2;
    frame.chroma_height = (surface->height + 1) / 2;
    int pitches[3] = {0, 0, 0};
    frameByteSize(surface->width, surface->height, surface->format, pitches);
    const int planes = planeCount(surface->format);
    for (int plane = 0; plane < planes; ++plane) {
        frame.pitches[plane] = pitches[plane];
        frame.plane_sizes[plane] =
            planeByteSize(surface->format, surface->width, surface->height, plane, pitches[plane]);
    }
    if (surface->host) {
        uint8_t* base = surface->host->data();
        frame.planes[0] = base;
        if (planes >= 2) {
            frame.planes[1] = base + frame.plane_sizes[0];
        }
        if (planes >= 3) {
            frame.planes[2] = frame.planes[1] + frame.plane_sizes[1];
        }
    }
    if (surface->device_resident) {
        // The device view and the host view describe the same slot; there is
        // no second allocation behind either pointer.
        frame.device_planes[0] = surface->device_ptr;
        frame.device_pitches[0] = static_cast<uint32_t>(pitches[0]);
        if (planes >= 2) {
            frame.device_planes[1] = surface->device_ptr + frame.plane_sizes[0];
            frame.device_pitches[1] = static_cast<uint32_t>(pitches[1]);
        }
        if (planes >= 3) {
            frame.device_planes[2] = frame.device_planes[1] + frame.plane_sizes[1];
            frame.device_pitches[2] = static_cast<uint32_t>(pitches[2]);
        }
    }
    frame.fence_value = surface->fence_value;
    return frame;
}

SurfacePool::~SurfacePool() {
    shutdown();
}

Result<void> SurfacePool::initialize(const PoolSpec& spec, bool simulate_device_memory) {
    shutdown();
    if (std::is_lteq(spec.slots <=> 0) || std::is_lteq(spec.width <=> 0) ||
        std::is_lteq(spec.height <=> 0) || spec.format == PixelFormat::Unknown) {
        return err(ErrorCode::InvalidArgument, "invalid surface pool specification",
                   spec.name);
    }
    const size_t frame_bytes = spec.frameBytes();
    if (std::is_eq(frame_bytes <=> 0)) {
        return err(ErrorCode::InvalidArgument, "unsupported surface pool format",
                   spec.name);
    }

    spec_ = spec;
    slots_.reserve(static_cast<size_t>(spec.slots));
    free_list_ = std::make_unique<MpmcBoundedQueue<SlotHandle>>(
        static_cast<size_t>(spec.slots) * 2ull);
    if (!free_list_->valid()) {
        return err(ErrorCode::OutOfMemory, "failed to allocate pool free list");
    }

    DevicePtr next_device = simulated_base_ +
                            static_cast<DevicePtr>(spec.simulated_device_base_mb) * 1024ull * 1024ull;
    for (int i = 0; i < spec.slots; ++i) {
        auto slot = std::make_unique<SurfaceSlot>();
        slot->id = SlotHandle(static_cast<uint32_t>(i));
        slot->width = spec.width;
        slot->height = spec.height;
        slot->format = spec.format;
        slot->allocation_bytes = frame_bytes;
        slot->host = std::make_unique<FrameBuffer>();
        const Result<void> allocation = slot->host->allocate(frame_bytes, spec.alignment);
        if (!allocation.has_value()) {
            shutdown();
            return err(allocation.error().code, "surface allocation failed",
                       std::format("{} slot {}", spec.name, i));
        }
        if (simulate_device_memory) {
            slot->device_ptr = next_device;
            next_device += static_cast<DevicePtr>(alignUp(frame_bytes, 2ull * 1024 * 1024));
        }
        slots_.push_back(std::move(slot));
        if (!free_list_->tryPush(SlotHandle(static_cast<uint32_t>(i)))) {
            shutdown();
            return err(ErrorCode::Internal, "free list initialisation failed");
        }
    }

    used_.store(0, std::memory_order_relaxed);
    acquire_count_.store(0, std::memory_order_relaxed);
    release_count_.store(0, std::memory_order_relaxed);
    empty_count_.store(0, std::memory_order_relaxed);
    high_slots_ = static_cast<size_t>(std::ceil(spec.slots * 0.65));
    critical_slots_ = static_cast<size_t>(std::ceil(spec.slots * 0.90));
    initialized_.store(true, std::memory_order_release);
    return {};
}

Result<void> SurfacePool::initializeDevice(const PoolSpec& spec, DeviceAllocator allocator,
                                     bool allocate_host_view) {
    shutdown();
    if (std::is_lteq(spec.slots <=> 0) || std::is_lteq(spec.width <=> 0) ||
        std::is_lteq(spec.height <=> 0) || spec.format == PixelFormat::Unknown) {
        return err(ErrorCode::InvalidArgument, "invalid surface pool specification",
                   spec.name);
    }
    if (allocator.allocate == nullptr || allocator.free == nullptr) {
        return err(ErrorCode::InvalidArgument,
                   "device-backed pool requires a working allocator", spec.name);
    }
    const size_t frame_bytes = spec.frameBytes();
    if (std::is_eq(frame_bytes <=> 0)) {
        return err(ErrorCode::InvalidArgument, "unsupported surface pool format",
                   spec.name);
    }

    spec_ = spec;
    device_allocator_ = allocator;
    device_backed_ = true;
    device_bytes_.store(0, std::memory_order_relaxed);
    slots_.reserve(static_cast<size_t>(spec.slots));
    free_list_ = std::make_unique<MpmcBoundedQueue<SlotHandle>>(
        static_cast<size_t>(spec.slots) * 2ull);
    if (!free_list_->valid()) {
        shutdown();
        return err(ErrorCode::OutOfMemory, "failed to allocate pool free list");
    }

    const size_t alignment = std::max(spec.alignment, kDeviceAlignment);
    for (int i = 0; i < spec.slots; ++i) {
        auto slot = std::make_unique<SurfaceSlot>();
        slot->id = SlotHandle(static_cast<uint32_t>(i));
        slot->width = spec.width;
        slot->height = spec.height;
        slot->format = spec.format;
        slot->allocation_bytes = frame_bytes;
        slot->device_ptr = allocator.allocate(allocator.context, frame_bytes, alignment);
        if (slot->device_ptr == kInvalidDevicePtr) {
            shutdown();
            return err(ErrorCode::OutOfMemory,
                       "device allocation failed for pool slot",
                       std::format("{} slot {}", spec.name, i));
        }
        slot->device_resident = true;
        device_bytes_.fetch_add(frame_bytes, std::memory_order_relaxed);
        device_allocations_.fetch_add(1, std::memory_order_relaxed);
        // Publish the slot before any further failure point so shutdown() can
        // release the physical allocation.
        slots_.push_back(std::move(slot));
        if (allocate_host_view) {
            slots_.back()->host = std::make_unique<FrameBuffer>();
            const Result<void> allocation = slots_.back()->host->allocate(frame_bytes, spec.alignment);
            if (!allocation.has_value()) {
                shutdown();
                return err(allocation.error().code, "staging view allocation failed",
                           std::format("{} slot {}", spec.name, i));
            }
        }
        if (!free_list_->tryPush(SlotHandle(static_cast<uint32_t>(i)))) {
            shutdown();
            return err(ErrorCode::Internal, "free list initialisation failed");
        }
    }

    used_.store(0, std::memory_order_relaxed);
    acquire_count_.store(0, std::memory_order_relaxed);
    release_count_.store(0, std::memory_order_relaxed);
    empty_count_.store(0, std::memory_order_relaxed);
    high_slots_ = static_cast<size_t>(std::ceil(spec.slots * 0.65));
    critical_slots_ = static_cast<size_t>(std::ceil(spec.slots * 0.90));
    initialized_.store(true, std::memory_order_release);
    return {};
}

void SurfacePool::shutdown() {
    initialized_.store(false, std::memory_order_release);
    // Deterministic reverse-order destruction of device allocations
    for (auto it = slots_.rbegin(); it != slots_.rend(); ++it) {
        auto& slot = *it;
        if (slot && slot->device_resident && device_allocator_.free != nullptr) {
            device_allocator_.free(device_allocator_.context, slot->device_ptr);
            slot->device_resident = false;
            slot->device_ptr = kInvalidDevicePtr;
            device_frees_.fetch_add(1, std::memory_order_relaxed);
            const size_t bytes = slot->allocation_bytes;
            const size_t current = device_bytes_.load(std::memory_order_relaxed);
            device_bytes_.store(std::is_gteq(current <=> bytes) ? current - bytes : 0,
                                std::memory_order_relaxed);
        }
    }
    device_allocator_ = {};
    device_backed_ = false;
    free_list_.reset();
    slots_.clear();
    used_.store(0, std::memory_order_relaxed);
}

SurfaceLease SurfacePool::acquire() {
    if (!isInitialized() || !free_list_) {
        return {};
    }
    SlotHandle id{};
    if (!free_list_->tryPop(id)) {
        empty_count_.fetch_add(1, std::memory_order_relaxed);
        return {};
    }
    SurfaceSlot* surface = slot(id);
    if (surface == nullptr) {
        return {};
    }
    surface->state.store(1, std::memory_order_release);
    used_.fetch_add(1, std::memory_order_acq_rel);
    acquire_count_.fetch_add(1, std::memory_order_relaxed);
    return SurfaceLease(this, id);
}

void SurfacePool::release(SlotHandle slot_id) {
    if (!isInitialized() || !slot_id.isValid() ||
        std::is_gteq(static_cast<size_t>(slot_id.v) <=> slots_.size())) {
        return;
    }
    SurfaceSlot* surface = slot(slot_id);
    if (surface == nullptr) {
        return;
    }
    const uint32_t previous = surface->state.exchange(0, std::memory_order_acq_rel);
    if (std::is_eq(previous <=> 0)) {
        return;  // double release: ignore rather than corrupt the free list
    }
    used_.fetch_sub(1, std::memory_order_acq_rel);
    release_count_.fetch_add(1, std::memory_order_relaxed);
    while (!free_list_->tryPush(slot_id)) [[unlikely]] {
        std::this_thread::yield();
    }
    if (release_callback_) {
        release_callback_();
    }
}

double SurfacePool::occupancy() const noexcept {
    if (std::is_lteq(spec_.slots <=> 0)) {
        return 0.0;
    }
    return static_cast<double>(used()) / static_cast<double>(spec_.slots);
}

bool SurfacePool::highWatermark() const noexcept {
    return std::is_gteq(used() <=> high_slots_);
}

bool SurfacePool::criticalWatermark() const noexcept {
    return std::is_gteq(used() <=> critical_slots_);
}

SurfaceSlot* SurfacePool::slot(SlotHandle id) noexcept {
    return (id.isValid() && std::is_lt(static_cast<size_t>(id.v) <=> slots_.size()))
               ? slots_[id.v].get()
               : nullptr;
}

const SurfaceSlot* SurfacePool::slot(SlotHandle id) const noexcept {
    return (id.isValid() && std::is_lt(static_cast<size_t>(id.v) <=> slots_.size()))
               ? slots_[id.v].get()
               : nullptr;
}

BackpressureController::BackpressureController(WatermarkLevels levels) : levels_(levels) {}

void BackpressureController::update(size_t used_slots, size_t capacity) {
    occupancy_ = std::is_gt(capacity <=> 0)
                     ? static_cast<double>(used_slots) / static_cast<double>(capacity)
                     : 0.0;
    const bool now_critical = std::is_gteq(occupancy_ <=> levels_.critical);
    if (now_critical && !critical_) {
        ++critical_count_;
    }
    critical_ = now_critical;

    if (!paused_ && std::is_gteq(occupancy_ <=> levels_.high)) {
        paused_ = true;
        ++pause_count_;
    } else if (paused_ && std::is_lteq(occupancy_ <=> levels_.low)) {
        paused_ = false;
        ++resume_count_;
    }
}

void BackpressureController::reset() {
    paused_ = false;
    critical_ = false;
    occupancy_ = 0.0;
    pause_count_ = 0;
    resume_count_ = 0;
    critical_count_ = 0;
    vram_tier_ = WatermarkTier::Low;
    low_stable_counter_ = 0;
    emergency_fallbacks_ = 0;
    restores_ = 0;
}

const char* watermarkTierName(WatermarkTier tier) noexcept {
    switch (tier) {
        case WatermarkTier::Low:
            return "Low";
        case WatermarkTier::High:
            return "High";
        case WatermarkTier::Critical:
            return "Critical";
    }
    return "Unknown";
}

void BackpressureController::updateVramState(uint64_t used_bytes, uint64_t total_bytes) {
    if (std::is_eq(total_bytes <=> 0)) {
        return;
    }
    const double ratio = static_cast<double>(used_bytes) / static_cast<double>(total_bytes);

    if (std::is_eq(vram_tier_ <=> WatermarkTier::Low)) {
        // Enter backpressure earlier to preserve WDDM 3.2 mobile-GPU headroom.
        if (std::is_gteq(ratio <=> 0.70)) {
            vram_tier_ = WatermarkTier::High;
        }
    } else if (std::is_eq(vram_tier_ <=> WatermarkTier::High)) {
        // WDDM 3.2 can page out resources near 83%; degrade at 82%.
        if (std::is_gteq(ratio <=> 0.82)) {
            vram_tier_ = WatermarkTier::Critical;
            low_stable_counter_ = 0;
            ++emergency_fallbacks_;
            // Unload the TensorRT RIFE engine and hot-degrade to NvOFFRUC:
            // this releases > 1.0 GB inside the current frame boundary
            // (FMEA-01, < 50 ms).
            if (emergency_fallback_) {
                emergency_fallback_();
            }
        } else if (std::is_lt(ratio <=> 0.55)) {
            vram_tier_ = WatermarkTier::Low;
        }
    } else if (std::is_eq(vram_tier_ <=> WatermarkTier::Critical)) {
        if (std::is_lt(ratio <=> 0.55)) {
            // 300 consecutive samples at 60 Hz is the documented 5 s of
            // stable headroom before the preferred mode is restored.
            if (std::is_lt(low_stable_counter_ <=> 300u)) {
                ++low_stable_counter_;
            }
            if (std::is_gteq(low_stable_counter_ <=> 300u)) {
                vram_tier_ = WatermarkTier::Low;
                low_stable_counter_ = 0;
                ++restores_;
                if (restore_preferred_) {
                    restore_preferred_();
                }
            }
        } else {
            low_stable_counter_ = 0;
        }
    }
}

void TdrGuard::completed(std::chrono::microseconds elapsed) noexcept {
    size_t current = in_flight_.load(std::memory_order_relaxed);
    while (std::is_gt(current <=> 0) &&
           !in_flight_.compare_exchange_weak(current, current - 1, std::memory_order_acq_rel)) {
    }
    completed_total_.fetch_add(1, std::memory_order_acq_rel);
    const int64_t elapsed_us = elapsed.count();
    int64_t prev_max = max_frame_time_us_.load(std::memory_order_relaxed);
    while (elapsed_us > prev_max &&
           !max_frame_time_us_.compare_exchange_weak(
               prev_max, elapsed_us, std::memory_order_relaxed)) {
    }
    if (std::is_gt(elapsed <=> std::chrono::duration_cast<std::chrono::microseconds>(config_.frame_budget))) {
        budget_violations_.fetch_add(1, std::memory_order_acq_rel);
    }
}

}  // namespace rtx
