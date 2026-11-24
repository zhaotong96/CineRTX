// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 12 (HYG-P12-03 / Wave 12.2-C): Pure integer-arithmetic VRAM hysteresis scheduler
// (7,300 MiB baseline, 82% degrade / 68% recover, signed int32_t dwell_frames).

#pragma once

#include <cstdint>
#include <string_view>
#include <tuple>

namespace rtx {

enum class PipelineTier : uint8_t {
    VsrFirst_RifeFull = 0,
    VsrFirst_RifeHalf = 1,
    VsrFirst_Nvofa = 2,
    FgFirst_RifeFull = 3,
    PassThrough_NoFruc = 4,
};

inline constexpr uint64_t kDefaultDxgiVramBudgetBytes = 7300ull * 1024ull * 1024ull;
inline constexpr uint64_t kVramDegradePct = 82ull;
inline constexpr uint64_t kVramRecoverPct = 68ull;
inline constexpr uint32_t kVramMinDwellFrames = 120u;

[[nodiscard]] std::string_view pipelineTierName(PipelineTier tier) noexcept;

// Phase 12 (Wave 12.2-C): tuple state (dxgi_budget, trt_peak, codec_ref, min_dwell,
//   dwell_frames, transitions, last_pressure, tier) + free functions.
using VramScheduler = std::tuple<uint64_t, uint64_t, uint64_t, uint32_t, int32_t, uint32_t, double, PipelineTier>;

[[nodiscard]] VramScheduler makeVramScheduler(
    uint64_t dxgi_budget_bytes = kDefaultDxgiVramBudgetBytes,
    uint32_t min_dwell_frames = kVramMinDwellFrames) noexcept;

void vramSetDxgiBudgetBytes(VramScheduler& s, uint64_t budget_bytes) noexcept;
void vramSetTrtPeakAllocationBytes(VramScheduler& s, uint64_t trt_peak_bytes) noexcept;
void vramSetCodecRefPoolBytes(VramScheduler& s, uint64_t codec_ref_bytes) noexcept;
bool vramObserveFrame(VramScheduler& s, uint64_t cuda_free_bytes, uint64_t working_set_bytes) noexcept;

[[nodiscard]] PipelineTier vramCurrentTier(const VramScheduler& s) noexcept;
[[nodiscard]] uint64_t vramDxgiBudgetBytes(const VramScheduler& s) noexcept;
[[nodiscard]] uint64_t vramHighWatermarkBytes(const VramScheduler& s) noexcept;
[[nodiscard]] uint64_t vramLowWatermarkBytes(const VramScheduler& s) noexcept;
[[nodiscard]] double vramLastPressureRatio(const VramScheduler& s) noexcept;

}  // namespace rtx
