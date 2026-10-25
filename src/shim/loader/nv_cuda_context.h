/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * Shared CUDA primary-context bootstrap for the NVDEC and NVENC wrappers.
 * The driver is resolved at runtime; no NVIDIA import library is linked.
 */

#ifndef RTX_SHIM_NV_CUDA_CONTEXT_H
#define RTX_SHIM_NV_CUDA_CONTEXT_H

#include <string>

#include "nv_func_tables.h"
#include "rtx_shim_abi.h"

namespace rtx_shim::cuda {

class Context {
public:
    Context() = default;
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    bool open(std::string* error = nullptr);
    void close();

    bool valid() const noexcept { return context_ != nullptr && driver_.readyForProbe(); }
    CUcontext handle() const noexcept { return context_; }
    CUdevice device() const noexcept { return device_; }
    const DriverTable& driver() const noexcept { return driver_; }
    DriverTable& driver() noexcept { return driver_; }
    const DynLibrary& library() const noexcept { return library_; }

    int computeMajor() const;
    int computeMinor() const;
    std::string deviceName() const;
    // Scheduler flags of the retained primary context (0 when the driver does
    // not expose cuDevicePrimaryCtxGetState).
    unsigned int primaryFlags() const noexcept;
    bool blockingSync() const noexcept;

private:
    DynLibrary library_;
    DriverTable driver_;
    CUcontext context_ = nullptr;
    CUdevice device_ = 0;
};

// Maps a CUDA driver result onto the shim ABI result space.
RtxResult mapResult(CUresult result);

}  // namespace rtx_shim::cuda

#endif /* RTX_SHIM_NV_CUDA_CONTEXT_H */
