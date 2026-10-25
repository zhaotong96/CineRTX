/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * C ABI exports for the isolated NGX bridge.
 *
 * The bridge owns the headless D3D12 device, the shared ping-pong textures and
 * the CUDA external-memory import (d3d12_interop.cpp).  When the RTX Video SDK
 * is compiled in (RTX_WITH_NGX_SDK=ON) NVIDIA's static stub is linked into this
 * DLL and ngx_d3d12_bridge.cpp evaluates VSR and TrueHDR on the same queue.
 * Without the SDK the bridge still validates the interop layer and reports
 * RTX_ERR_NGX for evaluation calls instead of pretending a texture exists.
 */

#define RTX_SHIM_EXPORTS 1

#include "ngx_bridge_abi.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "d3d12_interop.h"
#include "p010_pack_runtime.h"
#if defined(RTX_HAVE_NGX_SDK)
#include "ngx_d3d12_bridge.h"
#endif

namespace {

constexpr const char* kSdkMissingDiagnostic =
    "rtx_ngx_bridge: the RTX Video SDK (linked nvsdk_ngx_s.lib plus "
    "nvngx_vsr.dll / nvngx_truehdr.dll) is not available, so VSR and TrueHDR "
    "cannot run.";

constexpr const char* kInteropOnlyDiagnostic =
    "rtx_ngx_bridge: the D3D12/CUDA interop layer is available, but the RTX "
    "Video SDK feature libraries are not installed, so VSR and TrueHDR cannot "
    "run (RTX_ERR_NGX).";

// Opt-in bring-up trace (RTX_BRIDGE_TRACE=1), mirroring the other bridge TUs.
bool traceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("RTX_BRIDGE_TRACE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

void trace(const char* step, const std::string& detail = std::string()) {
    if (!traceEnabled()) {
        return;
    }
    std::fprintf(stderr, "[rtx_ngx_remaster] %s%s%s\n", step,
                 detail.empty() ? "" : ": ", detail.c_str());
    std::fflush(stderr);
}

// Presence of each RTX Video SDK component, probed independently.  `host`
// means the static NGX stub was linked into this DLL (build-time capability);
// the two feature DLLs are deployed next to the bridge in plugins/nvidia_rtx.
struct SdkComponents {
    bool vsr = false;      // nvngx_vsr.dll
    bool truehdr = false;  // nvngx_truehdr.dll
    bool host = false;     // nvsdk_ngx_s.lib linked at build time
};

struct BridgeImpl {
    RtxNgxBridgeConfig config{};
    std::unique_ptr<rtx_ngx::D3D12Interop> interop;
#if defined(RTX_HAVE_NGX_SDK)
    std::unique_ptr<rtx_ngx::NgxD3D12Bridge> ngx;
#endif
    std::string error;
    SdkComponents sdk;

    // ----- remaster state (P0-1.4) -----
    // One mutex serialises remaster frames; the P010 ring has two buffers so
    // the core can overlap NVENC(frame i) with NGX(frame i+1) on the
    // independent encode engine.  Buffer parity follows the frame counter:
    // call i writes ring[i & 1], so the core must hold frame i's encode back
    // until frame i-2 has completed.  The events time the per-frame stages
    // from the pack stream so the core can report GPU (not CPU wall) numbers
    // for Gate 2.2.
    std::mutex remaster_mutex;
    uint64_t p010_ring[2] = {0, 0};  // CUdeviceptr, 0 until the first remaster
    size_t p010_pitch = 0;           // bytes per row
    uint64_t p010_bytes = 0;
    uint64_t remaster_frames_issued = 0;
    void* remaster_event[4] = {nullptr, nullptr, nullptr, nullptr};
    bool remaster_events_ready = false;
    RtxNgxRemasterStats remaster_stats{};

    ~BridgeImpl() {
        // The core must destroy its NVENC session before the bridge, but even
        // a misordered caller gets a safe teardown: flush NGX first (waits for
        // the outstanding signal), then free the P010 ring while the interop
        // (and therefore the CUDA context binding) is still alive.
#if defined(RTX_HAVE_NGX_SDK)
        if (ngx) {
            std::string ignored;
            (void)ngx->flush(&ignored);
        }
#endif
        if (interop) {
            std::string ignored;
            for (uint64_t& buffer : p010_ring) {
                if (buffer != 0) {
                    (void)interop->free_device(buffer, &ignored);
                    buffer = 0;
                }
            }
        }
        if (interop) {
            for (void*& event : remaster_event) {
                interop->event_destroy(event);
                event = nullptr;
            }
        }
        remaster_events_ready = false;
    }
};

// Per-component probe of the proprietary RTX Video SDK feature libraries.  The
// probe checks the isolated plugin directory only and never executes the
// feature libraries' DllMain: they are designed to be initialised by the NGX
// runtime, and a standalone LoadLibrary/FreeLibrary pair was observed to spin
// forever on driver teardown (RTX 5060, driver 617.14).  The real load happens
// inside NVSDK_NGX_D3D12_Init through PathListInfo.
SdkComponents probeSdk() {
    SdkComponents components;
#if defined(RTX_HAVE_NGX_SDK)
    components.host = true;
#endif
#if defined(_WIN32)
    const std::wstring directory = rtx_ngx::plugin_directory();
    auto present = [&directory](const wchar_t* name) {
        const std::wstring path = directory + L"\\" + name;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    };
    components.vsr = present(L"nvngx_vsr.dll");
    components.truehdr = present(L"nvngx_truehdr.dll");
#endif
    return components;
}

// A feature counts as available only when both the linked host stub and its
// feature library are present.  The previous boolean OR misreported readiness
// from a single leftover driver DLL.
bool vsrAvailable(const SdkComponents& components) {
    return components.host && components.vsr;
}

bool truehdrAvailable(const SdkComponents& components) {
    return components.host && components.truehdr;
}

// Names the components missing for the requested feature set; empty when
// complete.
std::string missingComponents(const SdkComponents& components, bool need_vsr,
                              bool need_truehdr) {
    std::string missing;
    auto append = [&missing](const char* name) {
        if (!missing.empty()) {
            missing += ", ";
        }
        missing += name;
    };
    if ((need_vsr || need_truehdr) && !components.host) {
        append("nvsdk_ngx_s.lib (rebuild with RTX_WITH_NGX_SDK=ON)");
    }
    if (need_vsr && !components.vsr) {
        append("nvngx_vsr.dll");
    }
    if (need_truehdr && !components.truehdr) {
        append("nvngx_truehdr.dll");
    }
    return missing;
}

bool validConfig(const RtxNgxBridgeConfig* config) {
    if (config == nullptr) {
        return false;
    }
    if (config->input_width <= 0 || config->input_height <= 0 ||
        config->output_width <= 0 || config->output_height <= 0) {
        return false;
    }
    if (config->vsr_quality < 1 || config->vsr_quality > 4) {
        return false;
    }
    if (config->output_width < config->input_width ||
        config->output_height < config->input_height) {
        return false;
    }
    if (config->enable_truehdr != 0 && config->enable_truehdr != 1) {
        return false;
    }
    return true;
}

BridgeImpl* asBridge(RtxNgxBridge* bridge) { return reinterpret_cast<BridgeImpl*>(bridge); }

void copyText(char* destination, size_t capacity, const std::string& text) {
    if (destination == nullptr || capacity == 0) {
        return;
    }
    std::strncpy(destination, text.c_str(), capacity - 1);
    destination[capacity - 1] = '\0';
}

}  // namespace

extern "C" {

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_query_caps(RtxNgxBridgeCaps* caps) {
    if (caps == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::memset(caps, 0, sizeof(*caps));
    const SdkComponents sdk = probeSdk();
    caps->vsr_available = vsrAvailable(sdk) ? 1 : 0;
    caps->truehdr_available = truehdrAvailable(sdk) ? 1 : 0;
    // The device is created per bridge instance; a query reports the
    // capability, not a live device.  Interop (W4) is independent of the
    // proprietary SDK (W5), so it is reported from the actual driver/D3D12
    // availability.
    caps->d3d12_device_created = 0;
    caps->cuda_interop_available = rtx_ngx::interopPrerequisitesAvailable() ? 1 : 0;
    const std::string missing = missingComponents(sdk, true, true);
    if (missing.empty()) {
        copyText(caps->diagnostics, sizeof(caps->diagnostics),
                 std::string("RTX Video SDK ready; D3D12/CUDA interop available"));
    } else if (caps->cuda_interop_available != 0) {
        std::string text = kInteropOnlyDiagnostic;
        text += " Missing: ";
        text += missing;
        text += ".";
        copyText(caps->diagnostics, sizeof(caps->diagnostics), text);
    } else {
        std::string text = kSdkMissingDiagnostic;
        text += " Missing: ";
        text += missing;
        text += ".";
        copyText(caps->diagnostics, sizeof(caps->diagnostics), text);
    }
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_create(const RtxNgxBridgeConfig* config,
                                                           RtxNgxBridge** bridge) {
    if (bridge == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *bridge = nullptr;
    if (!validConfig(config)) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    auto impl = std::make_unique<BridgeImpl>();
    impl->config = *config;
    impl->sdk = probeSdk();
    impl->interop = std::make_unique<rtx_ngx::D3D12Interop>();
    std::string error;
    if (!impl->interop->create(*config, &error)) {
        // The interop layer is the prerequisite for everything else; a failure
        // here is an environment problem, not a missing feature.  A removed
        // adapter is reported distinctly so the core can recover instead of
        // treating it as a permanent capability loss (anti-TDR mechanism 4).
        if (impl->interop->valid()) {
            std::string lost_error;
            if (impl->interop->pollDeviceLost(&lost_error) && !lost_error.empty()) {
                error = lost_error;
            }
        }
        impl->error = error.empty() ? "D3D12/CUDA interop initialisation failed" : error;
        return impl->interop->deviceLost() ? RTX_ERR_DEVICE_LOST : RTX_ERR_ARCH_UNSUPPORTED;
    }
    // Validate the DLL combination this configuration actually needs
    // (expert hardening item 3): upscaling requires the VSR library, TrueHDR
    // the TrueHDR library, and both sit on top of the linked host stub.
    const bool need_vsr = config->output_width > config->input_width ||
                          config->output_height > config->input_height;
    const bool need_truehdr = config->enable_truehdr != 0;
    const std::string missing = missingComponents(impl->sdk, need_vsr, need_truehdr);
    if (!missing.empty()) {
        impl->error = "rtx_ngx_bridge: the RTX Video SDK is incomplete for this "
                      "configuration; missing: " + missing + ".";
        return RTX_ERR_NGX;
    }
#if defined(RTX_HAVE_NGX_SDK)
    impl->ngx = std::make_unique<rtx_ngx::NgxD3D12Bridge>();
    std::string ngx_error;
    if (!impl->ngx->create(*impl->interop, *config, &ngx_error)) {
        impl->error = "rtx_ngx_bridge: NGX evaluator creation failed: " + ngx_error;
        if (impl->interop->pollDeviceLost(nullptr)) {
            return RTX_ERR_DEVICE_LOST;
        }
        return RTX_ERR_NGX;
    }
#endif
    impl->error.clear();
    *bridge = reinterpret_cast<RtxNgxBridge*>(impl.release());
    return RTX_OK;
}

RTX_SHIM_API void RTX_SHIM_CALL rtx_ngx_bridge_destroy(RtxNgxBridge* bridge) {
    delete asBridge(bridge);
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_get_textures(
    RtxNgxBridge* bridge, RtxNgxTextureDesc* input_desc, RtxNgxTextureDesc* output_desc,
    uint64_t* input_shared_handle, uint64_t* output_shared_handle) {
    if (input_desc == nullptr || output_desc == nullptr || input_shared_handle == nullptr ||
        output_shared_handle == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *input_shared_handle = 0;
    *output_shared_handle = 0;
    if (bridge == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    const rtx_ngx::InteropTexture& input = impl->interop->input();
    const rtx_ngx::InteropTexture& output = impl->interop->output();
    input_desc->width = input.width;
    input_desc->height = input.height;
    input_desc->format = input.format;
    input_desc->allow_uav = 1;
    input_desc->allocation_size = input.allocation_size;
    output_desc->width = output.width;
    output_desc->height = output.height;
    output_desc->format = output.format;
    output_desc->allow_uav = 1;
    output_desc->allocation_size = output.allocation_size;
    *input_shared_handle = reinterpret_cast<uint64_t>(input.shared_handle);
    *output_shared_handle = reinterpret_cast<uint64_t>(output.shared_handle);
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_process(RtxNgxBridge* bridge,
                                                            uint64_t input_fence_value) {
    if (bridge == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
#if defined(RTX_HAVE_NGX_SDK)
    if (impl->ngx) {
        std::string error;
        if (!impl->ngx->process(input_fence_value, &error)) {
            impl->error = error;
            if (impl->interop->pollDeviceLost(nullptr)) {
                return RTX_ERR_DEVICE_LOST;
            }
            return RTX_ERR_NGX;
        }
        impl->error.clear();
        return RTX_OK;
    }
#endif
    std::string lost_error;
    if (impl->interop->pollDeviceLost(&lost_error)) {
        impl->error = lost_error;
        return RTX_ERR_DEVICE_LOST;
    }
    return RTX_ERR_NGX;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_get_fence(RtxNgxBridge* bridge,
                                                              uint64_t* fence_handle,
                                                              uint64_t* timeline_value) {
    if (bridge == nullptr || fence_handle == nullptr || timeline_value == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid() ||
        impl->interop->fence_shared_handle() == nullptr) {
        return RTX_ERR_ARCH_UNSUPPORTED;
    }
    *fence_handle = reinterpret_cast<uint64_t>(impl->interop->fence_shared_handle());
    *timeline_value = impl->interop->timeline_value();
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_flush(RtxNgxBridge* bridge) {
    if (bridge == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
#if defined(RTX_HAVE_NGX_SDK)
    if (impl->ngx) {
        std::string ngx_error;
        if (!impl->ngx->flush(&ngx_error)) {
            impl->error = ngx_error;
            if (impl->interop->pollDeviceLost(nullptr)) {
                return RTX_ERR_DEVICE_LOST;
            }
            return RTX_ERR_INTERNAL;
        }
    }
#endif
    std::string error;
    if (impl->interop && impl->interop->valid() && !impl->interop->flush_queue(&error)) {
        impl->error = error;
        if (impl->interop->pollDeviceLost(nullptr)) {
            return RTX_ERR_DEVICE_LOST;
        }
        return RTX_ERR_INTERNAL;
    }
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_reset(RtxNgxBridge* bridge, int32_t width,
                                                          int32_t height) {
    if (bridge == nullptr || width <= 0 || height <= 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    // Texture recreation is driven by the core's ResolutionGuard; the bridge
    // currently requires a new instance after a geometry change.
    return RTX_ERR_UNSUPPORTED;
}

RTX_SHIM_API const char* RTX_SHIM_CALL rtx_ngx_bridge_last_error(RtxNgxBridge* bridge) {
    if (bridge == nullptr) {
        return kSdkMissingDiagnostic;
    }
    return asBridge(bridge)->error.c_str();
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_set_frame_stats(
    RtxNgxBridge* bridge, const RtxNgxFrameStats* stats) {
    if (bridge == nullptr || stats == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (!std::isfinite(stats->y_norm) || !std::isfinite(stats->p_high) ||
        stats->y_norm < 0.0 || stats->y_norm > 1.0 || stats->p_high < 0.0 ||
        stats->p_high > 1.0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
#if defined(RTX_HAVE_NGX_SDK)
    if (impl->ngx) {
        impl->ngx->set_frame_stats(*stats);
    }
#endif
    return RTX_OK;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_selftest(int32_t width, int32_t height,
                                                             char* detail, size_t detail_capacity) {
    if (detail == nullptr || detail_capacity == 0 || width <= 0 || height <= 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    return rtx_ngx::run_interop_selftest(static_cast<uint32_t>(width),
                                         static_cast<uint32_t>(height), detail,
                                         detail_capacity);
}

RTX_SHIM_API uint32_t RTX_SHIM_CALL rtx_ngx_bridge_abi_version(void) {
    return RTX_NGX_BRIDGE_ABI_VERSION;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_get_remaster_stats(
    RtxNgxBridge* bridge, RtxNgxRemasterStats* stats) {
    if (bridge == nullptr || stats == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
    std::lock_guard<std::mutex> lock(impl->remaster_mutex);
    *stats = impl->remaster_stats;
    return RTX_OK;
}

namespace {

// Allocates the two P010 ring buffers and the four timing events on the
// first remaster call.  Returns false with impl->error set on failure.
bool ensureRemasterResources(BridgeImpl* impl) {
    if (impl->p010_ring[0] != 0 && impl->p010_ring[1] != 0 && impl->remaster_events_ready) {
        return true;
    }
    rtx_ngx::D3D12Interop& interop = *impl->interop;
    std::string error;
    if (impl->p010_ring[0] == 0 || impl->p010_ring[1] == 0) {
        // cuMemAllocPitch takes the width in BYTES (P010 luma row = 2 * width)
        // and pads rows to a 16-byte granularity; UV starts at pitch * height.
        const size_t width_bytes = static_cast<size_t>(impl->config.output_width) * 2u;
        const size_t rows =
            static_cast<size_t>(impl->config.output_height) * 3u / 2u + 1u;
        for (uint64_t& buffer : impl->p010_ring) {
            if (buffer != 0) {
                continue;
            }
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
        }
        impl->p010_bytes = impl->p010_pitch * static_cast<size_t>(impl->config.output_height) * 3u / 2u;
        trace("remaster: P010 ring (2x) allocated",
              std::to_string(impl->p010_bytes) + " bytes each, pitch " +
                  std::to_string(impl->p010_pitch));
    }
    if (!impl->remaster_events_ready) {
        for (void*& event : impl->remaster_event) {
            if (event != nullptr) {
                continue;
            }
            if (!interop.event_create(&event, &error)) {
                for (void*& created : impl->remaster_event) {
                    if (created != nullptr) {
                        interop.event_destroy(created);
                        created = nullptr;
                    }
                }
                impl->error = "rtx_ngx_bridge_remaster: " + error;
                return false;
            }
        }
        impl->remaster_events_ready = true;
    }
    return true;
}

bool validRemasterInput(const RtxNgxRemasterInput* input) {
    return input != nullptr && input->y_plane != 0 && input->uv_plane != 0 &&
           input->y_pitch != 0 && input->uv_pitch != 0 &&
           (input->format == RTX_PIXEL_NV12 || input->format == RTX_PIXEL_P010) &&
           (input->full_range == 0 || input->full_range == 1) &&
           input->matrix >= 0 && input->matrix <= 2;
}

}  // namespace

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_remaster(
    RtxNgxBridge* bridge, const RtxNgxRemasterInput* input, RtxNgxRemasterOutput* output,
    RtxNgxRemasterStats* stats) {
    if (bridge == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (!validRemasterInput(input) || output == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
#if !defined(RTX_HAVE_NGX_SDK)
    impl->error = "rtx_ngx_bridge_remaster: this bridge was built without the RTX Video SDK";
    return RTX_ERR_NGX;
#else
    if (impl->ngx == nullptr) {
        impl->error = "rtx_ngx_bridge_remaster: the NGX evaluator is not initialised";
        return RTX_ERR_NGX;
    }
    rtx_ngx::D3D12Interop& interop = *impl->interop;
    std::lock_guard<std::mutex> lock(impl->remaster_mutex);

    std::string error;
    if (!interop.bind_context(&error)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return interop.pollDeviceLost(nullptr) ? RTX_ERR_DEVICE_LOST : RTX_ERR_INTERNAL;
    }
    if (!ensureRemasterResources(impl)) {
        return RTX_ERR_OUT_OF_MEMORY;
    }

    void* const stream = interop.pack_stream();
    if (stream == nullptr) {
        impl->error = "rtx_ngx_bridge_remaster: the pack stream is unavailable";
        return RTX_ERR_INTERNAL;
    }

    // Frame timeline: V is drawn from advance_timeline() (the single source of
    // signal values); NgxD3D12Bridge::process(V) waits V on the D3D12 queue and
    // signals V+1, so V+1 is always the correct pack-side wait value.
    const uint64_t v = interop.advance_timeline();
    trace("remaster: frame begin", "V=" + std::to_string(v));

    // e0 | preprocess -> input surface | e1 | signal V | [NGX evaluates] |
    // wait V+1 | e2 | pack v2 -> P010 | e3 | stream sync
    if (!interop.event_record(impl->remaster_event[0], stream, &error)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
    rtx_ngx::PreprocessLaunch preprocess{};
    preprocess.y_plane = input->y_plane;
    preprocess.uv_plane = input->uv_plane;
    preprocess.output_surface = interop.input().surface;
    preprocess.width = impl->config.input_width;
    preprocess.height = impl->config.input_height;
    preprocess.y_pitch_bytes = static_cast<int32_t>(input->y_pitch);
    preprocess.uv_pitch_bytes = static_cast<int32_t>(input->uv_pitch);
    preprocess.format = input->format;
    preprocess.full_range = input->full_range;
    preprocess.matrix = input->matrix;
    RtxResult launched = rtx_ngx::launchPreprocess(preprocess, stream);
    if (launched != RTX_OK) {
        impl->error = std::string("rtx_ngx_bridge_remaster: ") + rtx_ngx::packLastError();
        return launched;
    }
    if (!interop.event_record(impl->remaster_event[1], stream, &error)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }

    const bool semaphore = interop.semaphore_ready();
    if (semaphore) {
        // GPU-only hand-off: the pack stream signals the shared fence after the
        // preprocess kernel, the D3D12 queue waits V inside process(), and the
        // pack stream below waits V+1 after TrueHDR.
        if (!interop.fence_signal_cuda(stream, v, &error)) {
            impl->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
    } else {
        // Fallback for drivers without the external-semaphore API: drain the
        // preprocess kernel on the CPU and let the queue signal its own V.
        if (!interop.stream_synchronize(stream, &error)) {
            impl->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
        if (!interop.queue_signal_fence(v, &error)) {
            impl->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
    }

    const auto ngx_started = std::chrono::steady_clock::now();
    if (!impl->ngx->process(v, &error)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return interop.pollDeviceLost(nullptr) ? RTX_ERR_DEVICE_LOST : RTX_ERR_NGX;
    }

    if (semaphore) {
        if (!interop.fence_wait_cuda(stream, v + 1, &error)) {
            impl->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
    } else {
        if (!interop.wait_for_fence_value(v + 1, std::chrono::milliseconds(10000), &error)) {
            impl->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
    }

    if (!interop.event_record(impl->remaster_event[2], stream, &error)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
    // Ring parity: frame i writes p010_ring[i & 1].  The caller owns the
    // ordering (frame i may only be issued after frame i-2 finished encoding)
    // because only it knows when NVENC released the older buffer.
    const uint64_t p010_buffer = impl->p010_ring[impl->remaster_frames_issued & 1];
    rtx_ngx::PackV2Launch pack{};
    pack.input_surface = interop.output().surface;
    pack.y_plane = p010_buffer;
    pack.uv_plane = p010_buffer + impl->p010_pitch *
                                      static_cast<size_t>(impl->config.output_height);
    pack.y_pitch_samples = static_cast<uint32_t>(impl->p010_pitch / 2u);
    pack.uv_pitch_samples = pack.y_pitch_samples;
    pack.width = impl->config.output_width;
    pack.height = impl->config.output_height;
    // TrueHDR emits linear scRGB (BT.709 primaries, 1.0 = 80 nits); without
    // TrueHDR the shared RGBA16F texture holds the legacy linear BT.2020
    // interpretation, which is what pack v1/v2 default to.
    pack.input_is_pq = 0;
    pack.input_is_scrgb = impl->config.enable_truehdr != 0 ? 1 : 0;
    pack.linear_scale_nits = 80.0f;
    launched = rtx_ngx::launchPackV2(pack, stream);
    if (launched != RTX_OK) {
        impl->error = std::string("rtx_ngx_bridge_remaster: ") + rtx_ngx::packLastError();
        return launched;
    }
    if (!interop.event_record(impl->remaster_event[3], stream, &error)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
    // Synchronous contract: on return the P010 planes are complete and NVENC
    // may register the buffer immediately.  This wait costs CPU time but no
    // pixel ever crosses the bus.
    if (!interop.stream_synchronize(stream, &error)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }

    double preprocess_ms = 0.0;
    double ngx_ms = 0.0;
    double pack_ms = 0.0;
    double frame_ms = 0.0;
    if (!interop.event_elapsed(&preprocess_ms, impl->remaster_event[0],
                               impl->remaster_event[1], &error) ||
        !interop.event_elapsed(&pack_ms, impl->remaster_event[2], impl->remaster_event[3],
                               &error) ||
        !interop.event_elapsed(&frame_ms, impl->remaster_event[0], impl->remaster_event[3],
                               &error)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
    // The NGX stage spans the fence signal, the D3D12 evaluation and the fence
    // wait; with the external semaphore the stream events see exactly that
    // gap, on the fallback path the wall clock is the honest number.
    if (semaphore) {
        if (!interop.event_elapsed(&ngx_ms, impl->remaster_event[1],
                                   impl->remaster_event[2], &error)) {
            impl->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
    } else {
        ngx_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                           ngx_started)
                     .count();
    }

    std::memset(output, 0, sizeof(*output));
    output->y_plane = p010_buffer;
    output->uv_plane = p010_buffer + impl->p010_pitch *
                                        static_cast<size_t>(impl->config.output_height);
    output->y_pitch = static_cast<uint32_t>(impl->p010_pitch);
    output->uv_pitch = static_cast<uint32_t>(impl->p010_pitch);
    output->width = impl->config.output_width;
    output->height = impl->config.output_height;
    output->format = RTX_PIXEL_P010;

    impl->remaster_stats.preprocess_ms += preprocess_ms;
    impl->remaster_stats.ngx_ms += ngx_ms;
    impl->remaster_stats.pack_kernel_ms += pack_ms;
    impl->remaster_stats.frame_ms += frame_ms;
    impl->remaster_stats.frames += 1;
    impl->remaster_stats.pack_launches += 1;
    impl->remaster_frames_issued += 1;
    if (stats != nullptr) {
        *stats = impl->remaster_stats;
    }
    impl->error.clear();
    trace("remaster: frame done",
          "V=" + std::to_string(v) + " pre=" + std::to_string(preprocess_ms) +
              "ms ngx=" + std::to_string(ngx_ms) + "ms pack=" + std::to_string(pack_ms) + "ms");
    return RTX_OK;
#endif
}

}  // extern "C"
