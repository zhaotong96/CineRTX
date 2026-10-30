// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// DirectComposition 60 Hz zero-copy live viewport (W9-2).
//
// The viewport is a child visual of the main HWND: GDI keeps painting the
// chrome, and the region the visual covers is excluded from the GDI clip so the
// two compositors never fight over the same pixels.
//
// Everything here is optional.  When D3D12, DXGI or DirectComposition cannot be
// initialised (a headless CI machine, a VM without a WARP adapter, a driver
// reset) the viewport reports why and the GUI keeps running with GDI only.

#pragma once

#include <cstdint>
#include <string>

#include "viewport_throttle.h"

#if defined(_WIN32) && defined(RTX_HAVE_DCOMP_VIEWPORT)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace rtx::gui {

// Split-screen tone-map mode: left of the split samples the 1080p original,
// right samples the 4K remaster after PQ -> sRGB.
struct ViewportSplitState {
    float split = 0.5f;
    bool enabled = true;
};

class DcompViewport {
public:
    DcompViewport() = default;
    ~DcompViewport();

    DcompViewport(const DcompViewport&) = delete;
    DcompViewport& operator=(const DcompViewport&) = delete;

    // Creates the D3D12 device, the direct queue, the swap chain
    // and the composition target for `hwnd`.  Returns false and fills
    // `diagnostics()` when any prerequisite is missing.
    bool create(HWND hwnd, uint32_t width, uint32_t height, std::string* error);
    void destroy();

    bool valid() const noexcept { return valid_; }
    const std::string& diagnostics() const noexcept { return diagnostics_; }

    // Called from WM_SIZE.  Recreates the swap-chain buffers only when the size
    // actually changed, because ResizeBuffers drops the visual for a frame.
    bool resize(uint32_t width, uint32_t height, std::string* error);

    // Called on every UI tick.  Returns true when a frame was submitted.
    bool present(double now_seconds, bool occluded, bool minimized);

    // Called from WM_MOUSEMOVE.  Returns true when the split moved, which means
    // the caller should invalidate the GDI chrome around the viewport.
    bool setCursor(int client_x, int client_y, int viewport_width);

    // Region the GDI paint path must exclude (the visual owns those pixels).
    RECT clientRect() const noexcept { return rect_; }

    ViewportThrottle& throttle() noexcept { return throttle_; }
    const ViewportThrottle& throttle() const noexcept { return throttle_; }
    ViewportSplitState split() const noexcept { return split_; }

private:
    bool createDevice(std::string* error);
    bool createSwapChain(HWND hwnd, std::string* error);
    bool createComposition(HWND hwnd, std::string* error);

    bool valid_ = false;
    HWND hwnd_ = nullptr;
    RECT rect_{};
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    void* device_ = nullptr;      // ID3D12Device*
    void* queue_ = nullptr;       // ID3D12CommandQueue* (LOW priority)
    void* swap_chain_ = nullptr;  // IDXGISwapChain3*
    void* dcomp_device_ = nullptr;  // IDCompositionDevice*
    void* dcomp_target_ = nullptr;  // IDCompositionTarget*
    void* dcomp_visual_ = nullptr;  // IDCompositionVisual*
    ViewportThrottle throttle_{};
    ViewportSplitState split_{};
    std::string diagnostics_;
};

}  // namespace rtx::gui

#endif  // _WIN32 && RTX_HAVE_DCOMP_VIEWPORT
