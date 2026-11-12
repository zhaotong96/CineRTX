/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Runtime loader and launcher for the RGBA16F -> P010 pack kernel.
 *
 * The kernel ships as PTX text (p010_pack_ptx.h) and is JIT-compiled by the
 * CUDA driver through cuModuleLoadData.  Nothing here links cudart64_*.dll, so
 * the import-table audit stays at zero CUDA runtime imports.  When nvcc is
 * available at build time, CMake regenerates the PTX from p010_pack.cu; the
 * checked-in copy is the fallback for machines without a CUDA toolkit.
 */

#include "ngx_bridge_abi.h"

#include <cstring>
#include <mutex>
#include <string>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverlength-strings"
#endif
#include "p010_pack_ptx.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "p010_pack_runtime.h"

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
    // nvcuda.dll is the driver, not the runtime; it is present on any machine
    // with an NVIDIA display driver.
    HMODULE module = LoadLibraryExW(L"nvcuda.dll", nullptr,
                                    LOAD_LIBRARY_SEARCH_SYSTEM32);
    return reinterpret_cast<void*>(module);
}

void* resolveSymbol(void* library, const char* name) {
    return reinterpret_cast<void*>(
        GetProcAddress(reinterpret_cast<HMODULE>(library), name));
}
#else
void* openCudaDriver() { return dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL); }
void* resolveSymbol(void* library, const char* name) { return dlsym(library, name); }
#endif

bool resolveDriver(DriverApi& api, std::string* error) {
    void* library = openCudaDriver();
    if (library == nullptr) {
        *error = "nvcuda.dll is not available";
        return false;
    }
    api.module_load_data =
        reinterpret_cast<decltype(api.module_load_data)>(resolveSymbol(library, "cuModuleLoadData"));
    api.module_get_function = reinterpret_cast<decltype(api.module_get_function)>(
        resolveSymbol(library, "cuModuleGetFunction"));
    api.module_unload =
        reinterpret_cast<decltype(api.module_unload)>(resolveSymbol(library, "cuModuleUnload"));
    api.launch_kernel = reinterpret_cast<decltype(api.launch_kernel)>(
        resolveSymbol(library, "cuLaunchKernel"));
    api.get_error_name = reinterpret_cast<decltype(api.get_error_name)>(
        resolveSymbol(library, "cuGetErrorName"));
    api.init = reinterpret_cast<decltype(api.init)>(resolveSymbol(library, "cuInit"));
    api.device_get = reinterpret_cast<decltype(api.device_get)>(resolveSymbol(library, "cuDeviceGet"));
    api.ctx_get_current = reinterpret_cast<decltype(api.ctx_get_current)>(
        resolveSymbol(library, "cuCtxGetCurrent"));
    api.ctx_set_current = reinterpret_cast<decltype(api.ctx_set_current)>(
        resolveSymbol(library, "cuCtxSetCurrent"));
    api.primary_ctx_retain = reinterpret_cast<decltype(api.primary_ctx_retain)>(
        resolveSymbol(library, "cuDevicePrimaryCtxRetain"));
    if (api.module_load_data == nullptr || api.module_get_function == nullptr ||
        api.module_unload == nullptr || api.launch_kernel == nullptr) {
        *error = "CUDA driver is missing the module API (cuModuleLoadData)";
        return false;
    }
    return true;
}

struct PackState {
    std::mutex mutex;
    DriverApi api{};
    CUmodule module = nullptr;
    CUfunction kernel = nullptr;
    // P0-1.3: the remaster chain adds the preprocess kernel (NV12/P010 ->
    // R10G10B10A2) and the pack v2 kernel (TrueHDR scRGB -> BT.2020 PQ P010).
    CUfunction preprocess_kernel = nullptr;
    CUfunction pack_v2_kernel = nullptr;
    // Phase 9: the headless fused remaster kernel (optional in older PTX).
    CUfunction fused_remaster_kernel = nullptr;
    CUmodule interp_module = nullptr;
    CUfunction interp_kernel = nullptr;
    // The primary context retained at load time.  CUcontext follows strict
    // thread-local binding semantics: a worker thread that never ran the load
    // path has no current context, and D3D12 calls can detach the context at
    // any time.  The handle is persisted so every launch can re-bind it.
    void* primary_context = nullptr;
    bool attempted = false;
    bool ready = false;
    std::string error;
};

PackState& state() {
    static PackState instance;
    return instance;
}

// Binds the retained primary context to the calling thread.  Returns false
// with a diagnostic when the driver refuses, instead of letting the launch
// fail later with a bare CUDA_ERROR_INVALID_CONTEXT.
bool bindPrimaryContext(PackState& pack) {
    if (pack.api.ctx_set_current == nullptr || pack.primary_context == nullptr) {
        pack.error = "the CUDA driver lacks the context API needed to bind the pack kernel";
        return false;
    }
    const CUresult bound = pack.api.ctx_set_current(pack.primary_context);
    if (bound != kSuccess) {
        const char* name = "CUDA error";
        if (pack.api.get_error_name != nullptr) {
            (void)pack.api.get_error_name(bound, &name);
        }
        pack.error = std::string("cuCtxSetCurrent failed before the pack launch: ") + name;
        return false;
    }
    return true;
}

bool ensureLoaded(PackState& pack) {
    if (pack.attempted) {
        return pack.ready;
    }
    pack.attempted = true;
    if (!resolveDriver(pack.api, &pack.error)) {
        return false;
    }
    // cuModuleLoadData needs an initialised driver and a current context.  In
    // the transcode pipeline the shim has already made the primary context
    // current; a standalone caller (the pack self-test) has not.
    if (pack.api.init != nullptr) {
        const CUresult initialised = pack.api.init(0);
        if (initialised != kSuccess) {
            pack.error = "cuInit failed with CUDA error " + std::to_string(initialised);
            return false;
        }
    }
    void* current = nullptr;
    if (pack.api.ctx_get_current != nullptr) {
        (void)pack.api.ctx_get_current(&current);
    }
    if (current == nullptr && pack.api.device_get != nullptr &&
        pack.api.primary_ctx_retain != nullptr && pack.api.ctx_set_current != nullptr) {
        int device = 0;
        if (pack.api.device_get(&device, 0) == kSuccess) {
            void* primary = nullptr;
            if (pack.api.primary_ctx_retain(&primary, device) == kSuccess) {
                (void)pack.api.ctx_set_current(primary);
                // The retain is intentionally kept for the process lifetime:
                // the module and the context must outlive every launch.
                pack.primary_context = primary;
            }
        }
    } else if (current != nullptr) {
        // A context is already current on the loading thread; remember it so
        // later launches on other threads can re-bind the same one.
        pack.primary_context = current;
    }
    const CUresult loaded =
        pack.api.module_load_data(&pack.module, static_cast<const void*>(rtx_ngx::kP010PackPtx));
    if (loaded != kSuccess) {
        pack.error = "cuModuleLoadData failed with CUDA error " + std::to_string(loaded) +
                     " (PTX JIT rejected the embedded kernel)";
        return false;
    }
    const CUresult found = pack.api.module_get_function(
        &pack.kernel, pack.module, "rtx_rgba16f_to_p010_bt2020nc");
    if (found != kSuccess || pack.kernel == nullptr) {
        pack.error = "kernel rtx_rgba16f_to_p010_bt2020nc is missing from the PTX module";
        pack.api.module_unload(pack.module);
        pack.module = nullptr;
        return false;
    }
    // The remaster kernels were added with the v2 ABI bridge; a PTX that
    // predates them still supports the legacy pack path, so their absence is
    // only fatal for the remaster entry (reported by launchPreprocess /
    // launchPackV2 through RTX_ERR_UNSUPPORTED).
    const CUresult preprocess_found = pack.api.module_get_function(
        &pack.preprocess_kernel, pack.module, "rtx_yuv_to_rgb10");
    const CUresult pack2_found = pack.api.module_get_function(
        &pack.pack_v2_kernel, pack.module, "rtx_rgba16f_to_p010_bt2020nc_v2");
    if (preprocess_found != kSuccess || pack2_found != kSuccess) {
        pack.preprocess_kernel = preprocess_found == kSuccess ? pack.preprocess_kernel : nullptr;
        pack.pack_v2_kernel = pack2_found == kSuccess ? pack.pack_v2_kernel : nullptr;
    }
    // Phase 9: the fused Linux remaster kernel is optional in the same way.
    CUfunction fused_kernel = nullptr;
    const CUresult fused_found = pack.api.module_get_function(
        &fused_kernel, pack.module, "rtx_cuda_remaster_fused_p010");
    pack.fused_remaster_kernel = fused_found == kSuccess ? fused_kernel : nullptr;
    pack.ready = true;
    return true;
}

}  // namespace

namespace rtx_ngx {

const char* packLastError() {
    return rtx_ngx_pack_last_error();
}

RtxResult launchPreprocess(const PreprocessLaunch& launch, void* stream) {
    if (launch.y_plane == 0 || launch.uv_plane == 0 || launch.output_surface == 0 ||
        launch.width <= 0 || launch.height <= 0 || launch.y_pitch_bytes <= 0 ||
        launch.uv_pitch_bytes <= 0 ||
        (launch.format != RTX_PIXEL_NV12 && launch.format != RTX_PIXEL_P010)) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    PackState& pack = state();
    std::lock_guard<std::mutex> lock(pack.mutex);
    if (!ensureLoaded(pack)) {
        return RTX_ERR_NGX;
    }
    if (pack.preprocess_kernel == nullptr) {
        pack.error = "kernel rtx_yuv_to_rgb10 is missing from the PTX module";
        return RTX_ERR_UNSUPPORTED;
    }
    if (!bindPrimaryContext(pack)) {
        return RTX_ERR_INTERNAL;
    }

    unsigned long long y_plane = launch.y_plane;
    unsigned long long uv_plane = launch.uv_plane;
    unsigned long long surface = launch.output_surface;
    int width = launch.width;
    int height = launch.height;
    int y_pitch = launch.y_pitch_bytes;
    int uv_pitch = launch.uv_pitch_bytes;
    int format = launch.format == RTX_PIXEL_P010 ? 2 : 1;
    int full_range = launch.full_range != 0 ? 1 : 0;
    int matrix = launch.matrix;

    void* arguments[] = {&y_plane, &uv_plane, &surface, &width,    &height,
                         &y_pitch, &uv_pitch,  &format,  &full_range, &matrix};
    const unsigned block = 16;
    const unsigned grid_x = (static_cast<unsigned>(width) + block - 1u) / block;
    const unsigned grid_y = (static_cast<unsigned>(height) + block - 1u) / block;
    const CUresult launched =
        pack.api.launch_kernel(pack.preprocess_kernel, grid_x, grid_y, 1, block, block, 1, 0,
                               reinterpret_cast<CUstream>(stream), arguments, nullptr);
    if (launched != kSuccess) {
        const char* name = "CUDA error";
        if (pack.api.get_error_name != nullptr) {
            (void)pack.api.get_error_name(launched, &name);
        }
        pack.error = std::string("cuLaunchKernel failed for rtx_yuv_to_rgb10: ") + name;
        return RTX_ERR_INTERNAL;
    }
    return RTX_OK;
}

RtxResult launchPackV2(const PackV2Launch& launch, void* stream) {
    if (launch.input_surface == 0 || launch.y_plane == 0 || launch.uv_plane == 0 ||
        launch.width <= 0 || launch.height <= 0 || (launch.width & 1) != 0 ||
        (launch.height & 1) != 0 || launch.y_pitch_samples == 0 ||
        launch.uv_pitch_samples == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    PackState& pack = state();
    std::lock_guard<std::mutex> lock(pack.mutex);
    if (!ensureLoaded(pack)) {
        return RTX_ERR_NGX;
    }
    if (pack.pack_v2_kernel == nullptr) {
        pack.error = "kernel rtx_rgba16f_to_p010_bt2020nc_v2 is missing from the PTX module";
        return RTX_ERR_UNSUPPORTED;
    }
    if (!bindPrimaryContext(pack)) {
        return RTX_ERR_INTERNAL;
    }

    unsigned long long surface = launch.input_surface;
    unsigned long long y_plane = launch.y_plane;
    unsigned long long uv_plane = launch.uv_plane;
    int width = launch.width;
    int height = launch.height;
    int y_pitch = static_cast<int>(launch.y_pitch_samples);
    int uv_pitch = static_cast<int>(launch.uv_pitch_samples);
    int input_is_pq = launch.input_is_pq != 0 ? 1 : 0;
    int input_is_scrgb = launch.input_is_scrgb != 0 ? 1 : 0;
    float nits = launch.linear_scale_nits > 0.0f ? launch.linear_scale_nits : 80.0f;
    // Phase 7 fused stages.
    unsigned long long lut_tex = launch.lut_tex;
    int lut_dim = launch.lut_dim;
    unsigned long long stats_devptr = launch.stats_devptr;
    unsigned long long preview_ptr = launch.preview_ptr;
    int preview_width = launch.preview_width;
    int preview_height = launch.preview_height;
    int preview_pitch_px = launch.preview_pitch_px;
    float split_x_norm = launch.split_x_norm;
    unsigned long long src_surface = launch.src_surface;

    void* arguments[] = {&surface,   &y_plane,       &uv_plane,       &width,
                         &height,    &y_pitch,       &uv_pitch,       &input_is_pq,
                         &input_is_scrgb, &nits,     &lut_tex,        &lut_dim,
                         &stats_devptr, &preview_ptr, &preview_width, &preview_height,
                         &preview_pitch_px, &split_x_norm, &src_surface};
    const unsigned block = 16;
    const unsigned grid_x = (static_cast<unsigned>(width) / 2u + block - 1u) / block;
    const unsigned grid_y = (static_cast<unsigned>(height) / 2u + block - 1u) / block;
    const CUresult launched =
        pack.api.launch_kernel(pack.pack_v2_kernel, grid_x, grid_y, 1, block, block, 1, 0,
                               reinterpret_cast<CUstream>(stream), arguments, nullptr);
    if (launched != kSuccess) {
        const char* name = "CUDA error";
        if (pack.api.get_error_name != nullptr) {
            (void)pack.api.get_error_name(launched, &name);
        }
        pack.error = std::string("cuLaunchKernel failed for rtx_rgba16f_to_p010_bt2020nc_v2: ") +
                     name;
        return RTX_ERR_INTERNAL;
    }
    return RTX_OK;
}

RtxResult launchFusedRemaster(const FusedRemasterLaunch& launch, void* stream) {
    if (launch.src_y == 0 || launch.src_uv == 0 || launch.dst_y == 0 || launch.dst_uv == 0 ||
        launch.src_width <= 0 || launch.src_height <= 0 || launch.dst_width <= 0 ||
        launch.dst_height <= 0 || launch.src_y_pitch == 0 || launch.src_uv_pitch == 0 ||
        launch.dst_y_pitch_samples == 0 || launch.dst_uv_pitch_samples == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    PackState& pack = state();
    std::lock_guard<std::mutex> lock(pack.mutex);
    if (!ensureLoaded(pack)) {
        return RTX_ERR_NGX;
    }
    if (pack.fused_remaster_kernel == nullptr) {
        pack.error = "kernel rtx_cuda_remaster_fused_p010 is missing from the PTX module";
        return RTX_ERR_UNSUPPORTED;
    }
    if (!bindPrimaryContext(pack)) {
        return RTX_ERR_INTERNAL;
    }

    unsigned long long src_y = launch.src_y;
    unsigned long long src_uv = launch.src_uv;
    int src_y_pitch = static_cast<int>(launch.src_y_pitch);
    int src_uv_pitch = static_cast<int>(launch.src_uv_pitch);
    int src_format = launch.src_format;
    int src_full_range = launch.src_full_range;
    int src_matrix = launch.src_matrix;
    unsigned long long dst_y = launch.dst_y;
    unsigned long long dst_uv = launch.dst_uv;
    int dst_y_pitch = static_cast<int>(launch.dst_y_pitch_samples);
    int dst_uv_pitch = static_cast<int>(launch.dst_uv_pitch_samples);
    int src_width = launch.src_width;
    int src_height = launch.src_height;
    int dst_width = launch.dst_width;
    int dst_height = launch.dst_height;
    unsigned long long lut_tex = launch.lut_tex;
    int lut_dim = launch.lut_dim;
    int vsr_quality = launch.vsr_quality;
    int enable_itm = launch.enable_itm;
    float max_luminance = launch.max_luminance;
    float y_norm_prev = launch.y_norm_prev;
    unsigned long long stats_devptr = launch.stats_devptr;

    void* arguments[] = {&src_y,      &src_uv,       &src_y_pitch,  &src_uv_pitch,
                         &src_format, &src_full_range, &src_matrix, &dst_y,
                         &dst_uv,     &dst_y_pitch,  &dst_uv_pitch, &src_width,
                         &src_height, &dst_width,    &dst_height,   &lut_tex,
                         &lut_dim,    &vsr_quality,  &enable_itm,   &max_luminance,
                         &y_norm_prev, &stats_devptr};
    const unsigned block = 16;
    const unsigned grid_x = (static_cast<unsigned>(launch.dst_width) / 2u + block - 1u) / block;
    const unsigned grid_y = (static_cast<unsigned>(launch.dst_height) / 2u + block - 1u) / block;
    const CUresult launched =
        pack.api.launch_kernel(pack.fused_remaster_kernel, grid_x, grid_y, 1, block, block, 1, 0,
                               reinterpret_cast<CUstream>(stream), arguments, nullptr);
    if (launched != kSuccess) {
        const char* name = "CUDA error";
        if (pack.api.get_error_name != nullptr) {
            (void)pack.api.get_error_name(launched, &name);
        }
        pack.error = std::string("cuLaunchKernel failed for rtx_cuda_remaster_fused_p010: ") +
                     name;
        return RTX_ERR_INTERNAL;
    }
    return RTX_OK;
}

}  // namespace rtx_ngx

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_pack_available(void) {
    PackState& pack = state();
    std::lock_guard<std::mutex> lock(pack.mutex);
    return ensureLoaded(pack) ? RTX_OK : RTX_ERR_NGX;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_pack_launch(
    const RtxNgxPackSurfaces* surfaces, const RtxNgxPackConfig* config, uint64_t cuda_stream) {
    if (surfaces == nullptr || config == nullptr || surfaces->input_surface == 0 ||
        surfaces->y_plane == 0 || surfaces->uv_plane == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    if (config->width <= 0 || config->height <= 0 || (config->width & 1) != 0 ||
        (config->height & 1) != 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    PackState& pack = state();
    std::lock_guard<std::mutex> lock(pack.mutex);
    if (!ensureLoaded(pack)) {
        return RTX_ERR_NGX;
    }
    // Expert hardening (RTX verdict 3.1): CUcontext is thread-local.  The
    // scheduler's pack worker thread is not the thread that loaded the module,
    // and D3D12 calls can detach the context at any time, so re-bind the
    // retained primary context before every launch instead of relying on the
    // load-time binding.
    if (!bindPrimaryContext(pack)) {
        return RTX_ERR_INTERNAL;
    }

    unsigned long long surface = surfaces->input_surface;
    unsigned long long y_plane = surfaces->y_plane;
    unsigned long long uv_plane = surfaces->uv_plane;
    int width = config->width;
    int height = config->height;
    int y_pitch = static_cast<int>(surfaces->y_pitch_samples);
    int uv_pitch = static_cast<int>(surfaces->uv_pitch_samples);
    int input_is_pq = config->input_is_pq != 0 ? 1 : 0;

    void* arguments[] = {&surface, &y_plane, &uv_plane, &width, &height,
                         &y_pitch, &uv_pitch, &input_is_pq};
    const unsigned block = 16;
    const unsigned grid_x = (static_cast<unsigned>(width) / 2u + block - 1u) / block;
    const unsigned grid_y = (static_cast<unsigned>(height) / 2u + block - 1u) / block;
    const CUresult launched =
        pack.api.launch_kernel(pack.kernel, grid_x, grid_y, 1, block, block, 1, 0,
                               reinterpret_cast<CUstream>(static_cast<uintptr_t>(cuda_stream)),
                               arguments, nullptr);
    if (launched != kSuccess) {
        const char* name = "CUDA error";
        if (pack.api.get_error_name != nullptr) {
            (void)pack.api.get_error_name(launched, &name);
        }
        pack.error = std::string("cuLaunchKernel failed for rtx_rgba16f_to_p010_bt2020nc: ") + name;
        return RTX_ERR_INTERNAL;
    }
    return RTX_OK;
}

static const char kInterpolatePtx[] =
    ".version 7.0\n"
    ".target sm_50\n"
    ".address_size 64\n"
    ".visible .entry rtx_interpolate_nv12(\n"
    "    .param .u64 prev_ptr,\n"
    "    .param .u64 curr_ptr,\n"
    "    .param .u32 pitch,\n"
    "    .param .u32 width,\n"
    "    .param .u32 height,\n"
    "    .param .f32 alpha,\n"
    "    .param .u64 out_ptr\n"
    ")\n"
    "{\n"
    "    .reg .pred %p<3>;\n"
    "    .reg .b32 %r<12>;\n"
    "    .reg .b64 %rd<14>;\n"
    "    .reg .f32 %f<8>;\n"
    "    ld.param.u32 %r1, [pitch];\n"
    "    ld.param.u32 %r2, [width];\n"
    "    ld.param.u32 %r3, [height];\n"
    "    ld.param.f32 %f1, [alpha];\n"
    "    ld.param.u64 %rd1, [prev_ptr];\n"
    "    ld.param.u64 %rd2, [curr_ptr];\n"
    "    ld.param.u64 %rd3, [out_ptr];\n"
    "    mov.u32 %r4, %r3;\n"
    "    shr.u32 %r5, %r4, 1;\n"
    "    add.u32 %r6, %r4, %r5;\n"
    "    mov.u32 %r7, %ctaid.x;\n"
    "    mov.u32 %r8, %ntid.x;\n"
    "    mov.u32 %r9, %tid.x;\n"
    "    mad.lo.u32 %r10, %r7, %r8, %r9;\n"
    "    mov.u32 %r7, %ctaid.y;\n"
    "    mov.u32 %r8, %ntid.y;\n"
    "    mov.u32 %r9, %tid.y;\n"
    "    mad.lo.u32 %r11, %r7, %r8, %r9;\n"
    "    setp.ge.u32 %p1, %r10, %r2;\n"
    "    @%p1 bra DONE;\n"
    "    setp.ge.u32 %p2, %r11, %r6;\n"
    "    @%p2 bra DONE;\n"
    "    cvt.u64.u32 %rd4, %r11;\n"
    "    cvt.u64.u32 %rd5, %r1;\n"
    "    mul.wide.u32 %rd6, %r11, %r1;\n"
    "    cvt.u64.u32 %rd7, %r10;\n"
    "    add.u64 %rd8, %rd6, %rd7;\n"
    "    add.u64 %rd9, %rd1, %rd8;\n"
    "    add.u64 %rd10, %rd2, %rd8;\n"
    "    add.u64 %rd11, %rd3, %rd8;\n"
    "    ld.global.u8 %r1, [%rd9];\n"
    "    ld.global.u8 %r2, [%rd10];\n"
    "    cvt.rn.f32.u32 %f2, %r1;\n"
    "    cvt.rn.f32.u32 %f3, %r2;\n"
    "    sub.f32 %f4, 1.0, %f1;\n"
    "    mul.f32 %f5, %f4, %f2;\n"
    "    fma.rn.f32 %f6, %f1, %f3, %f5;\n"
    "    cvt.rni.u32.f32 %r3, %f6;\n"
    "    st.global.u8 [%rd11], %r3;\n"
    "DONE:\n"
    "    ret;\n"
    "}\n";

RtxResult rtx_ngx::launchInterpolateNv12(uint64_t prev_nv12, uint64_t curr_nv12,
                               uint32_t pitch, uint32_t width, uint32_t height,
                               float alpha, uint64_t out_nv12, void* stream) {
    if (prev_nv12 == 0 || curr_nv12 == 0 || out_nv12 == 0 || pitch == 0 || width == 0 || height == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    PackState& pack = state();
    std::lock_guard<std::mutex> lock(pack.mutex);
    if (!ensureLoaded(pack)) {
        return RTX_ERR_NGX;
    }
    if (!bindPrimaryContext(pack)) {
        return RTX_ERR_INTERNAL;
    }
    if (pack.interp_kernel == nullptr) {
        const CUresult res = pack.api.module_load_data(&pack.interp_module, kInterpolatePtx);
        if (res != kSuccess) {
            pack.error = "cuModuleLoadData failed for interpolate PTX: " + std::to_string(res);
            return RTX_ERR_NGX;
        }
        const CUresult fn_res = pack.api.module_get_function(&pack.interp_kernel, pack.interp_module, "rtx_interpolate_nv12");
        if (fn_res != kSuccess || pack.interp_kernel == nullptr) {
            pack.error = "cuModuleGetFunction failed for rtx_interpolate_nv12";
            return RTX_ERR_NGX;
        }
    }
    unsigned long long p_prev = prev_nv12;
    unsigned long long p_curr = curr_nv12;
    unsigned int p_pitch = pitch;
    unsigned int p_width = width;
    unsigned int p_height = height;
    float p_alpha = alpha;
    unsigned long long p_out = out_nv12;

    void* args[] = { &p_prev, &p_curr, &p_pitch, &p_width, &p_height, &p_alpha, &p_out };
    const unsigned block_x = 16;
    const unsigned block_y = 16;
    const unsigned total_rows = height + height / 2;
    const unsigned grid_x = (width + block_x - 1) / block_x;
    const unsigned grid_y = (total_rows + block_y - 1) / block_y;

    const CUresult launched = pack.api.launch_kernel(
        pack.interp_kernel, grid_x, grid_y, 1, block_x, block_y, 1, 0,
        reinterpret_cast<CUstream>(stream), args, nullptr);
    if (launched != kSuccess) {
        const char* name = "CUDA error";
        if (pack.api.get_error_name != nullptr) {
            (void)pack.api.get_error_name(launched, &name);
        }
        pack.error = std::string("cuLaunchKernel failed for rtx_interpolate_nv12: ") + name;
        return RTX_ERR_INTERNAL;
    }
    return RTX_OK;
}

RTX_SHIM_API const char* RTX_SHIM_CALL rtx_ngx_pack_last_error(void) {
    PackState& pack = state();
    std::lock_guard<std::mutex> lock(pack.mutex);
    return pack.error.c_str();
}
