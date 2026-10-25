// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Runtime loader for the MIT-licensed rtx_ngx_bridge.dll remaster contract.
// The GPL core never links the bridge and never touches an NVIDIA SDK header;
// this class resolves the append-only v2 ABI (query_caps / create / destroy /
// remaster / get_remaster_stats / last_error) and rejects any bridge whose
// rtx_ngx_bridge_abi_version is older than the remaster contract.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "shim/ngx_bridge_abi.h"

namespace rtx {

class NgxRemasterBridge {
public:
    NgxRemasterBridge() = default;
    ~NgxRemasterBridge();
    NgxRemasterBridge(const NgxRemasterBridge&) = delete;
    NgxRemasterBridge& operator=(const NgxRemasterBridge&) = delete;

    // Loads rtx_ngx_bridge.dll from RTX_NGX_BRIDGE_PATH or the executable
    // directory and verifies the ABI version plus the remaster entry points.
    bool open(std::string* error = nullptr);
    void close();
    bool loaded() const noexcept { return handle_ != nullptr; }
    const std::string& path() const noexcept { return path_; }
    const std::string& error() const noexcept { return error_; }

    RtxResult queryCaps(RtxNgxBridgeCaps* caps) const;
    RtxResult create(const RtxNgxBridgeConfig* config, RtxNgxBridge** bridge) const;
    void destroy(RtxNgxBridge* bridge) const;
    RtxResult remaster(RtxNgxBridge* bridge, const RtxNgxRemasterInput* input,
                       RtxNgxRemasterOutput* output, RtxNgxRemasterStats* stats) const;
    RtxResult getRemasterStats(RtxNgxBridge* bridge, RtxNgxRemasterStats* stats) const;
    const char* lastError(RtxNgxBridge* bridge) const;

private:
    template <typename Fn>
    bool resolve(Fn& function, const char* name, std::string* error);

    void* handle_ = nullptr;
    std::string path_;
    std::string error_;

    RtxResult(RTX_SHIM_CALL* query_caps_)(RtxNgxBridgeCaps*) = nullptr;
    RtxResult(RTX_SHIM_CALL* create_)(const RtxNgxBridgeConfig*, RtxNgxBridge**) = nullptr;
    void(RTX_SHIM_CALL* destroy_)(RtxNgxBridge*) = nullptr;
    RtxResult(RTX_SHIM_CALL* remaster_)(RtxNgxBridge*, const RtxNgxRemasterInput*,
                                        RtxNgxRemasterOutput*, RtxNgxRemasterStats*) = nullptr;
    RtxResult(RTX_SHIM_CALL* get_remaster_stats_)(RtxNgxBridge*, RtxNgxRemasterStats*) = nullptr;
    const char*(RTX_SHIM_CALL* last_error_)(RtxNgxBridge*) = nullptr;
};

}  // namespace rtx
