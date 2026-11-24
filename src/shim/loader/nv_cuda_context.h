/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Shared CUDA primary-context bootstrap for the NVDEC and NVENC wrappers.
 * The driver is resolved at runtime; no NVIDIA import library is linked.
 */

#ifndef RTX_SHIM_NV_CUDA_CONTEXT_H
#define RTX_SHIM_NV_CUDA_CONTEXT_H

#include <memory>
#include <string>
#include <tuple>

#include "nv_func_tables.h"
#include "rtx_shim_abi.h"

namespace rtx_shim::cuda {

// Phase 12 (Wave 12.2-D): standard tuple + RAII release guard replaces cuda::Context wrapper.
// Layout: (library, driver_table, cu_context, cu_device, primary_ctx_release_guard).
using Context = std::tuple<DynLibrary, DriverTable, CUcontext, CUdevice, std::shared_ptr<void>>;

bool cudaCtxOpen(Context& ctx, std::string* error = nullptr, int device_index = 0);
void cudaCtxClose(Context& ctx);

inline bool cudaCtxValid(const Context& ctx) noexcept {
    const auto& [lib, drv, h, dev, guard] = ctx;
    (void)lib; (void)dev; (void)guard;
    return h != nullptr && drv.readyForProbe();
}
inline CUcontext cudaCtxHandle(const Context& ctx) noexcept {
    const auto& [lib, drv, h, dev, guard] = ctx;
    (void)lib; (void)drv; (void)dev; (void)guard;
    return h;
}
inline CUdevice cudaCtxDevice(const Context& ctx) noexcept {
    const auto& [lib, drv, h, dev, guard] = ctx;
    (void)lib; (void)drv; (void)h; (void)guard;
    return dev;
}
inline const DriverTable& cudaCtxDriver(const Context& ctx) noexcept {
    const auto& [lib, drv, h, dev, guard] = ctx;
    (void)lib; (void)h; (void)dev; (void)guard;
    return drv;
}
inline DriverTable& cudaCtxDriver(Context& ctx) noexcept {
    auto& [lib, drv, h, dev, guard] = ctx;
    (void)lib; (void)h; (void)dev; (void)guard;
    return drv;
}
inline const DynLibrary& cudaCtxLibrary(const Context& ctx) noexcept {
    const auto& [lib, drv, h, dev, guard] = ctx;
    (void)drv; (void)h; (void)dev; (void)guard;
    return lib;
}

int cudaCtxComputeMajor(const Context& ctx);
int cudaCtxComputeMinor(const Context& ctx);
std::string cudaCtxDeviceName(const Context& ctx);
unsigned int cudaCtxPrimaryFlags(const Context& ctx) noexcept;
bool cudaCtxBlockingSync(const Context& ctx) noexcept;

// Maps a CUDA driver result onto the shim ABI result space.
RtxResult mapResult(CUresult result);

}  // namespace rtx_shim::cuda

#endif /* RTX_SHIM_NV_CUDA_CONTEXT_H */
