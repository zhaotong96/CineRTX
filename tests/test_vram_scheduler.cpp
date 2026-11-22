// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include "test_framework.h"

#include <cstdint>

#include "rtx/vram_scheduler.h"

using namespace rtx;

RTX_TEST(vram_scheduler_7300mib_watermarks) {
    VramScheduler sched;
    constexpr uint64_t kMiB = 1024ull * 1024ull;
    RTX_CHECK_EQ(sched.dxgiBudgetBytes(), 7300ull * kMiB);
    RTX_CHECK_EQ(sched.highWatermarkBytes(), 5986ull * kMiB);
    RTX_CHECK_EQ(sched.lowWatermarkBytes(), 4964ull * kMiB);
    RTX_CHECK_EQ(sched.currentTier(), PipelineTier::VsrFirst_RifeFull);
}

RTX_TEST(vram_scheduler_five_tier_degradation_and_120f_hysteresis) {
    constexpr uint64_t kMiB = 1024ull * 1024ull;
    VramScheduler sched(7300ull * kMiB, 120u);
    sched.setTrtPeakAllocationBytes(3200ull * kMiB);
    sched.setCodecRefPoolBytes(1200ull * kMiB);

    const PipelineTier expected[4] = {
        PipelineTier::VsrFirst_RifeHalf,
        PipelineTier::VsrFirst_Nvofa,
        PipelineTier::FgFirst_RifeFull,
        PipelineTier::PassThrough_NoFruc,
    };

    for (PipelineTier target : expected) {
        for (uint32_t f = 0; f < 119u; ++f) {
            RTX_CHECK(!sched.observeFrame(1100ull * kMiB, 1800ull * kMiB));
        }
        RTX_CHECK(sched.observeFrame(1100ull * kMiB, 1800ull * kMiB));
        RTX_CHECK_EQ(sched.currentTier(), target);
    }

    for (uint32_t f = 0; f < 125u; ++f) {
        RTX_CHECK(!sched.observeFrame(500ull * kMiB, 2500ull * kMiB));
    }
    RTX_CHECK_EQ(sched.currentTier(), PipelineTier::PassThrough_NoFruc);

    sched.setTrtPeakAllocationBytes(1000ull * kMiB);
    sched.setCodecRefPoolBytes(800ull * kMiB);
    for (uint32_t f = 0; f < 119u; ++f) {
        RTX_CHECK(!sched.observeFrame(4000ull * kMiB, 500ull * kMiB));
    }
    RTX_CHECK(sched.observeFrame(4000ull * kMiB, 500ull * kMiB));
    RTX_CHECK_EQ(sched.currentTier(), PipelineTier::FgFirst_RifeFull);
}
