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

    auto a1_res = nleRegisterAsset(project, NleMediaAsset{
        .uuid = "uuid-a1", .name = "clip_a.mp4", .path = "media/clip_a.mp4",
        .duration_flicks = 20 * kSec, .width = 1920, .height = 1080, .offline = false});
    RTX_REQUIRE(a1_res.has_value());
    const uint64_t a1 = *a1_res;

    // 1. AddClip
    auto c1_res = nleAddClip(project, history, TimelineClip{
        .asset_id = a1, .name = "Intro", .lane = 1,
        .start_flicks = 0, .in_flicks = 0, .out_flicks = 10 * kSec, .gain = 1.0f});
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
    RTX_CHECK_EQ(project.clip(c1)->in_flicks, 1 * kSec);
    RTX_CHECK_EQ(project.clip(c1)->out_flicks, 9 * kSec);

    // 3. SplitClip at 4s (inside [0, 8s))
    auto c2_res = nleSplitClip(project, history, c1, 4 * kSec);
    RTX_REQUIRE(c2_res.has_value());
    const uint64_t c2 = *c2_res;
    RTX_REQUIRE_EQ(project.clips.size(), 2u);
    RTX_CHECK_EQ(nleClipDuration(*project.clip(c1)), 4 * kSec);
    RTX_CHECK_EQ(project.clip(c2)->start_flicks, 4 * kSec);
    RTX_CHECK_EQ(project.clip(c2)->in_flicks, 5 * kSec);
    RTX_CHECK_EQ(project.clip(c2)->out_flicks, 9 * kSec);

    // 4. MoveClip
    RTX_REQUIRE(nleMoveClip(project, history, c2, 0, 2 * kSec).has_value());
    RTX_CHECK_EQ(project.clip(c2)->lane, 0);
    RTX_CHECK_EQ(project.clip(c2)->start_flicks, 2 * kSec);

    // 5. ReorderTracks
    const std::array<int8_t, kNleTrackCount> swapped{1, 0, 2, 3};
    RTX_REQUIRE(nleReorderTracks(project, history, swapped).has_value());
    RTX_CHECK_EQ(static_cast<int>(project.lane_stack[0]), 1);
    RTX_CHECK_EQ(static_cast<int>(project.lane_stack[1]), 0);

    // 6. RelinkAsset
    project.asset(a1)->offline = true;
    RTX_REQUIRE(nleRelinkAsset(project, history, a1, "media/clip_a_relinked.mp4",
                               25 * kSec, 3840, 2160).has_value());
    RTX_CHECK_EQ(project.asset(a1)->path, std::string("media/clip_a_relinked.mp4"));
    RTX_CHECK_EQ(project.asset(a1)->offline, false);
    RTX_CHECK_EQ(project.asset(a1)->width, 3840);

    // 7. DeleteClip
    RTX_REQUIRE(nleDeleteClip(project, history, c2).has_value());
    RTX_CHECK(project.clip(c2) == nullptr);
    RTX_CHECK_EQ(project.clips.size(), 1u);

    // Undo all 7 operations back to empty timeline
    for (int i = 0; i < 7; ++i) {
        RTX_REQUIRE(nleUndo(project, history).has_value());
    }
    RTX_CHECK(project.clips.empty());
    RTX_CHECK_EQ(project.asset(a1)->path, std::string("media/clip_a.mp4"));
    RTX_CHECK_EQ(static_cast<int>(project.lane_stack[0]), 0);
    RTX_CHECK(!nleUndo(project, history).has_value());

    // Redo all 7 operations forward
    for (int i = 0; i < 7; ++i) {
        RTX_REQUIRE(nleRedo(project, history).has_value());
    }
    RTX_CHECK_EQ(project.clips.size(), 1u);
    RTX_CHECK_EQ(project.asset(a1)->path, std::string("media/clip_a_relinked.mp4"));
    RTX_CHECK(!nleRedo(project, history).has_value());
}

RTX_TEST(nle_json_rfc8259_strict_round_trip) {
    NleProject project{};
    EditStacks history{};

    auto a1 = nleRegisterAsset(project, NleMediaAsset{
        .uuid = "u-1", .name = "a\"quote.mp4", .path = "C:\\media\\a.mp4",
        .duration_flicks = 12 * kSec, .width = 3840, .height = 2160, .offline = false});
    auto a2 = nleRegisterAsset(project, NleMediaAsset{
        .uuid = "u-2", .name = "b.mp4", .path = "C:\\media\\b.mp4",
        .duration_flicks = 8 * kSec, .width = 1920, .height = 1080, .offline = false});
    RTX_REQUIRE(a1.has_value() && a2.has_value());

    RTX_REQUIRE(nleAddClip(project, history, TimelineClip{
        .asset_id = *a1, .name = "Clip 1", .lane = 1,
        .start_flicks = 0, .in_flicks = 0, .out_flicks = 6 * kSec, .gain = 1.0f}).has_value());
    RTX_REQUIRE(nleAddClip(project, history, TimelineClip{
        .asset_id = *a2, .name = "Clip 2", .lane = 0,
        .start_flicks = 3 * kSec, .in_flicks = 1 * kSec, .out_flicks = 5 * kSec, .gain = 0.5f}).has_value());
    project.playhead_flicks = 2 * kSec;

    const std::string json = project.toJson();
    RTX_CHECK(!hasTrailingComma(json));

    auto parsed = NleProject::fromJson(json);
    RTX_REQUIRE(parsed.has_value());
    RTX_CHECK_EQ(parsed->assets.size(), 2u);
    RTX_CHECK_EQ(parsed->clips.size(), 2u);
    RTX_CHECK_EQ(parsed->playhead_flicks, 2 * kSec);
    RTX_CHECK_EQ(parsed->assets[0].name, std::string("a\"quote.mp4"));
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
             "startFlicks": 0, "inFlicks": 0, "outFlicks": 705600000, "gain": 0.8}
        ],
        "assets": [
            {"id": 7, "uuid": "u-7", "name": "src.mp4", "path": "media/src.mp4",
             "durationFlicks": 1411200000, "width": 1920, "height": 1080, "offline": false}
        ]
    })";
    auto parsed = NleProject::fromJson(reordered);
    RTX_REQUIRE(parsed.has_value());
    RTX_REQUIRE_EQ(parsed->assets.size(), 1u);
    RTX_REQUIRE_EQ(parsed->clips.size(), 1u);
    RTX_CHECK_EQ(parsed->clips[0].asset_id, 7ULL);

    // S-1.5 / S-2.2: Duplicate explicit non-zero IDs must be rejected
    EditStacks history{};
    auto dup_asset = nleRegisterAsset(*parsed, NleMediaAsset{
        .id = 7, .uuid = "u-dup", .name = "dup.mp4", .path = "media/dup.mp4",
        .duration_flicks = kSec});
    RTX_CHECK(!dup_asset.has_value());

    auto dup_clip = nleAddClip(*parsed, history, TimelineClip{
        .id = 10, .asset_id = 7, .name = "DupClip", .lane = 1,
        .start_flicks = 0, .in_flicks = 0, .out_flicks = kSec});
    RTX_CHECK(!dup_clip.has_value());
    RTX_CHECK(history.first.empty());  // Failed add must not pollute undo stack

    // S-1.5: Relink with shorter duration must clamp referencing clip out_flicks
    RTX_REQUIRE(nleRelinkAsset(*parsed, history, 7, "media/shorter.mp4", kSec / 2, 1920, 1080).has_value());
    RTX_CHECK_EQ(parsed->clip(10)->out_flicks, kSec / 2);
}

RTX_TEST(nle_render_graph_sweep_line_hard_cut_idr_and_audio_slices) {
    NleProject project{};
    EditStacks history{};

    auto a1 = nleRegisterAsset(project, NleMediaAsset{
        .uuid = "u-base", .name = "base.mp4", .path = "media/base.mp4",
        .duration_flicks = 10 * kSec, .width = 1920, .height = 1080, .offline = false});
    auto a2 = nleRegisterAsset(project, NleMediaAsset{
        .uuid = "u-cut", .name = "cutaway.mp4", .path = "media/cutaway.mp4",
        .duration_flicks = 4 * kSec, .width = 1920, .height = 1080, .offline = false});
    RTX_REQUIRE(a1.has_value() && a2.has_value());

    // V1 base [0s, 10s), V2 cutaway [3s, 6s), A1 audio [0s, 10s)
    RTX_REQUIRE(nleAddClip(project, history, TimelineClip{
        .asset_id = *a1, .name = "Base", .lane = 1,
        .start_flicks = 0, .in_flicks = 0, .out_flicks = 10 * kSec}).has_value());
    RTX_REQUIRE(nleAddClip(project, history, TimelineClip{
        .asset_id = *a2, .name = "Cutaway", .lane = 0,
        .start_flicks = 3 * kSec, .in_flicks = 0, .out_flicks = 3 * kSec}).has_value());
    RTX_REQUIRE(nleAddClip(project, history, TimelineClip{
        .asset_id = *a1, .name = "Base A1", .lane = 2,
        .start_flicks = 0, .in_flicks = 0, .out_flicks = 10 * kSec, .gain = 0.75f}).has_value());

    auto compiled = compileRenderGraph(project);
    RTX_REQUIRE(compiled.has_value());
    const auto& segs = *compiled;
    RTX_REQUIRE_EQ(segs.size(), 3u);

    RTX_CHECK_EQ(segs[0].source_path, std::string("media/base.mp4"));
    RTX_CHECK_EQ(segs[0].timeline_in_flicks, 0LL);
    RTX_CHECK_EQ(segs[0].timeline_out_flicks, 3 * kSec);
    RTX_CHECK(!segs[0].hard_cut);
    RTX_CHECK(!segs[0].force_keyframe);

    RTX_CHECK_EQ(segs[1].source_path, std::string("media/cutaway.mp4"));
    RTX_CHECK_EQ(segs[1].timeline_in_flicks, 3 * kSec);
    RTX_CHECK_EQ(segs[1].timeline_out_flicks, 6 * kSec);
    RTX_CHECK_EQ(segs[1].source_in_flicks, 0LL);
    RTX_CHECK_EQ(segs[1].source_out_flicks, 3 * kSec);
    RTX_CHECK(segs[1].hard_cut);
    RTX_CHECK(segs[1].force_keyframe);

    RTX_CHECK_EQ(segs[2].source_path, std::string("media/base.mp4"));
    RTX_CHECK_EQ(segs[2].timeline_in_flicks, 6 * kSec);
    RTX_CHECK_EQ(segs[2].timeline_out_flicks, 10 * kSec);
    RTX_CHECK_EQ(segs[2].source_in_flicks, 6 * kSec);
    RTX_CHECK_EQ(segs[2].source_out_flicks, 10 * kSec);
    RTX_CHECK(segs[2].hard_cut);
    RTX_CHECK(segs[2].force_keyframe);

    // S-1.2: A1 audio render graph slice extraction
    auto audio_segs = compileAudioRenderGraph(project);
    RTX_REQUIRE(audio_segs.has_value());
    RTX_REQUIRE_EQ(audio_segs->size(), 1u);
    RTX_CHECK_EQ((*audio_segs)[0].source_path, std::string("media/base.mp4"));
    RTX_CHECK_EQ((*audio_segs)[0].timeline_out_flicks, 10 * kSec);
    RTX_CHECK_NEAR((*audio_segs)[0].gain, 0.75f, 1e-6f);

    // S-1.2: Offline asset must return ErrorCode::NotFound instead of silent black frames
    project.asset(*a2)->offline = true;
    auto offline_err = compileRenderGraph(project);
    RTX_REQUIRE(!offline_err.has_value());
    RTX_CHECK_EQ(offline_err.error().code, ErrorCode::NotFound);
}
