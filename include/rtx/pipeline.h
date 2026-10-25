// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// End-to-end reference pipeline used by the CLI and by the integration tests.
// It deliberately exercises the same stage boundaries as the NVIDIA path:
// decode/source -> FRUC -> VSR/TrueHDR -> P010 packing -> encode -> mux/sidecar.

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "rtx/backend.h"
#include "rtx/hdr.h"
#include "rtx/rational.h"
#include "rtx/scheduler.h"

namespace rtx {

struct PipelineRunConfig {
    int width = 1920;
    int height = 1080;
    int target_width = 3840;
    int target_height = 2160;
    int input_frames = 12;
    int scene_cut_frame = -1;
    int bar_speed = 7;
    Rational input_fps{24, 1};
    Rational output_fps{60, 1};
    bool interpolate = true;
    bool enable_hdr = true;
    bool scene_detection = true;
    int vsr_quality = 2;

    std::string output_p010;
    std::string hdr_sidecar;
    std::string trace_path;
};

struct PipelineReport {
    std::string backend;
    std::string device;
    int input_width = 0;
    int input_height = 0;
    int output_width = 0;
    int output_height = 0;
    int64_t input_frames = 0;
    int64_t output_frames = 0;
    int64_t interpolated_frames = 0;
    int64_t duplicated_frames = 0;
    int64_t scene_cuts = 0;
    uint64_t packet_bytes = 0;
    uint64_t p010_bytes = 0;
    Hdr10Metadata hdr{};
    PipelineMetrics metrics{};
    std::string zero_copy = "not-applicable (CPU reference path)";

    std::string toJson() const;
};

using PipelineLog = std::function<void(const std::string&)>;

// Deterministic synthetic SDR source with a moving edge and an optional cut.
Result<PipelineReport> runSyntheticPipeline(const PipelineRunConfig& config,
                                            const PipelineLog& log = {});

// Headerless NV12 input.  When config.input_frames <= 0 the reader consumes the
// whole file; otherwise it stops after that many frames.
Result<PipelineReport> runRawNv12Pipeline(const PipelineRunConfig& config,
                                          const std::string& input_path,
                                          const PipelineLog& log = {});

}  // namespace rtx
