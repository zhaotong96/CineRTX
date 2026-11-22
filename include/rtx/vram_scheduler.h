// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 11.4: DXGI-budget-aware VRAM hysteresis scheduler (7,300 MiB baseline,
// 82% high watermark / 68% low watermark / 120-frame minimum dwell).

#pragma once

#include <cstdint>
#include <string_view>

namespace rtx {

enum class PipelineTier : uint8_t {
    VsrFirst_RifeFull = 0,
    VsrFirst_RifeHalf = 1,
    VsrFirst_Nvofa = 2,
    FgFirst_RifeFull = 3,
    PassThrough_NoFruc = 4,
};

inline constexpr uint64_t kDefaultDxgiVramBudgetBytes = 7300ull * 1024ull * 1024ull;
inline constexpr double kVramHighWatermarkRatio = 0.82;
inline constexpr double kVramLowWatermarkRatio = 0.68;
inline constexpr uint32_t kVramMinDwellFrames = 120u;

[[nodiscard]] std::string_view pipelineTierName(PipelineTier tier) noexcept;

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 维护 DXGI 显存预算、82%/68% 滞后双水位与 120 帧最小驻留计数器的自适应降级状态机，std 容器无法表达跨帧防抖与梯队单调迁移动力学。
// Invariant: dxgi_budget_bytes_ > 0; tier_ 在 [VsrFirst_RifeFull, PassThrough_NoFruc] 内单步迁移且仅在 frames_in_tier_ >= min_dwell_frames_ 时允许切换。
class VramScheduler {
public:
    explicit VramScheduler(uint64_t dxgi_budget_bytes = kDefaultDxgiVramBudgetBytes,
                           uint32_t min_dwell_frames = kVramMinDwellFrames) noexcept;

    void setDxgiBudgetBytes(uint64_t budget_bytes) noexcept;
    void setTrtPeakAllocationBytes(uint64_t trt_peak_bytes) noexcept;
    void setCodecRefPoolBytes(uint64_t codec_ref_bytes) noexcept;

    bool observeFrame(uint64_t cuda_free_bytes, uint64_t working_set_bytes) noexcept;

    [[nodiscard]] PipelineTier currentTier() const noexcept { return tier_; }
    [[nodiscard]] uint64_t dxgiBudgetBytes() const noexcept { return dxgi_budget_bytes_; }
    [[nodiscard]] uint64_t highWatermarkBytes() const noexcept;
    [[nodiscard]] uint64_t lowWatermarkBytes() const noexcept;
    [[nodiscard]] double lastPressureRatio() const noexcept { return last_pressure_; }
    [[nodiscard]] uint32_t framesInCurrentTier() const noexcept { return frames_in_tier_; }
    [[nodiscard]] uint64_t trtPeakAllocationBytes() const noexcept { return trt_peak_bytes_; }
    [[nodiscard]] uint32_t transitionsCount() const noexcept { return transitions_; }

private:
    uint64_t dxgi_budget_bytes_ = kDefaultDxgiVramBudgetBytes;
    uint64_t trt_peak_bytes_ = 0;
    uint64_t codec_ref_bytes_ = 0;
    uint32_t min_dwell_frames_ = kVramMinDwellFrames;
    uint32_t frames_in_tier_ = 0;
    uint32_t consecutive_high_frames_ = 0;
    uint32_t consecutive_low_frames_ = 0;
    uint32_t transitions_ = 0;
    double last_pressure_ = 0.0;
    PipelineTier tier_ = PipelineTier::VsrFirst_RifeFull;
};

}  // namespace rtx
