// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-R：统一 NLE 核心工程模型（代数和 NleEditDelta + C++23 Deducing this）。
// 彻底废除 7 个 OOP 命令类与 EditHistory 包装类，以纯标准库 std::variant 与
// std::pair<std::deque<NleEditDelta>, std::deque<NleEditDelta>> 表达全部可逆编辑操作。

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "rtx/status.h"

namespace rtx {

enum class NleTrackKind : uint8_t {
    Video = 0,
    Audio = 1,
    Subtitle = 2,
};

// 四轨布局：0 = V2（覆盖画中画/叠层），1 = V1（主视频轨），2 = A1（主音轨），3 = S1（字幕轨）。
inline constexpr std::size_t kNleTrackCount = 4;
inline constexpr std::size_t kNleUndoCapacity = 256;

[[nodiscard]] constexpr NleTrackKind nleTrackKind(int lane) noexcept {
    if (lane == 0 || lane == 1) return NleTrackKind::Video;
    if (lane == 2) return NleTrackKind::Audio;
    return NleTrackKind::Subtitle;
}

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: NLE 媒体素材库领域模型，封装 UUID、相对/绝对路径、Flicks 整数时长、分辨率与离线状态，std::tuple 丢失全部具名语义与序列化契约。
// Invariant: id > 0；duration_flicks >= 0；offline 为 true 时表示源媒体待 Relink。
struct NleMediaAsset {
    uint64_t id{0};
    std::string uuid;
    std::string name;
    std::string path;
    int64_t duration_flicks{0};
    int width{0};
    int height{0};
    bool offline{false};
};

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 时间轴非破坏性片段领域模型（命名为 TimelineClip 以彻底隔离旧版 NleClip），具名承载源入出点、轨道位置与音频增益。
// Invariant: id > 0; 0 <= lane < kNleTrackCount; start_flicks >= 0; out_flicks >= in_flicks >= 0; gain >= 0.0f.
struct TimelineClip {
    uint64_t id{0};
    uint64_t asset_id{0};
    std::string name;
    int lane{1};
    int64_t start_flicks{0};
    int64_t in_flicks{0};
    int64_t out_flicks{0};
    float gain{1.0f};
};

[[nodiscard]] constexpr int64_t nleClipDuration(const TimelineClip& clip) noexcept {
    return clip.out_flicks > clip.in_flicks ? (clip.out_flicks - clip.in_flicks) : 0;
}

// 6 支纯代数和类型：覆盖 Add/Delete、Trim、Split、Move、ReorderTracks、RelinkAsset 全部 7 类可逆编辑操作。
using NleEditDelta = std::variant<
    std::pair<bool /*is_delete*/, TimelineClip>,                                                   // Add (false) / Delete (true)
    std::tuple<uint64_t /*clip_id*/, std::pair<int64_t, int64_t>, std::pair<int64_t, int64_t>>,   // Trim (old_in_out, new_in_out)
    std::tuple<uint64_t /*kept_id*/, uint64_t /*created_id*/, int64_t /*at*/, int64_t /*old_out*/>, // Split
    std::tuple<uint64_t /*clip_id*/, std::pair<int, int64_t>, std::pair<int, int64_t>>,           // Move (old_lane_start, new_lane_start)
    std::pair<std::array<int8_t, kNleTrackCount>, std::array<int8_t, kNleTrackCount>>,            // ReorderTracks (old, new)
    std::tuple<uint64_t /*asset_id*/, NleMediaAsset /*prev*/, NleMediaAsset /*verified*/>         // RelinkAsset
>;

using EditStacks = std::pair<std::deque<NleEditDelta>, std::deque<NleEditDelta>>;  // {undo_stack, redo_stack}

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: NLE 工程根聚合：统一持有素材表、时间轴片段表、轨道优先级栈与播放头状态，并通过 C++23 Deducing this 提供零 const_cast 双向查找。
// Invariant: clips 中所有片段的 asset_id 均在 assets 中存在有效注册项; lane_stack 为 0 到 kNleTrackCount-1 的排列; toJson() 严格输出 RFC 8259 合规 JSON（零尾逗号）。
struct NleProject {
    std::vector<NleMediaAsset> assets;
    std::vector<TimelineClip> clips;
    std::array<int8_t, kNleTrackCount> lane_stack{0, 1, 2, 3};
    int64_t playhead_flicks{0};
    int64_t mark_in_flicks{-1};
    int64_t mark_out_flicks{-1};
    uint64_t selected_clip_id{0};
    uint64_t next_id{1};

    template <typename Self>
    [[nodiscard]] auto* clip(this Self& self, uint64_t id) noexcept {
        auto it = std::ranges::find(self.clips, id, &TimelineClip::id);
        return it == self.clips.end() ? nullptr : &*it;
    }

    template <typename Self>
    [[nodiscard]] auto* asset(this Self& self, uint64_t id) noexcept {
        auto it = std::ranges::find(self.assets, id, &NleMediaAsset::id);
        return it == self.assets.end() ? nullptr : &*it;
    }

    [[nodiscard]] int64_t timelineDurationFlicks() const noexcept;
    [[nodiscard]] std::string toJson() const;
    [[nodiscard]] static Result<NleProject> fromJson(std::string_view json_text);
};

[[nodiscard]] Result<uint64_t> nleRegisterAsset(NleProject& project, NleMediaAsset asset);

[[nodiscard]] Result<uint64_t> nleAddClip(NleProject& project, EditStacks& history,
                                          TimelineClip clip);

[[nodiscard]] Result<void> nleTrimClip(NleProject& project, EditStacks& history,
                                       uint64_t clip_id, int64_t new_in_flicks,
                                       int64_t new_out_flicks);

[[nodiscard]] Result<uint64_t> nleSplitClip(NleProject& project, EditStacks& history,
                                            uint64_t clip_id, int64_t split_timeline_flicks);

[[nodiscard]] Result<void> nleMoveClip(NleProject& project, EditStacks& history,
                                       uint64_t clip_id, int new_lane, int64_t new_start_flicks);

[[nodiscard]] Result<void> nleDeleteClip(NleProject& project, EditStacks& history,
                                         uint64_t clip_id);

[[nodiscard]] Result<void> nleReorderTracks(NleProject& project, EditStacks& history,
                                            std::array<int8_t, kNleTrackCount> new_stack);

[[nodiscard]] Result<void> nleRelinkAsset(NleProject& project, EditStacks& history,
                                          uint64_t asset_id, std::string new_path,
                                          int64_t verified_duration_flicks = -1,
                                          int verified_width = 0, int verified_height = 0);

[[nodiscard]] Result<void> nleUndo(NleProject& project, EditStacks& history);
[[nodiscard]] Result<void> nleRedo(NleProject& project, EditStacks& history);

}  // namespace rtx
