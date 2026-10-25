// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// High-level backend contract.  The CPU reference backend implements every
// operation deterministically; the NVIDIA backend forwards to the MIT-licensed
// shim in include/shim/rtx_shim_abi.h and never appears in the core's import
// table.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "rtx/status.h"
#include "rtx/types.h"

namespace rtx {

struct DeviceInfo {
    std::string name;
    uint8_t luid[8] = {};
    uint32_t node_mask = 0;
    int compute_major = 0;
    int compute_minor = 0;
    uint64_t vram_bytes = 0;
    uint32_t driver_version = 0;
    uint32_t cuda_version = 0;
    bool nvidia = false;

    bool turingOrNewer() const {
        return compute_major > 7 || (compute_major == 7 && compute_minor >= 5);
    }
    std::string toString() const;
};

struct BackendCapabilities {
    bool nvdec = false;
    bool nvenc_hevc10 = false;
    bool nvenc_av1 = false;
    bool vsr = false;
    bool truehdr = false;
    bool nvofa = false;
    bool tensorrt = false;
    bool d3d12_interop = false;
    bool cpu_reference = false;

    std::string toString() const;
};

struct BackendConfig {
    int width = 1920;
    int height = 1080;
    int target_width = 3840;
    int target_height = 2160;
    int vsr_quality = 2;
    bool enable_truehdr = true;
    bool enable_nvofa = true;
    bool enable_rife = false;
    size_t vram_budget_bytes = 7168ull * 1024 * 1024;
};

struct InterpRequest {
    const Frame* previous = nullptr;
    const Frame* next = nullptr;
    float timestep = 0.5f;
    Frame* output = nullptr;
};

struct EnhanceRequest {
    const Frame* input = nullptr;
    Frame* output = nullptr;
    Hdr10Metadata hdr{};
};

class IEngineBackend {
public:
    virtual ~IEngineBackend() = default;

    virtual const char* name() const noexcept = 0;
    virtual Result<DeviceInfo> deviceInfo() const = 0;
    virtual BackendCapabilities capabilities() const = 0;
    virtual Status initialize(const BackendConfig& config) = 0;
    virtual void shutdown() = 0;

    virtual Result<Frame> makeFrame(int width, int height, PixelFormat format) = 0;
    virtual Result<void> interpolate(const InterpRequest& request) = 0;
    virtual Result<void> enhance(const EnhanceRequest& request) = 0;
    virtual Result<void> packP010(const Frame& rgba16f, Frame& p010) = 0;
    virtual Result<void> encode(const Frame& p010, Packet& packet) = 0;
};

// Deterministic reference backend.  Used by unit tests, the CI smoke test and
// the --backend cpu fallback.  It performs real pixel math (BT.2020 NCL, PQ,
// P010 packing) but no hardware acceleration.
class CpuReferenceBackend final : public IEngineBackend {
public:
    CpuReferenceBackend();
    ~CpuReferenceBackend() override;

    const char* name() const noexcept override { return "cpu-reference"; }
    Result<DeviceInfo> deviceInfo() const override;
    BackendCapabilities capabilities() const override;
    Status initialize(const BackendConfig& config) override;
    void shutdown() override;

    Result<Frame> makeFrame(int width, int height, PixelFormat format) override;
    Result<void> interpolate(const InterpRequest& request) override;
    Result<void> enhance(const EnhanceRequest& request) override;
    Result<void> packP010(const Frame& rgba16f, Frame& p010) override;
    Result<void> encode(const Frame& p010, Packet& packet) override;

private:
    BackendConfig config_{};
    bool initialized_ = false;
};

std::unique_ptr<IEngineBackend> makeCpuBackend();

}  // namespace rtx
