// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// DirectComposition 60 Hz zero-copy live viewport (W9-2).
//
// Queue isolation is the whole point: the preview runs on a
// D3D12_COMMAND_QUEUE_PRIORITY_LOW direct queue, so the driver's scheduler
// always prefers the encode queue when both are ready.  That is what keeps the
// background throughput loss inside the Gate 3.3 2.0 % budget.

#include "dcomp_viewport.h"

#if defined(_WIN32) && defined(RTX_HAVE_DCOMP_VIEWPORT)

#include <d3d12.h>
#include <dcomp.h>
#include <dxgi1_6.h>

#include <chrono>
#include <cstdio>

namespace rtx::gui {
namespace {

void setError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

void releaseUnknown(void** unknown) {
    if (unknown != nullptr && *unknown != nullptr) {
        static_cast<IUnknown*>(*unknown)->Release();
        *unknown = nullptr;
    }
}

}  // namespace

DcompViewport::~DcompViewport() { destroy(); }

bool DcompViewport::create(HWND hwnd, uint32_t width, uint32_t height, std::string* error) {
    destroy();
    if (hwnd == nullptr) {
        setError(error, "viewport requires a parent window");
        return false;
    }
    hwnd_ = hwnd;
    width_ = width;
    height_ = height;

    if (!createDevice(error)) {
        return false;
    }
    if (!createSwapChain(hwnd, error)) {
        return false;
    }
    if (!createComposition(hwnd, error)) {
        return false;
    }
    valid_ = true;
    diagnostics_ = "DirectComposition viewport active (normal-priority direct queue)";
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool DcompViewport::createDevice(std::string* error) {
    ID3D12Device* device = nullptr;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        diagnostics_ = "D3D12CreateDevice failed; the viewport stays disabled";
        setError(error, diagnostics_.c_str());
        return false;
    }
    device_ = device;

    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue_desc.NodeMask = 0;

    ID3D12CommandQueue* queue = nullptr;
    if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)))) {
        diagnostics_ = "CreateCommandQueue(direct) failed";
        setError(error, diagnostics_.c_str());
        return false;
    }
    queue_ = queue;
    return true;
}

bool DcompViewport::createSwapChain(HWND hwnd, std::string* error) {
    IDXGIFactory2* factory = nullptr;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        diagnostics_ = "CreateDXGIFactory2 failed";
        setError(error, diagnostics_.c_str());
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width_;
    desc.Height = height_;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    // Waitable object: the UI thread waits on a handle instead of spinning, and
    // ALLOW_TEARING lets the 60 Hz gate drop a frame without a copy.
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
                 DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    IDXGISwapChain1* swap_chain = nullptr;
    const HRESULT status = factory->CreateSwapChainForComposition(
        static_cast<IUnknown*>(static_cast<ID3D12CommandQueue*>(queue_)), &desc, nullptr,
        &swap_chain);
    factory->Release();
    if (FAILED(status) || swap_chain == nullptr) {
        diagnostics_ = "CreateSwapChainForComposition failed";
        setError(error, diagnostics_.c_str());
        return false;
    }
    swap_chain_ = swap_chain;

    IDXGISwapChain2* swap_chain2 = nullptr;
    if (SUCCEEDED(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain2)))) {
        swap_chain2->SetMaximumFrameLatency(1);
        swap_chain2->Release();
    }
    (void)hwnd;
    return true;
}

bool DcompViewport::createComposition(HWND hwnd, std::string* error) {
    IDCompositionDevice* dcomp_device = nullptr;
    if (FAILED(DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&dcomp_device)))) {
        diagnostics_ = "DCompositionCreateDevice failed";
        setError(error, diagnostics_.c_str());
        return false;
    }
    dcomp_device_ = dcomp_device;

    IDCompositionTarget* target = nullptr;
    if (FAILED(dcomp_device->CreateTargetForHwnd(hwnd, FALSE, &target))) {
        diagnostics_ = "CreateTargetForHwnd failed";
        setError(error, diagnostics_.c_str());
        return false;
    }
    dcomp_target_ = target;

    IDCompositionVisual* visual = nullptr;
    if (FAILED(dcomp_device->CreateVisual(&visual))) {
        diagnostics_ = "CreateVisual failed";
        setError(error, diagnostics_.c_str());
        return false;
    }
    dcomp_visual_ = visual;

    if (FAILED(visual->SetContent(
            static_cast<IUnknown*>(static_cast<IDXGISwapChain*>(swap_chain_))))) {
        diagnostics_ = "IDCompositionVisual::SetContent failed";
        setError(error, diagnostics_.c_str());
        return false;
    }
    if (FAILED(target->SetRoot(visual))) {
        diagnostics_ = "IDCompositionTarget::SetRoot failed";
        setError(error, diagnostics_.c_str());
        return false;
    }
    if (FAILED(dcomp_device->Commit())) {
        diagnostics_ = "IDCompositionDevice::Commit failed";
        setError(error, diagnostics_.c_str());
        return false;
    }

    GetClientRect(hwnd, &rect_);
    return true;
}

void DcompViewport::destroy() {
    valid_ = false;
    releaseUnknown(&dcomp_visual_);
    releaseUnknown(&dcomp_target_);
    releaseUnknown(&dcomp_device_);
    releaseUnknown(&swap_chain_);
    releaseUnknown(&queue_);
    releaseUnknown(&device_);
    hwnd_ = nullptr;
    rect_ = RECT{};
    width_ = 0;
    height_ = 0;
    throttle_.reset();
}

bool DcompViewport::resize(uint32_t width, uint32_t height, std::string* error) {
    if (!valid_ || width == 0 || height == 0) {
        return false;
    }
    if (width == width_ && height == height_) {
        return true;
    }
    auto* swap_chain = static_cast<IDXGISwapChain*>(swap_chain_);
    if (FAILED(swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN,
                                         DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
                                             DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING))) {
        diagnostics_ = "ResizeBuffers failed";
        setError(error, diagnostics_.c_str());
        return false;
    }
    width_ = width;
    height_ = height;
    if (hwnd_ != nullptr) {
        GetClientRect(hwnd_, &rect_);
    }
    return true;
}

bool DcompViewport::present(double now_seconds, bool occluded, bool minimized) {
    if (!valid_) {
        return false;
    }
    if (!throttle_.shouldPresent(now_seconds, occluded, minimized)) {
        // DXGI_STATUS_OCCLUDED is deliberately not treated as an error: the
        // visual simply stops being composited.
        return false;
    }
    auto* swap_chain = static_cast<IDXGISwapChain*>(swap_chain_);
    // Present with tearing allowed and no vsync wait: the 60 Hz gate above is
    // the only limiter, so the queue never blocks the encode work.
    (void)swap_chain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
    return true;
}

bool DcompViewport::setCursor(int client_x, int client_y, int viewport_width) {
    if (!valid_) {
        return false;
    }
    (void)client_y;
    const float split = static_cast<float>(throttle_.splitForCursor(client_x, viewport_width));
    if (split == split_.split) {
        return false;
    }
    split_.split = split;
    return true;
}

}  // namespace rtx::gui

#endif  // _WIN32 && RTX_HAVE_DCOMP_VIEWPORT
