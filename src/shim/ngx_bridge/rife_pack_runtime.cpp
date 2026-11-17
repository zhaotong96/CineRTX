/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Runtime loader and launcher for RIFE pre/post-processing CUDA kernels.
 * Phase 9: the deployed PTX module exposes
 *   - rife_nv12_to_nchw_7ch_fp32     (single NV12 base pointer ABI)
 *   - rife_nchw_3ch_fp32_to_nv12     (single NV12 base pointer ABI)
 * Legacy kernel names are kept only as fallback aliases for old modules.
 */

#include "rife_pack_runtime.h"

#include <cstring>
#include <mutex>
#include <string>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#endif
#include "rife_pack_ptx.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

using CUresult = int;
using CUmodule = void*;
using CUfunction = void*;
using CUstream = void*;
using CUcontext = void*;

constexpr CUresult kSuccess = 0;

struct DriverApi {
    CUresult (*init)(unsigned int) = nullptr;
    CUresult (*device_get)(int*, int) = nullptr;
    CUresult (*ctx_get_current)(void**) = nullptr;
    CUresult (*ctx_set_current)(void*) = nullptr;
    CUresult (*primary_ctx_retain)(void**, int) = nullptr;
    CUresult (*module_load_data)(CUmodule*, const void*) = nullptr;
    CUresult (*module_get_function)(CUfunction*, CUmodule, const char*) = nullptr;
    CUresult (*module_unload)(CUmodule) = nullptr;
    CUresult (*launch_kernel)(CUfunction, unsigned, unsigned, unsigned, unsigned, unsigned,
                              unsigned, unsigned, CUstream, void**, void**) = nullptr;
    CUresult (*get_error_name)(CUresult, const char**) = nullptr;
};

#if defined(_WIN32)
void* openCudaDriver() {
    HMODULE module = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return reinterpret_cast<void*>(module);
}

void* resolveSymbol(void* library, const char* name) {
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(library), name));
}
#else
void* openCudaDriver() { return dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL); }
void* resolveSymbol(void* library, const char* name) { return dlsym(library, name); }
#endif

bool resolveDriver(DriverApi& api, std::string* error) {
    void* library = openCudaDriver();
    if (library == nullptr) {
        if (error) *error = "nvcuda.dll is not available";
        return false;
    }
    api.init = reinterpret_cast<decltype(api.init)>(resolveSymbol(library, "cuInit"));
    api.device_get = reinterpret_cast<decltype(api.device_get)>(resolveSymbol(library, "cuDeviceGet"));
    api.ctx_get_current = reinterpret_cast<decltype(api.ctx_get_current)>(resolveSymbol(library, "cuCtxGetCurrent"));
    api.ctx_set_current = reinterpret_cast<decltype(api.ctx_set_current)>(resolveSymbol(library, "cuCtxSetCurrent"));
    api.primary_ctx_retain = reinterpret_cast<decltype(api.primary_ctx_retain)>(resolveSymbol(library, "cuDevicePrimaryCtxRetain"));
    api.module_load_data = reinterpret_cast<decltype(api.module_load_data)>(resolveSymbol(library, "cuModuleLoadData"));
    api.module_get_function = reinterpret_cast<decltype(api.module_get_function)>(resolveSymbol(library, "cuModuleGetFunction"));
    api.module_unload = reinterpret_cast<decltype(api.module_unload)>(resolveSymbol(library, "cuModuleUnload"));
    api.launch_kernel = reinterpret_cast<decltype(api.launch_kernel)>(resolveSymbol(library, "cuLaunchKernel"));
    api.get_error_name = reinterpret_cast<decltype(api.get_error_name)>(resolveSymbol(library, "cuGetErrorName"));

    if (!api.module_load_data || !api.module_get_function || !api.launch_kernel) {
        if (error) *error = "CUDA driver is missing required module/launch APIs";
        return false;
    }
    return true;
}

struct RifePackState {
    std::mutex mutex;
    DriverApi api{};
    CUmodule module = nullptr;
    CUfunction preprocess_kernel = nullptr;
    CUfunction preprocess_separate_kernel = nullptr;
    CUfunction postprocess_kernel = nullptr;
    CUfunction fp32_to_fp16_kernel = nullptr;
    CUfunction fp16_to_fp32_kernel = nullptr;
    CUfunction eval_block_sad_kernel = nullptr;
    CUcontext primary_context = nullptr;
    bool primary_context_retained = false;
    bool attempted = false;
    bool ready = false;
    std::string error;
};

RifePackState& state() {
    static RifePackState instance;
    return instance;
}

/* Formats a precise CUDA driver diagnostic: "<message>: <code> (<name>)". */
std::string driverError(const DriverApi& api, const char* message, CUresult code) {
    std::string text = std::string(message) + " " + std::to_string(code);
    if (api.get_error_name) {
        const char* name = nullptr;
        if (api.get_error_name(code, &name) == kSuccess && name != nullptr) {
            text += std::string(" (") + name + ")";
        }
    }
    return text;
}

bool bindPrimaryContext(RifePackState& s) {
    /* Reuse the caller's active CUDA context when present: overwriting a live
     * thread context with the primary context corrupts driver state. */
    CUcontext current = nullptr;
    if (s.api.ctx_get_current &&
        s.api.ctx_get_current(&current) == kSuccess && current != nullptr) {
        return true;
    }

    /* Only retain the device primary context when no context is active. */
    if (!s.primary_context_retained) {
        if (!s.api.device_get) {
            s.error = "no CUDA driver cuDeviceGet API";
            return false;
        }
        int dev = 0;
        const CUresult dev_res = s.api.device_get(&dev, 0);
        if (dev_res != kSuccess) {
            s.error = driverError(s.api, "cuDeviceGet failed:", dev_res);
            return false;
        }
        if (!s.api.primary_ctx_retain) {
            s.error = "no CUDA driver cuDevicePrimaryCtxRetain API";
            return false;
        }
        const CUresult retain_res = s.api.primary_ctx_retain(&s.primary_context, dev);
        if (retain_res != kSuccess) {
            s.error = driverError(s.api, "cuDevicePrimaryCtxRetain failed:", retain_res);
            return false;
        }
        s.primary_context_retained = true;
    }

    if (!s.primary_context) {
        s.error = "no CUDA context available for RIFE kernels";
        return false;
    }
    if (!s.api.ctx_set_current) {
        s.error = "no CUDA driver cuCtxSetCurrent API";
        return false;
    }
    const CUresult set_res = s.api.ctx_set_current(s.primary_context);
    if (set_res != kSuccess) {
        s.error = driverError(s.api, "cuCtxSetCurrent failed:", set_res);
        return false;
    }
    return true;
}

bool ensureLoaded(RifePackState& s) {
    if (s.ready) return true;
    if (s.attempted) return false;
    s.attempted = true;

    if (!resolveDriver(s.api, &s.error)) {
        return false;
    }
    if (s.api.init) s.api.init(0);

    if (!bindPrimaryContext(s)) {
        return false;
    }

    const CUresult load_res = s.api.module_load_data(&s.module, rtx_ngx::kRifePackPtx);
    if (load_res != kSuccess) {
        s.error = driverError(s.api, "cuModuleLoadData failed for rife_pack:", load_res);
        return false;
    }

    /* Phase 9 kernel names first; legacy names remain as fallback aliases. */
    (void)s.api.module_get_function(&s.preprocess_kernel, s.module, "rife_nv12_to_nchw_7ch_fp32");
    if (!s.preprocess_kernel) {
        (void)s.api.module_get_function(&s.preprocess_kernel, s.module, "rife_preprocess_nv12_to_rgb");
    }
    (void)s.api.module_get_function(&s.postprocess_kernel, s.module, "rife_nchw_3ch_fp32_to_nv12");
    if (!s.postprocess_kernel) {
        (void)s.api.module_get_function(&s.postprocess_kernel, s.module, "rife_postprocess_rgb_to_nv12");
    }
    /* The separate-variant kernel is intentionally absent from the Phase 9
     * module; resolution failure here is not fatal. */
    (void)s.api.module_get_function(&s.preprocess_separate_kernel, s.module, "rife_preprocess_nv12_to_separate_rgb");

    /* Phase 9.1 (RW-P9-02): fp32<->fp16 conversion kernels backing the kHALF
     * engine I/O chain. */
    (void)s.api.module_get_function(&s.fp32_to_fp16_kernel, s.module, "rife_fp32_to_fp16");
    (void)s.api.module_get_function(&s.fp16_to_fp32_kernel, s.module, "rife_fp16_to_fp32");
    (void)s.api.module_get_function(&s.eval_block_sad_kernel, s.module, "rtx_eval_block_sad_8x8");

    if (!s.preprocess_kernel || !s.postprocess_kernel ||
        !s.fp32_to_fp16_kernel || !s.fp16_to_fp32_kernel) {
        s.error = "failed to locate RIFE kernels in loaded module";
        return false;
    }

    s.ready = true;
    s.error.clear();
    return true;
}

}  // namespace

namespace rtx_ngx {

bool isRifePackAvailable() {
    RifePackState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return ensureLoaded(s);
}

const char* rifePackLastError() {
    RifePackState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.error.c_str();
}

RtxResult launchRifePreprocess(const RifePreprocessLaunch& launch, void* stream) {
    /* Phase 9 ABI: prev/curr are single NV12 base pointers; the UV plane is
     * derived in-kernel from base + height * pitch. */
    if (launch.y_plane_0 == 0 || launch.y_plane_1 == 0 ||
        launch.output_tensor == 0 || launch.padded_width == 0 || launch.padded_height == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }

    RifePackState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!ensureLoaded(s) || !bindPrimaryContext(s)) {
        return RTX_ERR_NGX;
    }

    unsigned long long p_prev = launch.y_plane_0;
    unsigned long long p_curr = launch.y_plane_1;
    unsigned int p_pitch = launch.y_pitch;
    unsigned int p_w = launch.width;
    unsigned int p_h = launch.height;
    unsigned int p_padw = launch.padded_width;
    unsigned int p_padh = launch.padded_height;
    unsigned int p_full = static_cast<unsigned int>(launch.full_range);
    float p_alpha = launch.alpha;
    unsigned long long p_out = launch.output_tensor;
    unsigned long long p_sad = launch.d_sad_sum;

    void* args[] = {
        &p_prev, &p_curr, &p_pitch, &p_w, &p_h,
        &p_padw, &p_padh, &p_full, &p_alpha, &p_out, &p_sad
    };

    const unsigned block_x = 16;
    const unsigned block_y = 16;
    const unsigned grid_x = (launch.padded_width + block_x - 1) / block_x;
    const unsigned grid_y = (launch.padded_height + block_y - 1) / block_y;

    const CUresult res = s.api.launch_kernel(
        s.preprocess_kernel, grid_x, grid_y, 1, block_x, block_y, 1,
        0, reinterpret_cast<CUstream>(stream), args, nullptr);

    if (res != kSuccess) {
        s.error = driverError(s.api, "cuLaunchKernel failed for rife_preprocess:", res);
        return RTX_ERR_INTERNAL;
    }
    return RTX_OK;
}

RtxResult launchRifePreprocessSeparate(const RifePreprocessSeparateLaunch& launch, void* stream) {
    if (launch.y_plane_0 == 0 || launch.uv_plane_0 == 0 ||
        launch.y_plane_1 == 0 || launch.uv_plane_1 == 0 ||
        launch.img0_tensor == 0 || launch.img1_tensor == 0 ||
        launch.padded_width == 0 || launch.padded_height == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }

    RifePackState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!ensureLoaded(s) || !bindPrimaryContext(s)) {
        return RTX_ERR_NGX;
    }

    if (!s.preprocess_separate_kernel) {
        s.error = "rife_preprocess_nv12_to_separate_rgb is not in the Phase 9 PTX module";
        return RTX_ERR_UNSUPPORTED;
    }

    unsigned long long p_y0 = launch.y_plane_0;
    unsigned long long p_uv0 = launch.uv_plane_0;
    unsigned long long p_y1 = launch.y_plane_1;
    unsigned long long p_uv1 = launch.uv_plane_1;
    unsigned long long p_img0 = launch.img0_tensor;
    unsigned long long p_img1 = launch.img1_tensor;
    unsigned long long p_timestep = launch.timestep_tensor;
    unsigned int p_w = launch.width;
    unsigned int p_h = launch.height;
    unsigned int p_ypitch = launch.y_pitch;
    unsigned int p_uvpitch = launch.uv_pitch;
    unsigned int p_padw = launch.padded_width;
    unsigned int p_padh = launch.padded_height;
    float p_alpha = launch.alpha;
    unsigned int p_full = static_cast<unsigned int>(launch.full_range);
    unsigned long long p_sad = launch.d_sad_sum;

    void* args[] = {
        &p_y0, &p_uv0, &p_y1, &p_uv1, &p_img0, &p_img1, &p_timestep,
        &p_w, &p_h, &p_ypitch, &p_uvpitch,
        &p_padw, &p_padh, &p_alpha, &p_full, &p_sad
    };

    const unsigned block_x = 16;
    const unsigned block_y = 16;
    const unsigned grid_x = (launch.padded_width + block_x - 1) / block_x;
    const unsigned grid_y = (launch.padded_height + block_y - 1) / block_y;

    const CUresult res = s.api.launch_kernel(
        s.preprocess_separate_kernel, grid_x, grid_y, 1, block_x, block_y, 1,
        0, reinterpret_cast<CUstream>(stream), args, nullptr);

    if (res != kSuccess) {
        s.error = driverError(s.api, "cuLaunchKernel failed for rife_preprocess_separate:", res);
        return RTX_ERR_INTERNAL;
    }
    return RTX_OK;
}

RtxResult launchRifePostprocess(const RifePostprocessLaunch& launch, void* stream) {
    /* Phase 9 ABI: dst is a single NV12 base pointer (UV plane written at
     * base + height * pitch in-kernel). Phase 9.1 (RW-P9-05): the full_range
     * selector reaches the kernel and picks the color matrix uniformly. */
    if (launch.rgb_tensor == 0 || launch.y_plane == 0 ||
        launch.width == 0 || launch.height == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }

    RifePackState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!ensureLoaded(s) || !bindPrimaryContext(s)) {
        return RTX_ERR_NGX;
    }

    unsigned long long p_src = launch.rgb_tensor;
    unsigned long long p_dst = launch.y_plane;
    unsigned int p_pitch = launch.y_pitch;
    unsigned int p_w = launch.width;
    unsigned int p_h = launch.height;
    unsigned int p_padw = launch.padded_width;
    unsigned int p_padh = launch.padded_height;
    unsigned int p_full = static_cast<unsigned int>(launch.full_range);

    void* args[] = {
        &p_src, &p_dst, &p_pitch, &p_w, &p_h, &p_padw, &p_padh, &p_full
    };

    const unsigned block_x = 16;
    const unsigned block_y = 16;
    const unsigned grid_x = (launch.width + block_x - 1) / block_x;
    const unsigned grid_y = (launch.height + block_y - 1) / block_y;

    const CUresult res = s.api.launch_kernel(
        s.postprocess_kernel, grid_x, grid_y, 1, block_x, block_y, 1,
        0, reinterpret_cast<CUstream>(stream), args, nullptr);

    if (res != kSuccess) {
        s.error = driverError(s.api, "cuLaunchKernel failed for rife_postprocess:", res);
        return RTX_ERR_INTERNAL;
    }
    return RTX_OK;
}

/* Phase 9.1 (RW-P9-02): element-wise fp32 -> fp16 conversion launch.
 * Grid (count + 255) / 256, block 256 (one element per thread). */
RtxResult launchFp32ToFp16(uint64_t src_fp32, uint64_t dst_fp16, uint32_t count, void* stream) {
    if (src_fp32 == 0 || dst_fp16 == 0 || count == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }

    RifePackState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!ensureLoaded(s) || !bindPrimaryContext(s)) {
        return RTX_ERR_NGX;
    }
    if (!s.fp32_to_fp16_kernel) {
        s.error = "rife_fp32_to_fp16 is not in the PTX module";
        return RTX_ERR_UNSUPPORTED;
    }

    unsigned long long p_src = src_fp32;
    unsigned long long p_dst = dst_fp16;
    unsigned int p_count = count;
    void* args[] = {&p_src, &p_dst, &p_count};

    const unsigned block_x = 256;
    const unsigned grid_x = (count + block_x - 1) / block_x;

    const CUresult res = s.api.launch_kernel(
        s.fp32_to_fp16_kernel, grid_x, 1, 1, block_x, 1, 1,
        0, reinterpret_cast<CUstream>(stream), args, nullptr);

    if (res != kSuccess) {
        s.error = driverError(s.api, "cuLaunchKernel failed for rife_fp32_to_fp16:", res);
        return RTX_ERR_INTERNAL;
    }
    return RTX_OK;
}

/* Phase 9.1 (RW-P9-02): element-wise fp16 -> fp32 conversion launch.
 * Grid (count + 255) / 256, block 256 (one element per thread). */
RtxResult launchFp16ToFp32(uint64_t src_fp16, uint64_t dst_fp32, uint32_t count, void* stream) {
    if (src_fp16 == 0 || dst_fp32 == 0 || count == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }

    RifePackState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!ensureLoaded(s) || !bindPrimaryContext(s)) {
        return RTX_ERR_NGX;
    }
    if (!s.fp16_to_fp32_kernel) {
        s.error = "rife_fp16_to_fp32 is not in the PTX module";
        return RTX_ERR_UNSUPPORTED;
    }

    unsigned long long p_src = src_fp16;
    unsigned long long p_dst = dst_fp32;
    unsigned int p_count = count;
    void* args[] = {&p_src, &p_dst, &p_count};

    const unsigned block_x = 256;
    const unsigned grid_x = (count + block_x - 1) / block_x;

    const CUresult res = s.api.launch_kernel(
        s.fp16_to_fp32_kernel, grid_x, 1, 1, block_x, 1, 1,
        0, reinterpret_cast<CUstream>(stream), args, nullptr);

    if (res != kSuccess) {
        s.error = driverError(s.api, "cuLaunchKernel failed for rife_fp16_to_fp32:", res);
        return RTX_ERR_INTERNAL;
    }
    return RTX_OK;
}

RtxResult launchEvalBlockSad8x8(uint64_t prev_y, uint64_t curr_y, uint32_t pitch,
                               uint32_t width, uint32_t height,
                               uint64_t d_block_sad_64, void* stream) {
    if (prev_y == 0 || curr_y == 0 || d_block_sad_64 == 0 || width == 0 || height == 0 || pitch == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    RifePackState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!ensureLoaded(s) || !bindPrimaryContext(s)) {
        return RTX_ERR_NGX;
    }
    if (!s.eval_block_sad_kernel) {
        s.error = "kernel rtx_eval_block_sad_8x8 is missing from rife_pack.ptx";
        return RTX_ERR_UNSUPPORTED;
    }

    unsigned long long p_prev = prev_y;
    unsigned long long p_curr = curr_y;
    int p_pitch = static_cast<int>(pitch);
    int p_width = static_cast<int>(width);
    int p_height = static_cast<int>(height);
    unsigned long long p_out = d_block_sad_64;

    void* args[] = { &p_prev, &p_curr, &p_pitch, &p_width, &p_height, &p_out };
    const unsigned block_x = 16;
    const unsigned block_y = 16;
    const unsigned grid_x = (width + block_x - 1) / block_x;
    const unsigned grid_y = (height + block_y - 1) / block_y;

    const CUresult res = s.api.launch_kernel(
        s.eval_block_sad_kernel, grid_x, grid_y, 1, block_x, block_y, 1, 0,
        reinterpret_cast<CUstream>(stream), args, nullptr);
    if (res != kSuccess) {
        s.error = driverError(s.api, "cuLaunchKernel failed for rtx_eval_block_sad_8x8:", res);
        return RTX_ERR_INTERNAL;
    }
    return RTX_OK;
}

}  // namespace rtx_ngx
