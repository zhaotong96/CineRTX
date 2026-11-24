// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <cstdint>

#include "rtx/vram_scheduler.h"

using namespace rtx;

RTX_TEST(vram_scheduler_7300mib_watermarks) {
    VramScheduler sched = makeVramScheduler();
    constexpr uint64_t kMiB = 1024ull * 1024ull;
    RTX_CHECK_EQ(vramDxgiBudgetBytes(sched), 7300ull * kMiB);
    RTX_CHECK_EQ(vramHighWatermarkBytes(sched), 5986ull * kMiB);
    RTX_CHECK_EQ(vramLowWatermarkBytes(sched), 4964ull * kMiB);
    RTX_CHECK_EQ(vramCurrentTier(sched), PipelineTier::VsrFirst_RifeFull);
}

RTX_TEST(vram_scheduler_five_tier_degradation_and_120f_hysteresis) {
    constexpr uint64_t kMiB = 1024ull * 1024ull;
    VramScheduler sched = makeVramScheduler(7300ull * kMiB, 120u);
    vramSetTrtPeakAllocationBytes(sched, 3200ull * kMiB);
    vramSetCodecRefPoolBytes(sched, 1200ull * kMiB);

    const PipelineTier expected[4] = {
        PipelineTier::VsrFirst_RifeHalf,
        PipelineTier::VsrFirst_Nvofa,
        PipelineTier::FgFirst_RifeFull,
        PipelineTier::PassThrough_NoFruc,
    };

    for (PipelineTier target : expected) {
        for (uint32_t f = 0; f < 119u; ++f) {
            RTX_CHECK(!vramObserveFrame(sched, 1100ull * kMiB, 1800ull * kMiB));
        }
        RTX_CHECK(vramObserveFrame(sched, 1100ull * kMiB, 1800ull * kMiB));
        RTX_CHECK_EQ(vramCurrentTier(sched), target);
    }

    for (uint32_t f = 0; f < 125u; ++f) {
        RTX_CHECK(!vramObserveFrame(sched, 500ull * kMiB, 2500ull * kMiB));
    }
    RTX_CHECK_EQ(vramCurrentTier(sched), PipelineTier::PassThrough_NoFruc);

    vramSetTrtPeakAllocationBytes(sched, 1000ull * kMiB);
    vramSetCodecRefPoolBytes(sched, 800ull * kMiB);
    for (uint32_t f = 0; f < 119u; ++f) {
        RTX_CHECK(!vramObserveFrame(sched, 4000ull * kMiB, 500ull * kMiB));
    }
    RTX_CHECK(vramObserveFrame(sched, 4000ull * kMiB, 500ull * kMiB));
    RTX_CHECK_EQ(vramCurrentTier(sched), PipelineTier::FgFirst_RifeFull);
}
