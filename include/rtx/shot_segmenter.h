// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
#pragma once

#include <array>
#include <cstdint>
#include <tuple>

namespace rtx {

// ShotTransitionDecision: (is_scene_cut, reset_vsr_temporal, force_encoder_idr,
//   shot_group_id, global_sad_norm, outlier_block_ratio)
using ShotTransitionDecision = std::tuple<bool, bool, bool, uint64_t, float, float>;

// FrameDiffMetrics: [mean_sad, max_block_sad, changed_block_ratio, hist_cosine_dist]
using FrameDiffMetrics = std::array<float, 4>;

// Phase 12 (Wave 12.2-C): tuple state (shot_group_id, frames_in_shot, ema_motion) + free functions.
using ShotGroupSegmenter = std::tuple<uint64_t, uint32_t, float>;

[[nodiscard]] constexpr ShotGroupSegmenter makeShotSegmenter() noexcept {
    return {0u, 0u, 0.03f};
}

constexpr void resetShotSegmenter(ShotGroupSegmenter& state) noexcept {
    state = makeShotSegmenter();
}

[[nodiscard]] ShotTransitionDecision evaluateShot(
    ShotGroupSegmenter& state,
    const std::array<uint32_t, 64>& block_sad,
    int width, int height, int max_sample_val = 255) noexcept;

[[nodiscard]] ShotTransitionDecision evaluateShotTransition(
    ShotGroupSegmenter& state,
    uint64_t frame_index,
    const FrameDiffMetrics& metrics) noexcept;

// Phase 13 (Wave B-A / 79_ Step 2.3): Stateless public scene-cut & fade-to-black temporal boundary bus.
enum class TemporalBoundaryType : uint8_t {
    None = 0,
    HardCut = 1,
    FadeToBlack = 2,
    HighMotion = 3,
};

[[nodiscard]] TemporalBoundaryType analyze_temporal_boundary(
    const uint8_t* prev_y, const uint8_t* curr_y,
    int width, int height, int pitch) noexcept;

}  // namespace rtx
