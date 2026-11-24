// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "rtx/vram_scheduler.h"

#include <algorithm>
#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace rtx {
namespace {

[[nodiscard]] bool mul128Ge(uint64_t a, uint64_t b, uint64_t c, uint64_t d) noexcept {
#if defined(_MSC_VER) && defined(_M_X64)
    uint64_t lhs_hi = 0, rhs_hi = 0;
    const uint64_t lhs_lo = _umul128(a, b, &lhs_hi);
    const uint64_t rhs_lo = _umul128(c, d, &rhs_hi);
    return (lhs_hi != rhs_hi) ? (lhs_hi > rhs_hi) : (lhs_lo >= rhs_lo);
#elif defined(__SIZEOF_INT128__)
    __extension__ using u128 = unsigned __int128;
    return static_cast<u128>(a) * static_cast<u128>(b) >=
           static_cast<u128>(c) * static_cast<u128>(d);
#else
    const uint64_t q = c / 100ull;
    const uint64_t r = c % 100ull;
    return a >= q * d + (r * d) / 100ull;
#endif
}

[[nodiscard]] bool mul128Le(uint64_t a, uint64_t b, uint64_t c, uint64_t d) noexcept {
    return mul128Ge(c, d, a, b);
}

}  // namespace

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

VramScheduler makeVramScheduler(uint64_t dxgi_budget_bytes, uint32_t min_dwell_frames) noexcept {
    return {dxgi_budget_bytes > 0 ? dxgi_budget_bytes : kDefaultDxgiVramBudgetBytes,
            0ull, 0ull,
            min_dwell_frames > 0 ? min_dwell_frames : kVramMinDwellFrames,
            0, 0u, 0.0, PipelineTier::VsrFirst_RifeFull};
}

void vramSetDxgiBudgetBytes(VramScheduler& s, uint64_t budget_bytes) noexcept {
    auto& [dxgi_budget, trt_peak, codec_ref, min_dwell, dwell, trans, pressure, tier] = s;
    (void)trt_peak; (void)codec_ref; (void)min_dwell; (void)dwell; (void)trans; (void)pressure; (void)tier;
    if (budget_bytes > 0) dxgi_budget = budget_bytes;
}

void vramSetTrtPeakAllocationBytes(VramScheduler& s, uint64_t trt_peak_bytes) noexcept {
    auto& [dxgi_budget, trt_peak, codec_ref, min_dwell, dwell, trans, pressure, tier] = s;
    (void)dxgi_budget; (void)codec_ref; (void)min_dwell; (void)dwell; (void)trans; (void)pressure; (void)tier;
    trt_peak = trt_peak_bytes;
}

void vramSetCodecRefPoolBytes(VramScheduler& s, uint64_t codec_ref_bytes) noexcept {
    auto& [dxgi_budget, trt_peak, codec_ref, min_dwell, dwell, trans, pressure, tier] = s;
    (void)dxgi_budget; (void)trt_peak; (void)min_dwell; (void)dwell; (void)trans; (void)pressure; (void)tier;
    codec_ref = codec_ref_bytes;
}

PipelineTier vramCurrentTier(const VramScheduler& s) noexcept {
    const auto& [b, t, c, m, d, tr, p, tier] = s;
    (void)b; (void)t; (void)c; (void)m; (void)d; (void)tr; (void)p;
    return tier;
}

uint64_t vramDxgiBudgetBytes(const VramScheduler& s) noexcept {
    const auto& [b, t, c, m, d, tr, p, tier] = s;
    (void)t; (void)c; (void)m; (void)d; (void)tr; (void)p; (void)tier;
    return b;
}

uint64_t vramHighWatermarkBytes(const VramScheduler& s) noexcept {
    const uint64_t b = vramDxgiBudgetBytes(s);
    return (b / 100ull) * kVramDegradePct + ((b % 100ull) * kVramDegradePct) / 100ull;
}

uint64_t vramLowWatermarkBytes(const VramScheduler& s) noexcept {
    const uint64_t b = vramDxgiBudgetBytes(s);
    return (b / 100ull) * kVramRecoverPct + ((b % 100ull) * kVramRecoverPct) / 100ull;
}

double vramLastPressureRatio(const VramScheduler& s) noexcept {
    const auto& [b, t, c, m, d, tr, p, tier] = s;
    (void)b; (void)t; (void)c; (void)m; (void)d; (void)tr; (void)tier;
    return p;
}

bool vramObserveFrame(VramScheduler& s, uint64_t cuda_free_bytes, uint64_t working_set_bytes) noexcept {
    auto& [dxgi_budget, trt_peak, codec_ref, min_dwell, dwell_frames, transitions, last_pressure, tier] = s;
    const uint64_t budget = dxgi_budget > 0 ? dxgi_budget : kDefaultDxgiVramBudgetBytes;
    const uint64_t modeled_used = trt_peak + codec_ref + working_set_bytes;
    const uint64_t cuda_implied_used =
        (cuda_free_bytes > 0 && cuda_free_bytes < budget) ? (budget - cuda_free_bytes) : 0ull;
    const uint64_t effective_used = (std::max)(modeled_used, cuda_implied_used);
    last_pressure = static_cast<double>(effective_used) / static_cast<double>(budget);

    const int32_t target_dwell = static_cast<int32_t>((std::min)(min_dwell, uint32_t{1'000'000u}));
    if (target_dwell <= 0) {
        dwell_frames = 0;
        return false;
    }
    if (mul128Ge(effective_used, 100ull, budget, kVramDegradePct)) {
        if (tier == PipelineTier::PassThrough_NoFruc) {
            dwell_frames = 0;
            return false;
        }
        dwell_frames = (dwell_frames > 0) ? (dwell_frames + 1) : 1;
        if (dwell_frames >= target_dwell) {
            tier = static_cast<PipelineTier>(static_cast<uint8_t>(tier) + 1u);
            dwell_frames = 0;
            ++transitions;
            return true;
        }
        return false;
    }
    if (mul128Le(effective_used, 100ull, budget, kVramRecoverPct)) {
        if (tier == PipelineTier::VsrFirst_RifeFull) {
            dwell_frames = 0;
            return false;
        }
        dwell_frames = (dwell_frames < 0) ? (dwell_frames - 1) : -1;
        if (dwell_frames <= -target_dwell) {
            tier = static_cast<PipelineTier>(static_cast<uint8_t>(tier) - 1u);
            dwell_frames = 0;
            ++transitions;
            return true;
        }
        return false;
    }
    dwell_frames = 0;
    return false;
}

}  // namespace rtx
