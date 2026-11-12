// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 7 (Task 7-C1) implementation.  See preview_viewport.h for the design
// contract.  Everything is COM RAII; no dcomp.dll is ever touched and the
// import surface is limited to the d3d12.dll/dxgi.dll entries the GUI target
// already links.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "preview_viewport.h"

#include <d3d12.h>
#include <dxgi1_5.h>

#include <cstring>
#include <utility>

namespace rtx::gui {
namespace {

constexpr wchar_t kPreviewChildClass[] = L"CineRTXPreviewChild";

LRESULT CALLBACK previewChildProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MOUSEMOVE:
            // A/B split dragging: the parent computes the split ratio from the
            // child-relative coordinates and republishes it to the pipeline.
            SendMessageW(GetParent(hwnd), WM_APP_SPLIT_DRAG, static_cast<WPARAM>(msg), lParam);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

bool ensureChildClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = previewChildProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPreviewChildClass;
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    return true;
}

template <typename T>
void safeRelease(T*& pointer) noexcept {
    if (pointer != nullptr) {
        pointer->Release();
        pointer = nullptr;
    }
}

}  // namespace

Result<PreviewViewport> PreviewViewport::create(HWND parent_hwnd, int width, int height) {
    if (parent_hwnd == nullptr || width <= 0 || height <= 0) {
        return err(ErrorCode::InvalidArgument, "preview viewport needs a parent and a size");
    }
    HINSTANCE instance = reinterpret_cast<HINSTANCE>(
        GetWindowLongPtrW(parent_hwnd, GWLP_HINSTANCE));
    if (instance == nullptr) {
        instance = GetModuleHandleW(nullptr);
    }
    if (!ensureChildClass(instance)) {
        return err(ErrorCode::Internal, "cannot register the preview child window class");
    }
    constexpr DWORD kChildStyle =
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    HWND child = CreateWindowExW(0, kPreviewChildClass, L"", kChildStyle, 0, 0, width, height,
                                 parent_hwnd, nullptr, instance, nullptr);
    if (child == nullptr) {
        return err(ErrorCode::Internal, "cannot create the preview child window");
    }

    PreviewViewport viewport;
    viewport.child_hwnd_ = child;
    viewport.width_ = width;
    viewport.height_ = height;

    IDXGIFactory2* factory = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    ID3D12Resource* upload = nullptr;
    IDXGISwapChain1* swap_chain = nullptr;
    IDXGISwapChain3* swap_chain3 = nullptr;
    ID3D12Resource* rts[2] = {nullptr, nullptr};
    HANDLE fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (fence_event == nullptr) {
        safeRelease(factory);
        DestroyWindow(child);
        return err(ErrorCode::OutOfMemory, "cannot create the preview fence event");
    }
    viewport.fence_event_ = fence_event;

    auto fail = [&](const char* message) {
        safeRelease(rts[0]);
        safeRelease(rts[1]);
        safeRelease(swap_chain3);
        safeRelease(swap_chain);
        safeRelease(upload);
        safeRelease(fence);
        safeRelease(list);
        safeRelease(allocator);
        safeRelease(queue);
        safeRelease(device);
        safeRelease(factory);
        DestroyWindow(child);
        return err(ErrorCode::Internal, message);
    };

    IDXGIFactory5* factory5 = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        return fail("CreateDXGIFactory1 failed for the preview viewport");
    }
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5)))) {
        BOOL tearing = FALSE;
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
                                                    &tearing, sizeof(tearing)))) {
            viewport.tearing_supported_ = tearing != FALSE;
        }
        safeRelease(factory5);
    }
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        return fail("D3D12CreateDevice failed for the preview viewport");
    }
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)))) {
        return fail("cannot create the preview command queue");
    }
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              IID_PPV_ARGS(&allocator)))) {
        return fail("cannot create the preview command allocator");
    }
    // Older d3d12.h headers (mingw) only declare CreateCommandList, which
    // returns the list in the recording state; close it immediately.
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator,
                                         nullptr, IID_PPV_ARGS(&list)))) {
        return fail("cannot create the preview command list");
    }
    (void)list->Close();
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        return fail("cannot create the preview fence");
    }

    // Upload heap sized to one full preview frame (rows padded to 256 bytes).
    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
    const UINT64 row_pitch = ((static_cast<UINT64>(width) * 4u) + 255u) & ~255ull;
    D3D12_RESOURCE_DESC upload_desc{};
    upload_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    upload_desc.Width = row_pitch * static_cast<UINT64>(height);
    upload_desc.Height = 1;
    upload_desc.DepthOrArraySize = 1;
    upload_desc.MipLevels = 1;
    upload_desc.SampleDesc.Count = 1;
    upload_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&heap_props, D3D12_HEAP_FLAG_NONE, &upload_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&upload)))) {
        return fail("cannot create the preview upload buffer");
    }
    D3D12_RANGE read_range{0, 0};
    void* mapped = nullptr;
    if (FAILED(upload->Map(0, &read_range, &mapped)) || mapped == nullptr) {
        return fail("cannot map the preview upload buffer");
    }

    DXGI_SWAP_CHAIN_DESC1 swap_desc{};
    swap_desc.Width = static_cast<UINT>(width);
    swap_desc.Height = static_cast<UINT>(height);
    swap_desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = 2;  // double buffered: 2 x 2.07 MB = 4.14 MB VRAM
    swap_desc.Scaling = DXGI_SCALING_STRETCH;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    if (viewport.tearing_supported_) {
        swap_desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    }
    if (FAILED(factory->CreateSwapChainForHwnd(queue, child, &swap_desc, nullptr, nullptr,
                                               &swap_chain))) {
        return fail("CreateSwapChainForHwnd (FLIP_DISCARD) failed for the preview viewport");
    }
    if (FAILED(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain3)))) {
        return fail("QueryInterface(IDXGISwapChain3) failed for the preview viewport");
    }
    for (UINT i = 0; i < 2; ++i) {
        if (FAILED(swap_chain->GetBuffer(i, IID_PPV_ARGS(&rts[i])))) {
            return fail("GetBuffer failed for swap chain render target");
        }
    }

    viewport.factory_ = factory;
    viewport.device_ = device;
    viewport.cmd_queue_ = queue;
    viewport.cmd_allocator_ = allocator;
    viewport.cmd_list_ = list;
    viewport.cmd_fence_ = fence;
    viewport.upload_buffer_ = upload;
    viewport.upload_map_ = mapped;
    viewport.upload_pitch_ = static_cast<uint32_t>(row_pitch);
    viewport.swap_chain_ = swap_chain;
    viewport.swap_chain3_ = swap_chain3;
    viewport.render_targets_[0] = rts[0];
    viewport.render_targets_[1] = rts[1];
    return viewport;
}

PreviewViewport::~PreviewViewport() { release(); }

PreviewViewport::PreviewViewport(PreviewViewport&& other) noexcept { *this = std::move(other); }

PreviewViewport& PreviewViewport::operator=(PreviewViewport&& other) noexcept {
    if (this != &other) {
        release();
        child_hwnd_ = std::exchange(other.child_hwnd_, nullptr);
        factory_ = std::exchange(other.factory_, nullptr);
        device_ = std::exchange(other.device_, nullptr);
        cmd_queue_ = std::exchange(other.cmd_queue_, nullptr);
        cmd_allocator_ = std::exchange(other.cmd_allocator_, nullptr);
        cmd_list_ = std::exchange(other.cmd_list_, nullptr);
        cmd_fence_ = std::exchange(other.cmd_fence_, nullptr);
        fence_event_ = std::exchange(other.fence_event_, nullptr);
        fence_value_ = std::exchange(other.fence_value_, uint64_t{0});
        upload_buffer_ = std::exchange(other.upload_buffer_, nullptr);
        upload_map_ = std::exchange(other.upload_map_, nullptr);
        upload_pitch_ = std::exchange(other.upload_pitch_, uint32_t{0});
        swap_chain_ = std::exchange(other.swap_chain_, nullptr);
        swap_chain3_ = std::exchange(other.swap_chain3_, nullptr);
        render_targets_[0] = std::exchange(other.render_targets_[0], nullptr);
        render_targets_[1] = std::exchange(other.render_targets_[1], nullptr);
        width_ = std::exchange(other.width_, 960);
        height_ = std::exchange(other.height_, 540);
        split_ratio_ = std::exchange(other.split_ratio_, 0.5f);
        last_present_skipped_ = std::exchange(other.last_present_skipped_, false);
        tearing_supported_ = std::exchange(other.tearing_supported_, false);
    }
    return *this;
}

void PreviewViewport::release() noexcept {
    if (cmd_queue_ != nullptr && cmd_fence_ != nullptr) {
        auto* fence = reinterpret_cast<ID3D12Fence*>(cmd_fence_);
        if (fence->GetCompletedValue() < fence_value_) {
            if (fence_event_ != nullptr &&
                SUCCEEDED(fence->SetEventOnCompletion(fence_value_, fence_event_))) {
                WaitForSingleObject(fence_event_, 1000);
            }
        }
    }
    if (upload_buffer_ != nullptr && upload_map_ != nullptr) {
        D3D12_RANGE written{0, 0};
        reinterpret_cast<ID3D12Resource*>(upload_buffer_)->Unmap(0, &written);
        upload_map_ = nullptr;
    }
    safeRelease(reinterpret_cast<ID3D12Resource*&>(render_targets_[0]));
    safeRelease(reinterpret_cast<ID3D12Resource*&>(render_targets_[1]));
    safeRelease(reinterpret_cast<IDXGISwapChain3*&>(swap_chain3_));
    safeRelease(reinterpret_cast<IDXGISwapChain1*&>(swap_chain_));
    safeRelease(reinterpret_cast<ID3D12Resource*&>(upload_buffer_));
    safeRelease(reinterpret_cast<ID3D12Fence*&>(cmd_fence_));
    safeRelease(reinterpret_cast<ID3D12GraphicsCommandList*&>(cmd_list_));
    safeRelease(reinterpret_cast<ID3D12CommandAllocator*&>(cmd_allocator_));
    safeRelease(reinterpret_cast<ID3D12CommandQueue*&>(cmd_queue_));
    safeRelease(reinterpret_cast<ID3D12Device*&>(device_));
    safeRelease(reinterpret_cast<IDXGIFactory2*&>(factory_));
    if (fence_event_ != nullptr) {
        CloseHandle(fence_event_);
        fence_event_ = nullptr;
    }
    if (child_hwnd_ != nullptr) {
        DestroyWindow(child_hwnd_);
        child_hwnd_ = nullptr;
    }
}

void PreviewViewport::setBounds(int x, int y, int w, int h) noexcept {
    if (child_hwnd_ != nullptr) {
        MoveWindow(child_hwnd_, x, y, w, h, TRUE);
    }
}

void PreviewViewport::presentFromHostOrShared(const void* r10g10b10a2_data,
                                              std::uint32_t pitch) noexcept {
    last_present_skipped_ = true;
    if (swap_chain_ == nullptr || swap_chain3_ == nullptr || r10g10b10a2_data == nullptr ||
        upload_map_ == nullptr) {
        return;
    }
    auto* swap = reinterpret_cast<IDXGISwapChain1*>(swap_chain_);
    auto* swap3 = reinterpret_cast<IDXGISwapChain3*>(swap_chain3_);
    auto* device = reinterpret_cast<ID3D12Device*>(device_);
    auto* queue = reinterpret_cast<ID3D12CommandQueue*>(cmd_queue_);
    auto* allocator = reinterpret_cast<ID3D12CommandAllocator*>(cmd_allocator_);
    auto* list = reinterpret_cast<ID3D12GraphicsCommandList*>(cmd_list_);
    auto* upload = reinterpret_cast<ID3D12Resource*>(upload_buffer_);
    auto* fence = reinterpret_cast<ID3D12Fence*>(cmd_fence_);

    if (device != nullptr && device->GetDeviceRemovedReason() != S_OK) {
        return;
    }

    // 1. Wait on CPU for previous command list execution to complete before resetting allocator
    if (fence_value_ > 0 && fence != nullptr) {
        if (fence->GetCompletedValue() < fence_value_) {
            if (fence_event_ != nullptr &&
                SUCCEEDED(fence->SetEventOnCompletion(fence_value_, fence_event_))) {
                if (WaitForSingleObject(fence_event_, 50) != WAIT_OBJECT_0) {
                    return;  // GPU still busy with previous present; skip frame
                }
            }
        }
    }

    // 2. Query active back buffer index from the flip swap chain
    const UINT back_buffer_idx = swap3->GetCurrentBackBufferIndex();
    if (back_buffer_idx >= 2) {
        return;
    }
    auto* back_buffer = reinterpret_cast<ID3D12Resource*>(render_targets_[back_buffer_idx]);
    if (back_buffer == nullptr) {
        return;
    }

    // 3. Copy host rows into upload mapping
    const uint32_t copy_bytes = static_cast<uint32_t>(width_) * 4u;
    const uint8_t* source = static_cast<const uint8_t*>(r10g10b10a2_data);
    uint8_t* target = static_cast<uint8_t*>(upload_map_);
    for (int row = 0; row < height_; ++row) {
        std::memcpy(target + static_cast<size_t>(row) * upload_pitch_,
                    source + static_cast<size_t>(row) * pitch, copy_bytes);
    }

    if (FAILED(allocator->Reset()) || FAILED(list->Reset(allocator, nullptr))) {
        return;
    }

    D3D12_RESOURCE_BARRIER to_dest{};
    to_dest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_dest.Transition.pResource = back_buffer;
    to_dest.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    to_dest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    to_dest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &to_dest);

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
    src.PlacedFootprint.Footprint.Width = static_cast<UINT>(width_);
    src.PlacedFootprint.Footprint.Height = static_cast<UINT>(height_);
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = upload_pitch_;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = back_buffer;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER to_present{};
    to_present.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    to_present.Transition.pResource = back_buffer;
    to_present.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    to_present.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    to_present.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &to_present);

    if (FAILED(list->Close())) {
        return;
    }

    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);

    const UINT flags =
        (tearing_supported_ ? DXGI_PRESENT_ALLOW_TEARING : 0u) | DXGI_PRESENT_DO_NOT_WAIT;
    const HRESULT presented = swap->Present(0, flags);
    if (SUCCEEDED(presented)) {
        last_present_skipped_ = false;
    } else {
        last_present_skipped_ = true;
    }

    ++fence_value_;
    (void)queue->Signal(fence, fence_value_);
}

}  // namespace rtx::gui
