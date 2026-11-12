// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Stage-1 NVIDIA transcode pipeline: libavformat demux -> NVDEC -> NVENC ->
// libavformat mux, with decoded frames staying in shim-owned device memory.
// The raw NV12/P010 artefact path exercises the same NVDEC/NVENC sessions but
// explicitly accounts for its host staging traffic.

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

// Snapshot handed to the optional progress callback after every decoded frame.
// It is called from the transcoding thread; consumers must not block.
struct HwTranscodeProgress {
    int64_t frames_decoded = 0;
    int64_t frames_encoded = 0;
    int64_t total_frames = 0;  // 0 when the container has no duration
    double elapsed_ms = 0.0;
};

struct HwTranscodeOptions {
    std::filesystem::path input_path;
    std::filesystem::path output_path;

    // Raw NV12 input geometry.  Container mode reads geometry from the demuxer.
    int width = 0;
    int height = 0;
    int frame_limit = 0;  // 0 = consume the whole stream

    VideoCodec codec = VideoCodec::AV1;
    int bit_depth = 0;  // 0 = preserve source depth
    int bitrate_kbps = 0;
    int preset = 5;
    int rate_control = 0;  // 0 = CBR, 1 = VBR, 2 = const-QP
    int qp = 0;
    Rational fps{0, 1};
    bool hdr = true;
    bool copy_audio = true;
    // Phase 7 (Task 7-B2): emit SMPTE ST 2094-40 (HDR10+) dynamic metadata
    // per frame (AV1 OBU_METADATA / HEVC prefix SEI).
    bool hdr10plus = false;
    // Phase 7 (Task 7-B1): Adobe .cube 3D LUT applied inside the fused pack
    // kernel (empty = no LUT).
    std::filesystem::path lut_path;

    // Product remaster geometry. The product has no passthrough mode: decoded
    // NV12/P010 frames always traverse the NGX bridge (preprocess -> VSR ->
    // TrueHDR -> pack v2) before NVENC.
    int target_width = 0;   // 0 = 3840 default, resolved by the transcode path
    int target_height = 0;  // 0 = 2160 default
    bool vsr_enabled = true;
    int vsr_quality = 2;    // 1..4, NGX VSR quality level
    int peak_nits = 1000;   // 400..4000, TrueHDR peak luminance in cd/m^2
    int geometry_mode = 0;  // 0 = AutoDirect (9:16 portrait aware), 1 = Pillarbox

    // Phase 8 (Task 8-A1): explicit GPU binding for the NVDEC/NVENC sessions
    // (0 = default device; must be < the rtx_shim topology device count).
    int device_index = 0;
    // Phase 8 (Task 8-A2 / Task 8-C1): GOP-segmented transcode start point.
    // The demuxer seeks to the key frame at or before this timestamp before
    // the first packet is read (milliseconds into the stream; -1 = start).
    int64_t seek_start_ms = -1;
    // Phase 8 (Task 8-C1, test hook only): inject one simulated
    // RTX_ERR_DEVICE_LOST from the remaster call at this encoded-frame count.
    // 0 disables injection; the anti-TDR self-heal path is exercised by
    // rtx_tests --filter tdr_recovery on real hardware.
    int fault_inject_frame = 0;

    bool verbose = false;

    // Polled once per frame by the container path.  A set flag aborts the
    // transcode with ErrorCode::State and a "cancelled" detail.
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
    // Phase 7: real GPU-reduced luminance statistics over the whole stream
    // (nits).  Zero when the run produced no frame statistics.
    int measured_max_cll_nits = 0;
    int measured_max_fall_nits = 0;

    // Emits a machine-readable record for the Gate 1 audit script.
    std::string toJson() const;
};

// Verification-only container passthrough (NVDEC -> NVENC).  This is not a
// product path: CineRTX.exe always remasters, and this entry point exists for
// the codec/zero-copy regression harness.
Result<HwTranscodeReport> runNvidiaContainerTranscode(const HwTranscodeOptions& options);

// The only product transcode: NVDEC NV12/P010 -> rtx_ngx_bridge remaster
// (VSR + TrueHDR + pack v2, all device memory) -> NVENC HEVC Main10 with HDR10
// VUI/SEI.  Both GUI and CLI call this function; it reports path =
// "vsr-remaster" with zero host pixel copies.
Result<HwTranscodeReport> runNvidiaRemasterTranscode(const HwTranscodeOptions& options);

// Phase 7 (Task 7-C2, verdict §4.3 iron rule 3): timeline scrubbing.  Seeks
// the input to the key frame at or before `seek_pts_ms` (milliseconds into
// the stream), decodes exactly one frame, remasters it through the bridge
// with the A/B preview path enabled and publishes the 960x540 R10G10B10A2
// frame through options.preview.  No output file is produced.
Result<void> runScrubPreview(const HwTranscodeOptions& options, int64_t seek_pts_ms);



}  // namespace rtx
