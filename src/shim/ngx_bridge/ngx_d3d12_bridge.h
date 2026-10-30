/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * VSR -> TrueHDR evaluator on the headless D3D12 device owned by
 * D3D12Interop.  This is the only translation unit that includes the
 * proprietary RTX Video SDK headers, and it is compiled only into the Windows
 * bridge, which links nvsdk_ngx_s.lib.
 */

#ifndef RTX_NGX_BRIDGE_NGX_D3D12_BRIDGE_H
#define RTX_NGX_BRIDGE_NGX_D3D12_BRIDGE_H

#include <cstdint>
#include <mutex>
#include <string>

#include "ngx_bridge_abi.h"

namespace rtx_ngx {

class D3D12Interop;

class NgxD3D12Bridge {
public:
    NgxD3D12Bridge();
    ~NgxD3D12Bridge();

    NgxD3D12Bridge(const NgxD3D12Bridge&) = delete;
    NgxD3D12Bridge& operator=(const NgxD3D12Bridge&) = delete;

    bool create(D3D12Interop& interop, const RtxNgxBridgeConfig& config, std::string* error);
    void destroy();

    bool ready() const noexcept { return initialized_; }

    // Waits for the CUDA signal on the shared fence, evaluates VSR and/or
    // TrueHDR and signals input_fence_value + 1 on the same fence.
    bool process(uint64_t input_fence_value, std::string* error);

    // Completes the outstanding submission before the bridge is flushed or
    // destroyed, so no command list outlives its allocator.
    bool flush(std::string* error);

    // Per-frame luma statistics for the content-adaptive TrueHDR law.  The
    // values are consumed by the next process() call.
    void set_frame_stats(const RtxNgxFrameStats& stats);

    const std::string& diagnostics() const noexcept { return diagnostics_; }

private:
    bool create_features(std::string* error);
    bool apply_pending_stats(std::string* error);
    void release_features();
    void transition(void* resource, int before, int after);
    void uav_barrier(void* resource);

    D3D12Interop* interop_ = nullptr;
    void* device_ = nullptr;
    void* allocator_ = nullptr;
    void* command_list_ = nullptr;
    void* ngx_params_ = nullptr;
    void* vsr_handle_ = nullptr;
    void* truehdr_handle_ = nullptr;

    bool ngx_initialized_ = false;
    bool initialized_ = false;
    bool needs_vsr_ = false;
    bool needs_truehdr_ = false;
    bool needs_mid_ = false;

    int input_width_ = 0;
    int input_height_ = 0;
    int output_width_ = 0;
    int output_height_ = 0;
    int vsr_quality_ = 1;
    unsigned int node_mask_ = 1;

    int base_contrast_ = 125;
    int base_saturation_ = 75;
    int base_middle_gray_ = 44;
    int contrast_ = 125;
    int saturation_ = 75;
    int middle_gray_ = 44;
    int max_luminance_ = 1000;
    double smooth_contrast_ = 125.0;
    double smooth_saturation_ = 75.0;
    double smooth_middle_gray_ = 44.0;
    bool stats_initialized_ = false;

    bool has_pending_stats_ = false;
    RtxNgxFrameStats pending_stats_{};

    uint64_t pending_signal_ = 0;
    std::wstring plugin_dir_;
    std::wstring app_data_dir_;

    std::mutex mutex_;
    std::string diagnostics_;
};

}  // namespace rtx_ngx

#endif  // RTX_NGX_BRIDGE_NGX_D3D12_BRIDGE_H
