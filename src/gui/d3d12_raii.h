// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// COM / D3D12 域所有权与 GPU Fence 异步退役机制（Phase 11 / Track 11A / Wave 12.2-D）。
// COM 智能指针统一收口 rtx::ComPtr；退役条目、退役队列与延迟退役池全部改用 std::tuple 与自由函数。
//
// 铁律：任何被 GPU 命令引用过的资源，在 Unmap / Release / ResizeBuffers 之前
// 必须确认其绑定 Fence 值真正完成。WaitForSingleObject 返回 WAIT_TIMEOUT 时
// 一律不得强拆——健康设备走延迟退役池，已移除设备走受控 Device-Lost 路径。

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d12.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <tuple>
#include <utility>
#include <vector>

#include "rtx/raii_owners.h"
#include "rtx/status.h"

namespace rtx::gui {

// ---- COM 域 ---------------------------------------------------------------
using ::rtx::ComPtr;

// ---- GPU Fence 异步退役 ---------------------------------------------------
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
using CompletedProbe = std::move_only_function<std::uint64_t()>;
using ReleaseHook = std::move_only_function<void()>;
#else
using CompletedProbe = std::function<std::uint64_t()>;
using ReleaseHook = std::function<void()>;
#endif

// 退役条目：<0> 绑定 Fence 值、<1> 拥有的引用计数、<2> 释放前钩子（Unmap）。
using GpuRetirementEntry = std::tuple<std::uint64_t, ComPtr<IUnknown>, ReleaseHook>;

// Phase 12 (Wave 12.2-D): standard tuple alias replaces GpuRetirementQueue wrapper.
// Layout: (window_keepalive, event_keepalive, completed_probe, fence_keepalive, device_lost_seen, entries).
using GpuRetirementQueue = std::tuple<
    rtx::UniqueHwnd,
    rtx::UniqueHandle,
    CompletedProbe,
    ComPtr<IUnknown>,
    bool,
    std::vector<GpuRetirementEntry>>;

[[nodiscard]] inline GpuRetirementQueue makeGpuRetirementQueue(CompletedProbe probe = {}) noexcept {
    GpuRetirementQueue q{};
    auto& [win, ev, prb, ka, lost, entries] = q;
    (void)win; (void)ev; (void)ka; (void)lost; (void)entries;
    prb = std::move(probe);
    return q;
}

inline void gpuSetProbe(GpuRetirementQueue& q, CompletedProbe probe) noexcept {
    auto& [win, ev, prb, ka, lost, entries] = q;
    (void)win; (void)ev; (void)ka; (void)lost; (void)entries;
    prb = std::move(probe);
}
inline void gpuSetKeepalive(GpuRetirementQueue& q, ComPtr<IUnknown> fence) noexcept {
    auto& [win, ev, prb, ka, lost, entries] = q;
    (void)win; (void)ev; (void)prb; (void)lost; (void)entries;
    ka = std::move(fence);
}
inline void gpuSetEventKeepalive(GpuRetirementQueue& q, rtx::UniqueHandle event) noexcept {
    auto& [win, ev, prb, ka, lost, entries] = q;
    (void)win; (void)prb; (void)ka; (void)lost; (void)entries;
    ev = std::move(event);
}
inline void gpuSetWindowKeepalive(GpuRetirementQueue& q, rtx::UniqueHwnd window) noexcept {
    auto& [win, ev, prb, ka, lost, entries] = q;
    (void)ev; (void)prb; (void)ka; (void)lost; (void)entries;
    win = std::move(window);
}

inline void gpuRetire(GpuRetirementQueue& q, std::uint64_t fence_value, IUnknown* object,
                      ReleaseHook before_release = {}) noexcept {
    if (object == nullptr) return;
    auto& [win, ev, prb, ka, lost, entries] = q;
    (void)win; (void)ev; (void)prb; (void)ka; (void)lost;
    ComPtr<IUnknown> owned;
    owned.reset(object);
    entries.emplace_back(fence_value, std::move(owned), std::move(before_release));
}

template <typename T>
inline void gpuRetire(GpuRetirementQueue& q, std::uint64_t fence_value, ComPtr<T>&& owner,
                      ReleaseHook before_release = {}) noexcept {
    gpuRetire(q, fence_value, static_cast<IUnknown*>(owner.release()), std::move(before_release));
}

inline void gpuReleaseAll(GpuRetirementQueue& q) noexcept {
    auto& [win, ev, prb, ka, lost, entries] = q;
    (void)win; (void)ev; (void)prb; (void)ka; (void)lost;
    for (auto& [fence_val, obj, hook] : entries) {
        (void)fence_val; (void)obj;
        if (hook) hook();
    }
    entries.clear();
}

// 释放所有 Fence 已达标的条目，返回释放个数。
// Phase 11.2 (C-03)：GetCompletedValue() == UINT64_MAX 是 D3D12 设备移除后的假完成信号。
inline std::size_t gpuCollect(GpuRetirementQueue& q, std::uint64_t completed_value) noexcept {
    auto& [win, ev, prb, ka, lost, entries] = q;
    (void)win; (void)ev; (void)prb; (void)ka;
    if (completed_value == UINT64_MAX) {
        lost = true;
        gpuReleaseAll(q);
        return 0;
    }
    return std::erase_if(entries, [completed_value](GpuRetirementEntry& entry) noexcept {
        auto& [fence_val, obj, before_release] = entry;
        (void)obj;
        if (fence_val <= completed_value) {
            if (before_release) before_release();
            return true;
        }
        return false;
    });
}

inline std::size_t gpuCollect(GpuRetirementQueue& q) noexcept {
    auto& [win, ev, prb, ka, lost, entries] = q;
    (void)win; (void)ev; (void)ka; (void)lost; (void)entries;
    if (!prb) return 0;
    return gpuCollect(q, prb());
}

[[nodiscard]] inline bool gpuDeviceLostSeen(const GpuRetirementQueue& q) noexcept {
    const auto& [win, ev, prb, ka, lost, entries] = q;
    (void)win; (void)ev; (void)prb; (void)ka; (void)entries;
    return lost;
}
inline void gpuResetDeviceLostSeen(GpuRetirementQueue& q) noexcept {
    auto& [win, ev, prb, ka, lost, entries] = q;
    (void)win; (void)ev; (void)prb; (void)ka; (void)entries;
    lost = false;
}
[[nodiscard]] inline std::size_t gpuPending(const GpuRetirementQueue& q) noexcept {
    const auto& [win, ev, prb, ka, lost, entries] = q;
    (void)win; (void)ev; (void)prb; (void)ka; (void)lost;
    return entries.size();
}
[[nodiscard]] inline bool gpuEmpty(const GpuRetirementQueue& q) noexcept {
    const auto& [win, ev, prb, ka, lost, entries] = q;
    (void)win; (void)ev; (void)prb; (void)ka; (void)lost;
    return entries.empty();
}

inline std::mutex& deferredPoolMutex() noexcept {
    static std::mutex mutex;
    return mutex;
}
inline std::vector<GpuRetirementQueue>& deferredPool() noexcept {
    static std::vector<GpuRetirementQueue> pool;
    return pool;
}
inline void deferRetirement(GpuRetirementQueue&& queue) noexcept {
    std::lock_guard<std::mutex> lock(deferredPoolMutex());
    deferredPool().push_back(std::move(queue));
}
inline std::size_t drainDeferredRetirement() noexcept {
    std::lock_guard<std::mutex> lock(deferredPoolMutex());
    std::size_t released = 0;
    auto& pool = deferredPool();
    for (std::size_t i = 0; i < pool.size();) {
        released += gpuCollect(pool[i]);
        if (gpuEmpty(pool[i])) {
            pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    return released;
}
inline std::size_t deferredRetirementPending() noexcept {
    std::lock_guard<std::mutex> lock(deferredPoolMutex());
    std::size_t total = 0;
    for (const auto& q : deferredPool()) total += gpuPending(q);
    return total;
}

using MappedUploadResource = std::pair<ComPtr<ID3D12Resource>, void*>;

// ---- 持久映射上传堆 -------------------------------------------------------
[[nodiscard]] inline Result<MappedUploadResource> createMappedUploadResource(
    ID3D12Device* device, std::uint64_t bytes) {
    if (device == nullptr || bytes == 0) {
        return err(ErrorCode::InvalidArgument, "mapped upload resource needs a device and a size");
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    MappedUploadResource out{};
    ID3D12Resource* raw = nullptr;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               __uuidof(ID3D12Resource),
                                               reinterpret_cast<void**>(&raw)))) {
        return err(ErrorCode::OutOfMemory, "cannot create the mapped upload resource");
    }
    out.first.reset(raw);
    D3D12_RANGE read_range{0, 0};
    if (FAILED(out.first->Map(0, &read_range, &out.second)) || out.second == nullptr) {
        return err(ErrorCode::Internal, "cannot map the upload resource");
    }
    return out;
}

}  // namespace rtx::gui
