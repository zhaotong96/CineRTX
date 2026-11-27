// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
#include "rtx/shot_segmenter.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace rtx {

ShotTransitionDecision evaluateShot(
    ShotGroupSegmenter& state,
    const std::array<uint32_t, 64>& block_sad,
    int width, int height, int max_sample_val) noexcept {
    auto& [shot_group_id, frames_in_shot, ema_motion] = state;
    if (width <= 0 || height <= 0 || max_sample_val <= 0) {
        return {false, false, false, shot_group_id, 0.0f, 0.0f};
    }
    uint64_t total_sad = 0;
    for (uint32_t s : block_sad) total_sad += s;
    const double total_norm_denom =
        static_cast<double>(width) * static_cast<double>(height) * static_cast<double>(max_sample_val);
    const float mu_k = static_cast<float>(static_cast<double>(total_sad) / total_norm_denom);

    const double block_w = std::max(1.0, static_cast<double>(width) / 8.0);
    const double block_h = std::max(1.0, static_cast<double>(height) / 8.0);
    const double block_denom = block_w * block_h * static_cast<double>(max_sample_val);

    int outlier_blocks = 0;
    for (uint32_t s : block_sad) {
        if (static_cast<float>(static_cast<double>(s) / block_denom) > 0.16f) ++outlier_blocks;
    }
    const float p_outlier = static_cast<float>(outlier_blocks) / 64.0f;
    const float baseline = std::max(ema_motion, 0.025f);
    const bool hard_cut =
        (mu_k > 0.22f) ||
        (mu_k > 0.085f && mu_k > 3.2f * baseline && p_outlier >= 0.30f) ||
        (p_outlier >= 0.375f && mu_k > 2.4f * ema_motion);

    if (hard_cut) {
        ++shot_group_id;
        frames_in_shot = 1;
        ema_motion = 0.03f;
        return {true, true, true, shot_group_id, mu_k, p_outlier};
    }
    ++frames_in_shot;
    constexpr float kAlpha = 2.0f / (8.0f + 1.0f);
    ema_motion = (1.0f - kAlpha) * ema_motion + kAlpha * mu_k;
    return {false, false, false, shot_group_id, mu_k, p_outlier};
}

ShotTransitionDecision evaluateShotTransition(
    ShotGroupSegmenter& state,
    uint64_t frame_index, const FrameDiffMetrics& metrics) noexcept {
    (void)frame_index;
    auto& [shot_group_id, frames_in_shot, ema_motion] = state;
    const auto [mu_k, max_block_sad, p_outlier, hist_dist] = metrics;
    (void)hist_dist;
    const float baseline = std::max(ema_motion, 0.025f);
    const bool hard_cut =
        (mu_k > 0.22f) ||
        (mu_k > 0.085f && mu_k > 3.2f * baseline) ||
        (p_outlier >= 0.375f && mu_k > 2.4f * ema_motion) ||
        (max_block_sad > 0.40f && p_outlier >= 0.25f);

    if (hard_cut) {
        ++shot_group_id;
        frames_in_shot = 1;
        ema_motion = 0.03f;
        return {true, true, true, shot_group_id, mu_k, p_outlier};
    }
    ++frames_in_shot;
    constexpr float kAlpha = 2.0f / (8.0f + 1.0f);
    ema_motion = (1.0f - kAlpha) * ema_motion + kAlpha * mu_k;
    return {false, false, false, shot_group_id, mu_k, p_outlier};
}

TemporalBoundaryType analyze_temporal_boundary(
    const uint8_t* prev_y, const uint8_t* curr_y,
    int width, int height, int pitch) noexcept {
    if (prev_y == nullptr || curr_y == nullptr || width < 8 || height < 8 || pitch < width) {
        return TemporalBoundaryType::None;
    }
    const int blocks_x = width / 8;
    const int blocks_y = height / 8;
    const int total_blocks = blocks_x * blocks_y;
    if (total_blocks <= 0) return TemporalBoundaryType::None;

    int changed_blocks = 0;
    for (int by = 0; by < blocks_y; ++by) {
        for (int bx = 0; bx < blocks_x; ++bx) {
            uint32_t block_sad = 0;
            for (int y = 0; y < 8; ++y) {
                const int row_off = (by * 8 + y) * pitch + (bx * 8);
                for (int x = 0; x < 8; ++x) {
                    block_sad += static_cast<uint32_t>(
                        std::abs(static_cast<int>(prev_y[row_off + x]) -
                                 static_cast<int>(curr_y[row_off + x])));
                }
            }
            if (block_sad > (8u * 8u * 25u)) {
                ++changed_blocks;
            }
        }
    }

    uint64_t sum_y = 0;
    uint64_t sum_sq_y = 0;
    const int64_t total_pixels = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    for (int i = 0; i < height; ++i) {
        const uint8_t* row = curr_y + static_cast<size_t>(i) * static_cast<size_t>(pitch);
        for (int j = 0; j < width; ++j) {
            const uint64_t val = row[j];
            sum_y += val;
            sum_sq_y += val * val;
        }
    }
    const double mean_y = static_cast<double>(sum_y) / static_cast<double>(total_pixels);
    const double var_y = (static_cast<double>(sum_sq_y) / static_cast<double>(total_pixels)) -
                         (mean_y * mean_y);
    if (mean_y < 15.0 && var_y < 10.0) {
        return TemporalBoundaryType::FadeToBlack;
    }
    const double block_change_ratio =
        static_cast<double>(changed_blocks) / static_cast<double>(total_blocks);
    if (block_change_ratio > 0.75) {
        return TemporalBoundaryType::HardCut;
    }
    if (block_change_ratio > 0.40) {
        return TemporalBoundaryType::HighMotion;
    }
    return TemporalBoundaryType::None;
}

}  // namespace rtx
