// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Two-slot even/odd ping-pong scheduling contract (W8-2).

#include "test_framework.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "pingpong_ring.h"

using namespace rtx_ngx;

RTX_TEST(pingpong_slot_parity) {
    PingPongRing ring;
    std::string error;
    RTX_REQUIRE(ring.initialize(PingPongEventMode::DisableTiming, &error));
    RTX_CHECK(ring.ready());
    RTX_CHECK(error.empty());

    // Even frames own slot 0, odd frames slot 1, forever.
    RTX_CHECK_EQ(PingPongRing::slotIndexFor(0), static_cast<size_t>(0));
    RTX_CHECK_EQ(PingPongRing::slotIndexFor(1), static_cast<size_t>(1));
    RTX_CHECK_EQ(PingPongRing::slotIndexFor(2), static_cast<size_t>(0));
    RTX_CHECK_EQ(PingPongRing::slotIndexFor(3), static_cast<size_t>(1));
    RTX_CHECK_EQ(PingPongRing::slotIndexFor(1024), static_cast<size_t>(0));
    RTX_CHECK_EQ(PingPongRing::slotIndexFor(1025), static_cast<size_t>(1));

    RTX_CHECK(&ring.slotFor(0) == &ring.slotFor(2));
    RTX_CHECK(&ring.slotFor(1) == &ring.slotFor(3));
    RTX_CHECK(&ring.slotFor(0) != &ring.slotFor(1));

    // The two slots must be distinct objects with independent bookkeeping.
    ring.completeSlot(0, 7);
    RTX_CHECK_EQ(ring.slotFor(0).d3d12_fence_val, static_cast<uint64_t>(7));
    RTX_CHECK_EQ(ring.slotFor(1).d3d12_fence_val, static_cast<uint64_t>(0));
    RTX_CHECK_EQ(ring.slotFor(0).frames_served, 1u);

    // A second initialise is rejected rather than silently resetting live state.
    RTX_CHECK(!ring.initialize(PingPongEventMode::DisableTiming, &error));
    ring.shutdown();
    RTX_CHECK(!ring.ready());
}

RTX_TEST(pingpong_event_disable_timing) {
    PingPongRing ring;
    RTX_REQUIRE(ring.initialize());
    // The default must be CU_EVENT_DISABLE_TIMING: a timing-enabled event costs
    // a GPU timestamp write on every signal, which is the phase-2 regression.
    RTX_CHECK(ring.eventTimingDisabled());
    RTX_CHECK(ring.eventMode() == PingPongEventMode::DisableTiming);
    ring.shutdown();

    // Tracing is the only path that may enable timing.
    PingPongRing traced;
    RTX_REQUIRE(traced.initialize(PingPongEventMode::Default));
    RTX_CHECK(!traced.eventTimingDisabled());
    RTX_CHECK(traced.eventMode() == PingPongEventMode::Default);
}

RTX_TEST(pingpong_scene_cut_duplicate) {
    PingPongRing ring;
    RTX_REQUIRE(ring.initialize());

    constexpr size_t kBytes = 64 * 48 * 3 / 2;
    std::vector<uint8_t> previous(kBytes, 0x3C);
    std::vector<uint8_t> current(kBytes, 0x00);

    RTX_CHECK(ring.duplicateFrame(previous.data(), current.data(), kBytes));
    RTX_CHECK(std::memcmp(current.data(), previous.data(), kBytes) == 0);
    RTX_CHECK_EQ(ring.duplicateCount(), static_cast<uint64_t>(1));
    RTX_CHECK_EQ(ring.sceneCutCount(), static_cast<uint64_t>(1));
    RTX_CHECK_EQ(ring.hostWaits(), static_cast<uint64_t>(0));

    // Invalid arguments are refused instead of copying from a null pointer.
    RTX_CHECK(!ring.duplicateFrame(nullptr, current.data(), kBytes));
    RTX_CHECK(!ring.duplicateFrame(previous.data(), nullptr, kBytes));
    RTX_CHECK(!ring.duplicateFrame(previous.data(), current.data(), 0));
    RTX_CHECK_EQ(ring.duplicateCount(), static_cast<uint64_t>(1));
}

RTX_TEST(pingpong_no_host_wait) {
    PingPongRing ring;
    RTX_REQUIRE(ring.initialize());

    // The first two frames have no predecessor on their parity.
    RTX_CHECK_EQ(ring.prepareSlot(0), static_cast<uint64_t>(0));
    RTX_CHECK_EQ(ring.prepareSlot(1), static_cast<uint64_t>(0));
    ring.completeSlot(0, 11);
    ring.completeSlot(1, 12);

    // Frame 2 reuses slot 0, so the GPU must wait for frame 0's value.  The
    // return value is a fence value to enqueue, not a blocking wait.
    RTX_CHECK_EQ(ring.prepareSlot(2), static_cast<uint64_t>(11));
    ring.completeSlot(2, 13);
    RTX_CHECK_EQ(ring.prepareSlot(3), static_cast<uint64_t>(12));
    ring.completeSlot(3, 14);
    RTX_CHECK_EQ(ring.prepareSlot(4), static_cast<uint64_t>(13));

    // Across the whole run the CPU never blocked on a cross-engine boundary.
    RTX_CHECK_EQ(ring.hostWaits(), static_cast<uint64_t>(0));
    RTX_CHECK_EQ(ring.submittedFrames(), static_cast<uint64_t>(4));
    RTX_CHECK_EQ(ring.slotFor(0).frames_served, 2u);
    RTX_CHECK_EQ(ring.slotFor(1).frames_served, 2u);

    // Negative frame indices are a caller bug; they must not index the array.
    RTX_CHECK_EQ(ring.prepareSlot(-1), static_cast<uint64_t>(0));
    ring.completeSlot(-1, 99);
    RTX_CHECK_EQ(ring.submittedFrames(), static_cast<uint64_t>(4));
}
