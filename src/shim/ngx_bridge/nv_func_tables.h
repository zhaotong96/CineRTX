/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Phase 12 (Wave 12.2-B1): Unified CUDA Driver API free-function dispatch for
 * rtx_ngx_bridge (zero named wrapper types).
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rtx_ngx {

using NvCuResult = int;
using NvCuDevicePtr = unsigned long long;
using NvCuStream = void*;
using NvCuModule = void*;
using NvCuFunction = void*;
using NvCuContext = void*;

namespace nv_cuda {

[[nodiscard]] bool loaded() noexcept;
[[nodiscard]] bool driverAvailable() noexcept;
[[nodiscard]] bool moduleApiLoaded() noexcept;

[[nodiscard]] NvCuResult init(unsigned int flags) noexcept;
[[nodiscard]] NvCuResult deviceGet(int* device, int ordinal) noexcept;
[[nodiscard]] NvCuResult ctxGetCurrent(NvCuContext* ctx) noexcept;
[[nodiscard]] NvCuResult ctxSetCurrent(NvCuContext ctx) noexcept;
[[nodiscard]] NvCuResult ctxPushCurrent(NvCuContext ctx) noexcept;
[[nodiscard]] NvCuResult ctxPopCurrent(NvCuContext* ctx) noexcept;
[[nodiscard]] NvCuResult primaryCtxRetain(NvCuContext* ctx, int device) noexcept;
[[nodiscard]] NvCuResult moduleLoadData(NvCuModule* mod, const void* image) noexcept;
[[nodiscard]] NvCuResult moduleGetFunction(NvCuFunction* fn, NvCuModule mod, const char* name) noexcept;
[[nodiscard]] NvCuResult moduleUnload(NvCuModule mod) noexcept;
[[nodiscard]] NvCuResult launchKernel(NvCuFunction fn, unsigned gx, unsigned gy, unsigned gz,
                                      unsigned bx, unsigned by, unsigned bz,
                                      unsigned shared_mem, NvCuStream stream,
                                      void** params, void** extra) noexcept;
[[nodiscard]] NvCuResult getErrorName(NvCuResult code, const char** name) noexcept;
[[nodiscard]] std::string formatError(const char* prefix, NvCuResult code);

[[nodiscard]] NvCuResult memAlloc(NvCuDevicePtr* dptr, size_t bytes) noexcept;
[[nodiscard]] NvCuResult memFree(NvCuDevicePtr dptr) noexcept;
[[nodiscard]] NvCuResult memAllocHost(void** ptr, size_t bytes) noexcept;
[[nodiscard]] NvCuResult memFreeHost(void* ptr) noexcept;
[[nodiscard]] NvCuResult memcpyHtoD(NvCuDevicePtr dst, const void* src, size_t bytes) noexcept;
[[nodiscard]] NvCuResult memcpyDtoH(void* dst, NvCuDevicePtr src, size_t bytes) noexcept;
[[nodiscard]] NvCuResult memcpyDtoDAsync(NvCuDevicePtr dst, NvCuDevicePtr src, size_t bytes, NvCuStream stream) noexcept;
[[nodiscard]] NvCuResult memsetD32Async(NvCuDevicePtr dst, unsigned int value, size_t count, NvCuStream stream) noexcept;
[[nodiscard]] NvCuResult streamCreate(NvCuStream* stream, unsigned int flags) noexcept;
[[nodiscard]] NvCuResult streamDestroy(NvCuStream stream) noexcept;
[[nodiscard]] NvCuResult streamSync(NvCuStream stream) noexcept;
[[nodiscard]] NvCuResult memcpy2DAsync(const void* copy_desc, NvCuStream stream) noexcept;

}  // namespace nv_cuda

}  // namespace rtx_ngx
