/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Headless D3D12 <-> CUDA interop implementation.  See d3d12_interop.h.
 * Phase 12 (Wave 12.2-B1): eliminated D3D12Api, CudaApi, CUDA_MEMCPY3D,
 * InteropTexture, PingPongSlot, and SharedBufferSlot.
 */

#include "d3d12_interop.h"
#include "ffnvcodec/dynlink_cuda.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <format>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tuple>
#include <type_traits>
#include <vector>

namespace rtx_ngx {
namespace {

const GUID kIidFactory4 = {0x1bc6ea02, 0xef36, 0x464f, {0xbf, 0x0c, 0x21, 0xca, 0x39, 0xe5, 0x16, 0x8a}};
const GUID kIidDevice = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
const GUID kIidCommandQueue = {0x0ec870a6, 0x5d7e, 0x4c22, {0x8c, 0xfc, 0x5b, 0xaa, 0xe0, 0x76, 0x16, 0xed}};
const GUID kIidResource = {0x696442be, 0xa72e, 0x4059, {0xbc, 0x79, 0x5b, 0x5c, 0x98, 0x04, 0x0f, 0xad}};
const GUID kIidFence = {0x0a753dcf, 0xc4d8, 0x4b91, {0xad, 0xf6, 0xbe, 0x5a, 0x60, 0xd9, 0x5a, 0x76}};

constexpr unsigned kExternalMemoryDedicated = 0x1;

/* Local C-ABI layout mirror for cuMemcpy3D_v2 (absent from older ffnvcodec dynlink headers). */
typedef struct {
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
} LocalCudaMemcpy3D;

using CUsurfObject = unsigned long long;
using CUtexObject = unsigned long long;
using CUexternalSemaphore = void*;

template <typename T>
T* as(void* pointer) { return static_cast<T*>(pointer); }

/* D3D12/DXGI dynamic dispatch state (file-scope, zero wrapper struct). */
HRESULT(WINAPI* g_d3d12_create_device)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**) = nullptr;
HRESULT(WINAPI* g_dxgi_create_factory)(REFIID, void**) = nullptr;
HRESULT(WINAPI* g_dxgi_declare_removal)() = nullptr;
bool g_d3d12_removal_attempted = false;
bool g_d3d12_removal_declared = false;

bool loadD3D12Api(std::string* error) {
    HMODULE d3d12 = LoadLibraryExW(L"d3d12.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE dxgi = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!d3d12 || !dxgi) {
        if (error) *error = "d3d12.dll or dxgi.dll is not available";
        return false;
    }
    g_d3d12_create_device = reinterpret_cast<decltype(g_d3d12_create_device)>(GetProcAddress(d3d12, "D3D12CreateDevice"));
    g_dxgi_create_factory = reinterpret_cast<decltype(g_dxgi_create_factory)>(GetProcAddress(dxgi, "CreateDXGIFactory1"));
    g_dxgi_declare_removal = reinterpret_cast<decltype(g_dxgi_declare_removal)>(GetProcAddress(dxgi, "DXGIDeclareAdapterRemovalSupport"));
    if (!g_d3d12_create_device || !g_dxgi_create_factory) {
        if (error) *error = "D3D12CreateDevice/CreateDXGIFactory1 exports are missing";
        return false;
    }
    if (!g_d3d12_removal_attempted) {
        g_d3d12_removal_attempted = true;
        if (g_dxgi_declare_removal) {
            g_d3d12_removal_declared = SUCCEEDED(g_dxgi_declare_removal());
        }
    }
    return true;
}

/* CUDA Driver dynamic dispatch state (file-scope, zero wrapper struct). */
CUresult (*g_cu_init)(unsigned int) = nullptr;
CUresult (*g_cu_device_get)(CUdevice*, int) = nullptr;
CUresult (*g_cu_ctx_get_current)(CUcontext*) = nullptr;
CUresult (*g_cu_ctx_set_current)(CUcontext) = nullptr;
CUresult (*g_cu_primary_ctx_retain)(CUcontext*, CUdevice) = nullptr;
CUresult (*g_cu_primary_ctx_set_flags)(CUdevice, unsigned int) = nullptr;
CUresult (*g_cu_import_ext_mem)(CUexternalMemory*, const CUDA_EXTERNAL_MEMORY_HANDLE_DESC*) = nullptr;
CUresult (*g_cu_get_mapped_mip)(CUmipmappedArray*, CUexternalMemory, const CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC*) = nullptr;
CUresult (*g_cu_get_mapped_buf)(CUdeviceptr*, CUexternalMemory, const CUDA_EXTERNAL_MEMORY_BUFFER_DESC*) = nullptr;
CUresult (*g_cu_mip_get_level)(CUarray*, CUmipmappedArray, unsigned int) = nullptr;
CUresult (*g_cu_surf_create)(CUsurfObject*, const CUDA_RESOURCE_DESC*) = nullptr;
CUresult (*g_cu_surf_destroy)(CUsurfObject) = nullptr;
CUresult (*g_cu_destroy_ext_mem)(CUexternalMemory) = nullptr;
CUresult (*g_cu_mip_destroy)(CUmipmappedArray) = nullptr;
CUresult (*g_cu_memcpy2d)(const CUDA_MEMCPY2D*) = nullptr;
CUresult (*g_cu_stream_sync)(CUstream) = nullptr;
CUresult (*g_cu_get_error_name)(CUresult, const char**) = nullptr;
CUresult (*g_cu_import_ext_sem)(CUexternalSemaphore*, const CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC*) = nullptr;
CUresult (*g_cu_signal_ext_sem)(const CUexternalSemaphore*, const CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS*, unsigned int, CUstream) = nullptr;
CUresult (*g_cu_wait_ext_sem)(const CUexternalSemaphore*, const CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS*, unsigned int, CUstream) = nullptr;
CUresult (*g_cu_destroy_ext_sem)(CUexternalSemaphore) = nullptr;
CUresult (*g_cu_stream_create_prio)(CUstream*, unsigned int, int) = nullptr;
CUresult (*g_cu_stream_destroy)(CUstream) = nullptr;
CUresult (*g_cu_stream_prio_range)(int*, int*) = nullptr;
CUresult (*g_cu_mem_alloc_pitch)(CUdeviceptr*, size_t*, size_t, size_t, unsigned int) = nullptr;
CUresult (*g_cu_mem_free)(CUdeviceptr) = nullptr;
CUresult (*g_cu_event_create)(CUevent*, unsigned int) = nullptr;
CUresult (*g_cu_event_record)(CUevent, CUstream) = nullptr;
CUresult (*g_cu_event_elapsed)(float*, CUevent, CUevent) = nullptr;
CUresult (*g_cu_event_destroy)(CUevent) = nullptr;
CUresult (*g_cu_memset_d2d16)(CUdeviceptr, size_t, unsigned short, size_t, size_t) = nullptr;
CUresult (*g_cu_memset_d16)(CUdeviceptr, unsigned short, size_t) = nullptr;
CUresult (*g_cu_memset_d32_async)(CUdeviceptr, unsigned int, size_t, CUstream) = nullptr;
CUresult (*g_cu_array3d_create)(CUarray*, const CUDA_ARRAY3D_DESCRIPTOR*) = nullptr;
CUresult (*g_cu_array_destroy)(CUarray) = nullptr;
CUresult (*g_cu_tex_create)(CUtexObject*, const CUDA_RESOURCE_DESC*, const CUDA_TEXTURE_DESC*, const CUDA_RESOURCE_VIEW_DESC*) = nullptr;
CUresult (*g_cu_tex_destroy)(CUtexObject) = nullptr;
CUresult (*g_cu_memcpy3d)(const LocalCudaMemcpy3D*) = nullptr;

bool loadCudaApi(std::string* error) {
    HMODULE module = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        if (error) *error = "nvcuda.dll is not available";
        return false;
    }
    auto load = [module](auto& fn, const char* primary, const char* fallback = nullptr) {
        FARPROC p = GetProcAddress(module, primary);
        if (!p && fallback) p = GetProcAddress(module, fallback);
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(p);
    };
    load(g_cu_init, "cuInit");
    load(g_cu_device_get, "cuDeviceGet");
    load(g_cu_ctx_get_current, "cuCtxGetCurrent");
    load(g_cu_ctx_set_current, "cuCtxSetCurrent");
    load(g_cu_primary_ctx_retain, "cuDevicePrimaryCtxRetain");
    load(g_cu_primary_ctx_set_flags, "cuDevicePrimaryCtxSetFlags");
    load(g_cu_import_ext_mem, "cuImportExternalMemory");
    load(g_cu_get_mapped_mip, "cuExternalMemoryGetMappedMipmappedArray");
    load(g_cu_get_mapped_buf, "cuExternalMemoryGetMappedBuffer");
    load(g_cu_mip_get_level, "cuMipmappedArrayGetLevel");
    load(g_cu_surf_create, "cuSurfObjectCreate");
    load(g_cu_surf_destroy, "cuSurfObjectDestroy");
    load(g_cu_destroy_ext_mem, "cuDestroyExternalMemory");
    load(g_cu_mip_destroy, "cuMipmappedArrayDestroy");
    load(g_cu_memcpy2d, "cuMemcpy2D_v2", "cuMemcpy2D");
    load(g_cu_stream_sync, "cuStreamSynchronize");
    load(g_cu_get_error_name, "cuGetErrorName");
    load(g_cu_import_ext_sem, "cuImportExternalSemaphore");
    load(g_cu_signal_ext_sem, "cuSignalExternalSemaphoresAsync");
    load(g_cu_wait_ext_sem, "cuWaitExternalSemaphoresAsync");
    load(g_cu_destroy_ext_sem, "cuDestroyExternalSemaphore");
    load(g_cu_stream_create_prio, "cuStreamCreateWithPriority");
    load(g_cu_stream_destroy, "cuStreamDestroy");
    load(g_cu_stream_prio_range, "cuCtxGetStreamPriorityRange");
    load(g_cu_mem_alloc_pitch, "cuMemAllocPitch_v2", "cuMemAllocPitch");
    load(g_cu_mem_free, "cuMemFree_v2", "cuMemFree");
    load(g_cu_event_create, "cuEventCreate");
    load(g_cu_event_record, "cuEventRecord");
    load(g_cu_event_elapsed, "cuEventElapsedTime");
    load(g_cu_event_destroy, "cuEventDestroy");
    load(g_cu_memset_d2d16, "cuMemsetD2D16_v2", "cuMemsetD2D16");
    load(g_cu_memset_d16, "cuMemsetD16_v2", "cuMemsetD16");
    load(g_cu_memset_d32_async, "cuMemsetD32Async");
    load(g_cu_array3d_create, "cuArray3DCreate_v2", "cuArray3DCreate");
    load(g_cu_array_destroy, "cuArrayDestroy");
    load(g_cu_tex_create, "cuTexObjectCreate");
    load(g_cu_tex_destroy, "cuTexObjectDestroy");
    load(g_cu_memcpy3d, "cuMemcpy3D_v2", "cuMemcpy3D");
    if (!g_cu_init || !g_cu_import_ext_mem || !g_cu_get_mapped_mip || !g_cu_surf_create || !g_cu_memcpy2d) {
        if (error) *error = "CUDA driver lacks external-memory API";
        return false;
    }
    return true;
}

const char* cudaErrorName(CUresult result) {
    const char* name = "CUDA error";
    if (g_cu_get_error_name && g_cu_get_error_name(result, &name) == CUDA_SUCCESS) return name;
    return name;
}

bool ensureCudaContext(std::string* error) {
    if (!g_cu_init && !loadCudaApi(error)) return false;
    if (!g_cu_device_get || !g_cu_primary_ctx_retain || !g_cu_ctx_set_current) {
        if (error) *error = "CUDA driver lacks context API";
        return false;
    }
    if (g_cu_init(0) != CUDA_SUCCESS) {
        if (error) *error = "cuInit failed";
        return false;
    }
    CUcontext current = nullptr;
    if (g_cu_ctx_get_current) g_cu_ctx_get_current(&current);
    if (current) return true;
    CUdevice device = 0;
    CUcontext primary = nullptr;
    if (g_cu_device_get(&device, 0) != CUDA_SUCCESS) {
        if (error) *error = "cannot open CUDA device 0";
        return false;
    }
    if (g_cu_primary_ctx_set_flags) {
        g_cu_primary_ctx_set_flags(device, CU_CTX_SCHED_BLOCKING_SYNC);
    }
    if (g_cu_primary_ctx_retain(&primary, device) != CUDA_SUCCESS) {
        if (error) *error = "cannot retain CUDA primary context";
        return false;
    }
    g_cu_ctx_set_current(primary);
    return true;
}

constexpr std::array<std::tuple<int32_t, DXGI_FORMAT, CUarray_format, unsigned int, uint32_t>, 3> kFormats{{
    {RTX_PIXEL_RGBA16F, DXGI_FORMAT_R16G16B16A16_FLOAT, CU_AD_FORMAT_HALF, 4u, 8u},
    {RTX_PIXEL_R10G10B10A2, DXGI_FORMAT_R10G10B10A2_UNORM, CU_AD_FORMAT_UNSIGNED_INT32, 1u, 4u},
    {RTX_PIXEL_RGB32, DXGI_FORMAT_R8G8B8A8_UNORM, CU_AD_FORMAT_UNSIGNED_INT8, 4u, 4u},
}};

std::tuple<DXGI_FORMAT, CUarray_format, unsigned int, uint32_t> lookupFormat(int32_t format) noexcept {
    for (const auto& [fmt, dxgi, cuda_fmt, channels, bytes] : kFormats) {
        if (fmt == format) return {dxgi, cuda_fmt, channels, bytes};
    }
    return {DXGI_FORMAT_UNKNOWN, CU_AD_FORMAT_UNSIGNED_INT8, 0u, 0u};
}

uint32_t channelBytes(CUarray_format format) noexcept {
    switch (format) {
        case CU_AD_FORMAT_HALF: return 2;
        case CU_AD_FORMAT_UNSIGNED_INT32: return 4;
        case CU_AD_FORMAT_UNSIGNED_INT8: return 1;
        default: return 0;
    }
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

void* D3D12Interop::input_resource() const noexcept {
    const auto& [res, h, surf, ext, mip, arr, w, hh, fmt, sz] = input_;
    (void)h; (void)surf; (void)ext; (void)mip; (void)arr; (void)w; (void)hh; (void)fmt; (void)sz;
    return res;
}
void* D3D12Interop::output_resource() const noexcept {
    const auto& [res, h, surf, ext, mip, arr, w, hh, fmt, sz] = output_;
    (void)h; (void)surf; (void)ext; (void)mip; (void)arr; (void)w; (void)hh; (void)fmt; (void)sz;
    return res;
}
void* D3D12Interop::mid_resource() const noexcept {
    const auto& [res, h, surf, ext, mip, arr, w, hh, fmt, sz] = mid_4k_;
    (void)h; (void)surf; (void)ext; (void)mip; (void)arr; (void)w; (void)hh; (void)fmt; (void)sz;
    return res;
}
uint64_t D3D12Interop::input_surface() const noexcept {
    const auto& [res, h, surf, ext, mip, arr, w, hh, fmt, sz] = input_;
    (void)res; (void)h; (void)ext; (void)mip; (void)arr; (void)w; (void)hh; (void)fmt; (void)sz;
    return surf;
}
uint64_t D3D12Interop::output_surface() const noexcept {
    const auto& [res, h, surf, ext, mip, arr, w, hh, fmt, sz] = output_;
    (void)res; (void)h; (void)ext; (void)mip; (void)arr; (void)w; (void)hh; (void)fmt; (void)sz;
    return surf;
}
void* D3D12Interop::previewRingResource(size_t slot) const noexcept {
    if (slot >= preview_ring_.size()) return nullptr;
    const auto& [res, h, ext, ptr] = preview_ring_[slot];
    (void)h; (void)ext; (void)ptr;
    return res;
}
uint64_t D3D12Interop::previewRingDevicePtr(size_t slot) const noexcept {
    if (slot >= preview_ring_.size()) return 0;
    const auto& [res, h, ext, ptr] = preview_ring_[slot];
    (void)res; (void)h; (void)ext;
    return ptr;
}

bool D3D12Interop::create(const RtxNgxBridgeConfig& config, std::string* error) {
    if (!loadD3D12Api(error) || !loadCudaApi(error)) return false;
    if (!create_device(config.luid, error)) return false;
    if (!ensureCudaContext(error)) return false;
    if (g_cu_ctx_get_current) {
        (void)g_cu_ctx_get_current(reinterpret_cast<CUcontext*>(&cuda_context_));
    }
    if (cuda_context_ == nullptr) {
        *error = "the CUDA primary context is not current after D3D12 creation";
        return false;
    }
    std::string ignored;
    (void)create_pack_stream(&ignored);
    if (!create_texture(static_cast<uint32_t>(config.input_width),
                        static_cast<uint32_t>(config.input_height), RTX_PIXEL_R10G10B10A2,
                        &input_, error)) {
        return false;
    }
    if (!create_texture(static_cast<uint32_t>(config.output_width),
                        static_cast<uint32_t>(config.output_height), RTX_PIXEL_RGBA16F,
                        &output_, error)) {
        return false;
    }
    if (static_cast<uint32_t>(config.output_width) > static_cast<uint32_t>(config.input_width) ||
        static_cast<uint32_t>(config.output_height) > static_cast<uint32_t>(config.input_height)) {
        if (!create_mid_texture(static_cast<uint32_t>(config.output_width),
                                static_cast<uint32_t>(config.output_height), error)) {
            return false;
        }
    }
    (void)import_fence_semaphore(&ignored);
    diagnostics_ = std::string("headless D3D12 device and shared textures ready; adapter removal ") +
                   (adapter_removal_declared_ ? "declared" : "not declared") +
                   "; pack stream priority " +
                   (pack_stream_ != nullptr ? (pack_stream_high_priority_ ? "high" : "default") : "unavailable");
    return true;
}

bool D3D12Interop::bind_context(std::string* error) {
    if (cuda_context_ == nullptr || g_cu_ctx_set_current == nullptr) {
        *error = "no CUDA context to bind";
        return false;
    }
    const CUresult result = g_cu_ctx_set_current(static_cast<CUcontext>(cuda_context_));
    if (result != CUDA_SUCCESS) {
        *error = std::string("cuCtxSetCurrent failed: ") + cudaErrorName(result);
        return false;
    }
    return true;
}

bool D3D12Interop::import_fence_semaphore(std::string* error) {
    if (fence_shared_handle_ == nullptr) {
        *error = "the timeline fence has no shared handle";
        return false;
    }
    if (!g_cu_import_ext_sem || !g_cu_signal_ext_sem || !g_cu_wait_ext_sem || !g_cu_destroy_ext_sem) {
        *error = "the CUDA driver lacks the external-semaphore API";
        return false;
    }
    if (!bind_context(error)) return false;
    CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC desc{};
    desc.type = CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE;
    desc.handle.win32.handle = fence_shared_handle_;
    desc.flags = 0;
    void* semaphore = nullptr;
    const CUresult imported = g_cu_import_ext_sem(&semaphore, &desc);
    if (imported != CUDA_SUCCESS || semaphore == nullptr) {
        *error = std::string("cuImportExternalSemaphore failed: ") + cudaErrorName(imported);
        return false;
    }
    external_semaphore_ = semaphore;
    return true;
}

bool D3D12Interop::fence_signal_cuda(void* cuda_stream, uint64_t value, std::string* error) {
    if (external_semaphore_ == nullptr || g_cu_signal_ext_sem == nullptr) {
        *error = "the fence semaphore is not imported";
        return false;
    }
    if (!bind_context(error)) return false;
    CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS params{};
    params.params.fence.value = value;
    void* semaphore = external_semaphore_;
    const CUresult res = g_cu_signal_ext_sem(&semaphore, &params, 1, static_cast<CUstream>(cuda_stream));
    if (res != CUDA_SUCCESS) {
        *error = std::string("cuSignalExternalSemaphoresAsync failed: ") + cudaErrorName(res);
        return false;
    }
    return true;
}

bool D3D12Interop::fence_wait_cuda(void* cuda_stream, uint64_t value, std::string* error) {
    if (external_semaphore_ == nullptr || g_cu_wait_ext_sem == nullptr) {
        *error = "the fence semaphore is not imported";
        return false;
    }
    if (!bind_context(error)) return false;
    CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS params{};
    params.params.fence.value = value;
    void* semaphore = external_semaphore_;
    const CUresult res = g_cu_wait_ext_sem(&semaphore, &params, 1, static_cast<CUstream>(cuda_stream));
    if (res != CUDA_SUCCESS) {
        *error = std::string("cuWaitExternalSemaphoresAsync failed: ") + cudaErrorName(res);
        return false;
    }
    return true;
}

bool D3D12Interop::queue_wait_fence(uint64_t value, std::string* error) {
    ID3D12CommandQueue* queue = as<ID3D12CommandQueue>(queue_);
    ID3D12Fence* fence = as<ID3D12Fence>(fence_);
    if (!queue || !fence) {
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
    if (!queue || !fence) {
        *error = "the D3D12 queue or fence is missing";
        return false;
    }
    if (FAILED(queue->Signal(fence, value))) {
        *error = "ID3D12CommandQueue::Signal on the shared fence failed";
        return false;
    }
    const uint64_t completed = fence->GetCompletedValue();
    auto stale = queue_pending_values_.begin();
    while (stale != queue_pending_values_.end() && *stale <= completed) ++stale;
    queue_pending_values_.erase(queue_pending_values_.begin(), stale);
    queue_pending_values_.push_back(value);
    queue_submitted_value_ = value;
    return true;
}

size_t D3D12Interop::queue_depth() const {
    const uint64_t completed = queue_completed_value();
    size_t depth = 0;
    for (uint64_t value : queue_pending_values_) {
        if (value > completed) ++depth;
    }
    return depth;
}

bool D3D12Interop::wait_for_queue_depth(size_t max_depth, std::chrono::milliseconds timeout,
                                        std::string* error) {
    ID3D12Fence* fence = as<ID3D12Fence>(fence_);
    if (!fence) {
        *error = "the D3D12 fence is missing";
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (queue_depth() <= max_depth) return true;
        const uint64_t completed = fence->GetCompletedValue();
        uint64_t oldest = 0;
        bool found = false;
        for (uint64_t value : queue_pending_values_) {
            if (value > completed && (!found || value < oldest)) {
                oldest = value;
                found = true;
            }
        }
        if (!found) return true;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) {
            *error = "cannot create the fence event";
            return false;
        }
        if (FAILED(fence->SetEventOnCompletion(oldest, event))) {
            CloseHandle(event);
            *error = "cannot arm the fence event for the queue-depth wait";
            return false;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   deadline - std::chrono::steady_clock::now()).count();
        const DWORD wait = WaitForSingleObject(event, remaining > 0 ? static_cast<DWORD>(remaining) : 0);
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
    if (!fence) {
        *error = "the D3D12 fence is missing";
        return false;
    }
    if (value == 0 || fence->GetCompletedValue() >= value) return true;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) {
        *error = "cannot create the shared-fence event";
        return false;
    }
    if (FAILED(fence->SetEventOnCompletion(value, event))) {
        CloseHandle(event);
        *error = "cannot arm the shared-fence event";
        return false;
    }
    const auto count = timeout.count();
    const DWORD wait = WaitForSingleObject(event, count > 0 ? static_cast<DWORD>(count) : 0);
    CloseHandle(event);
    std::string lost_error;
    if (pollDeviceLost(&lost_error)) {
        *error = "device removed while waiting for the shared fence: " + lost_error;
        return false;
    }
    if (wait != WAIT_OBJECT_0) {
        *error = std::format("the shared fence did not reach value {} in time", static_cast<unsigned long long>(value));
        return false;
    }
    return true;
}

bool D3D12Interop::pollDeviceLost(std::string* error) {
    ID3D12Device* device = as<ID3D12Device>(device_);
    if (!device) return device_lost_;
    const HRESULT reason = device->GetDeviceRemovedReason();
    if (SUCCEEDED(reason)) return false;
    device_lost_ = true;
    if (error != nullptr) {
        *error = std::format("D3D12 device removed (HRESULT 0x{:08x})", static_cast<unsigned long>(reason));
    }
    return true;
}

bool D3D12Interop::create_pack_stream(std::string* error) {
    if (pack_stream_ != nullptr) return true;
    if (!g_cu_stream_create_prio || !g_cu_stream_prio_range) {
        *error = "the CUDA driver lacks the stream-priority API";
        return false;
    }
    if (!bind_context(error)) return false;
    int least = 0, greatest = 0;
    if (const CUresult range = g_cu_stream_prio_range(&least, &greatest); range != CUDA_SUCCESS) {
        *error = std::string("cuCtxGetStreamPriorityRange failed: ") + cudaErrorName(range);
        return false;
    }
    CUstream stream = nullptr;
    const CUresult created = g_cu_stream_create_prio(&stream, CU_STREAM_NON_BLOCKING, greatest);
    if (created != CUDA_SUCCESS || !stream) {
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
    if (!bind_context(error)) return false;
    const uint16_t y_black = 0x1000, uv_neutral = 0x8000;
    if (g_cu_memset_d2d16) {
        g_cu_memset_d2d16(static_cast<CUdeviceptr>(device_ptr), pitch, y_black, width, height);
        g_cu_memset_d2d16(static_cast<CUdeviceptr>(device_ptr + pitch * height), pitch, uv_neutral, width, height / 2);
    } else if (g_cu_memset_d16) {
        for (size_t r = 0; r < height; ++r) g_cu_memset_d16(static_cast<CUdeviceptr>(device_ptr + r * pitch), y_black, width);
        const uint64_t uv_base = device_ptr + pitch * height;
        for (size_t r = 0; r < height / 2; ++r) g_cu_memset_d16(static_cast<CUdeviceptr>(uv_base + r * pitch), uv_neutral, width);
    }
    return true;
}

bool D3D12Interop::alloc_pitched(uint64_t* device_ptr, size_t* pitch, size_t width_bytes, size_t rows, std::string* error) {
    if (!device_ptr || !pitch || !g_cu_mem_alloc_pitch || !bind_context(error)) return false;
    CUdeviceptr p = 0; size_t r = 0;
    const CUresult res = g_cu_mem_alloc_pitch(&p, &r, width_bytes, rows, 16);
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    *device_ptr = static_cast<uint64_t>(p); *pitch = r;
    return true;
}

bool D3D12Interop::free_device(uint64_t device_ptr, std::string* error) {
    if (!device_ptr) return true;
    if (!g_cu_mem_free || !bind_context(error)) return false;
    const CUresult res = g_cu_mem_free(static_cast<CUdeviceptr>(device_ptr));
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    return true;
}

bool D3D12Interop::memset_d32_async(uint64_t device_ptr, unsigned int value, size_t count, void* stream, std::string* error) {
    if (!device_ptr || count == 0 || !g_cu_memset_d32_async || !bind_context(error)) return false;
    const CUresult res = g_cu_memset_d32_async(static_cast<CUdeviceptr>(device_ptr), value, count, static_cast<CUstream>(stream));
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    return true;
}

bool D3D12Interop::memcpy_dtoh(void* dst, uint64_t src, size_t bytes, std::string* error) {
    if (!dst || !src || bytes == 0 || !g_cu_memcpy2d || !bind_context(error)) return false;
    CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copy.srcDevice = static_cast<CUdeviceptr>(src);
    copy.srcPitch = bytes;
    copy.dstMemoryType = CU_MEMORYTYPE_HOST;
    copy.dstHost = dst;
    copy.dstPitch = bytes;
    copy.WidthInBytes = bytes;
    copy.Height = 1;
    const CUresult res = g_cu_memcpy2d(&copy);
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    return true;
}

bool D3D12Interop::memcpy_htod(uint64_t dst, const void* src, size_t bytes, std::string* error) {
    if (!dst || !src || bytes == 0 || !g_cu_memcpy2d || !bind_context(error)) return false;
    CUDA_MEMCPY2D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_HOST;
    copy.srcHost = src;
    copy.srcPitch = bytes;
    copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    copy.dstDevice = static_cast<CUdeviceptr>(dst);
    copy.dstPitch = bytes;
    copy.WidthInBytes = bytes;
    copy.Height = 1;
    const CUresult res = g_cu_memcpy2d(&copy);
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    return true;
}

bool D3D12Interop::create_lut_texture(const uint16_t* lut_half4_data, int32_t grid_size,
                                      void** texture_object, void** array_out, std::string* error) {
    if (!texture_object || !array_out) {
        *error = "create_lut_texture requires output pointers";
        return false;
    }
    *texture_object = nullptr;
    *array_out = nullptr;
    if (!g_cu_array3d_create || !g_cu_tex_create || !g_cu_array_destroy || !g_cu_tex_destroy || !g_cu_memcpy3d) {
        *error = "the CUDA driver lacks the 3D texture-object API";
        return false;
    }
    if (!bind_context(error)) return false;
    CUDA_ARRAY3D_DESCRIPTOR array_desc{};
    array_desc.Width = static_cast<size_t>(grid_size);
    array_desc.Height = static_cast<size_t>(grid_size);
    array_desc.Depth = static_cast<size_t>(grid_size);
    array_desc.Format = CU_AD_FORMAT_HALF;
    array_desc.NumChannels = 4;
    CUarray array = nullptr;
    const CUresult created = g_cu_array3d_create(&array, &array_desc);
    if (created != CUDA_SUCCESS || !array) {
        *error = std::string("cuArray3DCreate failed: ") + cudaErrorName(created);
        return false;
    }
    LocalCudaMemcpy3D copy{};
    copy.srcMemoryType = CU_MEMORYTYPE_HOST;
    copy.srcHost = lut_half4_data;
    copy.dstMemoryType = CU_MEMORYTYPE_ARRAY;
    copy.dstArray = array;
    copy.WidthInBytes = static_cast<unsigned>(grid_size) * 4u * sizeof(uint16_t);
    copy.Height = static_cast<unsigned>(grid_size);
    copy.Depth = static_cast<unsigned>(grid_size);
    if (const CUresult copied = g_cu_memcpy3d(&copy); copied != CUDA_SUCCESS) {
        *error = std::string("cuMemcpy3D (LUT H2D) failed: ") + cudaErrorName(copied);
        (void)g_cu_array_destroy(array);
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
    texture_desc.flags = 2u;  /* CU_TRSF_NORMALIZED_COORDINATES */
    CUtexObject tex_object = 0;
    const CUresult tex_created = g_cu_tex_create(&tex_object, &resource_desc, &texture_desc, nullptr);
    if (tex_created != CUDA_SUCCESS || tex_object == 0) {
        *error = std::string("cuTexObjectCreate failed: ") + cudaErrorName(tex_created);
        (void)g_cu_array_destroy(array);
        return false;
    }
    *texture_object = reinterpret_cast<void*>(static_cast<uintptr_t>(tex_object));
    *array_out = array;
    return true;
}

void D3D12Interop::destroy_lut_texture(void** texture_object, void** array) noexcept {
    if (texture_object && *texture_object) {
        if (g_cu_tex_destroy) g_cu_tex_destroy(static_cast<CUtexObject>(reinterpret_cast<uintptr_t>(*texture_object)));
        *texture_object = nullptr;
    }
    if (array && *array) {
        if (g_cu_array_destroy) g_cu_array_destroy(static_cast<CUarray>(*array));
        *array = nullptr;
    }
}

bool D3D12Interop::event_create(void** event, std::string* error) {
    if (!event) return false;
    *event = nullptr;
    if (!g_cu_event_create || !bind_context(error)) return false;
    CUevent handle = nullptr;
    const CUresult res = g_cu_event_create(&handle, 0);
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    *event = handle;
    return true;
}

void D3D12Interop::event_destroy(void* event) noexcept {
    if (event && g_cu_event_destroy) g_cu_event_destroy(static_cast<CUevent>(event));
}

bool D3D12Interop::event_record(void* event, void* stream, std::string* error) {
    if (!event || !g_cu_event_record || !bind_context(error)) return false;
    const CUresult res = g_cu_event_record(static_cast<CUevent>(event), static_cast<CUstream>(stream));
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    return true;
}

bool D3D12Interop::event_elapsed(double* ms, void* start, void* end, std::string* error) {
    if (!ms || !start || !end || !g_cu_event_elapsed) return false;
    *ms = 0.0;
    float val = 0.0f;
    const CUresult res = g_cu_event_elapsed(&val, static_cast<CUevent>(start), static_cast<CUevent>(end));
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    *ms = static_cast<double>(val);
    return true;
}

bool D3D12Interop::stream_synchronize(void* stream, std::string* error) {
    if (!g_cu_stream_sync || !bind_context(error)) return false;
    const CUresult res = g_cu_stream_sync(static_cast<CUstream>(stream));
    if (res != CUDA_SUCCESS) { if (error) *error = cudaErrorName(res); return false; }
    return true;
}

uint64_t D3D12Interop::queue_completed_value() const {
    ID3D12Fence* fence = as<ID3D12Fence>(fence_);
    return fence ? fence->GetCompletedValue() : 0;
}

bool D3D12Interop::create_device(const uint8_t luid[8], std::string* error) {
    adapter_removal_declared_ = g_d3d12_removal_declared;
    IDXGIFactory4* factory = nullptr;
    HRESULT hr = g_dxgi_create_factory(kIidFactory4, reinterpret_cast<void**>(&factory));
    if (FAILED(hr) || !factory) {
        *error = "CreateDXGIFactory1 failed";
        return false;
    }
    IDXGIAdapter1* matched = nullptr;
    for (UINT index = 0;; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (std::memcmp(&desc.AdapterLuid, luid, 8) == 0) {
            matched = adapter;
            break;
        }
        adapter->Release();
    }
    factory->Release();
    if (!matched) {
        *error = "no DXGI adapter matches the CUDA device LUID";
        return false;
    }
    ID3D12Device* device = nullptr;
    for (D3D_FEATURE_LEVEL level : {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_0}) {
        hr = g_d3d12_create_device(matched, level, kIidDevice, reinterpret_cast<void**>(&device));
        if (SUCCEEDED(hr) && device) break;
    }
    matched->Release();
    if (FAILED(hr) || !device) {
        *error = "D3D12CreateDevice failed for feature levels 12_1/12_0/11_0";
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    hr = device->CreateCommandQueue(&queue_desc, kIidCommandQueue, reinterpret_cast<void**>(&queue));
    if (FAILED(hr) || !queue) {
        device->Release();
        *error = "CreateCommandQueue failed";
        return false;
    }
    ID3D12Fence* fence = nullptr;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_SHARED, kIidFence, reinterpret_cast<void**>(&fence));
    if (FAILED(hr) || !fence) {
        queue->Release();
        device->Release();
        *error = "CreateFence failed";
        return false;
    }
    HANDLE fence_handle = nullptr;
    if (FAILED(device->CreateSharedHandle(fence, nullptr, GENERIC_ALL, nullptr, &fence_handle))) {
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
                                  TextureTuple* texture, std::string* error) {
    const auto [dxgi, cuda_fmt, channels, bpp] = lookupFormat(format);
    (void)cuda_fmt; (void)channels; (void)bpp;
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
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    ID3D12Device* device = as<ID3D12Device>(device_);
    ID3D12Resource* resource = nullptr;
    const HRESULT hr = device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_SHARED, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, kIidResource,
        reinterpret_cast<void**>(&resource));
    if (FAILED(hr) || !resource) {
        *error = "CreateCommittedResource(SHARED) failed";
        return false;
    }
    HANDLE handle = nullptr;
    if (FAILED(device->CreateSharedHandle(resource, nullptr, GENERIC_ALL, nullptr, &handle))) {
        resource->Release();
        *error = "CreateSharedHandle failed";
        return false;
    }
    auto& [t_res, t_handle, t_surf, t_ext, t_mip, t_arr, t_w, t_h, t_fmt, t_sz] = *texture;
    (void)t_surf; (void)t_ext; (void)t_mip; (void)t_arr;
    t_res = resource;
    t_handle = handle;
    t_w = width;
    t_h = height;
    t_fmt = format;
    t_sz = device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    return import_into_cuda(texture, error);
}

bool D3D12Interop::create_shared_buffer_ring(uint32_t slot_count, uint32_t width,
                                             uint32_t height, uint32_t bytes_per_pixel,
                                             std::string* error) {
    if (slot_count == 0 || width == 0 || height == 0 || bytes_per_pixel == 0) {
        if (error) *error = "create_shared_buffer_ring: invalid geometry";
        return false;
    }
    // Doc 47 (Invariant 1 & 3): 256-byte aligned row_bytes and full (slot_count, width, height) capacity check.
    const uint32_t row_bytes = ((width * bytes_per_pixel) + 255u) & ~255u;
    if (!preview_ring_.empty() && preview_ring_.size() == slot_count &&
        preview_ring_row_bytes_ == row_bytes && preview_ring_width_ >= width &&
        preview_ring_height_ >= height) {
        return true;
    }
    release_shared_buffer_ring();
    if (!bind_context(error)) return false;
    if (!g_cu_get_mapped_buf) {
        if (error) *error = "CUDA driver lacks cuExternalMemoryGetMappedBuffer";
        return false;
    }
    ID3D12Device* device = as<ID3D12Device>(device_);
    const unsigned long long total_bytes =
        static_cast<unsigned long long>(row_bytes) * static_cast<unsigned long long>(height);

    preview_ring_.assign(slot_count, {nullptr, nullptr, nullptr, 0});
    for (uint32_t i = 0; i < slot_count; ++i) {
        auto& [slot_res, slot_handle, slot_ext, slot_ptr] = preview_ring_[i];
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = total_bytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = 1;
        heap.VisibleNodeMask = 1;

        ID3D12Resource* resource = nullptr;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &desc,
                                                   D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                   kIidResource, reinterpret_cast<void**>(&resource))) ||
            !resource) {
            if (error) *error = "CreateCommittedResource(SHARED preview buffer) failed";
            release_shared_buffer_ring();
            return false;
        }
        slot_res = resource;
        HANDLE handle = nullptr;
        if (FAILED(device->CreateSharedHandle(resource, nullptr, GENERIC_ALL, nullptr, &handle))) {
            if (error) *error = "CreateSharedHandle for the preview buffer failed";
            release_shared_buffer_ring();
            return false;
        }
        slot_handle = handle;

        CUDA_EXTERNAL_MEMORY_HANDLE_DESC handle_desc{};
        handle_desc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
        handle_desc.handle.win32.handle = handle;
        handle_desc.size = device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
        handle_desc.flags = kExternalMemoryDedicated;

        CUexternalMemory external = nullptr;
        CUresult result = g_cu_import_ext_mem(&external, &handle_desc);
        if (result != CUDA_SUCCESS) {
            if (error) *error = std::string("cuImportExternalMemory(preview buffer) failed: ") + cudaErrorName(result);
            release_shared_buffer_ring();
            return false;
        }
        slot_ext = external;

        CUDA_EXTERNAL_MEMORY_BUFFER_DESC buffer_desc{};
        buffer_desc.size = total_bytes;
        CUdeviceptr mapped = 0;
        result = g_cu_get_mapped_buf(&mapped, external, &buffer_desc);
        if (result != CUDA_SUCCESS || mapped == 0) {
            if (error) *error = std::string("cuExternalMemoryGetMappedBuffer failed: ") + cudaErrorName(result);
            release_shared_buffer_ring();
            return false;
        }
        slot_ptr = mapped;
    }
    preview_ring_row_bytes_ = row_bytes;
    preview_ring_width_ = width;
    preview_ring_height_ = height;
    return true;
}

void D3D12Interop::release_shared_buffer_ring() noexcept {
    if (preview_ring_.empty()) {
        preview_ring_row_bytes_ = 0;
        preview_ring_width_ = 0;
        preview_ring_height_ = 0;
        return;
    }
    std::string ignored;
    (void)bind_context(&ignored);
    for (auto& [res, handle, ext, ptr] : preview_ring_) {
        if (ext && g_cu_destroy_ext_mem) {
            (void)g_cu_destroy_ext_mem(static_cast<CUexternalMemory>(ext));
            ext = nullptr;
        }
        if (res) {
            as<ID3D12Resource>(res)->Release();
            res = nullptr;
        }
        if (handle) {
            CloseHandle(static_cast<HANDLE>(handle));
            handle = nullptr;
        }
        ptr = 0;
    }
    preview_ring_.clear();
    preview_ring_row_bytes_ = 0;
    preview_ring_width_ = 0;
    preview_ring_height_ = 0;
}

bool D3D12Interop::create_mid_texture(uint32_t width, uint32_t height, std::string* error) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
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
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_COMMON, nullptr,
                                               kIidResource, reinterpret_cast<void**>(&resource))) ||
        !resource) {
        *error = "CreateCommittedResource for the 4K SDR mid texture failed";
        return false;
    }
    auto& [m_res, m_h, m_surf, m_ext, m_mip, m_arr, m_w, m_hh, m_fmt, m_sz] = mid_4k_;
    (void)m_h; (void)m_surf; (void)m_ext; (void)m_mip; (void)m_arr;
    m_res = resource;
    m_w = width;
    m_hh = height;
    m_fmt = RTX_PIXEL_RGB32;
    m_sz = device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    return true;
}

bool D3D12Interop::import_into_cuda(TextureTuple* texture, std::string* error) {
    auto& [t_res, t_handle, t_surf, t_ext, t_mip, t_arr, t_w, t_h, t_fmt, t_sz] = *texture;
    (void)t_res;
    CUDA_EXTERNAL_MEMORY_HANDLE_DESC handle_desc{};
    handle_desc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
    handle_desc.handle.win32.handle = t_handle;
    handle_desc.size = t_sz;
    handle_desc.flags = kExternalMemoryDedicated;

    CUexternalMemory external = nullptr;
    CUresult result = g_cu_import_ext_mem(&external, &handle_desc);
    if (result != CUDA_SUCCESS) {
        *error = std::string("cuImportExternalMemory failed: ") + cudaErrorName(result);
        return false;
    }
    const auto [dxgi, cuda_fmt, channels, bpp] = lookupFormat(t_fmt);
    (void)dxgi;
    if (channels == 0 || channelBytes(cuda_fmt) * channels != bpp) {
        g_cu_destroy_ext_mem(external);
        *error = "interop format " + std::to_string(t_fmt) + " has no consistent CUDA array descriptor";
        return false;
    }
    CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC mip_desc{};
    mip_desc.numLevels = 1;
    mip_desc.arrayDesc.Width = t_w;
    mip_desc.arrayDesc.Height = t_h;
    mip_desc.arrayDesc.Format = cuda_fmt;
    mip_desc.arrayDesc.NumChannels = channels;
    CUmipmappedArray mip = nullptr;
    result = g_cu_get_mapped_mip(&mip, external, &mip_desc);
    if (result != CUDA_SUCCESS) {
        g_cu_destroy_ext_mem(external);
        *error = std::string("cuExternalMemoryGetMappedMipmappedArray failed: ") + cudaErrorName(result);
        return false;
    }
    CUarray array = nullptr;
    result = g_cu_mip_get_level(&array, mip, 0);
    if (result != CUDA_SUCCESS) {
        g_cu_mip_destroy(mip);
        g_cu_destroy_ext_mem(external);
        *error = std::string("cuMipmappedArrayGetLevel failed: ") + cudaErrorName(result);
        return false;
    }
    CUDA_RESOURCE_DESC resource_desc{};
    resource_desc.resType = CU_RESOURCE_TYPE_ARRAY;
    resource_desc.res.array.hArray = array;
    CUsurfObject surface = 0;
    result = g_cu_surf_create(&surface, &resource_desc);
    if (result != CUDA_SUCCESS) {
        g_cu_mip_destroy(mip);
        g_cu_destroy_ext_mem(external);
        *error = std::string("cuSurfObjectCreate failed: ") + cudaErrorName(result);
        return false;
    }
    t_ext = external;
    t_mip = mip;
    t_arr = array;
    t_surf = surface;
    return true;
}

bool D3D12Interop::flush_queue(std::string* error) {
    ID3D12Fence* fence = as<ID3D12Fence>(fence_);
    const uint64_t value = advance_timeline();
    if (!queue_signal_fence(value, error)) return false;
    if (!waitForFence(fence, value)) {
        *error = "D3D12 fence wait failed";
        return false;
    }
    return true;
}

void D3D12Interop::release() {
    std::string ignored_error;
    if (cuda_context_ != nullptr) (void)bind_context(&ignored_error);
    release_shared_buffer_ring();
    {
        auto& [m_res, m_h, m_surf, m_ext, m_mip, m_arr, m_w, m_hh, m_fmt, m_sz] = mid_4k_;
        (void)m_h; (void)m_surf; (void)m_ext; (void)m_mip; (void)m_arr; (void)m_w; (void)m_hh; (void)m_fmt; (void)m_sz;
        if (m_res) { as<ID3D12Resource>(m_res)->Release(); m_res = nullptr; }
    }
    for (TextureTuple* texture : {&input_, &output_}) {
        auto& [res, handle, surf, ext, mip, arr, w, hh, fmt, sz] = *texture;
        (void)arr; (void)w; (void)hh; (void)fmt; (void)sz;
        if (surf != 0 && g_cu_surf_destroy) { g_cu_surf_destroy(surf); surf = 0; }
        if (mip && g_cu_mip_destroy) { g_cu_mip_destroy(static_cast<CUmipmappedArray>(mip)); mip = nullptr; }
        if (ext && g_cu_destroy_ext_mem) { g_cu_destroy_ext_mem(static_cast<CUexternalMemory>(ext)); ext = nullptr; }
        if (res) { as<ID3D12Resource>(res)->Release(); res = nullptr; }
        if (handle) { CloseHandle(static_cast<HANDLE>(handle)); handle = nullptr; }
    }
    if (external_semaphore_ && g_cu_destroy_ext_sem) { (void)g_cu_destroy_ext_sem(external_semaphore_); external_semaphore_ = nullptr; }
    if (pack_stream_ && g_cu_stream_destroy) { (void)g_cu_stream_destroy(static_cast<CUstream>(pack_stream_)); pack_stream_ = nullptr; }
    if (fence_shared_handle_) { CloseHandle(static_cast<HANDLE>(fence_shared_handle_)); fence_shared_handle_ = nullptr; }
    if (fence_) { as<ID3D12Fence>(fence_)->Release(); fence_ = nullptr; }
    if (queue_) { as<ID3D12CommandQueue>(queue_)->Release(); queue_ = nullptr; }
    if (device_) { as<ID3D12Device>(device_)->Release(); device_ = nullptr; }
    cuda_context_ = nullptr;
}

bool interopPrerequisitesAvailable() {
    std::string error;
    return loadD3D12Api(&error) && loadCudaApi(&error);
}

std::wstring plugin_directory() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&plugin_directory), &module) == 0) {
        return L"plugins\\nvidia_rtx";
    }
    wchar_t module_path[32768] = {};
    const DWORD module_length = GetModuleFileNameW(module, module_path, 32768);
    std::wstring path(module_length > 0 && module_length < 32768
                          ? std::wstring(module_path, module_length)
                          : std::wstring());
    if (const size_t slash = path.find_last_of(L"\\/"); slash != std::wstring::npos) path.resize(slash);
    auto present = [&path](const wchar_t* name) {
        const std::wstring candidate = path + L"\\" + name;
        const DWORD attributes = GetFileAttributesW(candidate.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    };
    if (present(L"nvngx_vsr.dll") || present(L"nvngx_truehdr.dll")) return path;
    return path + L"\\plugins\\nvidia_rtx";
#else
    return std::wstring();
#endif
}

void* load_plugin_library(const wchar_t* name) {
#if defined(_WIN32)
    if (!name) return nullptr;
    const std::wstring path = plugin_directory() + L"\\" + name;
    return LoadLibraryExW(path.c_str(), nullptr,
                          LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
    (void)name;
    return nullptr;
#endif
}

}  // namespace rtx_ngx
