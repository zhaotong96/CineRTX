// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-S：NLE 垂直切片全覆盖单元测试。
// 覆盖全部 7 类编辑操作、Undo/Redo、严格 RFC 8259 JSON 往返（含键乱序防御）、
// 重复显式 ID 拦截、Relink 缩短截断、离线资产硬拦截与 A1 音轨切片编译。

#include "test_framework.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "rtx/flicks.h"
#include "rtx/nle_project.h"
#include "rtx/render_graph.h"

using namespace rtx;

namespace {

constexpr int64_t kSec = static_cast<int64_t>(kFlicksPerSecond);

bool hasTrailingComma(std::string_view json) noexcept {
    for (std::size_t i = 0; i < json.size(); ++i) {
        if (json[i] != ',') continue;
        std::size_t j = i + 1;
        while (j < json.size() &&
               (json[j] == ' ' || json[j] == '\n' || json[j] == '\r' || json[j] == '\t')) {
            ++j;
        }
        if (j < json.size() && (json[j] == ']' || json[j] == '}')) {
            return true;
        }
    }
    return false;
}

}  // namespace

RTX_TEST(nle_seven_edit_ops_and_undo_redo) {
    NleProject project{};
    EditStacks history{};

    auto a1_res = nleRegisterAsset(project, makeNleAsset(
        0, "uuid-a1", "clip_a.mp4", "media/clip_a.mp4", 20 * kSec, 1920, 1080, false));
    RTX_REQUIRE(a1_res.has_value());
    const uint64_t a1 = *a1_res;

    // 1. AddClip
    auto c1_res = nleAddClip(project, history, makeTimelineClip(
        0, a1, "Intro", 1, 0, 0, 10 * kSec, 1.0f));
    RTX_REQUIRE(c1_res.has_value());
    const uint64_t c1 = *c1_res;
    RTX_REQUIRE(project.clip(c1) != nullptr);
    RTX_CHECK_EQ(nleClipDuration(*project.clip(c1)), 10 * kSec);

    // Deducing this const/non-const lookup check
    const NleProject& const_ref = project;
    RTX_REQUIRE(const_ref.clip(c1) != nullptr);
    RTX_REQUIRE(const_ref.asset(a1) != nullptr);

    // 2. TrimClip
    RTX_REQUIRE(nleTrimClip(project, history, c1, 1 * kSec, 9 * kSec).has_value());
    { const auto& [id, a, n, l, s, inf, outf, g] = *project.clip(c1); (void)id;(void)a;(void)n;(void)l;(void)s;(void)g; RTX_CHECK_EQ(inf, 1 * kSec); RTX_CHECK_EQ(outf, 9 * kSec); }

    // 3. SplitClip at 4s (inside [0, 8s))
    auto c2_res = nleSplitClip(project, history, c1, 4 * kSec);
    RTX_REQUIRE(c2_res.has_value());
    const uint64_t c2 = *c2_res;
    RTX_REQUIRE_EQ(project.clips.size(), 2u);
    RTX_CHECK_EQ(nleClipDuration(*project.clip(c1)), 4 * kSec);
    { const auto& [id, a, n, l, st, inf, outf, g] = *project.clip(c2); (void)id;(void)a;(void)n;(void)l;(void)g; RTX_CHECK_EQ(st, 4 * kSec); RTX_CHECK_EQ(inf, 5 * kSec); RTX_CHECK_EQ(outf, 9 * kSec); }

    // 4. MoveClip
    RTX_REQUIRE(nleMoveClip(project, history, c2, 0, 2 * kSec).has_value());
    { const auto& [id, a, n, ln, st, inf, outf, g] = *project.clip(c2); (void)id;(void)a;(void)n;(void)inf;(void)outf;(void)g; RTX_CHECK_EQ(ln, 0); RTX_CHECK_EQ(st, 2 * kSec); }

    // 5. ReorderTracks
    const std::array<int8_t, kNleTrackCount> swapped{1, 0, 2, 3};
    RTX_REQUIRE(nleReorderTracks(project, history, swapped).has_value());
    RTX_CHECK_EQ(static_cast<int>(project.lane_stack[0]), 1);
    RTX_CHECK_EQ(static_cast<int>(project.lane_stack[1]), 0);

    // 6. RelinkAsset
    { auto& [id, u, n, p, d, w, h, off] = *project.asset(a1); (void)id;(void)u;(void)n;(void)p;(void)d;(void)w;(void)h; off = true; }
    RTX_REQUIRE(nleRelinkAsset(project, history, a1, "media/clip_a_relinked.mp4",
                               25 * kSec, 3840, 2160).has_value());
    { const auto& [id, u, n, p, d, w, h, off] = *project.asset(a1); (void)id;(void)u;(void)n;(void)d;(void)h; RTX_CHECK_EQ(p, std::string("media/clip_a_relinked.mp4")); RTX_CHECK_EQ(off, false); RTX_CHECK_EQ(w, 3840); }

    // 7. DeleteClip
    RTX_REQUIRE(nleDeleteClip(project, history, c2).has_value());
    RTX_CHECK(project.clip(c2) == nullptr);
    RTX_CHECK_EQ(project.clips.size(), 1u);

    // Undo all 7 operations back to empty timeline
    for (int i = 0; i < 7; ++i) {
        RTX_REQUIRE(nleUndo(project, history).has_value());
    }
    RTX_CHECK(project.clips.empty());
    { const auto& [id, u, n, p, d, w, h, off] = *project.asset(a1); (void)id;(void)u;(void)n;(void)d;(void)w;(void)h;(void)off; RTX_CHECK_EQ(p, std::string("media/clip_a.mp4")); }
    RTX_CHECK_EQ(static_cast<int>(project.lane_stack[0]), 0);
    RTX_CHECK(!nleUndo(project, history).has_value());

    // Redo all 7 operations forward
    for (int i = 0; i < 7; ++i) {
        RTX_REQUIRE(nleRedo(project, history).has_value());
    }
    RTX_CHECK_EQ(project.clips.size(), 1u);
    { const auto& [id, u, n, p, d, w, h, off] = *project.asset(a1); (void)id;(void)u;(void)n;(void)d;(void)w;(void)h;(void)off; RTX_CHECK_EQ(p, std::string("media/clip_a_relinked.mp4")); }
    RTX_CHECK(!nleRedo(project, history).has_value());
}

RTX_TEST(nle_json_rfc8259_strict_round_trip) {
    NleProject project{};
    EditStacks history{};

    auto a1 = nleRegisterAsset(project, makeNleAsset(0, "u-1", "a\"quote.mp4", "C:\\media\\a.mp4", 12 * kSec, 3840, 2160, false));
    auto a2 = nleRegisterAsset(project, makeNleAsset(0, "u-2", "b.mp4", "C:\\media\\b.mp4", 8 * kSec, 1920, 1080, false));
    RTX_REQUIRE(a1.has_value() && a2.has_value());

    RTX_REQUIRE(nleAddClip(project, history, makeTimelineClip(0, *a1, "Clip 1", 1, 0, 0, 6 * kSec, 1.0f)).has_value());
    RTX_REQUIRE(nleAddClip(project, history, makeTimelineClip(0, *a2, "Clip 2", 0, 3 * kSec, 1 * kSec, 5 * kSec, 0.5f)).has_value());
    project.playhead_flicks = 2 * kSec;

    const std::string json = project.toJson();
    RTX_CHECK(!hasTrailingComma(json));

    auto parsed = NleProject::fromJson(json);
    RTX_REQUIRE(parsed.has_value());
    RTX_CHECK_EQ(parsed->assets.size(), 2u);
    RTX_CHECK_EQ(parsed->clips.size(), 2u);
    RTX_CHECK_EQ(parsed->playhead_flicks, 2 * kSec);
    { const auto& [id, u, nm, p, d, w, h, off] = parsed->assets[0]; (void)id;(void)u;(void)p;(void)d;(void)w;(void)h;(void)off; RTX_CHECK_EQ(nm, std::string("a\"quote.mp4")); }
    RTX_CHECK_EQ(parsed->toJson(), json);

    // Malformed payloads must return Result error without throwing exceptions
    RTX_CHECK(!NleProject::fromJson("").has_value());
    RTX_CHECK(!NleProject::fromJson("[]").has_value());
    RTX_CHECK(!NleProject::fromJson(R"({"clips":[{"id":1,"assetId":999}]})").has_value());
}

RTX_TEST(nle_json_out_of_order_keys_and_duplicate_id_guard) {
    // S-2.2: "clips" appears strictly before "assets" in valid JSON
    const std::string reordered = R"({
        "version": 1,
        "clips": [
            {"id": 10, "assetId": 7, "name": "Reordered", "lane": 1,
             "startFlicks": 0, "inFlicks": 0, "outFlicks": 706305600000, "gain": 0.8}
        ],
        "assets": [
            {"id": 7, "uuid": "u-7", "name": "src.mp4", "path": "media/src.mp4",
             "durationFlicks": 1412611200000, "width": 1920, "height": 1080, "offline": false}
        ]
    })";
    auto parsed = NleProject::fromJson(reordered);
    RTX_REQUIRE(parsed.has_value());
    RTX_REQUIRE_EQ(parsed->assets.size(), 1u);
    RTX_REQUIRE_EQ(parsed->clips.size(), 1u);
    { const auto& [id, aid, nm, l, s, inf, outf, g] = parsed->clips[0]; (void)id;(void)nm;(void)l;(void)s;(void)inf;(void)outf;(void)g; RTX_CHECK_EQ(aid, 7ULL); }

    // S-1.5 / S-2.2: Duplicate explicit non-zero IDs must be rejected
    EditStacks history{};
    auto dup_asset = nleRegisterAsset(*parsed, makeNleAsset(7, "u-dup", "dup.mp4", "media/dup.mp4", kSec));
    RTX_CHECK(!dup_asset.has_value());

    auto dup_clip = nleAddClip(*parsed, history, makeTimelineClip(10, 7, "DupClip", 1, 0, 0, kSec));
    RTX_CHECK(!dup_clip.has_value());
    RTX_CHECK(history.first.empty());  // Failed add must not pollute undo stack

    // S-1.5: Relink with shorter duration must clamp referencing clip out_flicks
    RTX_REQUIRE(nleRelinkAsset(*parsed, history, 7, "media/shorter.mp4", kSec / 2, 1920, 1080).has_value());
    { const auto& [id, aid, nm, l, s, inf, outf, g] = *parsed->clip(10); (void)id;(void)aid;(void)nm;(void)l;(void)s;(void)inf;(void)g; RTX_CHECK_EQ(outf, kSec / 2); }
}

RTX_TEST(nle_render_graph_sweep_line_hard_cut_idr_and_audio_slices) {
    NleProject project{};
    EditStacks history{};

    auto a1 = nleRegisterAsset(project, makeNleAsset(0, "u-base", "base.mp4", "media/base.mp4", 10 * kSec, 1920, 1080, false));
    auto a2 = nleRegisterAsset(project, makeNleAsset(0, "u-cut", "cutaway.mp4", "media/cutaway.mp4", 4 * kSec, 1920, 1080, false));
    RTX_REQUIRE(a1.has_value() && a2.has_value());

    // V1 base [0s, 10s), V2 cutaway [3s, 6s), A1 audio [0s, 10s)
    RTX_REQUIRE(nleAddClip(project, history, makeTimelineClip(0, *a1, "Base", 1, 0, 0, 10 * kSec)).has_value());
    RTX_REQUIRE(nleAddClip(project, history, makeTimelineClip(0, *a2, "Cutaway", 0, 3 * kSec, 0, 3 * kSec)).has_value());
    RTX_REQUIRE(nleAddClip(project, history, makeTimelineClip(0, *a1, "Base A1", 2, 0, 0, 10 * kSec, 0.75f)).has_value());

    auto compiled = compileRenderGraph(project);
    RTX_REQUIRE(compiled.has_value());
    const auto& segs = *compiled;
    RTX_REQUIRE_EQ(segs.size(), 3u);

    {
        const auto& [sp, si, so, ti, to, hc, fk] = segs[0];
        (void)si; (void)so;
        RTX_CHECK_EQ(sp, std::string("media/base.mp4"));
        RTX_CHECK_EQ(ti, 0LL);
        RTX_CHECK_EQ(to, 3 * kSec);
        RTX_CHECK(!hc);
        RTX_CHECK(!fk);
    }
    {
        const auto& [sp, si, so, ti, to, hc, fk] = segs[1];
        RTX_CHECK_EQ(sp, std::string("media/cutaway.mp4"));
        RTX_CHECK_EQ(ti, 3 * kSec);
        RTX_CHECK_EQ(to, 6 * kSec);
        RTX_CHECK_EQ(si, 0LL);
        RTX_CHECK_EQ(so, 3 * kSec);
        RTX_CHECK(hc);
        RTX_CHECK(fk);
    }
    {
        const auto& [sp, si, so, ti, to, hc, fk] = segs[2];
        RTX_CHECK_EQ(sp, std::string("media/base.mp4"));
        RTX_CHECK_EQ(ti, 6 * kSec);
        RTX_CHECK_EQ(to, 10 * kSec);
        RTX_CHECK_EQ(si, 6 * kSec);
        RTX_CHECK_EQ(so, 10 * kSec);
        RTX_CHECK(hc);
        RTX_CHECK(fk);
    }

    // S-1.2: A1 audio render graph slice extraction
    auto audio_segs = compileAudioRenderGraph(project);
    RTX_REQUIRE(audio_segs.has_value());
    RTX_REQUIRE_EQ(audio_segs->size(), 1u);
    {
        const auto& [ap, asi, aso, ati, ato, again] = (*audio_segs)[0];
        (void)asi; (void)aso; (void)ati;
        RTX_CHECK_EQ(ap, std::string("media/base.mp4"));
        RTX_CHECK_EQ(ato, 10 * kSec);
        RTX_CHECK_NEAR(again, 0.75f, 1e-6f);
    }

    // S-1.2: Offline asset must return ErrorCode::NotFound instead of silent black frames
    { auto& [id, u, n, p, d, w, h, off] = *project.asset(*a2); (void)id;(void)u;(void)n;(void)p;(void)d;(void)w;(void)h; off = true; }
    auto offline_err = compileRenderGraph(project);
    RTX_REQUIRE(!offline_err.has_value());
    RTX_CHECK_EQ(offline_err.error().code, ErrorCode::NotFound);
}
