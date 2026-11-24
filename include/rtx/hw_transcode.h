// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Unified NVIDIA remaster transcode pipeline: libavformat demux -> NVDEC ->
// Dual-Path FRUC (NVOFA / TensorRT RIFE) -> NGX VSR + TrueHDR -> P010 Pack v2
// -> NVENC 10-bit (AV1 / HEVC Main10) -> libavformat mux, with all frames
// remaining in device memory (zero host pixel copies).

#pragma once

#include "rtx/io.h"
#include "rtx/render_graph.h"
#include "rtx/rational.h"
#include "rtx/status.h"
#include "rtx/types.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <stop_token>
#include <string>
#include <tuple>

namespace rtx {

// Snapshot handed to the optional progress callback after every decoded/encoded frame.
// Invoked from the transcoding thread; consumers must not block.
// Phase 12 (Wave 12.2-C): tuple alias (frames_decoded, frames_encoded, total_frames, elapsed_ms).
using HwTranscodeProgress = std::tuple<int64_t, int64_t, int64_t, double>;

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 硬件转码全链路遥测与性能账单聚合（35+ 具名指标，涵盖零拷贝字节统计、重制耗时与音画计数），供 CLI/GUI/JSON 报告消费。
// Invariant: 零拷贝主循环成立时 decoder_host_pixel_copies 与 encoder_host_pixel_copies 严格为 0。
struct HwTranscodeOptions {
    std::filesystem::path input_path;
    std::filesystem::path output_path;

    int frame_limit = 0;  // 0 = consume the whole stream

    VideoCodec codec = VideoCodec::AV1;  // AV1 10-bit (default) or HEVC Main10
    bool use_cpu_encoder = false;        // true = CPU SVT-AV1 / x265 encoder
    std::string cpu_preset = "4";        // SVT-AV1 0..13 or x265 preset string
    int bitrate_kbps = 0;                // 0 = auto
    int preset = 5;                      // 1..7 (NVENC P1..P7, default P5)
    int rate_control = 3;                // 0 = CBR, 1 = VBR, 2 = const-QP, 3 = CQ, 4 = CRF
    int qp = 22;                         // 0..51 (unified default 22 across CLI/GUI/WebUI)
    bool copy_audio = true;
    bool vsr_before_fg = true;           // true = VSR upscale before Frame Gen (vsr-first)
// Phase 7 (Task 7-B2): emit SMPTE ST 2094-40 (HDR10+) dynamic metadata
// per frame (AV1 OBU_METADATA / HEVC prefix SEI).
    bool hdr10plus = false;
// Phase 7 (Task 7-B1): Adobe .cube 3D LUT applied inside the fused pack
// kernel (empty = no LUT).
    std::filesystem::path lut_path;

// Product remaster geometry. Decoded NV12/P010 frames always traverse
// the NGX bridge (preprocess -> VSR -> TrueHDR -> pack v2) before NVENC.
    int target_width = 3840;   // 4K UHD default (0 = native source resolution)
    int target_height = 2160;  // 4K UHD default (0 = native source resolution)
    bool vsr_enabled = true;
    int vsr_quality = 4;    // 1..4, NGX VSR quality level (default 4 Ultra)
    int peak_nits = 1000;   // 400..4000, TrueHDR peak luminance in cd/m^2
    int geometry_mode = 0;  // 0 = AutoDirect (9:16 portrait aware), 1 = Pillarbox

// Explicit GPU binding for the NVDEC/NVENC sessions (0 = default device).
    int device_index = 0;
// GOP-segmented transcode start point. Seek to key frame at/before timestamp (ms).
    int64_t seek_start_ms = -1;

// Frame-rate up-conversion (FRUC) options (defaults to 60fps hardware optical flow).
    bool fruc_enabled = true;
    Rational target_fps{60, 1};  // Default 60 FPS target
    std::filesystem::path rife_engine_path;  // Route B: TensorRT RIFE (.engine)

    bool verbose = false;

// Polled once per frame by the container path. A set flag aborts the
// transcode cleanly (flushing already encoded frames and writing the
// container trailer) and returns ErrorCode::State ("transcode cancelled").
    const std::atomic<bool>* cancel = nullptr;
// Phase 7 realtime monitoring: the GUI publishes the A/B split position
// (0..1) through this atomic; the transcode loop samples it per frame.
    const std::atomic<float>* split_ratio = nullptr;
// Phase 7 realtime monitoring: invoked from the transcode thread at most
// 15 times per second with the latest 960x540 R10G10B10A2 preview frame
// already staged in host memory (pitch in bytes, rows padded).
    std::function<void(const void* r10g10b10a2_data, std::uint32_t pitch)> preview;
    std::function<void(const HwTranscodeProgress&)> progress;
};

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 多字段领域模型：具名字段承载序列化/校验/反射契约，压进 std::tuple 会丢失全部字段语义与默认值契约。
// Invariant: 字段集合与默认值即对外契约（schema/测试逐字段锚定）；只增不删。
struct HwTranscodeReport {
    std::string backend = "nvidia";
    std::string device;
    std::string path;  // "vsr-remaster"

    std::string input_container;
    std::string output_container;
    std::string input_codec;
    std::string output_codec;
    int input_width = 0;
    int input_height = 0;
    int output_width = 0;
    int output_height = 0;
    int output_bit_depth = 10;

    int64_t packets_read = 0;
    int64_t packets_written = 0;
    int64_t frames_decoded = 0;
    int64_t frames_encoded = 0;
    uint64_t bitstream_bytes = 0;

    uint64_t decoder_device_to_device_bytes = 0;
    uint64_t decoder_device_to_host_bytes = 0;
    uint64_t decoder_host_pixel_copies = 0;
    uint64_t encoder_host_to_device_bytes = 0;
    uint64_t encoder_host_pixel_copies = 0;

    bool zero_copy_main_loop = false;
    double elapsed_ms = 0.0;
    double main_loop_ms = 0.0;

    double remaster_preprocess_ms = 0.0;
    double remaster_ngx_ms = 0.0;
    double remaster_pack_kernel_ms = 0.0;
    double remaster_frame_ms = 0.0;
    double remaster_pack_kernel_max_ms = 0.0;
    uint64_t remaster_pack_launches = 0;
    int vsr_quality = 0;
    int measured_max_cll_nits = 0;
    int measured_max_fall_nits = 0;

// Phase 8: dual-path FRUC statistics
    int64_t frames_interpolated = 0;
    int64_t fruc_scene_cuts = 0;
    int64_t fruc_failures = 0;
    std::string fruc_backend = "off";

// Emits a machine-readable JSON record.
    std::string toJson() const;
};

Result<HwTranscodeReport> runNvidiaRemasterTranscode(const HwTranscodeOptions& options);

// Phase 11.3-S (S-1.1): 多段执行图连续导出入口（单 NVENC + 单 Muxer 不断流，硬切点强制 IDR）。
[[nodiscard]] Result<HwTranscodeReport> runNvidiaRemasterRenderGraph(
    const HwTranscodeOptions& options,
    std::span<const RenderGraphSegment> segments,
    std::span<const AudioGraphSegment> audio_segments = {},
    std::stop_token stop_tok = {});

// Phase 11.2 (Task 5-1 / C-02): runScrubPreview removed -- the fake-playback
// scrub path performed a 15 Hz D2H readback that the zero-readback monitor
// (MediaPlaybackSession + rtx_ngx_bridge_borrow_preview) replaces entirely.
}  // namespace rtx
