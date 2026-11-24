// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-S / Wave 12.2-C：单遍离散事件时间扫描线渲染图编译器（含离线资产硬拦截与 A1 音轨切片）。

#pragma once

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "rtx/nle_project.h"
#include "rtx/status.h"

namespace rtx {

// Phase 12 (Wave 12.2-C): tuple aliases replace isomorphic video/audio segment bags.
// RenderGraphSegment: (source_path, source_in_flicks, source_out_flicks,
//   timeline_in_flicks, timeline_out_flicks, hard_cut, force_keyframe)
using RenderGraphSegment = std::tuple<std::string, int64_t, int64_t, int64_t, int64_t, bool, bool>;

// AudioGraphSegment: (source_path, source_in_flicks, source_out_flicks,
//   timeline_in_flicks, timeline_out_flicks, gain)
using AudioGraphSegment = std::tuple<std::string, int64_t, int64_t, int64_t, int64_t, float>;

[[nodiscard]] Result<std::vector<RenderGraphSegment>> compileRenderGraph(const NleProject& project);
[[nodiscard]] Result<std::vector<AudioGraphSegment>> compileAudioRenderGraph(const NleProject& project);

}  // namespace rtx
