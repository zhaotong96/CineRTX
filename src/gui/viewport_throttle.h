// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Viewport scheduling contract (W9-2), header-only so the headless CLI can
// verify it without a D3D12 device or a window.
//
// The 60 Hz viewport is the only part of the engine that runs on the UI thread,
// so its interference budget (Gate 3.3: <= 2.0 % background throughput loss,
// 0 PCIe pixel copies) depends entirely on three decisions:
//
//   1. never dispatch a compute shader more often than 1/60 s;
//   2. dispatch nothing at all while the window is occluded or minimised;
//   3. clamp the split-screen cursor so the shader never samples outside the
//      bound SRVs.

#pragma once

#include <cstdint>

namespace rtx::gui {

enum class ViewportState : uint8_t { Idle = 0, Presenting = 1, Occluded = 2, Minimized = 3 };

struct ViewportThrottleConfig {
    // 60 Hz is the panel rate; a 144 Hz panel still gets 60 because the
    // background transcode matters more than a smoother preview.
    double target_hz = 60.0;
    bool bypass_when_occluded = true;
};

class ViewportThrottle {
public:
    explicit ViewportThrottle(ViewportThrottleConfig config = {}) : config_(config) {
        if (!(config_.target_hz > 0.0)) {
            config_.target_hz = 60.0;
        }
    }

    // `now_seconds` is a monotonic clock (QueryPerformanceCounter / steady_clock
    // converted to seconds).  Returns true when the caller must dispatch the
    // tone-map compute shader for this tick.
    bool shouldPresent(double now_seconds, bool occluded, bool minimized) {
        if (minimized) {
            state_ = ViewportState::Minimized;
            if (config_.bypass_when_occluded) {
                ++occlusion_bypasses_;
                return false;
            }
        } else if (occluded) {
            state_ = ViewportState::Occluded;
            if (config_.bypass_when_occluded) {
                ++occlusion_bypasses_;
                return false;
            }
        }

        const double interval = 1.0 / config_.target_hz;
        if (has_presented_ && now_seconds - last_present_seconds_ < interval) {
            ++skipped_;
            return false;
        }
        last_present_seconds_ = now_seconds;
        has_presented_ = true;
        state_ = ViewportState::Presenting;
        ++presented_;
        return true;
    }

    // Maps a client-space cursor x onto the split position in [0, 1].  A zero
    // or negative width yields 0.5 (centred) instead of dividing by zero.
    double splitForCursor(int cursor_x, int viewport_width) const {
        if (viewport_width <= 0) {
            return 0.5;
        }
        double split = static_cast<double>(cursor_x) / static_cast<double>(viewport_width);
        if (split < 0.0) {
            split = 0.0;
        } else if (split > 1.0) {
            split = 1.0;
        }
        return split;
    }

    ViewportState state() const noexcept { return state_; }
    uint64_t presented() const noexcept { return presented_; }
    uint64_t skipped() const noexcept { return skipped_; }
    uint64_t occlusionBypasses() const noexcept { return occlusion_bypasses_; }
    double targetHz() const noexcept { return config_.target_hz; }

    void reset() noexcept {
        state_ = ViewportState::Idle;
        presented_ = 0;
        skipped_ = 0;
        occlusion_bypasses_ = 0;
        last_present_seconds_ = 0.0;
        has_presented_ = false;
    }

private:
    ViewportThrottleConfig config_{};
    ViewportState state_ = ViewportState::Idle;
    uint64_t presented_ = 0;
    uint64_t skipped_ = 0;
    uint64_t occlusion_bypasses_ = 0;
    double last_present_seconds_ = 0.0;
    bool has_presented_ = false;
};

}  // namespace rtx::gui
