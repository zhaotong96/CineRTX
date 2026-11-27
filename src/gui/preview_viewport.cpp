// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "preview_viewport.h"
#include "rtx/logger.h"
#include "rtx/sys_dynload.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <format>
#include <memory>
#include <utility>

#include "preview_tone_map_cs.h"

namespace rtx::gui {
namespace {

constexpr wchar_t kPreviewChildClass[] = L"CineRTXPreviewChild";
constexpr LONG_PTR kChildClassStyle = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;

constexpr UINT kSwapChainBuffers = 2;
constexpr UINT kSwapChainFlags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
constexpr float kSdrReferenceNits = 100.0f;
constexpr UINT kComputeGroup = 16;
constexpr UINT kRootConstantDwords = 12;

using ViewportConstants = std::array<std::uint32_t, kRootConstantDwords>;
static_assert(sizeof(ViewportConstants) == kRootConstantDwords * 4);

inline void resourceBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                            D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) noexcept {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
}
template <typename T, typename Host, typename MemFn, typename... Args>
inline bool callComOut(Host* host, MemFn method, ComPtr<T>& target, Args&&... args) noexcept {
    target.reset();
    T* raw = nullptr;
    if (FAILED((host->*method)(std::forward<Args>(args)..., __uuidof(T),
                               reinterpret_cast<void**>(&raw)))) {
        return false;
    }
    target.reset(raw);
    return true;
}
LRESULT CALLBACK previewChildProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTERROR) {
                return TRUE;  // Wave 0-A: block DefWindowProc MessageBeep
            }
            break;
        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc{};
            rtx::sys::getClientRect(hwnd, &rc);
            rtx::sys::setDCBrushColor(hdc, RGB(11, 13, 11));
            rtx::sys::fillRect(hdc, &rc, static_cast<HBRUSH>(rtx::sys::getStockObject(DC_BRUSH)));
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = rtx::sys::beginPaint(hwnd, &ps);
            rtx::sys::setDCBrushColor(hdc, RGB(11, 13, 11));
            rtx::sys::fillRect(hdc, &ps.rcPaint, static_cast<HBRUSH>(rtx::sys::getStockObject(DC_BRUSH)));
            rtx::sys::endPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MOUSEMOVE:
        case WM_LBUTTONDBLCLK:
            rtx::sys::sendMessageW(rtx::sys::getParent(hwnd), WM_APP_SPLIT_DRAG, static_cast<WPARAM>(msg), lParam);
            return 0;
        default:
            break;
    }
    return rtx::sys::defWindowProcW(hwnd, msg, wParam, lParam);
}
bool ensureChildClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = static_cast<UINT>(kChildClassStyle);
    wc.lpfnWndProc = previewChildProc;
    wc.hInstance = instance;
    wc.hCursor = rtx::sys::loadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPreviewChildClass;
    if (rtx::sys::registerClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    return true;
}
bool buildComputePipeline(ID3D12Device* device, ComPtr<ID3D12RootSignature>& root_signature,
                          ComPtr<ID3D12PipelineState>& pipeline) noexcept {
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].RegisterSpace = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = 1;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 2;
    params[0].DescriptorTable.pDescriptorRanges = ranges;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants.ShaderRegister = 0;
    params[1].Constants.RegisterSpace = 0;
    params[1].Constants.Num32BitValues = kRootConstantDwords;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC signature{};
    signature.NumParameters = 2;
    signature.pParameters = params;
    signature.NumStaticSamplers = 0;
    signature.pStaticSamplers = nullptr;
    signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob;
    ID3DBlob* raw_blob = nullptr;
    ID3DBlob* raw_errors = nullptr;
    if (FAILED(rtx::sys::d3d12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &raw_blob,
                                           &raw_errors))) {
        return false;
    }
    blob.reset(raw_blob);
    if (raw_errors != nullptr) {
        raw_errors->Release();
    }
    if (!callComOut(device, &ID3D12Device::CreateRootSignature, root_signature, 0,
                    blob->GetBufferPointer(), blob->GetBufferSize())) {
        return false;
    }
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = root_signature.get();
    pso.CS.pShaderBytecode = g_preview_tone_map_cs;
    pso.CS.BytecodeLength = sizeof(g_preview_tone_map_cs);
    return callComOut(device, &ID3D12Device::CreateComputePipelineState, pipeline, &pso);
}
}  // namespace

Result<PreviewViewport> PreviewViewport::create(HWND parent_hwnd, int width, int height) {
    if (parent_hwnd == nullptr || width <= 0 || height <= 0) {
        return err(ErrorCode::InvalidArgument, "preview viewport needs a parent and a size");
    }
    HINSTANCE instance =
        reinterpret_cast<HINSTANCE>(rtx::sys::getWindowLongPtrW(parent_hwnd, GWLP_HINSTANCE));
    if (instance == nullptr) {
        instance = GetModuleHandleW(nullptr);
    }
    if (!ensureChildClass(instance)) {
        return err(ErrorCode::Internal, "cannot register the preview child window class");
    }
    constexpr DWORD kChildStyle = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    HWND child = rtx::sys::createWindowExW(0, kPreviewChildClass, L"", kChildStyle, 0, 0, width, height,
                                 parent_hwnd, nullptr, instance, nullptr);
    if (child == nullptr) {
        return err(ErrorCode::Internal, "cannot create the preview child window");
    }
    PreviewViewport viewport;
    viewport.child_hwnd_.reset(child);
    viewport.target_width_ = std::max(1, width);
    viewport.target_height_ = std::max(1, height);
    viewport.width_ = viewport.target_width_;
    viewport.height_ = viewport.target_height_;

    viewport.dcomp_module_.reset(LoadLibraryExW(L"dcomp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32));
    if (!viewport.dcomp_module_) {
        return err(ErrorCode::DllMissing, "dcomp.dll is unavailable for the preview viewport");
    }
    viewport.dcomp_create_ = reinterpret_cast<PreviewViewport::DCompositionCreateDeviceFn>(
        GetProcAddress(viewport.dcomp_module_.get(), "DCompositionCreateDevice"));
    if (viewport.dcomp_create_ == nullptr) {
        return err(ErrorCode::DllMissing, "DCompositionCreateDevice is missing from dcomp.dll");
    }
    return viewport;
}
PreviewViewport::~PreviewViewport() { release(); }

PreviewViewport& PreviewViewport::operator=(PreviewViewport&& other) noexcept {
    if (this != &other) {
        release();
        child_hwnd_ = std::move(other.child_hwnd_);
        dcomp_module_ = std::move(other.dcomp_module_);
        dcomp_create_ = std::exchange(other.dcomp_create_, nullptr);
        dcomp_device_ = std::move(other.dcomp_device_);
        dcomp_target_ = std::move(other.dcomp_target_);
        visual_ = std::move(other.visual_);
        clip_ = std::move(other.clip_);
        factory_ = std::move(other.factory_);
        device_ = std::move(other.device_);
        cmd_queue_ = std::move(other.cmd_queue_);
        cmd_allocator_ = std::move(other.cmd_allocator_);
        cmd_list_ = std::move(other.cmd_list_);
        cmd_fence_ = std::move(other.cmd_fence_);
        fence_event_ = std::move(other.fence_event_);
        fence_value_ = std::exchange(other.fence_value_, std::uint64_t{0});
        retirement_ = std::move(other.retirement_);
        view_heap_ = std::move(other.view_heap_);
        root_signature_ = std::move(other.root_signature_);
        pipeline_ = std::move(other.pipeline_);
        for (int i = 0; i < 2; ++i) render_targets_[i] = std::move(other.render_targets_[i]);
        compute_target_ = std::move(other.compute_target_);
        swap_chain_ = std::move(other.swap_chain_);
        view_descriptor_size_ = std::exchange(other.view_descriptor_size_, UINT{0});
        target_width_ = std::exchange(other.target_width_, 960);
        target_height_ = std::exchange(other.target_height_, 540);
        width_ = std::exchange(other.width_, 960);
        height_ = std::exchange(other.height_, 540);
        split_ratio_ = std::exchange(other.split_ratio_, 0.5f);
        hdr_output_ = std::exchange(other.hdr_output_, false);
        last_present_skipped_ = std::exchange(other.last_present_skipped_, false);
        device_lost_ = std::exchange(other.device_lost_, false);
    }
    return *this;
}
Result<void> PreviewViewport::markDeviceLost(const char* message) noexcept {
    const HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : DXGI_ERROR_DEVICE_REMOVED;
    device_lost_ = true;
    return err(ErrorCode::DeviceLost, message, std::to_string(static_cast<long>(reason)));
}
Result<void> PreviewViewport::waitForGpuIdle(std::uint32_t timeout_ms) noexcept {
    if (device_lost_) {
        return err(ErrorCode::DeviceLost, "the preview D3D12 device has been removed");
    }
    if (!cmd_fence_ || !cmd_queue_) {
        return {};
    }
    const unsigned long long completed = cmd_fence_->GetCompletedValue();
    if (completed == UINT64_MAX) {
        gpuReleaseAll(retirement_);
        return markDeviceLost("the preview D3D12 device was removed (fence saturated)");
    }
    if (fence_value_ == 0 || completed >= fence_value_) {
        gpuCollect(retirement_);
        if (gpuDeviceLostSeen(retirement_)) {
            gpuResetDeviceLostSeen(retirement_);
            return markDeviceLost("the preview D3D12 device was removed");
        }
        return {};
    }
    if (!fence_event_) {
        return err(ErrorCode::State, "the preview fence has no completion event");
    }
    if (FAILED(cmd_fence_->SetEventOnCompletion(fence_value_, fence_event_.get()))) {
        return err(ErrorCode::Internal, "SetEventOnCompletion failed for the preview fence");
    }
    if (WaitForSingleObject(fence_event_.get(), timeout_ms) == WAIT_OBJECT_0) {
        gpuCollect(retirement_);
        if (gpuDeviceLostSeen(retirement_)) {
            gpuResetDeviceLostSeen(retirement_);
            return markDeviceLost("the preview D3D12 device was removed");
        }
        return {};
    }
    const HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : DXGI_ERROR_DEVICE_REMOVED;
    if (reason == S_OK) {
        return err(ErrorCode::Timeout, "the GPU is still executing the submitted preview commands");
    }
    gpuReleaseAll(retirement_);
    return markDeviceLost("the preview D3D12 device was removed while draining");
}
void PreviewViewport::retireAllGpuResources() noexcept {
    const std::uint64_t bound = fence_value_;
    for (auto& rt : render_targets_) {
        gpuRetire(retirement_, bound, std::move(rt));
    }
    gpuRetire(retirement_, bound, std::move(compute_target_));
    gpuRetire(retirement_, bound, std::move(view_heap_));
    gpuRetire(retirement_, bound, std::move(swap_chain_));
    gpuRetire(retirement_, bound, std::move(cmd_list_));
    gpuRetire(retirement_, bound, std::move(cmd_allocator_));
    gpuRetire(retirement_, bound, std::move(pipeline_));
    gpuRetire(retirement_, bound, std::move(root_signature_));
    gpuRetire(retirement_, bound, std::move(cmd_queue_));
    gpuRetire(retirement_, bound, std::move(device_));
}
void PreviewViewport::release() noexcept { releaseGpu(true); }

void PreviewViewport::releaseGpu(bool viewport_dying) noexcept {
    if (!cmd_fence_ || !cmd_queue_) {
        return;
    }
    const auto drained = waitForGpuIdle(1000);
    if (drained.has_value() || drained.error().code != ErrorCode::Timeout) {
        return;
    }
    retireAllGpuResources();
    ID3D12Fence* fence = cmd_fence_.get();
    gpuSetProbe(retirement_, [fence] { return fence->GetCompletedValue(); });
    gpuSetKeepalive(retirement_, ComPtr<IUnknown>(cmd_fence_.release()));
    gpuSetEventKeepalive(retirement_, std::move(fence_event_));
    if (viewport_dying) {
        gpuSetWindowKeepalive(retirement_, std::move(child_hwnd_));
    }
    deferRetirement(std::move(retirement_));
}
bool PreviewViewport::createViewHeap() noexcept {
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = 2;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heap.NodeMask = 0;
    if (!callComOut(device_.get(), &ID3D12Device::CreateDescriptorHeap, view_heap_, &heap)) {
        return false;
    }
    view_descriptor_size_ =
        device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}
bool PreviewViewport::createComputeTarget() noexcept {
    compute_target_.reset();
    if (!device_ || width_ <= 0 || height_ <= 0) {
        return false;
    }
    const DXGI_FORMAT back_format =
        hdr_output_ ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12_HEAP_PROPERTIES heap_props{};
    heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC tex{};
    tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex.Width = static_cast<UINT64>(width_);
    tex.Height = static_cast<UINT>(height_);
    tex.DepthOrArraySize = 1;
    tex.MipLevels = 1;
    tex.Format = back_format;
    tex.SampleDesc.Count = 1;
    tex.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    tex.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* raw_tex = nullptr;
    const HRESULT hr = device_->CreateCommittedResource(
        &heap_props, D3D12_HEAP_FLAG_NONE, &tex,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
        __uuidof(ID3D12Resource), reinterpret_cast<void**>(&raw_tex));
    if (FAILED(hr) || raw_tex == nullptr) {
        rtx::logError(std::format("createComputeTarget failed: hr=0x{:08X} ({}x{})",
                                  static_cast<unsigned long>(hr), width_, height_));
        return false;
    }
    compute_target_.reset(raw_tex);
    return true;
}
bool PreviewViewport::createCompositionSwapChain() noexcept {
    if (child_hwnd_) {
        RECT rc{};
        if (rtx::sys::getClientRect(child_hwnd_.get(), &rc)) {
            target_width_ = std::max(1, static_cast<int>(rc.right - rc.left));
            target_height_ = std::max(1, static_cast<int>(rc.bottom - rc.top));
        }
    }
    width_ = std::max(1, target_width_);
    height_ = std::max(1, target_height_);
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(width_);
    desc.Height = static_cast<UINT>(height_);
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = kSwapChainBuffers;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    desc.Flags = kSwapChainFlags;

    swap_chain_.reset();
    IDXGISwapChain1* raw_chain = nullptr;
    HRESULT hr = factory_->CreateSwapChainForComposition(cmd_queue_.get(), &desc, nullptr,
                                                         &raw_chain);
    if (FAILED(hr)) {
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        hr = factory_->CreateSwapChainForComposition(cmd_queue_.get(), &desc, nullptr, &raw_chain);
    }
    if (FAILED(hr)) {
        rtx::logError(std::format("CreateSwapChainForComposition failed: hr=0x{:08X} ({}x{})",
                                  static_cast<unsigned long>(hr), desc.Width, desc.Height));
        return false;
    }
    IDXGISwapChain4* raw_swap = nullptr;
    const bool queried = SUCCEEDED(raw_chain->QueryInterface(
        __uuidof(IDXGISwapChain4), reinterpret_cast<void**>(&raw_swap)));
    raw_chain->Release();
    if (!queried) {
        return false;
    }
    swap_chain_.reset(raw_swap);

    hdr_output_ = false;
    UINT support = 0;
    if (SUCCEEDED(swap_chain_->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709,
                                                      &support)) &&
        (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0) {
        if (SUCCEEDED(swap_chain_->ResizeBuffers(kSwapChainBuffers, desc.Width, desc.Height,
                                                 DXGI_FORMAT_R16G16B16A16_FLOAT, kSwapChainFlags)) &&
            SUCCEEDED(swap_chain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709))) {
            hdr_output_ = true;
        }
    }
    if (!hdr_output_) {
        (void)swap_chain_->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    }
    if (FAILED(swap_chain_->SetMaximumFrameLatency(1))) {
        return false;
    }
    for (UINT i = 0; i < kSwapChainBuffers; ++i) {
        ID3D12Resource* raw_target = nullptr;
        if (FAILED(swap_chain_->GetBuffer(i, __uuidof(ID3D12Resource),
                                          reinterpret_cast<void**>(&raw_target)))) {
            return false;
        }
        render_targets_[i].reset(raw_target);
    }
    return createComputeTarget();
}
bool PreviewViewport::adoptDevice(void* borrowed_device) noexcept {
    auto* raw = static_cast<ID3D12Device*>(borrowed_device);
    if (raw == nullptr) {
        return false;
    }
    if (device_.get() != raw) {
        if (visual_) {
            (void)visual_->SetContent(nullptr);
        }
        if (dcomp_target_) {
            (void)dcomp_target_->SetRoot(nullptr);
        }
        if (dcomp_device_) {
            (void)dcomp_device_->Commit();
        }
        releaseGpu(false);
    }
    raw->AddRef();
    device_.reset(raw);

    D3D12_COMMAND_QUEUE_DESC queue{};
    queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queue.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queue.NodeMask = 0;
    if (!callComOut(device_.get(), &ID3D12Device::CreateCommandQueue, cmd_queue_, &queue)) {
        return false;
    }
    if (!callComOut(device_.get(), &ID3D12Device::CreateCommandAllocator, cmd_allocator_,
                    D3D12_COMMAND_LIST_TYPE_DIRECT)) {
        return false;
    }
    if (!callComOut(device_.get(), &ID3D12Device::CreateCommandList, cmd_list_, 0u,
                    D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_allocator_.get(), nullptr)) {
        return false;
    }
    (void)cmd_list_->Close();
    if (!callComOut(device_.get(), &ID3D12Device::CreateFence, cmd_fence_, 0u,
                    D3D12_FENCE_FLAG_NONE)) {
        return false;
    }
    fence_event_.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!fence_event_) {
        return false;
    }
    fence_value_ = 0;
    device_lost_ = false;
    ID3D12Fence* fence = cmd_fence_.get();
    gpuSetProbe(retirement_, [fence] { return fence->GetCompletedValue(); });

    if (!buildComputePipeline(device_.get(), root_signature_, pipeline_)) {
        return false;
    }
    if (!createViewHeap()) {
        return false;
    }
    IDXGIFactory2* raw_factory = nullptr;
    if (FAILED(rtx::sys::createDxgiFactory1(__uuidof(IDXGIFactory2),
                                  reinterpret_cast<void**>(&raw_factory)))) {
        return false;
    }
    factory_.reset(raw_factory);
    if (!createCompositionSwapChain()) {
        return false;
    }
    dcomp_device_.reset();
    IDCompositionDevice* raw_dcomp = nullptr;
    if (FAILED(dcomp_create_(nullptr, __uuidof(IDCompositionDevice),
                             reinterpret_cast<void**>(&raw_dcomp)))) {
        return false;
    }
    dcomp_device_.reset(raw_dcomp);
    dcomp_target_.reset();
    IDCompositionTarget* raw_target = nullptr;
    // Doc 47 (Defect 4): topmost=FALSE so DComp visual stays in normal HWND Z-order below menus/toasts.
    if (FAILED(dcomp_device_->CreateTargetForHwnd(child_hwnd_.get(), FALSE, &raw_target))) {
        return false;
    }
    dcomp_target_.reset(raw_target);
    visual_.reset();
    IDCompositionVisual* raw_visual = nullptr;
    if (FAILED(dcomp_device_->CreateVisual(&raw_visual))) {
        return false;
    }
    visual_.reset(raw_visual);
    if (FAILED(visual_->SetContent(swap_chain_.get()))) {
        return false;
    }
    clip_.reset();
    IDCompositionRectangleClip* raw_clip = nullptr;
    if (SUCCEEDED(dcomp_device_->CreateRectangleClip(&raw_clip)) && raw_clip != nullptr) {
        clip_.reset(raw_clip);
        (void)clip_->SetLeft(0.0f);
        (void)clip_->SetTop(0.0f);
        (void)clip_->SetRight(static_cast<float>(width_));
        (void)clip_->SetBottom(static_cast<float>(height_));
        (void)visual_->SetClip(clip_.get());
    }
    if (FAILED(dcomp_target_->SetRoot(visual_.get()))) {
        return false;
    }
    return SUCCEEDED(dcomp_device_->Commit());
}
void PreviewViewport::setBounds(int x, int y, int w, int h) noexcept {
    if (!child_hwnd_) {
        return;
    }
    rtx::sys::setWindowPos(child_hwnd_.get(), HWND_TOP, x, y, std::max(1, w), std::max(1, h),
                 SWP_NOACTIVATE);
    RECT rc{};
    if (rtx::sys::getClientRect(child_hwnd_.get(), &rc)) {
        target_width_ = std::max(1, static_cast<int>(rc.right - rc.left));
        target_height_ = std::max(1, static_cast<int>(rc.bottom - rc.top));
    } else {
        target_width_ = std::max(1, w);
        target_height_ = std::max(1, h);
    }
    if (swap_chain_) {
        resizeSwapChain(target_width_, target_height_);
    }
}
void PreviewViewport::resizeSwapChain(int width, int height) noexcept {
    if (!swap_chain_ || width <= 0 || height <= 0 || device_lost_) {
        return;
    }
    if (width == width_ && height == height_) {
        return;
    }
    if (!waitForGpuIdle(100).has_value()) {
        return;
    }
    for (auto& rt : render_targets_) {
        rt.reset();
    }
    (void)cmd_allocator_->Reset();
    (void)cmd_list_->Reset(cmd_allocator_.get(), nullptr);
    (void)cmd_list_->Close();

    if (FAILED(swap_chain_->ResizeBuffers(kSwapChainBuffers, static_cast<UINT>(width),
                                          static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN,
                                          kSwapChainFlags))) {
        for (UINT i = 0; i < kSwapChainBuffers; ++i) {
            ID3D12Resource* raw_target = nullptr;
            if (SUCCEEDED(swap_chain_->GetBuffer(i, __uuidof(ID3D12Resource),
                                                 reinterpret_cast<void**>(&raw_target)))) {
                render_targets_[i].reset(raw_target);
            }
        }
        return;
    }
    width_ = width;
    height_ = height;
    view_heap_.reset();
    for (UINT i = 0; i < kSwapChainBuffers; ++i) {
        ID3D12Resource* raw_target = nullptr;
        if (FAILED(swap_chain_->GetBuffer(i, __uuidof(ID3D12Resource),
                                          reinterpret_cast<void**>(&raw_target)))) {
            return;
        }
        render_targets_[i].reset(raw_target);
    }
    (void)createComputeTarget();
    (void)createViewHeap();
    if (clip_) {
        (void)clip_->SetRight(static_cast<float>(width_));
        (void)clip_->SetBottom(static_cast<float>(height_));
    }
    if (dcomp_device_) {
        (void)dcomp_device_->Commit();
    }
}
void PreviewViewport::presentShared(void* borrowed_d3d12_device, void* shared_resource,
                                    std::uint32_t pitch, std::uint32_t src_width,
                                    std::uint32_t src_height, bool input_is_pq, float max_nits,
                                    const PreviewSlotTicket& ticket) noexcept {
    const std::uint32_t color_flags = input_is_pq
        ? (kPreviewLeftPq | kPreviewLeftBt2020 | kPreviewRightPq | kPreviewRightBt2020)
        : 0u;
    const float nits = (max_nits > 0.0f) ? max_nits : kSdrReferenceNits;
    presentShared(borrowed_d3d12_device, shared_resource, pitch, src_width, src_height,
                  color_flags, nits, nits, ticket);
}

void PreviewViewport::presentShared(void* borrowed_d3d12_device, void* shared_resource,
                                    std::uint32_t pitch, std::uint32_t src_width,
                                    std::uint32_t src_height, std::uint32_t color_flags,
                                    float left_max_nits, float right_max_nits,
                                    const PreviewSlotTicket& ticket) noexcept {
    last_present_skipped_ = true;
    const auto release_ticket = [](const PreviewSlotTicket* t) noexcept {
        if (t != nullptr && t->first != nullptr && t->second < t->first->size()) {
            (*t->first)[t->second].store(0, std::memory_order_release);
        }
    };
    std::unique_ptr<const PreviewSlotTicket, decltype(release_ticket)> ticket_guard(&ticket, release_ticket);
    if (borrowed_d3d12_device == nullptr || shared_resource == nullptr ||
        src_width == 0 || src_height == 0 || pitch < src_width * 4u || (pitch % 4u) != 0u) {
        return;
    }
    if (device_lost_) {
        return;
    }
    if (device_.get() != static_cast<ID3D12Device*>(borrowed_d3d12_device)) {
        if (!adoptDevice(borrowed_d3d12_device)) {
            return;
        }
    }
    if (swap_chain_ && (target_width_ != width_ || target_height_ != height_)) {
        resizeSwapChain(target_width_, target_height_);
    }
    if (!swap_chain_ || !pipeline_ || !view_heap_ || !cmd_list_ || !compute_target_) {
        return;
    }
    if (device_->GetDeviceRemovedReason() != S_OK) {
        device_lost_ = true;
        return;
    }
    if (!waitForGpuIdle(0).has_value()) {
        return;
    }
    if (WaitForSingleObject(swap_chain_->GetFrameLatencyWaitableObject(), 16) != WAIT_OBJECT_0) {
        return;
    }
    const UINT back_buffer_idx = swap_chain_->GetCurrentBackBufferIndex();
    if (back_buffer_idx >= kSwapChainBuffers || !render_targets_[back_buffer_idx]) {
        return;
    }
    ID3D12Resource* back_buffer = render_targets_[back_buffer_idx].get();
    const DXGI_FORMAT back_format =
        hdr_output_ ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;

    const D3D12_CPU_DESCRIPTOR_HANDLE cpu = view_heap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_R32_UINT;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.FirstElement = 0;
    srv.Buffer.NumElements = (pitch / 4u) * src_height;
    srv.Buffer.StructureByteStride = 0;
    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device_->CreateShaderResourceView(static_cast<ID3D12Resource*>(shared_resource), &srv, cpu);

    D3D12_CPU_DESCRIPTOR_HANDLE uav_handle = cpu;
    uav_handle.ptr += view_descriptor_size_;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = back_format;
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav.Texture2D.MipSlice = 0;
    uav.Texture2D.PlaneSlice = 0;
    device_->CreateUnorderedAccessView(compute_target_.get(), nullptr, &uav, uav_handle);

    if (FAILED(cmd_allocator_->Reset()) || FAILED(cmd_list_->Reset(cmd_allocator_.get(), nullptr))) {
        return;
    }
    cmd_list_->SetComputeRootSignature(root_signature_.get());
    cmd_list_->SetPipelineState(pipeline_.get());
    ID3D12DescriptorHeap* heaps[] = {view_heap_.get()};
    cmd_list_->SetDescriptorHeaps(1, heaps);
    cmd_list_->SetComputeRootDescriptorTable(0, view_heap_->GetGPUDescriptorHandleForHeapStart());

    const bool split_on = split_ratio_ > 0.0f && split_ratio_ < 1.0f;
    const float l_nits = (left_max_nits > 0.0f) ? left_max_nits : kSdrReferenceNits;
    const float r_nits = (right_max_nits > 0.0f) ? right_max_nits : kSdrReferenceNits;
    const ViewportConstants constants{
        src_width,
        src_height,
        static_cast<std::uint32_t>(width_),
        static_cast<std::uint32_t>(height_),
        pitch / 4u,
        hdr_output_ ? 1u : 0u,
        std::bit_cast<std::uint32_t>(split_ratio_),
        std::bit_cast<std::uint32_t>(l_nits),
        split_on ? 1u : 0u,
        color_flags,
        std::bit_cast<std::uint32_t>(r_nits),
        0u,
    };
    cmd_list_->SetComputeRoot32BitConstants(1, kRootConstantDwords, constants.data(), 0);

    cmd_list_->Dispatch((static_cast<UINT>(width_) + kComputeGroup - 1) / kComputeGroup,
                        (static_cast<UINT>(height_) + kComputeGroup - 1) / kComputeGroup, 1);
    resourceBarrier(cmd_list_.get(), compute_target_.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_COPY_SOURCE);
    resourceBarrier(cmd_list_.get(), back_buffer, D3D12_RESOURCE_STATE_PRESENT,
                    D3D12_RESOURCE_STATE_COPY_DEST);
    cmd_list_->CopyResource(back_buffer, compute_target_.get());
    resourceBarrier(cmd_list_.get(), compute_target_.get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    resourceBarrier(cmd_list_.get(), back_buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_PRESENT);

    if (FAILED(cmd_list_->Close())) {
        return;
    }
    ID3D12CommandList* lists[] = {cmd_list_.get()};
    cmd_queue_->ExecuteCommandLists(1, lists);

    last_present_skipped_ = FAILED(swap_chain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT));

    ++fence_value_;
    if (FAILED(cmd_queue_->Signal(cmd_fence_.get(), fence_value_))) {
        device_lost_ = true;
        gpuReleaseAll(retirement_);
        return;
    }
    (void)ticket_guard.release();
    auto* res = static_cast<IUnknown*>(shared_resource);
    res->AddRef();
    auto flags = ticket.first;
    const std::uint32_t slot_index = ticket.second;
    gpuRetire(retirement_, fence_value_, res, [flags = std::move(flags), slot_index] {
        if (flags != nullptr && slot_index < flags->size()) {
            (*flags)[slot_index].store(0, std::memory_order_release);
        }
    });
}
}  // namespace rtx::gui
