/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * rtx_fruc_bridge -- static archive linked into CineRTX (Zone C bridge).
 *
 * Zone C bridge: the only translation unit that is allowed to know that NVIDIA
 * modules exist.  It exposes nothing but the pure C table declared in
 * include/shim/fruc_bridge_abi.h.
 */

#include "offruc_bridge.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rtx_fruc {
namespace {

// ---------------------------------------------------------------------------
// CUDA driver subset.  Resolved by hand: linking cudart or cuda.lib would put
// nvcuda.dll into the import table and break Gate 3.0 check B.
// ---------------------------------------------------------------------------
using CuMemcpyDtoDAsyncFn = int (*)(uint64_t dst, uint64_t src, size_t bytes, void* stream);
using CuMemsetD8AsyncFn = int (*)(uint64_t dst, unsigned char value, size_t count, void* stream);
using CuStreamSynchronizeFn = int (*)(void* stream);

struct CudaApi {
    CuMemcpyDtoDAsyncFn memcpy_dtod_async = nullptr;
    CuMemsetD8AsyncFn memset_d8_async = nullptr;
    CuStreamSynchronizeFn stream_synchronize = nullptr;
    bool loaded = false;
};

struct OfrucApi {
    // The Optical Flow SDK entry points are used through opaque signatures: the
    // bridge only forwards handles, so no SDK header is required here (and none
    // may be included from a file that the GPL core also compiles).
    void* create = nullptr;
    void* process = nullptr;
    bool loaded = false;
};

#if defined(_WIN32)
constexpr unsigned long kLoadFlags =
    LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR;
using ModuleHandle = HMODULE;

ModuleHandle openSystemModule(const wchar_t* name) {
    return ::LoadLibraryExW(name, nullptr, kLoadFlags);
}

// NvOFFRUC.dll is an SDK redistributable, not a driver component: it is
// deployed beside CineRTX.exe.  Opening it by bare name would only ever reach
// System32, which is why a correctly deployed DLL used to be reported as
// unavailable.  RTX_NVIDIA_DIR is honoured first, then the executable
// directory, and only then the plain name.  LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
// resolves the sibling cudart64_110.dll the FRUC DLL imports, without widening
// the search path to the working directory or PATH.
ModuleHandle openSdkModule(const wchar_t* name) {
    std::vector<std::wstring> directories;
    // GetEnvironmentVariableW rather than _wgetenv: the CRT variant is flagged
    // as deprecated under /W4 and this file builds warning-clean.
    wchar_t configured[8192] = {};
    const DWORD configured_length =
        ::GetEnvironmentVariableW(L"RTX_NVIDIA_DIR", configured, 8192);
    if (configured_length > 0 && configured_length < 8192) {
        const std::wstring value(configured, configured_length);
        size_t start = 0;
        while (start <= value.size()) {
            const size_t end = value.find(L';', start);
            const std::wstring entry =
                value.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!entry.empty()) {
                directories.push_back(entry);
            }
            if (end == std::wstring::npos) {
                break;
            }
            start = end + 1;
        }
    }

    wchar_t buffer[32768] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, buffer, 32768);
    if (length != 0 && length < 32768) {
        std::wstring directory(buffer, length);
        const size_t slash = directory.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            directory.resize(slash);
            directories.push_back(directory);
        }
    }

    for (const std::wstring& directory : directories) {
        std::wstring candidate = directory;
        if (!candidate.empty() && candidate.back() != L'\\' && candidate.back() != L'/') {
            candidate.push_back(L'\\');
        }
        candidate += name;
        HMODULE module = ::LoadLibraryExW(candidate.c_str(), nullptr, kLoadFlags);
        if (module != nullptr) {
            return module;
        }
    }
    return ::LoadLibraryExW(name, nullptr, kLoadFlags);
}

void* resolveSymbol(ModuleHandle handle, const char* name) {
    return handle != nullptr ? reinterpret_cast<void*>(::GetProcAddress(handle, name)) : nullptr;
}
#else
using ModuleHandle = void*;

ModuleHandle openSystemModule(const char* name) {
    return ::dlopen(name, RTLD_NOW | RTLD_LOCAL);
}

void* resolveSymbol(ModuleHandle handle, const char* name) {
    return handle != nullptr ? ::dlsym(handle, name) : nullptr;
}
#endif

// Process-wide, lazily resolved modules.  Resolved once: a second LoadLibraryExW
// per bridge instance would leak a reference count for every transcode job.
struct Modules {
    ModuleHandle cuda = nullptr;
    ModuleHandle ofruc = nullptr;
    CudaApi cuda_api{};
    OfrucApi ofruc_api{};
    bool probed = false;
};

Modules& modules() {
    static Modules instance;
    return instance;
}

void probeModules() {
    Modules& state = modules();
    if (state.probed) {
        return;
    }
    state.probed = true;

#if defined(_WIN32)
    state.cuda = openSystemModule(L"nvcuda.dll");
    state.ofruc = openSdkModule(L"NvOFFRUC.dll");
#else
    state.cuda = openSystemModule("libcuda.so.1");
    state.ofruc = openSystemModule("libNvOFFRUC.so");
#endif

    if (state.cuda != nullptr) {
        state.cuda_api.memcpy_dtod_async =
            reinterpret_cast<CuMemcpyDtoDAsyncFn>(resolveSymbol(state.cuda, "cuMemcpyDtoDAsync"));
        state.cuda_api.memset_d8_async =
            reinterpret_cast<CuMemsetD8AsyncFn>(resolveSymbol(state.cuda, "cuMemsetD8Async"));
        state.cuda_api.stream_synchronize =
            reinterpret_cast<CuStreamSynchronizeFn>(resolveSymbol(state.cuda, "cuStreamSynchronize"));
        state.cuda_api.loaded = state.cuda_api.memcpy_dtod_async != nullptr;
    }
    if (state.ofruc != nullptr) {
        state.ofruc_api.create = resolveSymbol(state.ofruc, "NvOFFRUCCreate");
        state.ofruc_api.process = resolveSymbol(state.ofruc, "NvOFFRUCProcess");
        state.ofruc_api.loaded =
            state.ofruc_api.create != nullptr && state.ofruc_api.process != nullptr;
    }
}

}  // namespace

uint64_t nv12FrameBytes(uint32_t width, uint32_t height) noexcept {
    return static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * 3u / 2u;
}

uint64_t opticalFlowGridBytes(uint32_t width, uint32_t height) noexcept {
    // One motion vector per 4x4 luma block, two float2 components.
    const uint64_t cells =
        static_cast<uint64_t>((width + 3u) / 4u) * static_cast<uint64_t>((height + 3u) / 4u);
    return cells * 2u * sizeof(float) * 2u;
}

uint64_t estimateVramFootprint(uint32_t width, uint32_t height, bool gpu_mode) noexcept {
    if (!gpu_mode) {
        return 0;
    }
    // Two reference surfaces (frame N and N+1) plus the flow grid.  The hard
    // ceiling of the module is 120 MB; a 4K pair still fits comfortably.
    const uint64_t footprint = nv12FrameBytes(width, height) * 2u +
                               opticalFlowGridBytes(width, height);
    constexpr uint64_t kCeiling = 120ull * 1024ull * 1024ull;
    return std::min<uint64_t>(footprint, kCeiling);
}

const char* backendName(uint32_t backend_type) noexcept {
    switch (backend_type) {
        case RTX_FRUC_BACKEND_NONE:
            return "none";
        case RTX_FRUC_BACKEND_NVOFFRUC:
            return "nvofruc";
        case RTX_FRUC_BACKEND_TRT_RIFE:
            return "trt-rife";
        case RTX_FRUC_BACKEND_DUPLICATE:
            return "duplicate";
        default:
            return "unknown";
    }
}

const char* statusName(int32_t status) noexcept {
    switch (status) {
        case RTX_FRUC_OK:
            return "OK";
        case RTX_FRUC_ERR_INVALID_ARGUMENT:
            return "INVALID_ARGUMENT";
        case RTX_FRUC_ERR_ABI_MISMATCH:
            return "ABI_MISMATCH";
        case RTX_FRUC_ERR_UNAVAILABLE:
            return "UNAVAILABLE";
        case RTX_FRUC_ERR_CUDA:
            return "CUDA_ERROR";
        case RTX_FRUC_ERR_OUT_OF_MEMORY:
            return "OUT_OF_MEMORY";
        case RTX_FRUC_ERR_STATE:
            return "INVALID_STATE";
        case RTX_FRUC_ERR_UNSUPPORTED:
            return "UNSUPPORTED";
        default:
            return "UNKNOWN";
    }
}

void writeError(char* err_buf, uint32_t err_cap, const std::string& message) noexcept {
    if (err_buf == nullptr || err_cap == 0) {
        return;
    }
    const size_t limit = static_cast<size_t>(err_cap) - 1u;
    const size_t length = std::min(message.size(), limit);
    if (length > 0) {
        std::memcpy(err_buf, message.data(), length);
    }
    err_buf[length] = '\0';
}

}  // namespace rtx_fruc

// ---------------------------------------------------------------------------
// Bridge object
// ---------------------------------------------------------------------------

struct RtxFrucBridge {
    RtxFrucConfig config{};
    uint64_t frame_bytes = 0;
    uint64_t vram_footprint = 0;
    bool gpu_mode = false;
    bool vendor_ready = false;
    uint64_t bypass_count = 0;
    uint64_t interpolate_count = 0;
};

namespace {

using rtx_fruc::writeError;

int32_t validateConfig(const RtxFrucConfig* cfg, std::string& error) {
    if (cfg == nullptr) {
        error = "config pointer is null";
        return RTX_FRUC_ERR_INVALID_ARGUMENT;
    }
    if (cfg->abi_version != RTX_FRUC_BRIDGE_ABI_VERSION) {
        error = "ABI mismatch: caller " + std::to_string(cfg->abi_version) + ", bridge " +
                std::to_string(RTX_FRUC_BRIDGE_ABI_VERSION);
        return RTX_FRUC_ERR_ABI_MISMATCH;
    }
    if (cfg->width == 0 || cfg->height == 0) {
        error = "width and height must be non-zero";
        return RTX_FRUC_ERR_INVALID_ARGUMENT;
    }
    // NV12 requires even geometry: chroma is subsampled 2x2.
    if ((cfg->width & 1u) != 0 || (cfg->height & 1u) != 0) {
        error = "NV12 requires even width and height";
        return RTX_FRUC_ERR_INVALID_ARGUMENT;
    }
    if (cfg->surface_pitch < cfg->width) {
        error = "surface_pitch must be at least width";
        return RTX_FRUC_ERR_INVALID_ARGUMENT;
    }
    if (cfg->backend_type > RTX_FRUC_BACKEND_DUPLICATE) {
        error = "unknown backend_type " + std::to_string(cfg->backend_type);
        return RTX_FRUC_ERR_INVALID_ARGUMENT;
    }
    return RTX_FRUC_OK;
}

int32_t createBridge(const RtxFrucConfig* cfg, RtxFrucBridge** out_bridge, char* err_buf,
                     uint32_t err_cap) {
    if (out_bridge == nullptr) {
        writeError(err_buf, err_cap, "out_bridge is null");
        return RTX_FRUC_ERR_INVALID_ARGUMENT;
    }
    *out_bridge = nullptr;

    std::string error;
    const int32_t validation = validateConfig(cfg, error);
    if (validation != RTX_FRUC_OK) {
        writeError(err_buf, err_cap, error);
        return validation;
    }

    rtx_fruc::probeModules();
    const rtx_fruc::Modules& state = rtx_fruc::modules();
    const bool gpu_mode = cfg->cuda_context != nullptr;

    // The duplicate bypass needs no vendor module, so a bridge is always
    // constructible.  `vendor_ready` records whether the requested backend can
    // do real interpolation; interpolate() reports the missing dependency per
    // frame instead of silently emitting duplicates.
    bool vendor_ready = true;
    if (cfg->backend_type == RTX_FRUC_BACKEND_NVOFFRUC) {
        vendor_ready = state.ofruc_api.loaded && state.cuda_api.loaded && gpu_mode;
    } else if (cfg->backend_type == RTX_FRUC_BACKEND_TRT_RIFE) {
        vendor_ready = state.cuda_api.loaded && gpu_mode && cfg->model_engine_path != nullptr &&
                       cfg->model_engine_path[0] != '\0';
    }

    auto* bridge = new (std::nothrow) RtxFrucBridge();
    if (bridge == nullptr) {
        writeError(err_buf, err_cap, "cannot allocate the bridge object");
        return RTX_FRUC_ERR_OUT_OF_MEMORY;
    }
    bridge->config = *cfg;
    bridge->frame_bytes = rtx_fruc::nv12FrameBytes(cfg->width, cfg->height);
    bridge->gpu_mode = gpu_mode;
    bridge->vendor_ready = vendor_ready;
    bridge->vram_footprint = rtx_fruc::estimateVramFootprint(cfg->width, cfg->height, gpu_mode);
    *out_bridge = bridge;
    if (!vendor_ready) {
        // Non-fatal: the status bar turns this into a self-heal card while the
        // hard-cut bypass keeps the pipeline correct.
        writeError(err_buf, err_cap,
                   std::string("backend '") + rtx_fruc::backendName(cfg->backend_type) +
                       "' has no vendor module; duplicate bypass only");
    }
    return RTX_FRUC_OK;
}

// The hard-cut / static-frame bypass: out := prev, byte for byte.  This is the
// mechanism that makes "0 tearing artefacts across a scene cut" structural
// rather than statistical.
int32_t duplicateBypass(RtxFrucBridge* bridge, const RtxFrucFrameInput* in) {
    if (in->out_nv12_devptr == 0 || in->prev_nv12_devptr == 0) {
        return RTX_FRUC_ERR_INVALID_ARGUMENT;
    }
    if (bridge->gpu_mode) {
        const rtx_fruc::CudaApi& cuda = rtx_fruc::modules().cuda_api;
        if (cuda.memcpy_dtod_async == nullptr) {
            return RTX_FRUC_ERR_UNAVAILABLE;
        }
        const int status = cuda.memcpy_dtod_async(in->out_nv12_devptr, in->prev_nv12_devptr,
                                                  static_cast<size_t>(bridge->frame_bytes),
                                                  bridge->config.cuda_stream);
        if (status != 0) {
            return RTX_FRUC_ERR_CUDA;
        }
    } else {
        std::memcpy(reinterpret_cast<void*>(in->out_nv12_devptr),
                    reinterpret_cast<const void*>(in->prev_nv12_devptr),
                    static_cast<size_t>(bridge->frame_bytes));
    }
    ++bridge->bypass_count;
    return RTX_FRUC_OK;
}

int32_t interpolateFrame(RtxFrucBridge* bridge, const RtxFrucFrameInput* in) {
    if (bridge == nullptr || in == nullptr) {
        return RTX_FRUC_ERR_INVALID_ARGUMENT;
    }
    // A hard cut or a static frame is served by the duplicate bypass on every
    // backend: optical flow across a cut produces exactly the jelly artefact
    // this engine must never emit (FMEA-02).
    if (in->is_scene_cut != 0 || in->is_static_frame != 0 ||
        bridge->config.backend_type == RTX_FRUC_BACKEND_DUPLICATE) {
        return duplicateBypass(bridge, in);
    }
    if (!bridge->vendor_ready) {
        // Never fabricate a frame: report the missing dependency so the caller
        // can surface a self-heal card (or fall back to DUPLICATE explicitly).
        return RTX_FRUC_ERR_UNAVAILABLE;
    }
    if (in->timestep < 0.0f || in->timestep > 1.0f) {
        return RTX_FRUC_ERR_INVALID_ARGUMENT;
    }
    // Vendor optical-flow / TensorRT dispatch lives behind the OFA process
    // entry point; the CPU reference mode has no synthesised interpolant, so a
    // non-bypass request there is reported as unsupported rather than silently
    // returning a blended guess.
    if (!bridge->gpu_mode) {
        return RTX_FRUC_ERR_UNSUPPORTED;
    }
    ++bridge->interpolate_count;
    return RTX_FRUC_ERR_UNSUPPORTED;
}

int32_t buildEngine(const char* onnx_path, const char* engine_out_path, uint32_t width,
                    uint32_t height, uint32_t fp16, char* err_buf, uint32_t err_cap) {
    (void)width;
    (void)height;
    (void)fp16;
    if (onnx_path == nullptr || engine_out_path == nullptr) {
        writeError(err_buf, err_cap, "build_engine requires an ONNX input and an engine output");
        return RTX_FRUC_ERR_INVALID_ARGUMENT;
    }
    // Engine construction is deliberately not implemented inside the bridge:
    // the CLI owns the TensorRT builder (src/shim/backends/tensorrt) because it
    // also has to persist the .engine.sha256 sidecar and enforce the version
    // gate.  Keeping one implementation avoids two divergent build paths.
    writeError(err_buf, err_cap,
               "engine construction is owned by the CLI: run "
               "'CineRTX --build-rife --onnx <model.onnx> --engine <model.engine>'");
    return RTX_FRUC_ERR_UNSUPPORTED;
}

uint64_t queryVramFootprint(RtxFrucBridge* bridge) {
    return bridge != nullptr ? bridge->vram_footprint : 0;
}

void destroyBridge(RtxFrucBridge* bridge) {
    delete bridge;
}

const RtxFrucBridgeApi kApi = {
    RTX_FRUC_BRIDGE_ABI_VERSION,
    &createBridge,
    &interpolateFrame,
    &buildEngine,
    &queryVramFootprint,
    &destroyBridge,
};

}  // namespace

extern "C" {

RTX_FRUC_EXPORT const RtxFrucBridgeApi* rtx_fruc_bridge_get_api(void) {
    return &kApi;
}

RTX_FRUC_EXPORT uint32_t rtx_fruc_bridge_abi_version(void) {
    return RTX_FRUC_BRIDGE_ABI_VERSION;
}

}  // extern "C"
