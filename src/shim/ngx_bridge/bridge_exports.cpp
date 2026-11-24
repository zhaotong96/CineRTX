/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * C ABI exports for the isolated NGX bridge.
 * Phase 12 (Wave 12.2-B1): eliminated SdkComponents and BridgeImpl (merged
 * directly into opaque C-ABI handle struct RtxNgxBridge).
 */

#if !defined(RTX_SHIM_STATIC)
#define RTX_SHIM_EXPORTS 1
#endif

#include "ngx_bridge_abi.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>

#include "d3d12_interop.h"
#include "nvof_engine.h"
#include "p010_pack_runtime.h"
#include "rife_engine.h"
#if defined(RTX_HAVE_NGX_SDK)
#include "ngx_d3d12_bridge.h"
#endif

// @rtx-type-justification
// Category: C-ABI
// Why-Std-Fails: ngx_bridge_abi.h 导出的不透明 C ABI 句柄 struct RtxNgxBridge 的唯一实体定义，由 extern "C" create/destroy 跨越 DLL 边界持有并析构。
// Invariant: 所有成员变更与销毁受 remaster_mutex 或独占销毁周期保护；析构顺序先 flush NGX 再释放 CUDA 设备缓冲与事件。
struct RtxNgxBridge {
    RtxNgxBridgeConfig config{};
    std::unique_ptr<rtx_ngx::D3D12Interop> interop;
#if defined(RTX_HAVE_NGX_SDK)
    std::unique_ptr<rtx_ngx::NgxD3D12Bridge> ngx;
#endif
    std::string error;
    /* (vsr_dll_present, truehdr_dll_present, host_stub_linked) */
    std::tuple<bool, bool, bool> sdk{false, false, false};

    std::mutex remaster_mutex;
    uint64_t p010_ring[3] = {0, 0, 0};
    size_t p010_pitch = 0;
    uint64_t p010_bytes = 0;
    uint64_t remaster_frames_issued = 0;
    void* remaster_event[4] = {nullptr, nullptr, nullptr, nullptr};
    bool remaster_events_ready = false;
    RtxNgxRemasterStats remaster_stats{};
    uint64_t stats_devptr = 0;
    uint64_t preview_ring[3] = {0, 0, 0};
    uint32_t preview_pitch = 0;
    bool preview_ready = false;
    uint64_t last_preview_ptr = 0;
    bool headless_playback = false;
    void* lut_tex = nullptr;
    void* lut_array = nullptr;
    int32_t lut_dim = 0;
    uint64_t upload_buffer = 0;
    size_t upload_bytes = 0;
    uint64_t interp_buffer = 0;
    size_t interp_bytes = 0;
    std::unique_ptr<rtx_ngx::NvofEngine> nvof_engine;
    std::unique_ptr<rtx_ngx::RifeEngine> rife_engine;

    ~RtxNgxBridge() {
        rife_engine.reset();
#if defined(RTX_HAVE_NGX_SDK)
        if (ngx) {
            std::string ignored;
            (void)ngx->flush(&ignored);
        }
#endif
        if (interop) {
            std::string ignored;
            for (uint64_t& buffer : p010_ring) {
                if (buffer != 0) { (void)interop->free_device(buffer, &ignored); buffer = 0; }
            }
            for (uint64_t* dev : {&stats_devptr, &upload_buffer, &interp_buffer}) {
                if (*dev != 0) { (void)interop->free_device(*dev, &ignored); *dev = 0; }
            }
            for (uint64_t& buffer : preview_ring) buffer = 0;
            interop->destroy_lut_texture(&lut_tex, &lut_array);
            for (void*& event : remaster_event) { interop->event_destroy(event); event = nullptr; }
        }
        remaster_events_ready = false;
    }
};

namespace {

std::string g_last_bridge_create_error;

constexpr const char* kSdkMissingDiagnostic =
    "rtx_ngx_bridge: the RTX Video SDK (linked nvsdk_ngx_s.lib plus "
    "nvngx_vsr.dll / nvngx_truehdr.dll) is not available, so VSR and TrueHDR cannot run.";

constexpr const char* kInteropOnlyDiagnostic =
    "rtx_ngx_bridge: the D3D12/CUDA interop layer is available, but the RTX "
    "Video SDK feature libraries are not installed, so VSR and TrueHDR cannot run (RTX_ERR_NGX).";

std::tuple<bool, bool, bool> probeSdk() {
    bool vsr = false;
    bool truehdr = false;
    bool host = false;
#if defined(RTX_HAVE_NGX_SDK)
    host = true;
#endif
#if defined(_WIN32)
    const std::wstring directory = rtx_ngx::plugin_directory();
    auto present = [&directory](const wchar_t* name) {
        const std::wstring path = directory + L"\\" + name;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    };
    vsr = present(L"nvngx_vsr.dll");
    truehdr = present(L"nvngx_truehdr.dll");
#endif
    return {vsr, truehdr, host};
}

std::string missingComponents(const std::tuple<bool, bool, bool>& sdk, bool need_vsr, bool need_truehdr) {
    const auto [vsr, truehdr, host] = sdk;
    std::string missing;
    auto append = [&missing](const char* name) {
        if (!missing.empty()) missing += ", ";
        missing += name;
    };
    if ((need_vsr || need_truehdr) && !host) {
        append("nvsdk_ngx_s.lib (RTX Video SDK bridge not compiled on this platform)");
    }
    if (need_vsr && !vsr) append("nvngx_vsr.dll");
    if (need_truehdr && !truehdr) append("nvngx_truehdr.dll");
    return missing;
}

bool validConfig(const RtxNgxBridgeConfig* config) {
    if (!config) return false;
    if (config->input_width <= 0 || config->input_height <= 0 ||
        config->output_width <= 0 || config->output_height <= 0) return false;
    if (config->vsr_quality < 0 || config->vsr_quality > 4) return false;
    if (config->output_width < config->input_width || config->output_height < config->input_height) return false;
    return config->enable_truehdr == 0 || config->enable_truehdr == 1;
}

bool ensurePlaybackRing(RtxNgxBridge* impl) {
    rtx_ngx::D3D12Interop& interop = *impl->interop;
    std::string error;
    if (!interop.create_shared_buffer_ring(3u, static_cast<uint32_t>(impl->config.input_width),
                                           static_cast<uint32_t>(impl->config.input_height), 4u, &error)) {
        impl->error = "rtx_ngx_bridge_create(playback): " + error;
        return false;
    }
    for (uint32_t i = 0; i < 3u && i < interop.previewRingSize(); ++i) {
        impl->preview_ring[i] = interop.previewRingDevicePtr(i);
    }
    impl->preview_pitch = interop.previewRingRowBytes();
    impl->preview_ready = true;
    return true;
}

void copyText(char* destination, size_t capacity, const std::string& text) {
    if (!destination || capacity == 0) return;
#if defined(_WIN32)
    strncpy_s(destination, capacity, text.c_str(), _TRUNCATE);
#else
    std::strncpy(destination, text.c_str(), capacity - 1);
    destination[capacity - 1] = '\0';
#endif
}

#if defined(RTX_HAVE_NGX_SDK)
bool ensureRemasterResources(RtxNgxBridge* impl) {
    if (impl->p010_ring[0] != 0 && impl->p010_ring[1] != 0 && impl->p010_ring[2] != 0 &&
        impl->remaster_events_ready) {
        return true;
    }
    rtx_ngx::D3D12Interop& interop = *impl->interop;
    std::string error;
    if (impl->p010_ring[0] == 0 || impl->p010_ring[1] == 0 || impl->p010_ring[2] == 0) {
        const size_t width_bytes = static_cast<size_t>(impl->config.output_width) * 2u;
        const size_t rows = static_cast<size_t>(impl->config.output_height) * 3u / 2u + 1u;
        for (uint64_t& buffer : impl->p010_ring) {
            if (buffer != 0) continue;
            uint64_t pointer = 0;
            size_t pitch = 0;
            if (!interop.alloc_pitched(&pointer, &pitch, width_bytes, rows, &error)) {
                for (uint64_t& allocated : impl->p010_ring) {
                    if (allocated != 0) {
                        std::string ignored;
                        (void)interop.free_device(allocated, &ignored);
                        allocated = 0;
                    }
                }
                impl->error = "rtx_ngx_bridge_remaster: " + error;
                return false;
            }
            buffer = pointer;
            impl->p010_pitch = pitch;
            std::string clear_err;
            interop.clear_p010_black(buffer, pitch, impl->config.output_width, impl->config.output_height, &clear_err);
        }
        impl->p010_bytes = impl->p010_pitch * static_cast<size_t>(impl->config.output_height) * 3u / 2u;
    }
    if (impl->stats_devptr == 0) {
        uint64_t pointer = 0;
        size_t pitch = 0;
        if (!interop.alloc_pitched(&pointer, &pitch, 4u * sizeof(uint32_t), 1, &error)) {
            impl->error = "rtx_ngx_bridge_remaster: " + error;
            return false;
        }
        impl->stats_devptr = pointer;
    }
    if (!impl->remaster_events_ready) {
        for (void*& event : impl->remaster_event) {
            if (event != nullptr) continue;
            if (!interop.event_create(&event, &error)) {
                for (void*& created : impl->remaster_event) {
                    if (created != nullptr) { interop.event_destroy(created); created = nullptr; }
                }
                impl->error = "rtx_ngx_bridge_remaster: " + error;
                return false;
            }
        }
        impl->remaster_events_ready = true;
    }
    return true;
}
#endif

bool ensurePreviewResources(RtxNgxBridge* impl) {
    if (impl->preview_ready) return true;
    rtx_ngx::D3D12Interop& interop = *impl->interop;
    std::string error;
    constexpr uint32_t kPreviewSlots = 3;
    if (!interop.create_shared_buffer_ring(kPreviewSlots, 960u, 540u, 4u, &error)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return false;
    }
    for (uint32_t i = 0; i < kPreviewSlots && i < interop.previewRingSize(); ++i) {
        impl->preview_ring[i] = interop.previewRingDevicePtr(i);
    }
    impl->preview_pitch = interop.previewRingRowBytes();
    impl->preview_ready = true;
    return true;
}

bool validRemasterInput(const RtxNgxRemasterInput* input) {
    return input != nullptr && input->y_plane != 0 && input->uv_plane != 0 &&
           input->y_pitch != 0 && input->uv_pitch != 0 &&
           (input->format == RTX_PIXEL_NV12 || input->format == RTX_PIXEL_P010) &&
           (input->full_range == 0 || input->full_range == 1) &&
           ((input->matrix >= 0 && input->matrix <= 2) ||
            input->matrix == 9 || input->matrix == 5 || input->matrix == 6);
}

bool ensureInterpBuffer(RtxNgxBridge* impl, size_t needed) {
    if (impl->interp_buffer != 0 && impl->interp_bytes >= needed) return true;
    if (impl->interp_buffer != 0) {
        std::string ignored;
        (void)impl->interop->free_device(impl->interp_buffer, &ignored);
        impl->interp_buffer = 0;
        impl->interp_bytes = 0;
    }
    uint64_t pointer = 0;
    size_t p = 0;
    std::string error;
    if (!impl->interop->alloc_pitched(&pointer, &p, needed, 1, &error)) return false;
    impl->interp_buffer = pointer;
    impl->interp_bytes = needed;
    return true;
}

}  // namespace

extern "C" {

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_query_caps(RtxNgxBridgeCaps* caps) {
    if (!caps) return RTX_ERR_INVALID_ARGUMENT;
    std::memset(caps, 0, sizeof(*caps));
    const auto sdk = probeSdk();
    const auto [vsr, truehdr, host] = sdk;
    caps->vsr_available = (host && vsr) ? 1 : 0;
    caps->truehdr_available = (host && truehdr) ? 1 : 0;
    caps->d3d12_device_created = 0;
    caps->cuda_interop_available = rtx_ngx::interopPrerequisitesAvailable() ? 1 : 0;
    const std::string missing = missingComponents(sdk, true, true);
    if (missing.empty()) {
        copyText(caps->diagnostics, sizeof(caps->diagnostics),
                 "RTX Video SDK ready; D3D12/CUDA interop available");
    } else {
        std::string text = (caps->cuda_interop_available != 0) ? kInteropOnlyDiagnostic : kSdkMissingDiagnostic;
        text += " Missing: " + missing + ".";
        copyText(caps->diagnostics, sizeof(caps->diagnostics), text);
    }
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_create(const RtxNgxBridgeConfig* config,
                                                           RtxNgxBridge** bridge) {
    if (!bridge) return RTX_ERR_INVALID_ARGUMENT;
    *bridge = nullptr;
    if (!validConfig(config)) return RTX_ERR_INVALID_ARGUMENT;

    auto impl = std::make_unique<RtxNgxBridge>();
    impl->config = *config;
    impl->sdk = probeSdk();
    impl->interop = std::make_unique<rtx_ngx::D3D12Interop>();
    std::string error;
    if (!impl->interop->create(*config, &error)) {
        if (impl->interop->valid()) {
            std::string lost_error;
            if (impl->interop->pollDeviceLost(&lost_error) && !lost_error.empty()) error = lost_error;
        }
        impl->error = error.empty() ? "D3D12/CUDA interop initialisation failed" : error;
        return impl->interop->deviceLost() ? RTX_ERR_DEVICE_LOST : RTX_ERR_ARCH_UNSUPPORTED;
    }
    if (config->reserved[0] == 1u) {
        impl->headless_playback = true;
        if (!ensurePlaybackRing(impl.get())) return RTX_ERR_OUT_OF_MEMORY;
        impl->error.clear();
        *bridge = impl.release();
        return RTX_OK;
    }
    const bool need_vsr = config->output_width > config->input_width ||
                          config->output_height > config->input_height;
    const bool need_truehdr = config->enable_truehdr != 0;
    if (const std::string missing = missingComponents(impl->sdk, need_vsr, need_truehdr); !missing.empty()) {
        impl->error = "rtx_ngx_bridge: the RTX Video SDK is incomplete for this configuration; missing: " + missing + ".";
        return RTX_ERR_NGX;
    }
#if defined(RTX_HAVE_NGX_SDK)
    impl->ngx = std::make_unique<rtx_ngx::NgxD3D12Bridge>();
    std::string ngx_error;
    if (!impl->ngx->create(*impl->interop, *config, &ngx_error)) {
        impl->error = "rtx_ngx_bridge: NGX evaluator creation failed: " + ngx_error;
        g_last_bridge_create_error = impl->error;
        return impl->interop->pollDeviceLost(nullptr) ? RTX_ERR_DEVICE_LOST : RTX_ERR_NGX;
    }
#endif
    impl->error.clear();
    *bridge = impl.release();
    return RTX_OK;
}

RTX_SHIM_API void RTX_SHIM_CALL rtx_ngx_bridge_destroy(RtxNgxBridge* bridge) { delete bridge; }

RTX_SHIM_API const char* RTX_SHIM_CALL rtx_ngx_bridge_last_error(RtxNgxBridge* bridge) {
    if (!bridge) {
        return g_last_bridge_create_error.empty() ? kSdkMissingDiagnostic : g_last_bridge_create_error.c_str();
    }
    return bridge->error.c_str();
}

RTX_SHIM_API uint32_t RTX_SHIM_CALL rtx_ngx_bridge_abi_version(void) { return RTX_NGX_BRIDGE_ABI_VERSION; }

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_get_remaster_stats(
    RtxNgxBridge* bridge, RtxNgxRemasterStats* stats) {
    if (!bridge || !stats) return RTX_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(bridge->remaster_mutex);
    *stats = bridge->remaster_stats;
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_remaster(
    RtxNgxBridge* bridge, const RtxNgxRemasterInput* input, RtxNgxRemasterOutput* output,
    RtxNgxRemasterStats* stats) {
    (void)stats;
    if (!bridge || !validRemasterInput(input) || !output) return RTX_ERR_INVALID_ARGUMENT;
    if (!bridge->interop || !bridge->interop->valid()) return RTX_ERR_INVALID_ARGUMENT;
#if !defined(RTX_HAVE_NGX_SDK)
    bridge->error = "rtx_ngx_bridge_remaster: this bridge was built without the RTX Video SDK";
    return RTX_ERR_NGX;
#else
    if (!bridge->ngx) {
        bridge->error = "rtx_ngx_bridge_remaster: the NGX evaluator is not initialised";
        return RTX_ERR_NGX;
    }
    rtx_ngx::D3D12Interop& interop = *bridge->interop;
    std::lock_guard<std::mutex> lock(bridge->remaster_mutex);

    std::string error;
    if (!interop.bind_context(&error)) {
        bridge->error = "rtx_ngx_bridge_remaster: " + error;
        return interop.pollDeviceLost(nullptr) ? RTX_ERR_DEVICE_LOST : RTX_ERR_INTERNAL;
    }
    if (!ensureRemasterResources(bridge)) return RTX_ERR_OUT_OF_MEMORY;
    if (input->preview_rgba10_ptr != 0 && !ensurePreviewResources(bridge)) return RTX_ERR_OUT_OF_MEMORY;
    void* const stream = interop.pack_stream();
    if (!stream) {
        bridge->error = "rtx_ngx_bridge_remaster: the pack stream is unavailable";
        return RTX_ERR_INTERNAL;
    }
    const uint64_t v = interop.advance_timeline();

    if (bridge->stats_devptr != 0 && !interop.memset_d32_async(bridge->stats_devptr, 0, 4, stream, &error)) {
        bridge->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
    if (!interop.event_record(bridge->remaster_event[0], stream, &error)) {
        bridge->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
    int normalized_matrix = input->matrix;
    if (normalized_matrix == 9) normalized_matrix = 1;
    else if (normalized_matrix == 5 || normalized_matrix == 6) normalized_matrix = 2;

    RtxResult launched = rtx_ngx::launchPreprocess(
        std::make_tuple(input->y_plane, input->uv_plane, interop.input_surface(),
                        bridge->config.input_width, bridge->config.input_height,
                        static_cast<int32_t>(input->y_pitch), static_cast<int32_t>(input->uv_pitch),
                        input->format, input->full_range, normalized_matrix),
        stream);
    if (launched != RTX_OK) {
        bridge->error = std::string("rtx_ngx_bridge_remaster: ") + rtx_ngx::packLastError();
        return launched;
    }
    if (!interop.event_record(bridge->remaster_event[1], stream, &error)) {
        bridge->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
    const bool semaphore = interop.semaphore_ready();
    if (semaphore) {
        if (!interop.fence_signal_cuda(stream, v, &error)) {
            bridge->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
    } else {
        if (!interop.stream_synchronize(stream, &error) || !interop.queue_signal_fence(v, &error)) {
            bridge->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
    }
    const auto ngx_started = std::chrono::steady_clock::now();
    if (!bridge->ngx->process(v, &error, input->reset_temporal != 0)) {
        bridge->error = "rtx_ngx_bridge_remaster: " + error;
        return interop.pollDeviceLost(nullptr) ? RTX_ERR_DEVICE_LOST : RTX_ERR_NGX;
    }
    if (semaphore) {
        if (!interop.fence_wait_cuda(stream, v + 1, &error)) {
            bridge->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
    } else if (!interop.wait_for_fence_value(v + 1, std::chrono::milliseconds(10000), &error)) {
        bridge->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
    if (!interop.event_record(bridge->remaster_event[2], stream, &error)) {
        bridge->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
    const uint64_t p010_buffer = bridge->p010_ring[bridge->remaster_frames_issued % 3u];
    const uint32_t pitch_samples = static_cast<uint32_t>(bridge->p010_pitch / 2u);
    const int32_t input_is_pq = (bridge->config.enable_truehdr != 0) ? 0 : 1;
    const int32_t input_is_scrgb = (bridge->config.enable_truehdr != 0) ? 1 : 0;
    const float linear_scale_nits = (bridge->config.enable_truehdr != 0) ? 80.0f : 1.0f;
    const uint64_t preview_ptr = input->preview_rgba10_ptr;
    const int32_t preview_w = preview_ptr ? 960 : 0;
    const int32_t preview_h = preview_ptr ? 540 : 0;
    const int32_t preview_pitch_px = preview_ptr ? static_cast<int32_t>(bridge->preview_pitch / 4u) : 0;
    const float split_x = preview_ptr ? input->split_x_norm : -1.0f;
    const uint64_t src_surface =
        (preview_ptr && bridge->config.input_width == bridge->config.output_width &&
         bridge->config.input_height == bridge->config.output_height)
            ? interop.input_surface()
            : 0;

    launched = rtx_ngx::launchPackV2(
        std::make_tuple(interop.output_surface(), p010_buffer,
                        p010_buffer + bridge->p010_pitch * static_cast<size_t>(bridge->config.output_height),
                        pitch_samples, pitch_samples,
                        bridge->config.output_width, bridge->config.output_height,
                        input_is_pq, input_is_scrgb, linear_scale_nits,
                        reinterpret_cast<uint64_t>(bridge->lut_tex), bridge->lut_dim,
                        bridge->stats_devptr, preview_ptr, preview_w, preview_h,
                        preview_pitch_px, split_x, src_surface),
        stream);
    if (launched != RTX_OK) {
        bridge->error = std::string("rtx_ngx_bridge_remaster: ") + rtx_ngx::packLastError();
        return launched;
    }
    if (!interop.event_record(bridge->remaster_event[3], stream, &error) ||
        !interop.stream_synchronize(stream, &error)) {
        bridge->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
    double preprocess_ms = 0.0, ngx_ms = 0.0, pack_ms = 0.0, frame_ms = 0.0;
    if (!interop.event_elapsed(&preprocess_ms, bridge->remaster_event[0], bridge->remaster_event[1], &error) ||
        !interop.event_elapsed(&pack_ms, bridge->remaster_event[2], bridge->remaster_event[3], &error) ||
        !interop.event_elapsed(&frame_ms, bridge->remaster_event[0], bridge->remaster_event[3], &error)) {
        bridge->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
    if (semaphore) {
        if (!interop.event_elapsed(&ngx_ms, bridge->remaster_event[1], bridge->remaster_event[2], &error)) {
            bridge->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
    } else {
        ngx_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ngx_started).count();
    }
    std::memset(output, 0, sizeof(*output));
    output->y_plane = p010_buffer;
    output->uv_plane = p010_buffer + bridge->p010_pitch * static_cast<size_t>(bridge->config.output_height);
    output->y_pitch = static_cast<uint32_t>(bridge->p010_pitch);
    output->uv_pitch = static_cast<uint32_t>(bridge->p010_pitch);
    output->width = bridge->config.output_width;
    output->height = bridge->config.output_height;
    output->format = RTX_PIXEL_P010;
    output->ring_slot_index = static_cast<uint32_t>(bridge->remaster_frames_issued % 3u);

    uint32_t stats_host[4] = {0, 0, 0, 0};
    if (bridge->stats_devptr != 0 &&
        interop.memcpy_dtoh(stats_host, bridge->stats_devptr, sizeof(stats_host), &error)) {
        const float frame_max = std::bit_cast<float>(stats_host[0]);
        const float frame_sum = std::bit_cast<float>(stats_host[1]);
        const uint32_t samples = static_cast<uint32_t>(bridge->config.output_width) *
                                 static_cast<uint32_t>(bridge->config.output_height);
        const float frame_avg = samples > 0 ? frame_sum / static_cast<float>(samples) : 0.0f;
        output->frame_max_nits = static_cast<uint32_t>(fmaxf(0.0f, frame_max) + 0.5f);
        output->frame_avg_nits = static_cast<uint32_t>(fmaxf(0.0f, frame_avg) + 0.5f);
        if (bridge->ngx && samples > 0) {
            RtxNgxFrameStats adaptive{};
            adaptive.y_norm = std::clamp<double>(
                static_cast<double>(frame_avg) / static_cast<double>(std::max<uint32_t>(1u, bridge->config.max_luminance)),
                0.0, 1.0);
            adaptive.p_high = std::clamp<double>(static_cast<double>(stats_host[2]) / static_cast<double>(samples), 0.0, 1.0);
            bridge->ngx->set_frame_stats(adaptive);
        }
        bridge->last_preview_ptr = input->preview_rgba10_ptr;
    } else if (bridge->stats_devptr != 0) {
        bridge->error = "rtx_ngx_bridge_remaster: statistic readback failed: " + error;
        return RTX_ERR_INTERNAL;
    }
    bridge->remaster_stats.preprocess_ms += preprocess_ms;
    bridge->remaster_stats.ngx_ms += ngx_ms;
    bridge->remaster_stats.pack_kernel_ms += pack_ms;
    bridge->remaster_stats.frame_ms += frame_ms;
    bridge->remaster_stats.frames += 1;
    bridge->remaster_stats.pack_launches += 1;
    bridge->remaster_frames_issued += 1;
    if (stats) *stats = bridge->remaster_stats;
    bridge->error.clear();
    return RTX_OK;
#endif
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_set_lut3d(
    RtxNgxBridge* bridge, const uint16_t* lut_half4_data, int32_t grid_size) {
    if (!bridge || !bridge->interop || !bridge->interop->valid()) return RTX_ERR_INVALID_ARGUMENT;
    if (!lut_half4_data || grid_size <= 1) {
        std::lock_guard<std::mutex> lock(bridge->remaster_mutex);
        bridge->interop->destroy_lut_texture(&bridge->lut_tex, &bridge->lut_array);
        bridge->lut_dim = 0;
        return RTX_OK;
    }
    if (grid_size < 2 || grid_size > 256) return RTX_ERR_INVALID_ARGUMENT;
    if (static_cast<size_t>(grid_size) * grid_size * grid_size * 4u > (1ull << 30)) return RTX_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(bridge->remaster_mutex);
    void* texture_object = nullptr;
    void* array = nullptr;
    std::string error;
    if (!bridge->interop->create_lut_texture(lut_half4_data, grid_size, &texture_object, &array, &error)) {
        bridge->error = "rtx_ngx_bridge_set_lut3d: " + error;
        return RTX_ERR_NGX;
    }
    void* old_tex = bridge->lut_tex;
    void* old_array = bridge->lut_array;
    bridge->lut_tex = texture_object;
    bridge->lut_array = array;
    bridge->lut_dim = grid_size;
    if (old_tex || old_array) bridge->interop->destroy_lut_texture(&old_tex, &old_array);
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_preview_buffer(
    RtxNgxBridge* bridge, uint32_t slot, uint64_t* device_ptr, uint32_t* pitch) {
    if (!bridge || !device_ptr || !pitch || slot > 2) return RTX_ERR_INVALID_ARGUMENT;
    *device_ptr = 0; *pitch = 0;
    if (!bridge->interop || !bridge->interop->valid()) return RTX_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(bridge->remaster_mutex);
    if (!ensurePreviewResources(bridge)) return RTX_ERR_OUT_OF_MEMORY;
    *device_ptr = bridge->preview_ring[slot];
    *pitch = bridge->preview_pitch;
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_download_preview(
    RtxNgxBridge* bridge, void* destination, size_t destination_bytes) {
    if (!bridge || !destination || !bridge->interop || !bridge->interop->valid()) return RTX_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(bridge->remaster_mutex);
    if (bridge->last_preview_ptr == 0 || bridge->preview_pitch == 0) {
        bridge->error = "rtx_ngx_bridge_download_preview: no preview frame was produced yet";
        return RTX_ERR_UNSUPPORTED;
    }
    const size_t needed = static_cast<size_t>(bridge->preview_pitch) * 540u;
    if (destination_bytes < needed) {
        bridge->error = "rtx_ngx_bridge_download_preview: destination is too small";
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::string error;
    if (!bridge->interop->memcpy_dtoh(destination, bridge->last_preview_ptr, needed, &error)) {
        bridge->error = "rtx_ngx_bridge_download_preview: " + error;
        return RTX_ERR_INTERNAL;
    }
    bridge->error.clear();
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_upload_frame(
    RtxNgxBridge* bridge, const void* y_plane, const void* uv_plane, uint32_t y_pitch,
    uint32_t uv_pitch, int32_t width, int32_t height, uint64_t* y_device, uint64_t* uv_device) {
    if (!bridge || !y_plane || !uv_plane || !y_device || !uv_device ||
        y_pitch == 0 || uv_pitch == 0 || width <= 0 || height <= 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *y_device = 0; *uv_device = 0;
    if (!bridge->interop || !bridge->interop->valid()) return RTX_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(bridge->remaster_mutex);
    rtx_ngx::D3D12Interop& interop = *bridge->interop;
    const size_t y_bytes = static_cast<size_t>(y_pitch) * static_cast<size_t>(height);
    const size_t uv_bytes = static_cast<size_t>(uv_pitch) * static_cast<size_t>(height) / 2u;
    const size_t needed = y_bytes + uv_bytes;
    if (needed > bridge->upload_bytes) {
        if (bridge->upload_buffer != 0) {
            std::string ignored;
            (void)interop.free_device(bridge->upload_buffer, &ignored);
            bridge->upload_buffer = 0;
            bridge->upload_bytes = 0;
        }
        uint64_t pointer = 0; size_t pitch = 0; std::string error;
        if (!interop.alloc_pitched(&pointer, &pitch, needed, 1, &error)) {
            bridge->error = "rtx_ngx_bridge_upload_frame: " + error;
            return RTX_ERR_OUT_OF_MEMORY;
        }
        bridge->upload_buffer = pointer;
        bridge->upload_bytes = needed;
    }
    std::string error;
    if (!interop.memcpy_htod(bridge->upload_buffer, y_plane, y_bytes, &error) ||
        !interop.memcpy_htod(bridge->upload_buffer + y_bytes, uv_plane, uv_bytes, &error)) {
        bridge->error = "rtx_ngx_bridge_upload_frame: " + error;
        return RTX_ERR_INTERNAL;
    }
    *y_device = bridge->upload_buffer;
    *uv_device = bridge->upload_buffer + y_bytes;
    bridge->error.clear();
    return RTX_OK;
}

RTX_SHIM_API uint64_t RTX_SHIM_CALL rtx_ngx_bridge_get_interp_buffer(
    RtxNgxBridge* bridge, uint32_t width, uint32_t height) {
    if (!bridge || !bridge->interop || !bridge->interop->valid()) return 0;
    std::lock_guard<std::mutex> lock(bridge->remaster_mutex);
    const uint32_t aligned_width = std::max<uint32_t>(width, (width + 511u) & ~511u);
    const size_t needed = static_cast<size_t>(aligned_width) * height * 3u / 2u;
    return ensureInterpBuffer(bridge, needed) ? bridge->interp_buffer : 0;
}

RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_interpolate_nv12(
    RtxNgxBridge* bridge, uint64_t prev_nv12, uint64_t curr_nv12,
    uint32_t pitch, float alpha, uint64_t out_nv12) {
    if (!bridge || prev_nv12 == 0 || curr_nv12 == 0 || pitch == 0 ||
        !bridge->interop || !bridge->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(bridge->remaster_mutex);
    std::string ctx_err;
    if (!bridge->interop->bind_context(&ctx_err)) {
        bridge->error = "rtx_ngx_bridge_nvof: " + ctx_err;
        return RTX_ERR_INTERNAL;
    }
    const uint32_t width = bridge->config.input_width ? static_cast<uint32_t>(bridge->config.input_width) : pitch;
    const uint32_t height = (bridge->rife_engine && bridge->rife_engine->height() > 0)
                                ? bridge->rife_engine->height()
                                : (bridge->config.input_height ? static_cast<uint32_t>(bridge->config.input_height) : 1080u);
    if (out_nv12 == 0) {
        if (!ensureInterpBuffer(bridge, static_cast<size_t>(pitch) * height * 3u / 2u)) return RTX_ERR_OUT_OF_MEMORY;
        out_nv12 = bridge->interp_buffer;
    }
    if (!bridge->nvof_engine || bridge->nvof_engine->width() != width || bridge->nvof_engine->height() != height) {
        bridge->nvof_engine = std::make_unique<rtx_ngx::NvofEngine>();
        std::string of_err;
        if (!bridge->nvof_engine->init(bridge->interop->context(), width, height, bridge->interop->pack_stream(), &of_err)) {
            return rtx_ngx::launchNvofWarpNv12(prev_nv12, curr_nv12, 0, 0, 0, pitch, width, height,
                                               (width + 3u) / 4u, (height + 3u) / 4u, alpha, out_nv12,
                                               bridge->interop->pack_stream());
        }
    }
    uint64_t fwd_flow = 0, bwd_flow = 0, cost_buffer = 0;
    std::string exec_err;
    RtxResult res = RTX_OK;
    if (!bridge->nvof_engine->execute(prev_nv12, curr_nv12, pitch, bridge->interop->pack_stream(),
                                      &fwd_flow, &bwd_flow, &cost_buffer, &exec_err)) {
        res = rtx_ngx::launchNvofWarpNv12(prev_nv12, curr_nv12, 0, 0, 0, pitch, width, height,
                                          (width + 3u) / 4u, (height + 3u) / 4u, alpha, out_nv12,
                                          bridge->interop->pack_stream(), 0, 0);
    } else {
        res = rtx_ngx::launchNvofWarpNv12(prev_nv12, curr_nv12, fwd_flow, bwd_flow, cost_buffer, pitch, width, height,
                                          bridge->nvof_engine->gridWidth(), bridge->nvof_engine->gridHeight(),
                                          alpha, out_nv12, bridge->interop->pack_stream(),
                                          bridge->nvof_engine->flowStrideBytes(), bridge->nvof_engine->costStrideBytes());
    }
    if (res == RTX_OK) {
        std::string sync_err;
        (void)bridge->interop->stream_synchronize(bridge->interop->pack_stream(), &sync_err);
    }
    return res;
}

RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_init_rife(
    RtxNgxBridge* bridge, const char* engine_path, uint32_t width, uint32_t height) {
    if (!bridge || !engine_path || width == 0 || height == 0 || !bridge->interop || !bridge->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(bridge->remaster_mutex);
    std::string err;
    if (!bridge->interop->bind_context(&err)) {
        bridge->error = "rtx_ngx_bridge_init_rife: " + err;
        return RTX_ERR_INTERNAL;
    }
    if (!bridge->rife_engine) bridge->rife_engine = std::make_unique<rtx_ngx::RifeEngine>();
    if (!bridge->rife_engine->loadEngine(engine_path, width, height, &err)) {
        bridge->error = "rtx_ngx_bridge_init_rife: " + err;
        return RTX_ERR_DLL_MISSING;
    }
    bridge->error.clear();
    return RTX_OK;
}

RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_init_rife_ex(
    RtxNgxBridge* bridge, const char* engine_path, const RtxRifeInitConfig* config, uint64_t* out_trt_peak_bytes) {
    if (!bridge || !engine_path || !config || config->width == 0 || config->height == 0 ||
        !bridge->interop || !bridge->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(bridge->remaster_mutex);
    std::string err;
    if (!bridge->interop->bind_context(&err)) {
        bridge->error = "rtx_ngx_bridge_init_rife_ex: " + err;
        return RTX_ERR_INTERNAL;
    }
    if (!bridge->rife_engine) bridge->rife_engine = std::make_unique<rtx_ngx::RifeEngine>();
    const float scale = (config->scale > 0.0f && config->scale <= 1.0f) ? config->scale : 1.0f;
    const uint32_t align_mod = (config->align_mod > 0u) ? config->align_mod : 128u;
    if (!bridge->rife_engine->loadEngineEx(engine_path, config->width, config->height, scale, align_mod,
                                           config->reflect_padding != 0, out_trt_peak_bytes, &err)) {
        bridge->error = "rtx_ngx_bridge_init_rife_ex: " + err;
        return RTX_ERR_DLL_MISSING;
    }
    bridge->error.clear();
    return RTX_OK;
}

RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_interpolate_rife(
    RtxNgxBridge* bridge, uint64_t prev_nv12, uint64_t curr_nv12, uint32_t pitch,
    float alpha, uint64_t out_nv12, int32_t full_range, int32_t* scene_cut) {
    if (!bridge || prev_nv12 == 0 || curr_nv12 == 0 || pitch == 0 ||
        !bridge->interop || !bridge->interop->valid() || !bridge->rife_engine || !bridge->rife_engine->isLoaded()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(bridge->remaster_mutex);
    std::string ctx_err;
    if (!bridge->interop->bind_context(&ctx_err)) {
        bridge->error = "rtx_ngx_bridge_interpolate_rife: " + ctx_err;
        return RTX_ERR_INTERNAL;
    }
    const uint32_t height = bridge->config.input_height ? static_cast<uint32_t>(bridge->config.input_height) : 1080u;
    const size_t needed = static_cast<size_t>(pitch) * height * 3u / 2u;
    if (out_nv12 == 0 || (bridge->interp_bytes < needed && out_nv12 == bridge->interp_buffer)) {
        if (!ensureInterpBuffer(bridge, needed)) return RTX_ERR_OUT_OF_MEMORY;
        out_nv12 = bridge->interp_buffer;
    }
    int32_t local_scene_cut = 0;
    const int32_t rife_res = bridge->rife_engine->interpolate(
        prev_nv12, curr_nv12, pitch, alpha, out_nv12, full_range, &local_scene_cut, bridge->interop->pack_stream());
    if (scene_cut) *scene_cut = local_scene_cut;
    if (rife_res == RTX_OK) {
        std::string sync_err;
        (void)bridge->interop->stream_synchronize(bridge->interop->pack_stream(), &sync_err);
    } else {
        bridge->error = "rtx_ngx_bridge_interpolate_rife: TensorRT RIFE inference failed";
    }
    return rife_res;
}

RTX_SHIM_API void RTX_SHIM_CALL rtx_ngx_bridge_destroy_rife(RtxNgxBridge* bridge) {
    if (!bridge) return;
    std::lock_guard<std::mutex> lock(bridge->remaster_mutex);
    bridge->rife_engine.reset();
}

RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_borrow_preview(
    RtxNgxBridge* bridge, uint32_t slot, void** out_d3d12_device, void** out_d3d12_resource, uint32_t* out_pitch) {
    if (!bridge || !out_d3d12_device || !out_d3d12_resource || !out_pitch || slot > 2u) return RTX_ERR_INVALID_ARGUMENT;
    *out_d3d12_device = nullptr; *out_d3d12_resource = nullptr; *out_pitch = 0;
    if (!bridge->interop || !bridge->interop->valid()) return RTX_ERR_INVALID_ARGUMENT;
    void* resource = bridge->interop->previewRingResource(slot);
    if (!resource) {
        bridge->error = "rtx_ngx_bridge_borrow_preview: the preview ring is unavailable";
        return RTX_ERR_UNSUPPORTED;
    }
    *out_d3d12_device = bridge->interop->device();
    *out_d3d12_resource = resource;
    *out_pitch = bridge->interop->previewRingRowBytes();
    bridge->error.clear();
    return RTX_OK;
}

RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_playback_pack(
    RtxNgxBridge* bridge, uint64_t y_plane, uint64_t uv_plane, uint32_t y_pitch,
    uint32_t uv_pitch, uint32_t width, uint32_t height, int32_t format, int32_t full_range,
    int32_t matrix, uint32_t slot) {
    if (!bridge || y_plane == 0 || uv_plane == 0 || y_pitch == 0 || uv_pitch == 0 ||
        width == 0 || height == 0 || slot > 2u || !bridge->interop || !bridge->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (!bridge->headless_playback) {
        bridge->error = "rtx_ngx_bridge_playback_pack: requires the headless playback bridge";
        return RTX_ERR_UNSUPPORTED;
    }
    const uint64_t out_ptr = bridge->interop->previewRingDevicePtr(slot);
    if (out_ptr == 0) {
        bridge->error = "rtx_ngx_bridge_playback_pack: the preview ring is unavailable";
        return RTX_ERR_UNSUPPORTED;
    }
    void* stream = bridge->interop->pack_stream();
    if (!stream) {
        bridge->error = "rtx_ngx_bridge_playback_pack: the pack stream is unavailable";
        return RTX_ERR_INTERNAL;
    }
    std::string error;
    if (!bridge->interop->bind_context(&error)) {
        bridge->error = "rtx_ngx_bridge_playback_pack: " + error;
        return bridge->interop->pollDeviceLost(nullptr) ? RTX_ERR_DEVICE_LOST : RTX_ERR_INTERNAL;
    }
    const RtxResult packed = rtx_ngx::launchPreprocessBuffer(
        std::make_tuple(y_plane, uv_plane, out_ptr, static_cast<int32_t>(bridge->interop->previewRingRowBytes()),
                        static_cast<int32_t>(width), static_cast<int32_t>(height),
                        static_cast<int32_t>(y_pitch), static_cast<int32_t>(uv_pitch),
                        format, full_range, matrix),
        stream);
    if (packed != RTX_OK) {
        bridge->error = "rtx_ngx_bridge_playback_pack: " + std::string(rtx_ngx::packLastError());
        return packed;
    }
    if (!bridge->interop->stream_synchronize(stream, &error)) {
        bridge->error = "rtx_ngx_bridge_playback_pack: " + error;
        return RTX_ERR_INTERNAL;
    }
    bridge->last_preview_ptr = out_ptr;
    bridge->error.clear();
    return RTX_OK;
}

}  // extern "C"
