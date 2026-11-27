/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Runtime loader and launcher for the RGBA16F -> P010 pack kernel.
 * Phase 12 (Wave 12.2-B1): zero named wrapper structs; reuses nv_cuda::*
 * free-function driver dispatch and structured bindings.
 */

#include "ngx_bridge_abi.h"
#include "nv_func_tables.h"

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

namespace {

using namespace rtx_ngx;
constexpr NvCuResult kSuccess = 0;

std::mutex g_mutex;
NvCuModule g_module = nullptr;
NvCuFunction g_kernel = nullptr;
NvCuFunction g_preprocess_kernel = nullptr;
NvCuFunction g_preprocess_buffer_kernel = nullptr;
NvCuFunction g_pack_v2_kernel = nullptr;
NvCuFunction g_fused_remaster_kernel = nullptr;
NvCuModule g_interp_module = nullptr;
NvCuFunction g_interp_kernel = nullptr;
NvCuContext g_primary_context = nullptr;
bool g_attempted = false;
bool g_ready = false;
std::string g_error;

bool bindPrimaryContext() {
    if (g_primary_context == nullptr) {
        g_error = "the CUDA driver lacks the context API needed to bind the pack kernel";
        return false;
    }
    const NvCuResult bound = nv_cuda::ctxSetCurrent(g_primary_context);
    if (bound != kSuccess) {
        g_error = nv_cuda::formatError("cuCtxSetCurrent failed before the pack launch:", bound);
        return false;
    }
    return true;
}

bool ensureLoaded() {
    if (g_attempted) return g_ready;
    g_attempted = true;
    if (!nv_cuda::driverAvailable()) {
        g_error = "nvcuda.dll is not available";
        return false;
    }
    if (!nv_cuda::moduleApiLoaded()) {
        g_error = "CUDA driver is missing the module API (cuModuleLoadData)";
        return false;
    }
    if (const NvCuResult initialised = nv_cuda::init(0); initialised != kSuccess) {
        g_error = "cuInit failed with CUDA error " + std::to_string(initialised);
        return false;
    }
    NvCuContext current = nullptr;
    (void)nv_cuda::ctxGetCurrent(&current);
    if (current == nullptr) {
        int device = 0;
        NvCuContext primary = nullptr;
        if (nv_cuda::deviceGet(&device, 0) == kSuccess &&
            nv_cuda::primaryCtxRetain(&primary, device) == kSuccess) {
            (void)nv_cuda::ctxSetCurrent(primary);
            g_primary_context = primary;
        }
    } else {
        g_primary_context = current;
    }
    const NvCuResult loaded = nv_cuda::moduleLoadData(&g_module, static_cast<const void*>(kP010PackPtx));
    if (loaded != kSuccess) {
        g_error = "cuModuleLoadData failed with CUDA error " + std::to_string(loaded) +
                  " (PTX JIT rejected the embedded kernel)";
        return false;
    }
    if (nv_cuda::moduleGetFunction(&g_kernel, g_module, "rtx_rgba16f_to_p010_bt2020nc") != kSuccess ||
        g_kernel == nullptr) {
        g_error = "kernel rtx_rgba16f_to_p010_bt2020nc is missing from the PTX module";
        (void)nv_cuda::moduleUnload(g_module);
        g_module = nullptr;
        return false;
    }
    auto loadOptional = [](NvCuFunction& out, const char* name) {
        NvCuFunction fn = nullptr;
        out = (nv_cuda::moduleGetFunction(&fn, g_module, name) == kSuccess) ? fn : nullptr;
    };
    loadOptional(g_preprocess_kernel, "rtx_yuv_to_rgb10");
    loadOptional(g_pack_v2_kernel, "rtx_rgba16f_to_p010_bt2020nc_v2");
    loadOptional(g_preprocess_buffer_kernel, "rtx_yuv_to_rgb10_buf");
    loadOptional(g_fused_remaster_kernel, "rtx_cuda_remaster_fused_p010");
    g_ready = true;
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

constexpr const char kNvofWarpPtx[] =
    ".version 7.5\n"
    ".target sm_75\n"
    ".address_size 64\n"
    ".visible .entry rtx_nvof_warp_nv12(\n"
    "    .param .u64 prev_ptr,\n"
    "    .param .u64 curr_ptr,\n"
    "    .param .u64 fwd_flow_ptr,\n"
    "    .param .u64 bwd_flow_ptr,\n"
    "    .param .u64 cost_ptr,\n"
    "    .param .u32 pitch,\n"
    "    .param .u32 width,\n"
    "    .param .u32 height,\n"
    "    .param .u32 grid_width,\n"
    "    .param .u32 grid_height,\n"
    "    .param .u32 flow_stride_bytes,\n"
    "    .param .u32 cost_stride_bytes,\n"
    "    .param .f32 alpha,\n"
    "    .param .u64 out_ptr\n"
    ")\n"
    "{\n"
    "    .reg .pred %p<10>;\n"
    "    .reg .b32 %r<44>;\n"
    "    .reg .b64 %rd<26>;\n"
    "    .reg .f32 %f<26>;\n"
    "    ld.param.u32 %r1, [pitch];\n"
    "    ld.param.u32 %r2, [width];\n"
    "    ld.param.u32 %r3, [height];\n"
    "    ld.param.u32 %r4, [grid_width];\n"
    "    ld.param.u32 %r5, [grid_height];\n"
    "    ld.param.u32 %r36, [flow_stride_bytes];\n"
    "    ld.param.u32 %r37, [cost_stride_bytes];\n"
    "    ld.param.f32 %f1, [alpha];\n"
    "    ld.param.u64 %rd1, [prev_ptr];\n"
    "    ld.param.u64 %rd2, [curr_ptr];\n"
    "    ld.param.u64 %rd3, [fwd_flow_ptr];\n"
    "    ld.param.u64 %rd5, [cost_ptr];\n"
    "    ld.param.u64 %rd6, [out_ptr];\n"
    "    mov.u32 %r6, %ctaid.x;\n"
    "    mov.u32 %r7, %ntid.x;\n"
    "    mov.u32 %r8, %tid.x;\n"
    "    mad.lo.u32 %r9, %r6, %r7, %r8;\n"
    "    mov.u32 %r10, %ctaid.y;\n"
    "    mov.u32 %r11, %ntid.y;\n"
    "    mov.u32 %r12, %tid.y;\n"
    "    mad.lo.u32 %r13, %r10, %r11, %r12;\n"
    "    shr.u32 %r14, %r3, 1;\n"
    "    add.u32 %r15, %r3, %r14;\n"
    "    setp.ge.u32 %p1, %r9, %r2;\n"
    "    @%p1 bra L_WARP_DONE;\n"
    "    setp.ge.u32 %p2, %r13, %r15;\n"
    "    @%p2 bra L_WARP_DONE;\n"
    "    mul.wide.u32 %rd7, %r13, %r1;\n"
    "    cvt.u64.u32 %rd8, %r9;\n"
    "    add.u64 %rd9, %rd7, %rd8;\n"
    "    add.u64 %rd10, %rd6, %rd9;\n"
    "    add.u64 %rd11, %rd1, %rd9;\n"
    "    add.u64 %rd12, %rd2, %rd9;\n"
    "    sub.f32 %f8, 0f3F800000, %f1;\n"
    "    setp.eq.u64 %p6, %rd3, 0;\n"
    "    @%p6 bra L_LINEAR_BLEND;\n"
    "    setp.lt.u32 %p3, %r13, %r3;\n"
    "    @!%p3 bra L_UV_GRID;\n"
    "    mov.u32 %r19, %r13;\n"
    "    shr.u32 %r16, %r9, 2;\n"
    "    shr.u32 %r17, %r13, 2;\n"
    "    bra L_CLAMP_GRID;\n"
    "L_UV_GRID:\n"
    "    sub.u32 %r18, %r13, %r3;\n"
    "    shl.b32 %r19, %r18, 1;\n"
    "    shr.u32 %r16, %r9, 2;\n"
    "    shr.u32 %r17, %r19, 2;\n"
    "L_CLAMP_GRID:\n"
    "    sub.u32 %r20, %r4, 1;\n"
    "    min.u32 %r16, %r16, %r20;\n"
    "    sub.u32 %r21, %r5, 1;\n"
    "    min.u32 %r17, %r17, %r21;\n"
    "    add.u32 %r38, %r17, 1;\n"
    "    min.u32 %r38, %r38, %r21;\n"
    "    setp.eq.u64 %p4, %rd5, 0;\n"
    "    @%p4 bra L_WARP_FLOW;\n"
    "    mad.lo.u32 %r22, %r17, %r37, %r16;\n"
    "    cvt.u64.u32 %rd13, %r22;\n"
    "    add.u64 %rd14, %rd5, %rd13;\n"
    "    ld.global.u8 %r23, [%rd14];\n"
    "    setp.gt.u32 %p5, %r23, 140;\n"
    "    @%p5 bra L_LINEAR_BLEND;\n"
    "L_WARP_FLOW:\n"
    "    shl.b32 %r24, %r16, 2;\n"
    "    mad.lo.u32 %r39, %r17, %r36, %r24;\n"
    "    cvt.u64.u32 %rd15, %r39;\n"
    "    add.u64 %rd16, %rd3, %rd15;\n"
    "    ld.global.s16 %r25, [%rd16];\n"
    "    ld.global.s16 %r26, [%rd16+2];\n"
    "    mad.lo.u32 %r40, %r38, %r36, %r24;\n"
    "    cvt.u64.u32 %rd22, %r40;\n"
    "    add.u64 %rd23, %rd3, %rd22;\n"
    "    ld.global.s16 %r30, [%rd23];\n"
    "    ld.global.s16 %r31, [%rd23+2];\n"
    "    and.b32 %r41, %r19, 3;\n"
    "    cvt.rn.f32.u32 %f19, %r41;\n"
    "    mul.f32 %f19, %f19, 0f3E800000;\n"
    "    sub.f32 %f20, 0f3F800000, %f19;\n"
    "    cvt.rn.f32.s32 %f2, %r25;\n"
    "    cvt.rn.f32.s32 %f17, %r30;\n"
    "    mul.f32 %f2, %f2, %f20;\n"
    "    mad.rn.f32 %f2, %f17, %f19, %f2;\n"
    "    mul.f32 %f2, %f2, 0f3D000000;\n"
    "    cvt.rn.f32.s32 %f3, %r26;\n"
    "    cvt.rn.f32.s32 %f18, %r31;\n"
    "    mul.f32 %f3, %f3, %f20;\n"
    "    mad.rn.f32 %f3, %f18, %f19, %f3;\n"
    "    mul.f32 %f3, %f3, 0f3D000000;\n"
    "    @!%p3 mul.f32 %f3, %f3, 0f3F000000;\n"
    "    cvt.rn.f32.u32 %f4, %r9;\n"
    "    cvt.rn.f32.u32 %f5, %r13;\n"
    "    mul.f32 %f6, %f2, %f1;\n"
    "    mul.f32 %f7, %f3, %f1;\n"
    "    mul.f32 %f9, %f2, %f8;\n"
    "    mul.f32 %f10, %f3, %f8;\n"
    "    sub.f32 %f11, %f4, %f6;\n"
    "    sub.f32 %f12, %f5, %f7;\n"
    "    add.f32 %f13, %f4, %f9;\n"
    "    add.f32 %f14, %f5, %f10;\n"
    "    sub.u32 %r20, %r2, 1;\n"
    "    selp.s32 %r32, 0, %r3, %p3;\n"
    "    sub.u32 %r33, %r3, 1;\n"
    "    sub.u32 %r21, %r15, 1;\n"
    "    selp.s32 %r34, %r33, %r21, %p3;\n"
    "    and.b32 %r35, %r9, 1;\n"
    "    cvt.rni.s32.f32 %r27, %f11;\n"
    "    max.s32 %r27, %r27, 0;\n"
    "    min.s32 %r27, %r27, %r20;\n"
    "    @!%p3 and.b32 %r27, %r27, -2;\n"
    "    @!%p3 or.b32 %r27, %r27, %r35;\n"
    "    cvt.rni.s32.f32 %r28, %f12;\n"
    "    max.s32 %r28, %r28, %r32;\n"
    "    min.s32 %r28, %r28, %r34;\n"
    "    mul.wide.u32 %rd17, %r28, %r1;\n"
    "    cvt.u64.u32 %rd18, %r27;\n"
    "    add.u64 %rd19, %rd17, %rd18;\n"
    "    add.u64 %rd20, %rd1, %rd19;\n"
    "    ld.global.u8 %r25, [%rd20];\n"
    "    cvt.rni.s32.f32 %r27, %f13;\n"
    "    max.s32 %r27, %r27, 0;\n"
    "    min.s32 %r27, %r27, %r20;\n"
    "    @!%p3 and.b32 %r27, %r27, -2;\n"
    "    @!%p3 or.b32 %r27, %r27, %r35;\n"
    "    cvt.rni.s32.f32 %r28, %f14;\n"
    "    max.s32 %r28, %r28, %r32;\n"
    "    min.s32 %r28, %r28, %r34;\n"
    "    mul.wide.u32 %rd17, %r28, %r1;\n"
    "    cvt.u64.u32 %rd18, %r27;\n"
    "    add.u64 %rd19, %rd17, %rd18;\n"
    "    add.u64 %rd21, %rd2, %rd19;\n"
    "    ld.global.u8 %r26, [%rd21];\n"
    "    cvt.rn.f32.u32 %f15, %r25;\n"
    "    cvt.rn.f32.u32 %f16, %r26;\n"
    "    mul.f32 %f15, %f15, %f8;\n"
    "    mad.rn.f32 %f15, %f16, %f1, %f15;\n"
    "    cvt.rni.u32.f32 %r23, %f15;\n"
    "    st.global.u8 [%rd10], %r23;\n"
    "    bra L_WARP_DONE;\n"
    "L_LINEAR_BLEND:\n"
    "    ld.global.u8 %r25, [%rd11];\n"
    "    ld.global.u8 %r26, [%rd12];\n"
    "    cvt.rn.f32.u32 %f15, %r25;\n"
    "    cvt.rn.f32.u32 %f16, %r26;\n"
    "    mul.f32 %f15, %f15, %f8;\n"
    "    mad.rn.f32 %f15, %f16, %f1, %f15;\n"
    "    cvt.rni.u32.f32 %r23, %f15;\n"
    "    st.global.u8 [%rd10], %r23;\n"
    "L_WARP_DONE:\n"
    "    ret;\n"
    "}\n";

}  // namespace

namespace rtx_ngx {

const char* packLastError() { return rtx_ngx_pack_last_error(); }

RtxResult launchPreprocess(
    const std::tuple<uint64_t, uint64_t, uint64_t, int32_t, int32_t,
                     int32_t, int32_t, int32_t, int32_t, int32_t>& launch,
    void* stream) {
    const auto& [in_y, in_uv, in_surf, in_w, in_h, in_yp, in_uvp, in_fmt, in_fr, in_mat] = launch;
    if (in_y == 0 || in_uv == 0 || in_surf == 0 || in_w <= 0 || in_h <= 0 ||
        in_yp <= 0 || in_uvp <= 0 || (in_fmt != RTX_PIXEL_NV12 && in_fmt != RTX_PIXEL_P010)) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureLoaded()) return RTX_ERR_NGX;
    if (g_preprocess_kernel == nullptr) {
        g_error = "kernel rtx_yuv_to_rgb10 is missing from the PTX module";
        return RTX_ERR_UNSUPPORTED;
    }
    if (!bindPrimaryContext()) return RTX_ERR_INTERNAL;
    unsigned long long y_plane = in_y, uv_plane = in_uv, surface = in_surf;
    int width = in_w, height = in_h, y_pitch = in_yp, uv_pitch = in_uvp;
    int format = (in_fmt == RTX_PIXEL_P010) ? 2 : 1;
    int full_range = (in_fr != 0) ? 1 : 0;
    int matrix = in_mat;
    void* args[] = {&y_plane, &uv_plane, &surface, &width, &height,
                    &y_pitch, &uv_pitch, &format, &full_range, &matrix};
    const unsigned gx = (static_cast<unsigned>(width) + 15u) / 16u;
    const unsigned gy = (static_cast<unsigned>(height) + 15u) / 16u;
    return launchChecked(g_preprocess_kernel, "rtx_yuv_to_rgb10", gx, gy, 16, 16, stream, args);
}

RtxResult launchPreprocessBuffer(
    const std::tuple<uint64_t, uint64_t, uint64_t, int32_t, int32_t, int32_t,
                     int32_t, int32_t, int32_t, int32_t, int32_t>& launch,
    void* stream) {
    const auto& [in_y, in_uv, in_out, in_op, in_w, in_h, in_yp, in_uvp, in_fmt, in_fr, in_mat] = launch;
    if (in_y == 0 || in_uv == 0 || in_out == 0 || in_op <= 0 || in_w <= 0 || in_h <= 0 ||
        in_yp <= 0 || in_uvp <= 0 || (in_fmt != RTX_PIXEL_NV12 && in_fmt != RTX_PIXEL_P010)) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureLoaded()) return RTX_ERR_NGX;
    if (g_preprocess_buffer_kernel == nullptr) {
        g_error = "kernel rtx_yuv_to_rgb10_buf is missing from the PTX module";
        return RTX_ERR_UNSUPPORTED;
    }
    if (!bindPrimaryContext()) return RTX_ERR_INTERNAL;
    unsigned long long y_plane = in_y, uv_plane = in_uv, out_ptr = in_out;
    int out_pitch = in_op, width = in_w, height = in_h, y_pitch = in_yp, uv_pitch = in_uvp;
    int format = (in_fmt == RTX_PIXEL_P010) ? 2 : 1;
    int full_range = (in_fr != 0) ? 1 : 0;
    int matrix = in_mat;
    // Doc 47 (Defect 1): PTX rtx_yuv_to_rgb10_buf parameter order is
    // (y, uv, out, width, height, y_pitch, uv_pitch, format, full_range, matrix, out_pitch).
    void* args[] = {&y_plane, &uv_plane, &out_ptr, &width, &height,
                    &y_pitch, &uv_pitch, &format, &full_range, &matrix, &out_pitch};
    const unsigned gx = (static_cast<unsigned>(width) + 15u) / 16u;
    const unsigned gy = (static_cast<unsigned>(height) + 15u) / 16u;
    return launchChecked(g_preprocess_buffer_kernel, "rtx_yuv_to_rgb10_buf", gx, gy, 16, 16, stream, args);
}

RtxResult launchPackV2(
    const std::tuple<uint64_t, uint64_t, uint64_t, uint32_t, uint32_t,
                     int32_t, int32_t, int32_t, int32_t, float,
                     uint64_t, int32_t, uint64_t, uint64_t, int32_t,
                     int32_t, int32_t, float, uint64_t>& launch,
    void* stream) {
    const auto& [in_surf, in_y, in_uv, in_yp, in_uvp, in_w, in_h, in_pq, in_scrgb, in_nits,
                 in_lut, in_lut_dim, in_stats, in_prev, in_pw, in_ph, in_ppitch, in_split, in_src_surf] = launch;
    if (in_surf == 0 || in_y == 0 || in_uv == 0 || in_w <= 0 || in_h <= 0 ||
        (in_w & 1) != 0 || (in_h & 1) != 0 || in_yp == 0 || in_uvp == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureLoaded()) return RTX_ERR_NGX;
    if (g_pack_v2_kernel == nullptr) {
        g_error = "kernel rtx_rgba16f_to_p010_bt2020nc_v2 is missing from the PTX module";
        return RTX_ERR_UNSUPPORTED;
    }
    if (!bindPrimaryContext()) return RTX_ERR_INTERNAL;
    unsigned long long surface = in_surf, y_plane = in_y, uv_plane = in_uv;
    int width = in_w, height = in_h;
    int y_pitch = static_cast<int>(in_yp), uv_pitch = static_cast<int>(in_uvp);
    int input_is_pq = (in_pq != 0) ? 1 : 0, input_is_scrgb = (in_scrgb != 0) ? 1 : 0;
    float nits = (in_nits > 0.0f) ? in_nits : 80.0f;
    unsigned long long lut_tex = in_lut, stats_devptr = in_stats, preview_ptr = in_prev, src_surface = in_src_surf;
    int lut_dim = in_lut_dim, preview_width = in_pw, preview_height = in_ph, preview_pitch_px = in_ppitch;
    float split_x_norm = in_split;
    void* args[] = {&surface, &y_plane, &uv_plane, &width, &height, &y_pitch, &uv_pitch,
                    &input_is_pq, &input_is_scrgb, &nits, &lut_tex, &lut_dim, &stats_devptr,
                    &preview_ptr, &preview_width, &preview_height, &preview_pitch_px,
                    &split_x_norm, &src_surface};
    const unsigned gx = (static_cast<unsigned>(width) / 2u + 15u) / 16u;
    const unsigned gy = (static_cast<unsigned>(height) / 2u + 15u) / 16u;
    return launchChecked(g_pack_v2_kernel, "rtx_rgba16f_to_p010_bt2020nc_v2", gx, gy, 16, 16, stream, args);
}

RtxResult launchFusedRemaster(
    const std::tuple<uint64_t, uint64_t, uint32_t, uint32_t, int32_t, int32_t,
                     int32_t, uint64_t, uint64_t, uint32_t, uint32_t,
                     int32_t, int32_t, int32_t, int32_t, uint64_t, int32_t,
                     int32_t, int32_t, float, float, uint64_t>& launch,
    void* stream) {
    const auto& [in_sy, in_suv, in_syp, in_suvp, in_sfmt, in_sfr, in_smat,
                 in_dy, in_duv, in_dyp, in_duvp, in_sw, in_sh, in_dw, in_dh,
                 in_lut, in_lut_dim, in_vsr, in_itm, in_luma, in_ynorm, in_stats] = launch;
    if (in_sy == 0 || in_suv == 0 || in_dy == 0 || in_duv == 0 ||
        in_sw <= 0 || in_sh <= 0 || in_dw <= 0 || in_dh <= 0 ||
        in_syp == 0 || in_suvp == 0 || in_dyp == 0 || in_duvp == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureLoaded()) return RTX_ERR_NGX;
    if (g_fused_remaster_kernel == nullptr) {
        g_error = "kernel rtx_cuda_remaster_fused_p010 is missing from the PTX module";
        return RTX_ERR_UNSUPPORTED;
    }
    if (!bindPrimaryContext()) return RTX_ERR_INTERNAL;
    unsigned long long src_y = in_sy, src_uv = in_suv, dst_y = in_dy, dst_uv = in_duv;
    unsigned long long lut_tex = in_lut, stats_devptr = in_stats;
    int src_y_pitch = static_cast<int>(in_syp), src_uv_pitch = static_cast<int>(in_suvp);
    int dst_y_pitch = static_cast<int>(in_dyp), dst_uv_pitch = static_cast<int>(in_duvp);
    int src_format = in_sfmt, src_full_range = in_sfr, src_matrix = in_smat;
    int src_width = in_sw, src_height = in_sh, dst_width = in_dw, dst_height = in_dh;
    int lut_dim = in_lut_dim, vsr_quality = in_vsr, enable_itm = in_itm;
    float max_luminance = in_luma, y_norm_prev = in_ynorm;
    void* args[] = {&src_y, &src_uv, &src_y_pitch, &src_uv_pitch, &src_format, &src_full_range,
                    &src_matrix, &dst_y, &dst_uv, &dst_y_pitch, &dst_uv_pitch, &src_width,
                    &src_height, &dst_width, &dst_height, &lut_tex, &lut_dim, &vsr_quality,
                    &enable_itm, &max_luminance, &y_norm_prev, &stats_devptr};
    const unsigned gx = (static_cast<unsigned>(dst_width) / 2u + 15u) / 16u;
    const unsigned gy = (static_cast<unsigned>(dst_height) / 2u + 15u) / 16u;
    return launchChecked(g_fused_remaster_kernel, "rtx_cuda_remaster_fused_p010", gx, gy, 16, 16, stream, args);
}

RtxResult launchNvofWarpNv12(
    uint64_t prev_nv12, uint64_t curr_nv12,
    uint64_t fwd_flow, uint64_t bwd_flow, uint64_t cost_buffer,
    uint32_t pitch, uint32_t width, uint32_t height,
    uint32_t grid_width, uint32_t grid_height,
    float alpha, uint64_t out_nv12, void* stream,
    uint32_t flow_stride_bytes, uint32_t cost_stride_bytes) {
    if (prev_nv12 == 0 || curr_nv12 == 0 || out_nv12 == 0 || pitch == 0 || width == 0 || height == 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureLoaded()) return RTX_ERR_NGX;
    if (!bindPrimaryContext()) return RTX_ERR_INTERNAL;
    if (g_interp_kernel == nullptr) {
        if (const NvCuResult res = nv_cuda::moduleLoadData(&g_interp_module, kNvofWarpPtx); res != kSuccess) {
            g_error = "cuModuleLoadData failed for NVOFA warp PTX: " + std::to_string(res);
            return RTX_ERR_NGX;
        }
        if (nv_cuda::moduleGetFunction(&g_interp_kernel, g_interp_module, "rtx_nvof_warp_nv12") != kSuccess ||
            g_interp_kernel == nullptr) {
            g_error = "cuModuleGetFunction failed for rtx_nvof_warp_nv12";
            return RTX_ERR_NGX;
        }
    }
    unsigned long long p_prev = prev_nv12, p_curr = curr_nv12, p_fwd = fwd_flow, p_bwd = bwd_flow;
    unsigned long long p_cost = cost_buffer, p_out = out_nv12;
    unsigned int p_pitch = pitch, p_width = width, p_height = height;
    unsigned int p_gw = grid_width ? grid_width : (width + 3u) / 4u;
    unsigned int p_gh = grid_height ? grid_height : (height + 3u) / 4u;
    unsigned int p_flow_stride = flow_stride_bytes ? flow_stride_bytes : (p_gw * 4u);
    unsigned int p_cost_stride = cost_stride_bytes ? cost_stride_bytes : p_gw;
    float p_alpha = alpha;
    void* args[] = {&p_prev, &p_curr, &p_fwd, &p_bwd, &p_cost, &p_pitch, &p_width, &p_height,
                    &p_gw, &p_gh, &p_flow_stride, &p_cost_stride, &p_alpha, &p_out};
    const unsigned gx = (width + 15u) / 16u;
    const unsigned gy = (height + height / 2u + 15u) / 16u;
    return launchChecked(g_interp_kernel, "rtx_nvof_warp_nv12", gx, gy, 16, 16, stream, args);
}

RtxResult launchInterpolateNv12(uint64_t prev_nv12, uint64_t curr_nv12,
                                uint32_t pitch, uint32_t width, uint32_t height,
                                float alpha, uint64_t out_nv12, void* stream) {
    return launchNvofWarpNv12(prev_nv12, curr_nv12, 0, 0, 0, pitch, width, height,
                              (width + 3u) / 4u, (height + 3u) / 4u, alpha, out_nv12, stream);
}

}  // namespace rtx_ngx

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_pack_available(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return ensureLoaded() ? RTX_OK : RTX_ERR_NGX;
}

RTX_SHIM_API RtxResult RTX_SHIM_CALL rtx_ngx_pack_launch(
    const RtxNgxPackSurfaces* surfaces, const RtxNgxPackConfig* config, uint64_t cuda_stream) {
    if (surfaces == nullptr || config == nullptr || surfaces->input_surface == 0 ||
        surfaces->y_plane == 0 || surfaces->uv_plane == 0 ||
        config->width <= 0 || config->height <= 0 ||
        (config->width & 1) != 0 || (config->height & 1) != 0) {
        return RTX_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureLoaded()) return RTX_ERR_NGX;
    if (!bindPrimaryContext()) return RTX_ERR_INTERNAL;
    unsigned long long surface = surfaces->input_surface, y_plane = surfaces->y_plane, uv_plane = surfaces->uv_plane;
    int width = config->width, height = config->height;
    int y_pitch = static_cast<int>(surfaces->y_pitch_samples);
    int uv_pitch = static_cast<int>(surfaces->uv_pitch_samples);
    int input_is_pq = (config->input_is_pq != 0) ? 1 : 0;
    void* args[] = {&surface, &y_plane, &uv_plane, &width, &height, &y_pitch, &uv_pitch, &input_is_pq};
    const unsigned gx = (static_cast<unsigned>(width) / 2u + 15u) / 16u;
    const unsigned gy = (static_cast<unsigned>(height) / 2u + 15u) / 16u;
    return launchChecked(g_kernel, "rtx_rgba16f_to_p010_bt2020nc", gx, gy, 16, 16,
                         reinterpret_cast<void*>(static_cast<uintptr_t>(cuda_stream)), args);
}

RTX_SHIM_API const char* RTX_SHIM_CALL rtx_ngx_pack_last_error(void) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_error.c_str();
}
