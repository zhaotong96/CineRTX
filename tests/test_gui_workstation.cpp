// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Stage 2 (Track B) Win32 workstation unit tests: the viewport-driven dynamic
// VSR scaling function and the Flicks timebase math (spec gate W32.4.1).

#include "test_framework.h"

#include <filesystem>
#include <string>

#include "workstation_canvas.h"

using rtx::gui::computeViewportVsrTarget;
using rtx::gui::formatFlicksTimecode;
using rtx::gui::msToFlicks;

RTX_TEST(gui_vsr_scale_2x) {
    // Spec reference: a 1920x1080 source in a 3840x2160 viewport scales 2.00x.
    const auto target = computeViewportVsrTarget(1920, 1080, 3840, 2160);
    RTX_CHECK(target.vsr_active);
    RTX_CHECK_EQ(target.scale, 2.0);
    RTX_CHECK_EQ(target.target_width, 3840);
    RTX_CHECK_EQ(target.target_height, 2160);
}

RTX_TEST(gui_vsr_scale_portrait_1_6875) {
    // Spec reference: a 720x1280 portrait source in a 3840x2160 viewport
    // scales 1.6875x -> 1216x2160.
    const auto target = computeViewportVsrTarget(720, 1280, 3840, 2160);
    RTX_CHECK(target.vsr_active);
    RTX_CHECK_EQ(target.scale, 1.6875);
    RTX_CHECK_EQ(target.target_width, 1216);
    RTX_CHECK_EQ(target.target_height, 2160);
}

RTX_TEST(gui_vsr_bypass_below_unity) {
    // Scale <= 1.0 must bypass VSR and keep the source geometry untouched.
    const auto down = computeViewportVsrTarget(3840, 2160, 1920, 1080);
    RTX_CHECK(!down.vsr_active);
    RTX_CHECK_EQ(down.scale, 0.5);
    RTX_CHECK_EQ(down.target_width, 3840);
    RTX_CHECK_EQ(down.target_height, 2160);

    const auto unity = computeViewportVsrTarget(1920, 1080, 1920, 1080);
    RTX_CHECK(!unity.vsr_active);
    RTX_CHECK_EQ(unity.scale, 1.0);
    RTX_CHECK_EQ(unity.target_width, 1920);
    RTX_CHECK_EQ(unity.target_height, 1080);
}

RTX_TEST(gui_vsr_degenerate_inputs) {
    const auto zero = computeViewportVsrTarget(0, 0, 1920, 1080);
    RTX_CHECK(!zero.vsr_active);
    const auto negative = computeViewportVsrTarget(1920, 1080, -1, 540);
    RTX_CHECK(!negative.vsr_active);
}

RTX_TEST(gui_vsr_even_alignment) {
    // Every recomputed target dimension must be even-aligned (NVENC + NGX
    // require 2-alignment).
    for (int src_w = 637; src_w < 700; ++src_w) {
        for (int src_h = 359; src_h < 400; ++src_h) {
            const auto target = computeViewportVsrTarget(src_w, src_h, 3840, 2160);
            if (target.vsr_active) {
                RTX_CHECK_EQ(target.target_width % 2, 0);
                RTX_CHECK_EQ(target.target_height % 2, 0);
            }
        }
    }
}

RTX_TEST(gui_flicks_timebase) {
    // WebUI parity: FLICKS_PER_MS = 705600; 1 s = 705,600,000 flicks.
    RTX_CHECK_EQ(msToFlicks(1), 705600LL);
    RTX_CHECK_EQ(msToFlicks(1000), 705600000LL);
    RTX_CHECK_EQ(msToFlicks(0), 0LL);
}

RTX_TEST(gui_format_vsr_scale) {
    // RW-3 fix: exact scale formatting — no %.2f truncation of 1.6875.
    RTX_CHECK(rtx::gui::formatVsrScale(1.6875f) == std::wstring(L"1.6875x"));
    RTX_CHECK(rtx::gui::formatVsrScale(2.0f) == std::wstring(L"2.00x"));
    RTX_CHECK(rtx::gui::formatVsrScale(0.64f) == std::wstring(L"0.64x"));
    RTX_CHECK(rtx::gui::formatVsrScale(1.0f) == std::wstring(L"1.00x"));
    RTX_CHECK(rtx::gui::formatVsrScale(0.5f) == std::wstring(L"0.50x"));
}

RTX_TEST(gui_probe_media_file) {
    // RW-2 fix: probe failures fall back to the canonical baseline geometry
    // and the kAssumedClipMs duration instead of lying about real dimensions.
    const auto missing = rtx::gui::probeMediaFile(
        std::filesystem::temp_directory_path() / "rtx_gui_probe_missing_media.mp4");
    RTX_CHECK(!missing.ok);
    RTX_CHECK_EQ(missing.width, 1920);
    RTX_CHECK_EQ(missing.height, 1080);
    RTX_CHECK_EQ(missing.duration_ms, rtx::gui::kAssumedClipMs);

    const auto empty = rtx::gui::probeMediaFile(std::filesystem::path{});
    RTX_CHECK(!empty.ok);
    RTX_CHECK_EQ(empty.width, 1920);
}

RTX_TEST(gui_active_vsr_target_state) {
    // RW-3 fix: the recomputed target is stored state, consumable by the
    // preview / quick-play pipelines.
    rtx::gui::DynamicVsrTarget probe{};
    probe.scale = 1.6875;
    probe.target_width = 1216;
    probe.target_height = 2160;
    probe.vsr_active = true;
    rtx::gui::storeActiveVsrTarget(probe);
    const auto& stored = rtx::gui::activeVsrTarget();
    RTX_CHECK_EQ(stored.scale, 1.6875);
    RTX_CHECK_EQ(stored.target_width, 1216);
    RTX_CHECK_EQ(stored.target_height, 2160);
    RTX_CHECK(stored.vsr_active);
}

RTX_TEST(gui_flicks_timecode_format) {
    const std::wstring zero = formatFlicksTimecode(0, 30000);
    RTX_CHECK(zero.find(L"SMPTE 00:00:00:00") != std::wstring::npos);
    RTX_CHECK(zero.find(L"0 Flicks") != std::wstring::npos);
    RTX_CHECK(zero.find(L"705,600,000 Hz") != std::wstring::npos);

    const std::wstring mid = formatFlicksTimecode(1500, 30000);
    // 1500 ms at 24 fps film base = 36 frames = 00:00:01:12.
    RTX_CHECK(mid.find(L"SMPTE 00:00:01:12") != std::wstring::npos);
    RTX_CHECK(mid.find(L"1058400000 Flicks") != std::wstring::npos);

    // Playhead clamps to the duration.
    const std::wstring past = formatFlicksTimecode(99999, 30000);
    RTX_CHECK(past.find(L"SMPTE 00:00:30:00") != std::wstring::npos);
}
