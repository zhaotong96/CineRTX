/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Runtime loader and launcher for RIFE pre/post-processing CUDA kernels.
 * Phase 12 (Wave 12.2-B1): zero named wrapper structs; reuses nv_cuda::*
 * free-function driver dispatch and structured bindings.
 */

#include "rife_pack_runtime.h"
#include "nv_func_tables.h"

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

namespace {

using namespace rtx_ngx;
constexpr NvCuResult kSuccess = 0;

std::mutex g_mutex;
NvCuModule g_module = nullptr;
NvCuFunction g_preprocess_kernel = nullptr;
NvCuFunction g_preprocess_separate_kernel = nullptr;
NvCuFunction g_postprocess_kernel = nullptr;
NvCuFunction g_fp32_to_fp16_kernel = nullptr;
NvCuFunction g_fp16_to_fp32_kernel = nullptr;
NvCuFunction g_eval_block_sad_kernel = nullptr;
NvCuContext g_primary_context = nullptr;
bool g_primary_context_retained = false;
bool g_attempted = false;
bool g_ready = false;
std::string g_error;

bool bindPrimaryContext() {
    NvCuContext current = nullptr;
    if (nv_cuda::ctxGetCurrent(&current) == kSuccess && current != nullptr) {
        return true;
    }
    if (!g_primary_context_retained) {
        int dev = 0;
        if (const NvCuResult dev_res = nv_cuda::deviceGet(&dev, 0); dev_res != kSuccess) {
            g_error = nv_cuda::formatError("cuDeviceGet failed:", dev_res);
            return false;
        }
        if (const NvCuResult retain_res = nv_cuda::primaryCtxRetain(&g_primary_context, dev); retain_res != kSuccess) {
            g_error = nv_cuda::formatError("cuDevicePrimaryCtxRetain failed:", retain_res);
            return false;
        }
        g_primary_context_retained = true;
    }
    if (!g_primary_context) {
        g_error = "no CUDA context available for RIFE kernels";
        return false;
    }
    if (const NvCuResult set_res = nv_cuda::ctxSetCurrent(g_primary_context); set_res != kSuccess) {
        g_error = nv_cuda::formatError("cuCtxSetCurrent failed:", set_res);
        return false;
    }
    return true;
}

bool ensureLoaded() {
    if (g_ready) return true;
    if (g_attempted) return false;
    g_attempted = true;

    if (!nv_cuda::driverAvailable()) {
        g_error = "nvcuda.dll is not available";
        return false;
    }
    if (!nv_cuda::moduleApiLoaded()) {
        g_error = "CUDA driver is missing required module/launch APIs";
        return false;
    }
    (void)nv_cuda::init(0);
    if (!bindPrimaryContext()) return false;

    if (const NvCuResult load_res = nv_cuda::moduleLoadData(&g_module, kRifePackPtx); load_res != kSuccess) {
        g_error = nv_cuda::formatError("cuModuleLoadData failed for rife_pack:", load_res);
        return false;
    }
    (void)nv_cuda::moduleGetFunction(&g_preprocess_kernel, g_module, "rife_nv12_to_nchw_7ch_fp32");
    if (!g_preprocess_kernel) {
        (void)nv_cuda::moduleGetFunction(&g_preprocess_kernel, g_module, "rife_preprocess_nv12_to_rgb");
    }
    (void)nv_cuda::moduleGetFunction(&g_postprocess_kernel, g_module, "rife_nchw_3ch_fp32_to_nv12");
    if (!g_postprocess_kernel) {
        (void)nv_cuda::moduleGetFunction(&g_postprocess_kernel, g_module, "rife_postprocess_rgb_to_nv12");
    }
    (void)nv_cuda::moduleGetFunction(&g_preprocess_separate_kernel, g_module, "rife_preprocess_nv12_to_separate_rgb");
    (void)nv_cuda::moduleGetFunction(&g_fp32_to_fp16_kernel, g_module, "rife_fp32_to_fp16");
    (void)nv_cuda::moduleGetFunction(&g_fp16_to_fp32_kernel, g_module, "rife_fp16_to_fp32");
    (void)nv_cuda::moduleGetFunction(&g_eval_block_sad_kernel, g_module, "rtx_eval_block_sad_8x8");

    if (!g_preprocess_kernel || !g_postprocess_kernel ||
        !g_fp32_to_fp16_kernel || !g_fp16_to_fp32_kernel) {
        g_error = "failed to locate RIFE kernels in loaded module";
        return false;
    }
    g_ready = true;
    g_error.clear();
    return true;
}

RtxResult launchChecked(NvCuFunction fn, const char* name,
                        unsigned gx, unsigned gy, unsigned bx, unsigned by,
                        void* stream, void** args) {
    const NvCuResult res = nv_cuda::launchKernel(fn, gx, gy, 1, bx, by, 1, 0, stream, args, nullptr);
    if (res != kSuccess) {
        g_error = nv_cuda::formatError((std::string("cuLaunchKernel failed for ") + name + ":").c_str(), res);
        return RTX_ERR_INTERNAL;
    }
    return RTX_OK;
}

RtxResult launchConvert1D(NvCuFunction& fn, const char* name, const char* missing_msg,
                          uint64_t src, uint64_t dst, uint32_t count, void* stream) {
    if (src == 0 || dst == 0 || count == 0) return RTX_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureLoaded() || !bindPrimaryContext()) return RTX_ERR_NGX;
    if (!fn) {
        g_error = missing_msg;
        return RTX_ERR_UNSUPPORTED;
    }
    unsigned long long p_src = src, p_dst = dst;
    unsigned int p_count = count;
    void* args[] = {&p_src, &p_dst, &p_count};
    return launchChecked(fn, name, (count + 255u) / 256u, 1, 256, 1, stream, args);
}

}  // namespace

namespace rtx_ngx {

bool isRifePackAvailable() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return ensureLoaded();
}

const char* rifePackLastError() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_error.c_str();
}

RtxResult launchRifePreprocess(
    const std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                     uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                     float, int32_t, uint64_t, int32_t, uint32_t, uint32_t>& launch,
    void* stream) {
    const auto& [y0, uv0, y1, uv1, out_tensor, w, h, y_pitch, uv_pitch, pad_w, pad_h,
                 alpha, full_range, d_sad, reflect_pad, infer_w, infer_h] = launch;
    (void)uv0; (void)uv1; (void)uv_pitch; (void)reflect_pad;
    if (y0 == 0 || y1 == 0 || out_tensor == 0 || pad_w == 0 || pad_h == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureLoaded() || !bindPrimaryContext()) return RTX_ERR_NGX;
    unsigned long long p_prev = y0, p_curr = y1, p_out = out_tensor, p_sad = d_sad;
    unsigned int p_pitch = y_pitch, p_w = w, p_h = h, p_padw = pad_w, p_padh = pad_h;
    unsigned int p_full = static_cast<unsigned int>(full_range);
    float p_alpha = alpha;
    unsigned int p_infer_w = (infer_w > 0u) ? infer_w : w;
    unsigned int p_infer_h = (infer_h > 0u) ? infer_h : h;
    void* args[] = {&p_prev, &p_curr, &p_pitch, &p_w, &p_h, &p_padw, &p_padh,
                    &p_full, &p_alpha, &p_out, &p_sad, &p_infer_w, &p_infer_h};
    return launchChecked(g_preprocess_kernel, "rife_preprocess",
                         (pad_w + 15u) / 16u, (pad_h + 15u) / 16u, 16, 16, stream, args);
}

RtxResult launchRifePreprocessSeparate(
    const std::tuple<uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t,
                     uint64_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                     float, int32_t, uint64_t, int32_t>& launch,
    void* stream) {
    const auto& [y0, uv0, y1, uv1, img0, img1, timestep, w, h, y_pitch, uv_pitch,
                 pad_w, pad_h, alpha, full_range, d_sad, reflect_pad] = launch;
    (void)reflect_pad;
    if (y0 == 0 || uv0 == 0 || y1 == 0 || uv1 == 0 || img0 == 0 || img1 == 0 ||
        pad_w == 0 || pad_h == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureLoaded() || !bindPrimaryContext()) return RTX_ERR_NGX;
    if (!g_preprocess_separate_kernel) {
        g_error = "rife_preprocess_nv12_to_separate_rgb is not in the Phase 9 PTX module";
        return RTX_ERR_UNSUPPORTED;
    }
    unsigned long long p_y0 = y0, p_uv0 = uv0, p_y1 = y1, p_uv1 = uv1;
    unsigned long long p_img0 = img0, p_img1 = img1, p_timestep = timestep, p_sad = d_sad;
    unsigned int p_w = w, p_h = h, p_ypitch = y_pitch, p_uvpitch = uv_pitch;
    unsigned int p_padw = pad_w, p_padh = pad_h, p_full = static_cast<unsigned int>(full_range);
    float p_alpha = alpha;
    void* args[] = {&p_y0, &p_uv0, &p_y1, &p_uv1, &p_img0, &p_img1, &p_timestep,
                    &p_w, &p_h, &p_ypitch, &p_uvpitch, &p_padw, &p_padh, &p_alpha, &p_full, &p_sad};
    return launchChecked(g_preprocess_separate_kernel, "rife_preprocess_separate",
                         (pad_w + 15u) / 16u, (pad_h + 15u) / 16u, 16, 16, stream, args);
}

RtxResult launchRifePostprocess(
    const std::tuple<uint64_t, uint64_t, uint64_t, uint32_t, uint32_t, uint32_t, uint32_t,
                     uint32_t, uint32_t, int32_t, uint32_t, uint32_t>& launch,
    void* stream) {
    const auto& [rgb, y_plane, uv_plane, w, h, y_pitch, uv_pitch,
                 pad_w, pad_h, full_range, infer_w, infer_h] = launch;
    (void)uv_plane; (void)uv_pitch;
    if (rgb == 0 || y_plane == 0 || w == 0 || h == 0) return RTX_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureLoaded() || !bindPrimaryContext()) return RTX_ERR_NGX;
    unsigned long long p_src = rgb, p_dst = y_plane;
    unsigned int p_pitch = y_pitch, p_w = w, p_h = h, p_padw = pad_w, p_padh = pad_h;
    unsigned int p_full = static_cast<unsigned int>(full_range);
    unsigned int p_infer_w = (infer_w > 0u) ? infer_w : w;
    unsigned int p_infer_h = (infer_h > 0u) ? infer_h : h;
    void* args[] = {&p_src, &p_dst, &p_pitch, &p_w, &p_h, &p_padw, &p_padh,
                    &p_full, &p_infer_w, &p_infer_h};
    return launchChecked(g_postprocess_kernel, "rife_postprocess",
                         (w + 15u) / 16u, (h + 15u) / 16u, 16, 16, stream, args);
}

RtxResult launchFp32ToFp16(uint64_t src_fp32, uint64_t dst_fp16, uint32_t count, void* stream) {
    return launchConvert1D(g_fp32_to_fp16_kernel, "rife_fp32_to_fp16",
                           "rife_fp32_to_fp16 is not in the PTX module",
                           src_fp32, dst_fp16, count, stream);
}

RtxResult launchFp16ToFp32(uint64_t src_fp16, uint64_t dst_fp32, uint32_t count, void* stream) {
    return launchConvert1D(g_fp16_to_fp32_kernel, "rife_fp16_to_fp32",
                           "rife_fp16_to_fp32 is not in the PTX module",
                           src_fp16, dst_fp32, count, stream);
}

RtxResult launchEvalBlockSad8x8(uint64_t prev_y, uint64_t curr_y, uint32_t pitch,
                                uint32_t width, uint32_t height,
                                uint64_t d_block_sad_64, void* stream) {
    if (prev_y == 0 || curr_y == 0 || d_block_sad_64 == 0 || width == 0 || height == 0 || pitch == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureLoaded() || !bindPrimaryContext()) return RTX_ERR_NGX;
    if (!g_eval_block_sad_kernel) {
        g_error = "kernel rtx_eval_block_sad_8x8 is missing from rife_pack.ptx";
        return RTX_ERR_UNSUPPORTED;
    }
    (void)nv_cuda::memsetD32Async(static_cast<NvCuDevicePtr>(d_block_sad_64), 0, 64, stream);
    unsigned long long p_prev = prev_y, p_curr = curr_y, p_out = d_block_sad_64;
    int p_pitch = static_cast<int>(pitch), p_width = static_cast<int>(width), p_height = static_cast<int>(height);
    void* args[] = {&p_prev, &p_curr, &p_pitch, &p_width, &p_height, &p_out};
    return launchChecked(g_eval_block_sad_kernel, "rtx_eval_block_sad_8x8",
                         (width + 15u) / 16u, (height + 15u) / 16u, 16, 16, stream, args);
}

}  // namespace rtx_ngx
