// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-S：单遍离散事件时间扫描线渲染图编译器（含离线资产硬拦截与 A1 音轨切片）。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rtx/nle_project.h"
#include "rtx/status.h"

namespace rtx {

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 离散扫描线输出的线性视频渲染段契约，具名携带源路径、源入出点、时间轴入出点与硬切/IDR 关键帧标记。
// Invariant: timeline_out_flicks > timeline_in_flicks; source_out_flicks - source_in_flicks == timeline_out_flicks - timeline_in_flicks.
struct RenderGraphSegment {
    std::string source_path;
    int64_t source_in_flicks{0};
    int64_t source_out_flicks{0};
    int64_t timeline_in_flicks{0};
    int64_t timeline_out_flicks{0};
    bool hard_cut{false};
    bool force_keyframe{false};
};

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: A1 音轨渲染切片领域模型，具名承载音频源路径、源入出点、时间轴区间与线性增益，std::tuple 丢失混音管线字段语义。
// Invariant: timeline_out_flicks > timeline_in_flicks; source_out_flicks - source_in_flicks == timeline_out_flicks - timeline_in_flicks; gain >= 0.0f.
struct AudioGraphSegment {
    std::string source_path;
    int64_t source_in_flicks{0};
    int64_t source_out_flicks{0};
    int64_t timeline_in_flicks{0};
    int64_t timeline_out_flicks{0};
    float gain{1.0f};
};

[[nodiscard]] Result<std::vector<RenderGraphSegment>> compileRenderGraph(const NleProject& project);

[[nodiscard]] Result<std::vector<AudioGraphSegment>> compileAudioRenderGraph(const NleProject& project);

}  // namespace rtx
