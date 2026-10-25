/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 */

#include "nv_func_tables.h"

#include <sstream>
#include <vector>

namespace rtx_shim {
namespace {

template <typename Fn>
bool resolveOne(const DynLibrary& library, const char* name, Fn& target,
                std::vector<std::string>& missing) {
    void* address = library.symbol(name);
    if (address == nullptr) {
        target = nullptr;
        missing.emplace_back(name);
        return false;
    }
    target = reinterpret_cast<Fn>(address);
    return true;
}

std::string missingMessage(const char* component, const std::vector<std::string>& missing) {
    std::ostringstream out;
    out << component << " is missing required exports:";
    for (const std::string& name : missing) {
        out << ' ' << name;
    }
    return out.str();
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

    // Interop entry points are optional for probing; they are required before
    // the zero-copy path is declared available.
    std::vector<std::string> optional;
    resolveOne(library, "cuDevicePrimaryCtxSetFlags", cuDevicePrimaryCtxSetFlags, optional);
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

namespace nvenc {

bool Table::resolve(const DynLibrary& library, std::string* error) {
    std::vector<std::string> missing;
    resolveOne(library, "NvEncodeAPICreateInstance", createInstance, missing);
    resolveOne(library, "NvEncodeAPIGetMaxSupportedVersion", getMaxSupportedVersion, missing);
    if (createInstance == nullptr) {
        if (error != nullptr) {
            *error = missingMessage("nvEncodeAPI", missing);
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace rtx_shim::nvenc

namespace tensorrt {

bool Table::resolve(const DynLibrary& library, std::string* error) {
    std::vector<std::string> missing;
    resolveOne(library, "getInferLibVersion", getInferLibVersion, missing);
    if (getInferLibVersion == nullptr) {
        if (error != nullptr) {
            *error = missingMessage("nvinfer", missing);
        }
        return false;
    }

    std::vector<std::string> optional;
    resolveOne(library, "createInferBuilder_INTERNAL", createInferBuilder, optional);
    resolveOne(library, "createInferRuntime_INTERNAL", createInferRuntime, optional);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::string versionToString(int32_t version) {
    if (version <= 0) {
        return "unknown";
    }
    std::ostringstream out;
    if (version >= 10000) {
        out << (version / 10000) << '.' << ((version / 100) % 100) << '.' << (version % 100);
    } else {
        out << (version / 1000) << '.' << ((version / 100) % 10) << '.' << (version % 100);
    }
    return out.str();
}

}  // namespace rtx_shim::tensorrt

namespace ngx {

bool Table::resolve(const DynLibrary& library, std::string* error) {
    std::vector<std::string> optional;
    resolveOne(library, "NVSDK_NGX_D3D12_Init", init, optional);
    resolveOne(library, "NVSDK_NGX_D3D12_AllocateParameters", allocateParameters, optional);
    resolveOne(library, "NVSDK_NGX_D3D12_GetScratchBufferSize", getScratchBufferSize, optional);
    resolveOne(library, "NVSDK_NGX_D3D12_CreateFeature", createFeature, optional);
    resolveOne(library, "NVSDK_NGX_D3D12_EvaluateFeature", evaluateFeature, optional);
    resolveOne(library, "NVSDK_NGX_D3D12_ReleaseParameters", releaseParameters, optional);
    resolveOne(library, "NVSDK_NGX_D3D12_DestroyFeature", destroyFeature, optional);
    resolveOne(library, "NVSDK_NGX_D3D12_Shutdown", shutdown, optional);
    resolveOne(library, "NVSDK_NGX_Parameter_SetUI", setUI, optional);
    resolveOne(library, "NVSDK_NGX_Parameter_SetI", setI, optional);
    resolveOne(library, "NVSDK_NGX_Parameter_SetF", setF, optional);
    resolveOne(library, "NVSDK_NGX_Parameter_SetD", setD, optional);
    resolveOne(library, "NVSDK_NGX_Parameter_SetULL", setULL, optional);

    if (init == nullptr) {
        if (error != nullptr) {
            *error = missingMessage("nvsdk_ngx", optional);
        }
        return false;
    }
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

}  // namespace rtx_shim::ngx

}  // namespace rtx_shim
