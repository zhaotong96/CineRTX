// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/vram_scheduler.h"

#include <algorithm>
#include <cmath>

namespace rtx {

std::string_view pipelineTierName(PipelineTier tier) noexcept {
    switch (tier) {
        case PipelineTier::VsrFirst_RifeFull: return "VsrFirst_RifeFull";
        case PipelineTier::VsrFirst_RifeHalf: return "VsrFirst_RifeHalf";
        case PipelineTier::VsrFirst_Nvofa: return "VsrFirst_Nvofa";
        case PipelineTier::FgFirst_RifeFull: return "FgFirst_RifeFull";
        case PipelineTier::PassThrough_NoFruc: return "PassThrough_NoFruc";
    }
    return "PassThrough_NoFruc";
}

VramScheduler::VramScheduler(uint64_t dxgi_budget_bytes, uint32_t min_dwell_frames) noexcept
    : dxgi_budget_bytes_(dxgi_budget_bytes > 0 ? dxgi_budget_bytes : kDefaultDxgiVramBudgetBytes),
      min_dwell_frames_(min_dwell_frames > 0 ? min_dwell_frames : kVramMinDwellFrames) {}

void VramScheduler::setDxgiBudgetBytes(uint64_t budget_bytes) noexcept {
    if (budget_bytes > 0) {
        dxgi_budget_bytes_ = budget_bytes;
    }
}

void VramScheduler::setTrtPeakAllocationBytes(uint64_t trt_peak_bytes) noexcept {
    trt_peak_bytes_ = trt_peak_bytes;
}

void VramScheduler::setCodecRefPoolBytes(uint64_t codec_ref_bytes) noexcept {
    codec_ref_bytes_ = codec_ref_bytes;
}

uint64_t VramScheduler::highWatermarkBytes() const noexcept {
    return static_cast<uint64_t>(std::llround(static_cast<double>(dxgi_budget_bytes_) * kVramHighWatermarkRatio));
}

uint64_t VramScheduler::lowWatermarkBytes() const noexcept {
    return static_cast<uint64_t>(std::llround(static_cast<double>(dxgi_budget_bytes_) * kVramLowWatermarkRatio));
}

bool VramScheduler::observeFrame(uint64_t cuda_free_bytes, uint64_t working_set_bytes) noexcept {
    const uint64_t budget = dxgi_budget_bytes_ > 0 ? dxgi_budget_bytes_ : kDefaultDxgiVramBudgetBytes;
    const uint64_t modeled_used = trt_peak_bytes_ + codec_ref_bytes_ + working_set_bytes;
    uint64_t cuda_implied_used = 0;
    if (cuda_free_bytes > 0 && cuda_free_bytes < budget) {
        cuda_implied_used = budget - cuda_free_bytes;
    }
    const uint64_t effective_used = (std::max)(modeled_used, cuda_implied_used);
    last_pressure_ = static_cast<double>(effective_used) / static_cast<double>(budget);

    ++frames_in_tier_;

    if (last_pressure_ >= kVramHighWatermarkRatio) {
        ++consecutive_high_frames_;
        consecutive_low_frames_ = 0;
        if (consecutive_high_frames_ >= min_dwell_frames_ &&
            tier_ != PipelineTier::PassThrough_NoFruc) {
            tier_ = static_cast<PipelineTier>(static_cast<uint8_t>(tier_) + 1u);
            frames_in_tier_ = 0;
            consecutive_high_frames_ = 0;
            ++transitions_;
            return true;
        }
    } else if (last_pressure_ <= kVramLowWatermarkRatio) {
        ++consecutive_low_frames_;
        consecutive_high_frames_ = 0;
        if (consecutive_low_frames_ >= min_dwell_frames_ &&
            tier_ != PipelineTier::VsrFirst_RifeFull) {
            tier_ = static_cast<PipelineTier>(static_cast<uint8_t>(tier_) - 1u);
            frames_in_tier_ = 0;
            consecutive_low_frames_ = 0;
            ++transitions_;
            return true;
        }
    } else {
        consecutive_high_frames_ = 0;
        consecutive_low_frames_ = 0;
    }
    return false;
}

}  // namespace rtx
