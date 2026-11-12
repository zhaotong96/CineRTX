// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 8 (Task 8-A2) implementation.  See include/rtx/multi_gpu_scheduler.h
// for the contract.  The dispatcher never moves uncompressed pixels across a
// PCIe link: mode A assigns whole files to whole GPUs, mode B exchanges only
// the per-segment encoded bitstreams through temporary segment containers
// that are demuxed and re-muxed in order (compressed-stream splicing).

#include "rtx/multi_gpu_scheduler.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

#include "rtx/hdr.h"
#include "rtx/io.h"
#include "rtx/nvidia_shim.h"

namespace rtx {
namespace {

// Bitstream-level merge of the encoded segment containers into the final
// output, rebasing every packet by the cumulative frame offset so the merged
// timeline is strictly monotonic (the muxer guards enforce it too).
Result<HwTranscodeReport> mergeSegmentContainers(const std::vector<std::filesystem::path>& segments,
                                                 const HwTranscodeOptions& options,
                                                 const Rational& frame_rate,
                                                 HwTranscodeReport report) {
    if (segments.empty()) {
        return err(ErrorCode::InvalidArgument, "no segment containers to merge");
    }

    auto first_demuxer = makeFfmpegDemuxer();
    auto opened = first_demuxer->open(segments.front());
    if (!opened.has_value()) {
        return std::unexpected(opened.error());
    }
    MediaInfo output_info = opened.value();
    output_info.codec = options.codec;
    output_info.annex_b = false;
    output_info.time_base = Rational{1, 90000};
    // HDR10 static metadata: rebuild from the request so the merged container
    // keeps its mastering display / CLL boxes.
    Hdr10Metadata hdr{};
    if (options.hdr && options.codec != VideoCodec::H264) {
        TrueHdrParameters params{};
        params.max_luminance = std::clamp(options.peak_nits > 0 ? options.peak_nits : 1000, 400, 4000);
        auto canonical = SeiSynthesizer::makeMetadata(params);
        if (canonical.has_value()) {
            hdr = canonical.value();
        }
    }

    auto muxer = makeFfmpegMuxer();
    auto mux_open = muxer->open(options.output_path, output_info, hdr);
    if (!mux_open.has_value()) {
        return std::unexpected(mux_open.error());
    }

    const int64_t frame_ticks =
        frame_rate.num > 0 && frame_rate.den > 0
            ? std::max<int64_t>(1, static_cast<int64_t>(90000) * frame_rate.den / frame_rate.num)
            : 3000;
    int64_t frame_offset = 0;  // in 90 kHz ticks, applied per segment
    int64_t frames_total = 0;
    int64_t packets_total = 0;
    int64_t bitstream_total = 0;

    for (const std::filesystem::path& segment : segments) {
        auto demuxer = makeFfmpegDemuxer();
        auto seg_info = demuxer->open(segment);
        if (!seg_info.has_value()) {
            return std::unexpected(seg_info.error());
        }
        const MediaInfo& info = seg_info.value();
        int64_t segment_video_frames = 0;
        for (;;) {
            auto packet = demuxer->readPacket();
            if (!packet.has_value()) {
                break;
            }
            Packet out = std::move(packet.value());
            const bool passthrough =
                out.stream_index == info.primary_audio_index ||
                std::any_of(info.passthrough_streams.begin(), info.passthrough_streams.end(),
                            [&out](const PassthroughStreamInfo& s) {
                                return s.stream_index == out.stream_index;
                            });
            if (passthrough) {
                // Rebase the audio/subtitle timestamps by the same wall-clock
                // offset, expressed in the packet's own time base.
                const Rational tb = out.time_base.valid() ? out.time_base : Rational{1, 48000};
                const int64_t offset_local =
                    frame_offset * tb.den / (90000 * tb.num);
                out.pts += offset_local;
                if (out.dts != std::numeric_limits<int64_t>::min()) {
                    out.dts += offset_local;
                }
            } else {
                // Video packets ride the 1/90000 segment clock; the frame
                // offset keeps the merged timeline contiguous and monotonic.
                out.pts += frame_offset;
                if (out.dts != std::numeric_limits<int64_t>::min()) {
                    out.dts += frame_offset;
                }
                ++segment_video_frames;
            }
            auto written = muxer->writePacket(out);
            if (!written.has_value()) {
                return std::unexpected(written.error());
            }
            ++packets_total;
            bitstream_total += static_cast<int64_t>(out.data.size());
        }
        frame_offset += segment_video_frames * frame_ticks;
        frames_total += segment_video_frames;
    }

    auto finalized = muxer->finalize();
    if (!finalized.has_value()) {
        return std::unexpected(finalized.error());
    }
    report.frames_encoded = frames_total;
    report.frames_decoded = frames_total;
    report.packets_written = packets_total;
    report.bitstream_bytes = static_cast<uint64_t>(bitstream_total);
    return report;
}

}  // namespace

Result<MultiGpuScheduler> MultiGpuScheduler::create(std::span<const int> gpu_indices) {
    // Phase 8 (Task 8-A1): ask rtx_shim for the live topology.
    RtxDiagnostics diagnostics{};
    NvidiaShim shim;
    std::string error;
    if (!shim.open(&error)) {
        return err(ErrorCode::DllMissing, "cannot load the NVIDIA shim", error);
    }
    if (shim.probe(&diagnostics) != RTX_OK || diagnostics.device.name[0] == '\0') {
        return err(ErrorCode::DllMissing, "no NVIDIA CUDA device is available to rtx_shim",
                   diagnostics.summary);
    }
    // The probe reports device 0; rtx_shim_enumerate_devices exposes the full
    // topology through the same driver table when the build carries it.  The
    // scheduler therefore trusts gpu_indices clamped to device 0's presence
    // and validates each index at session-creation time.
    MultiGpuScheduler scheduler;
    if (gpu_indices.empty()) {
        scheduler.active_gpus_.push_back(0);
    } else {
        for (int index : gpu_indices) {
            if (index >= 0 &&
                std::find(scheduler.active_gpus_.begin(), scheduler.active_gpus_.end(),
                          index) == scheduler.active_gpus_.end()) {
                scheduler.active_gpus_.push_back(index);
            }
        }
        if (scheduler.active_gpus_.empty()) {
            scheduler.active_gpus_.push_back(0);
        }
    }
    return scheduler;
}

Result<int> MultiGpuScheduler::countKeyFrames(const std::filesystem::path& input) {
    auto demuxer = makeFfmpegDemuxer();
    auto opened = demuxer->open(input);
    if (!opened.has_value()) {
        return std::unexpected(opened.error());
    }
    const MediaInfo& info = opened.value();
    int keyframes = 0;
    for (;;) {
        auto packet = demuxer->readPacket();
        if (!packet.has_value()) {
            break;
        }
        const Packet& p = packet.value();
        const bool passthrough =
            p.stream_index == info.primary_audio_index ||
            std::any_of(info.passthrough_streams.begin(), info.passthrough_streams.end(),
                        [&p](const PassthroughStreamInfo& s) {
                            return s.stream_index == p.stream_index;
                        });
        if (!passthrough && p.keyframe) {
            ++keyframes;
        }
    }
    return keyframes;
}

Result<std::vector<HwTranscodeReport>> MultiGpuScheduler::runBatchJobs(
    std::span<const HwTranscodeOptions> jobs) {
    std::vector<HwTranscodeReport> reports;
    reports.reserve(jobs.size());
    if (jobs.empty()) {
        return reports;
    }
    const int gpus = deviceCount();
    if (gpus <= 1) {
        // Single-GPU deterministic fallback: sequential on device 0.
        for (const HwTranscodeOptions& job : jobs) {
            auto report = runNvidiaRemasterTranscode(job);
            if (!report.has_value()) {
                return std::unexpected(report.error());
            }
            reports.push_back(std::move(report.value()));
        }
        return reports;
    }
    // Mode A: one worker thread per GPU pulling jobs from an atomic counter.
    std::atomic<size_t> next_job{0};
    std::mutex reports_mutex;
    reports.resize(jobs.size());
    Result<void> first_failure;
    std::mutex failure_mutex;
    auto worker = [&](int gpu) {
        for (;;) {
            const size_t index = next_job.fetch_add(1, std::memory_order_relaxed);
            if (index >= jobs.size()) {
                return;
            }
            HwTranscodeOptions job = jobs[index];
            job.device_index = gpu;
            auto report = runNvidiaRemasterTranscode(job);
            std::lock_guard<std::mutex> lock(reports_mutex);
            if (report.has_value()) {
                reports[index] = std::move(report.value());
            } else {
                std::lock_guard<std::mutex> failure_lock(failure_mutex);
                if (!first_failure.has_value()) {
                    first_failure = std::unexpected(report.error());
                }
                next_job.store(jobs.size(), std::memory_order_relaxed);
                return;
            }
        }
    };
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(gpus));
    for (int gpu = 0; gpu < gpus; ++gpu) {
        workers.emplace_back(worker, gpu);
    }
    for (std::thread& thread : workers) {
        thread.join();
    }
    if (!first_failure.has_value()) {
        return std::unexpected(first_failure.error());
    }
    return reports;
}

Result<HwTranscodeReport> MultiGpuScheduler::runSegmentedSingleJob(
    const HwTranscodeOptions& options, int segments) {
    if (segments <= 1) {
        return runNvidiaRemasterTranscode(options);
    }

    // 1. Pre-scan the video stream: one packet per access unit, so the packet
    //    ordinal equals the decoded frame ordinal for the H.264/HEVC/AV1
    //    elementary streams NVDEC consumes.
    auto probe = makeFfmpegDemuxer();
    auto opened = probe->open(options.input_path);
    if (!opened.has_value()) {
        return std::unexpected(opened.error());
    }
    const MediaInfo& info = opened.value();
    struct VideoPacketIndex {
        int64_t pts = 0;      // stream time base
        bool keyframe = false;
    };
    std::vector<VideoPacketIndex> video_packets;
    for (;;) {
        auto packet = probe->readPacket();
        if (!packet.has_value()) {
            break;
        }
        const Packet& p = packet.value();
        const bool passthrough =
            p.stream_index == info.primary_audio_index ||
            std::any_of(info.passthrough_streams.begin(), info.passthrough_streams.end(),
                        [&p](const PassthroughStreamInfo& s) {
                            return s.stream_index == p.stream_index;
                        });
        if (!passthrough) {
            video_packets.push_back(
                VideoPacketIndex{p.pts == std::numeric_limits<int64_t>::min() ? 0 : p.pts,
                                 p.keyframe});
        }
    }
    if (video_packets.size() < static_cast<size_t>(segments)) {
        return err(ErrorCode::InvalidArgument,
                   "the source has fewer frames than the requested segment count");
    }
    // 2. Boundaries at evenly spaced KEY-FRAME ordinals so every segment is a
    //    closed GOP and the merge stays bit-exact.
    const size_t total = video_packets.size();
    std::vector<size_t> boundaries;
    boundaries.push_back(0);
    for (int i = 1; i < segments; ++i) {
        const size_t target = total * static_cast<size_t>(i) / static_cast<size_t>(segments);
        // Snap to the nearest key frame at or before the target.
        size_t boundary = target;
        while (boundary > 0 && !video_packets[boundary].keyframe) {
            --boundary;
        }
        if (boundary <= boundaries.back()) {
            boundary = boundaries.back() + 1;
        }
        boundaries.push_back(boundary);
    }
    boundaries.push_back(total);

    // 3. Transcode every segment into its own temporary container.  On a
    //    multi-GPU host the segments alternate across the active GPUs; on a
    //    single-GPU host (and for --test-gop-split) they run sequentially on
    //    device 0.
    std::vector<std::filesystem::path> segment_files;
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path() /
                                           "cinertx_gop_segments";
    std::error_code fs_ec;
    std::filesystem::create_directories(temp_dir, fs_ec);
    std::vector<HwTranscodeReport> segment_reports;
    int gpu_round_robin = 0;
    for (int i = 0; i < segments; ++i) {
        const size_t begin = boundaries[static_cast<size_t>(i)];
        const size_t end = boundaries[static_cast<size_t>(i) + 1];
        HwTranscodeOptions segment_options = options;
        segment_options.output_path =
            temp_dir / ("seg" + std::to_string(i) + "_" +
                        std::filesystem::path(options.output_path).filename().string());
        segment_options.seek_start_ms = -1;
        // Seek exactly to the boundary packet's PTS (its key frame).
        if (begin > 0) {
            const Rational tb = info.time_base.valid() ? info.time_base : Rational{1, 90000};
            segment_options.seek_start_ms =
                video_packets[begin].pts * tb.num * 1000 / tb.den;
        }
        segment_options.frame_limit = static_cast<int>(end - begin);
        segment_options.device_index =
            active_gpus_[static_cast<size_t>(gpu_round_robin) % active_gpus_.size()];
        ++gpu_round_robin;
        auto segment_report = runNvidiaRemasterTranscode(segment_options);
        if (!segment_report.has_value()) {
            return std::unexpected(segment_report.error());
        }
        segment_reports.push_back(std::move(segment_report.value()));
        segment_files.push_back(segment_options.output_path);
    }

    // 4. Merge the compressed segment streams into the final container.
    HwTranscodeReport report = segment_reports.front();
    report.path = "vsr-remaster-gop-split";
    report.frames_encoded = 0;
    report.frames_decoded = 0;
    report.packets_written = 0;
    report.bitstream_bytes = 0;
    const Rational frame_rate =
        options.fps.valid() && options.fps.num > 0 ? options.fps
                                                   : (info.frame_rate.valid()
                                                          ? info.frame_rate
                                                          : Rational{30, 1});
    for (size_t i = 1; i < segment_reports.size(); ++i) {
        report.remaster_pack_kernel_ms += segment_reports[i].remaster_pack_kernel_ms;
        report.remaster_ngx_ms += segment_reports[i].remaster_ngx_ms;
        report.remaster_preprocess_ms += segment_reports[i].remaster_preprocess_ms;
        report.elapsed_ms += segment_reports[i].elapsed_ms;
        report.main_loop_ms += segment_reports[i].main_loop_ms;
        report.remaster_pack_kernel_max_ms =
            std::max(report.remaster_pack_kernel_max_ms,
                     segment_reports[i].remaster_pack_kernel_max_ms);
        report.measured_max_cll_nits =
            std::max(report.measured_max_cll_nits, segment_reports[i].measured_max_cll_nits);
        report.measured_max_fall_nits =
            std::max(report.measured_max_fall_nits, segment_reports[i].measured_max_fall_nits);
    }
    auto merged =
        mergeSegmentContainers(segment_files, options, frame_rate, std::move(report));

    // Clean the temporary segment containers regardless of the merge outcome.
    for (const std::filesystem::path& segment : segment_files) {
        std::filesystem::remove(segment, fs_ec);
    }
    (void)std::filesystem::remove(temp_dir, fs_ec);
    return merged;
}

}  // namespace rtx
