// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d12.h>
#include <dcomp.h>
#include <dxgi1_6.h>

#include <array>
#include <atomic>
#include <memory>
#include <cstdint>
#include <utility>

#include "d3d12_raii.h"
#include "rtx/raii_owners.h"
#include "rtx/status.h"

constexpr UINT WM_APP_SPLIT_DRAG = WM_APP + 30;

namespace rtx::gui {

using PreviewSlotTicket = std::pair<std::shared_ptr<std::array<std::atomic<std::uint32_t>, 3>>, std::uint32_t>;

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调 DirectComposition 视觉树、DXGI 交换链、D3D12 命令队列、Fence 异步退役与设备丢失恢复的有状态渲染视口，标准库智能指针组合无法表达其析构排空顺序与退化协议。
// Invariant: 释放 GPU 资源前必须等待 Fence 完成或移交进程级 deferRetirement() 池；共享槽位票据通过 shared_ptr 保持生命周期安全。
class PreviewViewport {
public:
    [[nodiscard]] static Result<PreviewViewport> create(HWND parent_hwnd, int width = 960,
                                                        int height = 540);
    ~PreviewViewport();

    PreviewViewport(const PreviewViewport&) = delete;
    PreviewViewport& operator=(const PreviewViewport&) = delete;
    PreviewViewport(PreviewViewport&& other) noexcept = default;
    PreviewViewport& operator=(PreviewViewport&& other) noexcept;

    void setBounds(int x, int y, int w, int h) noexcept;
    void resizeSwapChain(int width, int height) noexcept;
    [[nodiscard]] float splitRatio() const noexcept { return split_ratio_; }
    void setSplitRatio(float ratio) noexcept { split_ratio_ = ratio; }

    void presentShared(void* borrowed_d3d12_device, void* shared_resource,
                       std::uint32_t pitch, std::uint32_t src_width, std::uint32_t src_height,
                       bool input_is_pq, float max_nits,
                       const PreviewSlotTicket& ticket = {}) noexcept;

    [[nodiscard]] bool lastPresentSkipped() const noexcept { return last_present_skipped_; }
    [[nodiscard]] bool deviceLost() const noexcept { return device_lost_; }
    [[nodiscard]] bool hdrOutput() const noexcept { return hdr_output_; }
    [[nodiscard]] HWND hwnd() const noexcept { return child_hwnd_.get(); }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }

    [[nodiscard]] Result<void> waitForGpuIdle(std::uint32_t timeout_ms) noexcept;

private:
    PreviewViewport() = default;
    void release() noexcept;
    void releaseGpu(bool viewport_dying) noexcept;
    void retireAllGpuResources() noexcept;
    bool adoptDevice(void* borrowed_device) noexcept;
    bool createCompositionSwapChain() noexcept;
    bool createViewHeap() noexcept;
    [[nodiscard]] Result<void> markDeviceLost(const char* message) noexcept;

    using DCompositionCreateDeviceFn = HRESULT(WINAPI*)(IDXGIDevice*, REFIID, void**);

    UniqueHwnd child_hwnd_;
    UniqueModule dcomp_module_;
    DCompositionCreateDeviceFn dcomp_create_ = nullptr;
    ComPtr<IDCompositionDevice> dcomp_device_;
    ComPtr<IDCompositionTarget> dcomp_target_;
    ComPtr<IDCompositionVisual> visual_;
    ComPtr<IDXGIFactory2> factory_;
    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> cmd_queue_;
    ComPtr<ID3D12CommandAllocator> cmd_allocator_;
    ComPtr<ID3D12GraphicsCommandList> cmd_list_;
    ComPtr<ID3D12Fence> cmd_fence_;
    UniqueHandle fence_event_;
    std::uint64_t fence_value_ = 0;
    GpuRetirementQueue retirement_;
    ComPtr<ID3D12DescriptorHeap> view_heap_;
    ComPtr<ID3D12RootSignature> root_signature_;
    ComPtr<ID3D12PipelineState> pipeline_;
    ComPtr<ID3D12Resource> render_targets_[2];
    ComPtr<IDXGISwapChain4> swap_chain_;
    UINT view_descriptor_size_ = 0;
    int width_ = 960;
    int height_ = 540;
    float split_ratio_ = 0.5f;
    bool hdr_output_ = false;
    bool last_present_skipped_ = false;
    bool device_lost_ = false;
};

}  // namespace rtx::gui