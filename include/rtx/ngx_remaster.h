// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>

#include "shim/ngx_bridge_abi.h"

namespace rtx {

struct MediaInfo;
class IVideoFrameSink;

using PreviewSlotFlags = std::shared_ptr<std::array<std::atomic<std::uint32_t>, 3>>;

using PreviewSlotBorrow =
    std::tuple<void*, void*, std::uint32_t, PreviewSlotFlags>;

using BorrowCallback = std::function<bool(std::uint32_t& slot, PreviewSlotBorrow& borrow)>;

[[nodiscard]] std::unique_ptr<IVideoFrameSink> createHeadlessPreviewSink(
    const MediaInfo& info, std::function<void()> on_frame, BorrowCallback& out_borrow);

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 封装 NGX/D3D12 桥接函数表、嵌入/动态模块句柄与 3 槽预览注册表的有状态装载宿主，std::unique_ptr 无法表达多函数指针表与活动桥接原子发布不变量。
// Invariant: open()/close() 成对管理模块句柄与函数表；preview_slot_flags_ 通过 shared_ptr 共享所有权，避免 GPU 异步退役回调悬空访问。
class NgxRemasterBridge {
public:
    NgxRemasterBridge() = default;
    ~NgxRemasterBridge();
    NgxRemasterBridge(const NgxRemasterBridge&) = delete;
    NgxRemasterBridge& operator=(const NgxRemasterBridge&) = delete;

    bool open(std::string* error = nullptr);
    void close();
    bool loaded() const noexcept { return handle_ != nullptr || embedded_; }
    const std::string& path() const noexcept { return path_; }
    const std::string& error() const noexcept { return error_; }

    RtxResult queryCaps(RtxNgxBridgeCaps* caps) const;
    RtxResult create(const RtxNgxBridgeConfig* config, RtxNgxBridge** bridge) const;
    void releaseBridge(RtxNgxBridge* bridge) const;
    RtxResult remaster(RtxNgxBridge* bridge, const RtxNgxRemasterInput* input,
                       RtxNgxRemasterOutput* output, RtxNgxRemasterStats* stats) const;
    RtxResult getRemasterStats(RtxNgxBridge* bridge, RtxNgxRemasterStats* stats) const;
    RtxResult setLut3d(RtxNgxBridge* bridge, const uint16_t* lut_half4_data,
                       int32_t grid_size) const;
    RtxResult previewBuffer(RtxNgxBridge* bridge, uint32_t slot, uint64_t* device_ptr,
                            uint32_t* pitch) const;
    RtxResult downloadPreview(RtxNgxBridge* bridge, void* destination,
                              size_t destination_bytes) const;
    int32_t borrowPreview(RtxNgxBridge* bridge, uint32_t slot, void** out_d3d12_device,
                          void** out_d3d12_resource, uint32_t* out_pitch) const;
    bool hasBorrowPreviewApi() const noexcept { return borrow_preview_ != nullptr; }
    int32_t playbackPack(RtxNgxBridge* bridge, uint64_t y_plane, uint64_t uv_plane,
                         uint32_t y_pitch, uint32_t uv_pitch, uint32_t width, uint32_t height,
                         int32_t format, int32_t full_range, int32_t matrix, uint32_t slot) const;
    bool hasPlaybackPackApi() const noexcept { return playback_pack_ != nullptr; }

    [[nodiscard]] bool acquirePreviewSlot(std::uint32_t* out_slot) noexcept;
    void publishPreviewSlot(std::uint32_t slot) noexcept;
    [[nodiscard]] PreviewSlotFlags previewSlotFlagsArray() noexcept;
    [[nodiscard]] std::uint32_t latestPublishedSlot() const noexcept;
    [[nodiscard]] bool borrowPreviewSlot(std::uint32_t slot, void** out_d3d12_device,
                                         void** out_d3d12_resource,
                                         std::uint32_t* out_pitch) noexcept;
    static void publishActivePreviewBridge(NgxRemasterBridge& loader, RtxNgxBridge* bridge);
    static void clearActivePreviewBridge(NgxRemasterBridge* loader) noexcept;
    static bool borrowActivePreview(std::uint32_t slot, void** out_d3d12_device,
                                    void** out_d3d12_resource, std::uint32_t* out_pitch);
    [[nodiscard]] static std::uint32_t latestPreviewSlot();
    [[nodiscard]] static PreviewSlotFlags previewSlotFlags();
    RtxResult uploadFrame(RtxNgxBridge* bridge, const void* y_plane, const void* uv_plane,
                          uint32_t y_pitch, uint32_t uv_pitch, int32_t width, int32_t height,
                          uint64_t* y_device, uint64_t* uv_device) const;
    RtxResult interpolateNv12(RtxNgxBridge* bridge, uint64_t prev_nv12, uint64_t curr_nv12,
                             uint32_t pitch, float alpha, uint64_t out_nv12) const;
    uint64_t getInterpBuffer(RtxNgxBridge* bridge, uint32_t width, uint32_t height) const;
    int32_t initRife(RtxNgxBridge* bridge, const char* engine_path, uint32_t width, uint32_t height) const;
    int32_t initRifeEx(RtxNgxBridge* bridge, const char* engine_path,
                       const RtxRifeInitConfig* config, uint64_t* out_trt_peak_bytes) const;
    bool hasRifeExApi() const noexcept { return init_rife_ex_ != nullptr; }
    int32_t interpolateRife(RtxNgxBridge* bridge, uint64_t prev_nv12, uint64_t curr_nv12,
                           uint32_t pitch, float alpha, uint64_t out_nv12, int32_t full_range,
                           int32_t* scene_cut) const;
    int32_t buildRifeEngine(const char* onnx_path, const char* engine_out_path,
                           uint32_t width, uint32_t height, char* err_buf, uint32_t err_cap) const;
    void destroyRife(RtxNgxBridge* bridge) const;
    bool hasRifeApi() const noexcept { return init_rife_ != nullptr; }
    bool hasPhase7Api() const noexcept { return set_lut3d_ != nullptr; }
    const char* lastError(RtxNgxBridge* bridge) const;

private:

    static constexpr std::uint32_t kPreviewSlotCount = 3;

    PreviewSlotFlags preview_slot_flags_ = std::make_shared<std::array<std::atomic<std::uint32_t>, kPreviewSlotCount>>();
    std::atomic<std::uint32_t> preview_latest_slot_{0};
    RtxNgxBridge* preview_bridge_ = nullptr;

    void* handle_ = nullptr;
    bool embedded_ = false;
    std::string path_;
    std::string error_;

    RtxResult(RTX_SHIM_CALL* query_caps_)(RtxNgxBridgeCaps*) = nullptr;
    RtxResult(RTX_SHIM_CALL* create_)(const RtxNgxBridgeConfig*, RtxNgxBridge**) = nullptr;
    void(RTX_SHIM_CALL* destroy_)(RtxNgxBridge*) = nullptr;
    RtxResult(RTX_SHIM_CALL* remaster_)(RtxNgxBridge*, const RtxNgxRemasterInput*,
                                        RtxNgxRemasterOutput*, RtxNgxRemasterStats*) = nullptr;
    RtxResult(RTX_SHIM_CALL* get_remaster_stats_)(RtxNgxBridge*, RtxNgxRemasterStats*) = nullptr;
    const char*(RTX_SHIM_CALL* last_error_)(RtxNgxBridge*) = nullptr;
    RtxResult(RTX_SHIM_CALL* set_lut3d_)(RtxNgxBridge*, const uint16_t*, int32_t) = nullptr;
    RtxResult(RTX_SHIM_CALL* preview_buffer_)(RtxNgxBridge*, uint32_t, uint64_t*,
                                              uint32_t*) = nullptr;
    RtxResult(RTX_SHIM_CALL* download_preview_)(RtxNgxBridge*, void*, size_t) = nullptr;
    int32_t(RTX_SHIM_CALL* borrow_preview_)(RtxNgxBridge*, uint32_t, void**, void**,
                                            uint32_t*) = nullptr;
    int32_t(RTX_SHIM_CALL* playback_pack_)(RtxNgxBridge*, uint64_t, uint64_t, uint32_t, uint32_t,
                                           uint32_t, uint32_t, int32_t, int32_t, int32_t,
                                           uint32_t) = nullptr;
    RtxResult(RTX_SHIM_CALL* upload_frame_)(RtxNgxBridge*, const void*, const void*, uint32_t,
                                            uint32_t, int32_t, int32_t, uint64_t*,
                                            uint64_t*) = nullptr;
    int32_t(RTX_SHIM_CALL* interpolate_nv12_)(RtxNgxBridge*, uint64_t, uint64_t, uint32_t, float, uint64_t) = nullptr;
    uint64_t(RTX_SHIM_CALL* get_interp_buffer_)(RtxNgxBridge*, uint32_t, uint32_t) = nullptr;
    int32_t(RTX_SHIM_CALL* init_rife_)(RtxNgxBridge*, const char*, uint32_t, uint32_t) = nullptr;
    int32_t(RTX_SHIM_CALL* init_rife_ex_)(RtxNgxBridge*, const char*, const RtxRifeInitConfig*, uint64_t*) = nullptr;
    int32_t(RTX_SHIM_CALL* interpolate_rife_)(RtxNgxBridge*, uint64_t, uint64_t, uint32_t, float, uint64_t, int32_t, int32_t*) = nullptr;
    int32_t(RTX_SHIM_CALL* build_rife_engine_)(const char*, const char*, uint32_t, uint32_t, char*, uint32_t) = nullptr;
    void(RTX_SHIM_CALL* destroy_rife_)(RtxNgxBridge*) = nullptr;
};

}  // namespace rtx
