// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Runtime loader for the MIT-licensed rtx_shim.  The GPL CLI never links the
// shim and never resolves a NVIDIA symbol: it only calls the versioned C ABI.

#pragma once

#include <cstdint>
#include <string>

#include "shim/rtx_shim_abi.h"

namespace rtx::cli {

struct ShimProbeResult {
    bool loaded = false;
    std::string path;
    std::string error;
    uint32_t abi_version = 0;
    int32_t result = RTX_ERR_DLL_MISSING;
    RtxDiagnostics diagnostics{};
};

ShimProbeResult probeShim();

std::string formatProbeHuman(const ShimProbeResult& probe);
std::string formatProbeJson(const ShimProbeResult& probe);

std::string capabilityList(uint32_t caps);

}  // namespace rtx::cli
