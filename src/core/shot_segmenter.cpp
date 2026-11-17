// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
#include "rtx/shot_segmenter.h"

#include <algorithm>
#include <cstdint>

namespace rtx {

ShotTransitionDecision ShotGroupSegmenter::evaluate(
    const std::array<uint32_t, 64>& block_sad,
    int width, int height, int max_sample_val) noexcept {
    ShotTransitionDecision decision{};
    if (width <= 0 || height <= 0 || max_sample_val <= 0) {
        decision.shot_group_id = shot_group_id_;
        return decision;
    }

    uint64_t total_sad = 0;
    for (uint32_t s : block_sad) {
        total_sad += s;
    }

    const double total_norm_denom =
        static_cast<double>(width) * static_cast<double>(height) * static_cast<double>(max_sample_val);
    const float mu_k = static_cast<float>(static_cast<double>(total_sad) / total_norm_denom);

    const double block_w = std::max(1.0, static_cast<double>(width) / 8.0);
    const double block_h = std::max(1.0, static_cast<double>(height) / 8.0);
    const double block_denom = block_w * block_h * static_cast<double>(max_sample_val);

    int outlier_blocks = 0;
    for (uint32_t s : block_sad) {
        const float m_b = static_cast<float>(static_cast<double>(s) / block_denom);
        if (m_b > 0.16f) {
            ++outlier_blocks;
        }
    }
    const float p_outlier = static_cast<float>(outlier_blocks) / 64.0f;

    const float baseline = std::max(ema_motion_, 0.025f);
    const bool hard_cut =
        (mu_k > 0.22f) ||
        (mu_k > 0.085f && mu_k > 3.2f * baseline && p_outlier >= 0.30f) ||
        (p_outlier >= 0.375f && mu_k > 2.4f * ema_motion_);

    decision.global_sad_norm = mu_k;
    decision.outlier_block_ratio = p_outlier;

    if (hard_cut) {
        ++shot_group_id_;
        frames_in_shot_ = 1;
        ema_motion_ = 0.03f;
        decision.is_scene_cut = true;
        decision.reset_vsr_temporal = true;
        decision.force_encoder_idr = true;
    } else {
        ++frames_in_shot_;
        constexpr float kAlpha = 2.0f / (8.0f + 1.0f);
        ema_motion_ = (1.0f - kAlpha) * ema_motion_ + kAlpha * mu_k;
    }

    decision.shot_group_id = shot_group_id_;
    return decision;
}

ShotTransitionDecision ShotGroupSegmenter::evaluateTransition(
    uint64_t frame_index, const FrameDiffMetrics& metrics) noexcept {
    (void)frame_index;
    ShotTransitionDecision decision{};
    const float mu_k = metrics.mean_sad;
    const float p_outlier = metrics.changed_block_ratio;
    const float baseline = std::max(ema_motion_, 0.025f);
    const bool hard_cut =
        (mu_k > 0.22f) ||
        (mu_k > 0.085f && mu_k > 3.2f * baseline) ||
        (p_outlier >= 0.375f && mu_k > 2.4f * ema_motion_) ||
        (metrics.max_block_sad > 0.40f && p_outlier >= 0.25f);

    decision.global_sad_norm = mu_k;
    decision.outlier_block_ratio = p_outlier;

    if (hard_cut) {
        ++shot_group_id_;
        frames_in_shot_ = 1;
        ema_motion_ = 0.03f;
        decision.is_scene_cut = true;
        decision.reset_vsr_temporal = true;
        decision.force_encoder_idr = true;
    } else {
        ++frames_in_shot_;
        constexpr float kAlpha = 2.0f / (8.0f + 1.0f);
        ema_motion_ = (1.0f - kAlpha) * ema_motion_ + kAlpha * mu_k;
    }

    decision.shot_group_id = shot_group_id_;
    return decision;
}

} // namespace rtx
