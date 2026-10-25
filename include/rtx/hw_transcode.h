// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Stage-1 NVIDIA transcode pipeline: libavformat demux -> NVDEC -> NVENC ->
// libavformat mux, with decoded frames staying in shim-owned device memory.
// The raw NV12/P010 artefact path exercises the same NVDEC/NVENC sessions but
// explicitly accounts for its host staging traffic.

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "rtx/io.h"
#include "rtx/rational.h"
#include "rtx/status.h"
#include "rtx/types.h"

namespace rtx {

// Snapshot handed to the optional progress callback after every decoded frame.
// It is called from the transcoding thread; consumers must not block.
struct HwTranscodeProgress {
    int64_t frames_decoded = 0;
    int64_t frames_encoded = 0;
    int64_t total_frames = 0;  // 0 when the container has no duration
    double elapsed_ms = 0.0;
};

struct HwTranscodeOptions {
    std::string input_path;
    std::string output_path;

    // Raw NV12 input geometry.  Container mode reads geometry from the demuxer.
    int width = 0;
    int height = 0;
    int frame_limit = 0;  // 0 = consume the whole stream

    VideoCodec codec = VideoCodec::HEVC;
    int bit_depth = 0;  // 0 = preserve source depth
    int bitrate_kbps = 0;
    int preset = 5;
    int rate_control = 0;  // 0 = CBR, 1 = VBR, 2 = const-QP
    int qp = 0;
    Rational fps{0, 1};
    bool hdr = true;

    // VSR-remaster path (P0-1.5): route decoded NV12/P010 frames through the
    // NGX bridge (preprocess -> VSR -> TrueHDR -> pack v2) before NVENC.
    bool remaster = false;
    int target_width = 0;   // 0 = 3840 default, resolved by the transcode path
    int target_height = 0;  // 0 = 2160 default
    int vsr_quality = 2;    // 1..4, NGX VSR quality level

    // Allows the raw artefact path to retry with HEVC 8-bit / H.264 when the
    // installed encoder cannot do HEVC Main10 (for example Maxwell GM204).
    bool allow_encoder_fallback = true;
    bool verbose = false;

    // Polled once per frame by the container path.  A set flag aborts the
    // transcode with ErrorCode::State and a "cancelled" detail.
    const std::atomic<bool>* cancel = nullptr;
    std::function<void(const HwTranscodeProgress&)> progress;
};

struct HwTranscodeReport {
    std::string backend = "nvidia";
    std::string device;
    std::string path;  // "container" or "raw-artifact"

    std::string input_container;
    std::string output_container;
    std::string input_codec;
    std::string output_codec;
    int input_width = 0;
    int input_height = 0;
    int output_width = 0;
    int output_height = 0;
    int output_bit_depth = 8;

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
    // Wall time of the demux -> decode -> remaster -> encode loop only,
    // excluding demuxer open, NGX/bridge creation and NVENC session setup.
    // Zero when the path does not measure it separately.
    double main_loop_ms = 0.0;

    // VSR-remaster stage accounting, averaged over report.frames_encoded
    // (max_* is the worst single frame).  All zero on the non-remaster paths.
    double remaster_preprocess_ms = 0.0;
    double remaster_ngx_ms = 0.0;
    double remaster_pack_kernel_ms = 0.0;
    double remaster_frame_ms = 0.0;
    double remaster_pack_kernel_max_ms = 0.0;
    uint64_t remaster_pack_launches = 0;
    int vsr_quality = 0;

    // Emits a machine-readable record for the Gate 1 audit script.
    std::string toJson() const;
};

// Container transcode.  The pixel path is demux -> NVDEC slot -> NVENC;
// no host pixel copy is performed by this function.
Result<HwTranscodeReport> runNvidiaContainerTranscode(const HwTranscodeOptions& options);

// Production VSR-remaster container transcode (P0-1.5): NVDEC NV12/P010 ->
// rtx_ngx_bridge remaster (VSR + TrueHDR + pack v2, all device memory) ->
// NVENC HEVC Main10 with HDR10 VUI/SEI.  Requires options.remaster and the
// HEVC codec; reports path = "vsr-remaster" with zero host pixel copies.
Result<HwTranscodeReport> runNvidiaRemasterTranscode(const HwTranscodeOptions& options);

// Raw NV12 -> NVENC -> Annex-B -> NVDEC -> host P010 artefact.  Both hardware
// codecs are exercised, but the host staging counters are reported separately
// and zero_copy_main_loop is false for this diagnostic path.
Result<HwTranscodeReport> runNvidiaRawTranscode(const HwTranscodeOptions& options);

}  // namespace rtx
