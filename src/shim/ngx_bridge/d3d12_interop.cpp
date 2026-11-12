/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Headless D3D12 <-> CUDA interop implementation.  See d3d12_interop.h.
 */

#include "d3d12_interop.h"

// Headless Direct3D 12 device creation, timeline fence sharing, and CUDA external memory interop.
// Enables zero-copy texture exchange between Direct3D 12 (NGX VSR/TrueHDR) and CUDA.
// Requires driver-level external resource sharing across compatible adapters.

#include "ffnvcodec/dynlink_cuda.h"

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

namespace rtx_ngx {
namespace {

// Opt-in bring-up trace (RTX_BRIDGE_TRACE=1).  D3D12/CUDA interop failures are
// otherwise indistinguishable from a silent crash on a new driver.
bool traceEnabled() { return false; }

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
const GUID kIidFactory4 = {0x1bc6ea02, 0xef36, 0x464f, {0xbf, 0x0c, 0x21, 0xca, 0x39, 0xe5, 0x16, 0x8a}};
const GUID kIidDevice = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
const GUID kIidCommandQueue = {0x0ec870a6, 0x5d7e, 0x4c22, {0x8c, 0xfc, 0x5b, 0xaa, 0xe0, 0x76, 0x16, 0xed}};
const GUID kIidResource = {0x696442be, 0xa72e, 0x4059, {0xbc, 0x79, 0x5b, 0x5c, 0x98, 0x04, 0x0f, 0xad}};
const GUID kIidCommandAllocator = {0x6102dee4, 0xaf59, 0x4b09, {0xb9, 0x99, 0xb4, 0x4d, 0x73, 0xf0, 0x9b, 0x24}};
const GUID kIidGraphicsCommandList = {0x5b160d0f, 0xac1b, 0x4185, {0x8b, 0xa8, 0xb3, 0xae, 0x42, 0xa5, 0xa4, 0x55}};
const GUID kIidFence = {0x0a753dcf, 0xc4d8, 0x4b91, {0xad, 0xf6, 0xbe, 0x5a, 0x60, 0xd9, 0x5a, 0x76}};

constexpr unsigned kExternalMemoryDedicated = 0x1;

struct CUDA_MEMCPY3D {
    unsigned int srcXInBytes, srcY, srcZ, srcLOD;
    CUmemorytype srcMemoryType;
    const void* srcHost;
    CUdeviceptr srcDevice;
    CUarray srcArray;
    unsigned int srcPitch, dstXInBytes, dstY, dstZ, dstLOD;
    CUmemorytype dstMemoryType;
    void* dstHost;
    CUdeviceptr dstDevice;
    CUarray dstArray;
    unsigned int dstPitch, WidthInBytes, Height, Depth;
};

using CUsurfObject = unsigned long long;
using PFN_cuSurfObjectCreate = CUresult (*)(CUsurfObject*, const CUDA_RESOURCE_DESC*);
using PFN_cuSurfObjectDestroy = CUresult (*)(CUsurfObject);

template <typename T>
T* as(void* pointer) { return static_cast<T*>(pointer); }

struct D3D12Api {
    HRESULT(WINAPI* create_device)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**) = nullptr;
    HRESULT(WINAPI* create_factory)(REFIID, void**) = nullptr;
    HRESULT(WINAPI* declare_adapter_removal_support)() = nullptr;
    bool removal_attempted = false;
    bool removal_declared = false;
    HRESULT removal_result = S_OK;

    bool load(std::string* error) {
        HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE dxgi = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!d3d12 || !dxgi) {
            if (error) *error = "d3d12.dll or dxgi.dll is not available";
            return false;
        }
        create_device = reinterpret_cast<decltype(create_device)>(GetProcAddress(d3d12, "D3D12CreateDevice"));
        create_factory = reinterpret_cast<decltype(create_factory)>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
        declare_adapter_removal_support = reinterpret_cast<decltype(declare_adapter_removal_support)>(
            GetProcAddress(dxgi, "DXGIDeclareAdapterRemovalSupport"));
        if (!create_device || !create_factory) {
            if (error) *error = "D3D12CreateDevice/CreateDXGIFactory1 exports are missing";
            return false;
        }
        if (!removal_attempted) {
            removal_attempted = true;
            if (declare_adapter_removal_support) {
                removal_result = declare_adapter_removal_support();
                removal_declared = SUCCEEDED(removal_result);
            } else {
                removal_result = static_cast<HRESULT>(0x80004002L);
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
    CUresult (*import_external_memory)(CUexternalMemory*, const CUDA_EXTERNAL_MEMORY_HANDLE_DESC*) = nullptr;
    CUresult (*get_mapped_mipmapped_array)(CUmipmappedArray*, CUexternalMemory, const CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC*) = nullptr;
    CUresult (*mipmapped_array_get_level)(CUarray*, CUmipmappedArray, unsigned int) = nullptr;
    PFN_cuSurfObjectCreate surf_object_create = nullptr;
    PFN_cuSurfObjectDestroy surf_object_destroy = nullptr;
    CUresult (*destroy_external_memory)(CUexternalMemory) = nullptr;
    CUresult (*mipmapped_array_destroy)(CUmipmappedArray) = nullptr;
    CUresult (*memcpy2d)(const CUDA_MEMCPY2D*) = nullptr;
    CUresult (*memcpy2d_async)(const CUDA_MEMCPY2D*, CUstream) = nullptr;
    CUresult (*stream_synchronize)(CUstream) = nullptr;
    CUresult (*get_error_name)(CUresult, const char**) = nullptr;
    using CUexternalSemaphore = void*;
    CUresult (*import_external_semaphore)(CUexternalSemaphore*, const CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC*) = nullptr;
    CUresult (*signal_external_semaphores_async)(const CUexternalSemaphore*, const CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS*, unsigned int, CUstream) = nullptr;
    CUresult (*wait_external_semaphores_async)(const CUexternalSemaphore*, const CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS*, unsigned int, CUstream) = nullptr;
    CUresult (*destroy_external_semaphore)(CUexternalSemaphore) = nullptr;
    CUresult (*stream_create_with_priority)(CUstream*, unsigned int, int) = nullptr;
    CUresult (*stream_destroy)(CUstream) = nullptr;
    CUresult (*ctx_get_stream_priority_range)(int*, int*) = nullptr;
    CUresult (*mem_alloc_pitch)(CUdeviceptr*, size_t*, size_t, size_t, unsigned int) = nullptr;
    CUresult (*mem_free)(CUdeviceptr) = nullptr;
    CUresult (*event_create)(CUevent*, unsigned int) = nullptr;
    CUresult (*event_record)(CUevent, CUstream) = nullptr;
    CUresult (*event_elapsed)(float*, CUevent, CUevent) = nullptr;
    CUresult (*event_destroy)(CUevent) = nullptr;
    CUresult (*memset_d2d16)(CUdeviceptr, size_t, unsigned short, size_t, size_t) = nullptr;
    CUresult (*memset_d16)(CUdeviceptr, unsigned short, size_t) = nullptr;
    CUresult (*memset_d32_async)(CUdeviceptr, unsigned int, size_t, CUstream) = nullptr;
    CUresult (*array3d_create)(CUarray*, const CUDA_ARRAY3D_DESCRIPTOR*) = nullptr;
    CUresult (*array_destroy)(CUarray) = nullptr;
    using CUtexObject = unsigned long long;
    CUresult (*tex_object_create)(CUtexObject*, const CUDA_RESOURCE_DESC*, const CUDA_TEXTURE_DESC*, const CUDA_RESOURCE_VIEW_DESC*) = nullptr;
    CUresult (*tex_object_destroy)(CUtexObject) = nullptr;
    CUresult (*memcpy3d)(const CUDA_MEMCPY3D*) = nullptr;

    bool load(std::string* error) {
        HMODULE module = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) {
            if (error) *error = "nvcuda.dll is not available";
            return false;
        }
        auto sym = [module](const char* name) { return GetProcAddress(module, name); };
#define LOAD_CUDA_SYM(f, n) f = reinterpret_cast<decltype(f)>(sym(n))
        LOAD_CUDA_SYM(init, "cuInit");
        LOAD_CUDA_SYM(device_get, "cuDeviceGet");
        LOAD_CUDA_SYM(ctx_get_current, "cuCtxGetCurrent");
        LOAD_CUDA_SYM(ctx_set_current, "cuCtxSetCurrent");
        LOAD_CUDA_SYM(primary_ctx_retain, "cuDevicePrimaryCtxRetain");
        LOAD_CUDA_SYM(primary_ctx_set_flags, "cuDevicePrimaryCtxSetFlags");
        LOAD_CUDA_SYM(import_external_memory, "cuImportExternalMemory");
        LOAD_CUDA_SYM(get_mapped_mipmapped_array, "cuExternalMemoryGetMappedMipmappedArray");
        LOAD_CUDA_SYM(mipmapped_array_get_level, "cuMipmappedArrayGetLevel");
        LOAD_CUDA_SYM(surf_object_create, "cuSurfObjectCreate");
        LOAD_CUDA_SYM(surf_object_destroy, "cuSurfObjectDestroy");
        LOAD_CUDA_SYM(destroy_external_memory, "cuDestroyExternalMemory");
        LOAD_CUDA_SYM(mipmapped_array_destroy, "cuMipmappedArrayDestroy");
        LOAD_CUDA_SYM(memcpy2d, "cuMemcpy2D_v2");
        if (!memcpy2d) LOAD_CUDA_SYM(memcpy2d, "cuMemcpy2D");
        LOAD_CUDA_SYM(memcpy2d_async, "cuMemcpy2DAsync_v2");
        if (!memcpy2d_async) LOAD_CUDA_SYM(memcpy2d_async, "cuMemcpy2DAsync");
        LOAD_CUDA_SYM(stream_synchronize, "cuStreamSynchronize");
        LOAD_CUDA_SYM(get_error_name, "cuGetErrorName");
        LOAD_CUDA_SYM(import_external_semaphore, "cuImportExternalSemaphore");
        LOAD_CUDA_SYM(signal_external_semaphores_async, "cuSignalExternalSemaphoresAsync");
        LOAD_CUDA_SYM(wait_external_semaphores_async, "cuWaitExternalSemaphoresAsync");
        LOAD_CUDA_SYM(destroy_external_semaphore, "cuDestroyExternalSemaphore");
        LOAD_CUDA_SYM(stream_create_with_priority, "cuStreamCreateWithPriority");
        LOAD_CUDA_SYM(stream_destroy, "cuStreamDestroy");
        LOAD_CUDA_SYM(ctx_get_stream_priority_range, "cuCtxGetStreamPriorityRange");
        LOAD_CUDA_SYM(mem_alloc_pitch, "cuMemAllocPitch_v2");
        if (!mem_alloc_pitch) LOAD_CUDA_SYM(mem_alloc_pitch, "cuMemAllocPitch");
        LOAD_CUDA_SYM(mem_free, "cuMemFree_v2");
        if (!mem_free) LOAD_CUDA_SYM(mem_free, "cuMemFree");
        LOAD_CUDA_SYM(event_create, "cuEventCreate");
        LOAD_CUDA_SYM(event_record, "cuEventRecord");
        LOAD_CUDA_SYM(event_elapsed, "cuEventElapsedTime");
        LOAD_CUDA_SYM(event_destroy, "cuEventDestroy");
        LOAD_CUDA_SYM(memset_d2d16, "cuMemsetD2D16_v2");
        if (!memset_d2d16) LOAD_CUDA_SYM(memset_d2d16, "cuMemsetD2D16");
        LOAD_CUDA_SYM(memset_d16, "cuMemsetD16_v2");
        if (!memset_d16) LOAD_CUDA_SYM(memset_d16, "cuMemsetD16");
        LOAD_CUDA_SYM(memset_d32_async, "cuMemsetD32Async");
        LOAD_CUDA_SYM(array3d_create, "cuArray3DCreate_v2");
        if (!array3d_create) LOAD_CUDA_SYM(array3d_create, "cuArray3DCreate");
        LOAD_CUDA_SYM(array_destroy, "cuArrayDestroy");
        LOAD_CUDA_SYM(tex_object_create, "cuTexObjectCreate");
        LOAD_CUDA_SYM(tex_object_destroy, "cuTexObjectDestroy");
        LOAD_CUDA_SYM(memcpy3d, "cuMemcpy3D_v2");
        if (!memcpy3d) LOAD_CUDA_SYM(memcpy3d, "cuMemcpy3D");
#undef LOAD_CUDA_SYM
        if (!init || !import_external_memory || !get_mapped_mipmapped_array || !surf_object_create || !memcpy2d) {
            if (error) *error = "CUDA driver lacks external-memory API";
            return false;
        }
        return true;
    }
};

D3D12Api& d3d12Api() { static D3D12Api api; return api; }
CudaApi& cudaApi() { static CudaApi api; return api; }

const char* cudaErrorName(CUresult result) {
    const char* name = "CUDA error";
    CudaApi& cuda = cudaApi();
    if (cuda.get_error_name && cuda.get_error_name(result, &name) == CUDA_SUCCESS) return name;
    return name;
}

bool ensureCudaContext(std::string* error) {
    CudaApi& cuda = cudaApi();
    if (!cuda.init && !cuda.load(error)) return false;
    if (!cuda.device_get || !cuda.primary_ctx_retain || !cuda.ctx_set_current) {
        if (error) *error = "CUDA driver lacks context API";
        return false;
    }
    if (cuda.init(0) != CUDA_SUCCESS) {
        if (error) *error = "cuInit failed";
        return false;
    }
    CUcontext current = nullptr;
    if (cuda.ctx_get_current) cuda.ctx_get_current(&current);
    if (current) return true;
    CUdevice device = 0;
    CUcontext primary = nullptr;
    if (cuda.device_get(&device, 0) != CUDA_SUCCESS) {
        if (error) *error = "cannot open CUDA device 0";
        return false;
    }
    if (cuda.primary_ctx_set_flags) {
        cuda.primary_ctx_set_flags(device, CU_CTX_SCHED_BLOCKING_SYNC);
    }
    if (cuda.primary_ctx_retain(&primary, device) != CUDA_SUCCESS) {
        if (error) *error = "cannot retain CUDA primary context";
        return false;
    }
    cuda.ctx_set_current(primary);
    return true;
}

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
    for (const FormatMapping& m : kFormats) if (m.format == format) return &m;
    return nullptr;
}

DXGI_FORMAT toDxgi(int32_t format) {
    const FormatMapping* m = formatMapping(format);
    return m ? m->dxgi : DXGI_FORMAT_UNKNOWN;
}

CUarray_format toCudaFormat(int32_t format) {
    const FormatMapping* m = formatMapping(format);
    return m ? m->cuda : CU_AD_FORMAT_UNSIGNED_INT8;
}

int channelsOf(int32_t format) {
    const FormatMapping* m = formatMapping(format);
    return m ? static_cast<int>(m->channels) : 0;
}

uint32_t channelBytes(CUarray_format format) {
    switch (format) {
        case CU_AD_FORMAT_HALF: return 2;
        case CU_AD_FORMAT_UNSIGNED_INT32: return 4;
        case CU_AD_FORMAT_UNSIGNED_INT8: return 1;
        default: return 0;
    }
}

uint32_t bytesPerPixel(int32_t format) {
    const FormatMapping* m = formatMapping(format);
    return m ? m->bytes : 0;
}

bool waitForFence(ID3D12Fence* fence, uint64_t value) {
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) return false;
    const bool ok = SUCCEEDED(fence->SetEventOnCompletion(value, event)) &&
                    WaitForSingleObject(event, 10000) == WAIT_OBJECT_0;
    CloseHandle(event);
    return ok;
}

}  // namespace

D3D12Interop::~D3D12Interop() { release(); }

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

bool D3D12Interop::clear_p010_black(uint64_t device_ptr, size_t pitch, uint32_t width,
                                    uint32_t height, std::string* error) {
    if (device_ptr == 0) return true;
    CudaApi& cuda = cudaApi();
    if (!bind_context(error)) return false;
    const uint16_t y_black = 0x1000, uv_neutral = 0x8000;
    if (cuda.memset_d2d16 != nullptr) {
        cuda.memset_d2d16(static_cast<CUdeviceptr>(device_ptr), pitch, y_black, width, height);
        cuda.memset_d2d16(static_cast<CUdeviceptr>(device_ptr + pitch * height), pitch, uv_neutral, width, height / 2);
    } else if (cuda.memset_d16 != nullptr) {
        for (size_t r = 0; r < height; ++r) cuda.memset_d16(static_cast<CUdeviceptr>(device_ptr + r * pitch), y_black, width);
        const uint64_t uv_base = device_ptr + pitch * height;
        for (size_t r = 0; r < height / 2; ++r) cuda.memset_d16(static_cast<CUdeviceptr>(uv_base + r * pitch), uv_neutral, width);
    }
    return true;
}

bool D3D12Interop::alloc_pitched(uint64_t* device_ptr, size_t* pitch, size_t width_bytes, size_t rows, std::string* error) {
    if (!device_ptr || !pitch) return false;
    CudaApi& cuda = cudaApi();
    if (!cuda.mem_alloc_pitch || !bind_context(error)) return false;
    CUdeviceptr p = 0; size_t r = 0;
    CUresult res = cuda.mem_alloc_pitch(&p, &r, width_bytes, rows, 16);
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    *device_ptr = static_cast<uint64_t>(p); *pitch = r;
    return true;
}

bool D3D12Interop::free_device(uint64_t device_ptr, std::string* error) {
    if (!device_ptr) return true;
    CudaApi& cuda = cudaApi();
    if (!cuda.mem_free || !bind_context(error)) return false;
    CUresult res = cuda.mem_free(static_cast<CUdeviceptr>(device_ptr));
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    return true;
}

bool D3D12Interop::memset_d32_async(uint64_t device_ptr, unsigned int value, size_t count, void* stream, std::string* error) {
    if (!device_ptr || count == 0) return false;
    CudaApi& cuda = cudaApi();
    if (!cuda.memset_d32_async || !bind_context(error)) return false;
    CUresult res = cuda.memset_d32_async(static_cast<CUdeviceptr>(device_ptr), value, count, static_cast<CUstream>(stream));
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    return true;
}

bool D3D12Interop::memcpy_dtoh(void* dst, uint64_t src, size_t bytes, std::string* error) {
    if (!dst || !src || bytes == 0) return false;
    CudaApi& cuda = cudaApi();
    if (!cuda.memcpy2d || !bind_context(error)) return false;
    CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copy.srcDevice = static_cast<CUdeviceptr>(src);
    copy.srcPitch = bytes;
    copy.dstMemoryType = CU_MEMORYTYPE_HOST;
    copy.dstHost = dst;
    copy.dstPitch = bytes;
    copy.WidthInBytes = bytes;
    copy.Height = 1;
    CUresult res = cuda.memcpy2d(&copy);
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    return true;
}

bool D3D12Interop::memcpy_htod(uint64_t dst, const void* src, size_t bytes, std::string* error) {
    if (!dst || !src || bytes == 0) return false;
    CudaApi& cuda = cudaApi();
    if (!cuda.memcpy2d || !bind_context(error)) return false;
    CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_HOST;
    copy.srcHost = src;
    copy.srcPitch = bytes;
    copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    copy.dstDevice = static_cast<CUdeviceptr>(dst);
    copy.dstPitch = bytes;
    copy.WidthInBytes = bytes;
    copy.Height = 1;
    CUresult res = cuda.memcpy2d(&copy);
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    return true;
}

bool D3D12Interop::create_lut_texture(const uint16_t* lut_half4_data, int32_t grid_size,
                                      void** texture_object, void** array_out,
                                      std::string* error) {
    if (texture_object == nullptr || array_out == nullptr) {
        *error = "create_lut_texture requires output pointers";
        return false;
    }
    *texture_object = nullptr;
    *array_out = nullptr;
    CudaApi& cuda = cudaApi();
    if (cuda.array3d_create == nullptr || cuda.tex_object_create == nullptr ||
        cuda.array_destroy == nullptr || cuda.tex_object_destroy == nullptr ||
        cuda.memcpy3d == nullptr) {
        *error = "the CUDA driver lacks the 3D texture-object API";
        return false;
    }
    if (!bind_context(error)) {
        return false;
    }
    CUDA_ARRAY3D_DESCRIPTOR array_desc{};
    array_desc.Width = static_cast<size_t>(grid_size);
    array_desc.Height = static_cast<size_t>(grid_size);
    array_desc.Depth = static_cast<size_t>(grid_size);
    array_desc.Format = CU_AD_FORMAT_HALF;
    array_desc.NumChannels = 4;
    array_desc.Flags = 0;
    CUarray array = nullptr;
    const CUresult created = cuda.array3d_create(&array, &array_desc);
    if (created != CUDA_SUCCESS || array == nullptr) {
        *error = std::string("cuArray3DCreate failed: ") + cudaErrorName(created);
        return false;
    }
    // Upload the whole N^3 table with one cuMemcpy3D call (the dynlink header
    // predates the 3D entry point, so the struct and symbol are declared
    // locally against the stable driver ABI).
    CUDA_MEMCPY3D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_HOST;
    copy.srcHost = lut_half4_data;
    copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    copy.dstArray = array;
    copy.WidthInBytes = static_cast<unsigned>(grid_size) * 4u * sizeof(uint16_t);
    copy.Height = static_cast<unsigned>(grid_size);
    copy.Depth = static_cast<unsigned>(grid_size);
    const CUresult copied = cuda.memcpy3d(&copy);
    if (copied != CUDA_SUCCESS) {
        *error = std::string("cuMemcpy3D (LUT H2D) failed: ") + cudaErrorName(copied);
        (void)cuda.array_destroy(array);
        return false;
    }
    CUDA_RESOURCE_DESC resource_desc{};
    resource_desc.resType = CU_RESOURCE_TYPE_ARRAY;
    resource_desc.res.array.hArray = array;
    CUDA_TEXTURE_DESC texture_desc{};
    texture_desc.addressMode[0] = CU_TR_ADDRESS_MODE_CLAMP;
    texture_desc.addressMode[1] = CU_TR_ADDRESS_MODE_CLAMP;
    texture_desc.addressMode[2] = CU_TR_ADDRESS_MODE_CLAMP;
    texture_desc.filterMode = CU_TR_FILTER_MODE_LINEAR;
    // The dynlink header only declares CU_TRSF_READ_AS_INTEGER (1); the
    // normalized-coordinate flag is 2 in the stable driver ABI.
    texture_desc.flags = 2u /* CU_TRSF_NORMALIZED_COORDINATES */;
    CUtexObject tex_object = 0;
    const CUresult tex_created =
        cuda.tex_object_create(&tex_object, &resource_desc, &texture_desc, nullptr);
    if (tex_created != CUDA_SUCCESS || tex_object == 0) {
        *error = std::string("cuTexObjectCreate failed: ") + cudaErrorName(tex_created);
        (void)cuda.array_destroy(array);
        return false;
    }
    *texture_object = reinterpret_cast<void*>(static_cast<uintptr_t>(tex_object));
    *array_out = array;
    return true;
}

void D3D12Interop::destroy_lut_texture(void** texture_object, void** array) noexcept {
    CudaApi& cuda = cudaApi();
    if (texture_object && *texture_object) {
        if (cuda.tex_object_destroy) cuda.tex_object_destroy(static_cast<CudaApi::CUtexObject>(reinterpret_cast<uintptr_t>(*texture_object)));
        *texture_object = nullptr;
    }
    if (array && *array) {
        if (cuda.array_destroy) cuda.array_destroy(static_cast<CUarray>(*array));
        *array = nullptr;
    }
}

bool D3D12Interop::event_create(void** event, std::string* error) {
    if (!event) return false;
    *event = nullptr;
    CudaApi& cuda = cudaApi();
    if (!cuda.event_create || !bind_context(error)) return false;
    CUevent handle = nullptr;
    CUresult res = cuda.event_create(&handle, 0);
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    *event = handle;
    return true;
}

void D3D12Interop::event_destroy(void* event) noexcept {
    if (!event) return;
    CudaApi& cuda = cudaApi();
    if (cuda.event_destroy) cuda.event_destroy(static_cast<CUevent>(event));
}

bool D3D12Interop::event_record(void* event, void* stream, std::string* error) {
    if (!event) return false;
    CudaApi& cuda = cudaApi();
    if (!cuda.event_record || !bind_context(error)) return false;
    CUresult res = cuda.event_record(static_cast<CUevent>(event), static_cast<CUstream>(stream));
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    return true;
}

bool D3D12Interop::event_elapsed(double* ms, void* start, void* end, std::string* error) {
    if (!ms || !start || !end) return false;
    *ms = 0.0;
    CudaApi& cuda = cudaApi();
    if (!cuda.event_elapsed) return false;
    float val = 0.0f;
    CUresult res = cuda.event_elapsed(&val, static_cast<CUevent>(start), static_cast<CUevent>(end));
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    *ms = static_cast<double>(val);
    return true;
}

bool D3D12Interop::stream_synchronize(void* stream, std::string* error) {
    CudaApi& cuda = cudaApi();
    if (!cuda.stream_synchronize || !bind_context(error)) return false;
    CUresult res = cuda.stream_synchronize(static_cast<CUstream>(stream));
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
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

void D3D12Interop::release() {
    CudaApi& cuda = cudaApi();
    std::string ignored_error;
    if (cuda_context_ != nullptr) (void)bind_context(&ignored_error);
    if (mid_4k_.resource != nullptr) { as<ID3D12Resource>(mid_4k_.resource)->Release(); mid_4k_.resource = nullptr; }
    for (InteropTexture* texture : {&input_, &output_}) {
        if (texture->surface != 0 && cuda.surf_object_destroy) { cuda.surf_object_destroy(texture->surface); texture->surface = 0; }
        if (texture->mip_array && cuda.mipmapped_array_destroy) { cuda.mipmapped_array_destroy(static_cast<CUmipmappedArray>(texture->mip_array)); texture->mip_array = nullptr; }
        if (texture->external_memory && cuda.destroy_external_memory) { cuda.destroy_external_memory(static_cast<CUexternalMemory>(texture->external_memory)); texture->external_memory = nullptr; }
        if (texture->resource) { as<ID3D12Resource>(texture->resource)->Release(); texture->resource = nullptr; }
        if (texture->shared_handle) { CloseHandle(static_cast<HANDLE>(texture->shared_handle)); texture->shared_handle = nullptr; }
    }
    if (external_semaphore_ && cuda.destroy_external_semaphore) { (void)cuda.destroy_external_semaphore(external_semaphore_); external_semaphore_ = nullptr; }
    if (pack_stream_ && cuda.stream_destroy) { (void)cuda.stream_destroy(static_cast<CUstream>(pack_stream_)); pack_stream_ = nullptr; }
    if (fence_shared_handle_) { CloseHandle(static_cast<HANDLE>(fence_shared_handle_)); fence_shared_handle_ = nullptr; }
    if (fence_) { as<ID3D12Fence>(fence_)->Release(); fence_ = nullptr; }
    if (queue_) { as<ID3D12CommandQueue>(queue_)->Release(); queue_ = nullptr; }
    if (device_) { as<ID3D12Device>(device_)->Release(); device_ = nullptr; }
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

}  // namespace rtx_ngx
