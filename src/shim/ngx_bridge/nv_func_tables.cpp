/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 */

#include "nv_func_tables.h"

#include <mutex>
#include <type_traits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rtx_ngx::nv_cuda {

namespace {

constexpr NvCuResult kCudaNotReady = 3;

NvCuResult (*g_init)(unsigned int) = nullptr;
NvCuResult (*g_device_get)(int*, int) = nullptr;
NvCuResult (*g_ctx_get_current)(NvCuContext*) = nullptr;
NvCuResult (*g_ctx_set_current)(NvCuContext) = nullptr;
NvCuResult (*g_ctx_push_current)(NvCuContext) = nullptr;
NvCuResult (*g_ctx_pop_current)(NvCuContext*) = nullptr;
NvCuResult (*g_primary_ctx_retain)(NvCuContext*, int) = nullptr;
NvCuResult (*g_module_load_data)(NvCuModule*, const void*) = nullptr;
NvCuResult (*g_module_get_function)(NvCuFunction*, NvCuModule, const char*) = nullptr;
NvCuResult (*g_module_unload)(NvCuModule) = nullptr;
NvCuResult (*g_launch_kernel)(NvCuFunction, unsigned, unsigned, unsigned, unsigned, unsigned,
                              unsigned, unsigned, NvCuStream, void**, void**) = nullptr;
NvCuResult (*g_get_error_name)(NvCuResult, const char**) = nullptr;

NvCuResult (*g_mem_alloc)(NvCuDevicePtr*, size_t) = nullptr;
NvCuResult (*g_mem_free)(NvCuDevicePtr) = nullptr;
NvCuResult (*g_mem_alloc_host)(void**, size_t) = nullptr;
NvCuResult (*g_mem_free_host)(void*) = nullptr;
NvCuResult (*g_memcpy_htod)(NvCuDevicePtr, const void*, size_t) = nullptr;
NvCuResult (*g_memcpy_dtoh)(void*, NvCuDevicePtr, size_t) = nullptr;
NvCuResult (*g_memcpy_dtod_async)(NvCuDevicePtr, NvCuDevicePtr, size_t, NvCuStream) = nullptr;
NvCuResult (*g_memset_d32_async)(NvCuDevicePtr, unsigned int, size_t, NvCuStream) = nullptr;
NvCuResult (*g_stream_create)(NvCuStream*, unsigned int) = nullptr;
NvCuResult (*g_stream_destroy)(NvCuStream) = nullptr;
NvCuResult (*g_stream_sync)(NvCuStream) = nullptr;
NvCuResult (*g_memcpy_2d_async)(const void*, NvCuStream) = nullptr;

bool g_driver_available = false;
bool g_loaded = false;
bool g_module_api_loaded = false;

void ensureLoaded() noexcept {
    static std::once_flag once;
    std::call_once(once, []() noexcept {
#if defined(_WIN32)
        HMODULE h = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!h) return;
        auto load = [h](auto& fn, const char* primary, const char* fallback = nullptr) noexcept {
            FARPROC p = GetProcAddress(h, primary);
            if (!p && fallback) p = GetProcAddress(h, fallback);
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(p);
        };
#else
        void* h = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!h) return;
        auto load = [h](auto& fn, const char* primary, const char* fallback = nullptr) noexcept {
            void* p = dlsym(h, primary);
            if (!p && fallback) p = dlsym(h, fallback);
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(p);
        };
#endif
        g_driver_available = true;
        load(g_init, "cuInit");
        load(g_device_get, "cuDeviceGet");
        load(g_ctx_get_current, "cuCtxGetCurrent");
        load(g_ctx_set_current, "cuCtxSetCurrent");
        load(g_ctx_push_current, "cuCtxPushCurrent_v2", "cuCtxPushCurrent");
        load(g_ctx_pop_current, "cuCtxPopCurrent_v2", "cuCtxPopCurrent");
        load(g_primary_ctx_retain, "cuDevicePrimaryCtxRetain");
        load(g_module_load_data, "cuModuleLoadData");
        load(g_module_get_function, "cuModuleGetFunction");
        load(g_module_unload, "cuModuleUnload");
        load(g_launch_kernel, "cuLaunchKernel");
        load(g_get_error_name, "cuGetErrorName");

        load(g_mem_alloc, "cuMemAlloc_v2", "cuMemAlloc");
        load(g_mem_free, "cuMemFree_v2", "cuMemFree");
        load(g_mem_alloc_host, "cuMemAllocHost_v2", "cuMemAllocHost");
        load(g_mem_free_host, "cuMemFreeHost");
        load(g_memcpy_htod, "cuMemcpyHtoD_v2", "cuMemcpyHtoD");
        load(g_memcpy_dtoh, "cuMemcpyDtoH_v2", "cuMemcpyDtoH");
        load(g_memcpy_dtod_async, "cuMemcpyDtoDAsync_v2", "cuMemcpyDtoDAsync");
        load(g_memset_d32_async, "cuMemsetD32Async");
        load(g_stream_create, "cuStreamCreate");
        load(g_stream_destroy, "cuStreamDestroy_v2", "cuStreamDestroy");
        load(g_stream_sync, "cuStreamSynchronize");
        load(g_memcpy_2d_async, "cuMemcpy2DAsync_v2", "cuMemcpy2DAsync");

        g_loaded = (g_mem_alloc && g_mem_free && g_memcpy_dtoh && g_memcpy_dtod_async && g_stream_sync);
        g_module_api_loaded = (g_module_load_data && g_module_get_function && g_module_unload && g_launch_kernel);
    });
}

}  // namespace

bool loaded() noexcept { ensureLoaded(); return g_loaded; }
bool driverAvailable() noexcept { ensureLoaded(); return g_driver_available; }
bool moduleApiLoaded() noexcept { ensureLoaded(); return g_module_api_loaded; }

NvCuResult init(unsigned int flags) noexcept { ensureLoaded(); return g_init ? g_init(flags) : kCudaNotReady; }
NvCuResult deviceGet(int* device, int ordinal) noexcept { ensureLoaded(); return g_device_get ? g_device_get(device, ordinal) : kCudaNotReady; }
NvCuResult ctxGetCurrent(NvCuContext* ctx) noexcept { ensureLoaded(); return g_ctx_get_current ? g_ctx_get_current(ctx) : kCudaNotReady; }
NvCuResult ctxSetCurrent(NvCuContext ctx) noexcept { ensureLoaded(); return g_ctx_set_current ? g_ctx_set_current(ctx) : kCudaNotReady; }
NvCuResult ctxPushCurrent(NvCuContext ctx) noexcept { ensureLoaded(); return g_ctx_push_current ? g_ctx_push_current(ctx) : kCudaNotReady; }
NvCuResult ctxPopCurrent(NvCuContext* ctx) noexcept { ensureLoaded(); return g_ctx_pop_current ? g_ctx_pop_current(ctx) : kCudaNotReady; }
NvCuResult primaryCtxRetain(NvCuContext* ctx, int device) noexcept { ensureLoaded(); return g_primary_ctx_retain ? g_primary_ctx_retain(ctx, device) : kCudaNotReady; }
NvCuResult moduleLoadData(NvCuModule* mod, const void* image) noexcept { ensureLoaded(); return g_module_load_data ? g_module_load_data(mod, image) : kCudaNotReady; }
NvCuResult moduleGetFunction(NvCuFunction* fn, NvCuModule mod, const char* name) noexcept { ensureLoaded(); return g_module_get_function ? g_module_get_function(fn, mod, name) : kCudaNotReady; }
NvCuResult moduleUnload(NvCuModule mod) noexcept { ensureLoaded(); return g_module_unload ? g_module_unload(mod) : kCudaNotReady; }
NvCuResult launchKernel(NvCuFunction fn, unsigned gx, unsigned gy, unsigned gz,
                        unsigned bx, unsigned by, unsigned bz,
                        unsigned shared_mem, NvCuStream stream,
                        void** params, void** extra) noexcept {
    ensureLoaded();
    return g_launch_kernel ? g_launch_kernel(fn, gx, gy, gz, bx, by, bz, shared_mem, stream, params, extra) : kCudaNotReady;
}
NvCuResult getErrorName(NvCuResult code, const char** name) noexcept { ensureLoaded(); return g_get_error_name ? g_get_error_name(code, name) : kCudaNotReady; }

std::string formatError(const char* prefix, NvCuResult code) {
    ensureLoaded();
    std::string text = std::string(prefix) + " " + std::to_string(code);
    const char* name = nullptr;
    if (g_get_error_name && g_get_error_name(code, &name) == 0 && name != nullptr) {
        text += std::string(" (") + name + ")";
    }
    return text;
}

NvCuResult memAlloc(NvCuDevicePtr* dptr, size_t bytes) noexcept { ensureLoaded(); return g_mem_alloc ? g_mem_alloc(dptr, bytes) : kCudaNotReady; }
NvCuResult memFree(NvCuDevicePtr dptr) noexcept { ensureLoaded(); return g_mem_free ? g_mem_free(dptr) : kCudaNotReady; }
NvCuResult memAllocHost(void** ptr, size_t bytes) noexcept { ensureLoaded(); return g_mem_alloc_host ? g_mem_alloc_host(ptr, bytes) : kCudaNotReady; }
NvCuResult memFreeHost(void* ptr) noexcept { ensureLoaded(); return g_mem_free_host ? g_mem_free_host(ptr) : kCudaNotReady; }
NvCuResult memcpyHtoD(NvCuDevicePtr dst, const void* src, size_t bytes) noexcept { ensureLoaded(); return g_memcpy_htod ? g_memcpy_htod(dst, src, bytes) : kCudaNotReady; }
NvCuResult memcpyDtoH(void* dst, NvCuDevicePtr src, size_t bytes) noexcept { ensureLoaded(); return g_memcpy_dtoh ? g_memcpy_dtoh(dst, src, bytes) : kCudaNotReady; }
NvCuResult memcpyDtoDAsync(NvCuDevicePtr dst, NvCuDevicePtr src, size_t bytes, NvCuStream stream) noexcept { ensureLoaded(); return g_memcpy_dtod_async ? g_memcpy_dtod_async(dst, src, bytes, stream) : kCudaNotReady; }
NvCuResult memsetD32Async(NvCuDevicePtr dst, unsigned int value, size_t count, NvCuStream stream) noexcept { ensureLoaded(); return g_memset_d32_async ? g_memset_d32_async(dst, value, count, stream) : kCudaNotReady; }
NvCuResult streamCreate(NvCuStream* stream, unsigned int flags) noexcept { ensureLoaded(); return g_stream_create ? g_stream_create(stream, flags) : kCudaNotReady; }
NvCuResult streamDestroy(NvCuStream stream) noexcept { ensureLoaded(); return g_stream_destroy ? g_stream_destroy(stream) : kCudaNotReady; }
NvCuResult streamSync(NvCuStream stream) noexcept { ensureLoaded(); return g_stream_sync ? g_stream_sync(stream) : kCudaNotReady; }
NvCuResult memcpy2DAsync(const void* copy_desc, NvCuStream stream) noexcept { ensureLoaded(); return g_memcpy_2d_async ? g_memcpy_2d_async(copy_desc, stream) : kCudaNotReady; }

}  // namespace rtx_ngx::nv_cuda
