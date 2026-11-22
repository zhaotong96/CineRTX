/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * C ABI exports for the isolated NGX bridge.
 *
 * The bridge owns the headless D3D12 device, the shared ping-pong textures and
 * the CUDA external-memory import (d3d12_interop.cpp).  When the RTX Video SDK
 * is compiled in (every Windows build) NVIDIA's static stub is linked into this
 * DLL and ngx_d3d12_bridge.cpp evaluates VSR and TrueHDR on the same queue.
 * Without the SDK the bridge still validates the interop layer and reports
 * RTX_ERR_NGX for evaluation calls instead of pretending a texture exists.
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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <bit>
#include <memory>
#include <mutex>
#include <string>

#include "d3d12_interop.h"
#include "nvof_engine.h"
#include "p010_pack_runtime.h"
#include "rife_engine.h"
#if defined(RTX_HAVE_NGX_SDK)
#include "ngx_d3d12_bridge.h"
#endif

namespace {

static std::string g_last_bridge_create_error;

constexpr const char* kSdkMissingDiagnostic =
    "rtx_ngx_bridge: the RTX Video SDK (linked nvsdk_ngx_s.lib plus "
    "nvngx_vsr.dll / nvngx_truehdr.dll) is not available, so VSR and TrueHDR "
    "cannot run.";

constexpr const char* kInteropOnlyDiagnostic =
    "rtx_ngx_bridge: the D3D12/CUDA interop layer is available, but the RTX "
    "Video SDK feature libraries are not installed, so VSR and TrueHDR cannot "
    "run (RTX_ERR_NGX).";

// Opt-in bring-up trace (RTX_BRIDGE_TRACE=1), mirroring the other bridge TUs.
bool traceEnabled() { return false; }

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
// @rtx-type-justification
// Category: C-ABI
// Why-Std-Fails: extern "C" 跨模块导出结构/函数指针表：字段序与调用约定即 shim C ABI 契约，std::function/std::tuple 无法表达 C 布局承诺。
// Invariant: 字段序、对齐与函数指针签名与 ABI 头逐字对应；append-only，禁止重排字段。
struct SdkComponents {
    bool vsr = false;      // nvngx_vsr.dll
    bool truehdr = false;  // nvngx_truehdr.dll
    bool host = false;     // nvsdk_ngx_s.lib linked at build time
};

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调多资源异步生命周期的宿主状态机：std::unique_ptr/std::jthread/std::tuple 无法表达其声明序即析构逆序、线程亲和与受控降级路径的交叉不变量。
// Invariant: 成员声明序即依赖序（先声明者后析构）；状态迁移仅在持锁线程或属主线程发生。
struct BridgeImpl {
    RtxNgxBridgeConfig config{};
    std::unique_ptr<rtx_ngx::D3D12Interop> interop;
#if defined(RTX_HAVE_NGX_SDK)
    std::unique_ptr<rtx_ngx::NgxD3D12Bridge> ngx;
#endif
    std::string error;
    SdkComponents sdk;

    std::mutex remaster_mutex;
// ----- remaster state (P0-1.4) -----
// One mutex serialises remaster frames.  Phase 7 (Task 7-A3) upgrades the
// P010 ring from two to three buffers so the core can overlap
// NVENC(frame i-1) with NGX(frame i) and even hold a third frame in
// flight: the core gates on remasters_issued - encodes_completed < 3.
// Slot selection follows the frame counter: call i writes ring[i % 3].
// The events time the per-frame stages from the pack stream so the core
// can report GPU (not CPU wall) numbers for Gate 2.2.
    uint64_t p010_ring[3] = {0, 0, 0};  // CUdeviceptr, 0 until the first remaster
    size_t p010_pitch = 0;              // bytes per row
    uint64_t p010_bytes = 0;
    uint64_t remaster_frames_issued = 0;
    void* remaster_event[4] = {nullptr, nullptr, nullptr, nullptr};
    bool remaster_events_ready = false;
    RtxNgxRemasterStats remaster_stats{};
    uint64_t stats_devptr = 0;
// Phase 7: fused-kernel statistic slots (uint32[4]) and the 3-slot
// 960x540 R10G10B10A2 preview ring (Phase 11.2 C-02: shared D3D12 buffers,
// 3 x 2.07 MB = 6.22 MB VRAM; the viewport borrows the real resources).
    uint64_t preview_ring[3] = {0, 0, 0};
    uint32_t preview_pitch = 0;
    bool preview_ready = false;
    uint64_t last_preview_ptr = 0;
    bool headless_playback = false;
// Phase 11.2 (C-02): headless playback mode (config.reserved[0] == 1) --
// no NGX evaluator, ring sized to the source geometry at create time.
    void* lut_tex = nullptr;
// Phase 7: uploaded FP16 3D LUT (texture object + backing CUarray).
    void* lut_array = nullptr;
    int32_t lut_dim = 0;
    uint64_t upload_buffer = 0;
// Phase 8 (Task 8-B1): persistent device staging buffer for software-
// decoded P010 frames (grown on demand, freed in the destructor).
    size_t upload_bytes = 0;
    uint64_t interp_buffer = 0;
    size_t interp_bytes = 0;
    std::unique_ptr<rtx_ngx::NvofEngine> nvof_engine;
    std::unique_ptr<rtx_ngx::RifeEngine> rife_engine;

    ~BridgeImpl() {
        if (rife_engine) {
            rife_engine.reset();
        }
#if defined(RTX_HAVE_NGX_SDK)
// The core must destroy its NVENC session before the bridge, but even
// a misordered caller gets a safe teardown: flush NGX first (waits for
// the outstanding signal), then free the P010 ring while the interop
// (and therefore the CUDA context binding) is still alive.
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
            if (stats_devptr != 0) {
                (void)interop->free_device(stats_devptr, &ignored);
                stats_devptr = 0;
            }
            if (upload_buffer != 0) {
                (void)interop->free_device(upload_buffer, &ignored);
                upload_buffer = 0;
            }
            if (interp_buffer != 0) {
                (void)interop->free_device(interp_buffer, &ignored);
                interp_buffer = 0;
            }
            for (uint64_t& buffer : preview_ring) {
                buffer = 0;
// Phase 11.2 (C-02): the preview ring lives in shared D3D12
// buffers; its CUdeviceptrs are external-memory mappings owned
// (and released) by the interop, NOT cuMemAlloc allocations.
            }
            interop->destroy_lut_texture(&lut_tex, &lut_array);
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

SdkComponents probeSdk() {
// Per-component probe of the proprietary RTX Video SDK feature libraries.  The
// probe checks the isolated plugin directory only and never executes the
// feature libraries' DllMain: they are designed to be initialised by the NGX
// runtime, and a standalone LoadLibrary/FreeLibrary pair was observed to spin
// forever on driver teardown (RTX 5060, driver 617.14).  The real load happens
// inside NVSDK_NGX_D3D12_Init through PathListInfo.
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
bool vsrAvailable(const SdkComponents& components) {
    return components.host && components.vsr;
// A feature counts as available only when both the linked host stub and its
// feature library are present.  The previous boolean OR misreported readiness
// from a single leftover driver DLL.
}
bool truehdrAvailable(const SdkComponents& components) {
    return components.host && components.truehdr;
}
std::string missingComponents(const SdkComponents& components, bool need_vsr,
                              bool need_truehdr) {
    std::string missing;
    auto append = [&missing](const char* name) {
// Names the components missing for the requested feature set; empty when
// complete.
        if (!missing.empty()) {
            missing += ", ";
        }
        missing += name;
    };
    if ((need_vsr || need_truehdr) && !components.host) {
        append("nvsdk_ngx_s.lib (RTX Video SDK bridge not compiled on this platform)");
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
    if (config->vsr_quality < 0 || config->vsr_quality > 4) {
        return false;
    }
    if (config->output_width < config->input_width ||
        config->output_height < config->input_height) {
        return false;
// Phase 6.1 微瑕 3: 0 is the new "VSR explicitly disabled" sentinel.
    }
    if (config->enable_truehdr != 0 && config->enable_truehdr != 1) {
        return false;
    }
    return true;
}
BridgeImpl* asBridge(RtxNgxBridge* bridge) { return reinterpret_cast<BridgeImpl*>(bridge); }

bool ensurePlaybackRing(BridgeImpl* impl) {
    rtx_ngx::D3D12Interop& interop = *impl->interop;
    std::string error;
    const uint32_t width = static_cast<uint32_t>(impl->config.input_width);
    const uint32_t height = static_cast<uint32_t>(impl->config.input_height);
    if (!interop.create_shared_buffer_ring(3u, width, height, 4u, &error)) {
// Phase 11.2 (C-02): eagerly creates the headless playback ring at the source
// geometry declared in the config (input_width x input_height, 4 bytes per
// pixel, 3 slots) so the realtime session never lazily allocates mid-stream.
        impl->error = "rtx_ngx_bridge_create(playback): " + error;
        return false;
    }
    const auto& ring = interop.previewRing();
    for (uint32_t i = 0; i < 3u && i < ring.size(); ++i) {
        impl->preview_ring[i] = ring[i].device_ptr;
    }
    impl->preview_pitch = interop.previewRingRowBytes();
    impl->preview_ready = true;
    return true;
}
void copyText(char* destination, size_t capacity, const std::string& text) {
    if (destination == nullptr || capacity == 0) {
        return;
    }
#if defined(_WIN32)
    strncpy_s(destination, capacity, text.c_str(), _TRUNCATE);
#else
    std::strncpy(destination, text.c_str(), capacity - 1);
    destination[capacity - 1] = '\0';
#endif
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
    caps->d3d12_device_created = 0;
    caps->cuda_interop_available = rtx_ngx::interopPrerequisitesAvailable() ? 1 : 0;
    const std::string missing = missingComponents(sdk, true, true);
    if (missing.empty()) {
        copyText(caps->diagnostics, sizeof(caps->diagnostics),
                 std::string("RTX Video SDK ready; D3D12/CUDA interop available"));
    } else if (caps->cuda_interop_available != 0) {
        std::string text = kInteropOnlyDiagnostic;
// The device is created per bridge instance; a query reports the
// capability, not a live device.  Interop (W4) is independent of the
// proprietary SDK (W5), so it is reported from the actual driver/D3D12
// availability.
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
        if (impl->interop->valid()) {
            std::string lost_error;
            if (impl->interop->pollDeviceLost(&lost_error) && !lost_error.empty()) {
                error = lost_error;
            }
        }
        impl->error = error.empty() ? "D3D12/CUDA interop initialisation failed" : error;
        return impl->interop->deviceLost() ? RTX_ERR_DEVICE_LOST : RTX_ERR_ARCH_UNSUPPORTED;
    }
// The interop layer is the prerequisite for everything else; a failure
// here is an environment problem, not a missing feature.  A removed
// adapter is reported distinctly so the core can recover instead of
// treating it as a permanent capability loss (anti-TDR mechanism 4).
    if (config->reserved[0] == 1u) {
        impl->headless_playback = true;
        if (!ensurePlaybackRing(impl.get())) {
            return RTX_ERR_OUT_OF_MEMORY;
        }
        impl->error.clear();
        *bridge = reinterpret_cast<RtxNgxBridge*>(impl.release());
        return RTX_OK;
    }
// Phase 11.2 (C-02): headless playback mode -- config.reserved[0] == 1.
// No NGX evaluator, no SDK validation: the bridge is a bare headless
// D3D12/CUDA interop with a 3-slot shared buffer ring sized exactly to the
// source geometry, consumed by the realtime MediaPlaybackSession through
// rtx_ngx_bridge_playback_pack + rtx_ngx_bridge_borrow_preview.
    const bool need_vsr = config->output_width > config->input_width ||
                          config->output_height > config->input_height;
    const bool need_truehdr = config->enable_truehdr != 0;
    const std::string missing = missingComponents(impl->sdk, need_vsr, need_truehdr);
    if (!missing.empty()) {
        impl->error = "rtx_ngx_bridge: the RTX Video SDK is incomplete for this "
                      "configuration; missing: " + missing + ".";
        return RTX_ERR_NGX;
    }
// Validate the DLL combination this configuration actually needs
// (expert hardening item 3): upscaling requires the VSR library, TrueHDR
// the TrueHDR library, and both sit on top of the linked host stub.
#if defined(RTX_HAVE_NGX_SDK)
    impl->ngx = std::make_unique<rtx_ngx::NgxD3D12Bridge>();
    std::string ngx_error;
    if (!impl->ngx->create(*impl->interop, *config, &ngx_error)) {
        impl->error = "rtx_ngx_bridge: NGX evaluator creation failed: " + ngx_error;
        g_last_bridge_create_error = impl->error;
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
RTX_SHIM_API const char* RTX_SHIM_CALL rtx_ngx_bridge_last_error(RtxNgxBridge* bridge) {
    if (bridge == nullptr) {
        if (!g_last_bridge_create_error.empty()) {
            return g_last_bridge_create_error.c_str();
        }
        return kSdkMissingDiagnostic;
    }
    return asBridge(bridge)->error.c_str();
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

bool ensureRemasterResources(BridgeImpl* impl) {
    if (impl->p010_ring[0] != 0 && impl->p010_ring[1] != 0 && impl->p010_ring[2] != 0 &&
        impl->remaster_events_ready) {
        return true;
    }
    rtx_ngx::D3D12Interop& interop = *impl->interop;
    std::string error;
    if (impl->p010_ring[0] == 0 || impl->p010_ring[1] == 0 || impl->p010_ring[2] == 0) {
        const size_t width_bytes = static_cast<size_t>(impl->config.output_width) * 2u;
        const size_t rows =
            static_cast<size_t>(impl->config.output_height) * 3u / 2u + 1u;
        for (uint64_t& buffer : impl->p010_ring) {
            if (buffer != 0) {
                continue;
// Allocates the three P010 ring buffers, the fused-kernel statistic slots and
// the four timing events on the first remaster call.  Returns false with
// impl->error set on failure.
            }
            uint64_t pointer = 0;
            size_t pitch = 0;
            if (!interop.alloc_pitched(&pointer, &pitch, width_bytes, rows, &error)) {
                for (uint64_t& allocated : impl->p010_ring) {
                    if (allocated != 0) {
                        std::string ignored;
                        (void)interop.free_device(allocated, &ignored);
// cuMemAllocPitch takes the width in BYTES (P010 luma row = 2 * width)
// and pads rows to a 16-byte granularity; UV starts at pitch * height.
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
        trace("remaster: P010 ring (3x) allocated",
              std::to_string(impl->p010_bytes) + " bytes each, pitch " +
                  std::to_string(impl->p010_pitch));
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
            if (event != nullptr) {
                continue;
            }
// Phase 7: the fused pack kernel publishes max/sum/highlight statistics
// through a 4-slot uint32 device buffer, reset stream-ordered per frame.
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
bool ensurePreviewResources(BridgeImpl* impl) {
    if (impl->preview_ready) {
        return true;
    }
    rtx_ngx::D3D12Interop& interop = *impl->interop;
    std::string error;
    constexpr uint32_t kPreviewWidth = 960;
    constexpr uint32_t kPreviewHeight = 540;
    constexpr uint32_t kPreviewSlots = 3;
    if (!interop.create_shared_buffer_ring(kPreviewSlots, kPreviewWidth, kPreviewHeight, 4u,
                                           &error)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return false;
    }
    const auto& ring = interop.previewRing();
// Phase 11.2 (C-02): lazily creates the 3-slot 960x540 R10G10B10A2 preview
// ring (3 x 960 x 540 x 4 bytes = 6,220,800 B = 5.93 MiB total) on the first
// preview request.  Slots are D3D12_RESOURCE_DIMENSION_BUFFER committed with
// D3D12_HEAP_FLAG_SHARED in D3D12_RESOURCE_STATE_COMMON, exported through
// CreateSharedHandle and mapped into CUDA via cuImportExternalMemory +
// cuExternalMemoryGetMappedBuffer; the row pitch is exactly 3840 B (960 x 4),
// naturally aligned for both sides.  The transcode fused pack kernel writes
// plain 32-bit words at the mapped CUdeviceptr and the DirectComposition
// viewport borrows the real ID3D12Resource through rtx_ngx_bridge_borrow_preview.
    for (uint32_t i = 0; i < kPreviewSlots && i < ring.size(); ++i) {
        impl->preview_ring[i] = ring[i].device_ptr;
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
}  // namespace

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_remaster(
    RtxNgxBridge* bridge, const RtxNgxRemasterInput* input, RtxNgxRemasterOutput* output,
    RtxNgxRemasterStats* stats) {
    (void)stats;
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
    if (input->preview_rgba10_ptr != 0 && !ensurePreviewResources(impl)) {
        return RTX_ERR_OUT_OF_MEMORY;
    }
    void* const stream = interop.pack_stream();
    if (stream == nullptr) {
        impl->error = "rtx_ngx_bridge_remaster: the pack stream is unavailable";
        return RTX_ERR_INTERNAL;
    }
    const uint64_t v = interop.advance_timeline();
    trace("remaster: frame begin", "V=" + std::to_string(v));

    if (impl->stats_devptr != 0) {
        if (!interop.memset_d32_async(impl->stats_devptr, 0, 4, stream, &error)) {
            impl->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
    }
    if (!interop.event_record(impl->remaster_event[0], stream, &error)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
    rtx_ngx::PreprocessLaunch preprocess{};
    preprocess.y_plane = input->y_plane;
    preprocess.uv_plane = input->uv_plane;
    preprocess.output_surface = interop.input().surface;
    preprocess.width = impl->config.input_width;
// Frame timeline: V is drawn from advance_timeline() (the single source of
// signal values); NgxD3D12Bridge::process(V) waits V on the D3D12 queue and
// signals V+1, so V+1 is always the correct pack-side wait value.
    preprocess.height = impl->config.input_height;
    preprocess.y_pitch_bytes = static_cast<int32_t>(input->y_pitch);
    preprocess.uv_pitch_bytes = static_cast<int32_t>(input->uv_pitch);
// Phase 7: reset the fused-kernel statistic slots stream-ordered so the
// atomics of this frame start from zero.
    preprocess.format = input->format;
    preprocess.full_range = input->full_range;
    int normalized_matrix = input->matrix;
    if (normalized_matrix == 9) {
        normalized_matrix = 1;  // ISO BT.2020NC -> ABI 1
    } else if (normalized_matrix == 5 || normalized_matrix == 6) {
        normalized_matrix = 2;  // ISO BT.601 -> ABI 2
// e0 | preprocess -> input surface | e1 | signal V | [NGX evaluates] |
// wait V+1 | e2 | pack v2 -> P010 | e3 | stream sync
    }
    preprocess.matrix = normalized_matrix;
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
        if (!interop.fence_signal_cuda(stream, v, &error)) {
            impl->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
    } else {
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
    const bool reset_temporal = (input->reset_temporal != 0);
    if (!impl->ngx->process(v, &error, reset_temporal)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return interop.pollDeviceLost(nullptr) ? RTX_ERR_DEVICE_LOST : RTX_ERR_NGX;
    }
// GPU-only hand-off: the pack stream signals the shared fence after the
// preprocess kernel, the D3D12 queue waits V inside process(), and the
// pack stream below waits V+1 after TrueHDR.
    if (semaphore) {
        if (!interop.fence_wait_cuda(stream, v + 1, &error)) {
            impl->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
// Fallback for drivers without the external-semaphore API: drain the
// preprocess kernel on the CPU and let the queue signal its own V.
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
    const uint64_t p010_buffer = impl->p010_ring[impl->remaster_frames_issued % 3u];
    rtx_ngx::PackV2Launch pack{};
    pack.input_surface = interop.output().surface;
    pack.y_plane = p010_buffer;
    pack.uv_plane = p010_buffer + impl->p010_pitch *
                                      static_cast<size_t>(impl->config.output_height);
    pack.y_pitch_samples = static_cast<uint32_t>(impl->p010_pitch / 2u);
    pack.uv_pitch_samples = pack.y_pitch_samples;
    pack.width = impl->config.output_width;
    pack.height = impl->config.output_height;
    pack.lut_tex = reinterpret_cast<uint64_t>(impl->lut_tex);
    pack.lut_dim = impl->lut_dim;
    pack.stats_devptr = impl->stats_devptr;
    if (input->preview_rgba10_ptr != 0) {
        pack.preview_ptr = input->preview_rgba10_ptr;
        pack.preview_width = 960;
        pack.preview_height = 540;
        pack.preview_pitch_px = static_cast<int32_t>(impl->preview_pitch / 4u);
        pack.split_x_norm = input->split_x_norm;
        pack.src_surface = (impl->config.input_width == impl->config.output_width &&
                            impl->config.input_height == impl->config.output_height)
                               ? interop.input().surface
                               : 0;
// Ring parity: frame i writes p010_ring[i % 3].  The caller owns the
// ordering (frame i may only be issued after frame i-3+1 finished encoding)
// because only it knows when NVENC released the older buffers.
    }
    if (impl->config.enable_truehdr != 0) {
        pack.input_is_pq = 0;
        pack.input_is_scrgb = 1;
        pack.linear_scale_nits = 80.0f;
    } else {
        pack.input_is_pq = 1;
        pack.input_is_scrgb = 0;
        pack.linear_scale_nits = 1.0f;
    }
// Phase 7 fused stages: LUT, statistic slots and the A/B preview bypass.
    launched = rtx_ngx::launchPackV2(pack, stream);
    if (launched != RTX_OK) {
        impl->error = std::string("rtx_ngx_bridge_remaster: ") + rtx_ngx::packLastError();
        return launched;
    }
    if (!interop.event_record(impl->remaster_event[3], stream, &error)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
// The src_surface is the preprocessed input texture (input_width x input_height).
// If the transcode upscales or geometry changes (e.g. 720p/1080p -> 4K),
// sampling it with 4K coordinates in the fused pack kernel causes an out-of-bounds
// surf2Dread (CUDA_ERROR_ILLEGAL_ADDRESS). Only pass src_surface if dimensions match;
// otherwise fallback to sampling the remastered frame safely.
    if (!interop.stream_synchronize(stream, &error)) {
        impl->error = "rtx_ngx_bridge_remaster: " + error;
        return RTX_ERR_INTERNAL;
    }
    double preprocess_ms = 0.0;
// TrueHDR emits linear scRGB (BT.709 primaries, 1.0 = 80 nits); without
// TrueHDR the shared RGBA16F texture holds the legacy linear BT.2020
// interpretation, which is what pack v1/v2 default to.
// TrueHDR 模式：线性 scRGB (1.0 = 80 nits) -> PQ BT.2020 编码；
// SDR 原生直通模式：D3D12 输出标准 SDR 纹理 [0.0, 1.0]，传 input_is_pq = 1 使得 pack 核函数跳过 pqEncode，
// 直接以 0~1 线性比例量化输出至 Studio Swing [64, 940]，彻底根除灰雾与 181 截断，实现 SDR 满幅 0~255 输出！
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
    if (semaphore) {
        if (!interop.event_elapsed(&ngx_ms, impl->remaster_event[1],
                                   impl->remaster_event[2], &error)) {
            impl->error = "rtx_ngx_bridge_remaster: " + error;
            return RTX_ERR_INTERNAL;
        }
// Synchronous contract: on return the P010 planes are complete and NVENC
// may register the buffer immediately.  This wait costs CPU time but no
// pixel ever crosses the bus.
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
    output->ring_slot_index = static_cast<uint32_t>(impl->remaster_frames_issued % 3u);

    uint32_t stats_host[4] = {0, 0, 0, 0};
    if (impl->stats_devptr != 0 &&
// The NGX stage spans the fence signal, the D3D12 evaluation and the fence
// wait; with the external semaphore the stream events see exactly that
// gap, on the fallback path the wall clock is the honest number.
        interop.memcpy_dtoh(stats_host, impl->stats_devptr, sizeof(stats_host), &error)) {
        const float frame_max = std::bit_cast<float>(stats_host[0]);
        const float frame_sum = std::bit_cast<float>(stats_host[1]);
        const uint32_t samples =
            static_cast<uint32_t>(impl->config.output_width) *
            static_cast<uint32_t>(impl->config.output_height);
        const float frame_avg = samples > 0 ? frame_sum / static_cast<float>(samples) : 0.0f;
        output->frame_max_nits = static_cast<uint32_t>(fmaxf(0.0f, frame_max) + 0.5f);
        output->frame_avg_nits = static_cast<uint32_t>(fmaxf(0.0f, frame_avg) + 0.5f);
#if defined(RTX_HAVE_NGX_SDK)
        if (impl->ngx != nullptr && samples > 0) {
            RtxNgxFrameStats adaptive{};
            adaptive.y_norm = std::clamp<double>(
                static_cast<double>(frame_avg) /
                    static_cast<double>(std::max<uint32_t>(1u, impl->config.max_luminance)),
                0.0, 1.0);
            adaptive.p_high = std::clamp<double>(
                static_cast<double>(stats_host[2]) / static_cast<double>(samples), 0.0, 1.0);
            impl->ngx->set_frame_stats(adaptive);
        }
#endif
        impl->last_preview_ptr = input->preview_rgba10_ptr;
    } else if (impl->stats_devptr != 0) {
// Phase 7 (Task 7-A3): read the GPU-reduced frame statistics back and (a)
// publish them in the output struct for the caller's MaxCLL/MaxFALL
// accumulation, (b) feed the content-adaptive TrueHDR law automatically so
// the next frame's contrast/saturation/mid-grey react to this frame.
        impl->error = "rtx_ngx_bridge_remaster: statistic readback failed: " + error;
        return RTX_ERR_INTERNAL;
    }
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
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_set_lut3d(
    RtxNgxBridge* bridge, const uint16_t* lut_half4_data, int32_t grid_size) {
    if (bridge == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (lut_half4_data == nullptr || grid_size <= 1) {
        std::lock_guard<std::mutex> lock(impl->remaster_mutex);
        impl->interop->destroy_lut_texture(&impl->lut_tex, &impl->lut_array);
        impl->lut_dim = 0;
        return RTX_OK;
    }
    if (grid_size < 2 || grid_size > 256) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    const size_t expected = static_cast<size_t>(grid_size) * grid_size * grid_size * 4u;
    if (expected > (1ull << 30)) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(impl->remaster_mutex);
    void* texture_object = nullptr;
    void* array = nullptr;
    std::string error;
    if (!impl->interop->create_lut_texture(lut_half4_data, grid_size, &texture_object,
                                           &array, &error)) {
        impl->error = "rtx_ngx_bridge_set_lut3d: " + error;
        return RTX_ERR_NGX;
    }
    void* old_tex = impl->lut_tex;
    void* old_array = impl->lut_array;
    impl->lut_tex = texture_object;
    impl->lut_array = array;
    impl->lut_dim = grid_size;
    if (old_tex != nullptr || old_array != nullptr) {
// Unbind: the caller frees the active LUT by passing NULL / 0.
        impl->interop->destroy_lut_texture(&old_tex, &old_array);
    }
    return RTX_OK;
}
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_preview_buffer(
    RtxNgxBridge* bridge, uint32_t slot, uint64_t* device_ptr, uint32_t* pitch) {
    if (bridge == nullptr || device_ptr == nullptr || pitch == nullptr || slot > 2) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *device_ptr = 0;
    *pitch = 0;
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(impl->remaster_mutex);
    if (!ensurePreviewResources(impl)) {
        return RTX_ERR_OUT_OF_MEMORY;
    }
    *device_ptr = impl->preview_ring[slot];
    *pitch = impl->preview_pitch;
// Swap in the new texture and release the previous one.
    return RTX_OK;
}
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_download_preview(
    RtxNgxBridge* bridge, void* destination, size_t destination_bytes) {
    if (bridge == nullptr || destination == nullptr) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(impl->remaster_mutex);
    if (impl->last_preview_ptr == 0 || impl->preview_pitch == 0) {
        impl->error = "rtx_ngx_bridge_download_preview: no preview frame was produced yet";
        return RTX_ERR_UNSUPPORTED;
    }
    const size_t needed =
        static_cast<size_t>(impl->preview_pitch) * 540u;  // 960x540 rows x pitch
    if (destination_bytes < needed) {
        impl->error = "rtx_ngx_bridge_download_preview: destination is too small";
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::string error;
    if (!impl->interop->memcpy_dtoh(destination, impl->last_preview_ptr, needed, &error)) {
        impl->error = "rtx_ngx_bridge_download_preview: " + error;
        return RTX_ERR_INTERNAL;
    }
    impl->error.clear();
    return RTX_OK;
}
RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_bridge_upload_frame(
    RtxNgxBridge* bridge, const void* y_plane, const void* uv_plane, uint32_t y_pitch,
    uint32_t uv_pitch, int32_t width, int32_t height, uint64_t* y_device,
    uint64_t* uv_device) {
    if (bridge == nullptr || y_plane == nullptr || uv_plane == nullptr || y_device == nullptr ||
        uv_device == nullptr || y_pitch == 0 || uv_pitch == 0 || width <= 0 || height <= 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *y_device = 0;
    *uv_device = 0;
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(impl->remaster_mutex);
    rtx_ngx::D3D12Interop& interop = *impl->interop;
    const size_t needed = static_cast<size_t>(y_pitch) * static_cast<size_t>(height) +
                          static_cast<size_t>(uv_pitch) * static_cast<size_t>(height) / 2u;
    if (needed > impl->upload_bytes) {
        if (impl->upload_buffer != 0) {
            std::string ignored;
            (void)interop.free_device(impl->upload_buffer, &ignored);
            impl->upload_buffer = 0;
            impl->upload_bytes = 0;
        }
        uint64_t pointer = 0;
        size_t pitch = 0;
        std::string error;
        if (!interop.alloc_pitched(&pointer, &pitch, needed, 1, &error)) {
            impl->error = "rtx_ngx_bridge_upload_frame: " + error;
            return RTX_ERR_OUT_OF_MEMORY;
        }
        impl->upload_buffer = pointer;
        impl->upload_bytes = needed;
    }
    std::string error;
    if (!interop.memcpy_htod(impl->upload_buffer, y_plane,
                             static_cast<size_t>(y_pitch) * static_cast<size_t>(height),
                             &error)) {
        impl->error = "rtx_ngx_bridge_upload_frame: " + error;
        return RTX_ERR_INTERNAL;
    }
    const uint64_t uv_offset =
        static_cast<uint64_t>(y_pitch) * static_cast<uint64_t>(height);
    if (!interop.memcpy_htod(impl->upload_buffer + uv_offset, uv_plane,
                             static_cast<size_t>(uv_pitch) * static_cast<size_t>(height) / 2u,
// Grow the staging buffer lazily: one allocation holds both planes
// (luma at the start, chroma after), rows padded to the caller pitch.
                             &error)) {
        impl->error = "rtx_ngx_bridge_upload_frame: " + error;
        return RTX_ERR_INTERNAL;
    }
    *y_device = impl->upload_buffer;
    *uv_device = impl->upload_buffer + uv_offset;
    impl->error.clear();
    return RTX_OK;
}
RTX_SHIM_API uint64_t RTX_SHIM_CALL rtx_ngx_bridge_get_interp_buffer(
    RtxNgxBridge* bridge, uint32_t width, uint32_t height) {
    if (bridge == nullptr) return 0;
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid()) return 0;
    std::lock_guard<std::mutex> lock(impl->remaster_mutex);
    const uint32_t aligned_width = std::max<uint32_t>(width, (width + 511u) & ~511u);
    const size_t needed = static_cast<size_t>(aligned_width) * height * 3u / 2u;
    if (impl->interp_buffer != 0 && impl->interp_bytes >= needed) {
        return impl->interp_buffer;
    }
    if (impl->interp_buffer != 0) {
        std::string ignored;
        (void)impl->interop->free_device(impl->interp_buffer, &ignored);
        impl->interp_buffer = 0;
        impl->interp_bytes = 0;
    }
    uint64_t pointer = 0;
    size_t pitch = 0;
    std::string error;
    if (!impl->interop->alloc_pitched(&pointer, &pitch, needed, 1, &error)) {
        return 0;
    }
    impl->interp_buffer = pointer;
    impl->interp_bytes = needed;
    return impl->interp_buffer;
}
RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_interpolate_nv12(
    RtxNgxBridge* bridge, uint64_t prev_nv12, uint64_t curr_nv12,
    uint32_t pitch, float alpha, uint64_t out_nv12) {
    if (bridge == nullptr || prev_nv12 == 0 || curr_nv12 == 0 || pitch == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(impl->remaster_mutex);
    std::string ctx_err;
    if (!impl->interop->bind_context(&ctx_err)) {
        impl->error = "rtx_ngx_bridge_nvof: " + ctx_err;
        return RTX_ERR_INTERNAL;
    }
    uint32_t width = impl->config.input_width ? impl->config.input_width : pitch;
    uint32_t height = (impl->rife_engine && impl->rife_engine->height() > 0) ? impl->rife_engine->height() : (impl->config.input_height ? impl->config.input_height : 1080u);
    if (out_nv12 == 0) {
        const size_t needed = static_cast<size_t>(pitch) * height * 3u / 2u;
        if (impl->interp_buffer == 0 || impl->interp_bytes < needed) {
            if (impl->interp_buffer != 0) {
                std::string ignored;
                (void)impl->interop->free_device(impl->interp_buffer, &ignored);
                impl->interp_buffer = 0;
            }
            uint64_t pointer = 0;
            size_t p = 0;
            std::string error;
            if (!impl->interop->alloc_pitched(&pointer, &p, needed, 1, &error)) {
                return RTX_ERR_OUT_OF_MEMORY;
            }
            impl->interp_buffer = pointer;
            impl->interp_bytes = needed;
        }
        out_nv12 = impl->interp_buffer;
    }
    if (!impl->nvof_engine || impl->nvof_engine->width() != width || impl->nvof_engine->height() != height) {
        impl->nvof_engine = std::make_unique<rtx_ngx::NvofEngine>();
        std::string of_err;
        if (!impl->nvof_engine->init(impl->interop->context(), width, height, impl->interop->pack_stream(), &of_err)) {
            return rtx_ngx::launchNvofWarpNv12(prev_nv12, curr_nv12, 0, 0, 0, pitch, width, height,
                                               (width + 3u) / 4u, (height + 3u) / 4u, alpha, out_nv12,
                                               impl->interop->pack_stream());
        }
    }
    uint64_t fwd_flow = 0;
    uint64_t bwd_flow = 0;
    uint64_t cost_buffer = 0;
    std::string exec_err;
    if (!impl->nvof_engine->execute(prev_nv12, curr_nv12, pitch, impl->interop->pack_stream(),
                                   &fwd_flow, &bwd_flow, &cost_buffer, &exec_err)) {
        const RtxResult fb_res = rtx_ngx::launchNvofWarpNv12(
            prev_nv12, curr_nv12, 0, 0, 0, pitch, width, height,
            (width + 3u) / 4u, (height + 3u) / 4u, alpha, out_nv12,
            impl->interop->pack_stream(), 0, 0);
        if (fb_res == RTX_OK) {
            std::string sync_err;
            (void)impl->interop->stream_synchronize(impl->interop->pack_stream(), &sync_err);
        }
        return fb_res;
    }
    const RtxResult warp_res = rtx_ngx::launchNvofWarpNv12(
        prev_nv12, curr_nv12, fwd_flow, bwd_flow, cost_buffer, pitch, width, height,
        impl->nvof_engine->gridWidth(), impl->nvof_engine->gridHeight(),
        alpha, out_nv12, impl->interop->pack_stream(),
        impl->nvof_engine->flowStrideBytes(), impl->nvof_engine->costStrideBytes());
    if (warp_res == RTX_OK) {
        std::string sync_err;
        (void)impl->interop->stream_synchronize(impl->interop->pack_stream(), &sync_err);
    }
// Lazy initialization of hardware NVOFA engine
    return warp_res;
}
RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_init_rife(
    RtxNgxBridge* bridge,
// NVOFA fallback: launch warp with identity flow (scene bypass)
    const char* engine_path,
    uint32_t width,
    uint32_t height) {
    if (bridge == nullptr || engine_path == nullptr || width == 0 || height == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(impl->remaster_mutex);
    std::string ctx_err;
    if (!impl->interop->bind_context(&ctx_err)) {
        impl->error = "rtx_ngx_bridge_init_rife: " + ctx_err;
        return RTX_ERR_INTERNAL;
    }
    if (!impl->rife_engine) {
        impl->rife_engine = std::make_unique<rtx_ngx::RifeEngine>();
    }
    std::string err;
    if (!impl->rife_engine->loadEngine(engine_path, width, height, &err)) {
        impl->error = "rtx_ngx_bridge_init_rife: " + err;
        return RTX_ERR_DLL_MISSING;
    }
    impl->error.clear();
    return RTX_OK;
}
RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_init_rife_ex(
    RtxNgxBridge* bridge,
    const char* engine_path,
    const RtxRifeInitConfig* config,
    uint64_t* out_trt_peak_bytes) {
    if (bridge == nullptr || engine_path == nullptr || config == nullptr ||
        config->width == 0 || config->height == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(impl->remaster_mutex);
    std::string ctx_err;
    if (!impl->interop->bind_context(&ctx_err)) {
        impl->error = "rtx_ngx_bridge_init_rife_ex: " + ctx_err;
        return RTX_ERR_INTERNAL;
    }
    if (!impl->rife_engine) {
        impl->rife_engine = std::make_unique<rtx_ngx::RifeEngine>();
    }
    const float scale = (config->scale > 0.0f && config->scale <= 1.0f) ? config->scale : 1.0f;
    const uint32_t align_mod = (config->align_mod > 0u) ? config->align_mod : 128u;
    const bool reflect_pad = (config->reflect_padding != 0);
    std::string err;
    if (!impl->rife_engine->loadEngineEx(engine_path, config->width, config->height,
                                         scale, align_mod, reflect_pad,
                                         out_trt_peak_bytes, &err)) {
        impl->error = "rtx_ngx_bridge_init_rife_ex: " + err;
        return RTX_ERR_DLL_MISSING;
    }
    impl->error.clear();
    return RTX_OK;
}
RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_interpolate_rife(
    RtxNgxBridge* bridge,
    uint64_t prev_nv12,
    uint64_t curr_nv12,
    uint32_t pitch,
    float alpha,
    uint64_t out_nv12,
    int32_t full_range,
    int32_t* scene_cut) {
    if (bridge == nullptr || prev_nv12 == 0 || curr_nv12 == 0 || pitch == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid() || !impl->rife_engine || !impl->rife_engine->isLoaded()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(impl->remaster_mutex);
    std::string ctx_err;
    if (!impl->interop->bind_context(&ctx_err)) {
        impl->error = "rtx_ngx_bridge_interpolate_rife: " + ctx_err;
        return RTX_ERR_INTERNAL;
    }
    uint32_t height = impl->config.input_height ? impl->config.input_height : 1080;
    const size_t needed = static_cast<size_t>(pitch) * height * 3u / 2u;
    if (out_nv12 == 0) {
        if (impl->interp_buffer == 0 || impl->interp_bytes < needed) {
            if (impl->interp_buffer != 0) {
                std::string ignored;
                (void)impl->interop->free_device(impl->interp_buffer, &ignored);
                impl->interp_buffer = 0;
            }
            uint64_t pointer = 0;
            size_t p = 0;
            std::string error;
            if (!impl->interop->alloc_pitched(&pointer, &p, needed, 1, &error)) {
                return RTX_ERR_OUT_OF_MEMORY;
            }
            impl->interp_buffer = pointer;
            impl->interp_bytes = needed;
        }
        out_nv12 = impl->interp_buffer;
    } else if (impl->interp_bytes < needed && out_nv12 == impl->interp_buffer) {
        std::string ignored;
        (void)impl->interop->free_device(impl->interp_buffer, &ignored);
        uint64_t pointer = 0;
        size_t p = 0;
        std::string error;
        if (!impl->interop->alloc_pitched(&pointer, &p, needed, 1, &error)) {
            return RTX_ERR_OUT_OF_MEMORY;
        }
        impl->interp_buffer = pointer;
        impl->interp_bytes = needed;
        out_nv12 = impl->interp_buffer;
    }
    int32_t local_scene_cut = 0;
    int32_t rife_res = impl->rife_engine->interpolate(
        prev_nv12, curr_nv12, pitch, alpha, out_nv12, full_range, &local_scene_cut,
        impl->interop->pack_stream());
    if (scene_cut) {
        *scene_cut = local_scene_cut;
    }
    if (rife_res == RTX_OK) {
        std::string sync_err;
        (void)impl->interop->stream_synchronize(impl->interop->pack_stream(), &sync_err);
    } else {
        impl->error = "rtx_ngx_bridge_interpolate_rife: TensorRT RIFE inference failed";
    }
    return rife_res;
}
RTX_SHIM_API void RTX_SHIM_CALL rtx_ngx_bridge_destroy_rife(
    RtxNgxBridge* bridge) {
    if (bridge == nullptr) return;
    BridgeImpl* impl = asBridge(bridge);
    std::lock_guard<std::mutex> lock(impl->remaster_mutex);
    if (impl->rife_engine) {
        impl->rife_engine.reset();
    }
}
RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_borrow_preview(
    RtxNgxBridge* bridge, uint32_t slot, void** out_d3d12_device, void** out_d3d12_resource,
    uint32_t* out_pitch) {
    if (bridge == nullptr || out_d3d12_device == nullptr || out_d3d12_resource == nullptr ||
// Enlarge interp_buffer if caller passed cached pointer but pitch grew
        out_pitch == nullptr || slot > 2u) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    *out_d3d12_device = nullptr;
    *out_d3d12_resource = nullptr;
    *out_pitch = 0;
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    const auto& ring = impl->interop->previewRing();
    if (slot >= ring.size() || ring[slot].resource == nullptr) {
        impl->error = "rtx_ngx_bridge_borrow_preview: the preview ring is unavailable";
        return RTX_ERR_UNSUPPORTED;
    }
    *out_d3d12_device = impl->interop->device();
    *out_d3d12_resource = ring[slot].resource;
    *out_pitch = impl->interop->previewRingRowBytes();
    impl->error.clear();
    return RTX_OK;
}
RTX_SHIM_API int32_t RTX_SHIM_CALL rtx_ngx_bridge_playback_pack(
    RtxNgxBridge* bridge, uint64_t y_plane, uint64_t uv_plane, uint32_t y_pitch,
    uint32_t uv_pitch, uint32_t width, uint32_t height, int32_t format, int32_t full_range,
    int32_t matrix, uint32_t slot) {
    if (bridge == nullptr || y_plane == 0 || uv_plane == 0 || y_pitch == 0 || uv_pitch == 0 ||
        width == 0 || height == 0 || slot > 2u) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    BridgeImpl* impl = asBridge(bridge);
    if (!impl->interop || !impl->interop->valid()) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (!impl->headless_playback) {
        impl->error = "rtx_ngx_bridge_playback_pack: requires the headless playback bridge";
        return RTX_ERR_UNSUPPORTED;
    }
    const auto& ring = impl->interop->previewRing();
    if (slot >= ring.size() || ring[slot].device_ptr == 0) {
        impl->error = "rtx_ngx_bridge_playback_pack: the preview ring is unavailable";
        return RTX_ERR_UNSUPPORTED;
    }
    void* stream = impl->interop->pack_stream();
    if (stream == nullptr) {
        impl->error = "rtx_ngx_bridge_playback_pack: the pack stream is unavailable";
        return RTX_ERR_INTERNAL;
    }
    std::string error;
    if (!impl->interop->bind_context(&error)) {
        impl->error = "rtx_ngx_bridge_playback_pack: " + error;
        return impl->interop->pollDeviceLost(nullptr) ? RTX_ERR_DEVICE_LOST : RTX_ERR_INTERNAL;
    }
    rtx_ngx::PreprocessBufferLaunch launch{};
    launch.y_plane = y_plane;
    launch.uv_plane = uv_plane;
    launch.output_ptr = ring[slot].device_ptr;
    launch.output_pitch_bytes = impl->interop->previewRingRowBytes();
    launch.width = static_cast<int32_t>(width);
    launch.height = static_cast<int32_t>(height);
    launch.y_pitch_bytes = static_cast<int32_t>(y_pitch);
    launch.uv_pitch_bytes = static_cast<int32_t>(uv_pitch);
    launch.format = format;
    launch.full_range = full_range;
    launch.matrix = matrix;
    const RtxResult packed = launchPreprocessBuffer(launch, stream);
    if (packed != RTX_OK) {
        impl->error = "rtx_ngx_bridge_playback_pack: " + std::string(rtx_ngx::packLastError());
        return packed;
    }
    if (!impl->interop->stream_synchronize(stream, &error)) {
        impl->error = "rtx_ngx_bridge_playback_pack: " + error;
        return RTX_ERR_INTERNAL;
    }
    impl->last_preview_ptr = ring[slot].device_ptr;
    impl->error.clear();
    return RTX_OK;
// 头部播放桥专用：转码桥的 960x540 固定环与源尺寸不匹配，避免把按源尺寸
// 的写入灌进固定几何。
}
}  // extern "C"
// 同步语义：内核 + 流同步完成后才返回，槽位缓冲对视口的 D3D12 队列完全
// 可见，跨引擎读写不需要任何共享 fence 编排。
