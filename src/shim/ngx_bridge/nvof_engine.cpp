/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * NVIDIA Optical Flow Accelerator (NVOFA) driver API engine implementation.
 */

#include "nvof_engine.h"
#include "nv_func_tables.h"

#include <cstddef>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rtx_ngx {

namespace {

typedef struct CUDA_MEMCPY2D_st {
    size_t srcXInBytes;
    size_t srcY;
    int srcMemoryType;
    const void* srcHost;
    uint64_t srcDevice;
    void* srcArray;
    size_t srcPitch;
    size_t dstXInBytes;
    size_t dstY;
    int dstMemoryType;
    void* dstHost;
    uint64_t dstDevice;
    void* dstArray;
    size_t dstPitch;
    size_t WidthInBytes;
    size_t Height;
} CUDA_MEMCPY2D_V2;

}  // namespace

NvofEngine::NvofEngine() = default;

NvofEngine::~NvofEngine() {
    releaseBuffers();
    if (hof_ && api_.nvOFDestroy) {
        api_.nvOFDestroy(hof_);
        hof_ = nullptr;
    }
    if (nvof_module_) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(nvof_module_));
#else
        dlclose(nvof_module_);
#endif
        nvof_module_ = nullptr;
    }
}

void NvofEngine::releaseBuffers() noexcept {
    if (!api_.nvOFDestroyGPUBufferCuda) return;
    for (NvOFGPUBufferHandle* buf : {&input_buffer_, &ref_buffer_, &fwd_buffer_,
                                     &bwd_buffer_, &cost_buffer_, &bwd_cost_buffer_}) {
        if (*buf) {
            api_.nvOFDestroyGPUBufferCuda(*buf);
            *buf = nullptr;
        }
    }
    input_devptr_ = ref_devptr_ = fwd_devptr_ = bwd_devptr_ = cost_devptr_ = 0;
}

bool NvofEngine::isLoaded() const noexcept { return hof_ != nullptr; }

bool NvofEngine::init(void* cuContext, uint32_t width, uint32_t height, void* stream, std::string* error) {
    if (!cuContext || width == 0 || height == 0) {
        if (error) *error = "Invalid NVOFA parameters or CUDA context";
        return false;
    }
    if (!nv_cuda::loaded()) {
        if (error) *error = "Failed to bind CUDA driver functions for NVOFA";
        return false;
    }
#if defined(_WIN32)
    nvof_module_ = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!nvof_module_) {
        if (error) *error = "Cannot load C:\\Windows\\System32\\nvofapi64.dll";
        return false;
    }
    auto pfnCreate = reinterpret_cast<PFNNvOFAPICreateInstanceCuda>(
        GetProcAddress(static_cast<HMODULE>(nvof_module_), "NvOFAPICreateInstanceCuda"));
#else
    nvof_module_ = dlopen("libnvidia-opticalflow.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!nvof_module_) {
        if (error) *error = "Cannot load libnvidia-opticalflow.so.1";
        return false;
    }
    auto pfnCreate = reinterpret_cast<PFNNvOFAPICreateInstanceCuda>(
        dlsym(nvof_module_, "NvOFAPICreateInstanceCuda"));
#endif
    if (!pfnCreate) {
        if (error) *error = "NvOFAPICreateInstanceCuda entry point not found in nvofapi64.dll";
        return false;
    }
    constexpr uint32_t kApiVer = (5 << 4) | 0;
    NV_OF_STATUS st = pfnCreate(kApiVer, &api_);
    if (st != NV_OF_SUCCESS) {
        if (error) *error = "NvOFAPICreateInstanceCuda failed with status " + std::to_string(st);
        return false;
    }
    st = api_.nvCreateOpticalFlowCuda(cuContext, &hof_);
    if (st != NV_OF_SUCCESS || !hof_) {
        if (error) *error = "nvCreateOpticalFlowCuda failed with status " + std::to_string(st);
        return false;
    }
    NV_OF_INIT_PARAMS initParams{};
    initParams.width = width;
    initParams.height = height;
    initParams.outGridSize = NV_OF_OUTPUT_VECTOR_GRID_SIZE_4;
    initParams.mode = NV_OF_MODE_OPTICALFLOW;
    initParams.perfLevel = NV_OF_PERF_LEVEL_FAST;
    initParams.predDirection = NV_OF_PRED_DIRECTION_FORWARD;
    initParams.enableOutputCost = 1;
    initParams.inputBufferFormat = NV_OF_BUFFER_FORMAT_NV12;

    st = api_.nvOFInit(hof_, &initParams);
    if (st != NV_OF_SUCCESS) {
        if (error) *error = "nvOFInit failed with status " + std::to_string(st);
        return false;
    }
    (void)api_.nvOFSetIOCudaStreams(hof_, stream, stream);

    width_ = width;
    height_ = height;
    grid_width_ = (width + 3u) / 4u;
    grid_height_ = (height + 3u) / 4u;

    auto allocBuf = [&](uint32_t w, uint32_t h, NV_OF_BUFFER_USAGE usage, NV_OF_BUFFER_FORMAT fmt,
                        NvOFGPUBufferHandle* out_h, uint64_t* out_ptr,
                        NV_OF_CUDA_BUFFER_STRIDE_INFO* out_stride, const char* label) -> bool {
        NV_OF_BUFFER_DESCRIPTOR desc{};
        desc.width = w;
        desc.height = h;
        desc.bufferUsage = usage;
        desc.bufferFormat = fmt;
        const NV_OF_STATUS r = api_.nvOFCreateGPUBufferCuda(hof_, &desc, NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, out_h);
        if (r != NV_OF_SUCCESS) {
            if (error) *error = std::string("nvOFCreateGPUBufferCuda for ") + label + " failed: " + std::to_string(r);
            return false;
        }
        *out_ptr = api_.nvOFGPUBufferGetCUdeviceptr(*out_h);
        if (out_stride) (void)api_.nvOFGPUBufferGetStrideInfo(*out_h, out_stride);
        return true;
    };

    if (!allocBuf(width_, height_, NV_OF_BUFFER_USAGE_INPUT, NV_OF_BUFFER_FORMAT_NV12,
                  &input_buffer_, &input_devptr_, &input_stride_, "input buffer") ||
        !allocBuf(width_, height_, NV_OF_BUFFER_USAGE_INPUT, NV_OF_BUFFER_FORMAT_NV12,
                  &ref_buffer_, &ref_devptr_, nullptr, "reference buffer") ||
        !allocBuf(grid_width_, grid_height_, NV_OF_BUFFER_USAGE_OUTPUT, NV_OF_BUFFER_FORMAT_SHORT2,
                  &fwd_buffer_, &fwd_devptr_, &flow_stride_, "forward flow buffer") ||
        !allocBuf(grid_width_, grid_height_, NV_OF_BUFFER_USAGE_COST, NV_OF_BUFFER_FORMAT_UINT8,
                  &cost_buffer_, &cost_devptr_, &cost_stride_, "cost buffer")) {
        return false;
    }
    flow_stride_bytes_ = (flow_stride_.numPlanes > 0 && flow_stride_.strideInfo[0].strideXInBytes >= grid_width_ * 4u)
                             ? flow_stride_.strideInfo[0].strideXInBytes : grid_width_ * 4u;
    cost_stride_bytes_ = (cost_stride_.numPlanes > 0 && cost_stride_.strideInfo[0].strideXInBytes >= grid_width_)
                             ? cost_stride_.strideInfo[0].strideXInBytes : grid_width_;
    return true;
}

bool NvofEngine::execute(
    uint64_t prev_nv12, uint64_t curr_nv12, uint32_t pitch, void* stream,
    uint64_t* out_fwd_flow, uint64_t* out_bwd_flow, uint64_t* out_cost,
    std::string* error) {
    if (!hof_) {
        if (error) *error = "NVOFA engine is not initialized";
        return false;
    }
    const uint32_t dst_pitch = input_stride_.numPlanes > 0 ? input_stride_.strideInfo[0].strideXInBytes : width_;
    const uint32_t dst_y_rows = (input_stride_.numPlanes > 1 && input_stride_.strideInfo[0].strideYInBytes >= height_)
                                    ? input_stride_.strideInfo[0].strideYInBytes : height_;

    auto copyNv12 = [&](uint64_t src, uint64_t dst) {
        CUDA_MEMCPY2D_V2 copy{};
        copy.srcMemoryType = 2;
        copy.dstMemoryType = 2;
        copy.WidthInBytes = width_;
        copy.Height = height_;
        copy.srcDevice = src;
        copy.srcPitch = pitch;
        copy.dstDevice = dst;
        copy.dstPitch = dst_pitch;
        (void)nv_cuda::memcpy2DAsync(&copy, stream);
        copy.Height = height_ / 2;
        copy.srcDevice = src + static_cast<uint64_t>(pitch) * height_;
        copy.dstDevice = dst + static_cast<uint64_t>(dst_pitch) * dst_y_rows;
        (void)nv_cuda::memcpy2DAsync(&copy, stream);
    };
    copyNv12(prev_nv12, input_devptr_);
    copyNv12(curr_nv12, ref_devptr_);

    NV_OF_EXECUTE_INPUT_PARAMS inParams{};
    inParams.inputFrame = input_buffer_;
    inParams.referenceFrame = ref_buffer_;
    inParams.disableTemporalHints = 1;

    NV_OF_EXECUTE_OUTPUT_PARAMS outParams{};
    outParams.outputBuffer = fwd_buffer_;
    outParams.outputCostBuffer = cost_buffer_;

    const NV_OF_STATUS st = api_.nvOFExecute(hof_, &inParams, &outParams);
    if (st != NV_OF_SUCCESS) {
        if (error) *error = "nvOFExecute failed with status " + std::to_string(st);
        return false;
    }
    if (out_fwd_flow) *out_fwd_flow = fwd_devptr_;
    if (out_bwd_flow) *out_bwd_flow = 0;
    if (out_cost) *out_cost = cost_devptr_;
    return true;
}

}  // namespace rtx_ngx
