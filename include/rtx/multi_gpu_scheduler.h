// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 8 (Task 8-A2): multi-GPU dispatch, file-level and GOP-level.
//
// Zero cross-card uncompressed pixel traffic (verdict §4.2): the two sharding
// modes exchange only encoded bitstreams (a few MB) or nothing at all.
//   Mode A  runBatchJobs         - M jobs distributed over N GPUs with
//                                  work-stealing (one NvidiaTranscodeSession
//                                  per GPU, each on its own thread).
//   Mode B  runSegmentedSingleJob- one large file split at IDR key-frame
//                                  boundaries into K closed GOP segments,
//                                  each transcoded independently and merged
//                                  in segment order without re-encoding.
// On a single-GPU host the scheduler degrades deterministically: mode A runs
// the queue sequentially on GPU 0 and mode B still exercises the full
// split/merge machinery (optionally with the injected device-lost fault hook
// for the anti-TDR gate).

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "rtx/hw_transcode.h"
#include "rtx/status.h"

namespace rtx {

struct GopSegmentRange {
    int64_t start_pts = 0;  // video stream time base
    int64_t end_pts = 0;    // std::numeric_limits<int64_t>::max() for the tail
    int assigned_gpu = 0;
};

class MultiGpuScheduler {
public:
    // `gpu_indices` clamps the dispatch to the intersection with the devices
    // rtx_shim actually enumerated; empty = every available device.
    [[nodiscard]] static Result<MultiGpuScheduler> create(std::span<const int> gpu_indices = {});

    [[nodiscard]] int deviceCount() const noexcept { return static_cast<int>(active_gpus_.size()); }

    // Mode A: multi-file batch with work stealing.
    [[nodiscard]] Result<std::vector<HwTranscodeReport>> runBatchJobs(
        std::span<const HwTranscodeOptions> jobs);

    // Mode B: single large file split into `segments` GOP windows (>= 1),
    // transcoded and merged losslessly.  `segments == 1` is the passthrough
    // single-session path.
    [[nodiscard]] Result<HwTranscodeReport> runSegmentedSingleJob(
        const HwTranscodeOptions& options, int segments);

    // Number of key frames the source video stream carries (probe helper used
    // to pick the segment boundaries).
    [[nodiscard]] static Result<int> countKeyFrames(const std::filesystem::path& input);

private:
    MultiGpuScheduler() = default;
    std::vector<int> active_gpus_;
};

}  // namespace rtx
