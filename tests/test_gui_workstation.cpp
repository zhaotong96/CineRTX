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

RTX_TEST(gui_vsr_scale_2x) {
    const auto [tw, th, sc, act] = computeViewportVsrTarget(1920, 1080, 3840, 2160);
    RTX_CHECK(act);
    RTX_CHECK_EQ(sc, 2.0);
    RTX_CHECK_EQ(tw, 3840);
    RTX_CHECK_EQ(th, 2160);
}
RTX_TEST(gui_vsr_scale_portrait_1_6875) {
    const auto [tw, th, sc, act] = computeViewportVsrTarget(720, 1280, 3840, 2160);
    RTX_CHECK(act);
    RTX_CHECK_EQ(sc, 1.6875);
    RTX_CHECK_EQ(tw, 1216);
    RTX_CHECK_EQ(th, 2160);
}
RTX_TEST(gui_vsr_bypass_below_unity) {
    const auto [dw, dh, dsc, dact] = computeViewportVsrTarget(3840, 2160, 1920, 1080);
    RTX_CHECK(!dact);
    RTX_CHECK_EQ(dsc, 0.5);
    RTX_CHECK_EQ(dw, 3840);
    RTX_CHECK_EQ(dh, 2160);

    const auto [uw, uh, usc, uact] = computeViewportVsrTarget(1920, 1080, 1920, 1080);
    RTX_CHECK(!uact);
    RTX_CHECK_EQ(usc, 1.0);
    RTX_CHECK_EQ(uw, 1920);
    RTX_CHECK_EQ(uh, 1080);
}
RTX_TEST(gui_vsr_degenerate_inputs) {
    const auto [zw, zh, zsc, zact] = computeViewportVsrTarget(0, 0, 1920, 1080);
    (void)zw; (void)zh; (void)zsc;
    RTX_CHECK(!zact);
    const auto [nw, nh, nsc, nact] = computeViewportVsrTarget(1920, 1080, -1, 540);
    (void)nw; (void)nh; (void)nsc;
    RTX_CHECK(!nact);
}
RTX_TEST(gui_vsr_even_alignment) {
    for (int src_w = 637; src_w < 700; ++src_w) {
        for (int src_h = 359; src_h < 400; ++src_h) {
            const auto [tw, th, sc, act] = computeViewportVsrTarget(src_w, src_h, 3840, 2160);
            (void)sc;
            if (act) {
                RTX_CHECK_EQ(tw % 2, 0);
                RTX_CHECK_EQ(th % 2, 0);
            }
        }
    }
}
RTX_TEST(gui_flicks_timebase) {
    using namespace std::chrono;
    RTX_CHECK_EQ(duration_cast<rtx::Flicks>(milliseconds(1)).count(), 706305600LL);
    RTX_CHECK_EQ(duration_cast<rtx::Flicks>(milliseconds(1000)).count(), 706305600000LL);
    RTX_CHECK_EQ(duration_cast<rtx::Flicks>(milliseconds(0)).count(), 0LL);
}
RTX_TEST(gui_format_vsr_scale) {
    RTX_CHECK(rtx::gui::formatVsrScale(1.6875f) == std::wstring(L"1.6875x"));
    RTX_CHECK(rtx::gui::formatVsrScale(2.0f) == std::wstring(L"2.00x"));
    RTX_CHECK(rtx::gui::formatVsrScale(0.64f) == std::wstring(L"0.64x"));
    RTX_CHECK(rtx::gui::formatVsrScale(1.0f) == std::wstring(L"1.00x"));
    RTX_CHECK(rtx::gui::formatVsrScale(0.5f) == std::wstring(L"0.50x"));
}
RTX_TEST(gui_probe_media_file) {
    const auto [mw, mh, mdur, mok] = rtx::gui::probeMediaFile(
        std::filesystem::temp_directory_path() / "rtx_gui_probe_missing_media.mp4");
    RTX_CHECK(!mok);
    RTX_CHECK_EQ(mw, 1920);
    RTX_CHECK_EQ(mh, 1080);
    RTX_CHECK_EQ(mdur, rtx::gui::kDefaultTimelineSpan.count());

    const auto [ew, eh, edur, eok] = rtx::gui::probeMediaFile(std::filesystem::path{});
    (void)eh; (void)edur;
    RTX_CHECK(!eok);
    RTX_CHECK_EQ(ew, 1920);
}
RTX_TEST(gui_active_vsr_target_state) {
    rtx::gui::storeActiveVsrTarget({1216, 2160, 1.6875, true});
    const auto& [tw, th, sc, act] = rtx::gui::activeVsrTarget();
    RTX_CHECK_EQ(sc, 1.6875);
    RTX_CHECK_EQ(tw, 1216);
    RTX_CHECK_EQ(th, 2160);
    RTX_CHECK(act);
}
RTX_TEST(gui_flicks_timecode_format) {
    using namespace std::chrono;
    const int64_t dur_flicks = duration_cast<rtx::Flicks>(seconds(30)).count();

    const std::wstring zero = formatFlicksTimecode(0, dur_flicks);
    RTX_CHECK(zero.find(L"SMPTE 00:00:00:00") != std::wstring::npos);
    RTX_CHECK(zero.find(L"0 Flicks") != std::wstring::npos);
    RTX_CHECK(zero.find(L"706,305,600,000 Hz") != std::wstring::npos);

    const int64_t mid_flicks = duration_cast<rtx::Flicks>(milliseconds(1500)).count();
    const std::wstring mid = formatFlicksTimecode(mid_flicks, dur_flicks);
    RTX_CHECK(mid.find(L"SMPTE 00:00:01:12") != std::wstring::npos);
    RTX_CHECK(mid.find(L"1059458400000 Flicks") != std::wstring::npos);

    const int64_t past_flicks = duration_cast<rtx::Flicks>(milliseconds(99999)).count();
    const std::wstring past = formatFlicksTimecode(past_flicks, dur_flicks);
    RTX_CHECK(past.find(L"SMPTE 00:00:30:00") != std::wstring::npos);
}
#include "rtx/job_queue.h"
#include <atomic>
#include <condition_variable>
// 1500 ms at 24 fps film base = 36 frames = 00:00:01:12.
#include <mutex>
#include <vector>

// Playhead clamps to the duration.
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
            p.codec = (opts.codec == rtx::VideoCodec::AV1) ? "av1" : "hevc";
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
    p2.codec = "av1";
    p2.rate_control = "cq";
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

    {
        std::unique_lock<std::mutex> lk(mtx);
        cv.wait(lk, [&] { return job1_started; });
    }
    auto m_res = queue.moveJob(j3_id, -1);
    RTX_CHECK(m_res.has_value());

    auto r_res = queue.removeJob(j2_id);
    RTX_CHECK(r_res.has_value());

    rtx::TranscodeProfile p4{};
    p4.input = "job4.mp4";
// Wait until Job 1 is actively running
    p4.output = "job4_out.mp4";
    p4.codec = "hevc";
    p4.rate_control = "cq";
    p4.qp = 28;
    p4.pipeline_order = "vsr-first";
// Dynamic queue mutations while Job 1 is running:
// 1. Move Job 3 before Job 2
    auto j4_res = queue.enqueueJob({std::filesystem::path("job4.mp4")}, p4);
    RTX_CHECK(j4_res.has_value());

// 2. Remove Job 2
    {
        std::lock_guard<std::mutex> lk(mtx);
        allow_job1_finish = true;
// 3. Enqueue Job 4
    }
    cv.notify_all();

    while (queue.isWorkerRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    queue.stopQueue(true);

    std::lock_guard<std::mutex> lk(exec_mtx);
    RTX_CHECK_EQ(executed.size(), 3u);
// Unblock Job 1 to finish
    if (executed.size() == 3) {
        RTX_CHECK(executed[0].input.find("job1.mp4") != std::string::npos);
        RTX_CHECK_EQ(executed[0].profile.codec, "av1");
        RTX_CHECK_EQ(executed[0].profile.rate_control, "cq");
        RTX_CHECK_EQ(executed[0].profile.qp, 20);

// Wait for queue to process remaining jobs and become idle
        RTX_CHECK(executed[1].input.find("job3.mp4") != std::string::npos);
        RTX_CHECK_EQ(executed[1].profile.codec, "hevc");
        RTX_CHECK_EQ(executed[1].profile.rate_control, "vbr");
        RTX_CHECK_EQ(executed[1].profile.pipeline_order, "fg-first");

// Verify executions and order
        RTX_CHECK(executed[2].input.find("job4.mp4") != std::string::npos);
        RTX_CHECK_EQ(executed[2].profile.codec, "hevc");
        RTX_CHECK_EQ(executed[2].profile.rate_control, "cq");
        RTX_CHECK_EQ(executed[2].profile.qp, 28);
    }
}

#if defined(_WIN32)
#include "preview_viewport.h"
#include "rtx/string_utils.h"
#include <array>
#include <chrono>
#include <d3d12.h>
#include <memory>
#include <thread>

RTX_TEST(gui_preview_viewport_composition_swapchain_live) {
    HMODULE d3d12_mod = LoadLibraryW(L"d3d12.dll");
    if (!d3d12_mod) return;
    using D3D12CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    auto create_dev = reinterpret_cast<D3D12CreateDeviceFn>(
        GetProcAddress(d3d12_mod, "D3D12CreateDevice"));
    if (!create_dev) { FreeLibrary(d3d12_mod); return; }
    ID3D12Device* raw_dev = nullptr;
    if (FAILED(create_dev(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                          reinterpret_cast<void**>(&raw_dev))) || !raw_dev) {
        FreeLibrary(d3d12_mod);
        return;
    }
    rtx::ComPtr<ID3D12Device> device(raw_dev);
    HWND host = CreateWindowExW(0, L"STATIC", L"CineRTXViewportTestHost",
                                WS_OVERLAPPEDWINDOW, 0, 0, 320, 180,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    RTX_REQUIRE(host != nullptr);
    {
        auto vp_res = rtx::gui::PreviewViewport::create(host, 320, 180);
        RTX_REQUIRE(vp_res.has_value());
        D3D12_HEAP_PROPERTIES heap_props{};
        heap_props.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC buf_desc{};
        buf_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        buf_desc.Width = 256ull * 256ull * 4ull;
        buf_desc.Height = 1;
        buf_desc.DepthOrArraySize = 1;
        buf_desc.MipLevels = 1;
        buf_desc.Format = DXGI_FORMAT_UNKNOWN;
        buf_desc.SampleDesc.Count = 1;
        buf_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ID3D12Resource* raw_buf = nullptr;
        RTX_REQUIRE(SUCCEEDED(device->CreateCommittedResource(
            &heap_props, D3D12_HEAP_FLAG_NONE, &buf_desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
            reinterpret_cast<void**>(&raw_buf))));
        rtx::ComPtr<ID3D12Resource> dummy_frame(raw_buf);
        auto flags = std::make_shared<std::array<std::atomic<std::uint32_t>, 3>>();
        (*flags)[0].store(1, std::memory_order_release);
        vp_res->presentShared(device.get(), dummy_frame.get(), 256u * 4u, 256u, 256u,
                              false, 100.0f, rtx::gui::PreviewSlotTicket{flags, 0u});
        RTX_CHECK(!vp_res->deviceLost());
        RTX_CHECK(!vp_res->lastPresentSkipped());
        RTX_CHECK(vp_res->waitForGpuIdle(2000).has_value());
        RTX_CHECK_EQ((*flags)[0].load(std::memory_order_acquire), 0u);
    }
    DestroyWindow(host);
    FreeLibrary(d3d12_mod);
}

RTX_TEST(gui_job_queue_enqueue_and_worker_real_transcode) {
    wchar_t exe_buf[1024]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe_buf, static_cast<DWORD>(std::size(exe_buf)));
    std::filesystem::path fixture =
        (n > 0) ? (std::filesystem::path(exe_buf).parent_path() / "fixtures" / "h264_25fps_gop12.mkv")
                : std::filesystem::path("fixtures/h264_25fps_gop12.mkv");
    if (!std::filesystem::exists(fixture)) return;
    const std::filesystem::path out_file =
        std::filesystem::temp_directory_path() / "cinertx_p13r2_e2e_out.mp4";
    std::error_code ec;
    std::filesystem::remove(out_file, ec);
    rtx::EncoderJobQueue queue;
    rtx::TranscodeProfile profile{};
    profile.input = rtx::str::pathToUtf8(fixture);
    profile.output = rtx::str::pathToUtf8(out_file);
    profile.auto_output = false;
    profile.codec = "av1";
    profile.rate_control = "cq";
    profile.qp = 26;
    profile.preset = 5;
    profile.fps_mode = "original";
    profile.copy_audio = false;
    profile.frames = 10;
    auto enq = queue.enqueueJob({fixture}, profile, out_file.parent_path());
    RTX_REQUIRE(enq.has_value());
    queue.startWorker();
    for (int i = 0; i < 600 && queue.isWorkerRunning(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    queue.stopQueue(true);
    auto job = queue.findJob(enq.value());
    RTX_REQUIRE(job.has_value());
    RTX_CHECK(rtx::encoderJobStatus(*job) == rtx::JobStatus::Completed);
    RTX_CHECK(std::filesystem::exists(out_file));
    RTX_CHECK(std::filesystem::file_size(out_file, ec) > 0u);
    std::filesystem::remove(out_file, ec);
}
#endif

RTX_TEST(gui_aspect_fit_draw_rect_and_dual_color_flags) {
    // Doc 44 Step 1.2: Exact aspect-fit drawRect verification across 16:9, 9:16, 21:9
    const auto r1 = rtx::gui::computeAspectFitDrawRect(1000, 700, 1920, 1080);
    RTX_CHECK_EQ(r1[0], 0);
    RTX_CHECK_EQ(r1[1], 68);
    RTX_CHECK_EQ(r1[2], 1000);
    RTX_CHECK_EQ(r1[3], 563);

    const auto r2 = rtx::gui::computeAspectFitDrawRect(1000, 700, 720, 1280);
    RTX_CHECK_EQ(r2[0], 303);
    RTX_CHECK_EQ(r2[1], 0);
    RTX_CHECK_EQ(r2[2], 394);
    RTX_CHECK_EQ(r2[3], 700);

    const auto r3 = rtx::gui::computeAspectFitDrawRect(960, 540, 2560, 1080);
    RTX_CHECK_EQ(r3[0], 0);
    RTX_CHECK_EQ(r3[1], 67);
    RTX_CHECK_EQ(r3[2], 960);
    RTX_CHECK_EQ(r3[3], 405);

    RTX_CHECK_EQ(rtx::gui::kPreviewLeftPq, 1u);
    RTX_CHECK_EQ(rtx::gui::kPreviewRightPq | rtx::gui::kPreviewRightBt2020, 40u);
}

RTX_TEST(gui_transcode_preview_dynamic_resolution_and_hdr10_flags) {
    // Doc 48 Surgery 1: Dynamic preview dimensions (zero 960x540 hardcoding)
    const auto [w4k, h4k] = rtx::gui::derivePreviewDimensions(15360ull * 2160ull, 15360u, 3840u, 2160u);
    RTX_CHECK_EQ(w4k, 3840u);
    RTX_CHECK_EQ(h4k, 2160u);
    const auto [w_port, h_port] = rtx::gui::derivePreviewDimensions(3072ull * 1280ull, 3072u, 720u, 1280u);
    RTX_CHECK_EQ(w_port, 720u);
    RTX_CHECK_EQ(h_port, 1280u);
    const auto [w_uw, h_uw] = rtx::gui::derivePreviewDimensions(10240ull * 1080ull, 10240u, 2560u, 1080u);
    RTX_CHECK_EQ(w_uw, 2560u);
    RTX_CHECK_EQ(h_uw, 1080u);
    const auto [w_bad, h_bad] = rtx::gui::derivePreviewDimensions(0ull, 0u, 0u, 0u);
    RTX_CHECK_EQ(w_bad, 0u);
    RTX_CHECK_EQ(h_bad, 0u);

    // Doc 48 Surgery 2: HDR10 (PQ) input -> dual-side PQ + BT.2020 flags & 1000 nits
    rtx::MediaInfo hdr10_info{};
    hdr10_info.color = {rtx::ColorPrimaries::BT2020, rtx::TransferFunction::SMPTE2084,
                        rtx::MatrixCoefficients::BT2020NC, rtx::ColorRange::Limited};
    hdr10_info.hdr.cll = {1000u, 400u};
    rtx::TranscodeProfile hdr_prof{};
    hdr_prof.peak_nits = 1000;
    const auto [f_hdr, l_hdr, r_hdr] = rtx::gui::computeTranscodePreviewColorParams(hdr10_info, hdr_prof, 1000.0f);
    RTX_CHECK((f_hdr & rtx::gui::kPreviewLeftPq) != 0u);
    RTX_CHECK((f_hdr & rtx::gui::kPreviewLeftBt2020) != 0u);
    RTX_CHECK((f_hdr & rtx::gui::kPreviewRightPq) != 0u);
    RTX_CHECK((f_hdr & rtx::gui::kPreviewRightBt2020) != 0u);
    RTX_CHECK_EQ(static_cast<int>(l_hdr), 1000);
    RTX_CHECK_EQ(static_cast<int>(r_hdr), 1000);

    // SDR input + TrueHDR (600 nits) output -> left SDR (100 nits), right PQ+BT.2020 (600 nits)
    rtx::MediaInfo sdr_info{};
    sdr_info.color = {rtx::ColorPrimaries::BT709, rtx::TransferFunction::BT709,
                      rtx::MatrixCoefficients::BT709, rtx::ColorRange::Limited};
    rtx::TranscodeProfile truehdr_prof{};
    truehdr_prof.peak_nits = 600;
    const auto [f_th, l_th, r_th] = rtx::gui::computeTranscodePreviewColorParams(sdr_info, truehdr_prof, 600.0f);
    RTX_CHECK((f_th & rtx::gui::kPreviewLeftPq) == 0u);
    RTX_CHECK((f_th & rtx::gui::kPreviewLeftHlg) == 0u);
    RTX_CHECK((f_th & rtx::gui::kPreviewRightPq) != 0u);
    RTX_CHECK((f_th & rtx::gui::kPreviewRightBt2020) != 0u);
    RTX_CHECK_EQ(static_cast<int>(l_th), 100);
    RTX_CHECK_EQ(static_cast<int>(r_th), 600);

    // HLG input -> left HLG + BT.2020 (1000 nits)
    rtx::MediaInfo hlg_info{};
    hlg_info.color = {rtx::ColorPrimaries::BT2020, rtx::TransferFunction::HLG,
                      rtx::MatrixCoefficients::BT2020NC, rtx::ColorRange::Limited};
    const auto [f_hlg, l_hlg, r_hlg] = rtx::gui::computeTranscodePreviewColorParams(hlg_info, hdr_prof, 1000.0f);
    RTX_CHECK((f_hlg & rtx::gui::kPreviewLeftHlg) != 0u);
    RTX_CHECK((f_hlg & rtx::gui::kPreviewLeftBt2020) != 0u);
    RTX_CHECK_EQ(static_cast<int>(l_hlg), 1000);
    RTX_CHECK_EQ(static_cast<int>(r_hlg), 1000);
}

RTX_TEST(gui_drop_then_switch_120rife_queue_isolation) {
    // Doc 59 Case 4: Drop-to-queue immediate Pending + activeSyncIds targeted 120-rife sync + multi-job isolation.
    RTX_CHECK_EQ(rtx::gui::fpsModeStringFromIndex(0), std::string_view("original"));
    RTX_CHECK_EQ(rtx::gui::fpsModeStringFromIndex(1), std::string_view("60-nvofa"));
    RTX_CHECK_EQ(rtx::gui::fpsModeStringFromIndex(2), std::string_view("120-nvofa"));
    RTX_CHECK_EQ(rtx::gui::fpsModeStringFromIndex(3), std::string_view("60-rife"));
    RTX_CHECK_EQ(rtx::gui::fpsModeStringFromIndex(4), std::string_view("120-rife"));
    RTX_CHECK_EQ(rtx::gui::fpsModeIndexFromString("120-rife"), 4);
    RTX_CHECK_EQ(rtx::gui::fpsModeIndexFromString("unknown"), 1);

    rtx::EncoderJobQueue queue;
    rtx::TranscodeProfile default_prof{};
    default_prof.fps_mode = "60-nvofa";
    auto idA = queue.enqueueJob({std::filesystem::path("clipA.mp4")}, default_prof);
    RTX_REQUIRE(idA.has_value());
    std::unordered_set<uint64_t> activeSyncIds = {idA.value()};

    rtx::TranscodeProfile rife120_prof = default_prof;
    rife120_prof.fps_mode = std::string(rtx::gui::fpsModeStringFromIndex(4));
    RTX_REQUIRE(rtx::validateAndNormalizeProfile(rife120_prof, false).has_value());
    for (uint64_t id : activeSyncIds) {
        RTX_REQUIRE(queue.updatePendingJob(id, rife120_prof).has_value());
    }
    const rtx::HwTranscodeOptions optsA = rtx::profileToHwOptions(rife120_prof);
    RTX_CHECK(optsA.fruc_enabled);
    RTX_CHECK_EQ(optsA.target_fps.num, 120);
    RTX_CHECK_EQ(optsA.target_fps.den, 1);
    RTX_CHECK(!optsA.rife_engine_path.empty());

    auto idB = queue.enqueueJob({std::filesystem::path("clipB.mp4")}, default_prof);
    RTX_REQUIRE(idB.has_value());
    auto jobA = queue.findJob(idA.value());
    auto jobB = queue.findJob(idB.value());
    RTX_REQUIRE(jobA.has_value() && jobB.has_value());
    RTX_CHECK_EQ(std::get<5>(*jobA).fps_mode, std::string("120-rife"));
    RTX_CHECK_EQ(std::get<5>(*jobB).fps_mode, std::string("60-nvofa"));
}

RTX_TEST(test_merge_concat_homogeneity_classification) {
    const std::filesystem::path fix_dir = std::filesystem::current_path() / "fixtures";
    const std::filesystem::path h264 = fix_dir / "h264_25fps_gop12.mkv";
    const std::filesystem::path hevc = fix_dir / "hevc_25fps_gop12.mkv";
    if (!std::filesystem::exists(h264) || !std::filesystem::exists(hevc)) {
        const std::vector<std::filesystem::path> single_clip = {h264};
        RTX_CHECK(rtx::areConcatInputsHomogeneous(single_clip));
        return;
    }
    const std::vector<std::filesystem::path> homogeneous = {h264, h264};
    RTX_CHECK(rtx::areConcatInputsHomogeneous(homogeneous));

    const std::vector<std::filesystem::path> heterogeneous = {h264, hevc};
    RTX_CHECK(!rtx::areConcatInputsHomogeneous(heterogeneous));
}

RTX_TEST(playback_fractional_60fps_phase_and_wasapi_meltdown) {
    // 23.976fps -> 60.000fps rational phase progression (0..5 ticks at 60Hz)
    const auto p0 = rtx::computeFractionalFramePhase(0, 0, 0, 0);
    RTX_CHECK_EQ(std::get<0>(p0), 0);
    RTX_CHECK(!std::get<3>(p0));
    RTX_CHECK(std::get<4>(p0) == rtx::PlaybackSyncState::Normal);

    const auto p1 = rtx::computeFractionalFramePhase(1, 16667, 16667, 0);
    RTX_CHECK_EQ(std::get<0>(p1), 0);
    RTX_CHECK(std::get<2>(p1) > 0.39f && std::get<2>(p1) < 0.41f);
    RTX_CHECK(std::get<3>(p1));
    RTX_CHECK(std::get<4>(p1) == rtx::PlaybackSyncState::Normal);

    // WASAPI 8ms drift warning threshold (>8000 us, <=32000 us)
    const auto p_drift = rtx::computeFractionalFramePhase(2, 33333, 45000, 1);
    RTX_CHECK(std::get<4>(p_drift) == rtx::PlaybackSyncState::AudioDrift);
    RTX_CHECK(std::get<3>(p_drift));

    // WASAPI 32ms meltdown bypass (>32000 us): skip RIFE interpolation
    const auto p_melt = rtx::computeFractionalFramePhase(3, 50000, 90000, 2);
    RTX_CHECK_EQ(std::get<0>(p_melt), 2);
    RTX_CHECK(std::get<2>(p_melt) == 0.0f);
    RTX_CHECK(!std::get<3>(p_melt));
    RTX_CHECK(std::get<4>(p_melt) == rtx::PlaybackSyncState::MeltdownBypass);
}

RTX_TEST(dag_contract_validation_and_auto_tonemap) {
    using rtx::gui::DagColorContract;
    using rtx::gui::DagNodeContract;
    const DagNodeContract nvdec{1u, "NVDEC", DagColorContract::SdrBt709_8Bit, DagColorContract::SdrBt709_8Bit};
    const DagNodeContract truehdr{2u, "TrueHDR", DagColorContract::SdrBt709_8Bit, DagColorContract::HdrBt2020Pq_10Bit};
    const DagNodeContract vsr_sdr{3u, "VSR_SDR", DagColorContract::SdrBt709_8Bit, DagColorContract::SdrBt709_8Bit};

    const auto ok_edge = rtx::gui::validateDagEdge(nvdec, truehdr);
    RTX_CHECK(std::get<0>(ok_edge));
    RTX_CHECK(!std::get<1>(ok_edge));

    const auto hdr_to_sdr = rtx::gui::validateDagEdge(truehdr, vsr_sdr);
    RTX_CHECK(!std::get<0>(hdr_to_sdr));
    RTX_CHECK(std::get<1>(hdr_to_sdr));
    RTX_CHECK_EQ(std::string(std::get<2>(hdr_to_sdr)), std::string("VSR_SDR"));

    RTX_CHECK(rtx::gui::hitTestRect(10.0f, 20.0f, 100.0f, 40.0f, 50.0f, 35.0f));
    RTX_CHECK(!rtx::gui::hitTestRect(10.0f, 20.0f, 100.0f, 40.0f, 5.0f, 35.0f));
}

RTX_TEST(i18n_vformat_and_relayout_flag) {
    const rtx::Language orig = rtx::currentLanguage();
    const rtx::Language toggled = (orig == rtx::Language::Zh) ? rtx::Language::En : rtx::Language::Zh;
    rtx::setLanguage(toggled);
    RTX_CHECK(rtx::currentLanguage() == toggled);
    const std::string title = rtx::formatI18n(rtx::StrId::AppTitle);
    RTX_CHECK(!title.empty());
    rtx::setLanguage(orig);
    RTX_CHECK(rtx::currentLanguage() == orig);
}
