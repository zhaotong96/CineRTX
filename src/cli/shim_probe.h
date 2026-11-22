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

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 多字段领域模型：具名字段承载序列化/校验/反射契约，压进 std::tuple 会丢失全部字段语义与默认值契约。
// Invariant: 字段集合与默认值即对外契约（schema/测试逐字段锚定）；只增不删。
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
std::string escapeJson(const std::string& value);

std::string capabilityList(uint32_t caps);

}  // namespace rtx::cli
