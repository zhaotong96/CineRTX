// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Unified NVIDIA remaster transcode pipeline: libavformat demux -> NVDEC ->
// Dual-Path FRUC (NVOFA / TensorRT RIFE) -> NGX VSR + TrueHDR -> P010 Pack v2
// -> NVENC 10-bit (AV1 / HEVC Main10) -> libavformat mux, with all frames
// remaining in device memory (zero host pixel copies).

#pragma once

#include "rtx/io.h"
#include "rtx/rational.h"
#include "rtx/status.h"
#include "rtx/types.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace rtx {

// Snapshot handed to the optional progress callback after every decoded/encoded frame.
// Invoked from the transcoding thread; consumers must not block.
struct HwTranscodeProgress {
    int64_t frames_decoded = 0;
    int64_t frames_encoded = 0;
    int64_t total_frames = 0;  // 0 when the container has no duration
    double elapsed_ms = 0.0;
};

struct HwTranscodeOptions {
    std::filesystem::path input_path;
    std::filesystem::path output_path;

    int frame_limit = 0;  // 0 = consume the whole stream

    VideoCodec codec = VideoCodec::AV1;  // AV1 10-bit (default) or HEVC Main10
    int bitrate_kbps = 0;                // 0 = auto
    int preset = 5;                      // 1..7 (NVENC P1..P7, default P5)
    int rate_control = 0;                // 0 = CBR, 1 = VBR, 2 = const-QP
    int qp = 24;                         // 0..51 (unified default 24 across CLI/GUI/WebUI)
    bool copy_audio = true;
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

Result<void> runScrubPreview(const HwTranscodeOptions& options, int64_t seek_pts_ms);

}  // namespace rtx
