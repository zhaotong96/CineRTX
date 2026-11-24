// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Runtime loader for the MIT-licensed rtx_shim.  The GPL CLI never links the
// shim and never resolves a NVIDIA symbol: it only calls the versioned C ABI.

#pragma once

#include <cstdint>
#include <string>
#include <tuple>

#include "shim/rtx_shim_abi.h"

namespace rtx::cli {

// Phase 12 (Wave 12.2-B2): tuple alias replaces passive ShimProbeResult bag.
// (loaded, path, error, abi_version, result, diagnostics)
using ShimProbeResult = std::tuple<bool, std::string, std::string, uint32_t, int32_t, RtxDiagnostics>;

ShimProbeResult probeShim();

std::string formatProbeHuman(const ShimProbeResult& probe);
std::string formatProbeJson(const ShimProbeResult& probe);
std::string escapeJson(const std::string& value);

std::string capabilityList(uint32_t caps);

}  // namespace rtx::cli
