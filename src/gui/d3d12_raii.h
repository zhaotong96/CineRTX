// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// COM / D3D12 域所有权与 GPU Fence 异步退役机制（Phase 11 / Track 11A）。
// Phase 11.2-R（Std-First）：COM 智能指针统一收口 rtx::ComPtr；退役条目与
// 延迟退役池全部改用 std::tuple 与函数内局部静态，零手写包装类。
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
// 每个条目绑定一次命令提交的单调 Fence 值。只有探针报告的实际完成值 >= 条目
// Fence 值时才释放。探针目标（ID3D12Fence）的生命周期由 keepalive_ 保证，使
// 队列可以脱离创建它的视口独立存活于延迟退役池中。
// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调多资源异步生命周期的宿主状态机：std::unique_ptr/std::jthread/std::tuple 无法表达其声明序即析构逆序、线程亲和与受控降级路径的交叉不变量。
// Invariant: 成员声明序即依赖序（先声明者后析构）；状态迁移仅在持锁线程或属主线程发生。
class GpuRetirementQueue {
public:
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
    using CompletedProbe = std::move_only_function<std::uint64_t()>;
    using ReleaseHook = std::move_only_function<void()>;
#else
    using CompletedProbe = std::function<std::uint64_t()>;
    using ReleaseHook = std::function<void()>;
#endif
    using Entry = std::tuple<std::uint64_t, ComPtr<IUnknown>, ReleaseHook>;

// 退役条目：<0> 绑定 Fence 值、<1> 拥有的引用计数、<2> 释放前钩子（Unmap）。
    GpuRetirementQueue() noexcept = default;
    explicit GpuRetirementQueue(CompletedProbe probe) noexcept : probe_(std::move(probe)) {}

    GpuRetirementQueue(const GpuRetirementQueue&) = delete;
    GpuRetirementQueue& operator=(const GpuRetirementQueue&) = delete;
    GpuRetirementQueue(GpuRetirementQueue&&) noexcept = default;
    GpuRetirementQueue& operator=(GpuRetirementQueue&&) noexcept = default;

    void setProbe(CompletedProbe probe) noexcept { probe_ = std::move(probe); }
    void setKeepalive(ComPtr<IUnknown> fence) noexcept { keepalive_ = std::move(fence); }
    void setEventKeepalive(rtx::UniqueHandle event) noexcept { event_ = std::move(event); }
    void setWindowKeepalive(rtx::UniqueHwnd window) noexcept { window_ = std::move(window); }
// SetEventOnCompletion 注册的事件必须活到 Fence 达标之后。

// 交换链绑定的 HWND 必须活到交换链释放之后。
    void retire(std::uint64_t fence_value, IUnknown* object,
                ReleaseHook before_release = {}) noexcept {
// 接管 object 的一个引用计数。before_release 在引用计数归还前调用，
// 用于持久映射资源的 Unmap 等必须与释放同步发生的动作。
        if (object == nullptr) return;
        Entry entry{fence_value, ComPtr<IUnknown>{}, std::move(before_release)};
        std::get<1>(entry).reset(object);
        entries_.push_back(std::move(entry));
    }
    template <typename T>
    void retire(std::uint64_t fence_value, ComPtr<T>&& owner,
                ReleaseHook before_release = {}) noexcept {
        retire(fence_value, static_cast<IUnknown*>(owner.release()), std::move(before_release));
    }
    std::size_t collect() noexcept {
        if (!probe_) return 0;
        return collect(probe_());
    }
// 释放所有 Fence 已达标的条目，返回释放个数。
// Phase 11.2 (C-03)：GetCompletedValue() == UINT64_MAX 是 D3D12 在设备移除
// 后的"假完成"信号（DXGI_ERROR_DEVICE_REMOVED 会把所有 Fence 推满）。命中即
// 走受控 Device-Lost 路径：全部引用立即归还（releaseAll）并置 lost 标志，
// 绝不把已移除的设备误判为空闲健康。
    std::size_t collect(std::uint64_t completed_value) noexcept {
        if (completed_value == UINT64_MAX) {
            device_lost_seen_ = true;
            releaseAll();
            return 0;
        }
        return std::erase_if(entries_, [completed_value](Entry& entry) noexcept {
            auto& [fence_val, obj, before_release] = entry;
            (void)obj;
            if (fence_val <= completed_value) {
                if (before_release) before_release();
                return true;
            }
            return false;
        });
    }
    // collect() 命中 UINT64_MAX 假完成后置位；宿主据此触发受控清理。
    [[nodiscard]] bool deviceLostSeen() const noexcept { return device_lost_seen_; }
    void resetDeviceLostSeen() noexcept { device_lost_seen_ = false; }

    // Device-Lost 路径：GPU 已不存在，飞行中命令随之作废，引用计数必须归还。
    void releaseAll() noexcept {
        for (auto& e : entries_) {
            if (std::get<2>(e)) std::get<2>(e)();
        }
        entries_.clear();
    }
    std::size_t pending() const noexcept { return entries_.size(); }
    bool empty() const noexcept { return entries_.empty(); }

private:
    // 声明序即存活序：window_/event_ 最先声明、最后析构，保证交换链与 Fence
    // 释放之前 HWND 与事件句柄始终有效。
    rtx::UniqueHwnd window_;
    rtx::UniqueHandle event_;
    CompletedProbe probe_;
    ComPtr<IUnknown> keepalive_;
    bool device_lost_seen_ = false;
    std::vector<Entry> entries_;
};

inline std::mutex& deferredPoolMutex() noexcept {
    static std::mutex mutex;
    return mutex;
}
// 进程级延迟退役池。视口在 GPU 仍忙且设备健康时销毁，其退役队列整体移入此处，
// 由后续任意一次 drainDeferredRetirement() 在 Fence 达标后精确释放。
// （函数内双局部静态：进程级池的零样板表达，见施工单 Doc 24 §1.1。）
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
        released += pool[i].collect();
        if (pool[i].empty()) {
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
    for (const auto& q : deferredPool()) total += q.pending();
    return total;
}
using MappedUploadResource = std::pair<ComPtr<ID3D12Resource>, void*>;

// ---- 持久映射上传堆 -------------------------------------------------------
// 映射上传堆工厂：返回 <0> 拥有资源的引用计数、<1> 持久 CPU 映射。D3D12 契约
// 允许带映射释放（隐式 Unmap），故不额外手写包装；Fence 门禁由宿主负责——宿主
// 必须先 waitForGpuIdle 成功，或把 <0> release() 后交由 GpuRetirementQueue
// 按先 Unmap 后 Release 的钩子回收。
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
