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

#include "rtx/job_queue.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

RTX_TEST(test_encoder_job_queue_heterogeneous_dynamic_mutation) {
    std::mutex mtx;
    std::condition_variable cv;
    bool job1_started = false;
    bool allow_job1_finish = false;

    struct ExecutedJob {
        std::string input;
        rtx::TranscodeProfile profile;
    };
    std::vector<ExecutedJob> executed;
    std::mutex exec_mtx;

    rtx::EncoderJobQueue queue([&](const rtx::HwTranscodeOptions& opts) -> rtx::Result<rtx::HwTranscodeReport> {
        {
            std::lock_guard<std::mutex> lk(exec_mtx);
            rtx::TranscodeProfile p{};
            p.input = opts.input_path.string();
            p.output = opts.output_path.string();
            p.codec = opts.codec == rtx::VideoCodec::AV1 ? (opts.use_cpu_encoder ? "av1_cpu" : "av1") : (opts.use_cpu_encoder ? "hevc_cpu" : "hevc");
            p.rate_control = opts.rate_control == 3 ? "cq" : (opts.rate_control == 4 ? "crf" : "vbr");
            p.qp = opts.qp;
            p.pipeline_order = opts.vsr_before_fg ? "vsr-first" : "fg-first";
            executed.push_back({opts.input_path.string(), p});
        }

        if (opts.input_path.string().find("job1") != std::string::npos) {
            {
                std::lock_guard<std::mutex> lk(mtx);
                job1_started = true;
            }
            cv.notify_all();
            std::unique_lock<std::mutex> lk(mtx);
            cv.wait(lk, [&] { return allow_job1_finish; });
        }

        rtx::HwTranscodeReport r{};
        r.frames_encoded = 10;
        return r;
    });

    rtx::TranscodeProfile p1{};
    p1.input = "job1.mp4";
    p1.output = "job1_out.mp4";
    p1.codec = "av1";
    p1.rate_control = "cq";
    p1.qp = 20;
    p1.pipeline_order = "vsr-first";

    rtx::TranscodeProfile p2{};
    p2.input = "job2.mp4";
    p2.output = "job2_out.mp4";
    p2.codec = "av1_cpu";
    p2.rate_control = "crf";
    p2.qp = 24;
    p2.pipeline_order = "vsr-first";

    rtx::TranscodeProfile p3{};
    p3.input = "job3.mp4";
    p3.output = "job3_out.mp4";
    p3.codec = "hevc";
    p3.rate_control = "vbr";
    p3.qp = 26;
    p3.pipeline_order = "fg-first";

    auto j1_res = queue.enqueueJob({std::filesystem::path("job1.mp4")}, p1);
    RTX_CHECK(j1_res.has_value());
    auto j1_id = j1_res.value();
    (void)j1_id;

    auto j2_res = queue.enqueueJob({std::filesystem::path("job2.mp4")}, p2);
    RTX_CHECK(j2_res.has_value());
    auto j2_id = j2_res.value();

    auto j3_res = queue.enqueueJob({std::filesystem::path("job3.mp4")}, p3);
    RTX_CHECK(j3_res.has_value());
    auto j3_id = j3_res.value();

    queue.startWorker();

    // Wait until Job 1 is actively running
    {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [&] { return job1_started; });
    }

    // Dynamic queue mutations while Job 1 is running:
    // 1. Move Job 3 before Job 2
    auto m_res = queue.moveJob(j3_id, -1);
    RTX_CHECK(m_res.has_value());

    // 2. Remove Job 2
    auto r_res = queue.removeJob(j2_id);
    RTX_CHECK(r_res.has_value());

    // 3. Enqueue Job 4
    rtx::TranscodeProfile p4{};
    p4.input = "job4.mp4";
    p4.output = "job4_out.mp4";
    p4.codec = "hevc_cpu";
    p4.rate_control = "crf";
    p4.qp = 28;
    p4.pipeline_order = "vsr-first";
    auto j4_res = queue.enqueueJob({std::filesystem::path("job4.mp4")}, p4);
    RTX_CHECK(j4_res.has_value());

    // Unblock Job 1 to finish
    {
        std::lock_guard<std::mutex> lk(mtx);
        allow_job1_finish = true;
    }
    cv.notify_all();

    // Wait for queue to process remaining jobs and become idle
    while (queue.isWorkerRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    queue.stopQueue(true);

    // Verify executions and order
    std::lock_guard<std::mutex> lk(exec_mtx);
    RTX_CHECK_EQ(executed.size(), 3u);
    if (executed.size() == 3) {
        RTX_CHECK(executed[0].input.find("job1.mp4") != std::string::npos);
        RTX_CHECK_EQ(executed[0].profile.codec, "av1");
        RTX_CHECK_EQ(executed[0].profile.rate_control, "cq");
        RTX_CHECK_EQ(executed[0].profile.qp, 20);

        RTX_CHECK(executed[1].input.find("job3.mp4") != std::string::npos);
        RTX_CHECK_EQ(executed[1].profile.codec, "hevc");
        RTX_CHECK_EQ(executed[1].profile.rate_control, "vbr");
        RTX_CHECK_EQ(executed[1].profile.pipeline_order, "fg-first");

        RTX_CHECK(executed[2].input.find("job4.mp4") != std::string::npos);
        RTX_CHECK_EQ(executed[2].profile.codec, "hevc_cpu");
        RTX_CHECK_EQ(executed[2].profile.rate_control, "crf");
        RTX_CHECK_EQ(executed[2].profile.qp, 28);
    }
}
