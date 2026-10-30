/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Headless D3D12 <-> CUDA interop implementation.  See d3d12_interop.h.
 */

#include "d3d12_interop.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ffnvcodec/dynlink_cuda.h"

namespace rtx_ngx {
namespace {

// Opt-in bring-up trace (RTX_BRIDGE_TRACE=1).  D3D12/CUDA interop failures are
// otherwise indistinguishable from a silent crash on a new driver.
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
    std::fprintf(stderr, "[rtx_ngx] %s%s%s\n", step, detail.empty() ? "" : ": ",
                 detail.c_str());
    std::fflush(stderr);
}

// COM interface ids.  Declared locally so the bridge never links a Direct3D
// import library (the audit requires a zero static Direct3D import too).
const GUID kIidFactory4 = {0x1bc6ea02,
                           0xef36,
                           0x464f,
                           {0xbf, 0x0c, 0x21, 0xca, 0x39, 0xe5, 0x16, 0x8a}};
const GUID kIidDevice = {0x189819f1,
                         0x1db6,
                         0x4b57,
                         {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
const GUID kIidCommandQueue = {0x0ec870a6,
                               0x5d7e,
                               0x4c22,
                               {0x8c, 0xfc, 0x5b, 0xaa, 0xe0, 0x76, 0x16, 0xed}};
const GUID kIidResource = {0x696442be,
                           0xa72e,
                           0x4059,
                           {0xbc, 0x79, 0x5b, 0x5c, 0x98, 0x04, 0x0f, 0xad}};
const GUID kIidCommandAllocator = {0x6102dee4,
                                  0xaf59,
                                  0x4b09,
                                  {0xb9, 0x99, 0xb4, 0x4d, 0x73, 0xf0, 0x9b, 0x24}};
const GUID kIidGraphicsCommandList = {0x5b160d0f,
                                      0xac1b,
                                      0x4185,
                                      {0x8b, 0xa8, 0xb3, 0xae, 0x42, 0xa5, 0xa4, 0x55}};
const GUID kIidFence = {0x0a753dcf,
                        0xc4d8,
                        0x4b91,
                        {0xad, 0xf6, 0xbe, 0x5a, 0x60, 0xd9, 0x5a, 0x76}};

constexpr unsigned kExternalMemoryDedicated = 0x1;

// dynlink_cuda.h covers the decode/encode path; the surface-object entry points
// are declared here so the bridge stays self-contained.
using CUsurfObject = unsigned long long;
using PFN_cuSurfObjectCreate = CUresult (*)(CUsurfObject*, const CUDA_RESOURCE_DESC*);
using PFN_cuSurfObjectDestroy = CUresult (*)(CUsurfObject);

template <typename T>
T* as(void* pointer) {
    return static_cast<T*>(pointer);
}

struct D3D12Api {
    HRESULT(WINAPI* create_device)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**) = nullptr;
    HRESULT(WINAPI* create_factory)(REFIID, void**) = nullptr;
    // Enables "adapter removal" semantics: a device reset is reported as
    // DXGI_ERROR_DEVICE_REMOVED and the process can recover instead of being
    // terminated by the driver watchdog (anti-TDR mechanism 4).
    HRESULT(WINAPI* declare_adapter_removal_support)() = nullptr;
    // The declaration is process-wide and only valid before the first D3D12
    // device exists.  cuInit() itself creates DXGI state on current drivers
    // (observed: a later declaration returns DXGI_ERROR_INVALID_CALL), so it
    // runs exactly once here, at the earliest bridge entry point.
    bool removal_attempted = false;
    bool removal_declared = false;
    HRESULT removal_result = S_OK;

    bool load(std::string* error) {
        HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE dxgi = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (d3d12 == nullptr || dxgi == nullptr) {
            *error = "d3d12.dll or dxgi.dll is not available";
            return false;
        }
        create_device = reinterpret_cast<decltype(create_device)>(
            GetProcAddress(d3d12, "D3D12CreateDevice"));
        create_factory = reinterpret_cast<decltype(create_factory)>(
            GetProcAddress(dxgi, "CreateDXGIFactory1"));
        declare_adapter_removal_support = reinterpret_cast<decltype(declare_adapter_removal_support)>(
            GetProcAddress(dxgi, "DXGIDeclareAdapterRemovalSupport"));
        if (create_device == nullptr || create_factory == nullptr) {
            *error = "D3D12CreateDevice/CreateDXGIFactory1 exports are missing";
            return false;
        }
        if (!removal_attempted) {
            removal_attempted = true;
            if (declare_adapter_removal_support != nullptr) {
                removal_result = declare_adapter_removal_support();
                removal_declared = SUCCEEDED(removal_result);
            } else {
                removal_result = static_cast<HRESULT>(0x80004002L);  // E_NOINTERFACE
            }
        }
        return true;
    }
};

struct CudaApi {
    CUresult (*init)(unsigned int) = nullptr;
    CUresult (*device_get)(CUdevice*, int) = nullptr;
    CUresult (*ctx_get_current)(CUcontext*) = nullptr;
    CUresult (*ctx_set_current)(CUcontext) = nullptr;
    CUresult (*primary_ctx_retain)(CUcontext*, CUdevice) = nullptr;
    CUresult (*primary_ctx_set_flags)(CUdevice, unsigned int) = nullptr;
    CUresult (*import_external_memory)(CUexternalMemory*,
                                       const CUDA_EXTERNAL_MEMORY_HANDLE_DESC*) = nullptr;
    CUresult (*get_mapped_mipmapped_array)(CUmipmappedArray*, CUexternalMemory,
                                           const CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC*) = nullptr;
    CUresult (*mipmapped_array_get_level)(CUarray*, CUmipmappedArray, unsigned int) = nullptr;
    PFN_cuSurfObjectCreate surf_object_create = nullptr;
    PFN_cuSurfObjectDestroy surf_object_destroy = nullptr;
    CUresult (*destroy_external_memory)(CUexternalMemory) = nullptr;
    CUresult (*mipmapped_array_destroy)(CUmipmappedArray) = nullptr;
    // cuMemcpy2D accepts CUDA_MEMCPY2D with array endpoints, which covers both
    // directions without the array-specific entry points.
    CUresult (*memcpy2d)(const CUDA_MEMCPY2D*) = nullptr;
    CUresult (*memcpy2d_async)(const CUDA_MEMCPY2D*, CUstream) = nullptr;
    CUresult (*stream_synchronize)(CUstream) = nullptr;
    CUresult (*get_error_name)(CUresult, const char**) = nullptr;
    // GPU hardware fence interop: the shared ID3D12Fence is imported as a CUDA
    // external semaphore so the cross-engine hand-off never blocks the CPU.
    using CUexternalSemaphore = void*;
    CUresult (*import_external_semaphore)(CUexternalSemaphore*,
                                          const CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC*) = nullptr;
    CUresult (*signal_external_semaphores_async)(const CUexternalSemaphore*,
                                                 const CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS*,
                                                 unsigned int, CUstream) = nullptr;
    CUresult (*wait_external_semaphores_async)(const CUexternalSemaphore*,
                                               const CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS*,
                                               unsigned int, CUstream) = nullptr;
    CUresult (*destroy_external_semaphore)(CUexternalSemaphore) = nullptr;
    // Anti-TDR mechanism 3: the pack stage runs on a dedicated highest-priority
    // stream so it drains ahead of opportunistic copies.
    CUresult (*stream_create_with_priority)(CUstream*, unsigned int, int) = nullptr;
    CUresult (*stream_destroy)(CUstream) = nullptr;
    CUresult (*ctx_get_stream_priority_range)(int*, int*) = nullptr;
    // Remaster support (P0-1.4): the bridge owns a pitched P010 output buffer
    // and times the per-frame stages with CUDA events.  Resolved optionally so
    // an older driver keeps the non-remaster bridge paths working.
    CUresult (*mem_alloc_pitch)(CUdeviceptr*, size_t*, size_t, size_t, unsigned int) = nullptr;
    CUresult (*mem_free)(CUdeviceptr) = nullptr;
    CUresult (*event_create)(CUevent*, unsigned int) = nullptr;
    CUresult (*event_record)(CUevent, CUstream) = nullptr;
    CUresult (*event_elapsed)(float*, CUevent, CUevent) = nullptr;
    CUresult (*event_destroy)(CUevent) = nullptr;

    bool load(std::string* error) {
        HMODULE module = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module == nullptr) {
            *error = "nvcuda.dll is not available";
            return false;
        }
        auto sym = [module](const char* name) { return GetProcAddress(module, name); };
        init = reinterpret_cast<decltype(init)>(sym("cuInit"));
        device_get = reinterpret_cast<decltype(device_get)>(sym("cuDeviceGet"));
        ctx_get_current = reinterpret_cast<decltype(ctx_get_current)>(sym("cuCtxGetCurrent"));
        ctx_set_current = reinterpret_cast<decltype(ctx_set_current)>(sym("cuCtxSetCurrent"));
        primary_ctx_retain =
            reinterpret_cast<decltype(primary_ctx_retain)>(sym("cuDevicePrimaryCtxRetain"));
        primary_ctx_set_flags = reinterpret_cast<decltype(primary_ctx_set_flags)>(
            sym("cuDevicePrimaryCtxSetFlags"));
        import_external_memory =
            reinterpret_cast<decltype(import_external_memory)>(sym("cuImportExternalMemory"));
        get_mapped_mipmapped_array = reinterpret_cast<decltype(get_mapped_mipmapped_array)>(
            sym("cuExternalMemoryGetMappedMipmappedArray"));
        mipmapped_array_get_level =
            reinterpret_cast<decltype(mipmapped_array_get_level)>(sym("cuMipmappedArrayGetLevel"));
        surf_object_create =
            reinterpret_cast<decltype(surf_object_create)>(sym("cuSurfObjectCreate"));
        surf_object_destroy =
            reinterpret_cast<decltype(surf_object_destroy)>(sym("cuSurfObjectDestroy"));
        destroy_external_memory =
            reinterpret_cast<decltype(destroy_external_memory)>(sym("cuDestroyExternalMemory"));
        mipmapped_array_destroy = reinterpret_cast<decltype(mipmapped_array_destroy)>(
            sym("cuMipmappedArrayDestroy"));
        // 64-bit Windows exports the modern struct-compatible entry points with
        // a _v2 suffix since CUDA 3.2; the bare names keep the legacy 32-bit
        // ABI.  Loading the bare symbol into a 64-bit CUDA_MEMCPY2D call makes
        // the driver misparse the struct (observed: CUDA_ERROR_INVALID_CONTEXT).
        memcpy2d = reinterpret_cast<decltype(memcpy2d)>(sym("cuMemcpy2D_v2"));
        if (memcpy2d == nullptr) {
            memcpy2d = reinterpret_cast<decltype(memcpy2d)>(sym("cuMemcpy2D"));
        }
        memcpy2d_async = reinterpret_cast<decltype(memcpy2d_async)>(sym("cuMemcpy2DAsync_v2"));
        if (memcpy2d_async == nullptr) {
            memcpy2d_async =
                reinterpret_cast<decltype(memcpy2d_async)>(sym("cuMemcpy2DAsync"));
        }
        stream_synchronize =
            reinterpret_cast<decltype(stream_synchronize)>(sym("cuStreamSynchronize_v2"));
        if (stream_synchronize == nullptr) {
            stream_synchronize =
                reinterpret_cast<decltype(stream_synchronize)>(sym("cuStreamSynchronize"));
        }
        import_external_semaphore = reinterpret_cast<decltype(import_external_semaphore)>(
            sym("cuImportExternalSemaphore"));
        signal_external_semaphores_async =
            reinterpret_cast<decltype(signal_external_semaphores_async)>(
                sym("cuSignalExternalSemaphoresAsync_v2"));
        if (signal_external_semaphores_async == nullptr) {
            signal_external_semaphores_async =
                reinterpret_cast<decltype(signal_external_semaphores_async)>(
                    sym("cuSignalExternalSemaphoresAsync"));
        }
        wait_external_semaphores_async =
            reinterpret_cast<decltype(wait_external_semaphores_async)>(
                sym("cuWaitExternalSemaphoresAsync_v2"));
        if (wait_external_semaphores_async == nullptr) {
            wait_external_semaphores_async =
                reinterpret_cast<decltype(wait_external_semaphores_async)>(
                    sym("cuWaitExternalSemaphoresAsync"));
        }
        destroy_external_semaphore = reinterpret_cast<decltype(destroy_external_semaphore)>(
            sym("cuDestroyExternalSemaphore"));
        stream_create_with_priority = reinterpret_cast<decltype(stream_create_with_priority)>(
            sym("cuStreamCreateWithPriority"));
        stream_destroy = reinterpret_cast<decltype(stream_destroy)>(sym("cuStreamDestroy_v2"));
        if (stream_destroy == nullptr) {
            stream_destroy = reinterpret_cast<decltype(stream_destroy)>(sym("cuStreamDestroy"));
        }
        ctx_get_stream_priority_range =
            reinterpret_cast<decltype(ctx_get_stream_priority_range)>(
                sym("cuCtxGetStreamPriorityRange"));
        // The buffer/event families carry the legacy 32-bit ABI under their
        // bare names; on 64-bit Windows the _v2 forms are the struct-compatible
        // ones (same lesson as cuMemcpy2D above).  Fall back for old drivers.
        mem_alloc_pitch = reinterpret_cast<decltype(mem_alloc_pitch)>(sym("cuMemAllocPitch_v2"));
        if (mem_alloc_pitch == nullptr) {
            mem_alloc_pitch =
                reinterpret_cast<decltype(mem_alloc_pitch)>(sym("cuMemAllocPitch"));
        }
        mem_free = reinterpret_cast<decltype(mem_free)>(sym("cuMemFree_v2"));
        if (mem_free == nullptr) {
            mem_free = reinterpret_cast<decltype(mem_free)>(sym("cuMemFree"));
        }
        event_create = reinterpret_cast<decltype(event_create)>(sym("cuEventCreate"));
        event_record = reinterpret_cast<decltype(event_record)>(sym("cuEventRecord_v2"));
        if (event_record == nullptr) {
            event_record = reinterpret_cast<decltype(event_record)>(sym("cuEventRecord"));
        }
        event_elapsed = reinterpret_cast<decltype(event_elapsed)>(sym("cuEventElapsedTime"));
        event_destroy = reinterpret_cast<decltype(event_destroy)>(sym("cuEventDestroy_v2"));
        if (event_destroy == nullptr) {
            event_destroy = reinterpret_cast<decltype(event_destroy)>(sym("cuEventDestroy"));
        }
        get_error_name = reinterpret_cast<decltype(get_error_name)>(sym("cuGetErrorName"));
        // The external-semaphore family is optional: older drivers keep the
        // CPU-wait fallback alive.  Everything else is mandatory.
        if (init == nullptr || import_external_memory == nullptr ||
            get_mapped_mipmapped_array == nullptr || surf_object_create == nullptr ||
            memcpy2d == nullptr || memcpy2d_async == nullptr ||
            stream_synchronize == nullptr) {
            *error = std::string("the CUDA driver lacks the external-memory API (init=") +
                     (init ? "ok" : "null") + ", import=" +
                     (import_external_memory ? "ok" : "null") + ", mip=" +
                     (get_mapped_mipmapped_array ? "ok" : "null") + ", surf=" +
                     (surf_object_create ? "ok" : "null") + ", copy=" +
                     (memcpy2d ? "ok" : "null") + ", sync=" +
                     (stream_synchronize ? "ok" : "null") + ")";
            return false;
        }
        return true;
    }
};

D3D12Api& d3d12Api() {
    static D3D12Api api;
    return api;
}

CudaApi& cudaApi() {
    static CudaApi api;
    return api;
}

const char* cudaErrorName(CUresult result);

bool ensureCudaContext(std::string* error) {
    CudaApi& cuda = cudaApi();
    if (cuda.init == nullptr && !cuda.load(error)) {
        return false;
    }
    if (cuda.device_get == nullptr || cuda.primary_ctx_retain == nullptr ||
        cuda.ctx_set_current == nullptr) {
        *error = "the CUDA driver lacks the context API";
        return false;
    }
    if (cuda.init(0) != CUDA_SUCCESS) {
        *error = "cuInit failed";
        return false;
    }
    CUcontext current = nullptr;
    if (cuda.ctx_get_current != nullptr) {
        (void)cuda.ctx_get_current(&current);
    }
    if (current != nullptr) {
        return true;
    }
    CUdevice device = 0;
    CUcontext primary = nullptr;
    if (cuda.device_get(&device, 0) != CUDA_SUCCESS) {
        *error = "cannot open CUDA device 0";
        return false;
    }
    // Expert directive B1: every CUDA stream in this process uses blocking
    // synchronisation.  The default spin/yield scheduler burns an entire core
    // while the D3D12 queue evaluates VSR/TrueHDR, which steals CPU time from
    // the decode and encode stages.  An already-active primary context keeps
    // its flags, so a rejected update is not fatal.
    if (cuda.primary_ctx_set_flags != nullptr) {
        const CUresult flags =
            cuda.primary_ctx_set_flags(device, CU_CTX_SCHED_BLOCKING_SYNC);
        if (flags != CUDA_SUCCESS) {
            trace("create: primary context keeps its existing scheduler flags",
                  cudaErrorName(flags));
        }
    }
    if (cuda.primary_ctx_retain(&primary, device) != CUDA_SUCCESS) {
        *error = "cannot retain the CUDA primary context";
        return false;
    }
    (void)cuda.ctx_set_current(primary);
    return true;
}

const char* cudaErrorName(CUresult result) {
    const char* name = "CUDA error";
    CudaApi& cuda = cudaApi();
    if (cuda.get_error_name != nullptr && cuda.get_error_name(result, &name) == CUDA_SUCCESS) {
        return name;
    }
    return name;
}

// Single source of truth for the CUDA array descriptor of an interop texture.
// The channel count and the per-channel format must multiply out to exactly
// the DXGI texel size; cuExternalMemoryGetMappedMipmappedArray rejects any
// descriptor that overruns the imported D3D12 allocation (CUDA_ERROR_INVALID_
// VALUE).  A packed 32-bit DXGI format therefore maps to ONE 32-bit channel,
// not four.
struct FormatMapping {
    int32_t format;
    DXGI_FORMAT dxgi;
    CUarray_format cuda;
    unsigned int channels;
    uint32_t bytes;
};

constexpr FormatMapping kFormats[] = {
    {RTX_PIXEL_RGBA16F, DXGI_FORMAT_R16G16B16A16_FLOAT, CU_AD_FORMAT_HALF, 4, 8},
    {RTX_PIXEL_R10G10B10A2, DXGI_FORMAT_R10G10B10A2_UNORM, CU_AD_FORMAT_UNSIGNED_INT32, 1, 4},
    {RTX_PIXEL_RGB32, DXGI_FORMAT_R8G8B8A8_UNORM, CU_AD_FORMAT_UNSIGNED_INT8, 4, 4},
};

const FormatMapping* formatMapping(int32_t format) {
    for (const FormatMapping& mapping : kFormats) {
        if (mapping.format == format) {
            return &mapping;
        }
    }
    return nullptr;
}

DXGI_FORMAT toDxgi(int32_t format) {
    const FormatMapping* mapping = formatMapping(format);
    return mapping == nullptr ? DXGI_FORMAT_UNKNOWN : mapping->dxgi;
}

CUarray_format toCudaFormat(int32_t format) {
    const FormatMapping* mapping = formatMapping(format);
    return mapping == nullptr ? CU_AD_FORMAT_UNSIGNED_INT8 : mapping->cuda;
}

int channelsOf(int32_t format) {
    const FormatMapping* mapping = formatMapping(format);
    return mapping == nullptr ? 0 : static_cast<int>(mapping->channels);
}

// Bytes per channel for the CUarray formats used above.  A descriptor whose
// channels * channel_bytes != bytesPerPixel(format) overruns the D3D12
// allocation and is rejected by the driver.
uint32_t channelBytes(CUarray_format format) {
    switch (format) {
        case CU_AD_FORMAT_HALF:
            return 2;
        case CU_AD_FORMAT_UNSIGNED_INT32:
            return 4;
        case CU_AD_FORMAT_UNSIGNED_INT8:
            return 1;
        default:
            return 0;
    }
}

uint32_t bytesPerPixel(int32_t format) {
    const FormatMapping* mapping = formatMapping(format);
    return mapping == nullptr ? 0 : mapping->bytes;
}

bool waitForFence(ID3D12Fence* fence, uint64_t value) {
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr) {
        return false;
    }
    const bool ok = SUCCEEDED(fence->SetEventOnCompletion(value, event)) &&
                    WaitForSingleObject(event, 10000) == WAIT_OBJECT_0;
    CloseHandle(event);
    return ok;
}

}  // namespace

D3D12Interop::~D3D12Interop() { destroy(); }

bool D3D12Interop::create(const RtxNgxBridgeConfig& config, std::string* error) {
    trace("create: begin");
    if (!d3d12Api().load(error) || !cudaApi().load(error)) {
        trace("create: api load failed", *error);
        return false;
    }
    // Order matters: create the D3D12 device first, then bind the CUDA primary
    // context, then import.  Creating the device can detach the thread's CUDA
    // context; importing under an implicit context makes later copies fail with
    // CUDA_ERROR_INVALID_CONTEXT.
    if (!create_device(config.luid, error)) {
        trace("create: device failed", *error);
        return false;
    }
    trace("create: device ready");
    if (!ensureCudaContext(error)) {
        trace("create: cuda context failed", *error);
        return false;
    }
    if (cudaApi().ctx_get_current != nullptr) {
        (void)cudaApi().ctx_get_current(reinterpret_cast<CUcontext*>(&cuda_context_));
    }
    if (cuda_context_ == nullptr) {
        *error = "the CUDA primary context is not current after D3D12 creation";
        return false;
    }
    trace("create: cuda context bound");
    // Anti-TDR mechanism 3: want the pack stage on the driver's highest
    // priority stream so it cannot be starved by opportunistic copies.  A
    // driver without the priority API only loses the ordering hint.
    std::string stream_error;
    if (!create_pack_stream(&stream_error)) {
        trace("create: high-priority pack stream unavailable", stream_error);
    } else {
        trace("create: high-priority pack stream ready");
    }
    if (!create_texture(static_cast<uint32_t>(config.input_width),
                        static_cast<uint32_t>(config.input_height), RTX_PIXEL_R10G10B10A2,
                        &input_, error)) {
        trace("create: input texture failed", *error);
        return false;
    }
    trace("create: input texture ready");
    if (!create_texture(static_cast<uint32_t>(config.output_width),
                        static_cast<uint32_t>(config.output_height), RTX_PIXEL_RGBA16F,
                        &output_, error)) {
        trace("create: output texture failed", *error);
        return false;
    }
    trace("create: output texture ready");
    // Expert directive B-2.1: VSR writes a 4K SDR result that TrueHDR then
    // reads; the two features cannot work in place on textures of different
    // geometry, so an internal 4K SDR intermediate texture is required
    // whenever the configuration upscales.  It is pure D3D12 (no shared
    // handle, no CUDA import, ~32 MB at 4K).
    if (static_cast<uint32_t>(config.output_width) > static_cast<uint32_t>(config.input_width) ||
        static_cast<uint32_t>(config.output_height) >
            static_cast<uint32_t>(config.input_height)) {
        if (!create_mid_texture(static_cast<uint32_t>(config.output_width),
                                static_cast<uint32_t>(config.output_height), error)) {
            trace("create: mid texture failed", *error);
            return false;
        }
        trace("create: mid 4K texture ready");
    }
    // Import the shared fence into CUDA as an external semaphore so the W5/W6
    // main loop can hand work between the CUDA streams and the D3D12 queue
    // without a CPU wait (expert hardening item 2).  A driver without the
    // external-semaphore API degrades to the CPU-wait fallback, so failure
    // here is recorded but not fatal.
    std::string semaphore_error;
    if (!import_fence_semaphore(&semaphore_error)) {
        trace("create: external semaphore unavailable", semaphore_error);
    } else {
        trace("create: external semaphore ready");
    }
    diagnostics_ = std::string("headless D3D12 device and shared textures ready; adapter removal ") +
                   (adapter_removal_declared_ ? "declared" : "not declared") +
                   "; pack stream priority " +
                   (pack_stream_ != nullptr
                        ? (pack_stream_high_priority_ ? "high" : "default")
                        : "unavailable");
    return true;
}

bool D3D12Interop::bind_context(std::string* error) {
    CudaApi& cuda = cudaApi();
    if (cuda_context_ == nullptr || cuda.ctx_set_current == nullptr) {
        *error = "no CUDA context to bind";
        return false;
    }
    const CUresult result = cuda.ctx_set_current(static_cast<CUcontext>(cuda_context_));
    if (result != CUDA_SUCCESS) {
        *error = std::string("cuCtxSetCurrent failed: ") + cudaErrorName(result);
        return false;
    }
    return true;
}

bool D3D12Interop::import_fence_semaphore(std::string* error) {
    CudaApi& cuda = cudaApi();
    if (fence_shared_handle_ == nullptr) {
        *error = "the timeline fence has no shared handle";
        return false;
    }
    if (cuda.import_external_semaphore == nullptr ||
        cuda.signal_external_semaphores_async == nullptr ||
        cuda.wait_external_semaphores_async == nullptr ||
        cuda.destroy_external_semaphore == nullptr) {
        *error = "the CUDA driver lacks the external-semaphore API";
        return false;
    }
    if (!bind_context(error)) {
        return false;
    }
    CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC desc{};
    desc.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE;
    desc.handle.win32.handle = fence_shared_handle_;
    desc.flags = 0;
    void* semaphore = nullptr;
    const CUresult imported = cuda.import_external_semaphore(&semaphore, &desc);
    if (imported != CUDA_SUCCESS || semaphore == nullptr) {
        *error = std::string("cuImportExternalSemaphore failed: ") + cudaErrorName(imported);
        return false;
    }
    external_semaphore_ = semaphore;
    return true;
}

bool D3D12Interop::fence_signal_cuda(void* cuda_stream, uint64_t value, std::string* error) {
    CudaApi& cuda = cudaApi();
    if (external_semaphore_ == nullptr || cuda.signal_external_semaphores_async == nullptr) {
        *error = "the fence semaphore is not imported";
        return false;
    }
    if (!bind_context(error)) {
        return false;
    }
    CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS params{};
    params.params.fence.value = value;
    void* semaphore = external_semaphore_;
    const CUresult signalled = cuda.signal_external_semaphores_async(
        &semaphore, &params, 1, static_cast<CUstream>(cuda_stream));
    if (signalled != CUDA_SUCCESS) {
        *error = std::string("cuSignalExternalSemaphoresAsync failed: ") +
                 cudaErrorName(signalled);
        return false;
    }
    return true;
}

bool D3D12Interop::fence_wait_cuda(void* cuda_stream, uint64_t value, std::string* error) {
    CudaApi& cuda = cudaApi();
    if (external_semaphore_ == nullptr || cuda.wait_external_semaphores_async == nullptr) {
        *error = "the fence semaphore is not imported";
        return false;
    }
    if (!bind_context(error)) {
        return false;
    }
    CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS params{};
    params.params.fence.value = value;
    void* semaphore = external_semaphore_;
    const CUresult waited = cuda.wait_external_semaphores_async(
        &semaphore, &params, 1, static_cast<CUstream>(cuda_stream));
    if (waited != CUDA_SUCCESS) {
        *error = std::string("cuWaitExternalSemaphoresAsync failed: ") + cudaErrorName(waited);
        return false;
    }
    return true;
}

bool D3D12Interop::queue_wait_fence(uint64_t value, std::string* error) {
    ID3D12CommandQueue* queue = as<ID3D12CommandQueue>(queue_);
    ID3D12Fence* fence = as<ID3D12Fence>(fence_);
    if (queue == nullptr || fence == nullptr) {
        *error = "the D3D12 queue or fence is missing";
        return false;
    }
    if (FAILED(queue->Wait(fence, value))) {
        *error = "ID3D12CommandQueue::Wait on the shared fence failed";
        return false;
    }
    return true;
}

bool D3D12Interop::queue_signal_fence(uint64_t value, std::string* error) {
    ID3D12CommandQueue* queue = as<ID3D12CommandQueue>(queue_);
    ID3D12Fence* fence = as<ID3D12Fence>(fence_);
    if (queue == nullptr || fence == nullptr) {
        *error = "the D3D12 queue or fence is missing";
        return false;
    }
    if (FAILED(queue->Signal(fence, value))) {
        *error = "ID3D12CommandQueue::Signal on the shared fence failed";
        return false;
    }
    // Anti-TDR mechanism 2 bookkeeping: retain the values of submissions the
    // queue has not retired yet.  Values at or below the completed watermark
    // are pruned, keeping the list bounded by the live queue depth.
    const uint64_t completed = fence->GetCompletedValue();
    auto stale = queue_pending_values_.begin();
    while (stale != queue_pending_values_.end() && *stale <= completed) {
        ++stale;
    }
    queue_pending_values_.erase(queue_pending_values_.begin(), stale);
    queue_pending_values_.push_back(value);
    queue_submitted_value_ = value;
    return true;
}

size_t D3D12Interop::queue_depth() const {
    const uint64_t completed = queue_completed_value();
    size_t depth = 0;
    for (uint64_t value : queue_pending_values_) {
        if (value > completed) {
            ++depth;
        }
    }
    return depth;
}

bool D3D12Interop::wait_for_queue_depth(size_t max_depth, std::chrono::milliseconds timeout,
                                        std::string* error) {
    ID3D12Fence* fence = as<ID3D12Fence>(fence_);
    if (fence == nullptr) {
        *error = "the D3D12 fence is missing";
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (queue_depth() <= max_depth) {
            return true;
        }
        // Wait on the oldest outstanding submission only: once it retires the
        // depth strictly decreases, so the loop cannot spin on a busy fence.
        const uint64_t completed = fence->GetCompletedValue();
        uint64_t oldest = 0;
        bool found = false;
        for (uint64_t value : queue_pending_values_) {
            if (value > completed && (!found || value < oldest)) {
                oldest = value;
                found = true;
            }
        }
        if (!found) {
            return true;
        }
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (event == nullptr) {
            *error = "cannot create the fence event";
            return false;
        }
        if (FAILED(fence->SetEventOnCompletion(oldest, event))) {
            CloseHandle(event);
            *error = "cannot arm the fence event for the queue-depth wait";
            return false;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now())
                                   .count();
        const DWORD wait =
            WaitForSingleObject(event, remaining > 0 ? static_cast<DWORD>(remaining) : 0);
        CloseHandle(event);
        std::string lost_error;
        if (pollDeviceLost(&lost_error)) {
            *error = "device removed while waiting for queue depth: " + lost_error;
            return false;
        }
        if (wait == WAIT_TIMEOUT && std::chrono::steady_clock::now() >= deadline) {
            *error = "the D3D12 queue did not drain within the anti-TDR budget";
            return false;
        }
    }
}

bool D3D12Interop::wait_for_fence_value(uint64_t value, std::chrono::milliseconds timeout,
                                        std::string* error) {
    ID3D12Fence* fence = as<ID3D12Fence>(fence_);
    if (fence == nullptr) {
        *error = "the D3D12 fence is missing";
        return false;
    }
    if (value == 0 || fence->GetCompletedValue() >= value) {
        return true;
    }
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (event == nullptr) {
        *error = "cannot create the shared-fence event";
        return false;
    }
    if (FAILED(fence->SetEventOnCompletion(value, event))) {
        CloseHandle(event);
        *error = "cannot arm the shared-fence event";
        return false;
    }
    const auto count = timeout.count();
    const DWORD wait = WaitForSingleObject(
        event, count > 0 ? static_cast<DWORD>(count) : 0);
    CloseHandle(event);
    std::string lost_error;
    if (pollDeviceLost(&lost_error)) {
        *error = "device removed while waiting for the shared fence: " + lost_error;
        return false;
    }
    if (wait != WAIT_OBJECT_0) {
        char text[96] = {};
        std::snprintf(text, sizeof(text),
                      "the shared fence did not reach value %llu in time",
                      static_cast<unsigned long long>(value));
        *error = text;
        return false;
    }
    return true;
}

bool D3D12Interop::pollDeviceLost(std::string* error) {
    ID3D12Device* device = as<ID3D12Device>(device_);
    if (device == nullptr) {
        return device_lost_;
    }
    const HRESULT reason = device->GetDeviceRemovedReason();
    if (SUCCEEDED(reason)) {
        return false;
    }
    device_lost_ = true;
    if (error != nullptr) {
        char text[96] = {};
        std::snprintf(text, sizeof(text), "D3D12 device removed (HRESULT 0x%08lx)",
                      static_cast<unsigned long>(reason));
        *error = text;
    }
    return true;
}

bool D3D12Interop::create_pack_stream(std::string* error) {
    CudaApi& cuda = cudaApi();
    if (pack_stream_ != nullptr) {
        return true;
    }
    if (cuda.stream_create_with_priority == nullptr ||
        cuda.ctx_get_stream_priority_range == nullptr) {
        *error = "the CUDA driver lacks the stream-priority API";
        return false;
    }
    if (!bind_context(error)) {
        return false;
    }
    int least = 0;
    int greatest = 0;
    const CUresult range = cuda.ctx_get_stream_priority_range(&least, &greatest);
    if (range != CUDA_SUCCESS) {
        *error = std::string("cuCtxGetStreamPriorityRange failed: ") + cudaErrorName(range);
        return false;
    }
    CUstream stream = nullptr;
    // CUDA numbers the highest priority with the smallest value.
    const CUresult created =
        cuda.stream_create_with_priority(&stream, CU_STREAM_NON_BLOCKING, greatest);
    if (created != CUDA_SUCCESS || stream == nullptr) {
        *error = std::string("cuStreamCreateWithPriority failed: ") + cudaErrorName(created);
        return false;
    }
    pack_stream_ = stream;
    pack_stream_priority_ = greatest;
    pack_stream_high_priority_ = greatest < least;
    return true;
}

bool D3D12Interop::alloc_pitched(uint64_t* device_ptr, size_t* pitch, size_t width_bytes,
                                 size_t rows, std::string* error) {
    if (device_ptr == nullptr || pitch == nullptr) {
        *error = "alloc_pitched requires output pointers";
        return false;
    }
    *device_ptr = 0;
    *pitch = 0;
    CudaApi& cuda = cudaApi();
    if (cuda.mem_alloc_pitch == nullptr) {
        *error = "the CUDA driver lacks cuMemAllocPitch";
        return false;
    }
    if (!bind_context(error)) {
        return false;
    }
    CUdeviceptr pointer = 0;
    size_t row_pitch = 0;
    const CUresult allocated =
        cuda.mem_alloc_pitch(&pointer, &row_pitch, width_bytes, rows, 16);
    if (allocated != CUDA_SUCCESS) {
        *error = std::string("cuMemAllocPitch failed: ") + cudaErrorName(allocated);
        return false;
    }
    *device_ptr = static_cast<uint64_t>(pointer);
    *pitch = row_pitch;
    return true;
}

bool D3D12Interop::free_device(uint64_t device_ptr, std::string* error) {
    if (device_ptr == 0) {
        return true;
    }
    CudaApi& cuda = cudaApi();
    if (cuda.mem_free == nullptr) {
        *error = "the CUDA driver lacks cuMemFree";
        return false;
    }
    if (!bind_context(error)) {
        return false;
    }
    const CUresult freed = cuda.mem_free(static_cast<CUdeviceptr>(device_ptr));
    if (freed != CUDA_SUCCESS) {
        *error = std::string("cuMemFree failed: ") + cudaErrorName(freed);
        return false;
    }
    return true;
}

bool D3D12Interop::event_create(void** event, std::string* error) {
    if (event == nullptr) {
        *error = "event_create requires an output pointer";
        return false;
    }
    *event = nullptr;
    CudaApi& cuda = cudaApi();
    if (cuda.event_create == nullptr || cuda.event_destroy == nullptr) {
        *error = "the CUDA driver lacks the event API";
        return false;
    }
    if (!bind_context(error)) {
        return false;
    }
    CUevent handle = nullptr;
    constexpr unsigned int kEventDefault = 0x0;  // CU_EVENT_DEFAULT
    const CUresult created = cuda.event_create(&handle, kEventDefault);
    if (created != CUDA_SUCCESS || handle == nullptr) {
        *error = std::string("cuEventCreate failed: ") + cudaErrorName(created);
        return false;
    }
    *event = handle;
    return true;
}

void D3D12Interop::event_destroy(void* event) noexcept {
    if (event == nullptr) {
        return;
    }
    CudaApi& cuda = cudaApi();
    if (cuda.event_destroy == nullptr) {
        return;
    }
    (void)cuda.event_destroy(static_cast<CUevent>(event));
}

bool D3D12Interop::event_record(void* event, void* stream, std::string* error) {
    if (event == nullptr) {
        *error = "event_record requires an event";
        return false;
    }
    CudaApi& cuda = cudaApi();
    if (cuda.event_record == nullptr) {
        *error = "the CUDA driver lacks cuEventRecord";
        return false;
    }
    if (!bind_context(error)) {
        return false;
    }
    const CUresult recorded =
        cuda.event_record(static_cast<CUevent>(event), static_cast<CUstream>(stream));
    if (recorded != CUDA_SUCCESS) {
        *error = std::string("cuEventRecord failed: ") + cudaErrorName(recorded);
        return false;
    }
    return true;
}

bool D3D12Interop::event_elapsed(double* milliseconds, void* start, void* end,
                                 std::string* error) {
    if (milliseconds == nullptr || start == nullptr || end == nullptr) {
        *error = "event_elapsed requires both events and an output pointer";
        return false;
    }
    *milliseconds = 0.0;
    CudaApi& cuda = cudaApi();
    if (cuda.event_elapsed == nullptr) {
        *error = "the CUDA driver lacks cuEventElapsedTime";
        return false;
    }
    float value = 0.0f;
    const CUresult elapsed = cuda.event_elapsed(&value, static_cast<CUevent>(start),
                                                static_cast<CUevent>(end));
    if (elapsed != CUDA_SUCCESS) {
        *error = std::string("cuEventElapsedTime failed: ") + cudaErrorName(elapsed);
        return false;
    }
    *milliseconds = static_cast<double>(value);
    return true;
}

bool D3D12Interop::stream_synchronize(void* stream, std::string* error) {
    CudaApi& cuda = cudaApi();
    if (cuda.stream_synchronize == nullptr) {
        *error = "the CUDA driver lacks cuStreamSynchronize";
        return false;
    }
    if (!bind_context(error)) {
        return false;
    }
    const CUresult synced = cuda.stream_synchronize(static_cast<CUstream>(stream));
    if (synced != CUDA_SUCCESS) {
        *error = std::string("cuStreamSynchronize failed: ") + cudaErrorName(synced);
        return false;
    }
    return true;
}


uint64_t D3D12Interop::queue_completed_value() const {
    ID3D12Fence* fence = as<ID3D12Fence>(fence_);
    return fence != nullptr ? fence->GetCompletedValue() : 0;
}

bool D3D12Interop::create_device(const uint8_t luid[8], std::string* error) {
    // Anti-TDR mechanism 4 status was latched by D3D12Api::load(): a GPU hang
    // resurfaces as DXGI_ERROR_DEVICE_REMOVED (mapped to RTX_ERR_DEVICE_LOST)
    // instead of terminating the encoder.  Absent on Windows before 10 1803 or
    // when CUDA was initialised first, where DXGI reports INVALID_CALL.
    adapter_removal_declared_ = d3d12Api().removal_declared;
    if (adapter_removal_declared_) {
        trace("create: adapter removal declared");
    } else {
        char text[64] = {};
        std::snprintf(text, sizeof(text), "HRESULT 0x%08lx",
                      static_cast<unsigned long>(d3d12Api().removal_result));
        trace("create: adapter removal unsupported", text);
    }
    IDXGIFactory4* factory = nullptr;
    HRESULT hr = d3d12Api().create_factory(kIidFactory4, reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || factory == nullptr) {
        *error = "CreateDXGIFactory1 failed";
        return false;
    }
    IDXGIAdapter1* matched = nullptr;
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (std::memcmp(&desc.AdapterLuid, luid, 8) == 0) {
            matched = adapter;
            break;
        }
        adapter->Release();
    }
    factory->Release();
    if (matched == nullptr) {
        *error = "no DXGI adapter matches the CUDA device LUID";
        return false;
    }
    // 12_1 first (the spec target); older parts and drivers only expose 12_0
    // (Maxwell/Pascal) or 11_0.  Trying them in order keeps the interop layer
    // usable on the development host instead of failing at device creation.
    ID3D12Device* device = nullptr;
    for (D3D_FEATURE_LEVEL level : {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                                    D3D_FEATURE_LEVEL_11_0}) {
        hr = d3d12Api().create_device(matched, level, kIidDevice,
                                      reinterpret_cast<void**>(&device));
        if (SUCCEEDED(hr) && device != nullptr) {
            break;
        }
    }
    matched->Release();
    if (FAILED(hr) || device == nullptr) {
        *error = "D3D12CreateDevice failed for feature levels 12_1/12_0/11_0";
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ID3D12CommandQueue* queue = nullptr;
    hr = device->CreateCommandQueue(&queue_desc, kIidCommandQueue,
                                    reinterpret_cast<void**>(&queue));
    if (FAILED(hr) || queue == nullptr) {
        device->Release();
        *error = "CreateCommandQueue failed";
        return false;
    }
    ID3D12Fence* fence = nullptr;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, kIidFence,
                             reinterpret_cast<void**>(&fence));
    if (FAILED(hr) || fence == nullptr) {
        queue->Release();
        device->Release();
        *error = "CreateFence failed";
        return false;
    }
    // Export the fence as a shared Win32 handle so CUDA can import it as an
    // external semaphore (expert hardening item 2).  The handle is an NT
    // handle owned by this object; it stays valid for the fence lifetime.
    HANDLE fence_handle = nullptr;
    if (FAILED(device->CreateSharedHandle(fence, nullptr, GENERIC_ALL, nullptr,
                                          &fence_handle))) {
        fence->Release();
        queue->Release();
        device->Release();
        *error = "CreateSharedHandle for the timeline fence failed";
        return false;
    }
    device_ = device;
    queue_ = queue;
    fence_ = fence;
    fence_shared_handle_ = fence_handle;
    fence_value_ = 0;
    return true;
}

bool D3D12Interop::create_texture(uint32_t width, uint32_t height, int32_t format,
                                  InteropTexture* texture, std::string* error) {
    const DXGI_FORMAT dxgi = toDxgi(format);
    if (dxgi == DXGI_FORMAT_UNKNOWN) {
        *error = "unsupported interop pixel format";
        return false;
    }
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = dxgi;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                 D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    ID3D12Device* device = as<ID3D12Device>(device_);
    ID3D12Resource* resource = nullptr;
    const HRESULT hr = device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, kIidResource,
        reinterpret_cast<void**>(&resource));
    if (FAILED(hr) || resource == nullptr) {
        *error = "CreateCommittedResource(SHARED) failed";
        return false;
    }
    HANDLE handle = nullptr;
    if (FAILED(device->CreateSharedHandle(resource, nullptr, GENERIC_ALL, nullptr, &handle))) {
        resource->Release();
        *error = "CreateSharedHandle failed";
        return false;
    }
    texture->resource = resource;
    texture->shared_handle = handle;
    texture->width = width;
    texture->height = height;
    texture->format = format;
    texture->allocation_size = device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    if (!import_into_cuda(texture, error)) {
        return false;
    }
    return true;
}

bool D3D12Interop::create_mid_texture(uint32_t width, uint32_t height, std::string* error) {
    // 4K SDR intermediate surface for the VSR -> TrueHDR chain (expert B-2.1):
    // committed, default heap, UAV-capable, deliberately NOT shared and NOT
    // imported into CUDA -- only the D3D12 command queue touches it.
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    // The RTX Video SDK reference allocates the VSR -> TrueHDR intermediate as
    // R8G8B8A8_UNORM; TrueHDR reads that 8-bit SDR texture and produces the
    // 10-bit/FP16 HDR output.
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    ID3D12Device* device = as<ID3D12Device>(device_);
    ID3D12Resource* resource = nullptr;
    const HRESULT hr = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                       D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                       kIidResource, reinterpret_cast<void**>(&resource));
    if (FAILED(hr) || resource == nullptr) {
        *error = "CreateCommittedResource for the 4K SDR mid texture failed";
        return false;
    }
    mid_4k_.resource = resource;
    mid_4k_.width = width;
    mid_4k_.height = height;
    mid_4k_.format = RTX_PIXEL_RGB32;
    mid_4k_.allocation_size = device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    return true;
}

bool D3D12Interop::import_into_cuda(InteropTexture* texture, std::string* error) {
    CudaApi& cuda = cudaApi();
    CUDA_EXTERNAL_MEMORY_HANDLE_DESC handle_desc{};
    handle_desc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
    handle_desc.handle.win32.handle = texture->shared_handle;
    handle_desc.size = texture->allocation_size;
    // Windows 11 5xx/6xx drivers reject the import without DEDICATED.
    handle_desc.flags = kExternalMemoryDedicated;

    CUexternalMemory external = nullptr;
    CUresult result = cuda.import_external_memory(&external, &handle_desc);
    if (result != CUDA_SUCCESS) {
        *error = std::string("cuImportExternalMemory failed: ") + cudaErrorName(result);
        return false;
    }

    CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC mip_desc{};
    mip_desc.offset = 0;
    mip_desc.numLevels = 1;
    mip_desc.arrayDesc.Width = texture->width;
    mip_desc.arrayDesc.Height = texture->height;
    mip_desc.arrayDesc.Depth = 0;
    mip_desc.arrayDesc.Format = toCudaFormat(texture->format);
    mip_desc.arrayDesc.NumChannels = static_cast<unsigned int>(channelsOf(texture->format));
    // Guard the invariant the driver enforces opaquely: the array descriptor
    // must describe exactly the imported D3D12 allocation, byte for byte.
    if (mip_desc.arrayDesc.NumChannels == 0 ||
        channelBytes(mip_desc.arrayDesc.Format) * mip_desc.arrayDesc.NumChannels !=
            bytesPerPixel(texture->format)) {
        *error = "interop format " + std::to_string(texture->format) +
                 " has no consistent CUDA array descriptor";
        return false;
    }

    CUmipmappedArray mip = nullptr;
    result = cuda.get_mapped_mipmapped_array(&mip, external, &mip_desc);
    if (result != CUDA_SUCCESS) {
        cuda.destroy_external_memory(external);
        *error = std::string("cuExternalMemoryGetMappedMipmappedArray failed: ") +
                 cudaErrorName(result);
        return false;
    }
    CUarray array = nullptr;
    result = cuda.mipmapped_array_get_level(&array, mip, 0);
    if (result != CUDA_SUCCESS) {
        cuda.mipmapped_array_destroy(mip);
        cuda.destroy_external_memory(external);
        *error = std::string("cuMipmappedArrayGetLevel failed: ") + cudaErrorName(result);
        return false;
    }
    CUDA_RESOURCE_DESC resource_desc{};
    resource_desc.resType = CU_RESOURCE_TYPE_ARRAY;
    resource_desc.res.array.hArray = array;
    CUsurfObject surface = 0;
    result = cuda.surf_object_create(&surface, &resource_desc);
    if (result != CUDA_SUCCESS) {
        cuda.mipmapped_array_destroy(mip);
        cuda.destroy_external_memory(external);
        *error = std::string("cuSurfObjectCreate failed: ") + cudaErrorName(result);
        return false;
    }
    texture->external_memory = external;
    texture->mip_array = mip;
    texture->array = array;
    texture->surface = surface;
    return true;
}

bool D3D12Interop::flush_queue(std::string* error) {
    ID3D12Fence* fence = as<ID3D12Fence>(fence_);
    const uint64_t value = advance_timeline();
    if (!queue_signal_fence(value, error)) {
        return false;
    }
    if (!waitForFence(fence, value)) {
        *error = "D3D12 fence wait failed";
        return false;
    }
    return true;
}

bool D3D12Interop::upload(InteropTexture& texture, const void* data, size_t size,
                          std::string* error) {
    ID3D12Device* device = as<ID3D12Device>(device_);
    ID3D12CommandQueue* queue = as<ID3D12CommandQueue>(queue_);
    ID3D12Resource* target = as<ID3D12Resource>(texture.resource);

    D3D12_RESOURCE_DESC desc = target->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT rows = 0;
    UINT64 row_size = 0;
    UINT64 total = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &rows, &row_size, &total);
    if (size < static_cast<size_t>(row_size) * texture.height) {
        *error = "upload buffer is smaller than the texture";
        return false;
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer_desc{};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = total;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* staging = nullptr;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               kIidResource, reinterpret_cast<void**>(&staging)))) {
        *error = "cannot create the upload buffer";
        return false;
    }
    void* mapped = nullptr;
    if (FAILED(staging->Map(0, nullptr, &mapped))) {
        staging->Release();
        *error = "cannot map the upload buffer";
        return false;
    }
    const auto* source = static_cast<const uint8_t*>(data);
    for (UINT row = 0; row < texture.height; ++row) {
        std::memcpy(static_cast<uint8_t*>(mapped) + layout.Offset + row * layout.Footprint.RowPitch,
                    source + static_cast<size_t>(row) * row_size, static_cast<size_t>(row_size));
    }
    staging->Unmap(0, nullptr);

    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              kIidCommandAllocator,
                                              reinterpret_cast<void**>(&allocator)))) {
        staging->Release();
        *error = "cannot create a command allocator";
        return false;
    }
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                         kIidGraphicsCommandList,
                                         reinterpret_cast<void**>(&list)))) {
        allocator->Release();
        staging->Release();
        *error = "cannot create a command list";
        return false;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    list->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = target;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = staging;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = layout;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    list->ResourceBarrier(1, &barrier);
    list->Close();
    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    const bool ok = flush_queue(error);
    list->Release();
    allocator->Release();
    staging->Release();
    return ok;
}

bool D3D12Interop::download(InteropTexture& texture, void* data, size_t size,
                            std::string* error, uint64_t queue_wait_value) {
    ID3D12Device* device = as<ID3D12Device>(device_);
    ID3D12CommandQueue* queue = as<ID3D12CommandQueue>(queue_);
    ID3D12Resource* source_resource = as<ID3D12Resource>(texture.resource);

    D3D12_RESOURCE_DESC desc = source_resource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT rows = 0;
    UINT64 row_size = 0;
    UINT64 total = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &rows, &row_size, &total);
    if (size < static_cast<size_t>(row_size) * texture.height) {
        *error = "download buffer is smaller than the texture";
        return false;
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer_desc{};
    buffer_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Width = total;
    buffer_desc.Height = 1;
    buffer_desc.DepthOrArraySize = 1;
    buffer_desc.MipLevels = 1;
    buffer_desc.SampleDesc.Count = 1;
    buffer_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* staging = nullptr;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer_desc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr, kIidResource,
                                               reinterpret_cast<void**>(&staging)))) {
        *error = "cannot create the readback buffer";
        return false;
    }
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              kIidCommandAllocator,
                                              reinterpret_cast<void**>(&allocator)))) {
        staging->Release();
        *error = "cannot create a command allocator";
        return false;
    }
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator, nullptr,
                                         kIidGraphicsCommandList,
                                         reinterpret_cast<void**>(&list)))) {
        allocator->Release();
        staging->Release();
        *error = "cannot create a command list";
        return false;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = source_resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = staging;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = layout;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = source_resource;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = 0;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
    list->ResourceBarrier(1, &barrier);
    list->Close();
    // Optional GPU-side ordering: wait on the shared fence value that a CUDA
    // stream signalled, so the readback cannot overtake the CUDA write-back.
    if (queue_wait_value != 0) {
        ID3D12Fence* fence = as<ID3D12Fence>(fence_);
        if (fence == nullptr || FAILED(queue->Wait(fence, queue_wait_value))) {
            list->Release();
            allocator->Release();
            staging->Release();
            *error = "the D3D12 queue could not wait on the shared fence before readback";
            return false;
        }
    }
    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    bool ok = flush_queue(error);
    if (ok) {
        void* mapped = nullptr;
        if (FAILED(staging->Map(0, nullptr, &mapped))) {
            *error = "cannot map the readback buffer";
            ok = false;
        } else {
            auto* destination = static_cast<uint8_t*>(data);
            for (UINT row = 0; row < texture.height; ++row) {
                std::memcpy(destination + static_cast<size_t>(row) * row_size,
                            static_cast<const uint8_t*>(mapped) + layout.Offset +
                                row * layout.Footprint.RowPitch,
                            static_cast<size_t>(row_size));
            }
            staging->Unmap(0, nullptr);
        }
    }
    list->Release();
    allocator->Release();
    staging->Release();
    return ok;
}

void D3D12Interop::destroy() {
    CudaApi& cuda = cudaApi();
    std::string ignored_error;
    if (cuda_context_ != nullptr) {
        (void)bind_context(&ignored_error);
    }
    if (mid_4k_.resource != nullptr) {
        as<ID3D12Resource>(mid_4k_.resource)->Release();
        mid_4k_.resource = nullptr;
    }
    for (InteropTexture* texture : {&input_, &output_}) {
        if (texture->surface != 0 && cuda.surf_object_destroy != nullptr) {
            cuda.surf_object_destroy(texture->surface);
            texture->surface = 0;
        }
        if (texture->mip_array != nullptr && cuda.mipmapped_array_destroy != nullptr) {
            cuda.mipmapped_array_destroy(static_cast<CUmipmappedArray>(texture->mip_array));
            texture->mip_array = nullptr;
        }
        if (texture->external_memory != nullptr && cuda.destroy_external_memory != nullptr) {
            cuda.destroy_external_memory(static_cast<CUexternalMemory>(texture->external_memory));
            texture->external_memory = nullptr;
        }
        if (texture->resource != nullptr) {
            as<ID3D12Resource>(texture->resource)->Release();
            texture->resource = nullptr;
        }
        if (texture->shared_handle != nullptr) {
            CloseHandle(static_cast<HANDLE>(texture->shared_handle));
            texture->shared_handle = nullptr;
        }
    }
    if (external_semaphore_ != nullptr) {
        if (cuda.destroy_external_semaphore != nullptr) {
            (void)cuda.destroy_external_semaphore(external_semaphore_);
        }
        external_semaphore_ = nullptr;
    }
    if (pack_stream_ != nullptr) {
        if (cuda.stream_destroy != nullptr) {
            (void)cuda.stream_destroy(static_cast<CUstream>(pack_stream_));
        }
        pack_stream_ = nullptr;
    }
    if (fence_shared_handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(fence_shared_handle_));
        fence_shared_handle_ = nullptr;
    }
    if (fence_ != nullptr) {
        as<ID3D12Fence>(fence_)->Release();
        fence_ = nullptr;
    }
    if (queue_ != nullptr) {
        as<ID3D12CommandQueue>(queue_)->Release();
        queue_ = nullptr;
    }
    if (device_ != nullptr) {
        as<ID3D12Device>(device_)->Release();
        device_ = nullptr;
    }
    cuda_context_ = nullptr;
}

bool interopPrerequisitesAvailable() {
    std::string error;
    return d3d12Api().load(&error) && cudaApi().load(&error);
}

std::wstring plugin_directory() {
#if defined(_WIN32)
    // Explicit override wins (CI and commercial deployments), but only when it
    // actually contains a feature DLL.  A stale RTX_NGX_PLUGIN_DIR left over
    // from an older deployment must not hide the copies sitting beside the
    // bridge.
    wchar_t override_dir[32768] = {};
    const DWORD override_length =
        GetEnvironmentVariableW(L"RTX_NGX_PLUGIN_DIR", override_dir, 32768);
    if (override_length > 0 && override_length < 32768) {
        const std::wstring override_path(override_dir, override_length);
        for (const wchar_t* name : {L"nvngx_vsr.dll", L"nvngx_truehdr.dll"}) {
            const std::wstring candidate = override_path + L"\\" + name;
            const DWORD attributes = GetFileAttributesW(candidate.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                return override_path;
            }
        }
    }
    // Otherwise the bridge's own directory when the feature DLLs sit beside
    // it, or <bridge dir>/plugins/nvidia_rtx for a bridge at the app root.  The
    // module handle comes from the address of a function in this translation
    // unit, which is the documented way for a DLL to locate itself.
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&plugin_directory), &module) == 0) {
        return L"plugins\\nvidia_rtx";
    }
    wchar_t module_path[32768] = {};
    const DWORD module_length = GetModuleFileNameW(module, module_path, 32768);
    std::wstring path(module_length > 0 && module_length < 32768
                          ? std::wstring(module_path, module_length)
                          : std::wstring());
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path.resize(slash);
    }
    // Canonical staging, install and <app>/plugins/nvidia_rtx layouts keep the
    // feature DLLs in the bridge's own directory; only a bridge placed at the
    // app root uses the nested plugins/nvidia_rtx directory.
    auto present = [&path](const wchar_t* name) {
        const std::wstring candidate = path + L"\\" + name;
        const DWORD attributes = GetFileAttributesW(candidate.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    };
    if (present(L"nvngx_vsr.dll") || present(L"nvngx_truehdr.dll")) {
        return path;
    }
    return path + L"\\plugins\\nvidia_rtx";
#else
    return std::wstring();
#endif
}

void* load_plugin_library(const wchar_t* name) {
#if defined(_WIN32)
    if (name == nullptr) {
        return nullptr;
    }
    const std::wstring path = plugin_directory() + L"\\" + name;
    return LoadLibraryExW(path.c_str(), nullptr,
                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
    (void)name;
    return nullptr;
#endif
}

RtxResult run_interop_selftest(uint32_t width, uint32_t height, char* detail,
                               size_t detail_capacity) {
    if (detail == nullptr || detail_capacity == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    detail[0] = '\0';
    trace("selftest: enter");
    RtxNgxBridgeConfig config{};
    config.input_width = static_cast<int32_t>(width);
    config.input_height = static_cast<int32_t>(height);
    config.output_width = static_cast<int32_t>(width);
    config.output_height = static_cast<int32_t>(height);
    config.vsr_quality = 1;

    D3D12Interop interop;
    std::string error;
    // Load DXGI before touching CUDA: DXGIDeclareAdapterRemovalSupport is only
    // valid until cuInit() sets up DXGI state in the process (observed on
    // driver 617.14: a later call returns DXGI_ERROR_INVALID_CALL).
    if (!d3d12Api().load(&error)) {
        std::snprintf(detail, detail_capacity, "%s", error.c_str());
        return RTX_ERR_DLL_MISSING;
    }
    // The selftest does not need the NGX config; reuse the same code path with
    // the CUDA device's LUID so the adapter match is exercised.
    trace("selftest: cuda context");
    if (!ensureCudaContext(&error)) {
        std::snprintf(detail, detail_capacity, "%s", error.c_str());
        return RTX_ERR_DLL_MISSING;
    }
    trace("selftest: cuda context ready");
    CUdevice device = 0;
    char luid[8] = {};
    unsigned int node_mask = 0;
    HMODULE cuda_module = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (cuda_module == nullptr) {
        std::snprintf(detail, detail_capacity, "nvcuda.dll missing");
        return RTX_ERR_DLL_MISSING;
    }
    auto get_luid = reinterpret_cast<CUresult (*)(char*, unsigned int*, CUdevice)>(
        GetProcAddress(cuda_module, "cuDeviceGetLuid"));
    auto get_device = reinterpret_cast<CUresult (*)(CUdevice*, int)>(
        GetProcAddress(cuda_module, "cuDeviceGet"));
    trace("selftest: luid query");
    if (get_luid == nullptr || get_device == nullptr || get_device(&device, 0) != CUDA_SUCCESS) {
        FreeLibrary(cuda_module);
        std::snprintf(detail, detail_capacity, "cannot query the CUDA LUID");
        return RTX_ERR_INTERNAL;
    }
    if (get_luid(luid, &node_mask, device) != CUDA_SUCCESS) {
        FreeLibrary(cuda_module);
        std::snprintf(detail, detail_capacity, "cuDeviceGetLuid failed");
        return RTX_ERR_INTERNAL;
    }
    FreeLibrary(cuda_module);
    std::memcpy(config.luid, luid, 8);
    config.luid_node_mask = node_mask;

    if (!interop.create(config, &error)) {
        std::snprintf(detail, detail_capacity, "%s", error.c_str());
        return RTX_ERR_NGX;
    }

    // W6 anti-TDR mechanisms on the live device.
    if (interop.pack_stream() == nullptr) {
        std::snprintf(detail, detail_capacity,
                      "the high-priority pack stream was not created");
        return RTX_ERR_INTERNAL;
    }
    {
        // Two unanswered queue signals must never report a depth above the
        // anti-TDR ceiling, and the queue must drain within the frame budget.
        const uint64_t first = interop.advance_timeline();
        const uint64_t second = interop.advance_timeline();
        if (!interop.queue_signal_fence(first, &error) ||
            !interop.queue_signal_fence(second, &error)) {
            std::snprintf(detail, detail_capacity, "queue signal: %s", error.c_str());
            return RTX_ERR_INTERNAL;
        }
        if (interop.queue_depth() > 2) {
            std::snprintf(detail, detail_capacity,
                          "D3D12 queue depth exceeded 2 after two submissions");
            return RTX_ERR_INTERNAL;
        }
        if (!interop.wait_for_queue_depth(2, std::chrono::milliseconds(2000), &error)) {
            std::snprintf(detail, detail_capacity, "queue depth wait: %s", error.c_str());
            return RTX_ERR_INTERNAL;
        }
    }

    // 1. D3D12 writes a known gradient into the shared RGBA16F texture.
    const size_t pixels = static_cast<size_t>(width) * height;
    std::vector<uint16_t> gradient(pixels * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t index = (static_cast<size_t>(y) * width + x) * 4;
            gradient[index + 0] = static_cast<uint16_t>(x * 256u);
            gradient[index + 1] = static_cast<uint16_t>(y * 256u);
            gradient[index + 2] = static_cast<uint16_t>((x + y) * 128u);
            gradient[index + 3] = 0x3C00;  // 1.0h
        }
    }
    trace("selftest: upload");
    if (!interop.upload(interop.output(), gradient.data(), gradient.size() * sizeof(uint16_t),
                        &error)) {
        std::snprintf(detail, detail_capacity, "upload: %s", error.c_str());
        return RTX_ERR_INTERNAL;
    }
    // flush_queue() signalled this timeline value and the CPU confirmed it;
    // the semaphore wait below is the GPU-side form of the same ordering.
    const uint64_t upload_value = interop.timeline_value();

    // 2. CUDA reads the same texture through the imported surface.
    std::vector<uint16_t> cuda_view(pixels * 4);
    CudaApi& cuda = cudaApi();
    const size_t row_bytes = static_cast<size_t>(width) * 8;
    CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_ARRAY;
    copy.srcArray = static_cast<CUarray>(interop.output().array);
    copy.dstMemoryType = CU_MEMORYTYPE_HOST;
    copy.dstHost = cuda_view.data();
    copy.dstPitch = row_bytes;
    copy.WidthInBytes = row_bytes;
    copy.Height = height;
    trace("selftest: d3d12->cuda copy");
    std::string bind_error;
    if (!interop.bind_context(&bind_error)) {
        std::snprintf(detail, detail_capacity, "%s", bind_error.c_str());
        return RTX_ERR_INTERNAL;
    }
    // GPU-side wait on the value the D3D12 queue signalled after the upload:
    // this is the "cuWaitExternalSemaphoresAsync" leg of the 5-step hand-off.
    if (interop.semaphore_ready() &&
        !interop.fence_wait_cuda(nullptr, upload_value, &bind_error)) {
        std::snprintf(detail, detail_capacity, "%s", bind_error.c_str());
        return RTX_ERR_INTERNAL;
    }
    // Synchronous read: the host compare below must observe the completed
    // copy, so the read itself never leaves the stream asynchronous.
    CUresult result = cuda.memcpy2d(&copy);
    if (result != CUDA_SUCCESS) {
        std::snprintf(detail, detail_capacity, "D3D12->CUDA copy failed: %s", cudaErrorName(result));
        return RTX_ERR_INTERNAL;
    }
    if (std::memcmp(cuda_view.data(), gradient.data(), gradient.size() * sizeof(uint16_t)) != 0) {
        std::snprintf(detail, detail_capacity,
                      "D3D12 -> CUDA mismatch (tiled layout not resolved)");
        return RTX_ERR_INTERNAL;
    }

    // 3. CUDA writes the texture back, D3D12 reads it.
    for (size_t i = 0; i < cuda_view.size(); ++i) {
        cuda_view[i] = static_cast<uint16_t>(cuda_view[i] ^ 0x0001u);
    }
    CUDA_MEMCPY2D write_back{};
    write_back.srcMemoryType = CU_MEMORYTYPE_HOST;
    write_back.srcHost = cuda_view.data();
    write_back.srcPitch = row_bytes;
    write_back.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    write_back.dstArray = static_cast<CUarray>(interop.output().array);
    write_back.WidthInBytes = row_bytes;
    write_back.Height = height;
    trace("selftest: cuda->d3d12 copy");
    // The write-back must run in the very context that imported the array;
    // re-bind and fail loudly if the driver detached it again.
    if (!interop.bind_context(&bind_error)) {
        std::snprintf(detail, detail_capacity, "%s", bind_error.c_str());
        return RTX_ERR_INTERNAL;
    }
    result = cuda.memcpy2d_async(&write_back, nullptr);
    if (result != CUDA_SUCCESS) {
        std::snprintf(detail, detail_capacity, "CUDA->D3D12 copy failed: %s", cudaErrorName(result));
        return RTX_ERR_INTERNAL;
    }
    // Cross-API ordering (expert hardening item 2): the write is asynchronous
    // on the CUDA stream while the readback below runs on the D3D12 queue.
    // When the driver provides the external-semaphore API the hand-off is
    // GPU-only: the CUDA stream signals the shared fence and the D3D12 queue
    // waits on that value before the readback, so no CPU thread ever blocks
    // on a cross-engine boundary.  Without the API the CPU wait remains as
    // the honest fallback.
    uint64_t signal_value = 0;
    if (interop.semaphore_ready()) {
        // Draw the signal value from the shared timeline so it can never
        // collide with a later queue signal: a collision would satisfy the
        // queue's own CPU wait early and race the readback.
        signal_value = interop.advance_timeline();
        if (!interop.fence_signal_cuda(nullptr, signal_value, &bind_error)) {
            std::snprintf(detail, detail_capacity, "%s", bind_error.c_str());
            return RTX_ERR_INTERNAL;
        }
        trace("selftest: cuda signalled the shared fence");
    } else if (cuda.stream_synchronize != nullptr) {
        result = cuda.stream_synchronize(nullptr);
        if (result != CUDA_SUCCESS) {
            std::snprintf(detail, detail_capacity, "CUDA->D3D12 stream sync failed: %s",
                          cudaErrorName(result));
            return RTX_ERR_INTERNAL;
        }
    }
    std::vector<uint16_t> round_trip(pixels * 4);
    trace("selftest: download");
    if (!interop.download(interop.output(), round_trip.data(),
                          round_trip.size() * sizeof(uint16_t), &error, signal_value)) {
        std::snprintf(detail, detail_capacity, "download: %s", error.c_str());
        return RTX_ERR_INTERNAL;
    }
    if (std::memcmp(round_trip.data(), cuda_view.data(), cuda_view.size() * sizeof(uint16_t)) !=
        0) {
        // Report the first differing sample so a cross-engine ordering bug is
        // distinguishable from a tiled-layout problem in the log.
        size_t first = 0;
        while (first < cuda_view.size() && round_trip[first] == cuda_view[first]) {
            ++first;
        }
        std::snprintf(detail, detail_capacity,
                      "CUDA -> D3D12 mismatch (tiled layout not resolved): sample %zu of %zu, "
                      "expected 0x%04X, read back 0x%04X",
                      first, cuda_view.size(),
                      first < cuda_view.size() ? cuda_view[first] : 0,
                      first < round_trip.size() ? round_trip[first] : 0);
        return RTX_ERR_INTERNAL;
    }

    // Expert directive B-2.1: verify the upscale configuration on real
    // hardware.  A second short-lived instance must allocate (and cleanly
    // release) the internal 4K SDR mid texture that the VSR -> TrueHDR chain
    // needs; the round trip above runs at equal geometry and does not.
    {
        RtxNgxBridgeConfig upscale{};
        upscale.input_width = static_cast<int32_t>(width);
        upscale.input_height = static_cast<int32_t>(height);
        upscale.output_width = static_cast<int32_t>(width) * 2;
        upscale.output_height = static_cast<int32_t>(height) * 2;
        upscale.vsr_quality = 1;
        std::memcpy(upscale.luid, config.luid, sizeof(upscale.luid));
        upscale.luid_node_mask = config.luid_node_mask;
        D3D12Interop upscaled;
        std::string upscale_error;
        if (!upscaled.create(upscale, &upscale_error)) {
            std::snprintf(detail, detail_capacity, "upscale interop create failed: %s",
                          upscale_error.c_str());
            return RTX_ERR_INTERNAL;
        }
        if (upscaled.mid().resource == nullptr || upscaled.mid().allocation_size == 0) {
            std::snprintf(detail, detail_capacity,
                          "upscale interop did not allocate the 4K SDR mid texture");
            return RTX_ERR_INTERNAL;
        }
        trace("selftest: upscale mid texture verified");
    }

    std::snprintf(detail, detail_capacity,
                  "round trip OK: %ux%u RGBA16F, D3D12 -> CUDA -> D3D12; queue depth cap 2, "
                  "pack stream priority %s, adapter removal %s",
                  width, height, interop.packStreamHighPriority() ? "high" : "default",
                  interop.adapterRemovalDeclared() ? "declared" : "not declared");
    return RTX_OK;
}

}  // namespace rtx_ngx
