// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.3-R：统一 NLE 核心工程实现。
// 以纯函数 + 6 支 std::variant 代数和实现全部 7 类可逆编辑操作与 Undo/Redo，
// 以零尾逗号（严格 RFC 8259）输出 JSON，并以无异常 Result<T> 链路解析。

#include "rtx/nle_project.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <ranges>
#include <utility>

#include "rtx/json_scanner.h"

namespace rtx {
namespace {

void pushUndo(EditStacks& history, NleEditDelta delta) {
    auto& [undo_stack, redo_stack] = history;
    redo_stack.clear();
    undo_stack.push_back(std::move(delta));
    while (undo_stack.size() > kNleUndoCapacity) {
        undo_stack.pop_front();
    }
}

[[nodiscard]] bool validLane(int lane) noexcept {
    return lane >= 0 && lane < static_cast<int>(kNleTrackCount);
}

[[nodiscard]] bool validLaneStack(const std::array<int8_t, kNleTrackCount>& stack) noexcept {
    std::array<bool, kNleTrackCount> seen{};
    for (int8_t v : stack) {
        if (v < 0 || v >= static_cast<int8_t>(kNleTrackCount)) return false;
        if (seen[static_cast<std::size_t>(v)]) return false;
        seen[static_cast<std::size_t>(v)] = true;
    }
    return true;
}

Result<void> applyDelta(NleProject& project, const NleEditDelta& delta, bool invert) {
    return std::visit(
        [&](const auto& op) -> Result<void> {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, std::pair<bool, TimelineClip>>) {
                const auto& [is_delete, clip_copy] = op;
                const bool do_delete = invert ? !is_delete : is_delete;
                if (do_delete) {
                    const auto erased = std::erase_if(project.clips, [&](const TimelineClip& c) {
                        return c.id == clip_copy.id;
                    });
                    if (erased == 0) {
                        return err(ErrorCode::NotFound, "nle: clip not found for delete");
                    }
                    if (project.selected_clip_id == clip_copy.id) {
                        project.selected_clip_id = 0;
                    }
                } else {
                    if (project.asset(clip_copy.asset_id) == nullptr) {
                        return err(ErrorCode::InvalidArgument, "nle: asset not registered for clip");
                    }
                    if (project.clip(clip_copy.id) == nullptr) {
                        project.clips.push_back(clip_copy);
                    }
                }
                return {};
            } else if constexpr (std::is_same_v<T, std::tuple<uint64_t, std::pair<int64_t, int64_t>,
                                                              std::pair<int64_t, int64_t>>>) {
                const auto& [clip_id, old_io, new_io] = op;
                auto* target = project.clip(clip_id);
                if (!target) {
                    return err(ErrorCode::NotFound, "nle: clip not found for trim");
                }
                const auto& [in_f, out_f] = invert ? old_io : new_io;
                target->in_flicks = in_f;
                target->out_flicks = out_f;
                return {};
            } else if constexpr (std::is_same_v<T, std::tuple<uint64_t, uint64_t, int64_t, int64_t>>) {
                const auto& [kept_id, created_id, split_at, old_out] = op;
                auto* left = project.clip(kept_id);
                if (!left) {
                    return err(ErrorCode::NotFound, "nle: clip not found for split");
                }
                if (invert) {
                    std::erase_if(project.clips, [&](const TimelineClip& c) {
                        return c.id == created_id;
                    });
                    left->out_flicks = old_out;
                    if (project.selected_clip_id == created_id) {
                        project.selected_clip_id = kept_id;
                    }
                } else {
                    const int64_t offset = split_at - left->start_flicks;
                    const int64_t split_src = left->in_flicks + offset;
                    TimelineClip right = *left;
                    right.id = created_id;
                    right.start_flicks = split_at;
                    right.in_flicks = split_src;
                    right.out_flicks = old_out;
                    left->out_flicks = split_src;
                    project.clips.push_back(std::move(right));
                }
                return {};
            } else if constexpr (std::is_same_v<T, std::tuple<uint64_t, std::pair<int, int64_t>,
                                                              std::pair<int, int64_t>>>) {
                const auto& [clip_id, old_pos, new_pos] = op;
                auto* target = project.clip(clip_id);
                if (!target) {
                    return err(ErrorCode::NotFound, "nle: clip not found for move");
                }
                const auto& [lane, start] = invert ? old_pos : new_pos;
                target->lane = lane;
                target->start_flicks = start;
                return {};
            } else if constexpr (std::is_same_v<T, std::pair<std::array<int8_t, kNleTrackCount>,
                                                             std::array<int8_t, kNleTrackCount>>>) {
                const auto& [old_stack, new_stack] = op;
                project.lane_stack = invert ? old_stack : new_stack;
                return {};
            } else if constexpr (std::is_same_v<T, std::tuple<uint64_t, NleMediaAsset, NleMediaAsset>>) {
                const auto& [asset_id, prev_asset, verified_asset] = op;
                auto* target = project.asset(asset_id);
                if (!target) {
                    return err(ErrorCode::NotFound, "nle: asset not found for relink");
                }
                *target = invert ? prev_asset : verified_asset;
                return {};
            }
        },
        delta);
}

Result<int64_t> parseInt64Token(std::string_view token) {
    int64_t value = 0;
    const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec != std::errc{} || ptr != token.data() + token.size()) {
        return err(ErrorCode::InvalidArgument, "nle: invalid integer token in laneStack");
    }
    return value;
}

}  // namespace

int64_t NleProject::timelineDurationFlicks() const noexcept {
    int64_t max_end = 0;
    for (const auto& c : clips) {
        max_end = std::max(max_end, c.start_flicks + nleClipDuration(c));
    }
    return max_end;
}

std::string NleProject::toJson() const {
    std::string out;
    out.reserve(512 + assets.size() * 160 + clips.size() * 160);
    out += "{\n";
    out += "  \"version\": 1,\n";
    out += std::format("  \"nextId\": {},\n", next_id);
    out += std::format("  \"playheadFlicks\": {},\n", playhead_flicks);
    out += std::format("  \"markInFlicks\": {},\n", mark_in_flicks);
    out += std::format("  \"markOutFlicks\": {},\n", mark_out_flicks);
    out += std::format("  \"selectedClipId\": {},\n", selected_clip_id);

    out += "  \"laneStack\": [";
    for (std::size_t i = 0; i < lane_stack.size(); ++i) {
        if (i > 0) out += ", ";
        out += std::to_string(static_cast<int>(lane_stack[i]));
    }
    out += "],\n";

    out += "  \"assets\": [";
    for (std::size_t i = 0; i < assets.size(); ++i) {
        if (i > 0) out += ",";
        const auto& a = assets[i];
        out += "\n    {";
        out += std::format("\"id\": {}, ", a.id);
        out += "\"uuid\": " + rtx::json::jsonQuoted(a.uuid) + ", ";
        out += "\"name\": " + rtx::json::jsonQuoted(a.name) + ", ";
        out += "\"path\": " + rtx::json::jsonQuoted(a.path) + ", ";
        out += std::format("\"durationFlicks\": {}, ", a.duration_flicks);
        out += std::format("\"width\": {}, ", a.width);
        out += std::format("\"height\": {}, ", a.height);
        out += std::format("\"offline\": {}", a.offline ? "true" : "false");
        out += "}";
    }
    if (!assets.empty()) out += "\n  ";
    out += "],\n";

    out += "  \"clips\": [";
    for (std::size_t i = 0; i < clips.size(); ++i) {
        if (i > 0) out += ",";
        const auto& c = clips[i];
        out += "\n    {";
        out += std::format("\"id\": {}, ", c.id);
        out += std::format("\"assetId\": {}, ", c.asset_id);
        out += "\"name\": " + rtx::json::jsonQuoted(c.name) + ", ";
        out += std::format("\"lane\": {}, ", c.lane);
        out += std::format("\"startFlicks\": {}, ", c.start_flicks);
        out += std::format("\"inFlicks\": {}, ", c.in_flicks);
        out += std::format("\"outFlicks\": {}, ", c.out_flicks);
        out += std::format("\"gain\": {:.6f}", static_cast<double>(c.gain));
        out += "}";
    }
    if (!clips.empty()) out += "\n  ";
    out += "]\n";
    out += "}\n";
    return out;
}

Result<NleProject> NleProject::fromJson(std::string_view json_text) {
    std::size_t pos = 0;
    auto root = rtx::json::scanObject(json_text, pos);
    if (!root.has_value()) {
        return std::unexpected(root.error());
    }
    rtx::json::skipWs(json_text, pos);
    if (pos != json_text.size()) {
        return err(ErrorCode::InvalidArgument, "nle: trailing tokens after root JSON object");
    }

    NleProject project{};
    if (auto v = rtx::json::memberInt64(*root, "nextId", 1); v.has_value()) {
        if (*v <= 0) return err(ErrorCode::InvalidArgument, "nle: nextId must be positive");
        project.next_id = static_cast<uint64_t>(*v);
    } else {
        return std::unexpected(v.error());
    }
    if (auto v = rtx::json::memberInt64(*root, "playheadFlicks", 0); v.has_value()) {
        project.playhead_flicks = std::max<int64_t>(0, *v);
    } else {
        return std::unexpected(v.error());
    }
    if (auto v = rtx::json::memberInt64(*root, "markInFlicks", -1); v.has_value()) {
        project.mark_in_flicks = *v;
    } else {
        return std::unexpected(v.error());
    }
    if (auto v = rtx::json::memberInt64(*root, "markOutFlicks", -1); v.has_value()) {
        project.mark_out_flicks = *v;
    } else {
        return std::unexpected(v.error());
    }
    if (auto v = rtx::json::memberInt64(*root, "selectedClipId", 0); v.has_value()) {
        project.selected_clip_id = *v > 0 ? static_cast<uint64_t>(*v) : 0;
    } else {
        return std::unexpected(v.error());
    }

    if (rtx::json::findMember(*root, "laneStack") != nullptr) {
        auto arr = rtx::json::memberArray(*root, "laneStack");
        if (!arr.has_value()) return std::unexpected(arr.error());
        if (arr->size() != kNleTrackCount) {
            return err(ErrorCode::InvalidArgument, "nle: laneStack must have 4 elements");
        }
        std::array<int8_t, kNleTrackCount> parsed_stack{};
        for (std::size_t i = 0; i < kNleTrackCount; ++i) {
            auto val = parseInt64Token((*arr)[i]);
            if (!val.has_value()) return std::unexpected(val.error());
            parsed_stack[i] = static_cast<int8_t>(*val);
        }
        if (!validLaneStack(parsed_stack)) {
            return err(ErrorCode::InvalidArgument, "nle: laneStack must be a permutation of 0..3");
        }
        project.lane_stack = parsed_stack;
    }

    if (rtx::json::findMember(*root, "assets") != nullptr) {
        auto arr = rtx::json::memberArray(*root, "assets");
        if (!arr.has_value()) return std::unexpected(arr.error());
        project.assets.reserve(arr->size());
        for (std::string_view elem : *arr) {
            std::size_t epos = 0;
            auto obj = rtx::json::scanObject(elem, epos);
            if (!obj.has_value()) return std::unexpected(obj.error());

            NleMediaAsset a{};
            auto id = rtx::json::memberInt64(*obj, "id", 0);
            if (!id.has_value()) return std::unexpected(id.error());
            if (*id <= 0) return err(ErrorCode::InvalidArgument, "nle: asset id must be positive");
            a.id = static_cast<uint64_t>(*id);

            auto uuid = rtx::json::memberString(*obj, "uuid");
            if (!uuid.has_value()) return std::unexpected(uuid.error());
            a.uuid = std::move(*uuid);

            auto name = rtx::json::memberString(*obj, "name");
            if (!name.has_value()) return std::unexpected(name.error());
            a.name = std::move(*name);

            auto path = rtx::json::memberString(*obj, "path");
            if (!path.has_value()) return std::unexpected(path.error());
            a.path = std::move(*path);

            auto dur = rtx::json::memberInt64(*obj, "durationFlicks", 0);
            if (!dur.has_value()) return std::unexpected(dur.error());
            if (*dur < 0) return err(ErrorCode::InvalidArgument, "nle: asset duration must be >= 0");
            a.duration_flicks = *dur;

            auto w = rtx::json::memberInt(*obj, "width", 0);
            if (!w.has_value()) return std::unexpected(w.error());
            a.width = *w;

            auto h = rtx::json::memberInt(*obj, "height", 0);
            if (!h.has_value()) return std::unexpected(h.error());
            a.height = *h;

            auto offline = rtx::json::memberBool(*obj, "offline", false);
            if (!offline.has_value()) return std::unexpected(offline.error());
            a.offline = *offline;

            project.next_id = std::max(project.next_id, a.id + 1);
            project.assets.push_back(std::move(a));
        }
    }

    if (rtx::json::findMember(*root, "clips") != nullptr) {
        auto arr = rtx::json::memberArray(*root, "clips");
        if (!arr.has_value()) return std::unexpected(arr.error());
        project.clips.reserve(arr->size());
        for (std::string_view elem : *arr) {
            std::size_t epos = 0;
            auto obj = rtx::json::scanObject(elem, epos);
            if (!obj.has_value()) return std::unexpected(obj.error());

            TimelineClip c{};
            auto id = rtx::json::memberInt64(*obj, "id", 0);
            if (!id.has_value()) return std::unexpected(id.error());
            if (*id <= 0) return err(ErrorCode::InvalidArgument, "nle: clip id must be positive");
            c.id = static_cast<uint64_t>(*id);

            auto asset_id = rtx::json::memberInt64(*obj, "assetId", 0);
            if (!asset_id.has_value()) return std::unexpected(asset_id.error());
            if (*asset_id <= 0 || project.asset(static_cast<uint64_t>(*asset_id)) == nullptr) {
                return err(ErrorCode::InvalidArgument, "nle: clip references unknown assetId");
            }
            c.asset_id = static_cast<uint64_t>(*asset_id);

            auto name = rtx::json::memberString(*obj, "name");
            if (!name.has_value()) return std::unexpected(name.error());
            c.name = std::move(*name);

            auto lane = rtx::json::memberInt(*obj, "lane", 1);
            if (!lane.has_value()) return std::unexpected(lane.error());
            if (!validLane(*lane)) return err(ErrorCode::InvalidArgument, "nle: clip lane out of range");
            c.lane = *lane;

            auto start = rtx::json::memberInt64(*obj, "startFlicks", 0);
            if (!start.has_value()) return std::unexpected(start.error());
            if (*start < 0) return err(ErrorCode::InvalidArgument, "nle: clip startFlicks must be >= 0");
            c.start_flicks = *start;

            auto in_f = rtx::json::memberInt64(*obj, "inFlicks", 0);
            if (!in_f.has_value()) return std::unexpected(in_f.error());
            auto out_f = rtx::json::memberInt64(*obj, "outFlicks", 0);
            if (!out_f.has_value()) return std::unexpected(out_f.error());
            if (*in_f < 0 || *out_f < *in_f) {
                return err(ErrorCode::InvalidArgument, "nle: clip in/out range invalid");
            }
            c.in_flicks = *in_f;
            c.out_flicks = *out_f;

            auto gain = rtx::json::memberDouble(*obj, "gain", 1.0);
            if (!gain.has_value()) return std::unexpected(gain.error());
            if (!std::isfinite(*gain) || *gain < 0.0) {
                return err(ErrorCode::InvalidArgument, "nle: clip gain invalid");
            }
            c.gain = static_cast<float>(*gain);

            project.next_id = std::max(project.next_id, c.id + 1);
            project.clips.push_back(std::move(c));
        }
    }

    return project;
}

Result<uint64_t> nleRegisterAsset(NleProject& project, NleMediaAsset asset) {
    if (asset.path.empty()) {
        return err(ErrorCode::InvalidArgument, "nle: asset path must not be empty");
    }
    if (asset.duration_flicks < 0) {
        return err(ErrorCode::InvalidArgument, "nle: asset duration must be >= 0");
    }
    if (asset.id == 0) {
        asset.id = project.next_id++;
    } else {
        if (project.asset(asset.id) != nullptr) {
            return err(ErrorCode::InvalidArgument,
                       "nle: duplicate asset id " + std::to_string(asset.id));
        }
        project.next_id = std::max(project.next_id, asset.id + 1);
    }
    const uint64_t assigned_id = asset.id;
    project.assets.push_back(std::move(asset));
    return assigned_id;
}

Result<uint64_t> nleAddClip(NleProject& project, EditStacks& history, TimelineClip clip) {
    if (project.asset(clip.asset_id) == nullptr) {
        return err(ErrorCode::InvalidArgument, "nle: addClip references unregistered asset");
    }
    if (!validLane(clip.lane)) {
        return err(ErrorCode::InvalidArgument, "nle: addClip lane out of range");
    }
    if (clip.start_flicks < 0 || clip.in_flicks < 0 || clip.out_flicks <= clip.in_flicks) {
        return err(ErrorCode::InvalidArgument, "nle: addClip timing range invalid");
    }
    if (clip.id == 0) {
        clip.id = project.next_id++;
    } else {
        if (project.clip(clip.id) != nullptr) {
            return err(ErrorCode::InvalidArgument,
                       "nle: duplicate clip id " + std::to_string(clip.id));
        }
        project.next_id = std::max(project.next_id, clip.id + 1);
    }
    const uint64_t assigned_id = clip.id;
    NleEditDelta delta = std::pair<bool, TimelineClip>{false, clip};
    if (auto applied = applyDelta(project, delta, false); !applied.has_value()) {
        return std::unexpected(applied.error());
    }
    pushUndo(history, std::move(delta));
    return assigned_id;
}

Result<void> nleTrimClip(NleProject& project, EditStacks& history, uint64_t clip_id,
                         int64_t new_in_flicks, int64_t new_out_flicks) {
    auto* target = project.clip(clip_id);
    if (!target) {
        return err(ErrorCode::NotFound, "nle: trimClip target not found");
    }
    if (new_in_flicks < 0 || new_out_flicks <= new_in_flicks) {
        return err(ErrorCode::InvalidArgument, "nle: trimClip range invalid");
    }
    const auto* a = project.asset(target->asset_id);
    if (a != nullptr && a->duration_flicks > 0 && new_out_flicks > a->duration_flicks) {
        return err(ErrorCode::InvalidArgument, "nle: trimClip exceeds asset duration");
    }
    NleEditDelta delta = std::tuple<uint64_t, std::pair<int64_t, int64_t>, std::pair<int64_t, int64_t>>{
        clip_id, {target->in_flicks, target->out_flicks}, {new_in_flicks, new_out_flicks}};
    if (auto applied = applyDelta(project, delta, false); !applied.has_value()) {
        return applied;
    }
    pushUndo(history, std::move(delta));
    return {};
}

Result<uint64_t> nleSplitClip(NleProject& project, EditStacks& history, uint64_t clip_id,
                              int64_t split_timeline_flicks) {
    auto* target = project.clip(clip_id);
    if (!target) {
        return err(ErrorCode::NotFound, "nle: splitClip target not found");
    }
    const int64_t end_flicks = target->start_flicks + nleClipDuration(*target);
    if (split_timeline_flicks <= target->start_flicks || split_timeline_flicks >= end_flicks) {
        return err(ErrorCode::InvalidArgument, "nle: split point must lie strictly inside clip");
    }
    const uint64_t created_id = project.next_id++;
    NleEditDelta delta = std::tuple<uint64_t, uint64_t, int64_t, int64_t>{
        clip_id, created_id, split_timeline_flicks, target->out_flicks};
    if (auto applied = applyDelta(project, delta, false); !applied.has_value()) {
        return std::unexpected(applied.error());
    }
    pushUndo(history, std::move(delta));
    return created_id;
}

Result<void> nleMoveClip(NleProject& project, EditStacks& history, uint64_t clip_id,
                         int new_lane, int64_t new_start_flicks) {
    auto* target = project.clip(clip_id);
    if (!target) {
        return err(ErrorCode::NotFound, "nle: moveClip target not found");
    }
    if (!validLane(new_lane) || new_start_flicks < 0) {
        return err(ErrorCode::InvalidArgument, "nle: moveClip lane or start invalid");
    }
    NleEditDelta delta = std::tuple<uint64_t, std::pair<int, int64_t>, std::pair<int, int64_t>>{
        clip_id, {target->lane, target->start_flicks}, {new_lane, new_start_flicks}};
    if (auto applied = applyDelta(project, delta, false); !applied.has_value()) {
        return applied;
    }
    pushUndo(history, std::move(delta));
    return {};
}

Result<void> nleDeleteClip(NleProject& project, EditStacks& history, uint64_t clip_id) {
    auto* target = project.clip(clip_id);
    if (!target) {
        return err(ErrorCode::NotFound, "nle: deleteClip target not found");
    }
    NleEditDelta delta = std::pair<bool, TimelineClip>{true, *target};
    if (auto applied = applyDelta(project, delta, false); !applied.has_value()) {
        return applied;
    }
    pushUndo(history, std::move(delta));
    return {};
}

Result<void> nleReorderTracks(NleProject& project, EditStacks& history,
                              std::array<int8_t, kNleTrackCount> new_stack) {
    if (!validLaneStack(new_stack)) {
        return err(ErrorCode::InvalidArgument, "nle: reorderTracks requires permutation of 0..3");
    }
    NleEditDelta delta = std::pair<std::array<int8_t, kNleTrackCount>,
                                   std::array<int8_t, kNleTrackCount>>{project.lane_stack, new_stack};
    if (auto applied = applyDelta(project, delta, false); !applied.has_value()) {
        return applied;
    }
    pushUndo(history, std::move(delta));
    return {};
}

Result<void> nleRelinkAsset(NleProject& project, EditStacks& history, uint64_t asset_id,
                            std::string new_path, int64_t verified_duration_flicks,
                            int verified_width, int verified_height) {
    auto* target = project.asset(asset_id);
    if (!target) {
        return err(ErrorCode::NotFound, "nle: relinkAsset target not found");
    }
    if (new_path.empty()) {
        return err(ErrorCode::InvalidArgument, "nle: relinkAsset path must not be empty");
    }
    NleMediaAsset verified = *target;
    verified.path = std::move(new_path);
    if (verified_duration_flicks >= 0) {
        verified.duration_flicks = verified_duration_flicks;
    }
    if (verified_width > 0) verified.width = verified_width;
    if (verified_height > 0) verified.height = verified_height;
    verified.offline = false;

    const int64_t new_duration = verified.duration_flicks;
    NleEditDelta delta = std::tuple<uint64_t, NleMediaAsset, NleMediaAsset>{
        asset_id, *target, std::move(verified)};
    if (auto applied = applyDelta(project, delta, false); !applied.has_value()) {
        return applied;
    }
    pushUndo(history, std::move(delta));

    // 若新素材时长缩短，自动截断所有引用该素材且 out_flicks 越界的片段并压入可逆 Trim delta。
    if (verified_duration_flicks > 0) {
        for (const auto& c : project.clips) {
            if (c.asset_id != asset_id || c.out_flicks <= new_duration) continue;
            const int64_t clamped_in = std::min(c.in_flicks, std::max<int64_t>(0, new_duration - 1));
            const int64_t clamped_out = new_duration;
            if (clamped_out > clamped_in) {
                NleEditDelta trim_delta =
                    std::tuple<uint64_t, std::pair<int64_t, int64_t>, std::pair<int64_t, int64_t>>{
                        c.id, {c.in_flicks, c.out_flicks}, {clamped_in, clamped_out}};
                if (auto trimmed = applyDelta(project, trim_delta, false); trimmed.has_value()) {
                    pushUndo(history, std::move(trim_delta));
                }
            }
        }
    }
    return {};
}

Result<void> nleUndo(NleProject& project, EditStacks& history) {
    auto& [undo_stack, redo_stack] = history;
    if (undo_stack.empty()) {
        return err(ErrorCode::NotFound, "nle: undo stack is empty");
    }
    NleEditDelta delta = std::move(undo_stack.back());
    undo_stack.pop_back();
    if (auto applied = applyDelta(project, delta, true); !applied.has_value()) {
        undo_stack.push_back(std::move(delta));
        return applied;
    }
    redo_stack.push_back(std::move(delta));
    return {};
}

Result<void> nleRedo(NleProject& project, EditStacks& history) {
    auto& [undo_stack, redo_stack] = history;
    if (redo_stack.empty()) {
        return err(ErrorCode::NotFound, "nle: redo stack is empty");
    }
    NleEditDelta delta = std::move(redo_stack.back());
    redo_stack.pop_back();
    if (auto applied = applyDelta(project, delta, false); !applied.has_value()) {
        redo_stack.push_back(std::move(delta));
        return applied;
    }
    undo_stack.push_back(std::move(delta));
    return {};
}

}  // namespace rtx
