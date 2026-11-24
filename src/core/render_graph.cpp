// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-S / Wave 12.2-D：单遍离散事件时间扫描线渲染图编译器。
// 扫描线遇到离线资产（offline == true）立即返回 ErrorCode::NotFound，严禁静默黑帧；
// 同步支持 A1 音轨（lane == 2）切片编译，供多段导出与 AVX2 音频总线消费。

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
        const auto& [cid, aid, nm, lane, st, inf, outf, gain] = c;
        (void)cid;(void)aid;(void)nm;(void)st;(void)inf;(void)outf;(void)gain;
        if (nleTrackKind(lane) == NleTrackKind::Video && nleClipDuration(c) > 0) {
            video_clips.push_back(&c);
        }
    }
    if (video_clips.empty()) {
        return err(ErrorCode::InvalidArgument, "render graph: the project has no video clips");
    }

    std::vector<int64_t> cuts;
    cuts.reserve(video_clips.size() * 2);
    for (const TimelineClip* c : video_clips) {
        const auto& [cid, aid, nm, lane, st, inf, outf, gain] = *c;
        (void)cid;(void)aid;(void)nm;(void)lane;(void)inf;(void)outf;(void)gain;
        cuts.push_back(st);
        cuts.push_back(st + nleClipDuration(*c));
    }
    std::ranges::sort(cuts);
    const auto [dup_begin, dup_end] = std::ranges::unique(cuts);
    cuts.erase(dup_begin, dup_end);

    std::vector<RenderGraphSegment> segments;
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
        const int64_t t0 = cuts[i];
        const int64_t t1 = cuts[i + 1];
        auto covering = video_clips | std::views::filter([t0, t1](const TimelineClip* c) noexcept {
            const auto& [cid, aid, nm, lane, st, inf, outf, gain] = *c;
            (void)cid;(void)aid;(void)nm;(void)lane;(void)inf;(void)outf;(void)gain;
            return st <= t0 && (st + nleClipDuration(*c)) >= t1;
        });
        auto winner_it = std::ranges::min_element(
            covering, std::ranges::less{},
            [&](const TimelineClip* c) noexcept {
                const auto& [cid, aid, nm, lane, st, inf, outf, gain] = *c;
                (void)aid;(void)nm;(void)st;(void)inf;(void)outf;(void)gain;
                return std::pair{project.lane_stack[static_cast<std::size_t>(lane)], cid};
            });
        if (winner_it == covering.end()) continue;

        const auto& [w_id, w_aid, w_nm, w_lane, w_st, w_in, w_out, w_gain] = **winner_it;
        (void)w_id;(void)w_nm;(void)w_lane;(void)w_out;(void)w_gain;
        const auto* asset = project.asset(w_aid);
        if (!asset) {
            return err(ErrorCode::NotFound,
                       "timeline references missing asset id: " + std::to_string(w_aid));
        }
        const auto& [a_id, a_uuid, a_name, a_path, a_dur, a_w, a_h, a_off] = *asset;
        (void)a_id;(void)a_uuid;(void)a_dur;(void)a_w;(void)a_h;
        if (a_off) {
            return err(ErrorCode::NotFound,
                       "timeline references offline asset: " + (a_name.empty() ? a_path : a_name));
        }

        const int64_t src_in = w_in + (t0 - w_st);
        const int64_t src_out = src_in + (t1 - t0);
        if (!segments.empty()) {
            auto& [p_path, p_sin, p_sout, p_tin, p_tout, p_hc, p_fk] = segments.back();
            (void)p_sin;(void)p_tin;(void)p_hc;(void)p_fk;
            if (p_tout == t0 && p_path == a_path && p_sout == src_in) {
                p_tout = t1;
                p_sout = src_out;
                continue;
            }
        }
        const bool hard_cut = !segments.empty();
        segments.emplace_back(a_path, src_in, src_out, t0, t1, hard_cut, hard_cut);
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
        const auto& [cid, aid, nm, lane, st, inf, outf, gain] = c;
        (void)cid;(void)aid;(void)nm;(void)st;(void)inf;(void)outf;(void)gain;
        if (lane == 2 && nleTrackKind(lane) == NleTrackKind::Audio && nleClipDuration(c) > 0) {
            audio_clips.push_back(&c);
        }
    }
    std::ranges::sort(audio_clips, [](const TimelineClip* a, const TimelineClip* b) noexcept {
        const auto& [aid, aa, an, al, ast, ai, ao, ag] = *a;
        const auto& [bid, ba, bn, bl, bst, bi, bo, bg] = *b;
        (void)aa;(void)an;(void)al;(void)ai;(void)ao;(void)ag;
        (void)ba;(void)bn;(void)bl;(void)bi;(void)bo;(void)bg;
        return std::pair{ast, aid} < std::pair{bst, bid};
    });

    std::vector<AudioGraphSegment> segments;
    segments.reserve(audio_clips.size());
    for (const TimelineClip* c : audio_clips) {
        const auto& [cid, aid, nm, lane, st, inf, outf, gain] = *c;
        (void)cid;(void)nm;(void)lane;
        const auto* asset = project.asset(aid);
        if (!asset) {
            return err(ErrorCode::NotFound,
                       "audio timeline references missing asset id: " + std::to_string(aid));
        }
        const auto& [a_id, a_uuid, a_name, a_path, a_dur, a_w, a_h, a_off] = *asset;
        (void)a_id;(void)a_uuid;(void)a_dur;(void)a_w;(void)a_h;
        if (a_off) {
            return err(ErrorCode::NotFound,
                       "timeline references offline asset: " + (a_name.empty() ? a_path : a_name));
        }
        const int64_t dur = nleClipDuration(*c);
        segments.emplace_back(a_path, inf, outf, st, st + dur, gain);
    }
    return segments;
}

}  // namespace rtx
