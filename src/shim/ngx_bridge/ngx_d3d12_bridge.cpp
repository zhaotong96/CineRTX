/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * VSR -> TrueHDR evaluator on the interop-owned D3D12 queue.  See
 * ngx_d3d12_bridge.h.  The proprietary headers are reached only through the
 * RTX_WITH_NGX_SDK build, which links NVIDIA's nvsdk_ngx_s.lib stub.
 */

#include "ngx_d3d12_bridge.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d12.h>

#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers_truehdr.h"
#include "nvsdk_ngx_helpers_vsr.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "d3d12_interop.h"

namespace rtx_ngx {
namespace {

const GUID kIidCommandAllocator = {0x6102dee4,
                                   0xaf59,
                                   0x4b09,
                                   {0xb9, 0x99, 0xb4, 0x4d, 0x73, 0xf0, 0x9b, 0x24}};
const GUID kIidGraphicsCommandList = {0x5b160d0f,
                                      0xac1b,
                                      0x4185,
                                      {0x8b, 0xa8, 0xb3, 0xae, 0x42, 0xa5, 0xa4, 0x55}};

constexpr auto kFenceTimeout = std::chrono::milliseconds(10000);
constexpr double kDefaultSmoothing = 0.92;
constexpr double kDarkY = 0.18;
constexpr double kHighP = 0.05;
constexpr double kHighPSpan = 0.15;

// Opt-in bring-up trace (RTX_BRIDGE_TRACE=1); mirrors d3d12_interop.cpp.
void trace(const char* step) {
    static const bool enabled = [] {
        const char* value = std::getenv("RTX_BRIDGE_TRACE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    if (!enabled) {
        return;
    }
    std::fprintf(stderr, "[rtx_ngx_ngx] %s\n", step);
    std::fflush(stderr);
}

template <typename T>
T* as(void* pointer) {
    return static_cast<T*>(pointer);
}

std::string ngxResultText(const char* call, NVSDK_NGX_Result result) {
    char text[160] = {};
    std::snprintf(text, sizeof(text), "%s failed (NVSDK_NGX_Result 0x%08X)", call,
                  static_cast<unsigned int>(result));
    return text;
}

bool fileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// NGX wants a writable application-data directory.  Prefer
// %LOCALAPPDATA%\RTXTranscode\ngx, fall back to %TEMP%, and finally to the
// plugin directory the caller deployed the feature DLLs into.
std::wstring makeAppDataDirectory(const std::wstring& fallback) {
    auto prepare = [](const wchar_t* base, const wchar_t* suffix) -> std::wstring {
        if (base == nullptr || base[0] == L'\0') {
            return std::wstring();
        }
        std::wstring directory(base);
        directory += L"\\RTXTranscode";
        CreateDirectoryW(directory.c_str(), nullptr);
        directory += L"\\";
        directory += suffix;
        CreateDirectoryW(directory.c_str(), nullptr);
        return directory;
    };
    wchar_t buffer[32768] = {};
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, 32768);
    if (length > 0 && length < 32768) {
        std::wstring directory = prepare(buffer, L"ngx");
        if (!directory.empty()) {
            return directory;
        }
    }
    length = GetEnvironmentVariableW(L"TEMP", buffer, 32768);
    if (length > 0 && length < 32768) {
        std::wstring directory = prepare(buffer, L"RTXTranscode_ngx");
        if (!directory.empty()) {
            return directory;
        }
    }
    return fallback;
}

}  // namespace

NgxD3D12Bridge::NgxD3D12Bridge() = default;

NgxD3D12Bridge::~NgxD3D12Bridge() { destroy(); }

bool NgxD3D12Bridge::create(D3D12Interop& interop, const RtxNgxBridgeConfig& config,
                            std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        *error = "the NGX bridge is already initialized";
        return false;
    }
    if (!interop.valid()) {
        *error = "the D3D12/CUDA interop is not initialized";
        return false;
    }
    input_width_ = config.input_width;
    input_height_ = config.input_height;
    output_width_ = config.output_width;
    output_height_ = config.output_height;
    vsr_quality_ = std::clamp(config.vsr_quality, 1, 4);
    node_mask_ = config.luid_node_mask != 0 ? config.luid_node_mask : 1u;
    needs_vsr_ = output_width_ > input_width_ || output_height_ > input_height_;
    needs_truehdr_ = config.enable_truehdr != 0;
    needs_mid_ = needs_vsr_ && needs_truehdr_;
    if (!needs_vsr_ && !needs_truehdr_) {
        *error = "the NGX configuration requests neither VSR upscaling nor TrueHDR";
        return false;
    }
    if (needs_vsr_ &&
        (output_width_ < input_width_ || output_height_ < input_height_)) {
        *error = "VSR output geometry is smaller than the input";
        return false;
    }
    if (input_width_ <= 0 || input_height_ <= 0 || output_width_ <= 0 ||
        output_height_ <= 0) {
        *error = "the NGX configuration has non-positive geometry";
        return false;
    }

    const bool golden = config.contrast == 0 && config.saturation == 0 &&
                        config.middle_gray == 0;
    base_contrast_ = golden ? 125 : std::clamp(config.contrast, 0, 200);
    base_saturation_ = golden ? 75 : std::clamp(config.saturation, 0, 200);
    base_middle_gray_ = golden ? 44 : std::clamp(config.middle_gray, 10, 100);
    max_luminance_ = config.max_luminance == 0 ? 1000
                                               : std::clamp(config.max_luminance, 400, 2000);
    contrast_ = base_contrast_;
    saturation_ = base_saturation_;
    middle_gray_ = base_middle_gray_;
    smooth_contrast_ = static_cast<double>(contrast_);
    smooth_saturation_ = static_cast<double>(saturation_);
    smooth_middle_gray_ = static_cast<double>(middle_gray_);
    stats_initialized_ = false;
    has_pending_stats_ = false;

    plugin_dir_ = rtx_ngx::plugin_directory();
    app_data_dir_ = makeAppDataDirectory(plugin_dir_);
    if (needs_vsr_ && !fileExists(plugin_dir_ + L"\\nvngx_vsr.dll")) {
        *error = "nvngx_vsr.dll is missing from the NGX plugin directory";
        return false;
    }
    if (needs_truehdr_ && !fileExists(plugin_dir_ + L"\\nvngx_truehdr.dll")) {
        *error = "nvngx_truehdr.dll is missing from the NGX plugin directory";
        return false;
    }

    ID3D12Device* device = as<ID3D12Device>(interop.device());
    if (device == nullptr) {
        *error = "the interop device is missing";
        return false;
    }
    device_ = device;
    interop_ = &interop;

    auto cleanup = [this]() {
        release_features();
        if (command_list_ != nullptr) {
            as<ID3D12GraphicsCommandList>(command_list_)->Release();
            command_list_ = nullptr;
        }
        if (allocator_ != nullptr) {
            as<ID3D12CommandAllocator>(allocator_)->Release();
            allocator_ = nullptr;
        }
        device_ = nullptr;
        interop_ = nullptr;
        initialized_ = false;
    };
    auto fail = [error, &cleanup](const std::string& text) {
        cleanup();
        *error = text;
        return false;
    };

    ID3D12CommandAllocator* allocator = nullptr;
    HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 kIidCommandAllocator,
                                                 reinterpret_cast<void**>(&allocator));
    if (FAILED(hr) || allocator == nullptr) {
        return fail("CreateCommandAllocator for the NGX command list failed");
    }
    allocator_ = allocator;
    ID3D12GraphicsCommandList* list = nullptr;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                   kIidGraphicsCommandList,
                                   reinterpret_cast<void**>(&list));
    if (FAILED(hr) || list == nullptr) {
        return fail("CreateCommandList for the NGX evaluator failed");
    }
    command_list_ = list;
    if (FAILED(list->Close())) {
        return fail("closing the initial NGX command list failed");
    }
    // The reference implementation makes sure the queue and allocator are live
    // before NGX takes over the device.
    {
        ID3D12CommandList* lists[] = {list};
        as<ID3D12CommandQueue>(interop.queue())->ExecuteCommandLists(1, lists);
    }
    std::string flush_error;
    if (!interop.flush_queue(&flush_error)) {
        return fail("initial NGX queue submission failed: " + flush_error);
    }

    NVSDK_NGX_FeatureCommonInfo common{};
    wchar_t const* paths[1] = {plugin_dir_.c_str()};
    common.PathListInfo.Path = paths;
    common.PathListInfo.Length = 1;
    common.InternalData = nullptr;
    common.LoggingInfo.LoggingCallback = nullptr;
    common.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_OFF;
    common.LoggingInfo.DisableOtherLoggingSinks = false;

    NVSDK_NGX_Result status = NVSDK_NGX_D3D12_Init(
        static_cast<unsigned long long>(config.project_id), app_data_dir_.c_str(), device,
        &common, NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(status)) {
        return fail(ngxResultText("NVSDK_NGX_D3D12_Init", status));
    }
    ngx_initialized_ = true;

    NVSDK_NGX_Parameter* params = nullptr;
    status = NVSDK_NGX_D3D12_GetCapabilityParameters(&params);
    if (NVSDK_NGX_FAILED(status) || params == nullptr) {
        return fail(ngxResultText("NVSDK_NGX_D3D12_GetCapabilityParameters", status));
    }
    ngx_params_ = params;

    if (FAILED(allocator->Reset()) || FAILED(list->Reset(allocator, nullptr))) {
        return fail("resetting the NGX command list for feature creation failed");
    }
    if (!create_features(error)) {
        cleanup();
        return false;
    }
    if (FAILED(list->Close())) {
        return fail("closing the feature-creation command list failed");
    }
    {
        ID3D12CommandList* lists[] = {list};
        as<ID3D12CommandQueue>(interop.queue())->ExecuteCommandLists(1, lists);
    }
    if (!interop.flush_queue(&flush_error)) {
        return fail("NGX feature creation submission failed: " + flush_error);
    }

    char text[256] = {};
    std::snprintf(text, sizeof(text),
                  "NGX ready: %dx%d -> %dx%d, VSR quality %d, TrueHDR %s, mid texture %s",
                  input_width_, input_height_, output_width_, output_height_, vsr_quality_,
                  needs_truehdr_ ? "on" : "off", needs_mid_ ? "RGBA8 4K" : "unused");
    diagnostics_ = text;
    initialized_ = true;
    return true;
}

bool NgxD3D12Bridge::create_features(std::string* error) {
    NVSDK_NGX_Parameter* params = as<NVSDK_NGX_Parameter>(ngx_params_);
    ID3D12GraphicsCommandList* list = as<ID3D12GraphicsCommandList>(command_list_);

    auto feature_ready = [this, error, params, list](bool truehdr) -> bool {
        const char* name = truehdr ? "TrueHDR" : "VSR";
        const char* available_name = truehdr ? NVSDK_NGX_Parameter_TrueHDR_Available
                                             : NVSDK_NGX_Parameter_VSR_Available;
        int available = 0;
        NVSDK_NGX_Result status =
            NVSDK_NGX_Parameter_GetI(params, available_name, &available);
        if (NVSDK_NGX_FAILED(status) || available == 0) {
            *error = std::string("NGX reports ") + name +
                     " as unavailable on this driver/adapter";
            return false;
        }
        size_t scratch = 0;
        status = NVSDK_NGX_D3D12_GetScratchBufferSize(
            truehdr ? NVSDK_NGX_Feature_TrueHDR : NVSDK_NGX_Feature_VSR, params, &scratch);
        if (NVSDK_NGX_FAILED(status)) {
            *error = ngxResultText("NVSDK_NGX_D3D12_GetScratchBufferSize", status);
            return false;
        }
        if (scratch != 0) {
            char text[96] = {};
            std::snprintf(text, sizeof(text),
                          "%s unexpectedly needs %llu scratch bytes", name,
                          static_cast<unsigned long long>(scratch));
            *error = text;
            return false;
        }
        NVSDK_NGX_Feature_Create_Params create{};
        if (truehdr) {
            status = NGX_D3D12_CREATE_TRUEHDR_EXT(list, node_mask_, node_mask_,
                                                  reinterpret_cast<NVSDK_NGX_Handle**>(
                                                      &truehdr_handle_),
                                                  params, &create);
        } else {
            status = NGX_D3D12_CREATE_VSR_EXT(list, node_mask_, node_mask_,
                                              reinterpret_cast<NVSDK_NGX_Handle**>(
                                                  &vsr_handle_),
                                              params, &create);
        }
        if (NVSDK_NGX_FAILED(status)) {
            *error = ngxResultText(truehdr ? "NGX_D3D12_CREATE_TRUEHDR_EXT"
                                           : "NGX_D3D12_CREATE_VSR_EXT",
                                   status);
            return false;
        }
        return true;
    };

    if (needs_truehdr_ && !feature_ready(true)) {
        return false;
    }
    if (needs_vsr_ && !feature_ready(false)) {
        return false;
    }
    return true;
}

bool NgxD3D12Bridge::process(uint64_t input_fence_value, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || interop_ == nullptr) {
        *error = "the NGX bridge is not initialized";
        return false;
    }
    if (!apply_pending_stats(error)) {
        return false;
    }
    if (interop_->pollDeviceLost(error)) {
        return false;
    }
    // A single command allocator can only be reset once the GPU retired the
    // previous list.  The queue itself is capped at two submissions, but the
    // allocator needs the stricter guarantee.
    if (!interop_->wait_for_fence_value(pending_signal_, kFenceTimeout, error)) {
        return false;
    }
    if (!interop_->wait_for_queue_depth(2, kFenceTimeout, error)) {
        return false;
    }
    if (!interop_->queue_wait_fence(input_fence_value, error)) {
        return false;
    }

    ID3D12CommandAllocator* allocator = as<ID3D12CommandAllocator>(allocator_);
    ID3D12GraphicsCommandList* list = as<ID3D12GraphicsCommandList>(command_list_);
    if (FAILED(allocator->Reset()) || FAILED(list->Reset(allocator, nullptr))) {
        *error = "resetting the NGX command list failed";
        return false;
    }

    ID3D12Resource* input = as<ID3D12Resource>(interop_->input().resource);
    ID3D12Resource* output = as<ID3D12Resource>(interop_->output().resource);
    ID3D12Resource* mid = needs_mid_ ? as<ID3D12Resource>(interop_->mid().resource) : nullptr;
    const int common = static_cast<int>(D3D12_RESOURCE_STATE_COMMON);
    const int srv = static_cast<int>(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    const int uav = static_cast<int>(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    int input_state = common;
    int output_state = common;
    int mid_state = common;
    auto move = [this](ID3D12Resource* resource, int& state, int next) {
        if (resource == nullptr || state == next) {
            return;
        }
        transition(resource, state, next);
        state = next;
    };

    // The evaluator can fail after some barriers were recorded.  Put every
    // touched resource back into COMMON, close the list and keep the queue
    // timeline monotonic so a retry or a clean destroy stays valid.
    bool eval_ok = true;
    std::string eval_error;

    move(input, input_state, srv);
    if (needs_vsr_) {
        ID3D12Resource* target = needs_mid_ ? mid : output;
        int& target_state = needs_mid_ ? mid_state : output_state;
        move(target, target_state, uav);
        NVSDK_NGX_D3D12_VSR_Eval_Params eval{};
        eval.pInput = input;
        eval.pOutput = target;
        eval.InputSubrectBase = {0, 0};
        eval.InputSubrectSize = {static_cast<unsigned int>(input_width_),
                                 static_cast<unsigned int>(input_height_)};
        eval.OutputSubrectBase = {0, 0};
        eval.OutputSubrectSize = {static_cast<unsigned int>(output_width_),
                                  static_cast<unsigned int>(output_height_)};
        eval.QualityLevel = static_cast<NVSDK_NGX_VSR_QualityLevel>(vsr_quality_);
        const NVSDK_NGX_Result status = NGX_D3D12_EVALUATE_VSR_EXT(
            list, as<NVSDK_NGX_Handle>(vsr_handle_), as<NVSDK_NGX_Parameter>(ngx_params_),
            &eval);
        if (NVSDK_NGX_FAILED(status)) {
            eval_ok = false;
            eval_error = ngxResultText("NGX_D3D12_EVALUATE_VSR_EXT", status);
        } else {
            uav_barrier(target);
            if (needs_mid_) {
                move(mid, mid_state, srv);
            }
        }
    }

    if (eval_ok && needs_truehdr_) {
        ID3D12Resource* source = needs_mid_ ? mid : input;
        move(output, output_state, uav);
        NVSDK_NGX_D3D12_TRUEHDR_Eval_Params eval{};
        eval.pInput = source;
        eval.pOutput = output;
        eval.InputSubrectTL = {0, 0};
        // The helper writes Dimensions.Width/Height into TrueHDR.InRight /
        // InBottom, which the SDK treats as absolute bottom-right coordinates.
        eval.InputSubrectBR = {
            static_cast<unsigned int>(needs_mid_ ? output_width_ : input_width_),
            static_cast<unsigned int>(needs_mid_ ? output_height_ : input_height_)};
        eval.OutputSubrectTL = {0, 0};
        eval.OutputSubrectBR = {static_cast<unsigned int>(output_width_),
                                static_cast<unsigned int>(output_height_)};
        eval.Contrast = static_cast<unsigned int>(contrast_);
        eval.Saturation = static_cast<unsigned int>(saturation_);
        eval.MiddleGray = static_cast<unsigned int>(middle_gray_);
        eval.MaxLuminance = static_cast<unsigned int>(max_luminance_);
        const NVSDK_NGX_Result status = NGX_D3D12_EVALUATE_TRUEHDR_EXT(
            list, as<NVSDK_NGX_Handle>(truehdr_handle_),
            as<NVSDK_NGX_Parameter>(ngx_params_), &eval);
        if (NVSDK_NGX_FAILED(status)) {
            eval_ok = false;
            eval_error = ngxResultText("NGX_D3D12_EVALUATE_TRUEHDR_EXT", status);
        } else {
            uav_barrier(output);
        }
    }

    if (eval_ok) {
        move(output, output_state, common);
        if (needs_mid_) {
            move(mid, mid_state, common);
        }
        move(input, input_state, common);
    } else {
        // Reverse order of use so the restored states are valid even though the
        // feature never wrote its target.
        if (needs_mid_ && mid_state != common) {
            move(mid, mid_state, common);
        }
        move(output, output_state, common);
        move(input, input_state, common);
    }

    if (FAILED(list->Close())) {
        *error = "closing the NGX command list failed";
        initialized_ = false;
        return false;
    }
    ID3D12CommandList* lists[] = {list};
    as<ID3D12CommandQueue>(interop_->queue())->ExecuteCommandLists(1, lists);

    uint64_t signal = input_fence_value + 1;
    if (signal <= interop_->timeline_value()) {
        signal = interop_->timeline_value() + 1;
    }
    interop_->adopt_timeline(signal);
    if (!interop_->queue_signal_fence(signal, error)) {
        initialized_ = false;
        return false;
    }
    pending_signal_ = signal;

    if (!eval_ok) {
        initialized_ = false;
        *error = eval_error;
        return false;
    }
    return true;
}

bool NgxD3D12Bridge::apply_pending_stats(std::string* error) {
    (void)error;
    if (!has_pending_stats_) {
        return true;
    }
    has_pending_stats_ = false;
    const double y =
        std::isfinite(pending_stats_.y_norm) ? std::clamp(pending_stats_.y_norm, 0.0, 1.0) : 0.0;
    const double p =
        std::isfinite(pending_stats_.p_high) ? std::clamp(pending_stats_.p_high, 0.0, 1.0) : 0.0;

    const double base_contrast = static_cast<double>(base_contrast_);
    const double base_saturation = static_cast<double>(base_saturation_);
    const double base_middle = static_cast<double>(base_middle_gray_);
    const double contrast_dark_min = std::max(0.0, base_contrast - 20.0);
    const double contrast_highlight_cap = std::max(0.0, base_contrast - 15.0);
    const double middle_dark_max = std::min(100.0, base_middle + 4.0);
    const double middle_min = std::max(10.0, base_middle - 8.0);
    const double saturation_min = std::max(0.0, base_saturation - 7.0);

    double contrast = y >= kDarkY
                          ? base_contrast
                          : contrast_dark_min +
                                (base_contrast - contrast_dark_min) * (y / kDarkY);
    if (p > kHighP) {
        contrast = std::min(contrast, contrast_highlight_cap);
    }
    double middle_gray = base_middle;
    if (p > kHighP) {
        middle_gray = base_middle - (base_middle - middle_min) *
                                        std::min(1.0, (p - kHighP) / kHighPSpan);
    } else if (y < kDarkY) {
        middle_gray =
            base_middle + (middle_dark_max - base_middle) * (1.0 - y / kDarkY);
    }
    double saturation = base_saturation;
    if (p > kHighP) {
        saturation = base_saturation - (base_saturation - saturation_min) *
                                           std::min(1.0, (p - kHighP) / kHighPSpan);
    }

    if (!stats_initialized_ || pending_stats_.scene_cut != 0) {
        smooth_contrast_ = contrast;
        smooth_middle_gray_ = middle_gray;
        smooth_saturation_ = saturation;
        stats_initialized_ = true;
    } else {
        smooth_contrast_ =
            kDefaultSmoothing * smooth_contrast_ + (1.0 - kDefaultSmoothing) * contrast;
        smooth_middle_gray_ =
            kDefaultSmoothing * smooth_middle_gray_ + (1.0 - kDefaultSmoothing) * middle_gray;
        smooth_saturation_ =
            kDefaultSmoothing * smooth_saturation_ + (1.0 - kDefaultSmoothing) * saturation;
    }
    contrast_ = std::clamp(static_cast<int>(std::lround(smooth_contrast_)), 0, 200);
    middle_gray_ = std::clamp(static_cast<int>(std::lround(smooth_middle_gray_)), 10, 100);
    saturation_ = std::clamp(static_cast<int>(std::lround(smooth_saturation_)), 0, 200);
    return true;
}

bool NgxD3D12Bridge::flush(std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    trace("flush: begin");
    if (pending_signal_ == 0 || interop_ == nullptr) {
        return true;
    }
    if (!interop_->wait_for_fence_value(pending_signal_, kFenceTimeout, error)) {
        return false;
    }
    pending_signal_ = 0;
    trace("flush: done");
    return true;
}

void NgxD3D12Bridge::set_frame_stats(const RtxNgxFrameStats& stats) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_stats_ = stats;
    has_pending_stats_ = true;
}

void NgxD3D12Bridge::transition(void* resource, int before, int after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = as<ID3D12Resource>(resource);
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = static_cast<D3D12_RESOURCE_STATES>(before);
    barrier.Transition.StateAfter = static_cast<D3D12_RESOURCE_STATES>(after);
    as<ID3D12GraphicsCommandList>(command_list_)->ResourceBarrier(1, &barrier);
}

void NgxD3D12Bridge::uav_barrier(void* resource) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = as<ID3D12Resource>(resource);
    as<ID3D12GraphicsCommandList>(command_list_)->ResourceBarrier(1, &barrier);
}

void NgxD3D12Bridge::release_features() {
    if (vsr_handle_ != nullptr) {
        trace("release: VSR feature");
        (void)NVSDK_NGX_D3D12_ReleaseFeature(as<NVSDK_NGX_Handle>(vsr_handle_));
        vsr_handle_ = nullptr;
        trace("release: VSR feature done");
    }
    if (truehdr_handle_ != nullptr) {
        trace("release: TrueHDR feature");
        (void)NVSDK_NGX_D3D12_ReleaseFeature(as<NVSDK_NGX_Handle>(truehdr_handle_));
        truehdr_handle_ = nullptr;
        trace("release: TrueHDR feature done");
    }
    // The programming guide requires parameter maps to be destroyed before
    // shutdown; doing it afterwards hangs on driver 617.14.
    if (ngx_params_ != nullptr) {
        trace("release: DestroyParameters");
        (void)NVSDK_NGX_D3D12_DestroyParameters(as<NVSDK_NGX_Parameter>(ngx_params_));
        ngx_params_ = nullptr;
        trace("release: DestroyParameters done");
    }
    if (ngx_initialized_) {
        // NVSDK_NGX_D3D12_Shutdown1 is deliberately not called.  On driver
        // 617.14 with the static stub it never returns: the stack settles in
        // _nvngx!Shutdown1 -> nvapi64 -> NvTelemetryAPI64 waiting on a
        // telemetry teardown that never signals (reproduced with both the
        // device and nullptr, before any evaluation).  Feature handles and
        // parameter maps are released above, so the only cost is that the
        // NGX runtime stays initialised until process exit.  One bridge lives
        // for the whole job, so this does not accumulate per frame.
        trace("release: Shutdown1 not called (driver 617.14 telemetry deadlock)");
        ngx_initialized_ = false;
    }
}

void NgxD3D12Bridge::destroy() {
    std::lock_guard<std::mutex> lock(mutex_);
    trace("destroy: begin");
    if (interop_ != nullptr && pending_signal_ != 0) {
        std::string ignored;
        (void)interop_->wait_for_fence_value(pending_signal_, kFenceTimeout, &ignored);
        pending_signal_ = 0;
    }
    release_features();
    trace("destroy: features released");
    if (command_list_ != nullptr) {
        // Never submit a half-recorded command list during teardown; just close it
        // if it was left open so D3D12 does not complain on Release().
        ID3D12GraphicsCommandList* list = as<ID3D12GraphicsCommandList>(command_list_);
        (void)list->Close();
        as<ID3D12GraphicsCommandList>(command_list_)->Release();
        command_list_ = nullptr;
    }
    if (allocator_ != nullptr) {
        as<ID3D12CommandAllocator>(allocator_)->Release();
        allocator_ = nullptr;
    }
    device_ = nullptr;
    interop_ = nullptr;
    initialized_ = false;
    trace("destroy: done");
}

}  // namespace rtx_ngx
