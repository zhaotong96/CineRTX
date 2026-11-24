/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 */

#include "nv_func_tables.h"

#include <vector>
#include <format>

namespace rtx_shim {
namespace {

template <typename Fn>
bool resolveOne(const DynLibrary& library, const char* name, Fn& target,
                std::vector<std::string>& missing) {
    void* address = dynSymbol(library, name);
    if (address == nullptr) {
        target = nullptr;
        missing.emplace_back(name);
        return false;
    }
    target = reinterpret_cast<Fn>(address);
    return true;
}
std::string missingMessage(const char* component, const std::vector<std::string>& missing) {
    std::string out = std::format("{} is missing required exports:", component);
    for (const std::string& name : missing) {
        out += ' ';
        out += name;
    }
    return out;
}
}  // namespace

namespace cuda {

bool DriverTable::resolve(const DynLibrary& library, std::string* error) {
    std::vector<std::string> missing;
    resolveOne(library, "cuInit", cuInit, missing);
    resolveOne(library, "cuDriverGetVersion", cuDriverGetVersion, missing);
    resolveOne(library, "cuDeviceGetCount", cuDeviceGetCount, missing);
    resolveOne(library, "cuDeviceGet", cuDeviceGet, missing);
    resolveOne(library, "cuDeviceGetName", cuDeviceGetName, missing);
    resolveOne(library, "cuDeviceTotalMem_v2", cuDeviceTotalMem, missing);
    if (cuDeviceTotalMem == nullptr) {
        resolveOne(library, "cuDeviceTotalMem", cuDeviceTotalMem, missing);
    }
    resolveOne(library, "cuDeviceGetAttribute", cuDeviceGetAttribute, missing);
    resolveOne(library, "cuDeviceGetLuid", cuDeviceGetLuid, missing);
    resolveOne(library, "cuMemGetInfo_v2", cuMemGetInfo, missing);
    if (cuMemGetInfo == nullptr) {
        resolveOne(library, "cuMemGetInfo", cuMemGetInfo, missing);
    }
    resolveOne(library, "cuCtxGetCurrent", cuCtxGetCurrent, missing);
    resolveOne(library, "cuCtxSetCurrent", cuCtxSetCurrent, missing);
    resolveOne(library, "cuDevicePrimaryCtxRetain", cuDevicePrimaryCtxRetain, missing);
    resolveOne(library, "cuDevicePrimaryCtxRelease", cuDevicePrimaryCtxRelease, missing);
    resolveOne(library, "cuGetErrorName", cuGetErrorName, missing);
    resolveOne(library, "cuGetErrorString", cuGetErrorString, missing);

    std::vector<std::string> optional;
    resolveOne(library, "cuDevicePrimaryCtxSetFlags", cuDevicePrimaryCtxSetFlags, optional);
// Interop entry points are optional for probing; they are required before
// the zero-copy path is declared available.
    resolveOne(library, "cuDevicePrimaryCtxGetState", cuDevicePrimaryCtxGetState, optional);
    resolveOne(library, "cuMemAlloc_v2", cuMemAlloc, optional);
    resolveOne(library, "cuMemFree_v2", cuMemFree, optional);
    resolveOne(library, "cuMemAllocPitch_v2", cuMemAllocPitch, optional);
    resolveOne(library, "cuMemcpy2DAsync_v2", cuMemcpy2DAsync, optional);
    resolveOne(library, "cuMemsetD8Async", cuMemsetD8Async, optional);
    resolveOne(library, "cuStreamCreate", cuStreamCreate, optional);
    resolveOne(library, "cuStreamDestroy_v2", cuStreamDestroy, optional);
    resolveOne(library, "cuStreamSynchronize", cuStreamSynchronize, optional);
    resolveOne(library, "cuStreamWaitEvent", cuStreamWaitEvent, optional);
    resolveOne(library, "cuEventCreate", cuEventCreate, optional);
    resolveOne(library, "cuEventRecord", cuEventRecord, optional);
    resolveOne(library, "cuEventSynchronize", cuEventSynchronize, optional);
    resolveOne(library, "cuEventQuery", cuEventQuery, optional);
    resolveOne(library, "cuEventDestroy_v2", cuEventDestroy, optional);
    resolveOne(library, "cuImportExternalMemory", cuImportExternalMemory, optional);
    resolveOne(library, "cuExternalMemoryGetMappedMipmappedArray",
               cuExternalMemoryGetMappedMipmappedArray, optional);
    resolveOne(library, "cuExternalMemoryGetMappedBuffer", cuExternalMemoryGetMappedBuffer,
               optional);
    resolveOne(library, "cuDestroyExternalMemory", cuDestroyExternalMemory, optional);
    resolveOne(library, "cuImportExternalSemaphore", cuImportExternalSemaphore, optional);
    resolveOne(library, "cuDestroyExternalSemaphore", cuDestroyExternalSemaphore, optional);
    resolveOne(library, "cuSignalExternalSemaphoresAsync", cuSignalExternalSemaphoresAsync,
               optional);
    resolveOne(library, "cuWaitExternalSemaphoresAsync", cuWaitExternalSemaphoresAsync, optional);
    resolveOne(library, "cuMipmappedArrayGetLevel", cuMipmappedArrayGetLevel, optional);
    resolveOne(library, "cuMipmappedArrayDestroy", cuMipmappedArrayDestroy, optional);
    resolveOne(library, "cuSurfObjectCreate", cuSurfObjectCreate, optional);
    resolveOne(library, "cuSurfObjectDestroy", cuSurfObjectDestroy, optional);

    if (!missing.empty()) {
        if (error != nullptr) {
            *error = missingMessage("nvcuda", missing);
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}
bool DriverTable::readyForProbe() const {
    return cuInit != nullptr && cuDriverGetVersion != nullptr && cuDeviceGetCount != nullptr &&
           cuDeviceGet != nullptr && cuDeviceGetName != nullptr && cuDeviceTotalMem != nullptr &&
           cuDeviceGetAttribute != nullptr && cuDeviceGetLuid != nullptr;
}
bool DriverTable::readyForInterop() const {
    return readyForProbe() && cuImportExternalMemory != nullptr &&
           cuExternalMemoryGetMappedMipmappedArray != nullptr &&
           cuImportExternalSemaphore != nullptr && cuSignalExternalSemaphoresAsync != nullptr &&
           cuWaitExternalSemaphoresAsync != nullptr && cuSurfObjectCreate != nullptr;
}
const char* resultName(CUresult result) {
    switch (result) {
        case CUDA_SUCCESS:
            return "CUDA_SUCCESS";
        case CUDA_ERROR_INVALID_VALUE:
            return "CUDA_ERROR_INVALID_VALUE";
        case CUDA_ERROR_OUT_OF_MEMORY:
            return "CUDA_ERROR_OUT_OF_MEMORY";
        case CUDA_ERROR_NOT_INITIALIZED:
            return "CUDA_ERROR_NOT_INITIALIZED";
        case CUDA_ERROR_DEINITIALIZED:
            return "CUDA_ERROR_DEINITIALIZED";
        case CUDA_ERROR_NO_DEVICE:
            return "CUDA_ERROR_NO_DEVICE";
        case CUDA_ERROR_INVALID_DEVICE:
            return "CUDA_ERROR_INVALID_DEVICE";
        case CUDA_ERROR_INVALID_CONTEXT:
            return "CUDA_ERROR_INVALID_CONTEXT";
        case CUDA_ERROR_NOT_SUPPORTED:
            return "CUDA_ERROR_NOT_SUPPORTED";
        default:
            return "CUDA_ERROR_UNKNOWN";
    }
}
}  // namespace rtx_shim::cuda

namespace ngx {

bool resolveExports(const DynLibrary& library, std::string* error) {
    static constexpr const char* kRequired[] = {
        "NVSDK_NGX_D3D12_Init",
        "NVSDK_NGX_D3D12_AllocateParameters",
        "NVSDK_NGX_D3D12_CreateFeature",
        "NVSDK_NGX_D3D12_EvaluateFeature",
        "NVSDK_NGX_D3D12_ReleaseFeature",
        "NVSDK_NGX_D3D12_DestroyParameters",
        "NVSDK_NGX_D3D12_Shutdown1",
    };
    for (const char* name : kRequired) {
        if (dynSymbol(library, name) == nullptr) {
            if (error != nullptr) *error = std::string("missing NGX export: ") + name;
            return false;
        }
    }
    if (error != nullptr) error->clear();
    return true;
}

}  // namespace ngx

}  // namespace rtx_shim
