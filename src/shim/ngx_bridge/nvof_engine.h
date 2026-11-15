/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Header for NVIDIA Optical Flow Accelerator (NVOFA) hardware optical flow engine.
 * Dynamically binds nvofapi64.dll in C:\Windows\System32 without static import dependencies.
 */

#ifndef RTX_NGX_BRIDGE_NVOF_ENGINE_H
#define RTX_NGX_BRIDGE_NVOF_ENGINE_H

#include <cstdint>
#include <memory>
#include <string>

namespace rtx_ngx {

// NVIDIA Optical Flow basic enums & types
typedef enum _NV_OF_STATUS {
    NV_OF_SUCCESS = 0,
    NV_OF_ERR_OF_NOT_AVAILABLE = 1,
    NV_OF_ERR_UNSUPPORTED_DEVICE = 2,
    NV_OF_ERR_DEVICE_DOES_NOT_EXIST = 3,
    NV_OF_ERR_INVALID_PTR = 4,
    NV_OF_ERR_INVALID_VAL = 5,
    NV_OF_ERR_INVALID_CALL = 6,
    NV_OF_ERR_OUT_OF_MEMORY = 7,
    NV_OF_ERR_INVALID_PARAM = 8,
    NV_OF_ERR_INVALID_VERSION = 9,
    NV_OF_ERR_OF_NOT_INITIALIZED = 10,
    NV_OF_ERR_GENERIC = 11,
} NV_OF_STATUS;

typedef enum _NV_OF_OUTPUT_VECTOR_GRID_SIZE {
    NV_OF_OUTPUT_VECTOR_GRID_SIZE_UNDEFINED = 0,
    NV_OF_OUTPUT_VECTOR_GRID_SIZE_1 = 1,
    NV_OF_OUTPUT_VECTOR_GRID_SIZE_2 = 2,
    NV_OF_OUTPUT_VECTOR_GRID_SIZE_4 = 4,
} NV_OF_OUTPUT_VECTOR_GRID_SIZE;

typedef enum _NV_OF_HINT_VECTOR_GRID_SIZE {
    NV_OF_HINT_VECTOR_GRID_SIZE_UNDEFINED = 0,
    NV_OF_HINT_VECTOR_GRID_SIZE_1 = 1,
    NV_OF_HINT_VECTOR_GRID_SIZE_2 = 2,
    NV_OF_HINT_VECTOR_GRID_SIZE_4 = 4,
    NV_OF_HINT_VECTOR_GRID_SIZE_8 = 8,
} NV_OF_HINT_VECTOR_GRID_SIZE;

typedef enum _NV_OF_MODE {
    NV_OF_MODE_UNDEFINED = 0,
    NV_OF_MODE_OPTICALFLOW = 1,
    NV_OF_MODE_STEREODISPARITY = 2,
} NV_OF_MODE;

typedef enum _NV_OF_PERF_LEVEL {
    NV_OF_PERF_LEVEL_UNDEFINED = 0,
    NV_OF_PERF_LEVEL_SLOW = 5,
    NV_OF_PERF_LEVEL_MEDIUM = 10,
    NV_OF_PERF_LEVEL_FAST = 20,
} NV_OF_PERF_LEVEL;

typedef enum _NV_OF_BUFFER_USAGE {
    NV_OF_BUFFER_USAGE_UNDEFINED = 0,
    NV_OF_BUFFER_USAGE_INPUT = 1,
    NV_OF_BUFFER_USAGE_OUTPUT = 2,
    NV_OF_BUFFER_USAGE_HINT = 3,
    NV_OF_BUFFER_USAGE_COST = 4,
    NV_OF_BUFFER_USAGE_GLOBAL_FLOW = 5,
} NV_OF_BUFFER_USAGE;

typedef enum _NV_OF_BUFFER_FORMAT {
    NV_OF_BUFFER_FORMAT_UNDEFINED = 0,
    NV_OF_BUFFER_FORMAT_GRAYSCALE8 = 1,
    NV_OF_BUFFER_FORMAT_NV12 = 2,
    NV_OF_BUFFER_FORMAT_ABGR8 = 3,
    NV_OF_BUFFER_FORMAT_SHORT = 4,
    NV_OF_BUFFER_FORMAT_SHORT2 = 5,
    NV_OF_BUFFER_FORMAT_UINT = 6,
    NV_OF_BUFFER_FORMAT_UINT8 = 7,
} NV_OF_BUFFER_FORMAT;

typedef enum _NV_OF_CUDA_BUFFER_TYPE {
    NV_OF_CUDA_BUFFER_TYPE_UNDEFINED = 0,
    NV_OF_CUDA_BUFFER_TYPE_CUARRAY = 1,
    NV_OF_CUDA_BUFFER_TYPE_CUDEVICEPTR = 2,
} NV_OF_CUDA_BUFFER_TYPE;

typedef enum _NV_OF_PRED_DIRECTION {
    NV_OF_PRED_DIRECTION_FORWARD = 1,
    NV_OF_PRED_DIRECTION_BOTH = 3,
} NV_OF_PRED_DIRECTION;

#pragma pack(push, 1)
typedef struct _NV_OF_FLOW_VECTOR {
    int16_t flowx;
    int16_t flowy;
} NV_OF_FLOW_VECTOR;
#pragma pack(pop)

using NV_OF_BOOL = uint32_t;
using NvOFHandle = void*;
using NvOFGPUBufferHandle = void*;
using NvOFPrivDataHandle = void*;

typedef struct _NV_OF_BUFFER_DESCRIPTOR {
    uint32_t width;
    uint32_t height;
    NV_OF_BUFFER_USAGE bufferUsage;
    NV_OF_BUFFER_FORMAT bufferFormat;
} NV_OF_BUFFER_DESCRIPTOR;

typedef struct _NV_OF_INIT_PARAMS {
    uint32_t width;
    uint32_t height;
    NV_OF_OUTPUT_VECTOR_GRID_SIZE outGridSize;
    NV_OF_HINT_VECTOR_GRID_SIZE hintGridSize;
    NV_OF_MODE mode;
    NV_OF_PERF_LEVEL perfLevel;
    NV_OF_BOOL enableExternalHints;
    NV_OF_BOOL enableOutputCost;
    NvOFPrivDataHandle hPrivData;
    uint32_t disparityRange;
    NV_OF_BOOL enableRoi;
    NV_OF_PRED_DIRECTION predDirection;
    NV_OF_BOOL enableGlobalFlow;
    NV_OF_BUFFER_FORMAT inputBufferFormat;
} NV_OF_INIT_PARAMS;

typedef struct _NV_OF_EXECUTE_INPUT_PARAMS {
    NvOFGPUBufferHandle inputFrame;
    NvOFGPUBufferHandle referenceFrame;
    NvOFGPUBufferHandle externalHints;
    NV_OF_BOOL disableTemporalHints;
    uint32_t padding;
    NvOFPrivDataHandle hPrivData;
    uint32_t numRois;
    void* roiData;
} NV_OF_EXECUTE_INPUT_PARAMS;

typedef struct _NV_OF_EXECUTE_OUTPUT_PARAMS {
    NvOFGPUBufferHandle outputBuffer;
    NvOFGPUBufferHandle outputCostBuffer;
    NvOFPrivDataHandle hPrivData;
    NvOFGPUBufferHandle bwdOutputBuffer;
    NvOFGPUBufferHandle bwdOutputCostBuffer;
    NvOFGPUBufferHandle globalFlowBuffer;
} NV_OF_EXECUTE_OUTPUT_PARAMS;

typedef struct _NV_OF_CUDA_BUFFER_STRIDE_INFO {
    struct {
        uint32_t strideXInBytes;
        uint32_t strideYInBytes;
    } strideInfo[3];
    uint32_t numPlanes;
} NV_OF_CUDA_BUFFER_STRIDE_INFO;

struct NV_OF_CUDA_API_FUNCTION_LIST {
    NV_OF_STATUS(__stdcall* nvCreateOpticalFlowCuda)(void* cuContext, NvOFHandle* hOf);
    NV_OF_STATUS(__stdcall* nvOFInit)(NvOFHandle hOf, const NV_OF_INIT_PARAMS* initParams);
    NV_OF_STATUS(__stdcall* nvOFCreateGPUBufferCuda)(
        NvOFHandle hOf, const NV_OF_BUFFER_DESCRIPTOR* bufferDesc,
        NV_OF_CUDA_BUFFER_TYPE bufferType, NvOFGPUBufferHandle* ofGpuBuffer);
    void* nvOFGPUBufferGetCUarray;
    uint64_t(__stdcall* nvOFGPUBufferGetCUdeviceptr)(NvOFGPUBufferHandle ofGpuBuffer);
    NV_OF_STATUS(__stdcall* nvOFGPUBufferGetStrideInfo)(
        NvOFGPUBufferHandle ofGpuBuffer, NV_OF_CUDA_BUFFER_STRIDE_INFO* strideInfo);
    NV_OF_STATUS(__stdcall* nvOFSetIOCudaStreams)(NvOFHandle hOf, void* inputStream, void* outputStream);
    NV_OF_STATUS(__stdcall* nvOFExecute)(
        NvOFHandle hOf, const NV_OF_EXECUTE_INPUT_PARAMS* executeInParams,
        NV_OF_EXECUTE_OUTPUT_PARAMS* executeOutParams);
    NV_OF_STATUS(__stdcall* nvOFDestroyGPUBufferCuda)(NvOFGPUBufferHandle buffer);
    NV_OF_STATUS(__stdcall* nvOFDestroy)(NvOFHandle hOf);
    NV_OF_STATUS(__stdcall* nvOFGetLastError)(NvOFHandle hOf, char lastError[], uint32_t* size);
    NV_OF_STATUS(__stdcall* nvOFGetCaps)(NvOFHandle hOf, uint32_t capsParam, uint32_t* capsVal, uint32_t* size);
};

typedef NV_OF_STATUS(__stdcall* PFNNvOFAPICreateInstanceCuda)(
    uint32_t apiVer, NV_OF_CUDA_API_FUNCTION_LIST* functionList);

class NvofEngine {
public:
    NvofEngine();
    ~NvofEngine();

    NvofEngine(const NvofEngine&) = delete;
    NvofEngine& operator=(const NvofEngine&) = delete;
    NvofEngine(NvofEngine&&) = delete;
    NvofEngine& operator=(NvofEngine&&) = delete;

    bool init(void* cuContext, uint32_t width, uint32_t height, void* stream, std::string* error);
    bool isLoaded() const noexcept;

    // Executes hardware optical flow estimation on input frames (prev_nv12, curr_nv12).
    // Outputs forward flow, backward flow, and cost device pointers.
    bool execute(uint64_t prev_nv12, uint64_t curr_nv12, uint32_t pitch, void* stream,
                 uint64_t* out_fwd_flow, uint64_t* out_bwd_flow, uint64_t* out_cost,
                 std::string* error);

    uint32_t width() const noexcept { return width_; }
    uint32_t height() const noexcept { return height_; }
    uint32_t gridWidth() const noexcept { return grid_width_; }
    uint32_t gridHeight() const noexcept { return grid_height_; }
    uint32_t flowStrideBytes() const noexcept { return flow_stride_bytes_; }
    uint32_t costStrideBytes() const noexcept { return cost_stride_bytes_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t grid_width_ = 0;
    uint32_t grid_height_ = 0;
    uint32_t flow_stride_bytes_ = 0;
    uint32_t cost_stride_bytes_ = 0;
};

}  // namespace rtx_ngx

#endif /* RTX_NGX_BRIDGE_NVOF_ENGINE_H */
