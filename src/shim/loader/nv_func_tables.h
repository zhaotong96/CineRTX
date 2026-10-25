/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Minimal ABI mirrors for the CUDA driver, NVENC, NvOFFRUC and TensorRT entry
 * points.  These are declarations only: every symbol is resolved at runtime, so
 * no proprietary import library is linked into the core or the shim.
 *
 * The CUDA layout mirrors are validated against cuda.h 12.9 with static_asserts
 * in nv_func_tables.cpp when RTX_HAVE_SYSTEM_CUDA_HEADER is defined.
 */

#ifndef RTX_SHIM_NV_FUNC_TABLES_H
#define RTX_SHIM_NV_FUNC_TABLES_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "dyn_library.h"

namespace rtx_shim {

// Opens an NVIDIA runtime DLL from RTX_NVIDIA_DIR, the executable directory,
// then (optionally) the system search path.  Implemented in nv_runtime.cpp.
bool openNvidiaLibrary(DynLibrary& library, const char* windows_name,
                       const char* posix_name, bool system_fallback,
                       std::string* error = nullptr);

}  // namespace rtx_shim

namespace rtx_shim::cuda {

// cuCtxCreate / cuDevicePrimaryCtxSetFlags scheduler flag.  Kept local so this
// header stays independent of the FFmpeg dynlink_cuda.h macro namespace.
inline constexpr unsigned int kCtxSchedBlockingSync = 0x04;

using CUresult = int;
using CUdevice = int;
using CUdeviceptr = unsigned long long;
using CUcontext = void*;
using CUstream = void*;
using CUevent = void*;
using CUexternalMemory = void*;
using CUexternalSemaphore = void*;
using CUmipmappedArray = void*;
using CUarray = void*;
using CUsurfObject = unsigned long long;

inline constexpr CUresult CUDA_SUCCESS = 0;
inline constexpr CUresult CUDA_ERROR_INVALID_VALUE = 1;
inline constexpr CUresult CUDA_ERROR_OUT_OF_MEMORY = 2;
inline constexpr CUresult CUDA_ERROR_NOT_INITIALIZED = 3;
inline constexpr CUresult CUDA_ERROR_DEINITIALIZED = 4;
inline constexpr CUresult CUDA_ERROR_NO_DEVICE = 100;
inline constexpr CUresult CUDA_ERROR_INVALID_DEVICE = 101;
inline constexpr CUresult CUDA_ERROR_INVALID_CONTEXT = 201;
inline constexpr CUresult CUDA_ERROR_NOT_SUPPORTED = 801;

inline constexpr int CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR = 75;
inline constexpr int CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR = 76;
inline constexpr int CU_DEVICE_ATTRIBUTE_UNIFIED_MEMORY = 83;

inline constexpr int CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD = 1;
inline constexpr int CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32 = 2;
inline constexpr int CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT = 3;
inline constexpr int CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE = 5;
inline constexpr unsigned int CUDA_EXTERNAL_MEMORY_DEDICATED = 0x1;

inline constexpr int CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD = 1;
inline constexpr int CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32 = 2;
inline constexpr int CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE = 4;

inline constexpr unsigned int CU_AD_FORMAT_UNSIGNED_INT8 = 0x01;
inline constexpr unsigned int CU_AD_FORMAT_UNSIGNED_INT16 = 0x02;
inline constexpr unsigned int CU_AD_FORMAT_UNSIGNED_INT32 = 0x03;
inline constexpr unsigned int CU_AD_FORMAT_HALF = 0x10;

inline constexpr int CU_MEMORYTYPE_HOST = 1;
inline constexpr int CU_MEMORYTYPE_DEVICE = 2;
inline constexpr int CU_MEMORYTYPE_ARRAY = 3;
inline constexpr int CU_MEMORYTYPE_UNIFIED = 4;

struct CUDA_MEMCPY2D {
    size_t srcXInBytes = 0;
    size_t srcY = 0;
    int srcMemoryType = 0;
    const void* srcHost = nullptr;
    CUdeviceptr srcDevice = 0;
    CUarray srcArray = nullptr;
    size_t srcPitch = 0;

    size_t dstXInBytes = 0;
    size_t dstY = 0;
    int dstMemoryType = 0;
    void* dstHost = nullptr;
    CUdeviceptr dstDevice = 0;
    CUarray dstArray = nullptr;
    size_t dstPitch = 0;

    size_t WidthInBytes = 0;
    size_t Height = 0;
};

struct CUDA_ARRAY3D_DESCRIPTOR {
    size_t Width = 0;
    size_t Height = 0;
    size_t Depth = 0;
    unsigned int Format = 0;
    unsigned int NumChannels = 0;
    unsigned int Flags = 0;
};

struct CUDA_EXTERNAL_MEMORY_HANDLE_DESC {
    int type = 0;
    union {
        int fd;
        struct {
            void* handle;
            const void* name;
        } win32;
        const void* nvSciBufObject;
    } handle = {};
    unsigned long long size = 0;
    unsigned int flags = 0;
    unsigned int reserved[16] = {};
};

struct CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC {
    unsigned long long offset = 0;
    CUDA_ARRAY3D_DESCRIPTOR arrayDesc{};
    unsigned int numLevels = 0;
    unsigned int reserved[16] = {};
};

struct CUDA_EXTERNAL_MEMORY_BUFFER_DESC {
    unsigned long long offset = 0;
    unsigned long long size = 0;
    unsigned int flags = 0;
    unsigned int reserved[16] = {};
};

struct CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC {
    int type = 0;
    union {
        int fd;
        struct {
            void* handle;
            const void* name;
        } win32;
        const void* nvSciSyncObj;
    } handle = {};
    unsigned int flags = 0;
    unsigned int reserved[16] = {};
};

struct CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS {
    struct {
        struct {
            unsigned long long value;
        } fence;
        union {
            void* fence;
            unsigned long long reserved;
        } nvSciSync;
        struct {
            unsigned long long key;
        } keyedMutex;
        unsigned int reserved[12];
    } params;
    unsigned int flags;
    unsigned int reserved[16];
};

struct CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS {
    struct {
        struct {
            unsigned long long value;
        } fence;
        union {
            void* fence;
            unsigned long long reserved;
        } nvSciSync;
        struct {
            unsigned long long key;
            unsigned int timeoutMs;
        } keyedMutex;
        unsigned int reserved[10];
    } params;
    unsigned int flags;
    unsigned int reserved[16];
};

struct DriverTable {
    using PFN_cuInit = CUresult (*)(unsigned int);
    using PFN_cuDriverGetVersion = CUresult (*)(int*);
    using PFN_cuDeviceGetCount = CUresult (*)(int*);
    using PFN_cuDeviceGet = CUresult (*)(CUdevice*, int);
    using PFN_cuDeviceGetName = CUresult (*)(char*, int, CUdevice);
    using PFN_cuDeviceTotalMem = CUresult (*)(size_t*, CUdevice);
    using PFN_cuDeviceGetAttribute = CUresult (*)(int*, int, CUdevice);
    using PFN_cuDeviceGetLuid = CUresult (*)(char*, unsigned int*, CUdevice);
    using PFN_cuMemGetInfo = CUresult (*)(size_t*, size_t*);
    using PFN_cuCtxGetCurrent = CUresult (*)(CUcontext*);
    using PFN_cuCtxSetCurrent = CUresult (*)(CUcontext);
    using PFN_cuDevicePrimaryCtxRetain = CUresult (*)(CUcontext*, CUdevice);
    using PFN_cuDevicePrimaryCtxRelease = CUresult (*)(CUdevice);
    using PFN_cuDevicePrimaryCtxSetFlags = CUresult (*)(CUdevice, unsigned int);
    using PFN_cuDevicePrimaryCtxGetState = CUresult (*)(CUdevice, unsigned int*, int*);
    using PFN_cuGetErrorName = CUresult (*)(CUresult, const char**);
    using PFN_cuGetErrorString = CUresult (*)(CUresult, const char**);
    using PFN_cuMemAlloc = CUresult (*)(CUdeviceptr*, size_t);
    using PFN_cuMemFree = CUresult (*)(CUdeviceptr);
    using PFN_cuMemAllocPitch = CUresult (*)(CUdeviceptr*, size_t*, size_t, size_t, unsigned int);
    using PFN_cuMemcpy2DAsync = CUresult (*)(const CUDA_MEMCPY2D*, CUstream);
    using PFN_cuMemsetD8Async = CUresult (*)(CUdeviceptr, unsigned char, size_t, CUstream);
    using PFN_cuStreamCreate = CUresult (*)(CUstream*, unsigned int);
    using PFN_cuStreamDestroy = CUresult (*)(CUstream);
    using PFN_cuStreamSynchronize = CUresult (*)(CUstream);
    using PFN_cuStreamWaitEvent = CUresult (*)(CUstream, CUevent, unsigned int);
    using PFN_cuEventCreate = CUresult (*)(CUevent*, unsigned int);
    using PFN_cuEventRecord = CUresult (*)(CUevent, CUstream);
    using PFN_cuEventSynchronize = CUresult (*)(CUevent);
    using PFN_cuEventQuery = CUresult (*)(CUevent);
    using PFN_cuEventDestroy = CUresult (*)(CUevent);
    using PFN_cuImportExternalMemory = CUresult (*)(CUexternalMemory*, const CUDA_EXTERNAL_MEMORY_HANDLE_DESC*);
    using PFN_cuExternalMemoryGetMappedMipmappedArray =
        CUresult (*)(CUmipmappedArray*, CUexternalMemory, const CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC*);
    using PFN_cuExternalMemoryGetMappedBuffer =
        CUresult (*)(CUdeviceptr*, CUexternalMemory, const CUDA_EXTERNAL_MEMORY_BUFFER_DESC*);
    using PFN_cuDestroyExternalMemory = CUresult (*)(CUexternalMemory);
    using PFN_cuImportExternalSemaphore = CUresult (*)(CUexternalSemaphore*, const CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC*);
    using PFN_cuDestroyExternalSemaphore = CUresult (*)(CUexternalSemaphore);
    using PFN_cuSignalExternalSemaphoresAsync =
        CUresult (*)(const CUexternalSemaphore*, const CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS*, unsigned int, CUstream);
    using PFN_cuWaitExternalSemaphoresAsync =
        CUresult (*)(const CUexternalSemaphore*, const CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS*, unsigned int, CUstream);
    using PFN_cuMipmappedArrayGetLevel = CUresult (*)(CUarray*, CUmipmappedArray, unsigned int);
    using PFN_cuMipmappedArrayDestroy = CUresult (*)(CUmipmappedArray);
    using PFN_cuSurfObjectCreate = CUresult (*)(CUsurfObject*, const void*);
    using PFN_cuSurfObjectDestroy = CUresult (*)(CUsurfObject);

    PFN_cuInit cuInit = nullptr;
    PFN_cuDriverGetVersion cuDriverGetVersion = nullptr;
    PFN_cuDeviceGetCount cuDeviceGetCount = nullptr;
    PFN_cuDeviceGet cuDeviceGet = nullptr;
    PFN_cuDeviceGetName cuDeviceGetName = nullptr;
    PFN_cuDeviceTotalMem cuDeviceTotalMem = nullptr;
    PFN_cuDeviceGetAttribute cuDeviceGetAttribute = nullptr;
    PFN_cuDeviceGetLuid cuDeviceGetLuid = nullptr;
    PFN_cuMemGetInfo cuMemGetInfo = nullptr;
    PFN_cuCtxGetCurrent cuCtxGetCurrent = nullptr;
    PFN_cuCtxSetCurrent cuCtxSetCurrent = nullptr;
    PFN_cuDevicePrimaryCtxRetain cuDevicePrimaryCtxRetain = nullptr;
    PFN_cuDevicePrimaryCtxRelease cuDevicePrimaryCtxRelease = nullptr;
    PFN_cuDevicePrimaryCtxSetFlags cuDevicePrimaryCtxSetFlags = nullptr;
    PFN_cuDevicePrimaryCtxGetState cuDevicePrimaryCtxGetState = nullptr;
    PFN_cuGetErrorName cuGetErrorName = nullptr;
    PFN_cuGetErrorString cuGetErrorString = nullptr;
    PFN_cuMemAlloc cuMemAlloc = nullptr;
    PFN_cuMemFree cuMemFree = nullptr;
    PFN_cuMemAllocPitch cuMemAllocPitch = nullptr;
    PFN_cuMemcpy2DAsync cuMemcpy2DAsync = nullptr;
    PFN_cuMemsetD8Async cuMemsetD8Async = nullptr;
    PFN_cuStreamCreate cuStreamCreate = nullptr;
    PFN_cuStreamDestroy cuStreamDestroy = nullptr;
    PFN_cuStreamSynchronize cuStreamSynchronize = nullptr;
    PFN_cuStreamWaitEvent cuStreamWaitEvent = nullptr;
    PFN_cuEventCreate cuEventCreate = nullptr;
    PFN_cuEventRecord cuEventRecord = nullptr;
    PFN_cuEventSynchronize cuEventSynchronize = nullptr;
    PFN_cuEventQuery cuEventQuery = nullptr;
    PFN_cuEventDestroy cuEventDestroy = nullptr;
    PFN_cuImportExternalMemory cuImportExternalMemory = nullptr;
    PFN_cuExternalMemoryGetMappedMipmappedArray cuExternalMemoryGetMappedMipmappedArray = nullptr;
    PFN_cuExternalMemoryGetMappedBuffer cuExternalMemoryGetMappedBuffer = nullptr;
    PFN_cuDestroyExternalMemory cuDestroyExternalMemory = nullptr;
    PFN_cuImportExternalSemaphore cuImportExternalSemaphore = nullptr;
    PFN_cuDestroyExternalSemaphore cuDestroyExternalSemaphore = nullptr;
    PFN_cuSignalExternalSemaphoresAsync cuSignalExternalSemaphoresAsync = nullptr;
    PFN_cuWaitExternalSemaphoresAsync cuWaitExternalSemaphoresAsync = nullptr;
    PFN_cuMipmappedArrayGetLevel cuMipmappedArrayGetLevel = nullptr;
    PFN_cuMipmappedArrayDestroy cuMipmappedArrayDestroy = nullptr;
    PFN_cuSurfObjectCreate cuSurfObjectCreate = nullptr;
    PFN_cuSurfObjectDestroy cuSurfObjectDestroy = nullptr;

    bool resolve(const DynLibrary& library, std::string* error = nullptr);
    bool readyForProbe() const;
    bool readyForInterop() const;
};

const char* resultName(CUresult result);

}  // namespace rtx_shim::cuda

namespace rtx_shim::nvenc {

using NVENCSTATUS = int;
inline constexpr NVENCSTATUS NV_ENC_SUCCESS = 0;

using PFN_NvEncodeAPICreateInstance = NVENCSTATUS (*)(void*);
using PFN_NvEncodeAPIGetMaxSupportedVersion = NVENCSTATUS (*)(uint32_t*);

struct Table {
    PFN_NvEncodeAPICreateInstance createInstance = nullptr;
    PFN_NvEncodeAPIGetMaxSupportedVersion getMaxSupportedVersion = nullptr;
    bool resolve(const DynLibrary& library, std::string* error = nullptr);
};

}  // namespace rtx_shim::nvenc

namespace rtx_shim::tensorrt {

using PFN_getInferLibVersion = int32_t (*)();
using PFN_createInferBuilder = void* (*)(void*);
using PFN_createInferRuntime = void* (*)(void*);

struct Table {
    PFN_getInferLibVersion getInferLibVersion = nullptr;
    PFN_createInferBuilder createInferBuilder = nullptr;
    PFN_createInferRuntime createInferRuntime = nullptr;
    bool resolve(const DynLibrary& library, std::string* error = nullptr);
};

std::string versionToString(int32_t version);

}  // namespace rtx_shim::tensorrt

namespace rtx_shim::ngx {

using PFN_Init = int (*)(const char*, const char*, const char*, int, void*, void*);
using PFN_AllocateParameters = int (*)(void**, int);
using PFN_GetScratchBufferSize = int (*)(int, uint64_t*);
using PFN_CreateFeature = int (*)(void*, int, void*, void**);
using PFN_EvaluateFeature = int (*)(void*, void*, void*, void*);
using PFN_ReleaseParameters = int (*)(void*);
using PFN_DestroyFeature = int (*)(void*);
using PFN_Shutdown = int (*)(void);
using PFN_ParameterSetUI = int (*)(void*, const char*, unsigned int);
using PFN_ParameterSetI = int (*)(void*, const char*, int);
using PFN_ParameterSetF = int (*)(void*, const char*, float);
using PFN_ParameterSetD = int (*)(void*, const char*, double);
using PFN_ParameterSetULL = int (*)(void*, const char*, unsigned long long);

struct Table {
    PFN_Init init = nullptr;
    PFN_AllocateParameters allocateParameters = nullptr;
    PFN_GetScratchBufferSize getScratchBufferSize = nullptr;
    PFN_CreateFeature createFeature = nullptr;
    PFN_EvaluateFeature evaluateFeature = nullptr;
    PFN_ReleaseParameters releaseParameters = nullptr;
    PFN_DestroyFeature destroyFeature = nullptr;
    PFN_Shutdown shutdown = nullptr;
    PFN_ParameterSetUI setUI = nullptr;
    PFN_ParameterSetI setI = nullptr;
    PFN_ParameterSetF setF = nullptr;
    PFN_ParameterSetD setD = nullptr;
    PFN_ParameterSetULL setULL = nullptr;

    bool resolve(const DynLibrary& library, std::string* error = nullptr);
};

}  // namespace rtx_shim::ngx

#endif /* RTX_SHIM_NV_FUNC_TABLES_H */
