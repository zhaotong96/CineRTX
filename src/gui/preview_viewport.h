// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 7 (Task 7-C1): realtime A/B monitoring viewport.
//
// Zero dcomp.dll dependency: a plain Win32 STATIC child HWND hosts a
// DXGI_SWAP_EFFECT_FLIP_DISCARD swap chain in R10G10B10A2_UNORM at the fixed
// 960x540 preview resolution (double buffered = 2 x 2.07 MB = 4.14 MB of VRAM,
// per the expert verdict §4.3 iron rule 1).  Presenting is strictly
// non-blocking (DXGI_PRESENT_DO_NOT_WAIT | DXGI_PRESENT_ALLOW_TEARING) and
// silently skips a frame when the DWM is busy, the window is occluded or
// minimised - the transcode thread is never delayed by the viewport.

#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>

#include "rtx/status.h"

// The viewport child window forwards A/B split dragging to its parent:
// wParam carries the forwarded mouse message (WM_LBUTTONDOWN /
// WM_MOUSEMOVE / WM_LBUTTONUP), lParam the child-client coordinates.
constexpr UINT WM_APP_SPLIT_DRAG = WM_APP + 30;

namespace rtx::gui {

class PreviewViewport {
public:
    // Creates the STATIC child window inside `parent_hwnd`, the D3D12 device,
    // the command queue and the FLIP_DISCARD swap chain bound to it.
    [[nodiscard]] static Result<PreviewViewport> create(HWND parent_hwnd, int width = 960,
                                                        int height = 540);
    ~PreviewViewport();

    PreviewViewport(const PreviewViewport&) = delete;
    PreviewViewport& operator=(const PreviewViewport&) = delete;
    PreviewViewport(PreviewViewport&& other) noexcept;
    PreviewViewport& operator=(PreviewViewport&& other) noexcept;

    // Repositions the child window inside the parent's client area.
    void setBounds(int x, int y, int w, int h) noexcept;
    [[nodiscard]] float splitRatio() const noexcept { return split_ratio_; }
    void setSplitRatio(float ratio) noexcept { split_ratio_ = ratio; }

    // Non-blocking present of one host-side 960x540 R10G10B10A2 frame
    // (`r10g10b10a2_data`, rows padded to `pitch` bytes).  When the swap
    // chain cannot take a frame the call returns without blocking.
    void presentFromHostOrShared(const void* r10g10b10a2_data, std::uint32_t pitch) noexcept;

    // True when the last present was skipped because the swap chain was not
    // ready (DWM busy / occluded) - the GUI uses this to count stall-free
    // frames instead of retrying.
    [[nodiscard]] bool lastPresentSkipped() const noexcept { return last_present_skipped_; }

    [[nodiscard]] HWND hwnd() const noexcept { return child_hwnd_; }

private:
    PreviewViewport() = default;
    void release() noexcept;

    HWND child_hwnd_ = nullptr;
    void* factory_ = nullptr;      // IDXGIFactory2*
    void* device_ = nullptr;       // ID3D12Device*
    void* cmd_queue_ = nullptr;    // ID3D12CommandQueue*
    void* cmd_allocator_ = nullptr;
    void* cmd_list_ = nullptr;
    void* cmd_fence_ = nullptr;
    HANDLE fence_event_ = nullptr;
    uint64_t fence_value_ = 0;
    void* upload_buffer_ = nullptr;    // ID3D12Resource* (UPLOAD heap, mapped)
    void* upload_map_ = nullptr;       // persistent CPU mapping
    uint32_t upload_pitch_ = 0;
    void* swap_chain_ = nullptr;       // IDXGISwapChain1*
    void* swap_chain3_ = nullptr;      // IDXGISwapChain3*
    void* render_targets_[2] = {nullptr, nullptr};  // ID3D12Resource*
    int width_ = 960;
    int height_ = 540;
    float split_ratio_ = 0.5f;         // default centre 50/50 A/B split
    bool last_present_skipped_ = false;
    bool tearing_supported_ = false;
};

}  // namespace rtx::gui
