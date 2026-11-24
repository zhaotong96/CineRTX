/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 */

#include "nv_cuda_context.h"

#include "nv_trace.h"

namespace rtx_shim::cuda {
namespace {

std::string cudaError(const DriverTable& driver, const char* operation, CUresult result) {
    const char* name = nullptr;
    const char* text = nullptr;
    if (driver.cuGetErrorName != nullptr) driver.cuGetErrorName(result, &name);
    if (driver.cuGetErrorString != nullptr) driver.cuGetErrorString(result, &text);
    std::string message = operation;
    message += " failed";
    if (name != nullptr) { message += ": "; message += name; }
    if (text != nullptr) { message += " ("; message += text; message += ')'; }
    return message;
}

}  // namespace

bool cudaCtxOpen(Context& ctx, std::string* error, int device_index) {
    if (cudaCtxValid(ctx)) return true;
    cudaCtxClose(ctx);
    auto& [library_, driver_, context_, device_, guard_] = ctx;

    std::string message;
    if (!openNvidiaLibrary(library_, "nvcuda.dll", "libcuda.so.1", true, &message)) {
        if (error != nullptr) *error = "nvcuda is unavailable: " + message;
        return false;
    }
    if (!driver_.resolve(library_, &message)) {
        if (error != nullptr) *error = message;
        return false;
    }
    CUresult result = driver_.cuInit(0);
    if (result != CUDA_SUCCESS) {
        if (error != nullptr) *error = cudaError(driver_, "cuInit", result);
        return false;
    }
    int count = 0;
    result = driver_.cuDeviceGetCount(&count);
    if (result != CUDA_SUCCESS || count <= 0) {
        if (error != nullptr) {
            *error = result == CUDA_SUCCESS ? "no CUDA device is visible"
                                            : cudaError(driver_, "cuDeviceGetCount", result);
        }
        return false;
    }
    const int requested_device = device_index < 0 ? 0 : device_index;
    result = driver_.cuDeviceGet(&device_, requested_device);
    if (result != CUDA_SUCCESS) {
        if (error != nullptr) *error = cudaError(driver_, "cuDeviceGet", result);
        return false;
    }
    if (driver_.cuDevicePrimaryCtxSetFlags != nullptr) {
        const CUresult flags = driver_.cuDevicePrimaryCtxSetFlags(device_, kCtxSchedBlockingSync);
        RTX_TRACE("cuda: cuDevicePrimaryCtxSetFlags(0x%x) -> %s", kCtxSchedBlockingSync,
                  flags == CUDA_SUCCESS ? "ok" : "rejected (context already active)");
    }
    result = driver_.cuDevicePrimaryCtxRetain(&context_, device_);
    if (result != CUDA_SUCCESS) {
        if (error != nullptr) *error = cudaError(driver_, "cuDevicePrimaryCtxRetain", result);
        context_ = nullptr;
        return false;
    }
    const auto release_fn = driver_.cuDevicePrimaryCtxRelease;
    const CUdevice retained_dev = device_;
    guard_ = std::shared_ptr<void>(context_, [release_fn, retained_dev](void*) noexcept {
        if (release_fn != nullptr) release_fn(retained_dev);
    });
    if (driver_.cuCtxSetCurrent != nullptr) {
        result = driver_.cuCtxSetCurrent(context_);
        if (result != CUDA_SUCCESS) {
            if (error != nullptr) *error = cudaError(driver_, "cuCtxSetCurrent", result);
            cudaCtxClose(ctx);
            return false;
        }
    }
    RTX_TRACE("cuda: primary context flags=0x%x blocking_sync=%d", cudaCtxPrimaryFlags(ctx),
              cudaCtxBlockingSync(ctx) ? 1 : 0);
    if (error != nullptr) error->clear();
    return true;
}

void cudaCtxClose(Context& ctx) {
    auto& [library_, driver_, context_, device_, guard_] = ctx;
    (void)driver_;
    guard_.reset();
    context_ = nullptr;
    device_ = 0;
    dynClose(library_);
}

int cudaCtxComputeMajor(const Context& ctx) {
    const auto& [lib, driver_, h, device_, guard] = ctx;
    (void)lib; (void)h; (void)guard;
    int value = 0;
    if (driver_.cuDeviceGetAttribute != nullptr) {
        driver_.cuDeviceGetAttribute(&value, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, device_);
    }
    return value;
}

int cudaCtxComputeMinor(const Context& ctx) {
    const auto& [lib, driver_, h, device_, guard] = ctx;
    (void)lib; (void)h; (void)guard;
    int value = 0;
    if (driver_.cuDeviceGetAttribute != nullptr) {
        driver_.cuDeviceGetAttribute(&value, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, device_);
    }
    return value;
}

std::string cudaCtxDeviceName(const Context& ctx) {
    const auto& [lib, driver_, h, device_, guard] = ctx;
    (void)lib; (void)h; (void)guard;
    char name[128] = {};
    if (driver_.cuDeviceGetName != nullptr) {
        driver_.cuDeviceGetName(name, sizeof(name), device_);
    }
    return name;
}

unsigned int cudaCtxPrimaryFlags(const Context& ctx) noexcept {
    const auto& [lib, driver_, h, device_, guard] = ctx;
    (void)lib; (void)h; (void)guard;
    unsigned int flags = 0;
    int active = 0;
    if (driver_.cuDevicePrimaryCtxGetState != nullptr) {
        (void)driver_.cuDevicePrimaryCtxGetState(device_, &flags, &active);
    }
    return flags;
}

bool cudaCtxBlockingSync(const Context& ctx) noexcept {
    return (cudaCtxPrimaryFlags(ctx) & kCtxSchedBlockingSync) != 0;
}

RtxResult mapResult(CUresult result) {
    switch (result) {
        case CUDA_SUCCESS: return RTX_OK;
        case CUDA_ERROR_OUT_OF_MEMORY: return RTX_ERR_OUT_OF_MEMORY;
        case CUDA_ERROR_NO_DEVICE:
        case CUDA_ERROR_INVALID_DEVICE:
        case CUDA_ERROR_INVALID_CONTEXT:
        case CUDA_ERROR_INVALID_VALUE: return RTX_ERR_INVALID_ARGUMENT;
        case CUDA_ERROR_NOT_SUPPORTED: return RTX_ERR_UNSUPPORTED;
        case CUDA_ERROR_DEINITIALIZED: return RTX_ERR_DEVICE_LOST;
        default: return RTX_ERR_INTERNAL;
    }
}

}  // namespace rtx_shim::cuda
