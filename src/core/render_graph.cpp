// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-S：单遍离散事件时间扫描线渲染图编译器。
// 严格遵循 38 号专家终审方案与 43 号施工单 S-1.2：
//   - 无 VideoClaim、无 CompiledRenderGraph 包装类；
//   - 扫描线遇到离线资产（asset->offline == true）立即返回 ErrorCode::NotFound，严禁静默黑帧；
//   - 同步支持 A1 音轨（lane == 2）切片编译，供多段导出与 AVX2 音频总线消费。

#include "rtx/render_graph.h"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <utility>
#include <vector>

namespace rtx {

Result<std::vector<RenderGraphSegment>> compileRenderGraph(const NleProject& project) {
    std::vector<const TimelineClip*> video_clips;
    video_clips.reserve(project.clips.size());
    for (const auto& c : project.clips) {
        if (nleTrackKind(c.lane) == NleTrackKind::Video && nleClipDuration(c) > 0) {
            video_clips.push_back(&c);
        }
    }
    if (video_clips.empty()) {
        return err(ErrorCode::InvalidArgument, "render graph: the project has no video clips");
    }

    std::vector<int64_t> cuts;
    cuts.reserve(video_clips.size() * 2);
    for (const TimelineClip* c : video_clips) {
        cuts.push_back(c->start_flicks);
        cuts.push_back(c->start_flicks + nleClipDuration(*c));
    }
    std::ranges::sort(cuts);
    const auto [dup_begin, dup_end] = std::ranges::unique(cuts);
    cuts.erase(dup_begin, dup_end);

    std::vector<RenderGraphSegment> segments;
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
        const int64_t t0 = cuts[i];
        const int64_t t1 = cuts[i + 1];
        auto covering = video_clips | std::views::filter([t0, t1](const TimelineClip* c) noexcept {
            return c->start_flicks <= t0 && (c->start_flicks + nleClipDuration(*c)) >= t1;
        });
        auto winner_it = std::ranges::min_element(
            covering, std::ranges::less{},
            [&](const TimelineClip* c) noexcept {
                return std::pair{project.lane_stack[static_cast<std::size_t>(c->lane)], c->id};
            });
        if (winner_it == covering.end()) continue;

        const TimelineClip* w = *winner_it;
        const auto* asset = project.asset(w->asset_id);
        if (!asset) {
            return err(ErrorCode::NotFound,
                       "timeline references missing asset id: " + std::to_string(w->asset_id));
        }
        if (asset->offline) {
            return err(ErrorCode::NotFound,
                       "timeline references offline asset: " +
                           (asset->name.empty() ? asset->path : asset->name));
        }

        const int64_t src_in = w->in_flicks + (t0 - w->start_flicks);
        const int64_t src_out = src_in + (t1 - t0);
        if (!segments.empty()) {
            auto& prev = segments.back();
            if (prev.timeline_out_flicks == t0 &&
                prev.source_path == asset->path &&
                prev.source_out_flicks == src_in) {
                prev.timeline_out_flicks = t1;
                prev.source_out_flicks = src_out;
                continue;
            }
        }
        const bool hard_cut = !segments.empty();
        segments.push_back(RenderGraphSegment{
            .source_path = asset->path,
            .source_in_flicks = src_in,
            .source_out_flicks = src_out,
            .timeline_in_flicks = t0,
            .timeline_out_flicks = t1,
            .hard_cut = hard_cut,
            .force_keyframe = hard_cut,
        });
    }
    if (segments.empty()) {
        return err(ErrorCode::InvalidArgument, "render graph: no visible video spans");
    }
    return segments;
}

Result<std::vector<AudioGraphSegment>> compileAudioRenderGraph(const NleProject& project) {
    std::vector<const TimelineClip*> audio_clips;
    audio_clips.reserve(project.clips.size());
    for (const auto& c : project.clips) {
        if (c.lane == 2 && nleTrackKind(c.lane) == NleTrackKind::Audio && nleClipDuration(c) > 0) {
            audio_clips.push_back(&c);
        }
    }
    std::ranges::sort(audio_clips, [](const TimelineClip* a, const TimelineClip* b) noexcept {
        return std::pair{a->start_flicks, a->id} < std::pair{b->start_flicks, b->id};
    });

    std::vector<AudioGraphSegment> segments;
    segments.reserve(audio_clips.size());
    for (const TimelineClip* c : audio_clips) {
        const auto* asset = project.asset(c->asset_id);
        if (!asset) {
            return err(ErrorCode::NotFound,
                       "audio timeline references missing asset id: " + std::to_string(c->asset_id));
        }
        if (asset->offline) {
            return err(ErrorCode::NotFound,
                       "timeline references offline asset: " +
                           (asset->name.empty() ? asset->path : asset->name));
        }
        const int64_t dur = nleClipDuration(*c);
        segments.push_back(AudioGraphSegment{
            .source_path = asset->path,
            .source_in_flicks = c->in_flicks,
            .source_out_flicks = c->out_flicks,
            .timeline_in_flicks = c->start_flicks,
            .timeline_out_flicks = c->start_flicks + dur,
            .gain = c->gain,
        });
    }
    return segments;
}

}  // namespace rtx
