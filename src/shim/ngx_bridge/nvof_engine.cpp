/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * NVIDIA Optical Flow Accelerator (NVOFA) driver API engine implementation.
 */

#include "nvof_engine.h"

#include <algorithm>
#include <cstring>
#include <mutex>
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

using CUresult = int;
constexpr CUresult kCudaSuccess = 0;

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

struct CudaDriverApi {
    CUresult (*memcpy_2d_async)(const CUDA_MEMCPY2D_V2*, void*) = nullptr;
    CUresult (*stream_sync)(void*) = nullptr;

    bool init() {
        static std::once_flag flag;
        static bool ok = false;
        std::call_once(flag, [this] {
#if defined(_WIN32)
            HMODULE mod = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!mod) return;
            memcpy_2d_async = reinterpret_cast<decltype(memcpy_2d_async)>(
                GetProcAddress(mod, "cuMemcpy2DAsync_v2"));
            if (!memcpy_2d_async) {
                memcpy_2d_async = reinterpret_cast<decltype(memcpy_2d_async)>(
                    GetProcAddress(mod, "cuMemcpy2DAsync"));
            }
            stream_sync = reinterpret_cast<decltype(stream_sync)>(
                GetProcAddress(mod, "cuStreamSynchronize"));
#else
            void* mod = dlopen("libcuda.so.1", RTLD_NOW | RTLD_LOCAL);
            if (!mod) return;
            memcpy_2d_async = reinterpret_cast<decltype(memcpy_2d_async)>(
                dlsym(mod, "cuMemcpy2DAsync_v2"));
            stream_sync = reinterpret_cast<decltype(stream_sync)>(
                dlsym(mod, "cuStreamSynchronize"));
#endif
            ok = (memcpy_2d_async != nullptr);
        });
        return ok;
    }
};

static CudaDriverApi g_cuda;

}  // namespace

struct NvofEngine::Impl {
#if defined(_WIN32)
    HMODULE nvof_module = nullptr;
#else
    void* nvof_module = nullptr;
#endif
    NV_OF_CUDA_API_FUNCTION_LIST api{};
    NvOFHandle hof = nullptr;

    NvOFGPUBufferHandle input_buffer = nullptr;
    NvOFGPUBufferHandle ref_buffer = nullptr;
    NvOFGPUBufferHandle fwd_buffer = nullptr;
    NvOFGPUBufferHandle bwd_buffer = nullptr;
    NvOFGPUBufferHandle cost_buffer = nullptr;
    NvOFGPUBufferHandle bwd_cost_buffer = nullptr;

    uint64_t input_devptr = 0;
    uint64_t ref_devptr = 0;
    uint64_t fwd_devptr = 0;
    uint64_t bwd_devptr = 0;
    uint64_t cost_devptr = 0;

    NV_OF_CUDA_BUFFER_STRIDE_INFO input_stride{};
    NV_OF_CUDA_BUFFER_STRIDE_INFO flow_stride{};
    NV_OF_CUDA_BUFFER_STRIDE_INFO cost_stride{};

    ~Impl() {
        releaseBuffers();
        if (hof && api.nvOFDestroy) {
            api.nvOFDestroy(hof);
            hof = nullptr;
        }
        if (nvof_module) {
#if defined(_WIN32)
            FreeLibrary(nvof_module);
#else
            dlclose(nvof_module);
#endif
            nvof_module = nullptr;
        }
    }

    void releaseBuffers() {
        if (!api.nvOFDestroyGPUBufferCuda) return;
        if (input_buffer) { api.nvOFDestroyGPUBufferCuda(input_buffer); input_buffer = nullptr; }
        if (ref_buffer) { api.nvOFDestroyGPUBufferCuda(ref_buffer); ref_buffer = nullptr; }
        if (fwd_buffer) { api.nvOFDestroyGPUBufferCuda(fwd_buffer); fwd_buffer = nullptr; }
        if (bwd_buffer) { api.nvOFDestroyGPUBufferCuda(bwd_buffer); bwd_buffer = nullptr; }
        if (cost_buffer) { api.nvOFDestroyGPUBufferCuda(cost_buffer); cost_buffer = nullptr; }
        if (bwd_cost_buffer) { api.nvOFDestroyGPUBufferCuda(bwd_cost_buffer); bwd_cost_buffer = nullptr; }
        input_devptr = 0;
        ref_devptr = 0;
        fwd_devptr = 0;
        bwd_devptr = 0;
        cost_devptr = 0;
    }
};

NvofEngine::NvofEngine() : impl_(std::make_unique<Impl>()) {}

NvofEngine::~NvofEngine() = default;

bool NvofEngine::isLoaded() const noexcept {
    return impl_ && impl_->hof != nullptr;
}

bool NvofEngine::init(void* cuContext, uint32_t width, uint32_t height, void* stream, std::string* error) {
    if (!cuContext || width == 0 || height == 0) {
        if (error) *error = "Invalid NVOFA parameters or CUDA context";
        return false;
    }

    if (!g_cuda.init()) {
        if (error) *error = "Failed to bind CUDA driver functions for NVOFA";
        return false;
    }

#if defined(_WIN32)
    impl_->nvof_module = LoadLibraryExW(L"nvofapi64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!impl_->nvof_module) {
        if (error) *error = "Cannot load C:\\Windows\\System32\\nvofapi64.dll";
        return false;
    }
    auto pfnCreate = reinterpret_cast<PFNNvOFAPICreateInstanceCuda>(
        GetProcAddress(impl_->nvof_module, "NvOFAPICreateInstanceCuda"));
#else
    impl_->nvof_module = dlopen("libnvidia-opticalflow.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!impl_->nvof_module) {
        if (error) *error = "Cannot load libnvidia-opticalflow.so.1";
        return false;
    }
    auto pfnCreate = reinterpret_cast<PFNNvOFAPICreateInstanceCuda>(
        dlsym(impl_->nvof_module, "NvOFAPICreateInstanceCuda"));
#endif

    if (!pfnCreate) {
        if (error) *error = "NvOFAPICreateInstanceCuda entry point not found in nvofapi64.dll";
        return false;
    }

    // Initialize function table (API version 5.0 = 0x50)
    constexpr uint32_t kApiVer = (5 << 4) | 0;
    NV_OF_STATUS st = pfnCreate(kApiVer, &impl_->api);
    if (st != NV_OF_SUCCESS) {
        if (error) *error = "NvOFAPICreateInstanceCuda failed with status " + std::to_string(st);
        return false;
    }

    st = impl_->api.nvCreateOpticalFlowCuda(cuContext, &impl_->hof);
    if (st != NV_OF_SUCCESS || !impl_->hof) {
        if (error) *error = "nvCreateOpticalFlowCuda failed with status " + std::to_string(st);
        return false;
    }

    // Configure 4x4 output grid, forward optical flow and cost calculation
    NV_OF_INIT_PARAMS initParams{};
    initParams.width = width;
    initParams.height = height;
    initParams.outGridSize = NV_OF_OUTPUT_VECTOR_GRID_SIZE_4;
    initParams.mode = NV_OF_MODE_OPTICALFLOW;
    initParams.perfLevel = NV_OF_PERF_LEVEL_FAST;
    initParams.predDirection = NV_OF_PRED_DIRECTION_FORWARD;
    initParams.enableOutputCost = 1;
    initParams.inputBufferFormat = NV_OF_BUFFER_FORMAT_NV12;

    st = impl_->api.nvOFInit(impl_->hof, &initParams);
    if (st != NV_OF_SUCCESS) {
        if (error) *error = "nvOFInit failed with status " + std::to_string(st);
        return false;
    }

    (void)impl_->api.nvOFSetIOCudaStreams(impl_->hof, stream, stream);

    width_ = width;
    height_ = height;
    grid_width_ = (width + 3u) / 4u;
    grid_height_ = (height + 3u) / 4u;

    // Allocate Input buffers (NV12 format)
    NV_OF_BUFFER_DESCRIPTOR inDesc{};
    inDesc.width = width_;
    inDesc.height = height_;
    inDesc.bufferUsage = NV_OF_BUFFER_USAGE_INPUT;
    inDesc.bufferFormat = NV_OF_BUFFER_FORMAT_NV12;

    st = impl_->api.nvOFCreateGPUBufferCuda(impl_->hof, &inDesc, NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, &impl_->input_buffer);
    if (st != NV_OF_SUCCESS) {
        if (error) *error = "nvOFCreateGPUBufferCuda for input buffer failed: " + std::to_string(st);
        return false;
    }
    impl_->input_devptr = impl_->api.nvOFGPUBufferGetCUdeviceptr(impl_->input_buffer);
    (void)impl_->api.nvOFGPUBufferGetStrideInfo(impl_->input_buffer, &impl_->input_stride);

    st = impl_->api.nvOFCreateGPUBufferCuda(impl_->hof, &inDesc, NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, &impl_->ref_buffer);
    if (st != NV_OF_SUCCESS) {
        if (error) *error = "nvOFCreateGPUBufferCuda for reference buffer failed: " + std::to_string(st);
        return false;
    }
    impl_->ref_devptr = impl_->api.nvOFGPUBufferGetCUdeviceptr(impl_->ref_buffer);

    // Allocate Forward & Backward Flow Vector buffers (SHORT2 format, 4x4 grid resolution)
    NV_OF_BUFFER_DESCRIPTOR flowDesc{};
    flowDesc.width = grid_width_;
    flowDesc.height = grid_height_;
    flowDesc.bufferUsage = NV_OF_BUFFER_USAGE_OUTPUT;
    flowDesc.bufferFormat = NV_OF_BUFFER_FORMAT_SHORT2;

    st = impl_->api.nvOFCreateGPUBufferCuda(impl_->hof, &flowDesc, NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, &impl_->fwd_buffer);
    if (st != NV_OF_SUCCESS) {
        if (error) *error = "nvOFCreateGPUBufferCuda for forward flow buffer failed: " + std::to_string(st);
        return false;
    }
    impl_->fwd_devptr = impl_->api.nvOFGPUBufferGetCUdeviceptr(impl_->fwd_buffer);
    (void)impl_->api.nvOFGPUBufferGetStrideInfo(impl_->fwd_buffer, &impl_->flow_stride);
    flow_stride_bytes_ = (impl_->flow_stride.numPlanes > 0 &&
                          impl_->flow_stride.strideInfo[0].strideXInBytes >= grid_width_ * 4u)
                             ? impl_->flow_stride.strideInfo[0].strideXInBytes
                             : grid_width_ * 4u;

    // Allocate Cost buffer (UINT8 format, 4x4 grid resolution)
    NV_OF_BUFFER_DESCRIPTOR costDesc{};
    costDesc.width = grid_width_;
    costDesc.height = grid_height_;
    costDesc.bufferUsage = NV_OF_BUFFER_USAGE_COST;
    costDesc.bufferFormat = NV_OF_BUFFER_FORMAT_UINT8;

    st = impl_->api.nvOFCreateGPUBufferCuda(impl_->hof, &costDesc, NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR, &impl_->cost_buffer);
    if (st != NV_OF_SUCCESS) {
        if (error) *error = "nvOFCreateGPUBufferCuda for cost buffer failed: " + std::to_string(st);
        return false;
    }
    impl_->cost_devptr = impl_->api.nvOFGPUBufferGetCUdeviceptr(impl_->cost_buffer);
    (void)impl_->api.nvOFGPUBufferGetStrideInfo(impl_->cost_buffer, &impl_->cost_stride);
    cost_stride_bytes_ = (impl_->cost_stride.numPlanes > 0 &&
                          impl_->cost_stride.strideInfo[0].strideXInBytes >= grid_width_)
                             ? impl_->cost_stride.strideInfo[0].strideXInBytes
                             : grid_width_;

    return true;
}

bool NvofEngine::execute(
    uint64_t prev_nv12, uint64_t curr_nv12, uint32_t pitch, void* stream,
    uint64_t* out_fwd_flow, uint64_t* out_bwd_flow, uint64_t* out_cost,
    std::string* error) {

    if (!impl_ || !impl_->hof) {
        if (error) *error = "NVOFA engine is not initialized";
        return false;
    }

    const uint32_t dst_pitch = impl_->input_stride.numPlanes > 0
                                   ? impl_->input_stride.strideInfo[0].strideXInBytes
                                   : width_;
    const uint32_t dst_y_rows = (impl_->input_stride.numPlanes > 1 &&
                                 impl_->input_stride.strideInfo[0].strideYInBytes >= height_)
                                    ? impl_->input_stride.strideInfo[0].strideYInBytes
                                    : height_;

    // 1. Copy prev_nv12 -> input_buffer (Y plane & UV plane)
    CUDA_MEMCPY2D_V2 copy{};
    copy.srcMemoryType = 2; // CU_MEMORYTYPE_DEVICE
    copy.dstMemoryType = 2; // CU_MEMORYTYPE_DEVICE
    copy.WidthInBytes = width_;
    copy.Height = height_;
    copy.srcDevice = prev_nv12;
    copy.srcPitch = pitch;
    copy.dstDevice = impl_->input_devptr;
    copy.dstPitch = dst_pitch;
    g_cuda.memcpy_2d_async(&copy, stream);

    // UV Plane
    copy.Height = height_ / 2;
    copy.srcDevice = prev_nv12 + static_cast<uint64_t>(pitch) * height_;
    copy.dstDevice = impl_->input_devptr + static_cast<uint64_t>(dst_pitch) * dst_y_rows;
    g_cuda.memcpy_2d_async(&copy, stream);

    // 2. Copy curr_nv12 -> ref_buffer (Y plane & UV plane)
    copy.Height = height_;
    copy.srcDevice = curr_nv12;
    copy.dstDevice = impl_->ref_devptr;
    g_cuda.memcpy_2d_async(&copy, stream);

    copy.Height = height_ / 2;
    copy.srcDevice = curr_nv12 + static_cast<uint64_t>(pitch) * height_;
    copy.dstDevice = impl_->ref_devptr + static_cast<uint64_t>(dst_pitch) * dst_y_rows;
    g_cuda.memcpy_2d_async(&copy, stream);

    // 3. Execute NVOFA hardware optical flow (disableTemporalHints=1 prevents cross-pair state drift)
    NV_OF_EXECUTE_INPUT_PARAMS inParams{};
    inParams.inputFrame = impl_->input_buffer;
    inParams.referenceFrame = impl_->ref_buffer;
    inParams.disableTemporalHints = 1;

    NV_OF_EXECUTE_OUTPUT_PARAMS outParams{};
    outParams.outputBuffer = impl_->fwd_buffer;
    outParams.outputCostBuffer = impl_->cost_buffer;

    NV_OF_STATUS st = impl_->api.nvOFExecute(impl_->hof, &inParams, &outParams);
    if (st != NV_OF_SUCCESS) {
        if (error) *error = "nvOFExecute failed with status " + std::to_string(st);
        return false;
    }

    if (out_fwd_flow) *out_fwd_flow = impl_->fwd_devptr;
    if (out_bwd_flow) *out_bwd_flow = 0;
    if (out_cost) *out_cost = impl_->cost_devptr;

    return true;
}

}  // namespace rtx_ngx
